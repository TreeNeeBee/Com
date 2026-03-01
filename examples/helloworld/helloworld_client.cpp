/**
 * @file        helloworld_client.cpp
 * @author      Aii
 * @brief       HelloWorld Client — standard AUTOSAR AP development flow
 * @date        2026-02-11
 * @details     Development workflow:
 *                Step 1.  Define service in HelloWorld.fidl
 *                Step 2.  Generate framework code:
 *                         lap_sidl_gen --input HelloWorld.fidl --output gen/ --all
 *                Step 3.  Implement client (this file):
 *                         - Initialize binding
 *                         - Discover service → Create HelloWorldProxy
 *                         - Subscribe to events
 *                         - Call methods
 *                         - Read / write fields
 *
 *              Communication patterns demonstrated:
 *                Methods  : SayHello, Add, NotifyLog(F&F), ComputeHash
 *                Events   : Greeting, StatusChanged, DataStream
 *                Fields   : VisitorCount(ro), ServerName(rw), Temperature(rw+notify)
 *
 * @copyright   Copyright (c) 2026
 */

// ==================== Generated Headers ====================
#include "HelloWorldProxy.hpp"

// ==================== Binding / Infrastructure ====================
#include "CoreIPCBinding.hpp"
#include "BindingManager.hpp"

// ==================== Standard Library ====================
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

using namespace lap::com;
using namespace lap::com::examples;
using namespace lap::com::examples::proxy;
using namespace lap::com::binding;

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

    std::cout << "=== HelloWorld Client (Generated Proxy) ===" << std::endl;
    std::cout << "Service : " << HelloWorldProxy::kServiceName
              << "  ID=0x" << std::hex << HelloWorldProxy::kServiceId
              << std::dec << std::endl;

    // ================================================================
    // Phase 1 — Initialize CoreIPC Binding
    // ================================================================
    auto pBinding = MakeShared< CoreIPCBinding >();
    auto initR = pBinding->Initialize();
    if ( !initR )
    {
        std::cerr << "[ERROR] Binding: "
                  << initR.Error().Message() << std::endl;
        return 1;
    }
    std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );

    // Register binding with BindingManager
    auto& bindingMgr = BindingManager::GetInstance();
    {
        BindingConfig config;
        config.name     = "coreipc-client";
        config.priority = BindingPriority::kCoreIpc;
        config.enabled  = true;
        auto regR = bindingMgr.RegisterBinding( config, pBinding );
        if ( !regR.HasValue() )
        {
            std::cerr << "[ERROR] RegisterBinding failed." << std::endl;
            pBinding->Shutdown();
            return 1;
        }
    }
    std::cout << "[Client] CoreIPC binding registered." << std::endl;

    // ================================================================
    // Phase 2 — Service Discovery
    // ================================================================
    std::cout << "[Client] Discovering service (0x" << std::hex
              << HelloWorldProxy::kServiceId << std::dec
              << ") ..." << std::endl;

    bool found = false;
    for ( int attempt = 0; attempt < 30 && !found && g_running.load();
          ++attempt )
    {
        auto result = pBinding->FindService(
            HelloWorldProxy::kServiceId );
        if ( result.HasValue() && !result.Value().empty() )
        {
            std::cout << "[Client] Service found!" << std::endl;
            found = true;
        }
        else
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds( 200 ) );
        }
    }

    if ( !found )
    {
        std::cerr << "[ERROR] Service not found.  "
                  << "Is the server running?" << std::endl;
        pBinding->Shutdown();
        bindingMgr.Shutdown();
        return 1;
    }

    // ================================================================
    // Phase 3 — Create Proxy
    // ================================================================
    using HandleType = HelloWorldProxy::HandleType;
    HandleType handle( static_cast< InstanceIdentifierType >(
        HelloWorldProxy::kServiceId & 0xFFFFU ) );

    auto proxyResult = HelloWorldProxy::Create( handle );
    if ( !proxyResult.HasValue() )
    {
        std::cerr << "[ERROR] Proxy::Create failed." << std::endl;
        pBinding->Shutdown();
        bindingMgr.Shutdown();
        return 1;
    }

    auto proxy = std::move( proxyResult ).Value();
    std::cout << "[Client] Proxy created for "
              << HelloWorldProxy::kServiceName << std::endl;

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    // ================================================================
    // Phase 4 — Subscribe to Events
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
                      << sample.Value()->message.text << std::endl;
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
            int idx = static_cast< int >(
                sample.Value()->status );
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
        proxy.notifyLog( String( "Client started successfully!" ) );
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
    // Phase 6 — Field Operations
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

        proxy.serverName.Set( String( "MyCustomServer" ) );

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
    // Phase 7 — Listen for Events
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
    pBinding->Shutdown();
    bindingMgr.Shutdown();

    std::cout << "[Client] Summary: greetings="
              << greetingCount.load()
              << ", status=" << statusCount.load()
              << ", dataStream=" << dataStreamCount.load()
              << std::endl;
    std::cout << "[Client] Goodbye." << std::endl;
    return 0;
}
