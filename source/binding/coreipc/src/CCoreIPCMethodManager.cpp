/**
 * @file        CCoreIPCMethodManager.cpp
 * @author      LightAP Development Team
 * @brief       Core IPC binding — CCoreIPCMethodManager implementation
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Implements Method/Field RPC: CallMethod (with retry-send),
 *              RegisterMethod (with worker thread), GetField, SetField,
 *              StopServer, Shutdown, and methodServerThread.
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Split from monolithic CoreIPCBinding
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CCoreIPCMethodManager.hpp"
#include "CCoreIPCCodec.hpp"
#include "CRegistryProxy.hpp"
#include "ComTypes.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/IPCFactory.hpp>
#include <lap/log/CLog.hpp>

// ==================== Standard Library Headers ====================
#include <chrono>
#include <cstring>

namespace lap
{
namespace com
{
namespace binding
{

    using namespace lap::core;
    using namespace lap::core::ipc;

    // ====================================================================
    // Constructor
    // ====================================================================

    CCoreIPCMethodManager::CCoreIPCMethodManager(
        const CoreIPCConfig& config,
        ShmSegmentMap& mapShmSegments,
        SharedHandle< registry::CRegistryProxy >& pServiceRegistry,
        TransportMetrics& metrics,
        Mutex& facadeMutex ) noexcept
        : m_config( config )
        , m_mapShmSegments( mapShmSegments )
        , m_pServiceRegistry( pServiceRegistry )
        , m_metrics( metrics )
        , m_facadeMutex( facadeMutex )
    {
    }

    // ====================================================================
    // Method Communication — Client Side
    // ====================================================================

    Result< ByteBuffer > CCoreIPCMethodManager::CallMethod(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 methodId, const ByteBuffer& request ) noexcept
    {
        // Validate service availability (lock-free — registry is thread-safe)
        auto slotResult = m_pServiceRegistry->FindService( serviceId );
        if ( !slotResult || !slotResult->IsActive() ||
             slotResult->m_instanceId != instanceId ) {
            return Result< ByteBuffer >::FromError(
                lap::com::MakeErrorCode( ComErrc::kServiceNotAvailable ) );
        }

        const auto key = CCoreIPCCodec::MakeServiceKey( serviceId, instanceId );
        detail::MethodClientWrapper* pClient = nullptr;
        Bool bNewClient = false;

        // Brief lock to find or create the method client
        {
            LockGuard lock( m_facadeMutex );
            auto it = m_mapMethodClients.find( key );
            if ( it == m_mapMethodClients.end() ) {
                auto reqPath  = CCoreIPCCodec::MakeMethodRequestPath(
                    serviceId, instanceId );
                auto respPath = CCoreIPCCodec::MakeMethodResponsePath(
                    serviceId, instanceId );

                PublisherConfig pubConfig;
                pubConfig.max_chunks        = m_config.m_iMaxChunks;
                pubConfig.chunk_size        =
                    m_config.m_iMaxPayloadSize + kCoreIPCMethodHeaderSize;
                pubConfig.publish_timeout   = 100000000;  // 100ms
                pubConfig.policy            = PublishPolicy::kOverwrite;
                pubConfig.ipc_type          = IPCType::kMPSC;

                auto pubResult = Publisher::Create( reqPath, pubConfig );
                if ( !pubResult ) {
                    return Result< ByteBuffer >::FromError( pubResult.Error() );
                }

                SubscriberConfig subConfig;
                subConfig.channel_capacity  = m_config.m_iSubscriberQueueCapacity;
                subConfig.chunk_size        =
                    m_config.m_iMaxPayloadSize + kCoreIPCMethodHeaderSize;
                subConfig.timeout           = 10000000;  // 10ms — blocking receive timeout
                subConfig.empty_policy      = SubscribePolicy::kBlock;
                subConfig.ipc_type          = IPCType::kSPMC;

                auto subResult = Subscriber::Create( respPath, subConfig );
                if ( !subResult ) {
                    return Result< ByteBuffer >::FromError( subResult.Error() );
                }

                auto pWrapper = MakeUnique< detail::MethodClientWrapper > (
                    serviceId, instanceId, reqPath, respPath,
                    ::std::move( pubResult ).Value(),
                    ::std::move( subResult ).Value() );

                auto connectResult = pWrapper->m_responseSubscriber.Connect();
                if ( !connectResult ) {
                    return Result< ByteBuffer >::FromError( connectResult.Error() );
                }

                pClient = pWrapper.get();
                m_mapMethodClients[key] = ::std::move( pWrapper );
                bNewClient = true;
            } else {
                pClient = it->second.get();
            }
        }
        // --- facade mutex released ---

        if ( pClient == nullptr ) {
            return Result< ByteBuffer >::FromError(
                MakeErrorCode( CoreErrc::kInternalError ) );
        }

        // Wait for IPC scanner discovery on newly created endpoints
        if ( bNewClient ) {
            ::std::this_thread::sleep_for( ::std::chrono::milliseconds( 100 ) );
        }

        UniqueLock callLock( pClient->m_callMutex );

        const UInt64 token =
            m_iMethodRequestSeq.fetch_add( 1, ::std::memory_order_relaxed );
        auto encoded = CCoreIPCCodec::EncodeMethodMessage(
            methodId, token, 0, request );

        // Lambda for sending the request
        auto sendRequest = [&encoded, &pClient]() {
            return pClient->m_requestPublisher.Send(
                [&encoded]( UInt8, Byte* buf, Size size ) -> Size {
                    if ( size < encoded.size() ) {
                        return 0;
                    }
                    ::std::memcpy( buf, encoded.data(), encoded.size() );
                    return encoded.size();
                },
                PublishPolicy::kOverwrite );
        };

        auto sendResult = sendRequest();
        if ( !sendResult ) {
            return Result< ByteBuffer >::FromError( sendResult.Error() );
        }

        m_metrics.messagesSent++;
        m_metrics.bytesSent += encoded.size();

        auto deadline = ::std::chrono::steady_clock::now() +
                        ::std::chrono::milliseconds( m_config.m_iMethodCallTimeoutMs );

        constexpr auto kRetryInterval = ::std::chrono::milliseconds( 50 );
        auto nextRetrySend = ::std::chrono::steady_clock::now() + kRetryInterval;

        while ( ::std::chrono::steady_clock::now() < deadline ) {
            // Blocking receive — futex-based, wakes on data or timeout
            auto recvResult =
                pClient->m_responseSubscriber.Receive( SubscribePolicy::kBlock );
            if ( !recvResult ) {
                auto err = recvResult.Error();
                if ( err == CoreErrc::kChannelEmpty ||
                     err == CoreErrc::kChannelTimeout ) {
                    auto now = ::std::chrono::steady_clock::now();
                    if ( now >= nextRetrySend ) {
                        static_cast< void > ( sendRequest() );
                        nextRetrySend = now + kRetryInterval;
                    }
                    continue;
                }
                return Result< ByteBuffer >::FromError( err );
            }

            auto samples = ::std::move( recvResult ).Value();
            if ( samples.empty() ) {
                continue;
            }

            for ( auto& sample : samples ) {
                UInt32 respMethodId = 0;
                UInt64 respToken    = 0;
                Int32  status       = 0;
                UInt32 payloadSize  = 0;
                Size   offset       = 0;

                if ( !CCoreIPCCodec::DecodeMethodMessage(
                         sample.RawData(), sample.RawDataSize(),
                         respMethodId, respToken, status,
                         payloadSize, offset ) ) {
                    m_metrics.messagesDropped++;
                    continue;
                }

                if ( respToken != token || respMethodId != methodId ) {
                    continue;
                }

                m_metrics.messagesReceived++;
                m_metrics.bytesReceived += sample.RawDataSize();

                if ( status != 0 ) {
                    return Result< ByteBuffer >::FromError(
                        lap::com::MakeErrorCode(
                            static_cast< ComErrc > ( status ) ) );
                }

                ByteBuffer payload(
                    sample.RawData() + offset,
                    sample.RawData() + offset + payloadSize );

                return Result< ByteBuffer >::FromValue( ::std::move( payload ) );
            }
            // No matching response in this batch — blocking receive will wait
        }

        m_metrics.timeoutErrors++;
        return Result< ByteBuffer >::FromError(
            lap::com::MakeErrorCode( ComErrc::kTimeout ) );
    }

    // ====================================================================
    // Method Communication — Server Side
    // ====================================================================

    Result< void > CCoreIPCMethodManager::RegisterMethod(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 methodId, MethodHandler handler ) noexcept
    {
        const auto key = CCoreIPCCodec::MakeServiceKey( serviceId, instanceId );
        detail::MethodServerWrapper* pServer = nullptr;

        // Brief lock to find or create the method server
        {
            LockGuard lock( m_facadeMutex );
            auto it = m_mapMethodServers.find( key );
            if ( it == m_mapMethodServers.end() ) {
                auto reqPath  = CCoreIPCCodec::MakeMethodRequestPath(
                    serviceId, instanceId );
                auto respPath = CCoreIPCCodec::MakeMethodResponsePath(
                    serviceId, instanceId );

                // Create request SHM (MPSC)
                SharedMemoryConfig reqShmConfig{};
                reqShmConfig.max_chunks         =
                    static_cast< UInt16 > ( m_config.m_iMaxChunks );
                reqShmConfig.chunk_size         = static_cast< UInt32 > (
                    m_config.m_iMaxPayloadSize + kCoreIPCMethodHeaderSize );
                reqShmConfig.ipc_type           = IPCType::kMPSC;
                reqShmConfig.channel_capacity   = kMaxChannelCapacity;

                auto reqShmResult = CCoreIPCCodec::EnsureSharedMemory(
                    reqPath, reqShmConfig, m_mapShmSegments );
                if ( !reqShmResult ) {
                    LAP_LOG_ERROR() << "[CCoreIPCMethodManager] Failed request SHM: "
                                    << reqShmResult.Error().Message();
                    return Result< void >::FromError( reqShmResult.Error() );
                }

                // Create response SHM (SPMC)
                SharedMemoryConfig respShmConfig{};
                respShmConfig.max_chunks        =
                    static_cast< UInt16 > ( m_config.m_iMaxChunks );
                respShmConfig.chunk_size        = static_cast< UInt32 > (
                    m_config.m_iMaxPayloadSize + kCoreIPCMethodHeaderSize );
                respShmConfig.ipc_type          = IPCType::kSPMC;
                respShmConfig.channel_capacity  = kMaxChannelCapacity;

                auto respShmResult = CCoreIPCCodec::EnsureSharedMemory(
                    respPath, respShmConfig, m_mapShmSegments );
                if ( !respShmResult ) {
                    LAP_LOG_ERROR() << "[CCoreIPCMethodManager] Failed response SHM: "
                                    << respShmResult.Error().Message();
                    return Result< void >::FromError( respShmResult.Error() );
                }

                // Create request subscriber
                SubscriberConfig subConfig;
                subConfig.channel_capacity  = m_config.m_iSubscriberQueueCapacity;
                subConfig.chunk_size        =
                    m_config.m_iMaxPayloadSize + kCoreIPCMethodHeaderSize;
                subConfig.timeout           = 10000000;  // 10ms — blocking receive timeout
                subConfig.empty_policy      = SubscribePolicy::kBlock;
                subConfig.ipc_type          = IPCType::kMPSC;

                auto subResult = Subscriber::Create( reqPath, subConfig );
                if ( !subResult ) {
                    return Result< void >::FromError( subResult.Error() );
                }

                // Create response publisher
                PublisherConfig pubConfig;
                pubConfig.max_chunks        = m_config.m_iMaxChunks;
                pubConfig.chunk_size        =
                    m_config.m_iMaxPayloadSize + kCoreIPCMethodHeaderSize;
                pubConfig.publish_timeout   = 100000000;  // 100ms
                pubConfig.policy            = PublishPolicy::kOverwrite;
                pubConfig.ipc_type          = IPCType::kSPMC;

                auto pubResult = Publisher::Create( respPath, pubConfig );
                if ( !pubResult ) {
                    return Result< void >::FromError( pubResult.Error() );
                }

                auto pWrapper = MakeUnique< detail::MethodServerWrapper > (
                    serviceId, instanceId, reqPath, respPath,
                    ::std::move( subResult ).Value(),
                    ::std::move( pubResult ).Value() );

                auto connectResult = pWrapper->m_requestSubscriber.Connect();
                if ( !connectResult ) {
                    return Result< void >::FromError( connectResult.Error() );
                }

                pWrapper->m_bRunning = true;
                pWrapper->m_workerThread = ::std::thread(
                    &CCoreIPCMethodManager::methodServerThread, this,
                    pWrapper.get() );

                pServer = pWrapper.get();
                m_mapMethodServers[key] = ::std::move( pWrapper );
            } else {
                pServer = it->second.get();
            }
        }
        // --- facade mutex released ---

        if ( pServer == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( CoreErrc::kInternalError ) );
        }

        // Register handler under the server's own handler mutex
        {
            LockGuard lock( pServer->m_handlerMutex );
            pServer->m_mapHandlers[methodId] = ::std::move( handler );
        }

        return Result< void >::FromValue();
    }

    // ====================================================================
    // Field Communication
    // ====================================================================

    Result< ByteBuffer > CCoreIPCMethodManager::GetField(
        UInt64 serviceId, UInt64 instanceId, UInt32 fieldId ) noexcept
    {
        const UInt32 getterMethodId = fieldId | 0x10000U;
        return CallMethod( serviceId, instanceId, getterMethodId, ByteBuffer{} );
    }

    Result< void > CCoreIPCMethodManager::SetField(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 fieldId, const ByteBuffer& data ) noexcept
    {
        const UInt32 setterMethodId = fieldId | 0x20000U;
        auto result = CallMethod( serviceId, instanceId, setterMethodId, data );
        if ( !result ) {
            return Result< void >::FromError( result.Error() );
        }
        return Result< void >::FromValue();
    }

    // ====================================================================
    // Lifecycle
    // ====================================================================

    void CCoreIPCMethodManager::StopServer( UInt64 serviceKey ) noexcept
    {
        auto serverIt = m_mapMethodServers.find( serviceKey );
        if ( serverIt != m_mapMethodServers.end() ) {
            serverIt->second->m_bRunning = false;
            if ( serverIt->second->m_workerThread.joinable() ) {
                serverIt->second->m_workerThread.join();
            }
            m_mapMethodServers.erase( serverIt );
        }
    }

    void CCoreIPCMethodManager::Shutdown() noexcept
    {
        // Stop all method servers
        for ( auto& [key, pWrapper] : m_mapMethodServers ) {
            pWrapper->m_bRunning = false;
            if ( pWrapper->m_workerThread.joinable() ) {
                pWrapper->m_workerThread.join();
            }
        }
        m_mapMethodServers.clear();

        // Clear method clients
        m_mapMethodClients.clear();
    }

    // ====================================================================
    // Private — Method Server Thread
    // ====================================================================

    void CCoreIPCMethodManager::methodServerThread(
        detail::MethodServerWrapper* pWrapper ) noexcept
    {
        LAP_LOG_INFO() << "[CCoreIPCMethodManager] Server thread started for 0x"
                       << pWrapper->m_iServiceId << "_"
                       << pWrapper->m_iInstanceId;

        while ( pWrapper->m_bRunning ) {
            // Blocking receive — futex-based, wakes on data or timeout
            auto result =
                pWrapper->m_requestSubscriber.Receive( SubscribePolicy::kBlock );

            if ( !result ) {
                auto err = result.Error();
                if ( err == CoreErrc::kChannelEmpty ||
                     err == CoreErrc::kChannelTimeout ||
                     err == CoreErrc::kIPCInvalidState ) {
                    // Normal timeout or transient state — retry
                    continue;
                }

                LAP_LOG_WARN() << "[CCoreIPCMethodManager] Receive failed: "
                                << err.Message();
                ::std::this_thread::sleep_for( ::std::chrono::milliseconds( 10 ) );
                continue;
            }

            auto samples = ::std::move( result ).Value();
            if ( samples.empty() ) {
                continue;
            }

            for ( auto& sample : samples ) {
                UInt32 methodId     = 0;
                UInt64 token        = 0;
                Int32  status       = 0;
                UInt32 payloadSize  = 0;
                Size   offset       = 0;

                if ( !CCoreIPCCodec::DecodeMethodMessage(
                         sample.RawData(), sample.RawDataSize(),
                         methodId, token, status,
                         payloadSize, offset ) ) {
                    m_metrics.messagesDropped++;
                    continue;
                }

                ByteBuffer requestPayload(
                    sample.RawData() + offset,
                    sample.RawData() + offset + payloadSize );

                m_metrics.messagesReceived++;
                m_metrics.bytesReceived += sample.RawDataSize();

                // Find handler
                MethodHandler handler;
                {
                    LockGuard lock( pWrapper->m_handlerMutex );
                    auto it = pWrapper->m_mapHandlers.find( methodId );
                    if ( it != pWrapper->m_mapHandlers.end() ) {
                        handler = it->second;
                    }
                }

                ByteBuffer responsePayload;
                Int32 respStatus = 0;

                if ( handler ) {
                    try {
                        handler(
                            pWrapper->m_iServiceId, pWrapper->m_iInstanceId,
                            methodId,
                            static_cast< const void* >( &requestPayload ),
                            static_cast< void* >( &responsePayload ) );
                    } catch ( const ::std::exception& e ) {
                        LAP_LOG_ERROR() << "[CCoreIPCMethodManager] Handler exception: "
                                        << e.what();
                        respStatus = static_cast< Int32 > ( ComErrc::kInternal );
                    }
                } else {
                    respStatus =
                        static_cast< Int32 > ( ComErrc::kServiceNotAvailable );
                }

                auto encoded = CCoreIPCCodec::EncodeMethodMessage(
                    methodId, token, respStatus, responsePayload );

                auto sendResult = pWrapper->m_responsePublisher.Send(
                    [&encoded]( UInt8, Byte* buf, Size size ) -> Size {
                        if ( size < encoded.size() ) {
                            return 0;
                        }
                        ::std::memcpy( buf, encoded.data(), encoded.size() );
                        return encoded.size();
                    },
                    PublishPolicy::kOverwrite );

                if ( !sendResult ) {
                    LAP_LOG_ERROR() << "[CCoreIPCMethodManager] Response send failed: "
                                    << sendResult.Error().Message();
                    continue;
                }

                m_metrics.messagesSent++;
                m_metrics.bytesSent += encoded.size();
            }
        }

        LAP_LOG_INFO() << "[CCoreIPCMethodManager] Server thread stopped";
    }

} // namespace binding
} // namespace com
} // namespace lap
