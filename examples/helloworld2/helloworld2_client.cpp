/**
 * @file        helloworld2_client.cpp
 * @author      Aii
 * @brief       HelloWorld2 Client — standard-flow refactor using generated Proxy
 * @date        2026-02-23
 * @details     Development workflow (standard AUTOSAR AP flow):
 *
 *                Step 1.  Define service interface in HelloWorld2.fidl
 *
 *                Step 2.  Run generator (or: cmake --build . --target helloworld2_generate):
 *                         lap_sidl_gen \
 *                             --input HelloWorld2.fidl --output gen/ --author Aii --all
 *
 *                Step 3.  Implement client (this file):
 *                         - Initialize DdsBinding
 *                         - Discover service -> Create HelloWorld2ServiceProxy
 *                         - Subscribe to events
 *                         - Call methods
 *                         - Read / write fields
 *
 *              All serialization is handled transparently by the generated
 *              proxy and the CoreIPC binding — callers work with typed values.
 *
 *              Communication patterns demonstrated:
 *                Methods : SayHello, Add, NotifyLog (F&F), ComputeHash
 *                Events  : Greeting, StatusChanged, DataStream
 *                Fields  : VisitorCount (ro), ServerName (rw), Temperature (rw)
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
int main()
{
    std::signal( SIGINT, signalHandler );
    std::signal( SIGTERM, signalHandler );

    std::cout << "=== HelloWorld2 Client (Generated Proxy) ===" << std::endl;
    std::cout << "Service : " << HelloWorld2ServiceProxy::kServiceName
              << "  ID=0x" << std::hex << HelloWorld2ServiceProxy::kServiceId
              << std::dec << std::endl;

    // ================================================================
    // Phase 1 — Initialize CoreIPC Binding
    // ================================================================
    auto pBinding = MakeShared< DdsBinding >();
    auto initR = pBinding->Initialize();
    if ( !initR )
    {
        std::cerr << "[ERROR] Binding init: "
                  << initR.Error().Message() << std::endl;
        return 1;
    }
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

    auto& bindingMgr = BindingManager::GetInstance();
    {
        BindingConfig config;
        config.name     = "dds-client";
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
    // Register DDS type adapters (must match server's adapter registration)
    helloworld2::dds_adapter::RegisterHelloWorld2ServiceDdsAdapters(
        HelloWorld2ServiceProxy::kServiceId );
    std::cout << "[Client] DDS type adapters registered." << std::endl;
    std::cout << "[Client] DDS binding registered." << std::endl;

    // ================================================================
    // Phase 2 — Service Discovery
    // ================================================================
    std::cout << "[Client] Discovering service (0x" << std::hex
              << HelloWorld2ServiceProxy::kServiceId << std::dec
              << ") ..." << std::endl;

    bool found = false;
    for ( int attempt = 0; attempt < 30 && !found && g_running.load(); ++attempt )
    {
        auto result = pBinding->FindService(
            HelloWorld2ServiceProxy::kServiceId );
        if ( result.HasValue() && !result.Value().empty() )
        {
            std::cout << "[Client] Service found!" << std::endl;
            found = true;
        }
        else
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
        }
    }

    if ( !found )
    {
        std::cerr << "[ERROR] Service not found.  Is the server running?" << std::endl;
        pBinding->Shutdown();
        bindingMgr.Shutdown();
        return 1;
    }

    // ================================================================
    // Phase 3 — Create Proxy
    // ================================================================
    using HandleType = HelloWorld2ServiceProxy::HandleType;
    HandleType handle( static_cast< InstanceIdentifierType >(
        HelloWorld2ServiceProxy::kServiceId & 0xFFFFU ) );

    auto proxyResult = HelloWorld2ServiceProxy::Create( handle );
    if ( !proxyResult.HasValue() )
    {
        std::cerr << "[ERROR] Proxy::Create failed." << std::endl;
        pBinding->Shutdown();
        bindingMgr.Shutdown();
        return 1;
    }

    auto proxy = std::move( proxyResult ).Value();
    std::cout << "[Client] Proxy created for "
              << HelloWorld2ServiceProxy::kServiceName << std::endl;

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    // ================================================================
    // Phase 4 — Subscribe to Events
    // ================================================================
    std::atomic< UInt32 > greetingCount{ 0 };
    std::atomic< UInt32 > statusCount{ 0 };
    std::atomic< UInt32 > dataStreamCount{ 0 };

    // -- Greeting event : { String text } --
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

    // -- StatusChanged event : { ServerStatus status } --
    proxy.statusChanged.Subscribe();
    proxy.statusChanged.SetReceiveHandler( [&] {
        auto sample = proxy.statusChanged.GetNextSample();
        if ( sample.HasValue() && sample.Value() )
        {
            statusCount.fetch_add( 1 );
            static const char* names[] = {
                "STARTING", "RUNNING", "BUSY", "STOPPING"
            };
            int idx = static_cast< int >( sample.Value()->status );
            const char* name =
                ( idx >= 0 && idx <= 3 ) ? names[idx] : "UNKNOWN";
            std::cout << "[Client] StatusChanged -> " << name << std::endl;
        }
    } );

    // -- DataStream event : { DataChunk chunk } --
    proxy.dataStream.Subscribe();
    proxy.dataStream.SetReceiveHandler( [&] {
        auto sample = proxy.dataStream.GetNextSample();
        if ( sample.HasValue() && sample.Value() )
        {
            dataStreamCount.fetch_add( 1 );
            const auto& c = sample.Value()->chunk;
            std::cout << "[Client] DataStream chunk #" << c.sequenceNo
                      << "  size=" << c.payload.size() << std::endl;
        }
    } );

    std::cout << "[Client] Subscribed to all events." << std::endl;
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    // ================================================================
    // Phase 5 — Method Calls
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
            std::cerr << "[Client] SayHello failed: "
                      << r.Error().Message() << std::endl;
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

    // NotifyLog (fire-and-forget — void, no result to check)
    {
        proxy.notifyLog( String( "Client started successfully." ) );
        std::cout << "[Client] NotifyLog sent (fire-and-forget)." << std::endl;
    }

    // ComputeHash (request/response)
    {
        ByteArray payload = { 0x48, 0x65, 0x6C, 0x6C, 0x6F };  // "Hello"
        auto r = proxy.computeHash( payload );
        if ( r.HasValue() )
        {
            std::cout << "[Client] ComputeHash(\"Hello\") = 0x"
                      << std::hex << r.Value() << std::dec << std::endl;
        }
    }

    // ================================================================
    // Phase 6 — Field Access
    // ================================================================
    std::cout << "\n--- Field Access ---" << std::endl;

    // VisitorCount — readonly getter
    {
        auto r = proxy.visitorCount.Get();
        if ( r.HasValue() )
        {
            std::cout << "[Client] VisitorCount = "
                      << r.Value() << std::endl;
        }
    }

    // ServerName — getter
    {
        auto r = proxy.serverName.Get();
        if ( r.HasValue() )
        {
            std::cout << "[Client] ServerName = \""
                      << r.Value() << "\"" << std::endl;
        }
    }

    // ServerName — setter
    {
        auto r = proxy.serverName.Set( String( "LightAP-Client-Rename" ) );
        if ( r.HasValue() )
        {
            std::cout << "[Client] ServerName SET successfully." << std::endl;
        }
    }

    // Temperature — getter
    {
        auto r = proxy.temperature.Get();
        if ( r.HasValue() )
        {
            std::cout << "[Client] Temperature = "
                      << r.Value() << " C" << std::endl;
        }
    }

    // Temperature — setter
    {
        auto r = proxy.temperature.Set( Double( 37.5 ) );
        if ( r.HasValue() )
        {
            std::cout << "[Client] Temperature SET -> 37.5 C" << std::endl;
        }
    }

    // ================================================================
    // Phase 7 — Event observation loop (3 seconds)
    // ================================================================
    std::cout << "\n--- Observing events for 3 s ---" << std::endl;
    std::this_thread::sleep_for( std::chrono::seconds( 3 ) );

    std::cout << "\n--- Summary ---" << std::endl;
    std::cout << "  Greeting events   : " << greetingCount.load() << std::endl;
    std::cout << "  StatusChanged     : " << statusCount.load()   << std::endl;
    std::cout << "  DataStream chunks : " << dataStreamCount.load() << std::endl;

    // ================================================================
    // Phase 8 — Cleanup
    // ================================================================
    proxy.greeting.Unsubscribe();
    proxy.statusChanged.Unsubscribe();
    proxy.dataStream.Unsubscribe();

    pBinding->Shutdown();
    bindingMgr.Shutdown();

    std::cout << "[Client] Shutdown complete." << std::endl;
    return 0;
}
