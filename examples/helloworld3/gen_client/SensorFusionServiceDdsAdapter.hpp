/**
 * @file        SensorFusionServiceDdsAdapter.hpp
 * @author      Aii
 * @brief       Auto-generated DDS type adapter registration for SensorFusionService
 * @date        2026/02/09
 * @details     Auto-generated from examples/helloworld3/HelloWorld3.fidl by lap-sidl-gen v1.0
 * @copyright   Copyright (c) 2026
 * @note        DO NOT EDIT — This file is auto-generated
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>Auto-generated
 * </table>
 */

#ifndef HELLOWORLD3_SENSORFUSIONSERVICEDDSADAPTER_HPP
#define HELLOWORLD3_SENSORFUSIONSERVICEDDSADAPTER_HPP

// ==================== DDS Binding Headers ====================
#include "CDdsPayload.hpp"
#include "IDdsTypeAdapter.hpp"
#include "CDdsTypeRegistry.hpp"

// ==================== fastddsgen Type Headers ====================
#include "SensorFusionService.hpp"
#include "SensorFusionServicePubSubTypes.hpp"

// ==================== Application Type Headers ====================
#include "SensorFusionServiceTypes.hpp"

// ==================== Standard Library Headers ====================
#include <cstring>
#include <cstddef>

namespace helloworld3
{

namespace dds_adapter
{

// DDS namespace alias for typed event adapters
namespace DdsTypes = ::lap::examples::helloworld3;

// ================================================================
// Event Adapters (typed CDR via fastddsgen PubSubType)
// ================================================================

/// @brief DDS type adapter for event 'SensorAlert' (elementId=0x0001)
class SensorFusionService_SensorAlert_EventAdapter final
    : public ::lap::com::binding::IDdsTypeAdapter
{
public:
    eprosima::fastdds::dds::TypeSupport GetTypeSupport() const noexcept override {
        return eprosima::fastdds::dds::TypeSupport(
            new DdsTypes::SensorAlertEventPubSubType() );
    }

    void* CreateSample( const void* pData,
                        ::lap::core::Size dataSize ) const override {
        auto* pDds = new DdsTypes::SensorAlertEvent();
        if ( pData != nullptr && dataSize > 0u ) {
            const auto& app = *static_cast< const ::helloworld3::SensorAlertEvent* >( pData );
            {
                DdsTypes::AlertInfo dds_alert;
                dds_alert.level( static_cast< DdsTypes::AlertLevel >( static_cast< ::std::int32_t >( app.alert.level ) ) );
                dds_alert.source( app.alert.source );
                dds_alert.message( app.alert.message );
                dds_alert.timestamp( app.alert.timestamp );
                pDds->alert( ::std::move( dds_alert ) );
            }
        }
        return pDds;
    }

    const void* ExtractData( const void* pSample ) const noexcept override {
        if ( pSample == nullptr ) { return nullptr; }
        const auto& dds = *static_cast< const DdsTypes::SensorAlertEvent* >( pSample );
        try {
            thread_local ::helloworld3::SensorAlertEvent appBuf;
            {
                const auto& ddsRef_alert = dds.alert();
                appBuf.alert.level = static_cast< ::helloworld3::SensorTypes::AlertLevel >( static_cast< ::std::int32_t >( ddsRef_alert.level() ) );
                appBuf.alert.source = ddsRef_alert.source();
                appBuf.alert.message = ddsRef_alert.message();
                appBuf.alert.timestamp = ddsRef_alert.timestamp();
            }
            return &appBuf;
        } catch ( ... ) {
            return nullptr;
        }
    }

    void FreeSample( void* pSample ) const noexcept override {
        delete static_cast< DdsTypes::SensorAlertEvent* >( pSample );
    }
};

/// @brief DDS type adapter for event 'PositionUpdate' (elementId=0x0002)
class SensorFusionService_PositionUpdate_EventAdapter final
    : public ::lap::com::binding::IDdsTypeAdapter
{
public:
    eprosima::fastdds::dds::TypeSupport GetTypeSupport() const noexcept override {
        return eprosima::fastdds::dds::TypeSupport(
            new DdsTypes::PositionUpdateEventPubSubType() );
    }

    void* CreateSample( const void* pData,
                        ::lap::core::Size dataSize ) const override {
        auto* pDds = new DdsTypes::PositionUpdateEvent();
        if ( pData != nullptr && dataSize > 0u ) {
            const auto& app = *static_cast< const ::helloworld3::PositionUpdateEvent* >( pData );
            {
                DdsTypes::GeoPosition dds_position;
                dds_position.latitude( app.position.latitude );
                dds_position.longitude( app.position.longitude );
                dds_position.altitude( app.position.altitude );
                pDds->position( ::std::move( dds_position ) );
            }
            pDds->heading( app.heading );
            pDds->speed( app.speed );
        }
        return pDds;
    }

    const void* ExtractData( const void* pSample ) const noexcept override {
        if ( pSample == nullptr ) { return nullptr; }
        const auto& dds = *static_cast< const DdsTypes::PositionUpdateEvent* >( pSample );
        try {
            thread_local ::helloworld3::PositionUpdateEvent appBuf;
            {
                const auto& ddsRef_position = dds.position();
                appBuf.position.latitude = ddsRef_position.latitude();
                appBuf.position.longitude = ddsRef_position.longitude();
                appBuf.position.altitude = ddsRef_position.altitude();
            }
            appBuf.heading = dds.heading();
            appBuf.speed = dds.speed();
            return &appBuf;
        } catch ( ... ) {
            return nullptr;
        }
    }

    void FreeSample( void* pSample ) const noexcept override {
        delete static_cast< DdsTypes::PositionUpdateEvent* >( pSample );
    }
};

/// @brief DDS type adapter for event 'RawTelemetry' (elementId=0x0003)
class SensorFusionService_RawTelemetry_EventAdapter final
    : public ::lap::com::binding::IDdsTypeAdapter
{
public:
    eprosima::fastdds::dds::TypeSupport GetTypeSupport() const noexcept override {
        return eprosima::fastdds::dds::TypeSupport(
            new DdsTypes::RawTelemetryEventPubSubType() );
    }

    void* CreateSample( const void* pData,
                        ::lap::core::Size dataSize ) const override {
        auto* pDds = new DdsTypes::RawTelemetryEvent();
        if ( pData != nullptr && dataSize > 0u ) {
            const auto& app = *static_cast< const ::helloworld3::RawTelemetryEvent* >( pData );
            pDds->channelId( app.channelId );
            pDds->payload( app.payload );
        }
        return pDds;
    }

    const void* ExtractData( const void* pSample ) const noexcept override {
        if ( pSample == nullptr ) { return nullptr; }
        const auto& dds = *static_cast< const DdsTypes::RawTelemetryEvent* >( pSample );
        try {
            thread_local ::helloworld3::RawTelemetryEvent appBuf;
            appBuf.channelId = dds.channelId();
            appBuf.payload = dds.payload();
            return &appBuf;
        } catch ( ... ) {
            return nullptr;
        }
    }

    void FreeSample( void* pSample ) const noexcept override {
        delete static_cast< DdsTypes::RawTelemetryEvent* >( pSample );
    }
};

// ================================================================
// Registration Function
// ================================================================

/**
 * @brief  Register all DDS adapters for SensorFusionService with CDdsTypeRegistry
 * @param  serviceId  32-bit service ID (matches kServiceId in Proxy/Skeleton)
 * @note   Call once at application startup before any DDS communication.
 *         The static adapter instances are zero-overhead singletons.
 */
inline void RegisterSensorFusionServiceDdsAdapters(
    ::lap::core::UInt64 serviceId ) noexcept
{
    static SensorFusionService_SensorAlert_EventAdapter s_sensorAlertEventAdapter;
    ::lap::com::binding::CDdsTypeRegistry::Instance()
        .RegisterAdapter( serviceId, 1u, &s_sensorAlertEventAdapter );

    static SensorFusionService_PositionUpdate_EventAdapter s_positionUpdateEventAdapter;
    ::lap::com::binding::CDdsTypeRegistry::Instance()
        .RegisterAdapter( serviceId, 2u, &s_positionUpdateEventAdapter );

    static SensorFusionService_RawTelemetry_EventAdapter s_rawTelemetryEventAdapter;
    ::lap::com::binding::CDdsTypeRegistry::Instance()
        .RegisterAdapter( serviceId, 3u, &s_rawTelemetryEventAdapter );

}

} // namespace dds_adapter

} // namespace helloworld3

#endif // HELLOWORLD3_SENSORFUSIONSERVICEDDSADAPTER_HPP
