/**
 * @file        CRegistryDispatcher.cpp
 * @author      LightAP Development Team
 * @brief       Core IPC-based registry service implementation
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
#include "CRegistryDispatcher.hpp"
#include "ComTypes.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/IPCFactory.hpp>

// ==================== Standard Library Headers ====================
#include <chrono>
#include <unistd.h>
#include <sys/mman.h>

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
    static constexpr const char* kRequestChannelPath  = "/lap_registry_req";
    static constexpr const char* kResponseChannelPath = "/lap_registry_resp";

    CRegistryDispatcher::CRegistryDispatcher() noexcept
        : m_qmRegistry( RegistryType::kQM )
        , m_asilRegistry( RegistryType::kASIL )
        , m_bRunning( false )
        , m_iTotalRequests( 0 )
        , m_iSuccessfulOps( 0 )
        , m_iFailedOps( 0 )
    {
    }

    CRegistryDispatcher::~CRegistryDispatcher() noexcept
    {
        Shutdown();
    }

    Result< void > CRegistryDispatcher::Initialize() noexcept
    {
        // Step 1: Initialize QM and ASIL registries
        auto qmResult = m_qmRegistry.Initialize();
        if ( !qmResult.HasValue() )
        {
            LAP_COM_LOG_ERROR << "Failed to initialize QM registry";
            return qmResult;
        }

        auto asilResult = m_asilRegistry.Initialize();
        if ( !asilResult.HasValue() )
        {
            LAP_COM_LOG_ERROR << "Failed to initialize ASIL registry";
            return asilResult;
        }

        LAP_COM_LOG_INFO << "Registry service: Initialized dual registries";

        // Step 2: Create shared memory for request channel (MPSC)
        SharedMemoryConfig reqShmConfig{};
        reqShmConfig.max_chunks       = 1024;
        reqShmConfig.chunk_size       = sizeof( RegistryRequest );
        reqShmConfig.channel_capacity = 1024;
        reqShmConfig.ipc_type         = IPCType::kMPSC;

        auto reqShmResult = IPCFactory::CreateSHM( kRequestChannelPath, reqShmConfig );
        if ( !reqShmResult.HasValue() )
        {
            LAP_COM_LOG_ERROR << "Failed to create request SHM: "
                              << reqShmResult.Error().Message();
            return Result< void >::FromError( reqShmResult.Error() );
        }
        m_pRequestShm = std::move( reqShmResult ).Value();

        LAP_COM_LOG_INFO << "Registry service: Created request SHM on "
                         << kRequestChannelPath;

        // Step 3: Create shared memory for response channel (SPMC)
        SharedMemoryConfig respShmConfig{};
        respShmConfig.max_chunks       = 128;
        respShmConfig.chunk_size       = sizeof( RegistryResponse );
        respShmConfig.channel_capacity = 256;
        respShmConfig.ipc_type         = IPCType::kSPMC;

        auto respShmResult = IPCFactory::CreateSHM( kResponseChannelPath, respShmConfig );
        if ( !respShmResult.HasValue() )
        {
            LAP_COM_LOG_ERROR << "Failed to create response SHM: "
                              << respShmResult.Error().Message();
            return Result< void >::FromError( respShmResult.Error() );
        }
        m_pResponseShm = std::move( respShmResult ).Value();

        LAP_COM_LOG_INFO << "Registry service: Created response SHM on "
                         << kResponseChannelPath;

        // Step 4: Create Core IPC Subscriber for receiving requests (MPSC)
        SubscriberConfig subConfig;
        subConfig.ipc_type         = IPCType::kMPSC;  // Must match request SHM type
        subConfig.max_chunks       = 1024;             // Must match request SHM config
        subConfig.chunk_size       = sizeof( RegistryRequest );  // Must match request SHM config
        subConfig.channel_capacity = 1024;  // Large queue for high throughput
        subConfig.STmin = 0;                 // No throttling
        subConfig.empty_policy = lap::core::ipc::SubscribePolicy::kBlock;

        auto subResult = Subscriber::Create( kRequestChannelPath, subConfig );
        if ( !subResult.HasValue() )
        {
            LAP_COM_LOG_ERROR << "Failed to create request subscriber: "
                              << subResult.Error().Message();
            return Result< void >::FromError( subResult.Error() );
        }
        m_requestSubscriber.emplace( std::move( subResult ).Value() );

        LAP_COM_LOG_INFO << "Registry service: Created request subscriber on "
                         << kRequestChannelPath;

        // Step 5: Create Core IPC Publisher for sending responses (SPMC)
        PublisherConfig pubConfig;
        pubConfig.max_chunks = 128;  // Support many concurrent clients
        pubConfig.chunk_size = sizeof( RegistryResponse );
        pubConfig.loan_policy = lap::core::ipc::LoanPolicy::kWait;
        pubConfig.policy = lap::core::ipc::PublishPolicy::kOverwrite;

        auto pubResult = Publisher::Create( kResponseChannelPath, pubConfig );
        if ( !pubResult.HasValue() )
        {
            LAP_COM_LOG_ERROR << "Failed to create response publisher: "
                              << pubResult.Error().Message();
            return Result< void >::FromError( pubResult.Error() );
        }
        m_responsePublisher.emplace( std::move( pubResult ).Value() );

        LAP_COM_LOG_INFO << "Registry service: Created response publisher on "
                         << kResponseChannelPath;

        // Step 6: Initialize SD-Proxy service (auto-register in Slot 1 and 512)
        auto sdProxyResult = m_sdProxy.Initialize( m_qmRegistry );
        if ( !sdProxyResult.HasValue() )
        {
            LAP_COM_LOG_WARN << "Registry service: SD-Proxy initialization failed (non-fatal)";
            // Non-fatal: registry can operate without SD-Proxy
        }
        else
        {
            auto startResult = m_sdProxy.Start();
            if ( !startResult.HasValue() )
            {
                LAP_COM_LOG_WARN << "Registry service: SD-Proxy start failed (non-fatal)";
            }
        }

        return Result< void >::FromValue();
    }

    void CRegistryDispatcher::Shutdown() noexcept
    {
        Bool wasRunning = m_bRunning.exchange( false, std::memory_order_acq_rel );

        if ( !wasRunning )
        {
            return;  // Already shut down
        }

        LAP_COM_LOG_INFO << "Registry service: Shutting down...";

        // Stop SD-Proxy background threads first
        m_sdProxy.Stop();

        // Disconnect subscriber to unblock Receive()
        if ( m_requestSubscriber.has_value() )
        {
            m_requestSubscriber->Disconnect();
        }

        // Wait for event loop thread to finish
        if ( m_eventLoopThread.joinable() )
        {
            m_eventLoopThread.join();
        }

        LAP_COM_LOG_INFO << "Registry service: Shutdown complete"
                         << " (total_requests=" << m_iTotalRequests.load()
                         << ", successful=" << m_iSuccessfulOps.load()
                         << ", failed=" << m_iFailedOps.load() << ")";

        // Release IPC channels before SHM segments
        m_requestSubscriber.reset();
        m_responsePublisher.reset();

        // Release SHM segments and unlink POSIX shared memory
        m_pRequestShm.reset();
        m_pResponseShm.reset();
        ::shm_unlink( kRequestChannelPath );
        ::shm_unlink( kResponseChannelPath );
    }

    Result< void > CRegistryDispatcher::Run() noexcept
    {
        m_bRunning.store( true, std::memory_order_release );

        LAP_COM_LOG_INFO << "Registry service: Started event loop";

        // Connect subscriber
        auto connectResult = m_requestSubscriber->Connect();
        if ( !connectResult.HasValue() )
        {
            LAP_COM_LOG_ERROR << "Failed to connect request subscriber: "
                              << connectResult.Error().Message();
            return connectResult;
        }

        // Main event loop: Receive requests and process them
        while ( m_bRunning.load( std::memory_order_acquire ) )
        {
            // Receive request message (blocking)
            auto receiveFn = [this]( lap::core::UInt8 /*channelId*/,
                                     lap::core::Byte* data,
                                     lap::core::Size size ) -> lap::core::Size
            {
                if ( size < sizeof( RegistryRequest ) )
                {
                    LAP_COM_LOG_WARN << "Received undersized request: " << size << " bytes";
                    return 0;
                }

                // Parse request
                const RegistryRequest* pRequest = reinterpret_cast< const RegistryRequest* > ( data );

                m_iTotalRequests.fetch_add( 1, std::memory_order_relaxed );

                LAP_COM_LOG_DEBUG << "Processing request #" << pRequest->m_requestId
                                  << " op=" << static_cast< Int32 > ( pRequest->m_opType )
                                  << " service_id=0x" << pRequest->m_serviceId;

                // Process request
                RegistryResponse response = processRequest( *pRequest );

                // Update statistics
                if ( response.IsSuccess() )
                {
                    m_iSuccessfulOps.fetch_add( 1, std::memory_order_relaxed );
                }
                else
                {
                    m_iFailedOps.fetch_add( 1, std::memory_order_relaxed );
                }

                // Send response via SPMC channel
                auto sendFn = [&response]( lap::core::UInt8 /*channelId*/,
                                           lap::core::Byte* buf,
                                           lap::core::Size capacity ) -> lap::core::Size
                {
                    if ( capacity < sizeof( RegistryResponse ) )
                    {
                        return 0;
                    }
                    std::memcpy( buf, &response, sizeof( RegistryResponse ) );
                    return sizeof( RegistryResponse );
                };

                auto sendResult = m_responsePublisher->Send(
                    sendFn,
                    lap::core::ipc::PublishPolicy::kOverwrite );
                if ( !sendResult.HasValue() )
                {
                    LAP_COM_LOG_ERROR << "Failed to send response: "
                                      << sendResult.Error().Message();
                }

                return size;  // Consumed all bytes
            };

            auto recvResult = m_requestSubscriber->Receive( receiveFn );
            if ( !recvResult.HasValue() )
            {
                if ( m_bRunning.load( std::memory_order_acquire ) )
                {
                    // Only log error if still running (not during shutdown)
                    LAP_COM_LOG_ERROR << "Request receive failed: "
                                      << recvResult.Error().Message();
                }
                break;
            }
        }

        LAP_COM_LOG_INFO << "Registry service: Event loop exited";
        return Result< void >::FromValue();
    }

    RegistryResponse CRegistryDispatcher::processRequest( const RegistryRequest& request ) noexcept
    {
        switch ( request.m_opType )
        {
            case RegistryOpType::kRegisterService:
                return handleRegisterService( request );

            case RegistryOpType::kUnregisterService:
                return handleUnregisterService( request );

            case RegistryOpType::kUpdateHeartbeat:
                return handleUpdateHeartbeat( request );

            case RegistryOpType::kQueryService:
                return handleQueryService( request );

            default:
                return RegistryResponse::CreateError(
                    request.m_requestId,
                    request.m_opType,
                    request.m_serviceId,
                    RegistryResultCode::kInternalError,
                    "Unknown operation type"
                );
        }
    }

    RegistryResponse CRegistryDispatcher::handleRegisterService( const RegistryRequest& request ) noexcept
    {
        // Calculate slot index
        UInt32 slotIndex = CalculateSlot( request.m_serviceId );
        if ( slotIndex == 0 )
        {
            return RegistryResponse::CreateError(
                request.m_requestId,
                request.m_opType,
                request.m_serviceId,
                RegistryResultCode::kInvalidSlot,
                "Slot 0 is reserved (invalid service ID)"
            );
        }

        // Select registry based on service ID
        RegistryType regType = SelectRegistry( request.m_serviceId );
        CServiceRegistry* pTargetRegistry = nullptr;

        if ( regType == RegistryType::kASIL )
        {
            pTargetRegistry = &m_asilRegistry;
        }
        else if ( regType == RegistryType::kQM )
        {
            pTargetRegistry = &m_qmRegistry;
        }
        else  // kBoth (broadcast)
        {
            // Register in both registries
            auto qmResult = m_qmRegistry.RegisterService(
                slotIndex,
                request.m_serviceId,
                request.m_instanceId,
                request.m_majorVersion,
                request.m_minorVersion,
                request.m_bindingType,
                request.m_endpoint
            );

            auto asilResult = m_asilRegistry.RegisterService(
                slotIndex,
                request.m_serviceId,
                request.m_instanceId,
                request.m_majorVersion,
                request.m_minorVersion,
                request.m_bindingType,
                request.m_endpoint
            );

            if ( !qmResult.HasValue() )
            {
                return RegistryResponse::CreateError(
                    request.m_requestId,
                    request.m_opType,
                    request.m_serviceId,
                    RegistryResultCode::kInternalError,
                    "Failed to register in QM registry"
                );
            }

            if ( !asilResult.HasValue() )
            {
                return RegistryResponse::CreateError(
                    request.m_requestId,
                    request.m_opType,
                    request.m_serviceId,
                    RegistryResultCode::kInternalError,
                    "Failed to register in ASIL registry"
                );
            }

            return RegistryResponse::CreateSuccess(
                request.m_requestId,
                request.m_opType,
                request.m_serviceId,
                slotIndex
            );
        }

        // Register in selected registry
        auto result = pTargetRegistry->RegisterService(
            slotIndex,
            request.m_serviceId,
            request.m_instanceId,
            request.m_majorVersion,
            request.m_minorVersion,
            request.m_bindingType,
            request.m_endpoint
        );

        if ( !result.HasValue() )
        {
            return RegistryResponse::CreateError(
                request.m_requestId,
                request.m_opType,
                request.m_serviceId,
                RegistryResultCode::kInternalError,
                "Failed to register service"
            );
        }

        return RegistryResponse::CreateSuccess(
            request.m_requestId,
            request.m_opType,
            request.m_serviceId,
            slotIndex
        );
    }

    RegistryResponse CRegistryDispatcher::handleUnregisterService( const RegistryRequest& request ) noexcept
    {
        UInt32 slotIndex = CalculateSlot( request.m_serviceId );
        if ( slotIndex == 0 )
        {
            return RegistryResponse::CreateError(
                request.m_requestId,
                request.m_opType,
                request.m_serviceId,
                RegistryResultCode::kInvalidSlot,
                "Slot 0 is reserved"
            );
        }

        RegistryType regType = SelectRegistry( request.m_serviceId );

        if ( regType == RegistryType::kBoth )
        {
            m_qmRegistry.UnregisterService( slotIndex );
            m_asilRegistry.UnregisterService( slotIndex );
        }
        else if ( regType == RegistryType::kASIL )
        {
            m_asilRegistry.UnregisterService( slotIndex );
        }
        else
        {
            m_qmRegistry.UnregisterService( slotIndex );
        }

        return RegistryResponse::CreateSuccess(
            request.m_requestId,
            request.m_opType,
            request.m_serviceId,
            slotIndex
        );
    }

    RegistryResponse CRegistryDispatcher::handleUpdateHeartbeat( const RegistryRequest& request ) noexcept
    {
        UInt32 slotIndex = CalculateSlot( request.m_serviceId );
        if ( slotIndex == 0 )
        {
            return RegistryResponse::CreateError(
                request.m_requestId,
                request.m_opType,
                request.m_serviceId,
                RegistryResultCode::kInvalidSlot,
                "Slot 0 is reserved"
            );
        }

        RegistryType regType = SelectRegistry( request.m_serviceId );
        CServiceRegistry* pTargetRegistry = ( regType == RegistryType::kASIL )
                                          ? &m_asilRegistry
                                          : &m_qmRegistry;

        auto result = pTargetRegistry->UpdateHeartbeat( slotIndex, request.m_timestampNs );

        if ( !result.HasValue() )
        {
            return RegistryResponse::CreateError(
                request.m_requestId,
                request.m_opType,
                request.m_serviceId,
                RegistryResultCode::kInternalError,
                "Failed to update heartbeat"
            );
        }

        return RegistryResponse::CreateSuccess(
            request.m_requestId,
            request.m_opType,
            request.m_serviceId,
            slotIndex
        );
    }

    RegistryResponse CRegistryDispatcher::handleQueryService( const RegistryRequest& request ) noexcept
    {
        // Step 1: Local registry lookup (< 500ns)
        RegistryType regType = SelectRegistry( request.m_serviceId );

        CServiceRegistry* pTargetRegistry = ( regType == RegistryType::kASIL )
                                          ? &m_asilRegistry
                                          : &m_qmRegistry;

        auto slotResult = pTargetRegistry->FindService( request.m_serviceId );
        if ( slotResult && slotResult->IsActive() )
        {
            return RegistryResponse::CreateQuerySuccess(
                request.m_requestId,
                request.m_serviceId,
                static_cast< UInt32 > ( slotResult->m_instanceId ),
                CalculateSlot( request.m_serviceId ),
                slotResult->m_endpoint
            );
        }

        // Step 2: Fallback to SD-Proxy remote cache (< 1ms)
        if ( m_sdProxy.IsInitialized() )
        {
            auto remoteSlot = m_sdProxy.FindRemoteService( request.m_serviceId );
            if ( remoteSlot && remoteSlot->IsActive() )
            {
                LAP_COM_LOG_DEBUG << "Query service 0x"
                                   << request.m_serviceId
                                   << " resolved via SD-Proxy cache";

                return RegistryResponse::CreateQuerySuccess(
                    request.m_requestId,
                    request.m_serviceId,
                    static_cast< UInt32 > ( remoteSlot->m_instanceId ),
                    0,  // No local slot (remote service)
                    remoteSlot->m_endpoint
                );
            }

            // Step 3: Active query via DDS binding → Discovery Server (< 100ms)
            auto activeResult = m_sdProxy.ActiveQueryService( request.m_serviceId );
            if ( activeResult && activeResult->IsActive() )
            {
                LAP_COM_LOG_DEBUG << "Query service 0x"
                                   << request.m_serviceId
                                   << " resolved via SD-Proxy active DS query";

                return RegistryResponse::CreateQuerySuccess(
                    request.m_requestId,
                    request.m_serviceId,
                    static_cast< UInt32 > ( activeResult->m_instanceId ),
                    0,  // No local slot (remote service)
                    activeResult->m_endpoint
                );
            }
        }

        // Not found in local registry, SD-Proxy cache, or active DS query
        return RegistryResponse::CreateError(
            request.m_requestId,
            request.m_opType,
            request.m_serviceId,
            RegistryResultCode::kServiceNotFound,
            "Service not found in registry or SD-Proxy cache"
        );
    }

    RegistryType CRegistryDispatcher::SelectRegistry( UInt64 serviceId ) noexcept
    {
        UInt16 sid = static_cast< UInt16 > ( serviceId & 0xFFFF );

        if ( sid == RegistryConfig::kBroadcastServiceId )
        {
            return RegistryType::kBoth;
        }
        else if ( sid >= RegistryConfig::kAsilServiceIdMin &&
                  sid <= RegistryConfig::kAsilServiceIdMax )
        {
            return RegistryType::kASIL;
        }
        else if ( sid >= RegistryConfig::kQmServiceIdMin &&
                  sid <= RegistryConfig::kQmServiceIdMax )
        {
            return RegistryType::kQM;
        }
        else
        {
            return RegistryType::kQM;  // Fallback
        }
    }

    // ==================== SD-Proxy Bridge Factory ====================

    std::function< void( UInt64, const std::vector< UInt64 >&, Bool ) >
    CRegistryDispatcher::GetSDProxyBridgeFunc() noexcept
    {
        if ( !m_sdProxy.IsInitialized() )
        {
            return nullptr;
        }

        // Return a lambda that bridges DDS discovery events → SD-Proxy.
        // Captures `this` pointer — caller must ensure CRegistryDispatcher
        // outlives DdsBinding (guaranteed by startup/shutdown ordering).
        return [this](
            UInt64 serviceId,
            const std::vector< UInt64 >& instances,
            Bool isAvailable )
        {
            if ( isAvailable )
            {
                // For each discovered instance, bridge into SD-Proxy
                for ( auto instanceId : instances )
                {
                    m_sdProxy.OnRemoteServiceDiscovered(
                        serviceId,
                        instanceId,
                        "dds",       // binding type
                        "",          // endpoint (DDS topic resolved internally)
                        "dds_edp"    // source ECU (from EDP discovery)
                    );
                }
            }
            else
            {
                // All instances removed
                m_sdProxy.OnRemoteServiceRemoved( serviceId, 0 );
            }
        };
    }

} // namespace registry
} // namespace com
} // namespace lap
