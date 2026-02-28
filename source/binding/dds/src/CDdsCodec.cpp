/**
 * @file        CDdsCodec.cpp
 * @author      LightAP Development Team
 * @brief       DDS binding — Topic name / key generation and DDS entity factory
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Extracted from DdsHelpers.cpp
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CDdsCodec.hpp"
#include "ComTypes.hpp"

// ==================== Third-Party Headers ====================
#include <fastdds/dds/publisher/qos/DataWriterQos.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/rtps/attributes/RTPSParticipantAttributes.hpp>

// ==================== Standard Library Headers ====================
#include <sstream>

namespace lap
{
namespace com
{
namespace binding
{

    using namespace eprosima::fastdds::dds;

    // ====================================================================
    // Topic Name Generation
    // ====================================================================

    String CDdsCodec::MakeEventTopicName(
        UInt64 serviceId, UInt64 instanceId, UInt32 eventId ) noexcept
    {
        ::std::ostringstream oss;
        oss << "lap/com/" << ::std::hex
            << serviceId << "/" << instanceId << "/" << eventId;
        return oss.str();
    }

    String CDdsCodec::MakeMethodTopicName(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 methodId, Bool bIsRequest ) noexcept
    {
        ::std::ostringstream oss;
        oss << "LapComMethod_" << ::std::hex
            << serviceId << "_" << instanceId << "_" << methodId
            << ( bIsRequest ? "_req" : "_rep" );
        return oss.str();
    }

    // ====================================================================
    // Key Generation
    // ====================================================================

    String CDdsCodec::MakeEventKey(
        UInt64 serviceId, UInt64 instanceId, UInt32 eventId ) noexcept
    {
        ::std::ostringstream oss;
        oss << ::std::hex << serviceId << "_" << instanceId << "_" << eventId;
        return oss.str();
    }

    String CDdsCodec::MakeMethodKey(
        UInt64 serviceId, UInt64 instanceId, UInt32 methodId ) noexcept
    {
        ::std::ostringstream oss;
        oss << ::std::hex << serviceId << "_" << instanceId << "_" << methodId;
        return oss.str();
    }

    // ====================================================================
    // DDS Entity Factory
    // ====================================================================

    DataWriter* CDdsCodec::CreateWriter(
        Publisher* pPublisher, Topic* pTopic,
        const DdsConfig& config ) noexcept
    {
        DataWriterQos wqos = DATAWRITER_QOS_DEFAULT;

        if ( config.m_bReliable ) {
            wqos.reliability().kind = RELIABLE_RELIABILITY_QOS;
        } else {
            wqos.reliability().kind = BEST_EFFORT_RELIABILITY_QOS;
        }

        if ( config.m_bTransientLocal ) {
            wqos.durability().kind = TRANSIENT_LOCAL_DURABILITY_QOS;
        }

        wqos.history().kind  = KEEP_LAST_HISTORY_QOS;
        wqos.history().depth = config.m_iHistoryDepth;

        // Apply extended QoS (deadline, lifespan, resource limits)
        ApplyWriterQos( wqos, config );

        DataWriter* pWriter = pPublisher->create_datawriter( pTopic, wqos );
        if ( pWriter == nullptr ) {
            LAP_COM_LOG_ERROR << "Failed to create DataWriter";
        }

        return pWriter;
    }

    DataReader* CDdsCodec::CreateReader(
        Subscriber* pSubscriber, Topic* pTopic,
        const DdsConfig& config,
        DataReaderListener* pListener,
        StatusMask mask ) noexcept
    {
        DataReaderQos rqos = DATAREADER_QOS_DEFAULT;

        if ( config.m_bReliable ) {
            rqos.reliability().kind = RELIABLE_RELIABILITY_QOS;
        } else {
            rqos.reliability().kind = BEST_EFFORT_RELIABILITY_QOS;
        }

        if ( config.m_bTransientLocal ) {
            rqos.durability().kind = TRANSIENT_LOCAL_DURABILITY_QOS;
        }

        rqos.history().kind  = KEEP_LAST_HISTORY_QOS;
        rqos.history().depth = config.m_iHistoryDepth;

        // Apply extended QoS (deadline, lifespan, resource limits)
        ApplyReaderQos( rqos, config );

        DataReader* pReader = pSubscriber->create_datareader(
            pTopic, rqos, pListener, mask );

        if ( pReader == nullptr ) {
            LAP_COM_LOG_ERROR << "Failed to create DataReader";
        }

        return pReader;
    }

    // ====================================================================
    // FastDDS QoS Helpers
    // ====================================================================

    void CDdsCodec::ApplyWriterQos(
        DataWriterQos& wqos,
        const DdsConfig& config ) noexcept
    {
        // Deadline QoS
        if ( config.m_iDeadlinePeriodMs > 0 )
        {
            wqos.deadline().period.seconds =
                static_cast< int32_t >( config.m_iDeadlinePeriodMs / 1000 );
            wqos.deadline().period.nanosec =
                ( config.m_iDeadlinePeriodMs % 1000 ) * 1000000U;
        }

        // Lifespan QoS
        if ( config.m_iLifespanPeriodMs > 0 )
        {
            wqos.lifespan().duration.seconds =
                static_cast< int32_t >( config.m_iLifespanPeriodMs / 1000 );
            wqos.lifespan().duration.nanosec =
                ( config.m_iLifespanPeriodMs % 1000 ) * 1000000U;
        }

        // Resource limits — max payload
        if ( config.m_iMaxPayloadSize > 0 )
        {
            wqos.endpoint().history_memory_policy =
                eprosima::fastdds::rtps::PREALLOCATED_WITH_REALLOC_MEMORY_MODE;
        }
    }

    void CDdsCodec::ApplyReaderQos(
        DataReaderQos& rqos,
        const DdsConfig& config ) noexcept
    {
        // Deadline QoS
        if ( config.m_iDeadlinePeriodMs > 0 )
        {
            rqos.deadline().period.seconds =
                static_cast< int32_t >( config.m_iDeadlinePeriodMs / 1000 );
            rqos.deadline().period.nanosec =
                ( config.m_iDeadlinePeriodMs % 1000 ) * 1000000U;
        }

        // Lifespan QoS
        if ( config.m_iLifespanPeriodMs > 0 )
        {
            rqos.lifespan().duration.seconds =
                static_cast< int32_t >( config.m_iLifespanPeriodMs / 1000 );
            rqos.lifespan().duration.nanosec =
                ( config.m_iLifespanPeriodMs % 1000 ) * 1000000U;
        }

        // Resource limits — max payload
        if ( config.m_iMaxPayloadSize > 0 )
        {
            rqos.endpoint().history_memory_policy =
                eprosima::fastdds::rtps::PREALLOCATED_WITH_REALLOC_MEMORY_MODE;
        }
    }

} // namespace binding
} // namespace com
} // namespace lap
