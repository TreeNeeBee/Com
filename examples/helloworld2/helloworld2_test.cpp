/**
 * @file        helloworld2_test.cpp
 * @author      Aii
 * @brief       HelloWorld2 integration test — uses generated Proxy
 * @date        2026-02-23
 * @details     Integration test binary that connects to a running HelloWorld2
 *              server and exercises all communication patterns systematically.
 *              Intended to be orchestrated by run_helloworld2_test.sh.
 *
 *              Tested patterns:
 *                Methods  (4): SayHello, Add, NotifyLog(F&F), ComputeHash
 *                Events   (3): Greeting, StatusChanged, DataStream
 *                Fields   (3): VisitorCount(ro), ServerName(rw), Temperature(rw)
 *
 * @copyright   Copyright (c) 2026
 */

// ==================== Generated Headers (DO NOT EDIT) ====================
#include "gen/HelloWorld2ServiceProxy.hpp"
#include "gen/HelloWorld2ServiceDdsAdapter.hpp"

// ==================== Binding / Infrastructure ====================
#include "DdsBinding.hpp"
#include "BindingManager.hpp"

// ==================== Standard Library ====================
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace lap::com;
using namespace lap::com::binding;
using namespace helloworld2;
using namespace helloworld2::proxy;

// ========================================================================
// Test helpers
// ========================================================================
static int  g_pass = 0;
static int  g_fail = 0;

static void CHECK( bool cond, const char* desc )
{
    if ( cond ) {
        std::cout << "  [PASS] " << desc << std::endl;
        ++g_pass;
    } else {
        std::cerr << "  [FAIL] " << desc << std::endl;
        ++g_fail;
    }
}

// ========================================================================
// main
// ========================================================================
int main()
{
    std::cout << "=== HelloWorld2 Integration Test (Generated Proxy) ===" << std::endl;

    // ================================================================
    // Setup — CoreIPC binding
    // ================================================================
    auto pBinding = MakeShared< DdsBinding >();
    if ( !pBinding->Initialize() )
    {
        std::cerr << "[ERROR] Binding init failed." << std::endl;
        return 1;
    }
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

    auto& bindingMgr = BindingManager::GetInstance();
    {
        BindingConfig config;
        config.name     = "dds-test";
        config.priority = BindingPriority::kDds;
        config.enabled  = true;
        if ( !bindingMgr.RegisterBinding( config, pBinding ).HasValue() )
        {
            std::cerr << "[ERROR] RegisterBinding failed." << std::endl;
            pBinding->Shutdown();
            return 1;
        }
    }

    // Register DDS type adapters (must match server's adapter registration)
    helloworld2::dds_adapter::RegisterHelloWorld2ServiceDdsAdapters(
        HelloWorld2ServiceProxy::kServiceId );
    std::cout << "[Test] DDS type adapters registered." << std::endl;

    // ================================================================
    // Service Discovery — wait up to 6 s for server
    // ================================================================
    bool found = false;
    for ( int i = 0; i < 30 && !found; ++i )
    {
        auto r = pBinding->FindService( HelloWorld2ServiceProxy::kServiceId );
        if ( r.HasValue() && !r.Value().empty() )
        {
            found = true;
        }
        else
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
        }
    }

    if ( !found )
    {
        std::cerr << "[ERROR] Server not found. Aborting." << std::endl;
        pBinding->Shutdown();
        bindingMgr.Shutdown();
        return 1;
    }
    std::cout << "[Test] Server discovered." << std::endl;

    // Create Proxy
    using HandleType = HelloWorld2ServiceProxy::HandleType;
    HandleType handle( static_cast< InstanceIdentifierType >(
        HelloWorld2ServiceProxy::kServiceId & 0xFFFFU ) );
    auto proxyR = HelloWorld2ServiceProxy::Create( handle );
    if ( !proxyR.HasValue() )
    {
        std::cerr << "[ERROR] Proxy::Create failed." << std::endl;
        pBinding->Shutdown();
        bindingMgr.Shutdown();
        return 1;
    }
    auto proxy = std::move( proxyR ).Value();
    std::cout << "[Test] Proxy ready." << std::endl;

    // ================================================================
    // Test: Events — subscribe and collect samples for 2 s
    // ================================================================
    std::cout << "\n[Test] Events ---" << std::endl;

    std::atomic< int > greetingCount{ 0 };
    std::atomic< int > statusCount{ 0 };
    std::atomic< int > dataStreamCount{ 0 };
    bool statusRunning = false;

    proxy.greeting.Subscribe();
    proxy.greeting.SetReceiveHandler( [&] {
        auto s = proxy.greeting.GetNextSample();
        if ( s.HasValue() && s.Value() )
        {
            ++greetingCount;
        }
    } );

    proxy.statusChanged.Subscribe();
    proxy.statusChanged.SetReceiveHandler( [&] {
        auto s = proxy.statusChanged.GetNextSample();
        if ( s.HasValue() && s.Value() )
        {
            ++statusCount;
            if ( s.Value()->status ==
                 HelloWorld2Types::ServerStatus::kRunning )
            {
                statusRunning = true;
            }
        }
    } );

    proxy.dataStream.Subscribe();
    proxy.dataStream.SetReceiveHandler( [&] {
        auto s = proxy.dataStream.GetNextSample();
        if ( s.HasValue() && s.Value() )
        {
            ++dataStreamCount;
            CHECK( s.Value()->chunk.payload.size() > 0,
                   "DataStream chunk has payload" );
        }
    } );

    std::this_thread::sleep_for( std::chrono::seconds( 4 ) );

    CHECK( greetingCount.load() > 0,    "Received at least 1 Greeting event" );
    CHECK( statusCount.load()   > 0,    "Received at least 1 StatusChanged event" );
    CHECK( statusRunning,               "StatusChanged contains kRunning" );
    CHECK( dataStreamCount.load() > 0,  "Received at least 1 DataStream chunk" );

    // ================================================================
    // Test: Methods
    // ================================================================
    std::cout << "\n[Test] Methods ---" << std::endl;

    // SayHello
    {
        auto r = proxy.sayHello( String( "TestClient" ) );
        CHECK( r.HasValue(),                 "SayHello returned value" );
        CHECK( r.HasValue() && !r.Value().empty(),
               "SayHello response is non-empty" );
        CHECK( r.HasValue() && r.Value().find( "TestClient" ) != String::npos,
               "SayHello response contains visitor name" );
        if ( r.HasValue() )
        {
            std::cout << "         response: \"" << r.Value() << "\"" << std::endl;
        }
    }

    // Add
    {
        auto r = proxy.add( UInt32( 100 ), UInt32( 200 ) );
        CHECK( r.HasValue(),                 "Add returned value" );
        CHECK( r.HasValue() && r.Value() == 300,
               "Add(100, 200) = 300" );
    }

    // NotifyLog (fire-and-forget — no return value to test)
    {
        proxy.notifyLog( String( "Integration test log message" ) );
        CHECK( true, "NotifyLog sent (fire-and-forget)" );
    }

    // ComputeHash
    {
        ByteArray payload = { 0x48, 0x65, 0x6C, 0x6C, 0x6F };  // "Hello"
        auto r = proxy.computeHash( payload );
        CHECK( r.HasValue(), "ComputeHash returned value" );

        // Expected FNV-1a 64-bit hash of "Hello"
        constexpr UInt64 kExpectedHash = 0x63f0bfacf2c00f6bULL;
        CHECK( r.HasValue() && r.Value() == kExpectedHash,
               "ComputeHash(\"Hello\") = expected FNV-1a value" );
        if ( r.HasValue() )
        {
            std::cout << "         hash: 0x" << std::hex
                      << r.Value() << std::dec << std::endl;
        }
    }

    // ================================================================
    // Test: Method Error Handling (server-side input validation)
    // ================================================================
    std::cout << "\n[Test] Method Error Handling ---" << std::endl;

    // SayHello with empty name — server validates and rejects
    {
        auto r = proxy.sayHello( String( "" ) );
        CHECK( !r.HasValue(),
               "SayHello(\"\") returns error (not a value)" );
        CHECK( !r.HasValue() &&
               r.Error() == MakeErrorCode( ComErrc::kInvalidArgument, 0 ),
               "SayHello(\"\") error is kInvalidArgument" );
        if ( !r.HasValue() )
        {
            std::cout << "         error: " << r.Error().Message() << std::endl;
        }
    }

    // ComputeHash with empty payload — server validates and rejects
    {
        ByteArray emptyPayload{};
        auto r = proxy.computeHash( emptyPayload );
        CHECK( !r.HasValue(),
               "ComputeHash(empty) returns error (not a value)" );
        CHECK( !r.HasValue() &&
               r.Error() == MakeErrorCode( ComErrc::kInvalidArgument, 0 ),
               "ComputeHash(empty) error is kInvalidArgument" );
        if ( !r.HasValue() )
        {
            std::cout << "         error: " << r.Error().Message() << std::endl;
        }
    }

    // ================================================================
    // Test: Fields
    // ================================================================
    std::cout << "\n[Test] Fields ---" << std::endl;

    // VisitorCount (readonly)
    {
        auto r = proxy.visitorCount.Get();
        CHECK( r.HasValue(),               "VisitorCount.Get succeeded" );
        CHECK( r.HasValue() && r.Value() >= 1,
               "VisitorCount >= 1 (SayHello incremented it)" );
        if ( r.HasValue() )
        {
            std::cout << "         VisitorCount = " << r.Value() << std::endl;
        }
    }

    // ServerName (read-write)
    {
        auto rGet = proxy.serverName.Get();
        CHECK( rGet.HasValue(), "ServerName.Get succeeded" );

        auto rSet = proxy.serverName.Set( String( "LightAP-Test-Renamed" ) );
        CHECK( rSet.HasValue(), "ServerName.Set succeeded" );

        auto rGet2 = proxy.serverName.Get();
        CHECK( rGet2.HasValue() &&
               rGet2.Value() == "LightAP-Test-Renamed",
               "ServerName reflects new value after Set" );
    }

    // Temperature (read-write + field notify)
    {
        auto rGet = proxy.temperature.Get();
        CHECK( rGet.HasValue(), "Temperature.Get succeeded" );

        auto rSet = proxy.temperature.Set( Double( 42.0 ) );
        CHECK( rSet.HasValue(), "Temperature.Set succeeded" );

        auto rGet2 = proxy.temperature.Get();
        CHECK( rGet2.HasValue() && rGet2.Value() == 42.0,
               "Temperature reflects new value after Set" );
    }

    // ================================================================
    // Test: Field Notifications (DDS — SubscribeFieldNotification)
    // ================================================================
    std::cout << "\n[Test] Field Notifications ---" << std::endl;

    // VisitorCount — readonly + notify: SayHello increments and pushes update
    std::atomic< int > visitorNotifyCount{ 0 };
    UInt32 lastVisitorCount = 0;
    {
        auto subR = proxy.visitorCount.Subscribe( 4 );
        CHECK( subR.HasValue(), "VisitorCount.Subscribe succeeded" );
        proxy.visitorCount.SetReceiveHandler( [&] {
            auto s = proxy.visitorCount.GetNextSample();
            if ( s.HasValue() && s.Value() )
            {
                ++visitorNotifyCount;
                lastVisitorCount = *s.Value();
            }
        } );
    }

    // ServerName — notify: Set on proxy triggers server Update → notification
    std::atomic< bool > serverNameNotified{ false };
    String notifiedServerName;
    {
        auto subR = proxy.serverName.Subscribe( 4 );
        CHECK( subR.HasValue(), "ServerName.Subscribe succeeded" );
        proxy.serverName.SetReceiveHandler( [&] {
            auto s = proxy.serverName.GetNextSample();
            if ( s.HasValue() && s.Value() )
            {
                serverNameNotified = true;
                notifiedServerName = *s.Value();
            }
        } );
    }

    // Temperature — notify: Set on proxy triggers server Update → notification
    std::atomic< bool > tempNotified{ false };
    Double notifiedTemp = 0.0;
    {
        auto subR = proxy.temperature.Subscribe( 4 );
        CHECK( subR.HasValue(), "Temperature.Subscribe succeeded" );
        proxy.temperature.SetReceiveHandler( [&] {
            auto s = proxy.temperature.GetNextSample();
            if ( s.HasValue() && s.Value() )
            {
                tempNotified = true;
                notifiedTemp = *s.Value();
            }
        } );
    }

    // Trigger ServerName notification
    proxy.serverName.Set( String( "LightAP-NotifyTest" ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 600 ) );
    CHECK( serverNameNotified.load(), "ServerName field notification received" );
    CHECK( serverNameNotified.load() &&
           notifiedServerName == "LightAP-NotifyTest",
           "ServerName notification carries updated value" );

    // Trigger Temperature notification
    proxy.temperature.Set( Double( 99.0 ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 600 ) );
    CHECK( tempNotified.load(), "Temperature field notification received" );
    CHECK( tempNotified.load() && notifiedTemp == 99.0,
           "Temperature notification carries updated value" );

    // Trigger VisitorCount notification via SayHello
    proxy.sayHello( String( "NotifyTester" ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 600 ) );
    CHECK( visitorNotifyCount.load() > 0,
           "VisitorCount field notification received after SayHello" );
    if ( visitorNotifyCount.load() > 0 )
    {
        std::cout << "         VisitorCount notification value = "
                  << lastVisitorCount << std::endl;
    }

    // Unsubscribe field notifications
    proxy.visitorCount.Unsubscribe();
    proxy.serverName.Unsubscribe();
    proxy.temperature.Unsubscribe();

    // ================================================================
    // Cleanup
    // ================================================================
    proxy.greeting.Unsubscribe();
    proxy.statusChanged.Unsubscribe();
    proxy.dataStream.Unsubscribe();

    pBinding->Shutdown();
    bindingMgr.Shutdown();

    // ================================================================
    // Result
    // ================================================================
    std::cout << "\n======================================" << std::endl;
    std::cout << "  PASSED: " << g_pass << std::endl;
    std::cout << "  FAILED: " << g_fail << std::endl;
    std::cout << "======================================" << std::endl;

    return ( g_fail == 0 ) ? 0 : 1;
}
