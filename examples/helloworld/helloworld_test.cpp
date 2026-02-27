/**
 * @file        helloworld_test.cpp
 * @author      Aii
 * @brief       HelloWorld single-process integration test (CTest-compatible)
 * @date        2026-02-11
 * @details     Exercises the FULL generated Proxy/Skeleton API in a single
 *              process via CoreIPC shared-memory binding.
 *
 *              Verifies (46 checks):
 *                Infrastructure (5) : dispatcher, bindings, binding manager
 *                Methods       (8) : SayHello, Add, NotifyLog(F&F), ComputeHash
 *                Events        (7) : Greeting, StatusChanged, DataStream
 *                Fields       (14) : VisitorCount(ro), ServerName(rw),
 *                                    Temperature(rw + notification)
 *                Proxy         (1) : Create from handle
 *                Handler reg  (11) : All handlers registered successfully
 *
 *              NOTE: Same-process event testing requires TWO separate
 *              CoreIPCBinding instances: one for the skeleton (publisher) and
 *              one for the proxy (subscriber), so that publisher/subscriber
 *              shared-memory channels are connected correctly.
 *
 * @copyright   Copyright (c) 2026
 */

// ==================== Generated Headers ====================
#include "HelloWorldProxy.hpp"
#include "HelloWorldSkeleton.hpp"

// ==================== Binding / Infrastructure ====================
#include "CoreIPCBinding.hpp"
#include "CRegistryDispatcher.hpp"
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
using namespace lap::com::examples;
using namespace lap::com::examples::proxy;
using namespace lap::com::examples::skeleton;
using namespace lap::com::binding;
using namespace lap::com::registry;

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
    std::cout << "=== HelloWorld Integration Test ===" << std::endl;
    std::cout << "Service ID  : 0x" << std::hex
              << HelloWorldProxy::kServiceId << std::dec << std::endl;
    std::cout << "Schema Hash : " << HelloWorldProxy::kSchemaHash
              << std::endl;
    std::cout << std::endl;

    // ================================================================
    // 1. Infrastructure: registry + two bindings
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

    // Two binding instances for same-process testing:
    //   serverBinding  — skeleton (publisher side)
    //   clientBinding  — proxy   (subscriber side)
    auto pServerBinding = MakeShared< CoreIPCBinding >();
    auto pClientBinding = MakeShared< CoreIPCBinding >();
    auto sInit = pServerBinding->Initialize();
    CHECK_RESULT( sInit, "ServerBinding.Initialize()" );
    auto cInit = pClientBinding->Initialize();
    CHECK_RESULT( cInit, "ClientBinding.Initialize()" );

    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

    // Register server binding first (low priority) for skeleton
    auto& bindingMgr = BindingManager::GetInstance();
    {
        BindingConfig config;
        config.name     = "coreipc-server";
        config.priority = BindingPriority::kCustom;  // lower priority
        config.enabled  = true;
        auto regResult = bindingMgr.RegisterBinding(
            config, pServerBinding );
        CHECK_RESULT( regResult,
                      "BindingManager.RegisterBinding(server)" );
    }

    // ================================================================
    // 2. Skeleton — create, offer, register handlers
    // ================================================================
    std::cout << "\n--- Skeleton Setup ---" << std::endl;

    HelloWorldSkeleton skeleton(
        lap::core::InstanceSpecifier( "HelloWorld/Provider" ) );

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
    static std::string g_serverName = "GeneratedServer";
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
        config.priority = BindingPriority::kIceoryx2;  // higher
        config.enabled  = true;
        auto regResult = bindingMgr.RegisterBinding(
            config, pClientBinding );
        CHECK_RESULT( regResult,
                      "BindingManager.RegisterBinding(client)" );
    }

    // ================================================================
    // 3. Proxy — create from handle
    // ================================================================
    std::cout << "\n--- Proxy Creation ---" << std::endl;

    using HandleType = HelloWorldProxy::HandleType;
    HandleType handle( static_cast< InstanceIdentifierType >(
        HelloWorldProxy::kServiceId & 0xFFFFU ) );

    auto proxyResult = HelloWorldProxy::Create( handle );
    CHECK_RESULT( proxyResult, "HelloWorldProxy::Create()" );

    if ( !proxyResult.HasValue() )
    {
        std::cerr << "Proxy creation failed — aborting." << std::endl;
        skeleton.StopOfferService();
        pClientBinding->Shutdown();
        pServerBinding->Shutdown();
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
        auto r = proxy.notifyLog( String( "gen-test-message" ) );
        CHECK_RESULT( r,
                      "proxy.notifyLog( \"gen-test-message\" )" );
        std::this_thread::sleep_for(
            std::chrono::milliseconds( 100 ) );
        {
            std::lock_guard< std::mutex > lk( g_logMutex );
            CHECK( g_lastLog == "gen-test-message",
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
                sample.Value()->message.text =
                    "Hello from skeleton";
                sample.Value()->message.timestamp = 1234567890ULL;
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
            CHECK( capturedGreeting.message.text
                       == "Hello from skeleton",
                   "Greeting event text matches" );
        }
        proxy.greeting.Unsubscribe();
    }

    // StatusChanged event
    {
        std::atomic< bool > statusReceived{ false };
        StatusChangedEvent capturedStatus{};

        proxy.statusChanged.Subscribe();
        proxy.statusChanged.SetReceiveHandler( [&] {
            auto sample = proxy.statusChanged.GetNextSample();
            if ( sample.HasValue() && sample.Value() )
            {
                capturedStatus = *sample.Value();
                statusReceived = true;
            }
        } );

        std::this_thread::sleep_for(
            std::chrono::milliseconds( 50 ) );

        {
            auto sample = skeleton.statusChanged.Allocate();
            if ( sample.HasValue() )
            {
                sample.Value()->status =
                    HelloWorldTypes::ServerStatus::kRunning;
                skeleton.statusChanged.Send(
                    std::move( sample ).Value() );
            }
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
            CHECK( capturedStatus.status
                       == HelloWorldTypes::ServerStatus::kRunning,
                   "StatusChanged == kRunning" );
        }
        proxy.statusChanged.Unsubscribe();
    }

    // DataStream event
    {
        std::atomic< bool > dataReceived{ false };
        DataStreamEvent capturedData{};

        proxy.dataStream.Subscribe();
        proxy.dataStream.SetReceiveHandler( [&] {
            auto sample = proxy.dataStream.GetNextSample();
            if ( sample.HasValue() && sample.Value() )
            {
                capturedData = *sample.Value();
                dataReceived = true;
            }
        } );

        std::this_thread::sleep_for(
            std::chrono::milliseconds( 50 ) );

        {
            auto sample = skeleton.dataStream.Allocate();
            if ( sample.HasValue() )
            {
                sample.Value()->chunk.sequenceNo = 1;
                sample.Value()->chunk.totalSize  = 5;
                sample.Value()->chunk.payload    = {
                    0xDE, 0xAD, 0xBE, 0xEF, 0x00
                };
                skeleton.dataStream.Send(
                    std::move( sample ).Value() );
            }
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
            CHECK( capturedData.chunk.sequenceNo == 1,
                   "DataStream sequenceNo == 1" );
            CHECK( capturedData.chunk.payload.size() == 5,
                   "DataStream payload size == 5" );
        }
        proxy.dataStream.Unsubscribe();
    }

    // ================================================================
    // 6. Field Tests
    // ================================================================
    std::cout << "\n--- Field Tests ---" << std::endl;

    // VisitorCount — readonly (getter only)
    {
        auto r = proxy.visitorCount.Get();
        CHECK_RESULT( r, "proxy.visitorCount.Get()" );
        if ( r.HasValue() )
        {
            CHECK( r.Value() == 42, "VisitorCount == 42" );
        }
    }

    // ServerName — read-write (getter + setter)
    {
        auto r = proxy.serverName.Get();
        CHECK_RESULT( r, "proxy.serverName.Get()" );
        if ( r.HasValue() )
        {
            CHECK( r.Value() == "GeneratedServer",
                   "ServerName initial == \"GeneratedServer\"" );
        }

        auto setResult = proxy.serverName.Set(
            String( "NewServerName" ) );
        CHECK_RESULT( setResult, "proxy.serverName.Set()" );

        std::this_thread::sleep_for(
            std::chrono::milliseconds( 50 ) );

        auto r2 = proxy.serverName.Get();
        CHECK_RESULT( r2, "proxy.serverName.Get() after Set" );
        if ( r2.HasValue() )
        {
            CHECK( r2.Value() == "NewServerName",
                   "ServerName after Set == \"NewServerName\"" );
        }
    }

    // Temperature — read-write + notification
    {
        auto r = proxy.temperature.Get();
        CHECK_RESULT( r, "proxy.temperature.Get()" );
        if ( r.HasValue() )
        {
            CHECK( std::abs( r.Value() - 22.5 ) < 0.001,
                   "Temperature initial ~= 22.5" );
        }

        auto setResult = proxy.temperature.Set( 36.6 );
        CHECK_RESULT( setResult,
                      "proxy.temperature.Set( 36.6 )" );

        std::this_thread::sleep_for(
            std::chrono::milliseconds( 50 ) );

        auto r2 = proxy.temperature.Get();
        CHECK_RESULT( r2,
                      "proxy.temperature.Get() after Set" );
        if ( r2.HasValue() )
        {
            CHECK( std::abs( r2.Value() - 36.6 ) < 0.001,
                   "Temperature after Set ~= 36.6" );
        }

        // Notification test
        std::atomic< bool > tempNotified{ false };
        Double notifiedTemp = 0.0;

        proxy.temperature.Subscribe();
        proxy.temperature.SetReceiveHandler( [&] {
            auto sample = proxy.temperature.GetNextSample();
            if ( sample.HasValue() && sample.Value() )
            {
                notifiedTemp = *sample.Value();
                tempNotified = true;
            }
        } );

        std::this_thread::sleep_for(
            std::chrono::milliseconds( 50 ) );

        auto updateR = skeleton.temperature.Update( 99.9 );
        if ( updateR.HasValue() )
        {
            auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds( 2000 );
            while ( !tempNotified.load()
                    && std::chrono::steady_clock::now()
                           < deadline )
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds( 20 ) );
            }
            CHECK( tempNotified.load(),
                   "Temperature notification received" );
            if ( tempNotified.load() )
            {
                CHECK( std::abs( notifiedTemp - 99.9 ) < 0.001,
                       "Temperature notification value ~= 99.9" );
            }
        }

        proxy.temperature.Unsubscribe();
    }

    // ================================================================
    // 7. Cleanup
    // ================================================================
    std::cout << "\n--- Cleanup ---" << std::endl;

    skeleton.StopOfferService();
    pClientBinding->Shutdown();
    pServerBinding->Shutdown();
    dispatcher.Shutdown();
    if ( dispatcherThread.joinable() )
    {
        dispatcherThread.join();
    }
    bindingMgr.Shutdown();

    // ================================================================
    // Summary
    // ================================================================
    std::cout << "\n========================================" << std::endl;
    std::cout << "Results: " << ( g_totalTests - g_failedTests )
              << " / " << g_totalTests << " passed" << std::endl;

    if ( g_failedTests > 0 )
    {
        std::cout << "FAILED (" << g_failedTests << " failures)"
                  << std::endl;
    }
    else
    {
        std::cout << "ALL PASSED" << std::endl;
    }
    std::cout << "========================================" << std::endl;

    return g_failedTests == 0 ? 0 : 1;
}
