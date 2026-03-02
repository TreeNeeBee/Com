/**
 * @file        SensorFusionServiceTypes.hpp
 * @author      Aii
 * @brief       Auto-generated types for SensorFusionService service
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

#ifndef HELLOWORLD3_SENSORFUSIONSERVICETYPES_HPP
#define HELLOWORLD3_SENSORFUSIONSERVICETYPES_HPP

// ==================== Cross-Module Headers ====================
#include <com/ComTypes.hpp>
#include <core/CFuture.hpp>

// ==================== Serialization Headers ====================
#include "serialization/CSerializationTraits.hpp"

// ==================== Standard Library Headers ====================
#include <vector>

namespace helloworld3
{
// ==================== LAP/COM Type Aliases ====================
using lap::core::Result;
using lap::core::Optional;
using lap::core::String;
using lap::core::StringView;
using lap::core::Bool;
using lap::core::Char;
using lap::core::UInt8;
using lap::core::UInt16;
using lap::core::UInt32;
using lap::core::UInt64;
using lap::core::Int32;
using lap::core::Int64;
using lap::core::Float;
using lap::core::Double;
using ::lap::core::Future;
using ::lap::com::MethodCallProcessingMode;
using ::lap::com::ComErrc;
using ::lap::com::MakeErrorCode;
using ::lap::com::ServiceState;
using ByteArray = ::std::vector< UInt8 >;


    // ==================== Type Collection: SensorTypes ====================

    namespace SensorTypes
    {

    /**
     * @brief SensorType enumeration
     */
    enum class SensorType : Int32 {
        kLidar                  = 0,
        kCamera                 = 1,
        kRadar                  = 2,
        kUltrasonic             = 3,
        kImu                    = 4
    };

    /**
     * @brief AlertLevel enumeration
     */
    enum class AlertLevel : Int32 {
        kNone                   = 0,
        kInfo                   = 1,
        kWarning                = 2,
        kCritical               = 3
    };

    /**
     * @brief DeviceState enumeration
     */
    enum class DeviceState : Int32 {
        kOffline                = 0,
        kInitializing           = 1,
        kOnline                 = 2,
        kError                  = 3,
        kMaintenance            = 4
    };

    /**
     * @brief GeoPosition data structure
     */
    struct GeoPosition {
        Double                      latitude;
        Double                      longitude;
        Double                      altitude;
    };

    /**
     * @brief SensorReading data structure
     */
    struct SensorReading {
        UInt32                      sensorId;
        Double                      value;
        UInt64                      timestamp;
        Bool                        valid;
    };

    /**
     * @brief AlertInfo data structure
     */
    struct AlertInfo {
        AlertLevel                  level;
        String                      source;
        String                      message;
        UInt64                      timestamp;
    };

    /**
     * @brief DiagnosticReport data structure
     */
    struct DiagnosticReport {
        UInt32                      deviceId;
        DeviceState                 state;
        UInt32                      errorCount;
        Double                      uptime;
        String                      firmwareVersion;
    };

    using Angle = Double;

    using RawPayload = ::std::vector< UInt8 >;

    // --- ADL serialization (in-namespace for ADL) ---

    inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const GeoPosition& v ) noexcept {
        auto r_latitude = ::lap::com::serialization::SerializeValue( s, v.latitude );
        if ( !r_latitude.HasValue() ) { return r_latitude; }
        auto r_longitude = ::lap::com::serialization::SerializeValue( s, v.longitude );
        if ( !r_longitude.HasValue() ) { return r_longitude; }
        auto r_altitude = ::lap::com::serialization::SerializeValue( s, v.altitude );
        if ( !r_altitude.HasValue() ) { return r_altitude; }
        return Result< void >::FromValue();
    }

    inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, GeoPosition& v ) noexcept {
        auto r_latitude = ::lap::com::serialization::DeserializeValue( d, v.latitude );
        if ( !r_latitude.HasValue() ) { return r_latitude; }
        auto r_longitude = ::lap::com::serialization::DeserializeValue( d, v.longitude );
        if ( !r_longitude.HasValue() ) { return r_longitude; }
        auto r_altitude = ::lap::com::serialization::DeserializeValue( d, v.altitude );
        if ( !r_altitude.HasValue() ) { return r_altitude; }
        return Result< void >::FromValue();
    }

    inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const SensorReading& v ) noexcept {
        auto r_sensorId = ::lap::com::serialization::SerializeValue( s, v.sensorId );
        if ( !r_sensorId.HasValue() ) { return r_sensorId; }
        auto r_value = ::lap::com::serialization::SerializeValue( s, v.value );
        if ( !r_value.HasValue() ) { return r_value; }
        auto r_timestamp = ::lap::com::serialization::SerializeValue( s, v.timestamp );
        if ( !r_timestamp.HasValue() ) { return r_timestamp; }
        auto r_valid = ::lap::com::serialization::SerializeValue( s, v.valid );
        if ( !r_valid.HasValue() ) { return r_valid; }
        return Result< void >::FromValue();
    }

    inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, SensorReading& v ) noexcept {
        auto r_sensorId = ::lap::com::serialization::DeserializeValue( d, v.sensorId );
        if ( !r_sensorId.HasValue() ) { return r_sensorId; }
        auto r_value = ::lap::com::serialization::DeserializeValue( d, v.value );
        if ( !r_value.HasValue() ) { return r_value; }
        auto r_timestamp = ::lap::com::serialization::DeserializeValue( d, v.timestamp );
        if ( !r_timestamp.HasValue() ) { return r_timestamp; }
        auto r_valid = ::lap::com::serialization::DeserializeValue( d, v.valid );
        if ( !r_valid.HasValue() ) { return r_valid; }
        return Result< void >::FromValue();
    }

    inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const AlertInfo& v ) noexcept {
        auto r_level = ::lap::com::serialization::SerializeValue( s, v.level );
        if ( !r_level.HasValue() ) { return r_level; }
        auto r_source = ::lap::com::serialization::SerializeValue( s, v.source );
        if ( !r_source.HasValue() ) { return r_source; }
        auto r_message = ::lap::com::serialization::SerializeValue( s, v.message );
        if ( !r_message.HasValue() ) { return r_message; }
        auto r_timestamp = ::lap::com::serialization::SerializeValue( s, v.timestamp );
        if ( !r_timestamp.HasValue() ) { return r_timestamp; }
        return Result< void >::FromValue();
    }

    inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, AlertInfo& v ) noexcept {
        auto r_level = ::lap::com::serialization::DeserializeValue( d, v.level );
        if ( !r_level.HasValue() ) { return r_level; }
        auto r_source = ::lap::com::serialization::DeserializeValue( d, v.source );
        if ( !r_source.HasValue() ) { return r_source; }
        auto r_message = ::lap::com::serialization::DeserializeValue( d, v.message );
        if ( !r_message.HasValue() ) { return r_message; }
        auto r_timestamp = ::lap::com::serialization::DeserializeValue( d, v.timestamp );
        if ( !r_timestamp.HasValue() ) { return r_timestamp; }
        return Result< void >::FromValue();
    }

    inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const DiagnosticReport& v ) noexcept {
        auto r_deviceId = ::lap::com::serialization::SerializeValue( s, v.deviceId );
        if ( !r_deviceId.HasValue() ) { return r_deviceId; }
        auto r_state = ::lap::com::serialization::SerializeValue( s, v.state );
        if ( !r_state.HasValue() ) { return r_state; }
        auto r_errorCount = ::lap::com::serialization::SerializeValue( s, v.errorCount );
        if ( !r_errorCount.HasValue() ) { return r_errorCount; }
        auto r_uptime = ::lap::com::serialization::SerializeValue( s, v.uptime );
        if ( !r_uptime.HasValue() ) { return r_uptime; }
        auto r_firmwareVersion = ::lap::com::serialization::SerializeValue( s, v.firmwareVersion );
        if ( !r_firmwareVersion.HasValue() ) { return r_firmwareVersion; }
        return Result< void >::FromValue();
    }

    inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, DiagnosticReport& v ) noexcept {
        auto r_deviceId = ::lap::com::serialization::DeserializeValue( d, v.deviceId );
        if ( !r_deviceId.HasValue() ) { return r_deviceId; }
        auto r_state = ::lap::com::serialization::DeserializeValue( d, v.state );
        if ( !r_state.HasValue() ) { return r_state; }
        auto r_errorCount = ::lap::com::serialization::DeserializeValue( d, v.errorCount );
        if ( !r_errorCount.HasValue() ) { return r_errorCount; }
        auto r_uptime = ::lap::com::serialization::DeserializeValue( d, v.uptime );
        if ( !r_uptime.HasValue() ) { return r_uptime; }
        auto r_firmwareVersion = ::lap::com::serialization::DeserializeValue( d, v.firmwareVersion );
        if ( !r_firmwareVersion.HasValue() ) { return r_firmwareVersion; }
        return Result< void >::FromValue();
    }

    inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const SensorType& v ) noexcept {
        return s.Serialize( static_cast< Int32 >( v ) );
    }

    inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, SensorType& v ) noexcept {
        Int32 tmp = 0;
        auto r = d.Deserialize( tmp );
        if ( r.HasValue() ) { v = static_cast< SensorType >( tmp ); }
        return r;
    }

    inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const AlertLevel& v ) noexcept {
        return s.Serialize( static_cast< Int32 >( v ) );
    }

    inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, AlertLevel& v ) noexcept {
        Int32 tmp = 0;
        auto r = d.Deserialize( tmp );
        if ( r.HasValue() ) { v = static_cast< AlertLevel >( tmp ); }
        return r;
    }

    inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const DeviceState& v ) noexcept {
        return s.Serialize( static_cast< Int32 >( v ) );
    }

    inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, DeviceState& v ) noexcept {
        Int32 tmp = 0;
        auto r = d.Deserialize( tmp );
        if ( r.HasValue() ) { v = static_cast< DeviceState >( tmp ); }
        return r;
    }

    } // namespace SensorTypes

    /**
     * @brief Output type for method CalibrateDevice
     */
    struct CalibrateDeviceOutput {
        Bool                        success;
        String                      message;
    };

    /**
     * @brief Event data for broadcast SensorAlert
     * @note [SWS_CM_00700] — Event communication
     */
    struct SensorAlertEvent {
        SensorTypes::AlertInfo      alert;
    };

    /**
     * @brief Event data for broadcast PositionUpdate
     * @note [SWS_CM_00700] — Event communication
     */
    struct PositionUpdateEvent {
        SensorTypes::GeoPosition    position;
        SensorTypes::Angle          heading;
        Double                      speed;
    };

    /**
     * @brief Event data for broadcast RawTelemetry
     * @note [SWS_CM_00700] — Event communication
     */
    struct RawTelemetryEvent {
        UInt32                      channelId;
        ::std::vector< UInt8 >      payload;
    };


    // ==================== ADL Serialization Traits ====================
    // Required by CSerializationTraits.hpp for non-primitive types

    inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const SensorAlertEvent& v ) noexcept {
        auto r_alert = ::lap::com::serialization::SerializeValue( s, v.alert );
        if ( !r_alert.HasValue() ) { return r_alert; }
        return Result< void >::FromValue();
    }

    inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, SensorAlertEvent& v ) noexcept {
        auto r_alert = ::lap::com::serialization::DeserializeValue( d, v.alert );
        if ( !r_alert.HasValue() ) { return r_alert; }
        return Result< void >::FromValue();
    }

    inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const PositionUpdateEvent& v ) noexcept {
        auto r_position = ::lap::com::serialization::SerializeValue( s, v.position );
        if ( !r_position.HasValue() ) { return r_position; }
        auto r_heading = ::lap::com::serialization::SerializeValue( s, v.heading );
        if ( !r_heading.HasValue() ) { return r_heading; }
        auto r_speed = ::lap::com::serialization::SerializeValue( s, v.speed );
        if ( !r_speed.HasValue() ) { return r_speed; }
        return Result< void >::FromValue();
    }

    inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, PositionUpdateEvent& v ) noexcept {
        auto r_position = ::lap::com::serialization::DeserializeValue( d, v.position );
        if ( !r_position.HasValue() ) { return r_position; }
        auto r_heading = ::lap::com::serialization::DeserializeValue( d, v.heading );
        if ( !r_heading.HasValue() ) { return r_heading; }
        auto r_speed = ::lap::com::serialization::DeserializeValue( d, v.speed );
        if ( !r_speed.HasValue() ) { return r_speed; }
        return Result< void >::FromValue();
    }

    inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const RawTelemetryEvent& v ) noexcept {
        auto r_channelId = ::lap::com::serialization::SerializeValue( s, v.channelId );
        if ( !r_channelId.HasValue() ) { return r_channelId; }
        auto r_payload = ::lap::com::serialization::SerializeValue( s, v.payload );
        if ( !r_payload.HasValue() ) { return r_payload; }
        return Result< void >::FromValue();
    }

    inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, RawTelemetryEvent& v ) noexcept {
        auto r_channelId = ::lap::com::serialization::DeserializeValue( d, v.channelId );
        if ( !r_channelId.HasValue() ) { return r_channelId; }
        auto r_payload = ::lap::com::serialization::DeserializeValue( d, v.payload );
        if ( !r_payload.HasValue() ) { return r_payload; }
        return Result< void >::FromValue();
    }

    inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const CalibrateDeviceOutput& v ) noexcept {
        auto r_success = ::lap::com::serialization::SerializeValue( s, v.success );
        if ( !r_success.HasValue() ) { return r_success; }
        auto r_message = ::lap::com::serialization::SerializeValue( s, v.message );
        if ( !r_message.HasValue() ) { return r_message; }
        return Result< void >::FromValue();
    }

    inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, CalibrateDeviceOutput& v ) noexcept {
        auto r_success = ::lap::com::serialization::DeserializeValue( d, v.success );
        if ( !r_success.HasValue() ) { return r_success; }
        auto r_message = ::lap::com::serialization::DeserializeValue( d, v.message );
        if ( !r_message.HasValue() ) { return r_message; }
        return Result< void >::FromValue();
    }


// [SWS_CM_11501] — common inner namespace
namespace common
{

    /**
     * @brief Common service identification for SensorFusionService [SWS_CM_01010]
     * @version 1.0.0
     */
    class SensorFusionService {
    public:
        static constexpr UInt16 kServiceId = 0x017a;  ///< [SWS_CM_11506]
        static constexpr const Char* kServiceName = "SensorFusionService";
        static constexpr const Char* kSchemaHash  = "64e50584ec9f900e";
        static constexpr UInt32 kVersionMajor = 1;  ///< [SWS_CM_11507]
        static constexpr UInt32 kVersionMinor = 0;
    };

} // namespace common

} // namespace helloworld3

#endif // HELLOWORLD3_SENSORFUSIONSERVICETYPES_HPP
