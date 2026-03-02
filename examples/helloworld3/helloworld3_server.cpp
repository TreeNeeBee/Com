/**
 * @file        helloworld3_server.cpp
 * @author      Aii
 * @brief       HelloWorld3 Server — DDS-only binding — App Framework version
 * @date        2026-03-02
 * @details     Uses auto-generated SensorFusionServiceServerApp framework.
 *              All DDS + CoreIPC boilerplate is encapsulated in the generated
 *              ServerApp base class.
 *
 *              The developer only implements:
 *                - Method handlers   : OnGetSensorReading, OnCalibrateDevice,
 *                                      OnSubmitTelemetry, OnComputeDigest, OnGetDiagnostics
 *                - Field handlers    : OnGet/OnSet for all 5 fields
 *                - Lifecycle hooks   : OnStart, OnStop, OnTick
 *                - Event broadcasting via SendXxx() helpers
 *
 * @copyright   Copyright (c) 2026
 */

#include "SensorFusionServiceServerApp.hpp"
#include <iostream>
#include <mutex>

using namespace helloworld3;
using namespace helloworld3::server_app;

// ========================================================================
// FNV-1a 64-bit hash utility
// ========================================================================
static UInt64 Fnv1aHash( const ::std::vector< UInt8 >& data )
{
    UInt64 hash = 14695981039346656037ULL;
    for ( auto byte : data )
    {
        hash ^= static_cast< UInt64 >( byte );
        hash *= 1099511628211ULL;
    }
    return hash;
}

// ========================================================================
// MyServer — only business logic, zero binding/infrastructure boilerplate
// ========================================================================
class MyServer : public SensorFusionServiceServerApp
{
    std::mutex  m_mtx;
    UInt32      m_activeSensorCount = 5;
    String      m_systemName        = "LightAP-SensorFusion-DDS";
    Double      m_fusionRate        = 30.0;
    UInt32      m_eventSeq          = 0;

    SensorTypes::GeoPosition  m_currentPosition{ 39.9042, 116.4074, 50.0 };
    SensorTypes::DeviceState  m_systemState = SensorTypes::DeviceState::kOnline;

    // ==================== Method Handlers ====================

    Future< SensorTypes::SensorReading > OnGetSensorReading( UInt32 sensorId ) override
    {
        SensorTypes::SensorReading reading;
        reading.sensorId  = sensorId;
        reading.value     = 42.5 + static_cast< Double >( sensorId );
        reading.timestamp = 1709337600000ULL + sensorId * 1000;
        reading.valid     = ( sensorId < 100 );

        std::cout << "[Server] GetSensorReading(" << sensorId
                  << ") = " << reading.value << std::endl;
        return MakeReadyFuture< SensorTypes::SensorReading >( std::move( reading ) );
    }

    Future< CalibrateDeviceOutput > OnCalibrateDevice( UInt32 deviceId, Double offset ) override
    {
        CalibrateDeviceOutput out;
        if ( offset >= -10.0 && offset <= 10.0 )
        {
            out.success = true;
            out.message = "Device " + std::to_string( deviceId )
                        + " calibrated with offset " + std::to_string( offset );
        }
        else
        {
            out.success = false;
            out.message = "Offset out of range [-10, 10]";
        }
        std::cout << "[Server] CalibrateDevice(" << deviceId << ", "
                  << offset << ") → " << ( out.success ? "OK" : "FAIL" ) << std::endl;
        return MakeReadyFuture< CalibrateDeviceOutput >( std::move( out ) );
    }

    void OnSubmitTelemetry( UInt32 channelId, ::std::vector< UInt8 > payload ) override
    {
        std::cout << "[Server] SubmitTelemetry(ch=" << channelId
                  << ", " << payload.size() << " bytes)" << std::endl;
    }

    Future< UInt64 > OnComputeDigest( ::std::vector< UInt8 > data, String algorithm ) override
    {
        UInt64 digest = Fnv1aHash( data );
        std::cout << "[Server] ComputeDigest(" << data.size()
                  << " bytes, algo=" << algorithm << ") = 0x"
                  << std::hex << digest << std::dec << std::endl;
        return MakeReadyFuture< UInt64 >( digest );
    }

    Future< SensorTypes::DiagnosticReport > OnGetDiagnostics( UInt32 deviceId ) override
    {
        SensorTypes::DiagnosticReport report;
        report.deviceId        = deviceId;
        report.state           = SensorTypes::DeviceState::kOnline;
        report.errorCount      = 0;
        report.uptime          = 3600.0 * ( deviceId + 1 );
        report.firmwareVersion = "v2.1." + std::to_string( deviceId );

        std::cout << "[Server] GetDiagnostics(" << deviceId
                  << ") → fw=" << report.firmwareVersion << std::endl;
        return MakeReadyFuture< SensorTypes::DiagnosticReport >( std::move( report ) );
    }

    // ==================== Field Handlers ====================

    Future< UInt32 > OnGetActiveSensorCount() override
    {
        std::lock_guard< std::mutex > lk( m_mtx );
        return MakeReadyFuture< UInt32 >( m_activeSensorCount );
    }

    Future< String > OnGetSystemName() override
    {
        std::lock_guard< std::mutex > lk( m_mtx );
        return MakeReadyFuture< String >( m_systemName );
    }

    Future< void > OnSetSystemName( const String& value ) override
    {
        { std::lock_guard< std::mutex > lk( m_mtx ); m_systemName = value; }
        std::cout << "[Server] SystemName SET → \"" << value << "\"" << std::endl;
        return MakeReadyVoidFuture();
    }

    Future< Double > OnGetFusionRate() override
    {
        std::lock_guard< std::mutex > lk( m_mtx );
        return MakeReadyFuture< Double >( m_fusionRate );
    }

    Future< SensorTypes::GeoPosition > OnGetCurrentPosition() override
    {
        std::lock_guard< std::mutex > lk( m_mtx );
        return MakeReadyFuture< SensorTypes::GeoPosition >( m_currentPosition );
    }

    Future< void > OnSetCurrentPosition( const SensorTypes::GeoPosition& value ) override
    {
        {
            std::lock_guard< std::mutex > lk( m_mtx );
            m_currentPosition = value;
        }
        std::cout << "[Server] CurrentPosition SET → ("
                  << value.latitude << ", " << value.longitude
                  << ", " << value.altitude << ")" << std::endl;
        UpdateCurrentPosition( value );
        return MakeReadyVoidFuture();
    }

    Future< SensorTypes::DeviceState > OnGetSystemState() override
    {
        std::lock_guard< std::mutex > lk( m_mtx );
        return MakeReadyFuture< SensorTypes::DeviceState >( m_systemState );
    }

    Future< void > OnSetSystemState( const SensorTypes::DeviceState& value ) override
    {
        {
            std::lock_guard< std::mutex > lk( m_mtx );
            m_systemState = value;
        }
        UpdateSystemState( value );
        return MakeReadyVoidFuture();
    }

    // ==================== Lifecycle Hooks ====================

    void OnStart() override
    {
        SendSensorAlert( SensorTypes::AlertInfo{
            SensorTypes::AlertLevel::kInfo, "System", "SensorFusion started", 0 } );
        UpdateSystemState( SensorTypes::DeviceState::kOnline );
        std::cout << "[Server] Started. Broadcasting every 1 s.\n" << std::endl;
    }

    void OnStop() override
    {
        SendSensorAlert( SensorTypes::AlertInfo{
            SensorTypes::AlertLevel::kWarning, "System", "SensorFusion stopping", 0 } );
        UpdateSystemState( SensorTypes::DeviceState::kOffline );
    }

    bool OnTick( UInt32 /* tickCount */ ) override
    {
        ++m_eventSeq;

        // PositionUpdate every tick
        SensorTypes::GeoPosition pos;
        {
            std::lock_guard< std::mutex > lk( m_mtx );
            pos = m_currentPosition;
        }
        SendPositionUpdate( pos, static_cast< Double >( m_eventSeq % 360 ), 60.0 );

        // FusionRate notification every 3rd tick
        if ( m_eventSeq % 3 == 0 )
        {
            std::lock_guard< std::mutex > lk( m_mtx );
            UpdateFusionRate( m_fusionRate );
        }

        // RawTelemetry every 5th tick
        if ( m_eventSeq % 5 == 0 )
        {
            ::std::vector< UInt8 > payload( 32 );
            for ( UInt32 i = 0; i < 32; ++i )
            {
                payload[i] = static_cast< UInt8 >( ( m_eventSeq + i ) & 0xFF );
            }
            SendRawTelemetry( m_eventSeq, payload );
        }

        std::cout << "[Server] Tick #" << m_eventSeq << std::endl;
        return true;
    }
};

// ========================================================================
// main
// ========================================================================
int main()
{
    MyServer server;
    return server.Run();
}
