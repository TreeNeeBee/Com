/**
 * @file        helloworld2_server.cpp
 * @author      Aii
 * @brief       HelloWorld2 Server — dual-binding (DDS + CoreIPC) — Framework version
 * @date        2026-03-01
 * @details     Uses auto-generated HelloWorld2ServiceServerApp framework.
 *              All dual-binding boilerplate (dispatcher, CoreIPC, DDS, SD-Proxy
 *              bridge, BindingManager, DDS adapters, OfferService, signal handler,
 *              cleanup) is encapsulated in the generated ServerApp base class.
 *
 *              The developer only implements:
 *                - Method handlers   : OnSayHello, OnAdd, OnNotifyLog, OnComputeHash
 *                - Field handlers    : OnGet/OnSet for VisitorCount, ServerName, Temperature
 *                - Lifecycle hooks   : OnStart, OnStop, OnTick
 *                - Event broadcasting via SendXxx() helpers
 *
 * @copyright   Copyright (c) 2026
 */

#include "HelloWorld2ServiceServerApp.hpp"
#include <iostream>
#include <mutex>

using namespace helloworld2;
using namespace helloworld2::server_app;

// ========================================================================
// FNV-1a 64-bit hash (business-logic utility)
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
// MyServer — only business logic, zero binding/infrastructure boilerplate
// ========================================================================
class MyServer : public HelloWorld2ServiceServerApp
{
    // ---- Shared application state ----
    std::mutex  m_mtx;
    UInt32      m_visitorCount = 0;
    String      m_serverName   = "LightAP-HelloWorld2-DualBinding";
    Double      m_temperature  = 25.0;
    UInt32      m_eventSeq     = 0;

    // ==================== Method Handlers ====================

    Future< String > OnSayHello( String name ) override
    {
        { std::lock_guard< std::mutex > lk( m_mtx ); ++m_visitorCount; }
        String reply = "Hello, " + name + "! Welcome to LightAP (DualBinding).";
        std::cout << "[Server] SayHello(\"" << name << "\")  visitor #"
                  << m_visitorCount << std::endl;
        return MakeReadyFuture< String >( std::move( reply ) );
    }

    Future< UInt32 > OnAdd( UInt32 a, UInt32 b ) override
    {
        UInt32 sum = a + b;
        std::cout << "[Server] Add(" << a << ", " << b
                  << ") = " << sum << std::endl;
        return MakeReadyFuture< UInt32 >( sum );
    }

    void OnNotifyLog( String message ) override
    {
        std::cout << "[Server] LOG: " << message << std::endl;
    }

    Future< UInt64 > OnComputeHash( ::std::vector< UInt8 > data ) override
    {
        UInt64 hash = Fnv1aHash( data );
        std::cout << "[Server] ComputeHash(" << data.size()
                  << " bytes) = 0x" << std::hex << hash
                  << std::dec << std::endl;
        return MakeReadyFuture< UInt64 >( hash );
    }

    // ==================== Field Handlers ====================

    Future< UInt32 > OnGetVisitorCount() override
    {
        std::lock_guard< std::mutex > lk( m_mtx );
        return MakeReadyFuture< UInt32 >( m_visitorCount );
    }

    Future< String > OnGetServerName() override
    {
        std::lock_guard< std::mutex > lk( m_mtx );
        return MakeReadyFuture< String >( m_serverName );
    }

    Future< void > OnSetServerName( const String& value ) override
    {
        { std::lock_guard< std::mutex > lk( m_mtx ); m_serverName = value; }
        std::cout << "[Server] ServerName SET -> \""
                  << value << "\"" << std::endl;
        return MakeReadyVoidFuture();
    }

    Future< Double > OnGetTemperature() override
    {
        std::lock_guard< std::mutex > lk( m_mtx );
        return MakeReadyFuture< Double >( m_temperature );
    }

    Future< void > OnSetTemperature( Double value ) override
    {
        { std::lock_guard< std::mutex > lk( m_mtx ); m_temperature = value; }
        std::cout << "[Server] Temperature SET -> " << value
                  << " C" << std::endl;
        UpdateTemperature( value );          // Notify subscribers
        return MakeReadyVoidFuture();
    }

    // ==================== Lifecycle Hooks ====================

    void OnStart() override
    {
        // Initial status broadcast
        SendStatusChanged( HelloWorld2Types::ServerStatus::kRunning );
        std::cout << "[Server] StatusChanged -> RUNNING" << std::endl;
        std::cout << "[Server] Broadcasting every 1 s.  Ctrl+C to stop.\n"
                  << std::endl;
    }

    void OnStop() override
    {
        SendStatusChanged( HelloWorld2Types::ServerStatus::kStopping );
    }

    bool OnTick( UInt32 /* tickCount */ ) override
    {
        ++m_eventSeq;

        // Greeting event — every tick
        SendGreeting( "Greetings from LightAP DualBinding! (seq="
                      + std::to_string( m_eventSeq ) + ")" );

        // VisitorCount field notification — every 3rd tick
        if ( m_eventSeq % 3 == 0 )
        {
            std::lock_guard< std::mutex > lk( m_mtx );
            UpdateVisitorCount( m_visitorCount );
        }

        // DataStream event — every 5th tick
        if ( m_eventSeq % 5 == 0 )
        {
            HelloWorld2Types::DataChunk chunk;
            UInt32 seqNo           = m_eventSeq / 5;
            chunk.sequenceNo       = seqNo;
            chunk.totalSize        = 64;
            chunk.payload.resize( 64 );
            for ( UInt32 i = 0; i < 64; ++i )
            {
                chunk.payload[i] = static_cast< UInt8 >(
                    ( seqNo + i ) & 0xFF );
            }
            SendDataStream( chunk );
            std::cout << "[Server] DataStream #" << seqNo
                      << " sent." << std::endl;
        }

        std::cout << "[Server] Greeting #" << m_eventSeq
                  << " sent." << std::endl;
        return true;
    }
};

// ========================================================================
// main — one line launch
// ========================================================================
int main()
{
    MyServer server;
    return server.Run();
}
