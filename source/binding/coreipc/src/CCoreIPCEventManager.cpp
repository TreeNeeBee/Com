/**
 * @file        CCoreIPCEventManager.cpp
 * @author      LightAP Development Team
 * @brief       Core IPC binding — CCoreIPCEventManager implementation
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Implements event communication: SendEvent, SubscribeEvent,
 *              UnsubscribeEvent, and the listener thread polling loop.
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Split from monolithic CoreIPCBinding
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CCoreIPCEventManager.hpp"
#include "CCoreIPCServiceManager.hpp"
#include "CCoreIPCCodec.hpp"
#include "CRegistryProxy.hpp"

// ==================== Cross-Module Headers ====================
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

    CCoreIPCEventManager::CCoreIPCEventManager(
        const CoreIPCConfig& config,
        SharedHandle< registry::CRegistryProxy >& pServiceRegistry,
        TransportMetrics& metrics,
        CCoreIPCServiceManager& serviceManager ) noexcept
        : m_config( config )
        , m_pServiceRegistry( pServiceRegistry )
        , m_metrics( metrics )
        , m_serviceManager( serviceManager )
    {
    }

    // ====================================================================
    // Event Communication
    // ====================================================================

    Result< void > CCoreIPCEventManager::SendEvent(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 eventId, const ByteBuffer& data ) noexcept
    {
        auto key = CCoreIPCCodec::MakeServiceKey( serviceId, instanceId );
        auto* pWrapper = m_serviceManager.GetPublisher( key );

        if ( pWrapper == nullptr ) {
            LAP_LOG_ERROR() << "[CCoreIPCEventManager] Publisher not found for service";
            return Result< void >::FromError( MakeErrorCode( CoreErrc::kInvalidArgument ) );
        }

        // Encode message with eventId header
        auto encoded = CCoreIPCCodec::EncodeEventMessage( eventId, data );

        // Send using Core IPC lambda-based API
        auto result = pWrapper->m_publisher.Send(
            [&encoded]( UInt8, Byte* buf, Size size ) -> Size {
                if ( size < encoded.size() ) {
                    return 0;
                }
                ::std::memcpy( buf, encoded.data(), encoded.size() );
                return encoded.size();
            },
            PublishPolicy::kOverwrite );

        if ( !result ) {
            LAP_LOG_ERROR() << "[CCoreIPCEventManager] Failed to send event: "
                            << result.Error().Message();
            return Result< void >::FromError( result.Error() );
        }

        m_metrics.messagesSent++;
        m_metrics.bytesSent += encoded.size();

        return Result< void >::FromValue();
    }

    Result< void > CCoreIPCEventManager::SubscribeEvent(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 eventId, EventCallback callback ) noexcept
    {
        LAP_LOG_INFO() << "[CCoreIPCEventManager] SubscribeEvent: service=0x"
                       << serviceId << ", instance=0x" << instanceId
                       << ", event=" << eventId;

        auto eventKey = CCoreIPCCodec::MakeEventKey( serviceId, instanceId, eventId );

        // Check if already subscribed
        if ( m_mapSubscribers.find( eventKey ) != m_mapSubscribers.end() ) {
            LAP_LOG_WARN() << "[CCoreIPCEventManager] Already subscribed";
            return Result< void >::FromValue();
        }

        // Query CRegistryProxy to get shmPath from endpoint
        auto slotResult = m_pServiceRegistry->FindService( serviceId );

        if ( !slotResult || !slotResult->IsActive() ) {
            LAP_LOG_ERROR() << "[CCoreIPCEventManager] Service not found in registry";
            return Result< void >::FromError( MakeErrorCode( CoreErrc::kInvalidArgument ) );
        }

        if ( slotResult->m_instanceId != instanceId ) {
            LAP_LOG_ERROR() << "[CCoreIPCEventManager] Instance ID mismatch";
            return Result< void >::FromError( MakeErrorCode( CoreErrc::kInvalidArgument ) );
        }

        String shmPath( slotResult->m_endpoint );
        LAP_LOG_INFO() << "[CCoreIPCEventManager] Found endpoint: " << shmPath;

        // Create subscriber
        SubscriberConfig subConfig;
        subConfig.channel_capacity  = m_config.m_iSubscriberQueueCapacity;
        subConfig.chunk_size        = m_config.m_iMaxPayloadSize + kCoreIPCEventHeaderSize;
        subConfig.timeout           = 100000000;  // 100ms
        subConfig.empty_policy      = SubscribePolicy::kBlock;

        auto subResult = Subscriber::Create( shmPath, subConfig );
        if ( !subResult ) {
            LAP_LOG_ERROR() << "[CCoreIPCEventManager] Failed to create subscriber: "
                            << subResult.Error().Message();
            return Result< void >::FromError( subResult.Error() );
        }

        // Create wrapper
        auto pWrapper = MakeUnique< detail::SubscriberWrapper > (
            serviceId, instanceId, eventId, callback, shmPath,
            ::std::move( subResult ).Value() );

        auto connectResult = pWrapper->m_subscriber.Connect();
        if ( !connectResult ) {
            LAP_LOG_ERROR() << "[CCoreIPCEventManager] Failed to connect subscriber: "
                            << connectResult.Error().Message();
            return Result< void >::FromError( connectResult.Error() );
        }

        // Drain any stale entries
        for ( Size i = 0; i < m_config.m_iMaxChunks; ++i ) {
            auto drainResult = pWrapper->m_subscriber.Receive( SubscribePolicy::kSkip );
            if ( !drainResult ) {
                auto err = drainResult.Error();
                if ( err == CoreErrc::kChannelEmpty || err == CoreErrc::kChannelTimeout ) {
                    break;
                }
                if ( err == CoreErrc::kIPCInvalidState ) {
                    continue;
                }
                break;
            }
            if ( drainResult.Value().empty() ) {
                break;
            }
        }

        // Start listener thread
        pWrapper->m_bRunning = true;
        pWrapper->m_listenerThread = ::std::thread(
            &CCoreIPCEventManager::listenerThread, this, pWrapper.get() );

        m_mapSubscribers[eventKey] = ::std::move( pWrapper );

        LAP_LOG_INFO() << "[CCoreIPCEventManager] Subscription successful: " << shmPath;
        return Result< void >::FromValue();
    }

    Result< void > CCoreIPCEventManager::UnsubscribeEvent(
        UInt64 serviceId, UInt64 instanceId, UInt32 eventId ) noexcept
    {
        LAP_LOG_INFO() << "[CCoreIPCEventManager] UnsubscribeEvent: service=0x"
                       << serviceId << ", instance=0x" << instanceId
                       << ", event=" << eventId;

        auto eventKey = CCoreIPCCodec::MakeEventKey( serviceId, instanceId, eventId );
        auto it = m_mapSubscribers.find( eventKey );

        if ( it == m_mapSubscribers.end() ) {
            LAP_LOG_WARN() << "[CCoreIPCEventManager] Subscription not found";
            return Result< void >::FromValue();
        }

        // Stop listener thread
        it->second->m_bRunning = false;
        if ( it->second->m_listenerThread.joinable() ) {
            it->second->m_listenerThread.join();
        }

        m_mapSubscribers.erase( it );

        LAP_LOG_INFO() << "[CCoreIPCEventManager] Unsubscription successful";
        return Result< void >::FromValue();
    }

    // ====================================================================
    // Lifecycle
    // ====================================================================

    void CCoreIPCEventManager::Shutdown() noexcept
    {
        for ( auto& [key, pWrapper] : m_mapSubscribers ) {
            pWrapper->m_bRunning = false;
            if ( pWrapper->m_listenerThread.joinable() ) {
                pWrapper->m_listenerThread.join();
            }
        }
        m_mapSubscribers.clear();
    }

    // ====================================================================
    // Private — Listener Thread
    // ====================================================================

    void CCoreIPCEventManager::listenerThread(
        detail::SubscriberWrapper* pWrapper ) noexcept
    {
        LAP_LOG_INFO() << "[CCoreIPCEventManager] Listener started for service 0x"
                       << pWrapper->m_iServiceId << "_" << pWrapper->m_iInstanceId;

        while ( pWrapper->m_bRunning ) {
            // Blocking receive — futex-based, wakes on data or timeout
            auto result = pWrapper->m_subscriber.Receive( SubscribePolicy::kBlock );

            if ( !result ) {
                auto err = result.Error();
                if ( err == CoreErrc::kChannelEmpty ||
                     err == CoreErrc::kChannelTimeout ||
                     err == CoreErrc::kIPCInvalidState ) {
                    // Normal timeout or transient state — retry
                    continue;
                }
                LAP_LOG_WARN() << "[CCoreIPCEventManager] Receive failed: "
                                << err.Message();
                ::std::this_thread::sleep_for( ::std::chrono::milliseconds( 10 ) );
                continue;
            }

            auto samples = ::std::move( result ).Value();
            if ( samples.empty() ) {
                continue;
            }

            for ( auto& sample : samples ) {
                UInt32 eventId      = 0;
                UInt32 payloadSize  = 0;
                Size   offset       = 0;

                if ( !CCoreIPCCodec::DecodeEventMessage(
                         sample.RawData(), sample.RawDataSize(),
                         eventId, payloadSize, offset ) ) {
                    m_metrics.messagesDropped++;
                    continue;
                }

                if ( eventId != pWrapper->m_iEventId ) {
                    continue;
                }

                ByteBuffer payload(
                    sample.RawData() + offset,
                    sample.RawData() + offset + payloadSize );

                m_metrics.messagesReceived++;
                m_metrics.bytesReceived += sample.RawDataSize();

                try {
                    pWrapper->m_callback(
                        pWrapper->m_iServiceId, pWrapper->m_iInstanceId,
                        eventId, static_cast< const void* >( &payload ) );
                } catch ( const ::std::exception& e ) {
                    LAP_LOG_ERROR() << "[CCoreIPCEventManager] Callback exception: "
                                    << e.what();
                }
            }
        }

        LAP_LOG_INFO() << "[CCoreIPCEventManager] Listener stopped";
    }

} // namespace binding
} // namespace com
} // namespace lap
