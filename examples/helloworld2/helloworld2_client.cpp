/**
 * @file        helloworld2_client.cpp
 * @author      Aii
 * @brief       HelloWorld2 Client — dual-binding (DDS + CoreIPC) — Framework version
 * @date        2026-03-01
 * @details     Uses auto-generated HelloWorld2ServiceClientApp framework.
 *              All dual-binding boilerplate (CoreIPC, DDS, BindingManager,
 *              DDS adapters, service discovery, proxy creation, signal handler,
 *              event subscription/unsubscription, cleanup) is encapsulated in
 *              the generated ClientApp base class.
 *
 *              The developer only implements:
 *                - Event handlers   : OnGreeting, OnStatusChanged, OnDataStream
 *                - Lifecycle hooks  : OnConnected, OnDisconnected, OnTick
 *                - Uses method/field convenience wrappers: SayHello(), Add(), etc.
 *
 * @copyright   Copyright (c) 2026
 */

#include "HelloWorld2ServiceClientApp.hpp"
#include <atomic>
#include <iostream>

using namespace helloworld2;
using namespace helloworld2::client_app;

// ========================================================================
// MyClient — only business logic, zero binding/infrastructure boilerplate
// ========================================================================
class MyClient : public HelloWorld2ServiceClientApp
{
    std::atomic< UInt32 > m_greetingCount{ 0 };
    std::atomic< UInt32 > m_statusCount{ 0 };
    std::atomic< UInt32 > m_dataStreamCount{ 0 };

    // ==================== Event Handlers ====================

    void OnGreeting( const GreetingEvent& data ) override
    {
        UInt32 n = m_greetingCount.fetch_add( 1 ) + 1;
        std::cout << "[Client] Greeting #" << n << ": "
                  << data.text << std::endl;
    }

    void OnStatusChanged( const StatusChangedEvent& data ) override
    {
        m_statusCount.fetch_add( 1 );
        const char* names[] = { "STARTING", "RUNNING", "BUSY", "STOPPING" };
        int idx = static_cast< int >( data.status );
        const char* name = ( idx >= 0 && idx <= 3 ) ? names[idx] : "UNKNOWN";
        std::cout << "[Client] StatusChanged -> " << name << std::endl;
    }

    void OnDataStream( const DataStreamEvent& data ) override
    {
        m_dataStreamCount.fetch_add( 1 );
        std::cout << "[Client] DataStream #" << data.chunk.sequenceNo
                  << " (" << data.chunk.payload.size()
                  << " bytes)" << std::endl;
    }

    // ==================== Lifecycle Hooks ====================

    void OnConnected() override
    {
        // --- Method Calls ---
        std::cout << "\n--- Method Calls ---" << std::endl;

        // SayHello (request/response)
        {
            auto r = SayHello( String( "Alice" ) );
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
            auto r = Add( UInt32( 42 ), UInt32( 58 ) );
            if ( r.HasValue() )
            {
                std::cout << "[Client] Add(42, 58) = "
                          << r.Value() << std::endl;
            }
        }

        // NotifyLog (fire-and-forget)
        {
            NotifyLog( String( "Client started (dual-binding mode)!" ) );
            std::cout << "[Client] NotifyLog sent (fire-and-forget)."
                      << std::endl;
        }

        // ComputeHash (request/response)
        {
            ::std::vector< UInt8 > payload = {
                0x48, 0x65, 0x6C, 0x6C, 0x6F  // "Hello"
            };
            auto r = ComputeHash( payload );
            if ( r.HasValue() )
            {
                std::cout << "[Client] ComputeHash = 0x" << std::hex
                          << r.Value() << std::dec << std::endl;
            }
        }

        // --- Field Operations ---
        std::cout << "\n--- Field Operations ---" << std::endl;

        // VisitorCount (readonly)
        {
            auto r = GetVisitorCount();
            if ( r.HasValue() )
            {
                std::cout << "[Client] VisitorCount = "
                          << r.Value() << std::endl;
            }
        }

        // ServerName (read-write)
        {
            auto r = GetServerName();
            if ( r.HasValue() )
            {
                std::cout << "[Client] ServerName (before) = \""
                          << r.Value() << "\"" << std::endl;
            }

            SetServerName( String( "MyDualBindingServer" ) );

            auto r2 = GetServerName();
            if ( r2.HasValue() )
            {
                std::cout << "[Client] ServerName (after)  = \""
                          << r2.Value() << "\"" << std::endl;
            }
        }

        // Temperature (read-write + notification)
        {
            auto r = GetTemperature();
            if ( r.HasValue() )
            {
                std::cout << "[Client] Temperature (before) = "
                          << r.Value() << " C" << std::endl;
            }

            SetTemperature( 36.5 );
            std::this_thread::sleep_for(
                std::chrono::milliseconds( 200 ) );

            auto r2 = GetTemperature();
            if ( r2.HasValue() )
            {
                std::cout << "[Client] Temperature (after)  = "
                          << r2.Value() << " C" << std::endl;
            }
        }

        std::cout << "\n[Client] Listening for events for 5 seconds ..."
                  << std::endl;
    }

    bool OnTick( UInt32 tickCount ) override
    {
        // Listen for 5 ticks (~5 seconds), then stop
        return tickCount < 5;
    }

    void OnDisconnected() override
    {
        std::cout << "\n[Client] Summary: greetings="
                  << m_greetingCount.load()
                  << ", status=" << m_statusCount.load()
                  << ", dataStream=" << m_dataStreamCount.load()
                  << std::endl;
    }
};

// ========================================================================
// main — one line launch
// ========================================================================
int main()
{
    MyClient client;
    return client.Run();
}
