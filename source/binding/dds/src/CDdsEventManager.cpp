/**
 * @file        CDdsEventManager.cpp
 * @author      LightAP Development Team
 * @brief       DDS binding — Event communication implementation
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Extracted from DdsEvent.cpp
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CDdsEventManager.hpp"
#include "CDdsCodec.hpp"
#include "CDdsPayload.hpp"
#include "CDdsTypeRegistry.hpp"
#include "ComTypes.hpp"
#include "DdsReaderListener.hpp"

// ==================== Third-Party Headers ====================
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/core/status/PublicationMatchedStatus.hpp>
#include <fastdds/rtps/common/InstanceHandle.hpp>

// ==================== Standard Library Headers ====================
#include <chrono>
#include <cstdio>

namespace lap
{
namespace com
{
namespace binding
{

    using namespace eprosima::fastdds::dds;

    // ====================================================================
    // Constructor
    // ====================================================================

    CDdsEventManager::CDdsEventManager(
        const DdsConfig& config,
        DomainParticipant*& pParticipant,
        Publisher*& pPublisher,
        Subscriber*& pSubscriber,
        const TypeSupport& typeSupport,
        TransportMetrics& metrics ) noexcept
        : m_config( config )
        , m_pParticipant( pParticipant )
        , m_pPublisher( pPublisher )
        , m_pSubscriber( pSubscriber )
        , m_typeSupport( typeSupport )
        , m_metrics( metrics )
    {}

    // ====================================================================
    // Event Communication
    // ====================================================================

    Result< void > CDdsEventManager::PrepareChannel(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 eventId ) noexcept
    {
        auto key = CDdsCodec::MakeEventKey( serviceId, instanceId, eventId );
        auto& channel = m_mapChannels[key];

        if ( !channel.IsOpen() ) {
            auto topicName = CDdsCodec::MakeEventTopicName(
                serviceId, instanceId, eventId );

            // Use adapter's TypeSupport if available
            const auto* pAdapter = CDdsTypeRegistry::Instance().FindAdapter(
                serviceId, eventId );
            auto chosenType = m_typeSupport;
            if ( pAdapter != nullptr ) {
                chosenType = pAdapter->GetTypeSupport();
                chosenType.register_type( m_pParticipant );
            }

            if ( !channel.Open( m_pParticipant, m_pPublisher, m_pSubscriber,
                                chosenType, topicName, m_config ) ) {
                m_mapChannels.erase( key );
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }
        }

        // Eagerly create writer
        auto* pWriter = channel.GetOrCreateWriter();
        if ( pWriter == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kCommunicationFailure ) );
        }

        return Result< void >::FromValue();
    }

    Result< void > CDdsEventManager::SendEvent(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 eventId, const void* pData,
        Size dataSize ) noexcept
    {
        // ── Serialization Policy ──
        // 1. Look up type adapter via (serviceId, eventId)
        // 2. If found → adapter path (strongly-typed CDR via TypeSupport)
        // 3. If NOT found + dataSize > 0 → memcpy fallback into DdsPayload
        // 4. If NOT found + dataSize == 0 → Phase 1 ByteBuffer→DdsPayload compat

        if ( pData == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kInvalidArgument ) );
        }

        const auto* pAdapter = CDdsTypeRegistry::Instance().FindAdapter(
            serviceId, eventId );

        auto key = CDdsCodec::MakeEventKey( serviceId, instanceId, eventId );

        // Get or open channel (lazy creation)
        auto& channel = m_mapChannels[key];
        if ( !channel.IsOpen() ) {
            auto topicName = CDdsCodec::MakeEventTopicName(
                serviceId, instanceId, eventId );

            // Use adapter's TypeSupport if available, otherwise default DdsPayload
            auto chosenType = m_typeSupport;
            if ( pAdapter != nullptr ) {
                chosenType = pAdapter->GetTypeSupport();
                chosenType.register_type( m_pParticipant );
            }

            if ( !channel.Open( m_pParticipant, m_pPublisher, m_pSubscriber,
                                chosenType, topicName, m_config ) ) {
                m_mapChannels.erase( key );
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }
        }

        // Get or create the unique writer on this channel
        auto* pWriter = channel.GetOrCreateWriter();
        if ( pWriter == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kCommunicationFailure ) );
        }

        // Check if writer has matched readers (required for RELIABLE QoS)
        if ( m_config.m_bReliable ) {
            PublicationMatchedStatus pubStatus;
            pWriter->get_publication_matched_status( pubStatus );

            if ( pubStatus.current_count == 0 ) {
                LAP_COM_LOG_ERROR << "No matched readers for RELIABLE writer (service=0x"
                                  << serviceId << ", instance=0x" << instanceId
                                  << ", event=" << eventId << "). Write will likely fail!";
            }
        }

        // ── Write via appropriate serialization path ──
        auto start = ::std::chrono::steady_clock::now();
        ReturnCode_t ret;
        Size payloadSize = 0;

        if ( pAdapter != nullptr ) {
            // Path A: Typed adapter — CreateSample → write → FreeSample
            void* pSample = pAdapter->CreateSample( pData, dataSize );
            if ( pSample == nullptr ) {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }

            LAP_COM_LOG_DEBUG << "[DDS SEND] Typed adapter path: service=0x"
                              << serviceId << ", event=" << eventId
                              << ", dataSize=" << dataSize;

            ret = pWriter->write(
                pSample,
                eprosima::fastdds::rtps::c_InstanceHandle_Unknown );
            payloadSize = dataSize;
            pAdapter->FreeSample( pSample );

        } else if ( dataSize > 0 ) {
            // Path B: memcpy fallback — raw bytes into DdsPayload
            DdsPayload msg( ::std::vector< uint8_t >(
                reinterpret_cast< const uint8_t* >( pData ),
                reinterpret_cast< const uint8_t* >( pData ) + dataSize ) );

            LAP_COM_LOG_DEBUG << "[DDS SEND] Memcpy fallback path: service=0x"
                              << serviceId << ", event=" << eventId
                              << ", dataSize=" << dataSize;

            ret = pWriter->write(
                static_cast< void* > ( &msg ),
                eprosima::fastdds::rtps::c_InstanceHandle_Unknown );
            payloadSize = dataSize;

        } else {
            // Path C: Phase 1 compat — pData is ByteBuffer*
            const auto& data = *static_cast< const ByteBuffer* >( pData );

            DdsPayload msg( ::std::vector< uint8_t >(
                data.begin(), data.end() ) );

            LAP_COM_LOG_DEBUG << "[DDS SEND] ByteBuffer compat path: service=0x"
                              << serviceId << ", event=" << eventId
                              << ", size=" << data.size();

            ret = pWriter->write(
                static_cast< void* > ( &msg ),
                eprosima::fastdds::rtps::c_InstanceHandle_Unknown );
            payloadSize = data.size();
        }

        auto end = ::std::chrono::steady_clock::now();

        if ( ret != RETCODE_OK ) {
            LAP_COM_LOG_ERROR << "DDS write failed with code " << ret;
            m_metrics.messagesDropped++;
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kCommunicationFailure ) );
        }

        // Update metrics
        m_metrics.messagesSent++;
        m_metrics.bytesSent += payloadSize;

        auto latencyNs = ::std::chrono::duration_cast< ::std::chrono::nanoseconds > (
            end - start ).count();
        if ( m_metrics.messagesSent == 1 ) {
            m_metrics.minLatencyNs = latencyNs;
            m_metrics.maxLatencyNs = latencyNs;
            m_metrics.avgLatencyNs = static_cast< double > ( latencyNs );
        } else {
            m_metrics.minLatencyNs = ::std::min(
                m_metrics.minLatencyNs, static_cast< UInt64 > ( latencyNs ) );
            m_metrics.maxLatencyNs = ::std::max(
                m_metrics.maxLatencyNs, static_cast< UInt64 > ( latencyNs ) );
            m_metrics.avgLatencyNs =
                ( m_metrics.avgLatencyNs * ( m_metrics.messagesSent - 1 ) + latencyNs )
                / m_metrics.messagesSent;
        }

        return Result< void >::FromValue();
    }

    Result< void > CDdsEventManager::SubscribeEvent(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 eventId, EventCallback callback,
        Size dataSize ) noexcept
    {
        const auto* pAdapter = CDdsTypeRegistry::Instance().FindAdapter(
            serviceId, eventId );

        // ── Callback wrapping: when no adapter and dataSize > 0, the
        //    DdsReaderListener fallback path dispatches const void* pointing
        //    to a ByteBuffer.  The NVI erased callback expects const T*.
        //    Wrap to extract raw bytes from ByteBuffer for the typed caller.
        EventCallback finalCallback = callback;
        if ( pAdapter == nullptr && dataSize > 0 ) {
            finalCallback = [callback, dataSize](
                UInt64 svcId, UInt64 instId, UInt32 evtId,
                const void* pData )
            {
                const auto& buf =
                    *static_cast< const ByteBuffer* >( pData );
                if ( buf.size() >= dataSize ) {
                    callback( svcId, instId, evtId, buf.data() );
                }
            };
        }

        auto key = CDdsCodec::MakeEventKey( serviceId, instanceId, eventId );

        // Get or open channel
        auto& channel = m_mapChannels[key];
        if ( !channel.IsOpen() ) {
            auto topicName = CDdsCodec::MakeEventTopicName(
                serviceId, instanceId, eventId );

            // Use adapter's TypeSupport if available
            auto chosenType = m_typeSupport;
            if ( pAdapter != nullptr ) {
                chosenType = pAdapter->GetTypeSupport();
                chosenType.register_type( m_pParticipant );
            }

            if ( !channel.Open( m_pParticipant, m_pPublisher, m_pSubscriber,
                                chosenType, topicName, m_config ) ) {
                m_mapChannels.erase( key );
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }
        }

        if ( channel.ReaderCount() > 0 ) {
            LAP_COM_LOG_WARN << "Already subscribed: service=0x" << serviceId
                             << ", instance=0x" << instanceId
                             << ", event=" << eventId;
            return Result< void >::FromValue();
        }

        // Create listener — pass adapter for typed receive path
        auto pListener = MakeUnique< DdsReaderListener > (
            finalCallback, serviceId, instanceId, eventId, m_metrics, pAdapter );

        LAP_COM_LOG_INFO << "Creating DataReader with key=" << key
                         << ", QoS: reliable=" << m_config.m_bReliable
                         << ", transient_local=" << m_config.m_bTransientLocal
                         << ", history_depth=" << m_config.m_iHistoryDepth;

        auto* pReader = channel.AddReader(
            ::std::move( pListener ), StatusMask::all() );
        if ( pReader == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kCommunicationFailure ) );
        }

        LAP_COM_LOG_INFO << "Subscribed to event: service=0x" << serviceId
                         << ", instance=0x" << instanceId
                         << ", event=" << eventId;

        return Result< void >::FromValue();
    }

    Result< void > CDdsEventManager::UnsubscribeEvent(
        UInt64 serviceId, UInt64 instanceId, UInt32 eventId ) noexcept
    {
        auto key = CDdsCodec::MakeEventKey( serviceId, instanceId, eventId );

        auto it = m_mapChannels.find( key );
        if ( it == m_mapChannels.end() || it->second.ReaderCount() == 0 ) {
            LAP_COM_LOG_WARN << "Not subscribed: service=0x" << serviceId
                             << ", instance=0x" << instanceId
                             << ", event=" << eventId;
            return Result< void >::FromValue();
        }

        // Remove the reader (channel keeps the topic and possible writer)
        auto* pReader = it->second.GetFirstReader();
        if ( pReader != nullptr ) {
            it->second.RemoveReader( pReader );
        }

        // If channel is empty (no writer, no readers), close and remove
        if ( it->second.IsEmpty() ) {
            it->second.Close();
            m_mapChannels.erase( it );
        }

        LAP_COM_LOG_INFO << "Unsubscribed from event: service=0x" << serviceId
                         << ", instance=0x" << instanceId
                         << ", event=" << eventId;

        return Result< void >::FromValue();
    }

    // ====================================================================
    // Lifecycle
    // ====================================================================

    void CDdsEventManager::Shutdown() noexcept
    {
        for ( auto& [key, channel] : m_mapChannels )
        {
            channel.Close();
        }
        m_mapChannels.clear();
    }

} // namespace binding
} // namespace com
} // namespace lap
