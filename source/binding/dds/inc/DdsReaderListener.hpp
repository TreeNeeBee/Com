/**
 * @file        DdsReaderListener.hpp
 * @author      LightAP Development Team
 * @brief       DDS DataReader listener for async event reception
 * @date        2026/02/06
 * @copyright   Copyright (c) 2026
 *
 * @details     Listens for incoming DDS data samples and dispatches
 *              to the EventCallback registered by SubscribeEvent().
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/23  <td>1.0      <td>LightAP Team    <td>Initial (in DdsBinding.hpp)
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style refactor per code_rules.md
 * <tr><td>2026/02/06  <td>2.1      <td>Aii             <td>Extracted into separate header + .cpp
 * </table>
 */

#ifndef LAP_COM_DDS_READER_LISTENER_HPP
#define LAP_COM_DDS_READER_LISTENER_HPP

// ==================== Project-Internal Headers ====================
#include "BindingTypes.hpp"
#include "ITransportBinding.hpp"
#include "IDdsTypeAdapter.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/CTypedef.hpp>

// ==================== Third-Party Headers ====================
#include <fastdds/dds/subscriber/DataReaderListener.hpp>

namespace lap
{
namespace com
{
namespace binding
{

    using lap::core::UInt32;
    using lap::core::UInt64;

    // ====================================================================
    // DDS Reader Listener
    // ====================================================================

    /**
     * @brief   DataReader Listener for async event reception
     *
     * @details DdsPayload no longer carries addressing fields; they are
     *          captured at subscription time and stored in the listener.
     *
     *          If a type adapter is registered for this (serviceId, eventId),
     *          the listener uses the adapter's ExtractData for zero-copy
     *          deserialization.  Otherwise, the DdsPayload fallback path
     *          is used.
     */
    class DdsReaderListener : public eprosima::fastdds::dds::DataReaderListener
    {
    public:
        DdsReaderListener( EventCallback callback,
                           UInt64 serviceId,
                           UInt64 instanceId,
                           UInt32 eventId,
                           TransportMetrics& metrics,
                           const IDdsTypeAdapter* pAdapter = nullptr ) noexcept
            : m_callback( ::std::move( callback ) )
            , m_iServiceId( serviceId )
            , m_iInstanceId( instanceId )
            , m_iEventId( eventId )
            , m_metrics( metrics )
            , m_pAdapter( pAdapter )
        {}

        void on_data_available(
            eprosima::fastdds::dds::DataReader* reader ) override;

        void on_subscription_matched(
            eprosima::fastdds::dds::DataReader* reader,
            const eprosima::fastdds::dds::SubscriptionMatchedStatus& info ) override;

    private:
        EventCallback               m_callback;
        UInt64                      m_iServiceId;
        UInt64                      m_iInstanceId;
        UInt32                      m_iEventId;
        TransportMetrics&           m_metrics;
        const IDdsTypeAdapter*      m_pAdapter;     ///< nullptr → DdsPayload fallback
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_DDS_READER_LISTENER_HPP
