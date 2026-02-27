/**
 * @file        DdsReaderListener.cpp
 * @author      LightAP Development Team
 * @brief       DDS DataReader listener — event reception implementation
 * @date        2026/02/06
 * @copyright   Copyright (c) 2026
 *
 * @details     Implements on_data_available and on_subscription_matched
 *              for async event dispatching.
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/23  <td>1.0      <td>LightAP Team    <td>Initial (in DdsBinding.cpp)
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style refactor per code_rules.md
 * <tr><td>2026/02/06  <td>2.1      <td>Aii             <td>Extracted into separate .cpp
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "DdsReaderListener.hpp"
#include "CDdsPayload.hpp"
#include "ComTypes.hpp"

// ==================== Third-Party Headers ====================
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>

namespace lap
{
namespace com
{
namespace binding
{

    using namespace eprosima::fastdds::dds;

    // ====================================================================
    // DdsReaderListener Implementation
    // ====================================================================

    void DdsReaderListener::on_data_available( DataReader* reader )
    {
        if ( m_pAdapter != nullptr ) {
            // ── Typed adapter path: zero-copy extraction ──
            // Allocate an empty sample of the adapter's type for reading
            void* pSample = m_pAdapter->CreateSample( nullptr, 0 );
            if ( pSample == nullptr ) {
                LAP_COM_LOG_ERROR << "DDS adapter CreateSample(nullptr) failed";
                return;
            }

            SampleInfo info;
            auto ret = reader->take_next_sample( pSample, &info );
            if ( ret == RETCODE_OK ) {
                if ( info.valid_data ) {
                    const void* pAppData = m_pAdapter->ExtractData( pSample );
                    if ( pAppData != nullptr ) {
                        m_callback( m_iServiceId, m_iInstanceId,
                                    m_iEventId, pAppData );
                        m_metrics.messagesReceived++;
                    }
                }
            }
            m_pAdapter->FreeSample( pSample );

        } else {
            // ── DdsPayload fallback path ──
            DdsPayload msg;
            SampleInfo info;

            if ( reader->take_next_sample( &msg, &info ) == RETCODE_OK ) {
                if ( info.valid_data ) {
                    ByteBuffer data( msg.data().begin(), msg.data().end() );

                    m_callback( m_iServiceId, m_iInstanceId,
                                m_iEventId,
                                static_cast< const void* >( &data ) );

                    m_metrics.messagesReceived++;
                    m_metrics.bytesReceived += data.size();
                }
            }
        }
    }

    void DdsReaderListener::on_subscription_matched(
        DataReader* reader [[maybe_unused]],
        const SubscriptionMatchedStatus& info )
    {
        if ( info.current_count_change == 1 ) {
            LAP_COM_LOG_INFO << "DDS subscriber matched with publisher (total="
                             << info.current_count << ")";
        } else if ( info.current_count_change == -1 ) {
            LAP_COM_LOG_INFO << "DDS subscriber unmatched from publisher (remaining="
                             << info.current_count << ")";
        }
    }

} // namespace binding
} // namespace com
} // namespace lap
