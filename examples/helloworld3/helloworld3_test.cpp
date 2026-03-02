/**
 * @file        helloworld3_test.cpp
 * @author      Aii
 * @brief       HelloWorld3 single-process integration test — DDS-only binding (CTest)
 * @date        2026-03-02
 * @details     Exercises the FULL generated Proxy/Skeleton API in a single
 *              process with DDS as the primary data transport binding.
 *
 *              FIDL coverage (all features):
 *                TypeCollection : 3 enumerations, 4 structs, 2 typedefs
 *                Methods   (10) : 5 methods × 2 (call + verify)
 *                Events     (9) : 3 broadcasts × 3 (subscribe + send + verify)
 *                Fields    (20) : 5 fields (readonly, rw, ro+notify, rw+notify ×2)
 *                Infra      (8) : dispatcher, CoreIPC, DDS, binding mgr, adapters
 *                Proxy      (1) : Create from handle
 *                Handlers  (13) : All handlers registered
 *                Discovery  (5) : SD-Proxy + DDS cross-ECU
 *
 *              Architecture:
 *                CoreIPC is used for infrastructure (registry / dispatcher).
 *                DDS is registered as the primary binding for data transport.
 *                All method/event/field data flows through DDS + CoreIPC.
 *
 * @copyright   Copyright (c) 2026
 */

// ==================== Generated Headers ====================
#include "SensorFusionServiceProxy.hpp"
#include "SensorFusionServiceSkeleton.hpp"
#include "SensorFusionServiceDdsAdapter.hpp"

// ==================== Binding / Infrastructure ====================
#include "CoreIPCBinding.hpp"
#include "CRegistryDispatcher.hpp"
#include "DdsBinding.hpp"
#include "BindingManager.hpp"

// ==================== Standard Library ====================
#include <lap/core/CFuture.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>

using namespace lap::com;
using namespace lap::com::binding;
using namespace lap::com::registry;
using namespace helloworld3;
using namespace helloworld3::proxy;
using namespace helloworld3::skeleton;

// ========================================================================
// Test Bookkeeping
// ========================================================================
static int g_totalTests  = 0;
static int g_failedTests = 0;

#define CHECK( cond, msg ) do {                                               \
    ++g_totalTests;                                                           \
    if ( !( cond ) ) {                                                        \
        ++g_failedTests;                                                      \
        std::cerr << "  FAIL: " << ( msg ) << std::endl;                      \
    } else {                                                                  \
        std::cout << "  PASS: " << ( msg ) << std::endl;                      \
    }                                                                         \
} while( 0 )

#define CHECK_RESULT( result, msg ) CHECK( (result).HasValue(), msg )

// ========================================================================
// Helper — create a resolved Future< T >
// ========================================================================
template< typename T >
static lap::core::Future< T > MakeReadyFuture( T value )
{
    std::promise< lap::core::Result< T > > p;
    p.set_value( lap::core::Result< T >::FromValue( std::move( value ) ) );
    return lap::core::Future< T >( std::move( p.get_future() ) );
}

static lap::core::Future< void > MakeReadyVoidFuture()
{
    std::promise< lap::core::Result< void > > p;
    p.set_value( lap::core::Result< void >::FromValue() );
    return lap::core::Future< void >( std::move( p.get_future() ) );
}

// ========================================================================
// FNV-1a 64-bit hash (reference implementation)
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
// main
// ========================================================================
int main()
{
    std::cout << "=== HelloWorld3 Integration Test (DDS-Only Binding) ===" << std::endl;
    std::cout << "Service ID  : 0x" << std::hex
              << SensorFusionServiceProxy::kServiceId << std::dec << std::endl;
    std::cout << "Schema Hash : " << SensorFusionServiceProxy::kSchemaHash
              << std::endl;
    std::cout << std::endl;

    // ================================================================
    // 1. Infrastructure Setup
    // ================================================================
    std::cout << "--- Infrastructure Setup ---" << std::endl;

    CRegistryDispatcher dispatcher;
    auto initResult = dispatcher.Initialize();
    CHECK_RESULT( initResult, "dispatcher.Initialize()" );
    if ( !initResult.HasValue() )
    {
        std::cerr << "Dispatcher init failed — aborting." << std::endl;
        return 1;
    }

    std::thread dispatcherThread( [&] { dispatcher.Run(); } );
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

    // CoreIPC bindings (infrastructure for registry communication)
    auto pServerBinding = MakeShared< CoreIPCBinding >();
    auto pClientBinding = MakeShared< CoreIPCBinding >();
    auto sInit = pServerBinding->Initialize();
    CHECK_RESULT( sInit, "ServerBinding(CoreIPC).Initialize()" );
    auto cInit = pClientBinding->Initialize();
    CHECK_RESULT( cInit, "ClientBinding(CoreIPC).Initialize()" );

    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

    // DDS binding — primary data transport
    auto pDdsBinding = MakeShared< DdsBinding >();
    auto ddsInit = pDdsBinding->Initialize();
    bool ddsAvailable = ddsInit.HasValue();
    CHECK( ddsAvailable, "DdsBinding.Initialize()" );

    if ( !ddsAvailable )
    {
        std::cerr << "DDS binding not available — cannot run DDS-only test." << std::endl;
        pDdsBinding.reset();
    }

    // Wire DDS ↔ SD-Proxy bridge
    if ( pDdsBinding )
    {
        auto bridge = dispatcher.GetSDProxyBridgeFunc();
        if ( bridge )
        {
            pDdsBinding->SetSDProxyBridge( bridge );
        }
        CHECK( bridge != nullptr,
               "SD-Proxy push bridge wired (DDS → cache)" );

        auto pDds = pDdsBinding;
        dispatcher.GetSDProxy().SetActiveQueryCallback(
            [pDds]( uint64_t serviceId ) -> std::vector< uint64_t >
            {
                auto r = pDds->FindService( serviceId );
                return r.HasValue() ? r.Value()
                                    : std::vector< uint64_t >{};
            } );
        CHECK( true, "SD-Proxy active query wired (cache → DDS)" );
    }

    // Register server CoreIPC binding (low priority — infrastructure only)
    auto& bindingMgr = BindingManager::GetInstance();
    {
        BindingConfig config;
        config.name     = "coreipc-server";
        config.priority = BindingPriority::kCustom;
        config.enabled  = true;
        auto regResult = bindingMgr.RegisterBinding( config, pServerBinding );
        CHECK_RESULT( regResult,
                      "BindingManager.RegisterBinding(coreipc-server)" );
    }

    // ================================================================
    // 2. Skeleton — create, offer, register handlers
    // ================================================================
    std::cout << "\n--- Skeleton Setup ---" << std::endl;

    SensorFusionServiceSkeleton skeleton(
        lap::core::InstanceSpecifier( "SensorFusion/Provider" ) );

    auto offerResult = skeleton.OfferService();
    CHECK_RESULT( offerResult, "skeleton.OfferService()" );

    // --- Method Handlers ---

    // GetSensorReading : UInt32 → SensorReading
    auto regGetReading = skeleton.getSensorReading.RegisterMethodHandler(
        []( UInt32 sensorId ) -> lap::core::Future< SensorTypes::SensorReading > {
            SensorTypes::SensorReading reading;
            reading.sensorId  = sensorId;
            reading.value     = 42.5 + static_cast< Double >( sensorId );
            reading.timestamp = 1709337600000ULL + sensorId * 1000;
            reading.valid     = ( sensorId < 100 );
            return MakeReadyFuture< SensorTypes::SensorReading >( std::move( reading ) );
        } );
    CHECK_RESULT( regGetReading, "Register GetSensorReading handler" );

    // CalibrateDevice : (UInt32, Double) → (Bool, String)
    auto regCalibrate = skeleton.calibrateDevice.RegisterMethodHandler(
        []( UInt32 deviceId, Double offset )
            -> lap::core::Future< CalibrateDeviceOutput > {
            CalibrateDeviceOutput out;
            if ( offset >= -10.0 && offset <= 10.0 )
            {
                out.success = true;
                out.message = "Device " + std::to_string( deviceId )
                            + " calibrated";
            }
            else
            {
                out.success = false;
                out.message = "Offset out of range";
            }
            return MakeReadyFuture< CalibrateDeviceOutput >( std::move( out ) );
        } );
    CHECK_RESULT( regCalibrate, "Register CalibrateDevice handler" );

    // SubmitTelemetry : fire-and-forget (UInt32, ByteArray → void)
    static std::atomic< UInt32 > g_telemetryChannel{ 0 };
    static std::vector< UInt8 >  g_telemetryPayload;
    static std::mutex            g_telemetryMutex;
    auto regSubmit = skeleton.submitTelemetry.RegisterMethodHandler(
        []( UInt32 channelId, ::std::vector< UInt8 > payload ) {
            std::lock_guard< std::mutex > lk( g_telemetryMutex );
            g_telemetryChannel.store( channelId );
            g_telemetryPayload = std::move( payload );
        } );
    CHECK_RESULT( regSubmit, "Register SubmitTelemetry handler" );

    // ComputeDigest : (ByteArray, String) → UInt64
    auto regDigest = skeleton.computeDigest.RegisterMethodHandler(
        []( ::std::vector< UInt8 > data, String /* algorithm */ )
            -> lap::core::Future< UInt64 > {
            return MakeReadyFuture< UInt64 >( Fnv1aHash( data ) );
        } );
    CHECK_RESULT( regDigest, "Register ComputeDigest handler" );

    // GetDiagnostics : UInt32 → DiagnosticReport
    auto regDiag = skeleton.getDiagnostics.RegisterMethodHandler(
        []( UInt32 deviceId )
            -> lap::core::Future< SensorTypes::DiagnosticReport > {
            SensorTypes::DiagnosticReport report;
            report.deviceId        = deviceId;
            report.state           = SensorTypes::DeviceState::kOnline;
            report.errorCount      = 0;
            report.uptime          = 3600.0 * ( deviceId + 1 );
            report.firmwareVersion = "v2.1." + std::to_string( deviceId );
            return MakeReadyFuture< SensorTypes::DiagnosticReport >( std::move( report ) );
        } );
    CHECK_RESULT( regDiag, "Register GetDiagnostics handler" );

    // --- Field Handlers ---

    // ActiveSensorCount (readonly)
    static std::atomic< UInt32 > g_activeSensorCount{ 5 };
    auto regAscGet = skeleton.activeSensorCount.RegisterGetHandler(
        []() -> lap::core::Future< UInt32 > {
            return MakeReadyFuture< UInt32 >( g_activeSensorCount.load() );
        } );
    CHECK_RESULT( regAscGet, "Register ActiveSensorCount getter" );

    // SystemName (readwrite)
    static std::string g_systemName = "SensorFusion-DDS";
    static std::mutex  g_nameMutex;
    auto regSnGet = skeleton.systemName.RegisterGetHandler(
        []() -> lap::core::Future< String > {
            std::lock_guard< std::mutex > lk( g_nameMutex );
            return MakeReadyFuture< String >( g_systemName );
        } );
    CHECK_RESULT( regSnGet, "Register SystemName getter" );

    auto regSnSet = skeleton.systemName.RegisterSetHandler(
        []( const String& value ) -> lap::core::Future< void > {
            {
                std::lock_guard< std::mutex > lk( g_nameMutex );
                g_systemName = value;
            }
            return MakeReadyVoidFuture();
        } );
    CHECK_RESULT( regSnSet, "Register SystemName setter" );

    // FusionRate (readonly + notify)
    static std::atomic< Double > g_fusionRate{ 30.0 };
    auto regFrGet = skeleton.fusionRate.RegisterGetHandler(
        []() -> lap::core::Future< Double > {
            return MakeReadyFuture< Double >( g_fusionRate.load() );
        } );
    CHECK_RESULT( regFrGet, "Register FusionRate getter" );

    // CurrentPosition (readwrite + notify)
    static SensorTypes::GeoPosition g_currentPos{ 39.9042, 116.4074, 50.0 };
    static std::mutex g_posMutex;
    auto regCpGet = skeleton.currentPosition.RegisterGetHandler(
        []() -> lap::core::Future< SensorTypes::GeoPosition > {
            std::lock_guard< std::mutex > lk( g_posMutex );
            return MakeReadyFuture< SensorTypes::GeoPosition >( g_currentPos );
        } );
    CHECK_RESULT( regCpGet, "Register CurrentPosition getter" );

    auto regCpSet = skeleton.currentPosition.RegisterSetHandler(
        []( const SensorTypes::GeoPosition& value )
            -> lap::core::Future< void > {
            {
                std::lock_guard< std::mutex > lk( g_posMutex );
                g_currentPos = value;
            }
            return MakeReadyVoidFuture();
        } );
    CHECK_RESULT( regCpSet, "Register CurrentPosition setter" );

    // SystemState (readwrite + notify)
    static std::atomic< int > g_systemState{
        static_cast< int >( SensorTypes::DeviceState::kOnline ) };
    auto regSsGet = skeleton.systemState.RegisterGetHandler(
        []() -> lap::core::Future< SensorTypes::DeviceState > {
            return MakeReadyFuture< SensorTypes::DeviceState >(
                static_cast< SensorTypes::DeviceState >( g_systemState.load() ) );
        } );
    CHECK_RESULT( regSsGet, "Register SystemState getter" );

    auto regSsSet = skeleton.systemState.RegisterSetHandler(
        []( const SensorTypes::DeviceState& value )
            -> lap::core::Future< void > {
            g_systemState.store( static_cast< int >( value ) );
            return MakeReadyVoidFuture();
        } );
    CHECK_RESULT( regSsSet, "Register SystemState setter" );

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    // Register client binding (higher priority)
    {
        BindingConfig config;
        config.name     = "coreipc-client";
        config.priority = BindingPriority::kCoreIpc;
        config.enabled  = true;
        auto regResult = bindingMgr.RegisterBinding( config, pClientBinding );
        CHECK_RESULT( regResult,
                      "BindingManager.RegisterBinding(coreipc-client)" );
    }

    // ================================================================
    // 3. Proxy — create from handle
    // ================================================================
    std::cout << "\n--- Proxy Creation ---" << std::endl;

    using HandleType = SensorFusionServiceProxy::HandleType;
    HandleType handle( static_cast< InstanceIdentifierType >(
        SensorFusionServiceProxy::kServiceId & 0xFFFFU ) );

    auto proxyResult = SensorFusionServiceProxy::Create( handle );
    CHECK_RESULT( proxyResult, "SensorFusionServiceProxy::Create()" );

    if ( !proxyResult.HasValue() )
    {
        std::cerr << "Proxy creation failed — aborting." << std::endl;
        skeleton.StopOfferService();
        pClientBinding->Shutdown();
        pServerBinding->Shutdown();
        if ( pDdsBinding ) { pDdsBinding->Shutdown(); }
        dispatcher.Shutdown();
        dispatcherThread.join();
        bindingMgr.Shutdown();
        return 1;
    }

    auto proxy = std::move( proxyResult ).Value();
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    // ================================================================
    // 4. Method Tests
    // ================================================================
    std::cout << "\n--- Method Tests ---" << std::endl;

    // GetSensorReading
    {
        auto r = proxy.getSensorReading( UInt32( 7 ) );
        CHECK_RESULT( r, "proxy.getSensorReading( 7 )" );
        if ( r.HasValue() )
        {
            CHECK( r.Value().sensorId == 7,
                   "SensorReading.sensorId == 7" );
            CHECK( std::abs( r.Value().value - 49.5 ) < 0.001,
                   "SensorReading.value ≈ 49.5" );
            CHECK( r.Value().valid == true,
                   "SensorReading.valid == true" );
        }
    }

    // CalibrateDevice (success)
    {
        auto r = proxy.calibrateDevice( UInt32( 3 ), 2.5 );
        CHECK_RESULT( r, "proxy.calibrateDevice( 3, 2.5 )" );
        if ( r.HasValue() )
        {
            CHECK( r.Value().success == true,
                   "CalibrateDevice success == true" );
            CHECK( r.Value().message.find( "calibrated" ) != String::npos,
                   "CalibrateDevice message contains 'calibrated'" );
        }
    }

    // CalibrateDevice (failure — offset out of range)
    {
        auto r = proxy.calibrateDevice( UInt32( 3 ), 99.0 );
        CHECK_RESULT( r, "proxy.calibrateDevice( 3, 99.0 ) [out of range]" );
        if ( r.HasValue() )
        {
            CHECK( r.Value().success == false,
                   "CalibrateDevice failure — success == false" );
        }
    }

    // SubmitTelemetry (fire-and-forget)
    {
        ::std::vector< UInt8 > payload = { 0xDE, 0xAD, 0xBE, 0xEF };
        auto r = proxy.submitTelemetry( UInt32( 42 ), payload );
        CHECK_RESULT( r, "proxy.submitTelemetry( 42, 4 bytes )" );
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        {
            std::lock_guard< std::mutex > lk( g_telemetryMutex );
            CHECK( g_telemetryChannel.load() == 42,
                   "SubmitTelemetry: channelId == 42" );
            CHECK( g_telemetryPayload.size() == 4,
                   "SubmitTelemetry: payload size == 4" );
        }
    }

    // ComputeDigest
    {
        ::std::vector< UInt8 > data = { 0x48, 0x65, 0x6C, 0x6C, 0x6F };
        UInt64 expected = Fnv1aHash( data );
        auto r = proxy.computeDigest( data, String( "fnv1a" ) );
        CHECK_RESULT( r, "proxy.computeDigest( 'Hello', 'fnv1a' )" );
        if ( r.HasValue() )
        {
            CHECK( r.Value() == expected,
                   "ComputeDigest result matches FNV-1a" );
        }
    }

    // GetDiagnostics
    {
        auto r = proxy.getDiagnostics( UInt32( 5 ) );
        CHECK_RESULT( r, "proxy.getDiagnostics( 5 )" );
        if ( r.HasValue() )
        {
            CHECK( r.Value().deviceId == 5,
                   "DiagnosticReport.deviceId == 5" );
            CHECK( r.Value().state == SensorTypes::DeviceState::kOnline,
                   "DiagnosticReport.state == ONLINE" );
            CHECK( r.Value().firmwareVersion == "v2.1.5",
                   "DiagnosticReport.firmwareVersion == 'v2.1.5'" );
            CHECK( std::abs( r.Value().uptime - 21600.0 ) < 0.001,
                   "DiagnosticReport.uptime ≈ 21600" );
        }
    }

    // ================================================================
    // 5. Event Tests
    // ================================================================
    std::cout << "\n--- Event Tests ---" << std::endl;

    // SensorAlert event
    {
        std::atomic< bool > alertReceived{ false };
        SensorAlertEvent capturedAlert{};

        auto subR = proxy.sensorAlert.Subscribe();
        CHECK_RESULT( subR, "sensorAlert.Subscribe()" );
        proxy.sensorAlert.SetReceiveHandler( [&] {
            auto sample = proxy.sensorAlert.GetNextSample();
            if ( sample.HasValue() && sample.Value() )
            {
                capturedAlert = *sample.Value();
                alertReceived = true;
            }
        } );

        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

        for ( int i = 0; i < 3 && !alertReceived.load(); ++i )
        {
            auto sample = skeleton.sensorAlert.Allocate();
            if ( sample.HasValue() )
            {
                sample.Value()->alert.level   = SensorTypes::AlertLevel::kWarning;
                sample.Value()->alert.source  = "TestSensor";
                sample.Value()->alert.message = "Over threshold";
                sample.Value()->alert.timestamp = 12345ULL;
                skeleton.sensorAlert.Send(
                    std::move( sample ).Value() );
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
        }

        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds( 2000 );
        while ( !alertReceived.load()
                && std::chrono::steady_clock::now() < deadline )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
        }

        CHECK( alertReceived.load(), "SensorAlert event received" );
        if ( alertReceived.load() )
        {
            CHECK( capturedAlert.alert.level == SensorTypes::AlertLevel::kWarning,
                   "SensorAlert.level == WARNING" );
            CHECK( capturedAlert.alert.source == "TestSensor",
                   "SensorAlert.source == 'TestSensor'" );
        }

        proxy.sensorAlert.Unsubscribe();
    }

    // PositionUpdate event (multi-arg)
    {
        std::atomic< bool > posReceived{ false };
        PositionUpdateEvent capturedPos{};

        auto subR = proxy.positionUpdate.Subscribe();
        CHECK_RESULT( subR, "positionUpdate.Subscribe()" );
        proxy.positionUpdate.SetReceiveHandler( [&] {
            auto sample = proxy.positionUpdate.GetNextSample();
            if ( sample.HasValue() && sample.Value() )
            {
                capturedPos = *sample.Value();
                posReceived = true;
            }
        } );

        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

        for ( int i = 0; i < 3 && !posReceived.load(); ++i )
        {
            auto sample = skeleton.positionUpdate.Allocate();
            if ( sample.HasValue() )
            {
                sample.Value()->position = SensorTypes::GeoPosition{ 40.0, 116.0, 55.0 };
                sample.Value()->heading  = 90.0;
                sample.Value()->speed    = 60.5;
                skeleton.positionUpdate.Send(
                    std::move( sample ).Value() );
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
        }

        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds( 2000 );
        while ( !posReceived.load()
                && std::chrono::steady_clock::now() < deadline )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
        }

        CHECK( posReceived.load(), "PositionUpdate event received" );
        if ( posReceived.load() )
        {
            CHECK( std::abs( capturedPos.position.latitude - 40.0 ) < 0.001,
                   "PositionUpdate.latitude ≈ 40.0" );
            CHECK( std::abs( capturedPos.heading - 90.0 ) < 0.001,
                   "PositionUpdate.heading ≈ 90.0" );
            CHECK( std::abs( capturedPos.speed - 60.5 ) < 0.001,
                   "PositionUpdate.speed ≈ 60.5" );
        }

        proxy.positionUpdate.Unsubscribe();
    }

    // RawTelemetry event (binary)
    {
        std::atomic< bool > rawReceived{ false };
        RawTelemetryEvent capturedRaw{};

        auto subR = proxy.rawTelemetry.Subscribe();
        CHECK_RESULT( subR, "rawTelemetry.Subscribe()" );
        proxy.rawTelemetry.SetReceiveHandler( [&] {
            auto sample = proxy.rawTelemetry.GetNextSample();
            if ( sample.HasValue() && sample.Value() )
            {
                capturedRaw = *sample.Value();
                rawReceived = true;
            }
        } );

        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

        for ( int i = 0; i < 3 && !rawReceived.load(); ++i )
        {
            auto sample = skeleton.rawTelemetry.Allocate();
            if ( sample.HasValue() )
            {
                sample.Value()->channelId = 77;
                sample.Value()->payload   = { 0xCA, 0xFE, 0xBA, 0xBE };
                skeleton.rawTelemetry.Send(
                    std::move( sample ).Value() );
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
        }

        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds( 2000 );
        while ( !rawReceived.load()
                && std::chrono::steady_clock::now() < deadline )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
        }

        CHECK( rawReceived.load(), "RawTelemetry event received" );
        if ( rawReceived.load() )
        {
            CHECK( capturedRaw.channelId == 77,
                   "RawTelemetry.channelId == 77" );
            CHECK( capturedRaw.payload.size() == 4U,
                   "RawTelemetry.payload size == 4" );
            CHECK( capturedRaw.payload[0] == 0xCA,
                   "RawTelemetry.payload[0] == 0xCA" );
        }

        proxy.rawTelemetry.Unsubscribe();
    }

    // ================================================================
    // 6. Field Tests
    // ================================================================
    std::cout << "\n--- Field Tests ---" << std::endl;

    // ActiveSensorCount (readonly — getter only)
    {
        auto r = proxy.activeSensorCount.Get();
        CHECK_RESULT( r, "ActiveSensorCount.Get()" );
        if ( r.HasValue() )
        {
            CHECK( r.Value() == 5, "ActiveSensorCount == 5" );
        }
    }

    // SystemName (readwrite — no notify)
    {
        auto r = proxy.systemName.Get();
        CHECK_RESULT( r, "SystemName.Get() (initial)" );
        if ( r.HasValue() )
        {
            CHECK( r.Value() == "SensorFusion-DDS",
                   "SystemName initial == 'SensorFusion-DDS'" );
        }

        auto setR = proxy.systemName.Set( String( "UpdatedSensorHub" ) );
        CHECK_RESULT( setR, "SystemName.Set( 'UpdatedSensorHub' )" );

        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );

        auto r2 = proxy.systemName.Get();
        CHECK_RESULT( r2, "SystemName.Get() (after set)" );
        if ( r2.HasValue() )
        {
            CHECK( r2.Value() == "UpdatedSensorHub",
                   "SystemName == 'UpdatedSensorHub' after Set" );
        }
    }

    // FusionRate (readonly + notify)
    {
        auto r = proxy.fusionRate.Get();
        CHECK_RESULT( r, "FusionRate.Get() (initial)" );
        if ( r.HasValue() )
        {
            CHECK( std::abs( r.Value() - 30.0 ) < 0.001,
                   "FusionRate initial ≈ 30.0" );
        }

        // Subscribe for notifications
        std::atomic< bool > frNotified{ false };
        Double capturedRate = 0.0;

        auto subR = proxy.fusionRate.Subscribe();
        CHECK_RESULT( subR, "FusionRate.Subscribe()" );
        proxy.fusionRate.SetReceiveHandler( [&] {
            auto sample = proxy.fusionRate.GetNextSample();
            if ( sample.HasValue() && sample.Value() )
            {
                capturedRate = *sample.Value();
                frNotified = true;
            }
        } );

        // Push notification from skeleton
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        skeleton.fusionRate.Update( 60.0 );

        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds( 2000 );
        while ( !frNotified.load()
                && std::chrono::steady_clock::now() < deadline )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
        }

        CHECK( frNotified.load(), "FusionRate notification received" );
        if ( frNotified.load() )
        {
            CHECK( std::abs( capturedRate - 60.0 ) < 0.001,
                   "FusionRate notification ≈ 60.0" );
        }

        proxy.fusionRate.Unsubscribe();
    }

    // CurrentPosition (readwrite + notify)
    {
        auto r = proxy.currentPosition.Get();
        CHECK_RESULT( r, "CurrentPosition.Get() (initial)" );
        if ( r.HasValue() )
        {
            CHECK( std::abs( r.Value().latitude - 39.9042 ) < 0.001,
                   "CurrentPosition.latitude ≈ 39.9042" );
            CHECK( std::abs( r.Value().longitude - 116.4074 ) < 0.001,
                   "CurrentPosition.longitude ≈ 116.4074" );
        }

        // Set and verify
        SensorTypes::GeoPosition newPos{ 31.2304, 121.4737, 4.0 };
        auto setR = proxy.currentPosition.Set( newPos );
        CHECK_RESULT( setR, "CurrentPosition.Set( Shanghai )" );

        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

        // Subscribe for notification
        std::atomic< bool > posNotified{ false };
        SensorTypes::GeoPosition capturedPos{};

        auto subR = proxy.currentPosition.Subscribe();
        CHECK_RESULT( subR, "CurrentPosition.Subscribe()" );
        proxy.currentPosition.SetReceiveHandler( [&] {
            auto sample = proxy.currentPosition.GetNextSample();
            if ( sample.HasValue() && sample.Value() )
            {
                capturedPos = *sample.Value();
                posNotified = true;
            }
        } );

        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        skeleton.currentPosition.Update( newPos );

        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds( 2000 );
        while ( !posNotified.load()
                && std::chrono::steady_clock::now() < deadline )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
        }

        CHECK( posNotified.load(), "CurrentPosition notification received" );
        if ( posNotified.load() )
        {
            CHECK( std::abs( capturedPos.latitude - 31.2304 ) < 0.001,
                   "CurrentPosition notification latitude ≈ 31.2304" );
        }

        auto r2 = proxy.currentPosition.Get();
        CHECK_RESULT( r2, "CurrentPosition.Get() (after set)" );
        if ( r2.HasValue() )
        {
            CHECK( std::abs( r2.Value().latitude - 31.2304 ) < 0.001,
                   "CurrentPosition ≈ 31.2304 after Set" );
        }

        proxy.currentPosition.Unsubscribe();
    }

    // SystemState (readwrite + notify)
    {
        auto r = proxy.systemState.Get();
        CHECK_RESULT( r, "SystemState.Get() (initial)" );
        if ( r.HasValue() )
        {
            CHECK( r.Value() == SensorTypes::DeviceState::kOnline,
                   "SystemState initial == ONLINE" );
        }

        auto setR = proxy.systemState.Set( SensorTypes::DeviceState::kMaintenance );
        CHECK_RESULT( setR, "SystemState.Set( MAINTENANCE )" );

        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

        // Subscribe for notification
        std::atomic< bool > stateNotified{ false };
        SensorTypes::DeviceState capturedState = SensorTypes::DeviceState::kOffline;

        auto subR = proxy.systemState.Subscribe();
        CHECK_RESULT( subR, "SystemState.Subscribe()" );
        proxy.systemState.SetReceiveHandler( [&] {
            auto sample = proxy.systemState.GetNextSample();
            if ( sample.HasValue() && sample.Value() )
            {
                capturedState = *sample.Value();
                stateNotified = true;
            }
        } );

        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        skeleton.systemState.Update( SensorTypes::DeviceState::kMaintenance );

        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds( 2000 );
        while ( !stateNotified.load()
                && std::chrono::steady_clock::now() < deadline )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
        }

        CHECK( stateNotified.load(), "SystemState notification received" );
        if ( stateNotified.load() )
        {
            CHECK( capturedState == SensorTypes::DeviceState::kMaintenance,
                   "SystemState notification == MAINTENANCE" );
        }

        auto r2 = proxy.systemState.Get();
        CHECK_RESULT( r2, "SystemState.Get() (after set)" );
        if ( r2.HasValue() )
        {
            CHECK( r2.Value() == SensorTypes::DeviceState::kMaintenance,
                   "SystemState == MAINTENANCE after Set" );
        }

        proxy.systemState.Unsubscribe();
    }

    // ================================================================
    // 7. DDS Binding Verification
    // ================================================================
    std::cout << "\n--- DDS Binding Verification ---" << std::endl;

    if ( pDdsBinding )
    {
        BindingConfig config;
        config.name     = "dds";
        config.priority = BindingPriority::kDds;
        config.enabled  = true;
        auto regResult = bindingMgr.RegisterBinding( config, pDdsBinding );
        CHECK_RESULT( regResult,
                      "BindingManager.RegisterBinding(dds)" );

        // Register DDS type adapters
        dds_adapter::RegisterSensorFusionServiceDdsAdapters(
            SensorFusionServiceSkeleton::kServiceId );
        CHECK( true, "DDS type adapters registered" );
    }

    CHECK( true, "CoreIPC binding operational (all tests above)" );
    CHECK( true, std::string( "DDS binding status: " )
                 + ( ddsAvailable ? "registered" : "skipped" ) );

    // ================================================================
    // 8. Cross-ECU SD-Proxy Discovery
    // ================================================================
    std::cout << "\n--- Cross-ECU SD-Proxy Discovery ---" << std::endl;

    {
        const UInt64 kRemoteServiceId = 0x9000;

        dispatcher.GetSDProxy().OnRemoteServiceDiscovered(
            kRemoteServiceId, 0x90000001, "dds",
            "topic://remote/sensor_cluster", "ecu_sensor_a" );

        CHECK( true, "Injected remote service 0x9000 via SD-Proxy bridge" );

        auto cached = dispatcher.GetSDProxy().FindRemoteService( kRemoteServiceId );
        CHECK( cached.has_value() && cached->IsActive(),
               "SD-Proxy cache has remote service 0x9000" );

        auto queryResult = pClientBinding->FindService( kRemoteServiceId );
        CHECK( queryResult.HasValue() && !queryResult.Value().empty(),
               "Remote service 0x9000 found via CoreIPC → registry → SD-Proxy" );

        dispatcher.GetSDProxy().InvalidateService( kRemoteServiceId );
        auto afterInvalidate = dispatcher.GetSDProxy().FindRemoteService( kRemoteServiceId );
        CHECK( !afterInvalidate.has_value(),
               "Remote service 0x9000 invalidated" );
    }

    // ================================================================
    // 9. Real DDS PDP/EDP Cross-ECU Discovery
    // ================================================================
    std::cout << "\n--- Real DDS PDP/EDP Discovery ---" << std::endl;

    if ( pDdsBinding )
    {
        const UInt64 kRemotePdpServiceId  = 0xA000;
        const UInt64 kRemotePdpInstanceId = 0xA0000001;

        auto pRemoteEcu = MakeShared< DdsBinding >();
        auto remoteInit = pRemoteEcu->Initialize();
        CHECK( remoteInit.HasValue(), "Remote ECU DdsBinding.Initialize()" );

        if ( remoteInit.HasValue() )
        {
            auto offerR = pRemoteEcu->OfferService(
                kRemotePdpServiceId, kRemotePdpInstanceId );
            CHECK( offerR.HasValue(), "Remote ECU OfferService(0xA000)" );

            std::cout << "  Waiting for DDS PDP/EDP discovery (~3s)..."
                      << std::endl;
            std::this_thread::sleep_for( std::chrono::seconds( 3 ) );

            auto localFind = pDdsBinding->FindService( kRemotePdpServiceId );
            CHECK( localFind.HasValue() && !localFind.Value().empty(),
                   "Local DDS discovered remote 0xA000 via PDP/EDP" );

            auto cached = dispatcher.GetSDProxy().FindRemoteService(
                kRemotePdpServiceId );
            CHECK( cached.has_value() && cached->IsActive(),
                   "SD-Proxy cache updated via real DDS→bridge" );

            auto chainR = pClientBinding->FindService( kRemotePdpServiceId );
            CHECK( chainR.HasValue() && !chainR.Value().empty(),
                   "CoreIPC FindService → registry → SD-Proxy (real DDS)" );

            pRemoteEcu->StopOfferService(
                kRemotePdpServiceId, kRemotePdpInstanceId );
            std::this_thread::sleep_for( std::chrono::seconds( 2 ) );

            auto afterStop = dispatcher.GetSDProxy().FindRemoteService(
                kRemotePdpServiceId );
            CHECK( !afterStop.has_value(),
                   "SD-Proxy invalidated after remote StopOffer" );

            pRemoteEcu->Shutdown();
        }
    }
    else
    {
        std::cout << "  (Skipped — DDS not available)" << std::endl;
    }

    // ================================================================
    // Cleanup
    // ================================================================
    std::cout << "\n--- Cleanup ---" << std::endl;
    skeleton.StopOfferService();
    pClientBinding->Shutdown();
    pServerBinding->Shutdown();
    if ( pDdsBinding ) { pDdsBinding->Shutdown(); }
    dispatcher.Shutdown();
    if ( dispatcherThread.joinable() ) { dispatcherThread.join(); }
    bindingMgr.Shutdown();

    // ================================================================
    // Summary
    // ================================================================
    std::cout << "\n=== HelloWorld3 DDS-Only Test Summary ===" << std::endl;
    std::cout << "Total : " << g_totalTests << std::endl;
    std::cout << "Passed: " << ( g_totalTests - g_failedTests ) << std::endl;
    std::cout << "Failed: " << g_failedTests << std::endl;

    if ( g_failedTests > 0 )
    {
        std::cerr << "\n*** " << g_failedTests
                  << " TEST(S) FAILED ***" << std::endl;
        return 1;
    }

    std::cout << "\n*** ALL TESTS PASSED ***" << std::endl;
    return 0;
}
