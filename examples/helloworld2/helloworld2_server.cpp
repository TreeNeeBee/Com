/**
 * @file        helloworld2_server.cpp
 * @author      Aii
 * @brief       HelloWorld2 Server — dual-binding (DDS + CoreIPC)
 * @date        2026-03-01
 * @details     Standard AUTOSAR AP R25-11 development flow:
 *                Step 1.  Define service in HelloWorld2.fidl
 *                Step 2.  Generate framework code:
 *                         lap_sidl_gen --input HelloWorld2.fidl --output gen/ --all
 *                         fastddsgen gen/HelloWorld2Service.idl -d gen/ -replace
 *                Step 3.  Implement server (this file):
 *                         - Register BOTH CoreIPC and DDS bindings
 *                         - Create HelloWorld2ServiceSkeleton
 *                         - Register method / field handlers
 *                         - OfferService (binding-agnostic via BindingManager)
 *                         - Send events in main loop
 *
 *              Dual-binding architecture:
 *                CoreIPC   — local IPC via shared memory (BindingPriority::kIceoryx2)
 *                DDS       — network transport via FastDDS  (BindingPriority::kDds)
 *                BindingManager selects the highest-priority available binding.
 *
 *              Communication patterns demonstrated:
 *                Methods  : SayHello, Add, NotifyLog(F&F), ComputeHash
 *                Events   : Greeting, StatusChanged, DataStream
 *                Fields   : VisitorCount(ro), ServerName(rw), Temperature(rw+notify)
 *
 * @copyright   Copyright (c) 2026
 */

// ==================== Generated Headers ====================
#include "HelloWorld2ServiceSkeleton.hpp"
#include "HelloWorld2ServiceDdsAdapter.hpp"

// ==================== Binding / Infrastructure ====================
#include "CoreIPCBinding.hpp"
#include "CRegistryDispatcher.hpp"
#include "DdsBinding.hpp"
#include "BindingManager.hpp"

// ==================== Standard Library ====================
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <thread>

using namespace lap::com;
using namespace lap::com::binding;
using namespace lap::com::registry;
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
// FNV-1a 64-bit hash
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
int main( int /* argc */, char* /* argv */[] )
{
    std::signal( SIGINT, signalHandler );
    std::signal( SIGTERM, signalHandler );

    std::cout << "=== HelloWorld2 Server (Dual-Binding: CoreIPC + DDS) ===" << std::endl;
    std::cout << "Service : " << HelloWorld2ServiceSkeleton::kServiceName
              << "  ID=0x" << std::hex << HelloWorld2ServiceSkeleton::kServiceId
              << std::dec << std::endl;

    // ================================================================
    // Phase 1 — Start Registry Dispatcher (for CoreIPC)
    // ================================================================
    CRegistryDispatcher dispatcher;
    auto dispInit = dispatcher.Initialize();
    if ( !dispInit.HasValue() )
    {
        std::cerr << "[ERROR] Dispatcher: "
                  << dispInit.Error().Message() << std::endl;
        return 1;
    }

    std::thread dispatcherThread( [&]() { dispatcher.Run(); } );
    std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
    std::cout << "[Server] Registry dispatcher started." << std::endl;

    // ================================================================
    // Phase 2 — Initialize CoreIPC Binding
    // ================================================================
    auto pCoreIpcBinding = MakeShared< CoreIPCBinding >();
    auto ipcInitR = pCoreIpcBinding->Initialize();
    if ( !ipcInitR )
    {
        std::cerr << "[ERROR] CoreIPC binding init: "
                  << ipcInitR.Error().Message() << std::endl;
        dispatcher.Shutdown();
        if ( dispatcherThread.joinable() ) { dispatcherThread.join(); }
        return 1;
    }
    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    std::cout << "[Server] CoreIPC binding initialized." << std::endl;

    // ================================================================
    // Phase 3 — Initialize DDS Binding
    // ================================================================
    auto pDdsBinding = MakeShared< DdsBinding >();
    auto ddsInitR = pDdsBinding->Initialize();
    if ( !ddsInitR )
    {
        std::cerr << "[WARN] DDS binding init failed: "
                  << ddsInitR.Error().Message()
                  << " — running CoreIPC-only mode" << std::endl;
        pDdsBinding.reset();
    }
    else
    {
        std::cout << "[Server] DDS binding initialized." << std::endl;
    }

    // ================================================================
    // Phase 4 — Register Bindings with BindingManager
    // ================================================================
    auto& bindingMgr = BindingManager::GetInstance();

    // CoreIPC: highest priority for local IPC
    {
        BindingConfig config;
        config.name     = "coreipc-server";
        config.priority = BindingPriority::kIceoryx2;
        config.enabled  = true;
        auto regR = bindingMgr.RegisterBinding( config, pCoreIpcBinding );
        if ( !regR.HasValue() )
        {
            std::cerr << "[ERROR] RegisterBinding(CoreIPC) failed." << std::endl;
            pCoreIpcBinding->Shutdown();
            dispatcher.Shutdown();
            if ( dispatcherThread.joinable() ) { dispatcherThread.join(); }
            return 1;
        }
    }
    std::cout << "[Server] CoreIPC binding registered (priority="
              << static_cast< UInt32 >( BindingPriority::kIceoryx2 ) << ")." << std::endl;

    // DDS: network transport
    if ( pDdsBinding )
    {
        BindingConfig config;
        config.name     = "dds-server";
        config.priority = BindingPriority::kDds;
        config.enabled  = true;
        auto regR = bindingMgr.RegisterBinding( config, pDdsBinding );
        if ( !regR.HasValue() )
        {
            std::cerr << "[WARN] RegisterBinding(DDS) failed — DDS unavailable." << std::endl;
            pDdsBinding->Shutdown();
            pDdsBinding.reset();
        }
        else
        {
            std::cout << "[Server] DDS binding registered (priority="
                      << static_cast< UInt32 >( BindingPriority::kDds ) << ")." << std::endl;
        }
    }

    // ================================================================
    // Phase 5 — Register DDS Type Adapters
    // ================================================================
    if ( pDdsBinding )
    {
        dds_adapter::RegisterHelloWorld2ServiceDdsAdapters(
            HelloWorld2ServiceSkeleton::kServiceId );
        std::cout << "[Server] DDS type adapters registered." << std::endl;
    }

    // ================================================================
    // Phase 6 — Create Skeleton
    // ================================================================
    HelloWorld2ServiceSkeleton skeleton(
        lap::core::InstanceSpecifier( "HelloWorld2/Provider" ) );

    // ================================================================
    // Phase 7 — Shared Application State
    // ================================================================
    Mutex stateMutex;
    UInt32 visitorCount = 0;
    String serverName   = "LightAP-HelloWorld2-DualBinding";
    Double temperature  = 25.0;

    // ================================================================
    // Phase 8 — Register Method Handlers
    // ================================================================

    // SayHello : String → String  (request/response)
    skeleton.sayHello.RegisterMethodHandler(
        [&stateMutex, &visitorCount]( String name )
            -> lap::core::Future< String >
        {
            {
                ScopedLock< Mutex > lk( stateMutex );
                ++visitorCount;
            }
            String reply = "Hello, " + name + "! Welcome to LightAP (DualBinding).";
            std::cout << "[Server] SayHello(\"" << name << "\")  visitor #"
                      << visitorCount << std::endl;
            return MakeReadyFuture< String >( std::move( reply ) );
        } );

    // Add : (UInt32, UInt32) → UInt32  (request/response)
    skeleton.add.RegisterMethodHandler(
        []( UInt32 a, UInt32 b ) -> lap::core::Future< UInt32 >
        {
            UInt32 sum = a + b;
            std::cout << "[Server] Add(" << a << ", " << b
                      << ") = " << sum << std::endl;
            return MakeReadyFuture< UInt32 >( sum );
        } );

    // NotifyLog : String → void  (fire-and-forget, no response)
    skeleton.notifyLog.RegisterMethodHandler(
        []( String message )
        {
            std::cout << "[Server] LOG: " << message << std::endl;
        } );

    // ComputeHash : vector<UInt8> → UInt64  (request/response)
    skeleton.computeHash.RegisterMethodHandler(
        []( ::std::vector< UInt8 > data )
            -> lap::core::Future< UInt64 >
        {
            UInt64 hash = Fnv1aHash( data );
            std::cout << "[Server] ComputeHash(" << data.size()
                      << " bytes) = 0x" << std::hex << hash
                      << std::dec << std::endl;
            return MakeReadyFuture< UInt64 >( hash );
        } );

    // ================================================================
    // Phase 9 — Register Field Handlers
    // ================================================================

    // VisitorCount — readonly (getter only)
    skeleton.visitorCount.RegisterGetHandler(
        [&stateMutex, &visitorCount]()
            -> lap::core::Future< UInt32 >
        {
            ScopedLock< Mutex > lk( stateMutex );
            return MakeReadyFuture< UInt32 >( visitorCount );
        } );

    // ServerName — read-write (getter + setter)
    skeleton.serverName.RegisterGetHandler(
        [&stateMutex, &serverName]()
            -> lap::core::Future< String >
        {
            ScopedLock< Mutex > lk( stateMutex );
            return MakeReadyFuture< String >( serverName );
        } );

    skeleton.serverName.RegisterSetHandler(
        [&stateMutex, &serverName]( const String& value )
            -> lap::core::Future< void >
        {
            {
                ScopedLock< Mutex > lk( stateMutex );
                serverName = value;
            }
            std::cout << "[Server] ServerName SET -> \""
                      << serverName << "\"" << std::endl;
            return MakeReadyVoidFuture();
        } );

    // Temperature — read-write + notification (getter + setter + Update)
    skeleton.temperature.RegisterGetHandler(
        [&stateMutex, &temperature]()
            -> lap::core::Future< Double >
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
            std::cout << "[Server] Temperature SET -> " << value
                      << " C" << std::endl;
            // Notify subscribers of the new value
            skeleton.temperature.Update( value );
            return MakeReadyVoidFuture();
        } );

    std::cout << "[Server] All handlers registered." << std::endl;

    // ================================================================
    // Phase 10 — Offer Service (binding-agnostic)
    // ================================================================
    auto offerR = skeleton.OfferService();
    if ( !offerR.HasValue() )
    {
        std::cerr << "[ERROR] OfferService failed: "
                  << offerR.Error().Message() << std::endl;
        pCoreIpcBinding->Shutdown();
        if ( pDdsBinding ) { pDdsBinding->Shutdown(); }
        dispatcher.Shutdown();
        if ( dispatcherThread.joinable() ) { dispatcherThread.join(); }
        bindingMgr.Shutdown();
        return 1;
    }
    std::cout << "[Server] Service offered (via BindingManager → best binding)." << std::endl;

    // ================================================================
    // Phase 11 — Initial StatusChanged Event
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
    // Phase 12 — Main Loop: Periodic Broadcasts
    // ================================================================
    std::cout << "[Server] Broadcasting every 2 s.  Ctrl+C to stop.\n"
              << std::endl;

    UInt32 eventSeq = 0;
    while ( g_running.load() )
    {
        ++eventSeq;

        // -- Greeting event --
        {
            auto sample = skeleton.greeting.Allocate();
            if ( sample.HasValue() )
            {
                sample.Value()->text =
                    "Greetings from LightAP DualBinding! (seq="
                    + std::to_string( eventSeq ) + ")";
                skeleton.greeting.Send(
                    std::move( sample ).Value() );
            }
        }

        // -- VisitorCount field notification (every 3rd iteration) --
        if ( eventSeq % 3 == 0 )
        {
            ScopedLock< Mutex > lk( stateMutex );
            skeleton.visitorCount.Update( visitorCount );
        }

        // -- DataStream event (every 5th iteration) --
        if ( eventSeq % 5 == 0 )
        {
            auto sample = skeleton.dataStream.Allocate();
            if ( sample.HasValue() )
            {
                UInt32 seqNo = eventSeq / 5;
                sample.Value()->chunk.sequenceNo = seqNo;
                sample.Value()->chunk.totalSize  = 64;
                sample.Value()->chunk.payload.resize( 64 );
                for ( UInt32 i = 0; i < 64; ++i )
                {
                    sample.Value()->chunk.payload[i] =
                        static_cast< UInt8 >(
                            ( seqNo + i ) & 0xFF );
                }
                skeleton.dataStream.Send(
                    std::move( sample ).Value() );
                std::cout << "[Server] DataStream #" << seqNo
                          << " sent." << std::endl;
            }
        }

        std::cout << "[Server] Greeting #" << eventSeq
                  << " sent." << std::endl;

        // Sleep 2 s in small increments for signal responsiveness
        for ( int i = 0; i < 20 && g_running.load(); ++i )
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds( 100 ) );
        }
    }

    // ================================================================
    // Cleanup
    // ================================================================
    {
        auto sample = skeleton.statusChanged.Allocate();
        if ( sample.HasValue() )
        {
            sample.Value()->status =
                HelloWorld2Types::ServerStatus::kStopping;
            skeleton.statusChanged.Send(
                std::move( sample ).Value() );
        }
    }

    std::cout << "\n[Server] Shutting down ..." << std::endl;
    skeleton.StopOfferService();
    pCoreIpcBinding->Shutdown();
    if ( pDdsBinding ) { pDdsBinding->Shutdown(); }
    dispatcher.Shutdown();
    if ( dispatcherThread.joinable() ) { dispatcherThread.join(); }
    bindingMgr.Shutdown();

    std::cout << "[Server] Goodbye." << std::endl;
    return 0;
}
