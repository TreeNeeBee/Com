/**
 * @file        helloworld2_server.cpp
 * @author      Aii
 * @brief       HelloWorld2 Server — standard-flow refactor using generated Skeleton
 * @date        2026-02-23
 * @details     Development workflow (standard AUTOSAR AP flow):
 *
 *                Step 1.  Define service interface in HelloWorld2.fidl
 *
 *                Step 2.  Run generator (or: cmake --build . --target helloworld2_generate):
 *                         lap_sidl_gen \
 *                             --input HelloWorld2.fidl --output gen/ --author Aii --all
 *
 *                Step 3.  Implement server (this file):
 *                         - Register DDS type adapters (generated DdsAdapter header)
 *                         - Initialize DdsBinding
 *                         - Create HelloWorld2ServiceSkeleton
 *                         - Register method / field handlers
 *                         - OfferService
 *                         - Send events in main loop
 *
 *              All serialization is handled by the generated skeleton and DDS
 *              binding — no manual memcpy or binary manipulation required.
 *
 *              Communication patterns demonstrated:
 *                Methods : SayHello, Add, NotifyLog (F&F), ComputeHash
 *                Events  : Greeting, StatusChanged, DataStream
 *                Fields  : VisitorCount (ro), ServerName (rw), Temperature (rw)
 *
 * @copyright   Copyright (c) 2026
 */

// ==================== Generated Headers (DO NOT EDIT) ====================
#include "gen/HelloWorld2ServiceSkeleton.hpp"

// ==================== Binding / Infrastructure ====================
#include "DdsBinding.hpp"
#include "BindingManager.hpp"

// ==================== DDS Type Adapters (auto-generated from FIDL) ====================
#include "gen/HelloWorld2ServiceDdsAdapter.hpp"

// ==================== Standard Library ====================
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <thread>

using namespace lap::com;
using namespace lap::com::binding;
using namespace helloworld2;
using namespace helloworld2::skeleton;
using lap::core::ScopedLock;
using lap::core::Mutex;

// ========================================================================
// Signal Handler
// ========================================================================
static std::atomic< bool > g_running{ true };

void signalHandler( int ) { g_running.store( false ); }

// ========================================================================
// Helper — wrap a typed value in a resolved Future
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
// FNV-1a 64-bit hash (mirrors CSchemaHash — used by ComputeHash method)
// ========================================================================
static UInt64 Fnv1aHash( const ByteArray& data )
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
    std::signal( SIGINT, signalHandler );
    std::signal( SIGTERM, signalHandler );

    std::cout << "=== HelloWorld2 Server (Generated Skeleton) ===" << std::endl;
    std::cout << "Service : " << HelloWorld2ServiceSkeleton::kServiceName
              << "  ID=0x" << std::hex << HelloWorld2ServiceSkeleton::kServiceId
              << std::dec << std::endl;
    std::cout << "Schema  : " << HelloWorld2ServiceSkeleton::kSchemaHash
              << std::endl;

    // ================================================================
    // Phase 1 — Register DDS Type Adapters (auto-generated from HelloWorld2.fidl)
    // ================================================================
    helloworld2::dds_adapter::RegisterHelloWorld2ServiceDdsAdapters(
        static_cast< UInt64 >( HelloWorld2ServiceSkeleton::kServiceId ) );
    std::cout << "[Server] DDS type adapters registered." << std::endl;

    // ================================================================
    // Phase 2 — Initialize DDS Binding
    // ================================================================
    auto pBinding = MakeShared< DdsBinding >();
    auto initR = pBinding->Initialize();
    if ( !initR )
    {
        std::cerr << "[ERROR] DDS binding init: "
                  << initR.Error().Message() << std::endl;
        return 1;
    }
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

    auto& bindingMgr = BindingManager::GetInstance();
    {
        BindingConfig config;
        config.name     = "dds-server";
        config.priority = BindingPriority::kDds;
        config.enabled  = true;
        auto regR = bindingMgr.RegisterBinding( config, pBinding );
        if ( !regR.HasValue() )
        {
            std::cerr << "[ERROR] RegisterBinding failed." << std::endl;
            pBinding->Shutdown();
            return 1;
        }
    }
    std::cout << "[Server] DDS binding registered." << std::endl;

    // ================================================================
    // Phase 3 — Create Skeleton
    // ================================================================
    HelloWorld2ServiceSkeleton skeleton(
        lap::core::InstanceSpecifier( "HelloWorld2/Provider" ) );

    // ================================================================
    // Phase 4 — Shared Application State
    // ================================================================
    Mutex  stateMutex;
    UInt32 visitorCount = 0;
    String serverName   = "LightAP-HelloWorld2";
    Double temperature  = 25.0;

    // ================================================================
    // Phase 5 — Register Method Handlers
    // ================================================================

    // SayHello : String visitorName -> String greeting  (request/response)
    skeleton.sayHello.RegisterMethodHandler(
        [&stateMutex, &visitorCount, &skeleton]( String visitorName )
            -> lap::core::Future< String >
        {
            UInt32 visitor;
            {
                ScopedLock< Mutex > lk( stateMutex );
                visitor = ++visitorCount;
            }
            // Push VisitorCount field notification to all DDS subscribers
            skeleton.visitorCount.Update( visitor );
            String reply = "Hello, " + visitorName + "! You are visitor #"
                           + std::to_string( visitor ) + ". Welcome to LightAP.";
            std::cout << "[Server] SayHello(\"" << visitorName
                      << "\") -> visitor #" << visitor << std::endl;
            return MakeReadyFuture< String >( std::move( reply ) );
        } );

    // Add : (UInt32 a, UInt32 b) -> UInt32 sum  (request/response)
    skeleton.add.RegisterMethodHandler(
        []( UInt32 a, UInt32 b ) -> lap::core::Future< UInt32 >
        {
            UInt32 sum = a + b;
            std::cout << "[Server] Add(" << a << ", " << b
                      << ") = " << sum << std::endl;
            return MakeReadyFuture< UInt32 >( sum );
        } );

    // NotifyLog : String message -> void  (fire-and-forget, no response)
    skeleton.notifyLog.RegisterMethodHandler(
        []( String message )
        {
            std::cout << "[Server] LOG: " << message << std::endl;
        } );

    // ComputeHash : ByteArray data -> UInt64 hash  (request/response)
    skeleton.computeHash.RegisterMethodHandler(
        []( ByteArray data ) -> lap::core::Future< UInt64 >
        {
            UInt64 hash = Fnv1aHash( data );
            std::cout << "[Server] ComputeHash(" << data.size()
                      << " bytes) = 0x" << std::hex << hash
                      << std::dec << std::endl;
            return MakeReadyFuture< UInt64 >( hash );
        } );

    // ================================================================
    // Phase 6 — Register Field Handlers
    // ================================================================

    // VisitorCount — readonly getter
    skeleton.visitorCount.RegisterGetHandler(
        [&stateMutex, &visitorCount]() -> lap::core::Future< UInt32 >
        {
            ScopedLock< Mutex > lk( stateMutex );
            return MakeReadyFuture< UInt32 >( visitorCount );
        } );

    // ServerName — read-write getter + setter
    skeleton.serverName.RegisterGetHandler(
        [&stateMutex, &serverName]() -> lap::core::Future< String >
        {
            ScopedLock< Mutex > lk( stateMutex );
            return MakeReadyFuture< String >( serverName );
        } );

    skeleton.serverName.RegisterSetHandler(
        [&stateMutex, &serverName, &skeleton]( const String& value )
            -> lap::core::Future< void >
        {
            {
                ScopedLock< Mutex > lk( stateMutex );
                serverName = value;
            }
            // Push ServerName field notification to all DDS subscribers
            skeleton.serverName.Update( value );
            std::cout << "[Server] ServerName SET -> \""
                      << serverName << "\"" << std::endl;
            return MakeReadyVoidFuture();
        } );

    // Temperature — read-write getter + setter + field notifications via DDS
    skeleton.temperature.RegisterGetHandler(
        [&stateMutex, &temperature]() -> lap::core::Future< Double >
        {
            ScopedLock< Mutex > lk( stateMutex );
            return MakeReadyFuture< Double >( temperature );
        } );

    skeleton.temperature.RegisterSetHandler(
        [&stateMutex, &temperature, &skeleton]( const Double& value )
            -> lap::core::Future< void >
        {
            {
                ScopedLock< Mutex > lk( stateMutex );
                temperature = value;
            }
            // Push Temperature field notification to all DDS subscribers
            skeleton.temperature.Update( value );
            std::cout << "[Server] Temperature SET -> " << value
                      << " C" << std::endl;
            return MakeReadyVoidFuture();
        } );

    std::cout << "[Server] All handlers registered." << std::endl;

    // ================================================================
    // Phase 7 — Offer Service
    // ================================================================
    auto offerR = skeleton.OfferService();
    if ( !offerR.HasValue() )
    {
        std::cerr << "[ERROR] OfferService failed: "
                  << offerR.Error().Message() << std::endl;
        pBinding->Shutdown();
        bindingMgr.Shutdown();
        return 1;
    }
    std::cout << "[Server] Service offered." << std::endl;

    // ================================================================
    // Phase 8 — Initial StatusChanged -> RUNNING
    // ================================================================
    {
        auto sample = skeleton.statusChanged.Allocate();
        if ( sample.HasValue() )
        {
            sample.Value()->status =
                HelloWorld2Types::ServerStatus::kRunning;
            skeleton.statusChanged.Send(
                std::move( sample ).Value() );
            std::cout << "[Server] StatusChanged -> RUNNING" << std::endl;
        }
    }

    // ================================================================
    // Phase 9 — Main Loop: Periodic Broadcasts
    // ================================================================
    std::cout << "[Server] Broadcasting every 200 ms.  Ctrl+C to stop.\n"
              << std::endl;

    UInt32 iteration = 0;
    while ( g_running.load() )
    {
        ++iteration;

        // -- Greeting event (every iteration) --
        {
            auto sample = skeleton.greeting.Allocate();
            if ( sample.HasValue() )
            {
                sample.Value()->text =
                    "Greetings from HelloWorld2! (seq="
                    + std::to_string( iteration ) + ")";
                skeleton.greeting.Send( std::move( sample ).Value() );
            }
        }

        // -- DataStream event (every 5th iteration) --
        if ( iteration % 5 == 0 )
        {
            auto sample = skeleton.dataStream.Allocate();
            if ( sample.HasValue() )
            {
                UInt32 seqNo = iteration / 5;
                sample.Value()->chunk.sequenceNo = seqNo;
                sample.Value()->chunk.totalSize  = 16;
                sample.Value()->chunk.payload.resize( 16 );
                for ( UInt32 i = 0; i < 16; ++i )
                {
                    sample.Value()->chunk.payload[i] =
                        static_cast< UInt8 >( ( seqNo + i ) & 0xFF );
                }
                skeleton.dataStream.Send( std::move( sample ).Value() );
                std::cout << "[Server] DataStream chunk #" << seqNo
                          << " sent." << std::endl;
            }
        }

        // -- StatusChanged: re-broadcast RUNNING every 10th iteration --
        if ( iteration % 10 == 0 )
        {
            auto sample = skeleton.statusChanged.Allocate();
            if ( sample.HasValue() )
            {
                sample.Value()->status =
                    HelloWorld2Types::ServerStatus::kRunning;
                skeleton.statusChanged.Send(
                    std::move( sample ).Value() );
            }
        }

        std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    }

    // ================================================================
    // Phase 10 — Graceful Shutdown
    // ================================================================
    {
        auto sample = skeleton.statusChanged.Allocate();
        if ( sample.HasValue() )
        {
            sample.Value()->status =
                HelloWorld2Types::ServerStatus::kStopping;
            skeleton.statusChanged.Send( std::move( sample ).Value() );
            std::cout << "[Server] StatusChanged -> STOPPING" << std::endl;
        }
    }

    skeleton.StopOfferService();

    pBinding->Shutdown();
    bindingMgr.Shutdown();

    std::cout << "[Server] Shutdown complete." << std::endl;
    return 0;
}
