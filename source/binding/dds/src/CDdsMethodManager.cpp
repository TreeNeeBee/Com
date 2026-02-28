/**
 * @file        CDdsMethodManager.cpp
 * @author      LightAP Development Team
 * @brief       DDS binding — Method and Field communication implementation
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Extracted from DdsMethod.cpp
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CDdsMethodManager.hpp"
#include "CDdsCodec.hpp"
#include "CDdsPayload.hpp"
#include "ComTypes.hpp"

// ==================== Third-Party Headers ====================
#include <fastdds/dds/publisher/qos/DataWriterQos.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/dds/core/status/PublicationMatchedStatus.hpp>
#include <fastdds/dds/core/status/SubscriptionMatchedStatus.hpp>

// ==================== Standard Library Headers ====================
#include <chrono>
#include <cstring>
#include <sstream>
#include <thread>

namespace lap
{
namespace com
{
namespace binding
{

    using namespace eprosima::fastdds::dds;

    // ====================================================================
    // File-Local: DdsMethodReaderListener
    // ====================================================================

    namespace
    {
        /**
         * @brief   Method reader listener for server-side method handling
         *
         * @details Receives a DDS request sample (always DdsPayload), invokes
         *          the MethodHandler, and writes a DdsPayload reply.
         *
         *          The handler's pReq/pResp semantics depend on the method path:
         *            Phase 3 (m_requestSize > 0, m_responseSize > 0):
         *              pReq = raw typed bytes, pResp = raw typed bytes
         *            Phase 1 (m_requestSize == 0):
         *              pReq = ByteBuffer*, pResp = ByteBuffer*
         */
        class DdsMethodReaderListener : public DataReaderListener
        {
        public:
            DdsMethodReaderListener( MethodHandler handler,
                                     DataWriter* pResponseWriter,
                                     UInt64 serviceId,
                                     UInt64 instanceId,
                                     UInt32 methodId,
                                     TransportMetrics& metrics,
                                     Size requestSize = 0,
                                     Size responseSize = 0 ) noexcept
                : m_handler( ::std::move( handler ) )
                , m_pResponseWriter( pResponseWriter )
                , m_iServiceId( serviceId )
                , m_iInstanceId( instanceId )
                , m_iMethodId( methodId )
                , m_metrics( metrics )
                , m_requestSize( requestSize )
                , m_responseSize( responseSize )
            {}

            void on_subscription_matched(
                DataReader* /*reader*/,
                const SubscriptionMatchedStatus& status ) override
            {
                LAP_COM_LOG_DEBUG << "Method subscription matched: svc="
                                  << m_iServiceId
                                  << ", method=" << m_iMethodId
                                  << ", change=" << status.current_count_change
                                  << ", total=" << status.current_count;
            }

            void on_data_available( DataReader* reader ) override
            {

                DdsPayload msg;
                SampleInfo info;

                while ( reader->take_next_sample( &msg, &info ) == RETCODE_OK ) {

                    if ( !info.valid_data ) {
                        continue;
                    }

                    ByteBuffer responseBytes;
                    Int32 status = 0;

                    if ( m_handler ) {
                        try {
                            if ( m_requestSize > 0 ) {
                                // Phase 3: handler expects raw typed data
                                // pReq → raw bytes from DDS payload
                                // pResp → sized buffer for native output type
                                ::std::vector< UInt8 > responseBuf(
                                    m_responseSize, 0 );

                                m_handler( m_iServiceId, m_iInstanceId,
                                           m_iMethodId,
                                           static_cast< const void* >(
                                               msg.data().data() ),
                                           static_cast< void* >(
                                               responseBuf.data() ) );
                                responseBytes.assign(
                                    responseBuf.begin(), responseBuf.end() );

                            } else {
                                // Phase 1: handler expects ByteBuffer*
                                ByteBuffer request( msg.data().begin(),
                                                    msg.data().end() );
                                ByteBuffer responseBB;

                                m_handler( m_iServiceId, m_iInstanceId,
                                           m_iMethodId,
                                           static_cast< const void* >(
                                               &request ),
                                           static_cast< void* >(
                                               &responseBB ) );

                                responseBytes = ::std::move( responseBB );
                            }
                        } catch ( const ::std::exception& e ) {
                            status = static_cast< Int32 >(
                                ComErrc::kInternal );
                            LAP_COM_LOG_ERROR
                                << "Method handler exception: "
                                << e.what();
                        } catch ( ... ) {
                            status = static_cast< Int32 >(
                                ComErrc::kInternal );
                            LAP_COM_LOG_ERROR
                                << "Method handler threw unknown exception";
                        }
                    } else {
                        status = static_cast< Int32 >(
                            ComErrc::kServiceNotAvailable );
                    }

                    // Build response message — echo the request_id
                    DdsPayload respMsg;
                    respMsg.request_id( msg.request_id() );

                    // Encode status at front of payload (4 bytes LE)
                    ByteBuffer payload;
                    payload.reserve( 4 + responseBytes.size() );
                    payload.push_back( static_cast< UInt8 >(
                        status & 0xFF ) );
                    payload.push_back( static_cast< UInt8 >(
                        ( status >> 8 ) & 0xFF ) );
                    payload.push_back( static_cast< UInt8 >(
                        ( status >> 16 ) & 0xFF ) );
                    payload.push_back( static_cast< UInt8 >(
                        ( status >> 24 ) & 0xFF ) );
                    payload.insert( payload.end(),
                                    responseBytes.begin(),
                                    responseBytes.end() );
                    respMsg.data( ::std::move( payload ) );

                    if ( m_pResponseWriter != nullptr ) {
                        ReturnCode_t writeRet =
                            m_pResponseWriter->write( &respMsg );
                        if ( writeRet != RETCODE_OK ) {
                            LAP_COM_LOG_WARN
                                << "DDS method response write failed: "
                                << writeRet;
                        } else {
                            m_metrics.messagesSent++;
                            m_metrics.bytesSent += respMsg.data().size();
                        }
                    }
                }
            }

        private:
            MethodHandler       m_handler;
            DataWriter*         m_pResponseWriter;
            UInt64              m_iServiceId;
            UInt64              m_iInstanceId;
            UInt32              m_iMethodId;
            TransportMetrics&   m_metrics;
            Size                m_requestSize;
            Size                m_responseSize;
        };
    } // anonymous namespace

    // ====================================================================
    // Constructor
    // ====================================================================

    CDdsMethodManager::CDdsMethodManager(
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
    // Method Communication — Client Side
    // ====================================================================

    Result< void > CDdsMethodManager::CallMethod(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 methodId, const void* pRequest,
        void* pResponse,
        Size requestSize, Size responseSize ) noexcept
    {
        if ( m_pParticipant == nullptr || m_pPublisher == nullptr ||
             m_pSubscriber == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        if ( pRequest == nullptr || pResponse == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kInvalidArgument ) );
        }

        const auto key = CDdsCodec::MakeMethodKey( serviceId, instanceId, methodId );
        auto& pCallMutex = m_mapMethodMutexes[key];
        if ( !pCallMutex ) {
            pCallMutex = MakeUnique< Mutex > ();
        }

        UniqueLock callLock( *pCallMutex );

        const String reqTopicName = CDdsCodec::MakeMethodTopicName(
            serviceId, instanceId, methodId, true );
        const String repTopicName = CDdsCodec::MakeMethodTopicName(
            serviceId, instanceId, methodId, false );

        // Get or open request channel (owns the request writer)
        auto& reqChannel = m_mapChannels[reqTopicName];
        if ( !reqChannel.IsOpen() ) {
            if ( !reqChannel.Open( m_pParticipant, m_pPublisher, m_pSubscriber,
                                   m_typeSupport, reqTopicName, m_config ) ) {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }
        }

        auto* pReqWriter = reqChannel.GetOrCreateWriter();
        if ( pReqWriter == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kCommunicationFailure ) );
        }

        // Get or open reply channel (owns the reply reader)
        auto& repChannel = m_mapChannels[repTopicName];
        if ( !repChannel.IsOpen() ) {
            if ( !repChannel.Open( m_pParticipant, m_pPublisher, m_pSubscriber,
                                   m_typeSupport, repTopicName, m_config ) ) {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }
        }

        auto* pRepReader = repChannel.GetFirstReader();
        if ( pRepReader == nullptr ) {
            pRepReader = repChannel.AddReader();  // polling reader, no listener
            if ( pRepReader == nullptr ) {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }
        }

        if ( pReqWriter == nullptr || pRepReader == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kCommunicationFailure ) );
        }

        // Wait briefly for request/response matching.
        // 500 ms covers freshly-created per-method channels whose endpoint
        // discovery may lag behind an already-warmed SayHello channel.
        // Cross-process matching can take longer; the response-poll window
        // below (3 s + possible 1.5 s extension) covers remaining latency.
        Bool matchedBeforeSend = false;
        {
            const auto matchDeadline =
                ::std::chrono::steady_clock::now() + ::std::chrono::milliseconds( 500 );
            PublicationMatchedStatus pubStatus;
            SubscriptionMatchedStatus subStatus;

            while ( ::std::chrono::steady_clock::now() < matchDeadline ) {
                pReqWriter->get_publication_matched_status( pubStatus );
                pRepReader->get_subscription_matched_status( subStatus );
                if ( pubStatus.current_count > 0 && subStatus.current_count > 0 ) {
                    matchedBeforeSend = true;
                    break;
                }
                ::std::this_thread::sleep_for( ::std::chrono::milliseconds( 20 ) );
            }
            // Final check
            pReqWriter->get_publication_matched_status( pubStatus );
            pRepReader->get_subscription_matched_status( subStatus );
        }

        // Build and send request message
        DdsPayload requestMsg;
        const auto token = static_cast< UInt64 > (
            ::std::chrono::duration_cast< ::std::chrono::nanoseconds > (
                ::std::chrono::steady_clock::now().time_since_epoch() ).count() );
        requestMsg.request_id( token );

        // ── Request serialization: Phase 3 (typed) vs Phase 1 (ByteBuffer) ──
        if ( requestSize > 0 ) {
            // Phase 3: pRequest points to raw typed data (e.g. tuple<Int32,Int32>)
            requestMsg.data( ::std::vector< uint8_t >(
                reinterpret_cast< const uint8_t* >( pRequest ),
                reinterpret_cast< const uint8_t* >( pRequest ) + requestSize ) );
        } else {
            // Phase 1: pRequest is ByteBuffer*
            const auto& request = *static_cast< const ByteBuffer* >( pRequest );
            requestMsg.data(
                ::std::vector< uint8_t > ( request.begin(), request.end() ) );
        }

        auto writeRet = pReqWriter->write( &requestMsg );
        if ( writeRet != RETCODE_OK ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kCommunicationFailure ) );
        }

        m_metrics.messagesSent++;
        m_metrics.bytesSent += requestMsg.data().size();

        // Poll for response (3 s covers both in-process fast path and
        // cross-process DDS endpoint discovery latency).
        //
        // Retry logic: with VOLATILE durability, if the request was written
        // before endpoint matching completed, the server never receives it.
        // When matching is detected during polling, re-send the request.
        auto deadline =
            ::std::chrono::steady_clock::now() + ::std::chrono::milliseconds( 3000 );
        DdsPayload responseMsg;
        SampleInfo sampleInfo;
        Bool retriedSend = false;

        while ( ::std::chrono::steady_clock::now() < deadline ) {
            auto ret = pRepReader->take_next_sample( &responseMsg, &sampleInfo );
            if ( ret != RETCODE_OK ) {
                // No response yet — check if we need to re-send
                if ( !matchedBeforeSend && !retriedSend ) {
                    PublicationMatchedStatus pubStatus;
                    pReqWriter->get_publication_matched_status( pubStatus );
                    if ( pubStatus.current_count > 0 ) {
                        // Matching happened after our first write; re-send.
                        // Extend the deadline so the server has ≥1500 ms to
                        // respond even when matching was detected late.
                        const auto minDeadline =
                            ::std::chrono::steady_clock::now()
                            + ::std::chrono::milliseconds( 1500 );
                        if ( minDeadline > deadline ) {
                            deadline = minDeadline;
                        }
                        pReqWriter->write( &requestMsg );
                        m_metrics.messagesSent++;
                        m_metrics.bytesSent += requestMsg.data().size();
                        retriedSend = true;
                    }
                }
                ::std::this_thread::sleep_for( ::std::chrono::milliseconds( 10 ) );
                continue;
            }

            if ( !sampleInfo.valid_data ) {
                continue;
            }

            if ( responseMsg.request_id() != token ) {
                continue;
            }

            ByteBuffer payload(
                responseMsg.data().begin(), responseMsg.data().end() );

            if ( payload.size() < 4 ) {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }

            // Decode status from first 4 bytes (LE)
            Int32 status = static_cast< Int32 > ( payload[0] )
                         | ( static_cast< Int32 > ( payload[1] ) << 8 )
                         | ( static_cast< Int32 > ( payload[2] ) << 16 )
                         | ( static_cast< Int32 > ( payload[3] ) << 24 );

            if ( status != 0 ) {
                return Result< void >::FromError(
                    MakeErrorCode( static_cast< ComErrc > ( status ) ) );
            }

            payload.erase( payload.begin(), payload.begin() + 4 );

            m_metrics.messagesReceived++;
            m_metrics.bytesReceived += payload.size();

            // ── Response deserialization: Phase 3 vs Phase 1 ──
            if ( responseSize > 0 && payload.size() >= responseSize ) {
                // Phase 3: memcpy into caller's typed response buffer
                ::std::memcpy( pResponse, payload.data(), responseSize );
            } else {
                // Phase 1: pResponse is ByteBuffer*
                *static_cast< ByteBuffer* >( pResponse ) = ::std::move( payload );
            }
            return Result< void >::FromValue();
        }

        return Result< void >::FromError( MakeErrorCode( ComErrc::kTimeout ) );
    }

    // ====================================================================
    // Method Communication — Server Side
    // ====================================================================

    Result< void > CDdsMethodManager::RegisterMethod(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 methodId, MethodHandler handler,
        Size requestSize, Size responseSize ) noexcept
    {
        if ( m_pParticipant == nullptr || m_pPublisher == nullptr ||
             m_pSubscriber == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        const auto key = CDdsCodec::MakeMethodKey( serviceId, instanceId, methodId );
        m_mapMethodHandlers[key] = handler;

        const String reqTopicName = CDdsCodec::MakeMethodTopicName(
            serviceId, instanceId, methodId, true );
        const String repTopicName = CDdsCodec::MakeMethodTopicName(
            serviceId, instanceId, methodId, false );

        // Get or open reply channel (owns the response writer)
        auto& repChannel = m_mapChannels[repTopicName];
        if ( !repChannel.IsOpen() ) {
            if ( !repChannel.Open( m_pParticipant, m_pPublisher, m_pSubscriber,
                                   m_typeSupport, repTopicName, m_config ) ) {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }
        }

        auto* pRespWriter = repChannel.GetOrCreateWriter();
        if ( pRespWriter == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kCommunicationFailure ) );
        }

        // Get or open request channel (owns the request reader + listener)
        auto& reqChannel = m_mapChannels[reqTopicName];
        if ( !reqChannel.IsOpen() ) {
            if ( !reqChannel.Open( m_pParticipant, m_pPublisher, m_pSubscriber,
                                   m_typeSupport, reqTopicName, m_config ) ) {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }
        }

        if ( reqChannel.ReaderCount() == 0 ) {
            auto pListener = MakeUnique< DdsMethodReaderListener > (
                handler, pRespWriter, serviceId, instanceId, methodId,
                m_metrics, requestSize, responseSize );

            auto* pReqReader = reqChannel.AddReader(
                ::std::move( pListener ),
                StatusMask::all() );

            if ( pReqReader == nullptr ) {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }
        }

        return Result< void >::FromValue();
    }

    // ====================================================================
    // Field Communication
    // ====================================================================

    Result< void > CDdsMethodManager::GetField(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 fieldId, void* pOutValue ) noexcept
    {
        const UInt32 getterMethodId = fieldId | 0x10000U;
        ByteBuffer emptyRequest;
        return CallMethod( serviceId, instanceId, getterMethodId,
                           static_cast< const void* >( &emptyRequest ),
                           pOutValue );
    }

    Result< void > CDdsMethodManager::SetField(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 fieldId, const void* pValue ) noexcept
    {
        const UInt32 setterMethodId = fieldId | 0x20000U;
        ByteBuffer responseIgnored;
        return CallMethod( serviceId, instanceId, setterMethodId,
                           pValue,
                           static_cast< void* >( &responseIgnored ) );
    }

    // ====================================================================
    // Lifecycle
    // ====================================================================

    void CDdsMethodManager::Shutdown() noexcept
    {
        // Close all method channels (deletes readers, writers, topics)
        for ( auto& [topicName, channel] : m_mapChannels )
        {
            channel.Close();
        }
        m_mapChannels.clear();
        m_mapMethodHandlers.clear();
        m_mapMethodMutexes.clear();
    }

} // namespace binding
} // namespace com
} // namespace lap
