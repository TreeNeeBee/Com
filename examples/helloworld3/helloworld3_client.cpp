/**
 * @file        helloworld3_client.cpp
 * @author      Aii
 * @brief       HelloWorld3 Client — DDS-only binding — App Framework version
 * @date        2026-03-02
 * @details     Uses auto-generated SensorFusionServiceClientApp framework.
 *              All DDS + CoreIPC boilerplate is encapsulated in the generated
 *              ClientApp base class.
 *
 *              The developer only implements:
 *                - Event handlers   : OnSensorAlert, OnPositionUpdate, OnRawTelemetry
 *                - Field notifs     : OnFusionRateChanged, OnCurrentPositionChanged,
 *                                     OnSystemStateChanged
 *                - Lifecycle hooks  : OnConnected, OnDisconnected, OnTick
 *                - Uses convenience wrappers: GetSensorReading(), CalibrateDevice(), etc.
 *
 * @copyright   Copyright (c) 2026
 */

#include "SensorFusionServiceClientApp.hpp"
#include <atomic>
#include <iostream>

using namespace helloworld3;
using namespace helloworld3::client_app;

// ========================================================================
// MyClient — only business logic
// ========================================================================
class MyClient : public SensorFusionServiceClientApp
{
    std::atomic< UInt32 > m_alertCount{ 0 };
    std::atomic< UInt32 > m_positionCount{ 0 };
    std::atomic< UInt32 > m_telemetryCount{ 0 };

    // ==================== Event Handlers ====================

    void OnSensorAlert( const SensorAlertEvent& data ) override
    {
        UInt32 n = m_alertCount.fetch_add( 1 ) + 1;
        std::cout << "[Client] SensorAlert #" << n << ": ["
                  << static_cast< int >( data.alert.level ) << "] "
                  << data.alert.source << " — " << data.alert.message
                  << std::endl;
    }

    void OnPositionUpdate( const PositionUpdateEvent& data ) override
    {
        UInt32 n = m_positionCount.fetch_add( 1 ) + 1;
        std::cout << "[Client] Position #" << n << ": ("
                  << data.position.latitude << ", "
                  << data.position.longitude << ") hdg="
                  << data.heading << " spd=" << data.speed
                  << std::endl;
    }

    void OnRawTelemetry( const RawTelemetryEvent& data ) override
    {
        m_telemetryCount.fetch_add( 1 );
        std::cout << "[Client] Telemetry ch=" << data.channelId
                  << " (" << data.payload.size() << " bytes)"
                  << std::endl;
    }

    // ==================== Field Notifications ====================

    void OnFusionRateChanged( Double value ) override
    {
        std::cout << "[Client] FusionRate → " << value << " Hz" << std::endl;
    }

    void OnCurrentPositionChanged( const SensorTypes::GeoPosition& value ) override
    {
        std::cout << "[Client] CurrentPosition → ("
                  << value.latitude << ", " << value.longitude
                  << ", " << value.altitude << ")" << std::endl;
    }

    void OnSystemStateChanged( const SensorTypes::DeviceState& value ) override
    {
        const char* names[] = { "OFFLINE", "INITIALIZING", "ONLINE", "ERROR", "MAINTENANCE" };
        int idx = static_cast< int >( value );
        const char* name = ( idx >= 0 && idx <= 4 ) ? names[idx] : "UNKNOWN";
        std::cout << "[Client] SystemState → " << name << std::endl;
    }

    // ==================== Lifecycle Hooks ====================

    void OnConnected() override
    {
        std::cout << "\n--- Method Calls ---" << std::endl;

        // GetSensorReading
        {
            auto r = GetSensorReading( UInt32( 1 ) );
            if ( r.HasValue() )
            {
                std::cout << "[Client] Sensor #1 value=" << r.Value().value
                          << " valid=" << r.Value().valid << std::endl;
            }
        }

        // CalibrateDevice (valid)
        {
            auto r = CalibrateDevice( UInt32( 7 ), 2.5 );
            if ( r.HasValue() )
            {
                std::cout << "[Client] Calibrate → "
                          << ( r.Value().success ? "OK" : "FAIL" )
                          << ": " << r.Value().message << std::endl;
            }
        }

        // SubmitTelemetry (fire-and-forget)
        {
            SubmitTelemetry( UInt32( 5 ), { 0xAA, 0xBB, 0xCC } );
            std::cout << "[Client] SubmitTelemetry sent." << std::endl;
        }

        // ComputeDigest
        {
            auto r = ComputeDigest( { 0x48, 0x65, 0x6C, 0x6C, 0x6F }, "fnv1a" );
            if ( r.HasValue() )
            {
                std::cout << "[Client] Digest = 0x" << std::hex
                          << r.Value() << std::dec << std::endl;
            }
        }

        // GetDiagnostics
        {
            auto r = GetDiagnostics( UInt32( 3 ) );
            if ( r.HasValue() )
            {
                std::cout << "[Client] Diagnostics device #3: fw="
                          << r.Value().firmwareVersion
                          << " uptime=" << r.Value().uptime << "s"
                          << std::endl;
            }
        }

        // --- Field Operations ---
        std::cout << "\n--- Field Operations ---" << std::endl;

        // ActiveSensorCount (readonly)
        {
            auto r = GetActiveSensorCount();
            if ( r.HasValue() )
            {
                std::cout << "[Client] ActiveSensorCount = "
                          << r.Value() << std::endl;
            }
        }

        // SystemName (read-write)
        {
            auto r = GetSystemName();
            if ( r.HasValue() )
            {
                std::cout << "[Client] SystemName (before) = \""
                          << r.Value() << "\"" << std::endl;
            }
            SetSystemName( "MyDDS-SensorFusion" );
            auto r2 = GetSystemName();
            if ( r2.HasValue() )
            {
                std::cout << "[Client] SystemName (after)  = \""
                          << r2.Value() << "\"" << std::endl;
            }
        }

        // FusionRate (readonly + notify)
        {
            auto r = GetFusionRate();
            if ( r.HasValue() )
            {
                std::cout << "[Client] FusionRate = "
                          << r.Value() << " Hz" << std::endl;
            }
        }

        // CurrentPosition (readwrite + notify)
        {
            auto r = GetCurrentPosition();
            if ( r.HasValue() )
            {
                std::cout << "[Client] Position = ("
                          << r.Value().latitude << ", "
                          << r.Value().longitude << ")" << std::endl;
            }
            SetCurrentPosition( SensorTypes::GeoPosition{ 31.2304, 121.4737, 4.0 } );
            std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
            auto r2 = GetCurrentPosition();
            if ( r2.HasValue() )
            {
                std::cout << "[Client] Position (after) = ("
                          << r2.Value().latitude << ", "
                          << r2.Value().longitude << ")" << std::endl;
            }
        }

        // SystemState (readwrite + notify)
        {
            auto r = GetSystemState();
            if ( r.HasValue() )
            {
                std::cout << "[Client] SystemState = "
                          << static_cast< int >( r.Value() ) << std::endl;
            }
            SetSystemState( SensorTypes::DeviceState::kMaintenance );
        }

        std::cout << "\n[Client] Listening for events for 5 seconds ..."
                  << std::endl;
    }

    bool OnTick( UInt32 tickCount ) override
    {
        return tickCount < 5;
    }

    void OnDisconnected() override
    {
        std::cout << "\n[Client] Summary: alerts="
                  << m_alertCount.load()
                  << ", positions=" << m_positionCount.load()
                  << ", telemetry=" << m_telemetryCount.load()
                  << std::endl;
    }
};

// ========================================================================
// main
// ========================================================================
int main()
{
    MyClient client;
    return client.Run();
}
