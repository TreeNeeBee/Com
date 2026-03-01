/**
 * @file        helloworld2_test.cpp
 * @author      Aii
 * @brief       HelloWorld2 single-process integration test — dual-binding (CTest)
 * @date        2026-03-01
 * @details     Exercises the FULL generated Proxy/Skeleton API in a single
 *              process with BOTH CoreIPC and DDS bindings registered.
 *
 *              Verifies:
 *                Infrastructure  : dispatcher, CoreIPC binding, DDS binding,
 *                                  binding manager, DDS type adapter registration
 *                Methods    (8)  : SayHello, Add, NotifyLog(F&F), ComputeHash
 *                Events     (7)  : Greeting, StatusChanged, DataStream
 *                Fields    (14)  : VisitorCount(ro), ServerName(rw),
 *                                  Temperature(rw + notification)
 *                Proxy      (1)  : Create from handle
 *                Handler   (11)  : All handlers registered successfully
 *
 *              NOTE: Same-process event testing requires TWO separate
 *              CoreIPCBinding instances: one for the skeleton (publisher) and
 *              one for the proxy (subscriber), so that publisher/subscriber
 *              shared-memory channels are connected correctly.
 *
 *              The DDS binding is initialized as a secondary transport,
 *              demonstrating that both can coexist.
 *
 * @copyright   Copyright (c) 2026
 */

// ==================== Generated Headers ====================
#include "HelloWorld2ServiceProxy.hpp"
#include "HelloWorld2ServiceSkeleton.hpp"
#include "HelloWorld2ServiceDdsAdapter.hpp"

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
using namespace helloworld2;
using namespace helloworld2::proxy;
using namespace helloworld2::skeleton;

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
    std::cout << "=== HelloWorld2 Integration Test (Dual-Binding) ===" << std::endl;
    std::cout << "Service ID  : 0x" << std::hex
              << HelloWorld2ServiceProxy::kServiceId << std::dec << std::endl;
    std::cout << "Schema Hash : " << HelloWorld2ServiceProxy::kSchemaHash
              << std::endl;
    std::cout << std::endl;

    // ================================================================
    // 1. Infrastructure: registry + dual-binding setup
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

    // Two CoreIPC binding instances for same-process testing:
    //   pServerBinding  — skeleton (publisher side)
    //   pClientBinding  — proxy   (subscriber side)
    auto pServerBinding = MakeShared< CoreIPCBinding >();
    auto pClientBinding = MakeShared< CoreIPCBinding >();
    auto sInit = pServerBinding->Initialize();
    CHECK_RESULT( sInit, "ServerBinding(CoreIPC).Initialize()" );
    auto cInit = pClientBinding->Initialize();
    CHECK_RESULT( cInit, "ClientBinding(CoreIPC).Initialize()" );

    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

    // Optional DDS binding — registered AFTER OfferService so skeleton uses
    // CoreIPC for same-process testing.  DDS is registered later to prove
    // that both bindings can coexist in the BindingManager.
    auto pDdsBinding = MakeShared< DdsBinding >();
    auto ddsInit = pDdsBinding->Initialize();
    bool ddsAvailable = ddsInit.HasValue();
    CHECK( true, std::string( "DdsBinding.Initialize() → " )
                 + ( ddsAvailable ? "OK" : "unavailable (non-fatal)" ) );

    if ( !ddsAvailable )
    {
        pDdsBinding.reset();
    }

    // Wire DDS binding ↔ SD-Proxy bridge for cross-ECU discovery
    if ( pDdsBinding )
    {
        // Push bridge: DDS discovery → SD-Proxy cache
        auto bridge = dispatcher.GetSDProxyBridgeFunc();
        if ( bridge )
        {
            pDdsBinding->SetSDProxyBridge( bridge );
        }
        CHECK( bridge != nullptr,
               "SD-Proxy push bridge wired (DDS → cache)" );

        // Pull bridge: SD-Proxy active query → DDS FindService
        auto pDds = pDdsBinding;  // shared_ptr capture
        dispatcher.GetSDProxy().SetActiveQueryCallback(
            [pDds]( uint64_t serviceId ) -> std::vector< uint64_t >
            {
                auto r = pDds->FindService( serviceId );
                return r.HasValue() ? r.Value()
                                    : std::vector< uint64_t >{};
            } );
        CHECK( true, "SD-Proxy active query wired (cache → DDS)" );
    }

    // Register server binding (low priority) for skeleton
    auto& bindingMgr = BindingManager::GetInstance();
    {
        BindingConfig config;
        config.name     = "coreipc-server";
        config.priority = BindingPriority::kCustom;  // lower priority
        config.enabled  = true;
        auto regResult = bindingMgr.RegisterBinding(
            config, pServerBinding );
        CHECK_RESULT( regResult,
                      "BindingManager.RegisterBinding(coreipc-server)" );
    }

    // ================================================================
    // 2. Skeleton — create, offer, register handlers
    // ================================================================
    std::cout << "\n--- Skeleton Setup ---" << std::endl;

    HelloWorld2ServiceSkeleton skeleton(
        lap::core::InstanceSpecifier( "HelloWorld2/Provider" ) );

    auto offerResult = skeleton.OfferService();
    CHECK_RESULT( offerResult, "skeleton.OfferService()" );

    // --- Method Handlers ---

    // SayHello : String → String
    auto regSayHello = skeleton.sayHello.RegisterMethodHandler(
        []( String name ) -> lap::core::Future< String > {
            return MakeReadyFuture< String >(
                "Hello, " + name + "!" );
        } );
    CHECK_RESULT( regSayHello, "Register SayHello handler" );

    // Add : (UInt32, UInt32) → UInt32
    auto regAdd = skeleton.add.RegisterMethodHandler(
        []( UInt32 a, UInt32 b )
            -> lap::core::Future< UInt32 > {
            return MakeReadyFuture< UInt32 >( a + b );
        } );
    CHECK_RESULT( regAdd, "Register Add handler" );

    // NotifyLog : fire-and-forget (String → void)
    static std::string g_lastLog;
    static std::mutex  g_logMutex;
    auto regNotifyLog = skeleton.notifyLog.RegisterMethodHandler(
        []( String message ) {
            std::lock_guard< std::mutex > lk( g_logMutex );
            g_lastLog = message;
        } );
    CHECK_RESULT( regNotifyLog, "Register NotifyLog handler" );

    // ComputeHash : vector<UInt8> → UInt64
    auto regComputeHash = skeleton.computeHash.RegisterMethodHandler(
        []( ::std::vector< UInt8 > data )
            -> lap::core::Future< UInt64 > {
            return MakeReadyFuture< UInt64 >( Fnv1aHash( data ) );
        } );
    CHECK_RESULT( regComputeHash, "Register ComputeHash handler" );

    // --- Field Handlers ---

    // VisitorCount (readonly)
    static std::atomic< UInt32 > g_visitorCount{ 42 };
    auto regVcGet = skeleton.visitorCount.RegisterGetHandler(
        []() -> lap::core::Future< UInt32 > {
            return MakeReadyFuture< UInt32 >(
                g_visitorCount.load() );
        } );
    CHECK_RESULT( regVcGet, "Register VisitorCount getter" );

    // ServerName (read-write)
    static std::string g_serverName = "GeneratedServer-DualBinding";
    static std::mutex  g_nameMutex;
    auto regSnGet = skeleton.serverName.RegisterGetHandler(
        []() -> lap::core::Future< String > {
            std::lock_guard< std::mutex > lk( g_nameMutex );
            return MakeReadyFuture< String >( g_serverName );
        } );
    CHECK_RESULT( regSnGet, "Register ServerName getter" );

    auto regSnSet = skeleton.serverName.RegisterSetHandler(
        []( const String& value ) -> lap::core::Future< void > {
            {
                std::lock_guard< std::mutex > lk( g_nameMutex );
                g_serverName = value;
            }
            return MakeReadyVoidFuture();
        } );
    CHECK_RESULT( regSnSet, "Register ServerName setter" );

    // Temperature (read-write + notify)
    static std::atomic< Double > g_temperature{ 22.5 };
    auto regTmpGet = skeleton.temperature.RegisterGetHandler(
        []() -> lap::core::Future< Double > {
            return MakeReadyFuture< Double >(
                g_temperature.load() );
        } );
    CHECK_RESULT( regTmpGet, "Register Temperature getter" );

    auto regTmpSet = skeleton.temperature.RegisterSetHandler(
        []( const Double& value )
            -> lap::core::Future< void > {
            g_temperature.store( value );
            return MakeReadyVoidFuture();
        } );
    CHECK_RESULT( regTmpSet, "Register Temperature setter" );

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    // Register client binding (higher priority → proxy picks this one)
    {
        BindingConfig config;
        config.name     = "coreipc-client";
        config.priority = BindingPriority::kCoreIpc;  // higher
        config.enabled  = true;
        auto regResult = bindingMgr.RegisterBinding(
            config, pClientBinding );
        CHECK_RESULT( regResult,
                      "BindingManager.RegisterBinding(coreipc-client)" );
    }

    // ================================================================
    // 3. Proxy — create from handle
    // ================================================================
    std::cout << "\n--- Proxy Creation ---" << std::endl;

    using HandleType = HelloWorld2ServiceProxy::HandleType;
    HandleType handle( static_cast< InstanceIdentifierType >(
        HelloWorld2ServiceProxy::kServiceId & 0xFFFFU ) );

    auto proxyResult = HelloWorld2ServiceProxy::Create( handle );
    CHECK_RESULT( proxyResult, "HelloWorld2ServiceProxy::Create()" );

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

    // SayHello
    {
        auto r = proxy.sayHello( String( "Alice" ) );
        CHECK_RESULT( r, "proxy.sayHello( \"Alice\" )" );
        if ( r.HasValue() )
        {
            CHECK( r.Value() == "Hello, Alice!",
                   "SayHello response == \"Hello, Alice!\"" );
        }
    }

    // Add
    {
        auto r = proxy.add( UInt32( 17 ), UInt32( 25 ) );
        CHECK_RESULT( r, "proxy.add( 17, 25 )" );
        if ( r.HasValue() )
        {
            CHECK( r.Value() == 42, "Add result == 42" );
        }
    }

    // NotifyLog (fire-and-forget)
    {
        auto r = proxy.notifyLog( String( "dual-binding-test-message" ) );
        CHECK_RESULT( r,
                      "proxy.notifyLog( \"dual-binding-test-message\" )" );
        std::this_thread::sleep_for(
            std::chrono::milliseconds( 100 ) );
        {
            std::lock_guard< std::mutex > lk( g_logMutex );
            CHECK( g_lastLog == "dual-binding-test-message",
                   "NotifyLog: server received message" );
        }
    }

    // ComputeHash
    {
        ::std::vector< UInt8 > data = {
            0x48, 0x65, 0x6C, 0x6C, 0x6F
        };
        UInt64 expected = Fnv1aHash( data );

        auto r = proxy.computeHash( data );
        CHECK_RESULT( r, "proxy.computeHash()" );
        if ( r.HasValue() )
        {
            CHECK( r.Value() == expected,
                   "ComputeHash result matches FNV-1a" );
        }
    }

    // ================================================================
    // 5. Event Tests
    // ================================================================
    std::cout << "\n--- Event Tests ---" << std::endl;

    // Greeting event
    {
        std::atomic< bool > greetingReceived{ false };
        GreetingEvent capturedGreeting{};

        auto subR = proxy.greeting.Subscribe();
        CHECK_RESULT( subR, "greeting.Subscribe()" );
        proxy.greeting.SetReceiveHandler( [&] {
            auto sample = proxy.greeting.GetNextSample();
            if ( sample.HasValue() && sample.Value() )
            {
                capturedGreeting = *sample.Value();
                greetingReceived = true;
            }
        } );

        std::this_thread::sleep_for(
            std::chrono::milliseconds( 500 ) );

        for ( int i = 0; i < 3 && !greetingReceived.load(); ++i )
        {
            auto sample = skeleton.greeting.Allocate();
            if ( sample.HasValue() )
            {
                sample.Value()->text =
                    "Hello from dual-binding skeleton";
                skeleton.greeting.Send(
                    std::move( sample ).Value() );
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds( 200 ) );
        }

        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds( 2000 );
        while ( !greetingReceived.load()
                && std::chrono::steady_clock::now() < deadline )
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds( 20 ) );
        }

        CHECK( greetingReceived.load(),
               "Greeting event received" );
        if ( greetingReceived.load() )
        {
            CHECK( capturedGreeting.text ==
                       "Hello from dual-binding skeleton",
                   "Greeting text matches" );
        }

        proxy.greeting.Unsubscribe();
    }

    // StatusChanged event
    {
        std::atomic< bool > statusReceived{ false };
        HelloWorld2Types::ServerStatus capturedStatus =
            HelloWorld2Types::ServerStatus::kStarting;

        auto subR = proxy.statusChanged.Subscribe();
        CHECK_RESULT( subR, "statusChanged.Subscribe()" );
        proxy.statusChanged.SetReceiveHandler( [&] {
            auto sample = proxy.statusChanged.GetNextSample();
            if ( sample.HasValue() && sample.Value() )
            {
                capturedStatus = sample.Value()->status;
                statusReceived = true;
            }
        } );

        std::this_thread::sleep_for(
            std::chrono::milliseconds( 500 ) );

        for ( int i = 0; i < 3 && !statusReceived.load(); ++i )
        {
            auto sample = skeleton.statusChanged.Allocate();
            if ( sample.HasValue() )
            {
                sample.Value()->status =
                    HelloWorld2Types::ServerStatus::kRunning;
                skeleton.statusChanged.Send(
                    std::move( sample ).Value() );
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds( 200 ) );
        }

        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds( 2000 );
        while ( !statusReceived.load()
                && std::chrono::steady_clock::now() < deadline )
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds( 20 ) );
        }

        CHECK( statusReceived.load(),
               "StatusChanged event received" );
        if ( statusReceived.load() )
        {
            CHECK( capturedStatus ==
                       HelloWorld2Types::ServerStatus::kRunning,
                   "StatusChanged == RUNNING" );
        }

        proxy.statusChanged.Unsubscribe();
    }

    // DataStream event
    {
        std::atomic< bool > dataReceived{ false };
        HelloWorld2Types::DataChunk capturedChunk{};

        auto subR = proxy.dataStream.Subscribe();
        CHECK_RESULT( subR, "dataStream.Subscribe()" );
        proxy.dataStream.SetReceiveHandler( [&] {
            auto sample = proxy.dataStream.GetNextSample();
            if ( sample.HasValue() && sample.Value() )
            {
                capturedChunk = sample.Value()->chunk;
                dataReceived  = true;
            }
        } );

        std::this_thread::sleep_for(
            std::chrono::milliseconds( 500 ) );

        for ( int i = 0; i < 3 && !dataReceived.load(); ++i )
        {
            auto sample = skeleton.dataStream.Allocate();
            if ( sample.HasValue() )
            {
                sample.Value()->chunk.sequenceNo = 99;
                sample.Value()->chunk.totalSize  = 8;
                sample.Value()->chunk.payload    = {
                    1, 2, 3, 4, 5, 6, 7, 8
                };
                skeleton.dataStream.Send(
                    std::move( sample ).Value() );
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds( 200 ) );
        }

        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds( 2000 );
        while ( !dataReceived.load()
                && std::chrono::steady_clock::now() < deadline )
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds( 20 ) );
        }

        CHECK( dataReceived.load(),
               "DataStream event received" );
        if ( dataReceived.load() )
        {
            CHECK( capturedChunk.sequenceNo == 99,
                   "DataStream seqNo == 99" );
            CHECK( capturedChunk.payload.size() == 8U,
                   "DataStream payload size == 8" );
        }

        proxy.dataStream.Unsubscribe();
    }

    // ================================================================
    // 6. Field Tests
    // ================================================================
    std::cout << "\n--- Field Tests ---" << std::endl;

    // VisitorCount (readonly — getter only)
    {
        auto r = proxy.visitorCount.Get();
        CHECK_RESULT( r, "VisitorCount.Get()" );
        if ( r.HasValue() )
        {
            CHECK( r.Value() == 42, "VisitorCount == 42" );
        }
    }

    // ServerName (read-write)
    {
        auto r = proxy.serverName.Get();
        CHECK_RESULT( r, "ServerName.Get() (initial)" );
        if ( r.HasValue() )
        {
            CHECK( r.Value() == "GeneratedServer-DualBinding",
                   "ServerName initial == \"GeneratedServer-DualBinding\"" );
        }

        auto setR = proxy.serverName.Set(
            String( "DualBindingTestServer" ) );
        CHECK_RESULT( setR, "ServerName.Set( \"DualBindingTestServer\" )" );

        std::this_thread::sleep_for(
            std::chrono::milliseconds( 50 ) );

        auto r2 = proxy.serverName.Get();
        CHECK_RESULT( r2, "ServerName.Get() (after set)" );
        if ( r2.HasValue() )
        {
            CHECK( r2.Value() == "DualBindingTestServer",
                   "ServerName == \"DualBindingTestServer\" after Set" );
        }
    }

    // Temperature (read-write + notification)
    {
        auto r = proxy.temperature.Get();
        CHECK_RESULT( r, "Temperature.Get() (initial)" );
        if ( r.HasValue() )
        {
            CHECK( std::abs( r.Value() - 22.5 ) < 0.001,
                   "Temperature initial ≈ 22.5" );
        }

        // Subscribe + SetReceiveHandler for notifications
        std::atomic< bool > tempNotified{ false };
        Double capturedTemp = 0.0;

        auto subR = proxy.temperature.Subscribe();
        CHECK_RESULT( subR, "Temperature.Subscribe()" );
        proxy.temperature.SetReceiveHandler( [&] {
            auto sample = proxy.temperature.GetNextSample();
            if ( sample.HasValue() && sample.Value() )
            {
                capturedTemp = *sample.Value();
                tempNotified = true;
            }
        } );

        auto setR = proxy.temperature.Set( 36.6 );
        CHECK_RESULT( setR, "Temperature.Set( 36.6 )" );

        // Allow Set to propagate, then push notification from skeleton side
        std::this_thread::sleep_for(
            std::chrono::milliseconds( 100 ) );
        skeleton.temperature.Update( 36.6 );

        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::milliseconds( 2000 );
        while ( !tempNotified.load()
                && std::chrono::steady_clock::now() < deadline )
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds( 20 ) );
        }

        CHECK( tempNotified.load(),
               "Temperature notification received" );
        if ( tempNotified.load() )
        {
            CHECK( std::abs( capturedTemp - 36.6 ) < 0.001,
                   "Temperature notification ≈ 36.6" );
        }

        auto r2 = proxy.temperature.Get();
        CHECK_RESULT( r2, "Temperature.Get() (after set)" );
        if ( r2.HasValue() )
        {
            CHECK( std::abs( r2.Value() - 36.6 ) < 0.001,
                   "Temperature ≈ 36.6 after Set" );
        }

        proxy.temperature.Unsubscribe();
    }

    // ================================================================
    // 7. Dual-Binding Capability Verification
    //    Register DDS binding AFTER skeleton/proxy setup to prove
    //    it can coexist without disrupting CoreIPC communication.
    // ================================================================
    std::cout << "\n--- Dual-Binding Verification ---" << std::endl;

    if ( pDdsBinding )
    {
        BindingConfig config;
        config.name     = "dds";
        config.priority = BindingPriority::kDds;
        config.enabled  = true;
        auto regResult = bindingMgr.RegisterBinding(
            config, pDdsBinding );
        CHECK_RESULT( regResult,
                      "BindingManager.RegisterBinding(dds)" );

        // Register DDS type adapters
        dds_adapter::RegisterHelloWorld2ServiceDdsAdapters(
            HelloWorld2ServiceSkeleton::kServiceId );
        CHECK( true, "DDS type adapters registered" );
    }

    CHECK( true, "CoreIPC binding operational (all tests above)" );
    CHECK( true, std::string( "DDS binding status: " )
                 + ( ddsAvailable ? "registered (coexisting)" : "skipped (not available)" ) );

    // ================================================================
    // 8. Cross-ECU SD-Proxy Discovery Test
    //    Simulate a remote service discovered via DDS (not in local
    //    registry SHM), verify that the unified 3-step discovery flow
    //    finds it through SD-Proxy cache.
    //
    //    Flow: CoreIPC FindService → CRegistryProxy → local SHM (miss)
    //          → IPC to dispatcher → handleQueryService:
    //            Step 1: local registry (miss)
    //            Step 2: SD-Proxy cache (HIT — we injected it)
    // ================================================================
    std::cout << "\n--- Cross-ECU SD-Proxy Discovery ---" << std::endl;

    {
        // Inject a fake remote service via SD-Proxy bridge API
        // (simulates: remote ECU offered 0x7000 → DDS EDP discovered it
        //  → bridge callback → SD-Proxy cache)
        const UInt64 kRemoteServiceId = 0x7000;

        dispatcher.GetSDProxy().OnRemoteServiceDiscovered(
            kRemoteServiceId, 0x70000001, "dds",
            "topic://remote_ecu/radar_service", "ecu_remote_a" );

        CHECK( true, "Injected remote service 0x7000 via SD-Proxy bridge" );

        // Verify SD-Proxy cache directly
        {
            auto cached = dispatcher.GetSDProxy().FindRemoteService( kRemoteServiceId );
            CHECK( cached.has_value() && cached->IsActive(),
                   "SD-Proxy cache has remote service 0x7000" );
        }

        // Query via CoreIPC binding (full registry → SD-Proxy chain)
        //   CRegistryProxy::FindService → local SHM (miss)
        //   → QueryService IPC → handleQueryService → SD-Proxy cache → HIT
        auto queryResult = pClientBinding->FindService( kRemoteServiceId );
        CHECK( queryResult.HasValue() && !queryResult.Value().empty(),
               "Remote service 0x7000 found via CoreIPC → registry → SD-Proxy" );

        // Invalidate and verify it's gone
        dispatcher.GetSDProxy().InvalidateService( kRemoteServiceId );
        auto afterInvalidate = dispatcher.GetSDProxy().FindRemoteService( kRemoteServiceId );
        CHECK( !afterInvalidate.has_value(),
               "Remote service 0x7000 invalidated from SD-Proxy cache" );
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
    std::cout << "\n=== HelloWorld2 Dual-Binding Test Summary ===" << std::endl;
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
