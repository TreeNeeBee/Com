/**
 * @file        helloworld_client.cpp
 * @brief       HelloWorld Client — CoreIPC App Framework
 * @details     Client example using auto-generated HelloWorldClientApp framework.
 *              Only event handling and business logic is implemented; all binding,
 *              service discovery, and lifecycle boilerplate is encapsulated.
 * @copyright   Copyright (c) 2026
 */

#include "HelloWorldClientApp.hpp"

#include <iostream>

using namespace examples;
using namespace examples::client_app;

class MyHelloWorldClient : public HelloWorldClientApp
{
public:
    // ---- Event Handlers ----

    void OnGreeting( const GreetingEvent& data ) override
    {
        std::cout << "[Client] Greeting: " << data.message.text << std::endl;
    }

    void OnStatusChanged( const StatusChangedEvent& data ) override
    {
        const char* names[] = { "STARTING", "RUNNING", "BUSY", "STOPPING" };
        int idx = static_cast< int >( data.status );
        std::cout << "[Client] Status -> "
                  << ( idx >= 0 && idx <= 3 ? names[idx] : "?" ) << std::endl;
    }

    // ---- Lifecycle ----

    void OnConnected() override
    {
        // Method calls
        std::cout << "\n--- Methods ---" << std::endl;
        {
            auto r = SayHello( String( "Alice" ) );
            if ( r.HasValue() )
                std::cout << "[Client] SayHello -> " << r.Value() << std::endl;
        }
        {
            auto r = Add( UInt32( 17 ), UInt32( 25 ) );
            if ( r.HasValue() )
                std::cout << "[Client] Add(17,25) = " << r.Value() << std::endl;
        }
        NotifyLog( String( "Client started" ) );

        // Field access
        std::cout << "\n--- Fields ---" << std::endl;
        {
            auto r = GetVisitorCount();
            if ( r.HasValue() )
                std::cout << "[Client] VisitorCount = " << r.Value() << std::endl;
        }
        {
            auto r = GetServerName();
            if ( r.HasValue() )
                std::cout << "[Client] ServerName = " << r.Value() << std::endl;
            SetServerName( String( "MyServer" ) );
            auto r2 = GetServerName();
            if ( r2.HasValue() )
                std::cout << "[Client] ServerName (after set) = " << r2.Value() << std::endl;
        }
        {
            auto r = GetTemperature();
            if ( r.HasValue() )
                std::cout << "[Client] Temperature = " << r.Value() << std::endl;
        }

        std::cout << "\n[Client] Listening for events ..." << std::endl;
    }

    bool OnTick( UInt32 tickCount ) override
    {
        // Run for 5 ticks (~5 seconds) then stop
        return tickCount < 5;
    }

    void OnDisconnected() override
    {
        std::cout << "[Client] Done." << std::endl;
    }
};

int main()
{
    MyHelloWorldClient client;
    return client.Run();
}
