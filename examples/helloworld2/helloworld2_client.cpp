/**
 * @file        helloworld2_client.cpp
 * @author      Aii
 * @brief       HelloWorld2 Client — dual-binding (DDS + CoreIPC)
 * @date        2026-03-01
 * @details     Standard AUTOSAR AP R25-11 development flow:
 *                Step 1.  Define service in HelloWorld2.fidl
 *                Step 2.  Generate framework code:
 *                         lap_sidl_gen --input HelloWorld2.fidl --output gen/ --all
 *                         fastddsgen gen/HelloWorld2Service.idl -d gen/ -replace
 *                Step 3.  Implement client (this file):
 *                         - Register BOTH CoreIPC and DDS bindings
 *                         - Discover service via BindingManager
 *                         - Create HelloWorld2ServiceProxy
 *                         - Subscribe to events
 *                         - Call methods, read/write fields
 *
 *              Dual-binding architecture:
 *                CoreIPC   — local IPC via shared memory (BindingPriority::kCoreIpc)
 *                DDS       — network transport via FastDDS  (BindingPriority::kDds)
 *                BindingManager selects the highest-priority available binding.
 *
 * @copyright   Copyright (c) 2026
 */

// ==================== Generated Headers ====================
#include "HelloWorld2ServiceProxy.hpp"
#include "HelloWorld2ServiceDdsAdapter.hpp"

// ==================== Binding / Infrastructure ====================
#include "CoreIPCBinding.hpp"
#include "DdsBinding.hpp"
#include "BindingManager.hpp"

// ==================== Standard Library ====================
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

using namespace lap::com;
using namespace lap::com::binding;
using namespace helloworld2;
using namespace helloworld2::proxy;

// ========================================================================
// Signal Handler
// ========================================================================
static std::atomic< bool > g_running{ true };

void signalHandler( int ) { g_running.store( false ); }

// ========================================================================
// main
// ========================================================================
int main( int /* argc */, char* /* argv */[] )
{
    std::signal( SIGINT, signalHandler );
    std::signal( SIGTERM, signalHandler );

    std::cout << "=== HelloWorld2 Client (Dual-Binding: CoreIPC + DDS) ===" << std::endl;
    std::cout << "Service : " << HelloWorld2ServiceProxy::kServiceName
              << "  ID=0x" << std::hex << HelloWorld2ServiceProxy::kServiceId
              << std::dec << std::endl;

    // ================================================================
    // Phase 1 — Initialize CoreIPC Binding
    // ================================================================
    auto pCoreIpcBinding = MakeShared< CoreIPCBinding >();
    auto ipcInitR = pCoreIpcBinding->Initialize();
    if ( !ipcInitR )
    {
        std::cerr << "[ERROR] CoreIPC binding init: "
                  << ipcInitR.Error().Message() << std::endl;
        return 1;
    }
    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    std::cout << "[Client] CoreIPC binding initialized." << std::endl;

    // ================================================================
    // Phase 2 — Initialize DDS Binding
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
        std::cout << "[Client] DDS binding initialized." << std::endl;
    }

    // ================================================================
    // Phase 3 — Register Bindings with BindingManager
    // ================================================================
    auto& bindingMgr = BindingManager::GetInstance();

    // CoreIPC: highest priority for local IPC
    {
        BindingConfig config;
        config.name     = "coreipc-client";
        config.priority = BindingPriority::kCoreIpc;
        config.enabled  = true;
        auto regR = bindingMgr.RegisterBinding( config, pCoreIpcBinding );
        if ( !regR.HasValue() )
        {
            std::cerr << "[ERROR] RegisterBinding(CoreIPC) failed." << std::endl;
            pCoreIpcBinding->Shutdown();
            return 1;
        }
    }
    std::cout << "[Client] CoreIPC binding registered (priority="
              << static_cast< UInt32 >( BindingPriority::kCoreIpc ) << ")." << std::endl;

    // DDS: network transport
    if ( pDdsBinding )
    {
        BindingConfig config;
        config.name     = "dds-client";
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
            std::cout << "[Client] DDS binding registered (priority="
                      << static_cast< UInt32 >( BindingPriority::kDds ) << ")." << std::endl;
        }
    }

    // ================================================================
    // Phase 4 — Register DDS Type Adapters
    // ================================================================
    if ( pDdsBinding )
    {
        dds_adapter::RegisterHelloWorld2ServiceDdsAdapters(
            HelloWorld2ServiceProxy::kServiceId );
        std::cout << "[Client] DDS type adapters registered." << std::endl;
    }

    // ================================================================
    // Phase 5 — Service Discovery (Registry → SD-Proxy → Binding)
    // ================================================================
    //   CoreIPC FindService chain (unified 3-step flow):
    //     1. Local registry SHM lookup (< 500ns)
    //     2. IPC fallback → CRegistryDispatcher::handleQueryService
    //        2a. Local registry (dispatcher-side)
    //        2b. SD-Proxy remote cache (cross-ECU services, < 1ms)
    //        2c. SD-Proxy active query → DDS binding FindService (< 100ms)
    //   No need to call DDS binding directly — the registry+SD-Proxy
    //   chain covers both local and cross-ECU discovery.
    std::cout << "[Client] Discovering service (0x" << std::hex
              << HelloWorld2ServiceProxy::kServiceId << std::dec
              << ") via registry ..." << std::endl;

    bool found = false;
    for ( int attempt = 0; attempt < 30 && !found && g_running.load();
          ++attempt )
    {
        // Unified discovery: registry → SD-Proxy cache → active DDS query
        auto result = pCoreIpcBinding->FindService(
            HelloWorld2ServiceProxy::kServiceId );
        if ( result.HasValue() && !result.Value().empty() )
        {
            std::cout << "[Client] Service found via registry!" << std::endl;
            found = true;
        }

        if ( !found )
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds( 200 ) );
        }
    }

    if ( !found )
    {
        std::cerr << "[ERROR] Service not found.  "
                  << "Is the server running?" << std::endl;
        pCoreIpcBinding->Shutdown();
        if ( pDdsBinding ) { pDdsBinding->Shutdown(); }
        bindingMgr.Shutdown();
        return 1;
    }

    // ================================================================
    // Phase 6 — Create Proxy
    // ================================================================
    using HandleType = HelloWorld2ServiceProxy::HandleType;
    HandleType handle( static_cast< InstanceIdentifierType >(
        HelloWorld2ServiceProxy::kServiceId & 0xFFFFU ) );

    auto proxyResult = HelloWorld2ServiceProxy::Create( handle );
    if ( !proxyResult.HasValue() )
    {
        std::cerr << "[ERROR] Proxy::Create failed." << std::endl;
        pCoreIpcBinding->Shutdown();
        if ( pDdsBinding ) { pDdsBinding->Shutdown(); }
        bindingMgr.Shutdown();
        return 1;
    }

    auto proxy = std::move( proxyResult ).Value();
    std::cout << "[Client] Proxy created for "
              << HelloWorld2ServiceProxy::kServiceName << std::endl;

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    // ================================================================
    // Phase 7 — Subscribe to Events
    // ================================================================
    std::atomic< UInt32 > greetingCount{ 0 };
    std::atomic< UInt32 > statusCount{ 0 };
    std::atomic< UInt32 > dataStreamCount{ 0 };

    // -- Greeting event --
    proxy.greeting.Subscribe();
    proxy.greeting.SetReceiveHandler( [&] {
        auto sample = proxy.greeting.GetNextSample();
        if ( sample.HasValue() && sample.Value() )
        {
            UInt32 n = greetingCount.fetch_add( 1 ) + 1;
            std::cout << "[Client] Greeting #" << n << ": "
                      << sample.Value()->text << std::endl;
        }
    } );

    // -- StatusChanged event --
    proxy.statusChanged.Subscribe();
    proxy.statusChanged.SetReceiveHandler( [&] {
        auto sample = proxy.statusChanged.GetNextSample();
        if ( sample.HasValue() && sample.Value() )
        {
            statusCount.fetch_add( 1 );
            const char* names[] = {
                "STARTING", "RUNNING", "BUSY", "STOPPING"
            };
            int idx = static_cast< int >( sample.Value()->status );
            const char* name =
                ( idx >= 0 && idx <= 3 ) ? names[idx] : "UNKNOWN";
            std::cout << "[Client] StatusChanged -> " << name
                      << std::endl;
        }
    } );

    // -- DataStream event --
    proxy.dataStream.Subscribe();
    proxy.dataStream.SetReceiveHandler( [&] {
        auto sample = proxy.dataStream.GetNextSample();
        if ( sample.HasValue() && sample.Value() )
        {
            dataStreamCount.fetch_add( 1 );
            std::cout << "[Client] DataStream #"
                      << sample.Value()->chunk.sequenceNo
                      << " (" << sample.Value()->chunk.payload.size()
                      << " bytes)" << std::endl;
        }
    } );

    std::cout << "[Client] Subscribed to all events." << std::endl;
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    // ================================================================
    // Phase 8 — Method Calls
    // ================================================================
    std::cout << "\n--- Method Calls ---" << std::endl;

    // SayHello (request/response)
    {
        auto r = proxy.sayHello( String( "Alice" ) );
        if ( r.HasValue() )
        {
            std::cout << "[Client] SayHello(\"Alice\") -> \""
                      << r.Value() << "\"" << std::endl;
        }
        else
        {
            std::cerr << "[Client] SayHello failed." << std::endl;
        }
    }

    // Add (request/response)
    {
        auto r = proxy.add( UInt32( 42 ), UInt32( 58 ) );
        if ( r.HasValue() )
        {
            std::cout << "[Client] Add(42, 58) = "
                      << r.Value() << std::endl;
        }
    }

    // NotifyLog (fire-and-forget)
    {
        proxy.notifyLog( String( "Client started (dual-binding mode)!" ) );
        std::cout << "[Client] NotifyLog sent (fire-and-forget)."
                  << std::endl;
    }

    // ComputeHash (request/response)
    {
        ::std::vector< UInt8 > payload = {
            0x48, 0x65, 0x6C, 0x6C, 0x6F  // "Hello"
        };
        auto r = proxy.computeHash( payload );
        if ( r.HasValue() )
        {
            std::cout << "[Client] ComputeHash = 0x" << std::hex
                      << r.Value() << std::dec << std::endl;
        }
    }

    // ================================================================
    // Phase 9 — Field Operations
    // ================================================================
    std::cout << "\n--- Field Operations ---" << std::endl;

    // VisitorCount (readonly)
    {
        auto r = proxy.visitorCount.Get();
        if ( r.HasValue() )
        {
            std::cout << "[Client] VisitorCount = "
                      << r.Value() << std::endl;
        }
    }

    // ServerName (read-write)
    {
        auto r = proxy.serverName.Get();
        if ( r.HasValue() )
        {
            std::cout << "[Client] ServerName (before) = \""
                      << r.Value() << "\"" << std::endl;
        }

        proxy.serverName.Set( String( "MyDualBindingServer" ) );

        auto r2 = proxy.serverName.Get();
        if ( r2.HasValue() )
        {
            std::cout << "[Client] ServerName (after)  = \""
                      << r2.Value() << "\"" << std::endl;
        }
    }

    // Temperature (read-write + notification)
    {
        auto r = proxy.temperature.Get();
        if ( r.HasValue() )
        {
            std::cout << "[Client] Temperature (before) = "
                      << r.Value() << " C" << std::endl;
        }

        // Subscribe to field change notifications
        proxy.temperature.Subscribe();
        proxy.temperature.SetReceiveHandler( [&] {
            auto sample = proxy.temperature.GetNextSample();
            if ( sample.HasValue() && sample.Value() )
            {
                std::cout << "[Client] Temperature notification -> "
                          << *sample.Value() << " C" << std::endl;
            }
        } );

        proxy.temperature.Set( 36.5 );
        std::this_thread::sleep_for(
            std::chrono::milliseconds( 200 ) );

        auto r2 = proxy.temperature.Get();
        if ( r2.HasValue() )
        {
            std::cout << "[Client] Temperature (after)  = "
                      << r2.Value() << " C" << std::endl;
        }
    }

    // ================================================================
    // Phase 10 — Listen for Events
    // ================================================================
    std::cout << "\n[Client] Listening for events for 5 seconds ..."
              << std::endl;

    for ( int i = 0; i < 50 && g_running.load(); ++i )
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds( 100 ) );
    }

    // ================================================================
    // Cleanup
    // ================================================================
    std::cout << "\n[Client] Shutting down ..." << std::endl;
    proxy.greeting.Unsubscribe();
    proxy.statusChanged.Unsubscribe();
    proxy.dataStream.Unsubscribe();
    proxy.temperature.Unsubscribe();
    pCoreIpcBinding->Shutdown();
    if ( pDdsBinding ) { pDdsBinding->Shutdown(); }
    bindingMgr.Shutdown();

    std::cout << "[Client] Summary: greetings="
              << greetingCount.load()
              << ", status=" << statusCount.load()
              << ", dataStream=" << dataStreamCount.load()
              << std::endl;
    std::cout << "[Client] Goodbye." << std::endl;
    return 0;
}
