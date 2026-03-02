/**
 * @file        helloworld_server.cpp
 * @brief       HelloWorld Server — CoreIPC App Framework
 * @details     Server example using auto-generated HelloWorldServerApp framework.
 *              Only business logic is implemented; all binding/dispatcher/lifecycle
 *              boilerplate is encapsulated by the framework.
 * @copyright   Copyright (c) 2026
 */

#include "HelloWorldServerApp.hpp"

#include <iostream>
#include <mutex>

using namespace examples;
using namespace examples::server_app;

class MyHelloWorldServer : public HelloWorldServerApp
{
public:
    // ---- Method Handlers ----

    ::lap::core::Future< String > OnSayHello( String name ) override
    {
        { std::lock_guard< std::mutex > lk( m_mtx ); ++m_visitors; }
        std::cout << "[Server] SayHello(" << name << ")" << std::endl;
        return MakeReadyFuture< String >( "Hello, " + name + "!" );
    }

    ::lap::core::Future< UInt32 > OnAdd( UInt32 a, UInt32 b ) override
    {
        std::cout << "[Server] Add(" << a << "," << b << ")" << std::endl;
        return MakeReadyFuture< UInt32 >( a + b );
    }

    void OnNotifyLog( String message ) override
    {
        std::cout << "[Server] LOG: " << message << std::endl;
    }

    ::lap::core::Future< UInt64 > OnComputeHash( ::std::vector< UInt8 > data ) override
    {
        UInt64 h = 14695981039346656037ULL;
        for ( auto b : data ) { h ^= b; h *= 1099511628211ULL; }
        return MakeReadyFuture< UInt64 >( h );
    }

    // ---- Field Handlers ----

    ::lap::core::Future< UInt32 > OnGetVisitorCount() override
    {
        std::lock_guard< std::mutex > lk( m_mtx );
        return MakeReadyFuture< UInt32 >( m_visitors );
    }

    ::lap::core::Future< String > OnGetServerName() override
    {
        std::lock_guard< std::mutex > lk( m_mtx );
        return MakeReadyFuture< String >( m_serverName );
    }

    ::lap::core::Future< void > OnSetServerName( const String& value ) override
    {
        { std::lock_guard< std::mutex > lk( m_mtx ); m_serverName = value; }
        return MakeReadyVoidFuture();
    }

    ::lap::core::Future< Double > OnGetTemperature() override
    {
        return MakeReadyFuture< Double >( m_temperature );
    }

    ::lap::core::Future< void > OnSetTemperature( Double value ) override
    {
        m_temperature = value;
        UpdateTemperature( value );
        return MakeReadyVoidFuture();
    }

    // ---- Lifecycle ----

    void OnStart() override
    {
        std::cout << "[Server] Running.  Ctrl+C to stop." << std::endl;

        // Fire initial StatusChanged
        SendStatusChanged( HelloWorldTypes::ServerStatus::kRunning );
    }

    bool OnTick( UInt32 tickCount ) override
    {
        // Greeting event each tick
        HelloWorldTypes::GreetingMessage msg;
        msg.text = "Hello from LightAP (#" + std::to_string( tickCount ) + ")";
        msg.timestamp = static_cast< UInt64 >(
            std::chrono::system_clock::now().time_since_epoch().count() );
        SendGreeting( msg );

        return true;
    }

    void OnStop() override
    {
        std::cout << "[Server] Stopped." << std::endl;
    }

private:
    std::mutex m_mtx;
    UInt32     m_visitors    = 0;
    String     m_serverName  = "LightAP-HelloWorld";
    Double     m_temperature = 25.0;
};

int main()
{
    MyHelloWorldServer server;
    return server.Run();
}
