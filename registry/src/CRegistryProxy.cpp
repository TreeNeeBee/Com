/**
 * @file        CRegistryProxy.cpp
 * @author      LightAP Development Team
 * @brief       Registry client implementation (IPC-based v2.0)
 * @date        2026/02/06
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/05  <td>1.0      <td>LightAP Team    <td>Initial implementation
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style optimization per code_rules.md
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CRegistryProxy.hpp"
#include "CServiceRegistry.hpp"
#include "ComTypes.hpp"

// ==================== Standard Library Headers ====================
#include <chrono>
#include <cstring>
#include <unistd.h>

namespace lap
{
namespace com
{
namespace registry
{
    using namespace std::chrono;
    using lap::com::MakeErrorCode;
    using lap::com::ComErrc;

    // IPC channel paths
    static constexpr const char* kRegistryRequestChannel  = "/lap_registry_req";
    static constexpr const char* kRegistryResponseChannel = "/lap_registry_resp";

    CRegistryProxy::CRegistryProxy() noexcept
        : m_pQmRegistry( nullptr )
        , m_pAsilRegistry( nullptr )
        , m_iNextRequestId( 1 )
        , m_bRunning( false )
        , m_iTotalRequests( 0 )
        , m_iSuccessfulRequests( 0 )
        , m_iFailedRequests( 0 )
        , m_iTimeoutRequests( 0 )
    {
    }

    CRegistryProxy::~CRegistryProxy() noexcept
    {
        // Stop response listener thread
        m_bRunning.store( false, std::memory_order_release );

        if ( m_responseListenerThread.joinable() )
        {
            if ( m_responseSubscriber.has_value() )
            {
                m_responseSubscriber->Disconnect();
            }
            m_responseListenerThread.join();
        }
    }

    Result< void > CRegistryProxy::Initialize() noexcept
    {
        // Step 1: Initialize QM and ASIL registries (READ-ONLY shared memory)
        m_pQmRegistry = std::make_unique< CServiceRegistry > ( RegistryType::kQM );
        m_pAsilRegistry = std::make_unique< CServiceRegistry > ( RegistryType::kASIL );

        auto qmResult = m_pQmRegistry->Initialize();
        if ( !qmResult.HasValue() )
        {
            LAP_COM_LOG_ERROR << "CRegistryProxy: Failed to initialize QM registry";
            return qmResult;
        }

        auto asilResult = m_pAsilRegistry->Initialize();
        if ( !asilResult.HasValue() )
        {
            LAP_COM_LOG_ERROR << "CRegistryProxy: Failed to initialize ASIL registry";
            return asilResult;
        }

        LAP_COM_LOG_INFO << "CRegistryProxy: Initialized read-only registries";

        // Step 2: Create IPC Publisher for sending requests (MPSC)
        lap::core::ipc::PublisherConfig pubConfig;
        pubConfig.max_chunks  = 16;
        pubConfig.chunk_size  = sizeof( RegistryRequest );
        pubConfig.loan_policy = lap::core::ipc::LoanPolicy::kWait;
        pubConfig.policy      = lap::core::ipc::PublishPolicy::kOverwrite;
        pubConfig.ipc_type    = lap::core::ipc::IPCType::kMPSC;  // Must match request SHM type

        auto pubResult = lap::core::ipc::Publisher::Create( kRegistryRequestChannel, pubConfig );
        if ( !pubResult.HasValue() )
        {
            LAP_COM_LOG_ERROR << "CRegistryProxy: Failed to create request publisher: "
                              << pubResult.Error().Message();
            return Result< void >::FromError( pubResult.Error() );
        }
        m_requestPublisher.emplace( std::move( pubResult ).Value() );

        LAP_COM_LOG_INFO << "CRegistryProxy: Created request publisher on "
                         << kRegistryRequestChannel;

        // Step 3: Create IPC Subscriber for receiving responses (SPMC)
        lap::core::ipc::SubscriberConfig subConfig;
        subConfig.channel_capacity = 256;
        subConfig.STmin = 0;  // No throttling
        subConfig.empty_policy = lap::core::ipc::SubscribePolicy::kBlock;

        auto subResult = lap::core::ipc::Subscriber::Create( kRegistryResponseChannel, subConfig );
        if ( !subResult.HasValue() )
        {
            LAP_COM_LOG_ERROR << "CRegistryProxy: Failed to create response subscriber: "
                              << subResult.Error().Message();
            return Result< void >::FromError( subResult.Error() );
        }
        m_responseSubscriber.emplace( std::move( subResult ).Value() );

        LAP_COM_LOG_INFO << "CRegistryProxy: Created response subscriber on "
                         << kRegistryResponseChannel;

        // Step 4: Connect subscriber
        auto connectResult = m_responseSubscriber->Connect();
        if ( !connectResult.HasValue() )
        {
            LAP_COM_LOG_ERROR << "CRegistryProxy: Failed to connect response subscriber";
            return connectResult;
        }

        // Step 5: Start background response listener thread
        m_bRunning.store( true, std::memory_order_release );
        m_responseListenerThread = std::thread( [this]() {
            responseListenerLoop();
        } );

        LAP_COM_LOG_INFO << "CRegistryProxy: Initialization complete";
        return Result< void >::FromValue();
    }

    Result< UInt32 > CRegistryProxy::RegisterService(
        UInt64 serviceId,
        UInt64 instanceId,
        UInt32 majorVersion,
        UInt32 minorVersion,
        const char* bindingType,
        const char* endpoint,
        UInt32 timeoutMs ) noexcept
    {
        m_iTotalRequests.fetch_add( 1, std::memory_order_relaxed );

        // Generate unique request ID
        UInt64 reqId = generateRequestId();

        // Create request message
        RegistryRequest request = RegistryRequest::CreateRegisterRequest(
            reqId, serviceId, instanceId, majorVersion, minorVersion,
            bindingType, endpoint
        );

        LAP_COM_LOG_DEBUG << "CRegistryProxy: RegisterService request #" << reqId
                          << " service_id=0x" << serviceId;

        // Send request and wait for response
        auto responseResult = sendRequestAndWait( request, timeoutMs );
        if ( !responseResult.HasValue() )
        {
            m_iFailedRequests.fetch_add( 1, std::memory_order_relaxed );
            return Result< UInt32 >::FromError( responseResult.Error() );
        }

        const RegistryResponse& response = responseResult.Value();
        if ( !response.IsSuccess() )
        {
            m_iFailedRequests.fetch_add( 1, std::memory_order_relaxed );
            LAP_COM_LOG_ERROR << "CRegistryProxy: RegisterService failed: "
                              << response.m_errorMessage;
            return Result< UInt32 >::FromError(
                MakeErrorCode( ComErrc::kServiceNotOffered, 0 )
            );
        }

        m_iSuccessfulRequests.fetch_add( 1, std::memory_order_relaxed );
        LAP_COM_LOG_DEBUG << "CRegistryProxy: RegisterService success, slot="
                          << response.m_assignedSlotIndex;
        return Result< UInt32 >::FromValue( response.m_assignedSlotIndex );
    }

    Result< void > CRegistryProxy::UnregisterService( UInt64 serviceId, UInt32 timeoutMs ) noexcept
    {
        m_iTotalRequests.fetch_add( 1, std::memory_order_relaxed );

        UInt64 reqId = generateRequestId();
        RegistryRequest request = RegistryRequest::CreateUnregisterRequest( reqId, serviceId );

        LAP_COM_LOG_DEBUG << "CRegistryProxy: UnregisterService request #" << reqId
                          << " service_id=0x" << serviceId;

        auto responseResult = sendRequestAndWait( request, timeoutMs );
        if ( !responseResult.HasValue() )
        {
            m_iFailedRequests.fetch_add( 1, std::memory_order_relaxed );
            return Result< void >::FromError( responseResult.Error() );
        }

        const RegistryResponse& response = responseResult.Value();
        if ( !response.IsSuccess() )
        {
            m_iFailedRequests.fetch_add( 1, std::memory_order_relaxed );
            LAP_COM_LOG_ERROR << "CRegistryProxy: UnregisterService failed: "
                              << response.m_errorMessage;
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kServiceNotOffered, 0 )
            );
        }

        m_iSuccessfulRequests.fetch_add( 1, std::memory_order_relaxed );
        return Result< void >::FromValue();
    }

    Optional< ServiceSlot > CRegistryProxy::FindService( UInt64 serviceId ) const noexcept
    {
        // Local read-only access to shared memory (no IPC overhead)
        UInt16 sid = static_cast< UInt16 > ( serviceId & 0xFFFF );

        Optional< ServiceSlot > localResult;

        if ( sid >= RegistryConfig::kAsilServiceIdMin &&
             sid <= RegistryConfig::kAsilServiceIdMax )
        {
            localResult = m_pAsilRegistry->FindService( serviceId );
        }
        else
        {
            localResult = m_pQmRegistry->FindService( serviceId );
        }

        // If found locally, return immediately
        if ( localResult && localResult->IsActive() )
        {
            return localResult;
        }

        // Fallback: query via IPC (for cases where local memfd is not shared)
        return const_cast< CRegistryProxy* > ( this )->QueryService( serviceId, 5000 );
    }

    Optional< ServiceSlot > CRegistryProxy::QueryService( UInt64 serviceId, UInt32 timeoutMs ) noexcept
    {
        m_iTotalRequests.fetch_add( 1, std::memory_order_relaxed );

        UInt64 reqId = generateRequestId();
        RegistryRequest request = RegistryRequest::CreateQueryRequest( reqId, serviceId );

        LAP_COM_LOG_DEBUG << "CRegistryProxy: QueryService request #" << reqId
                          << " service_id=0x" << serviceId;

        auto responseResult = sendRequestAndWait( request, timeoutMs );
        if ( !responseResult.HasValue() )
        {
            m_iFailedRequests.fetch_add( 1, std::memory_order_relaxed );
            return {};
        }

        const RegistryResponse& response = responseResult.Value();
        if ( !response.IsSuccess() )
        {
            m_iFailedRequests.fetch_add( 1, std::memory_order_relaxed );
            return {};
        }

        m_iSuccessfulRequests.fetch_add( 1, std::memory_order_relaxed );

        // Construct ServiceSlot from response
        ServiceSlot slot;
        slot.m_serviceId    = response.m_serviceId;
        slot.m_instanceId   = static_cast< UInt64 > ( response.m_instanceId );
        slot.m_status.store( static_cast< UInt32 > ( SlotStatus::kActive ),
                             std::memory_order_release );

        // Endpoint is stored in m_errorMessage field for query responses
        std::strncpy( slot.m_endpoint, response.m_errorMessage,
                      sizeof( slot.m_endpoint ) - 1 );
        slot.m_endpoint[sizeof( slot.m_endpoint ) - 1] = '\0';

        LAP_COM_LOG_DEBUG << "CRegistryProxy: QueryService success, endpoint="
                          << slot.m_endpoint;

        return Optional< ServiceSlot > ( slot );
    }

    Result< void > CRegistryProxy::UpdateHeartbeat( UInt64 serviceId, UInt64 timestampNs ) noexcept
    {
        // Fire-and-forget: Send request without waiting for response
        UInt64 reqId = generateRequestId();
        RegistryRequest request = RegistryRequest::CreateHeartbeatRequest(
            reqId, serviceId, timestampNs
        );

        // Send via IPC (best-effort delivery)
        auto sendFn = [&request]( lap::core::UInt8 /*channelId*/,
                                  lap::core::Byte* buf,
                                  lap::core::Size capacity ) -> lap::core::Size
        {
            if ( capacity < sizeof( RegistryRequest ) )
            {
                return 0;
            }
            std::memcpy( buf, &request, sizeof( RegistryRequest ) );
            return sizeof( RegistryRequest );
        };

        auto result = m_requestPublisher->Send( sendFn, lap::core::ipc::PublishPolicy::kOverwrite );
        if ( !result.HasValue() )
        {
            return Result< void >::FromError( result.Error() );
        }

        return Result< void >::FromValue();
    }

    CRegistryProxy::Statistics CRegistryProxy::GetStatistics() const noexcept
    {
        return Statistics{
            m_iTotalRequests.load( std::memory_order_relaxed ),
            m_iSuccessfulRequests.load( std::memory_order_relaxed ),
            m_iFailedRequests.load( std::memory_order_relaxed ),
            m_iTimeoutRequests.load( std::memory_order_relaxed )
        };
    }

    Result< RegistryResponse > CRegistryProxy::sendRequestAndWait(
        const RegistryRequest& request,
        UInt32 timeoutMs ) noexcept
    {
        // Send request via IPC Publisher
        auto sendFn = [&request]( lap::core::UInt8 /*channelId*/,
                                  lap::core::Byte* buf,
                                  lap::core::Size capacity ) -> lap::core::Size
        {
            if ( capacity < sizeof( RegistryRequest ) )
            {
                return 0;
            }
            std::memcpy( buf, &request, sizeof( RegistryRequest ) );
            return sizeof( RegistryRequest );
        };

        auto sendResult = m_requestPublisher->Send( sendFn, lap::core::ipc::PublishPolicy::kOverwrite );
        if ( !sendResult.HasValue() )
        {
            LAP_COM_LOG_ERROR << "CRegistryProxy: Failed to send request: "
                              << sendResult.Error().Message();
            return Result< RegistryResponse >::FromError( sendResult.Error() );
        }

        // Wait for response (with timeout)
        std::unique_lock< std::mutex > lock( m_pendingResponsesMutex );

        Bool responseReceived = m_responseCv.wait_for(
            lock,
            std::chrono::milliseconds( timeoutMs ),
            [this, reqId = request.m_requestId]() {
                return m_mapPendingResponses.count( reqId ) > 0;
            }
        );

        if ( !responseReceived )
        {
            m_iTimeoutRequests.fetch_add( 1, std::memory_order_relaxed );
            LAP_COM_LOG_WARN << "CRegistryProxy: Request #" << request.m_requestId
                             << " timeout after " << timeoutMs << "ms";
            return Result< RegistryResponse >::FromError(
                MakeErrorCode( ComErrc::kCommunicationFailure, 0 )  // Timeout
            );
        }

        // Extract response
        RegistryResponse response = m_mapPendingResponses[request.m_requestId];
        m_mapPendingResponses.erase( request.m_requestId );

        return Result< RegistryResponse >::FromValue( response );
    }

    void CRegistryProxy::responseListenerLoop() noexcept
    {
        LAP_COM_LOG_INFO << "CRegistryProxy: Response listener thread started";

        while ( m_bRunning.load( std::memory_order_acquire ) )
        {
            auto receiveFn = [this]( lap::core::UInt8 /*channelId*/,
                                     lap::core::Byte* data,
                                     lap::core::Size size ) -> lap::core::Size
            {
                if ( size < sizeof( RegistryResponse ) )
                {
                    return 0;
                }

                const RegistryResponse* pResponse =
                    reinterpret_cast< const RegistryResponse* > ( data );

                LAP_COM_LOG_DEBUG << "CRegistryProxy: Received response for request #"
                                  << pResponse->m_requestId
                                  << " result="
                                  << ( pResponse->IsSuccess() ? "SUCCESS" : "FAILED" );

                // Store response and notify waiting thread
                {
                    std::lock_guard< std::mutex > lock( m_pendingResponsesMutex );
                    m_mapPendingResponses[pResponse->m_requestId] = *pResponse;
                }
                m_responseCv.notify_all();

                return size;
            };

            auto result = m_responseSubscriber->Receive( receiveFn );
            if ( !result.HasValue() )
            {
                if ( m_bRunning.load( std::memory_order_acquire ) )
                {
                    // Only log error if still running (not during shutdown)
                    LAP_COM_LOG_WARN << "CRegistryProxy: Response receive failed, retrying...";
                    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
                }
            }
        }

        LAP_COM_LOG_INFO << "CRegistryProxy: Response listener thread stopped";
    }

} // namespace registry
} // namespace com
} // namespace lap
