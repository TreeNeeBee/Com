/**
 * @file        HelloWorldServerApp.hpp
 * @author      Aii
 * @brief       Auto-generated server application framework for HelloWorld
 * @date        2026/02/09
 * @details     Auto-generated from examples/helloworld/HelloWorld.fidl by lap-sidl-gen v1.0
 * @copyright   Copyright (c) 2026
 * @note        DO NOT EDIT — This file is auto-generated
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>Auto-generated
 * </table>
 */

#ifndef EXAMPLES_HELLOWORLDSERVERAPP_HPP
#define EXAMPLES_HELLOWORLDSERVERAPP_HPP

// ==================== Generated Headers ====================
#include "HelloWorldSkeleton.hpp"

// ==================== Binding / Infrastructure ====================
#include "CoreIPCBinding.hpp"
#include "CRegistryDispatcher.hpp"
#include "BindingManager.hpp"

// ==================== Standard Library ====================
#include <atomic>
#include <chrono>
#include <csignal>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

namespace examples
{
// ==================== LAP/COM Type Aliases ====================
using lap::core::Result;
using lap::core::Optional;
using lap::core::String;
using lap::core::StringView;
using lap::core::Bool;
using lap::core::Char;
using lap::core::UInt8;
using lap::core::UInt16;
using lap::core::UInt32;
using lap::core::UInt64;
using lap::core::Int32;
using lap::core::Int64;
using lap::core::Float;
using lap::core::Double;
using ::lap::core::Future;
using ::lap::com::MethodCallProcessingMode;
using ::lap::com::ComErrc;
using ::lap::com::MakeErrorCode;
using ::lap::com::ServiceState;
using ByteArray = ::std::vector< UInt8 >;


namespace server_app
{

// ==================== Future Utilities ====================

template< typename T >
inline ::lap::core::Future< T > MakeReadyFuture( T value )
{
    ::std::promise< ::lap::core::Result< T > > p;
    p.set_value( ::lap::core::Result< T >::FromValue( ::std::move( value ) ) );
    return ::lap::core::Future< T >( ::std::move( p.get_future() ) );
}

inline ::lap::core::Future< void > MakeReadyVoidFuture()
{
    ::std::promise< ::lap::core::Result< void > > p;
    p.set_value( ::lap::core::Result< void >::FromValue() );
    return ::lap::core::Future< void >( ::std::move( p.get_future() ) );
}

/**
 * @brief Server application framework for HelloWorld
 * @details Encapsulates all CoreIPC binding boilerplate.
 *          User subclasses and implements only the business logic:
 *          - Method handler callbacks (pure virtual)
 *          - Field getter/setter callbacks (pure virtual)
 *          - Event sending via helper methods
 *          - Lifecycle hooks (OnStart / OnStop / OnTick)
 *
 * Usage:
 *   class MyServer : public HelloWorldServerApp {
 *       // implement pure virtual handlers ...
 *   };
 *   int main() { MyServer s; return s.Run(); }
 */
class HelloWorldServerApp
{
public:
    virtual ~HelloWorldServerApp() = default;

    // ==================== Method Handlers (implement business logic) ====================

    /// Handle SayHello → String
    virtual ::lap::core::Future< String > OnSayHello( String name ) = 0;

    /// Handle Add → UInt32
    virtual ::lap::core::Future< UInt32 > OnAdd( UInt32 a, UInt32 b ) = 0;

    /// Handle NotifyLog (fire-and-forget)
    virtual void OnNotifyLog( String message ) = 0;

    /// Handle ComputeHash → UInt64
    virtual ::lap::core::Future< UInt64 > OnComputeHash( ::std::vector< UInt8 > data ) = 0;


    // ==================== Field Handlers (implement state management) ====================

    /// Get VisitorCount value
    virtual ::lap::core::Future< UInt32 > OnGetVisitorCount() = 0;

    /// Get ServerName value
    virtual ::lap::core::Future< String > OnGetServerName() = 0;

    /// Set ServerName value
    virtual ::lap::core::Future< void > OnSetServerName( const String& value ) = 0;

    /// Get Temperature value
    virtual ::lap::core::Future< Double > OnGetTemperature() = 0;

    /// Set Temperature value
    virtual ::lap::core::Future< void > OnSetTemperature( Double value ) = 0;


    // ==================== Lifecycle Hooks (optional overrides) ====================

    /// Called after OfferService succeeds, before main loop
    virtual void OnStart() {}

    /// Called after main loop exits, before shutdown
    virtual void OnStop() {}

    /// Called each tick (~1s). Return false to stop the server.
    virtual bool OnTick( UInt32 /* tickCount */ ) { return true; }

    // ==================== Event Helpers (send events to subscribers) ====================

    /// Send Greeting event (pre-built struct)
    void SendGreeting( const GreetingEvent& data )
    {
        if ( !m_pSkeleton ) { return; }
        auto sample = m_pSkeleton->greeting.Allocate();
        if ( sample.HasValue() )
        {
            *sample.Value() = data;
            m_pSkeleton->greeting.Send( ::std::move( sample ).Value() );
        }
    }

    /// Send Greeting event (individual fields)
    void SendGreeting( const HelloWorldTypes::GreetingMessage& message )
    {
        if ( !m_pSkeleton ) { return; }
        auto sample = m_pSkeleton->greeting.Allocate();
        if ( sample.HasValue() )
        {
            sample.Value()->message = message;
            m_pSkeleton->greeting.Send( ::std::move( sample ).Value() );
        }
    }

    /// Send StatusChanged event (pre-built struct)
    void SendStatusChanged( const StatusChangedEvent& data )
    {
        if ( !m_pSkeleton ) { return; }
        auto sample = m_pSkeleton->statusChanged.Allocate();
        if ( sample.HasValue() )
        {
            *sample.Value() = data;
            m_pSkeleton->statusChanged.Send( ::std::move( sample ).Value() );
        }
    }

    /// Send StatusChanged event (individual fields)
    void SendStatusChanged( const HelloWorldTypes::ServerStatus& status )
    {
        if ( !m_pSkeleton ) { return; }
        auto sample = m_pSkeleton->statusChanged.Allocate();
        if ( sample.HasValue() )
        {
            sample.Value()->status = status;
            m_pSkeleton->statusChanged.Send( ::std::move( sample ).Value() );
        }
    }

    /// Send DataStream event (pre-built struct)
    void SendDataStream( const DataStreamEvent& data )
    {
        if ( !m_pSkeleton ) { return; }
        auto sample = m_pSkeleton->dataStream.Allocate();
        if ( sample.HasValue() )
        {
            *sample.Value() = data;
            m_pSkeleton->dataStream.Send( ::std::move( sample ).Value() );
        }
    }

    /// Send DataStream event (individual fields)
    void SendDataStream( const HelloWorldTypes::DataChunk& chunk )
    {
        if ( !m_pSkeleton ) { return; }
        auto sample = m_pSkeleton->dataStream.Allocate();
        if ( sample.HasValue() )
        {
            sample.Value()->chunk = chunk;
            m_pSkeleton->dataStream.Send( ::std::move( sample ).Value() );
        }
    }


    // ==================== Field Notifications (push updates to subscribers) ====================

    /// Notify subscribers of Temperature change
    void UpdateTemperature( Double value )
    {
        if ( m_pSkeleton ) { m_pSkeleton->temperature.Update( value ); }
    }


    // ==================== Framework Entry Point ====================

    /**
     * @brief Run the server (blocks until Stop() or SIGINT/SIGTERM)
     * @return 0 on success, non-zero on error
     */
    int Run( int /* argc */ = 0, char** /* argv */ = nullptr )
    {
        // Install signal handler
        s_instance_ = this;
        ::std::signal( SIGINT, &HelloWorldServerApp::signalHandler_ );
        ::std::signal( SIGTERM, &HelloWorldServerApp::signalHandler_ );

        ::std::cout << "=== " << skeleton::HelloWorldSkeleton::kServiceName << " Server (CoreIPC) ===" << ::std::endl;

        // Phase 1 — Registry Dispatcher
        ::lap::com::registry::CRegistryDispatcher dispatcher;
        auto dispInit = dispatcher.Initialize();
        if ( !dispInit.HasValue() )
        {
            ::std::cerr << "[ServerApp] Dispatcher init failed: "
                        << dispInit.Error().Message() << ::std::endl;
            return 1;
        }
        ::std::thread dispThread( [&]() { dispatcher.Run(); } );
        ::std::this_thread::sleep_for( ::std::chrono::milliseconds( 300 ) );

        // Phase 2 — CoreIPC Binding
        auto pCoreIpc = ::lap::com::MakeShared< ::lap::com::binding::CoreIPCBinding >();
        auto ipcR = pCoreIpc->Initialize();
        if ( !ipcR )
        {
            ::std::cerr << "[ServerApp] CoreIPC init failed: "
                        << ipcR.Error().Message() << ::std::endl;
            dispatcher.Shutdown();
            if ( dispThread.joinable() ) { dispThread.join(); }
            return 1;
        }
        ::std::this_thread::sleep_for( ::std::chrono::milliseconds( 200 ) );

        // Phase 4 — BindingManager Registration
        auto& mgr = ::lap::com::binding::BindingManager::GetInstance();
        {
            ::lap::com::binding::BindingConfig cfg;
            cfg.name     = "coreipc-server";
            cfg.priority = ::lap::com::binding::BindingPriority::kCoreIpc;
            cfg.enabled  = true;
            auto r = mgr.RegisterBinding( cfg, pCoreIpc );
            if ( !r.HasValue() )
            {
                ::std::cerr << "[ServerApp] RegisterBinding(CoreIPC) failed" << ::std::endl;
                pCoreIpc->Shutdown();
                dispatcher.Shutdown();
                if ( dispThread.joinable() ) { dispThread.join(); }
                return 1;
            }
        }

        // Phase 6 — Create Skeleton
        skeleton::HelloWorldSkeleton skel(
            ::lap::core::InstanceSpecifier( "HelloWorld/Provider" ) );
        m_pSkeleton = &skel;

        // Phase 7 — Wire Method Handlers → virtual callbacks
        skel.sayHello.RegisterMethodHandler(
            [this]( String name ) -> ::lap::core::Future< String >
            { return OnSayHello( ::std::move( name ) ); } );
        skel.add.RegisterMethodHandler(
            [this]( UInt32 a, UInt32 b ) -> ::lap::core::Future< UInt32 >
            { return OnAdd( ::std::move( a ), ::std::move( b ) ); } );
        skel.notifyLog.RegisterMethodHandler(
            [this]( String message )
            { OnNotifyLog( ::std::move( message ) ); } );
        skel.computeHash.RegisterMethodHandler(
            [this]( ::std::vector< UInt8 > data ) -> ::lap::core::Future< UInt64 >
            { return OnComputeHash( ::std::move( data ) ); } );

        // Phase 8 — Wire Field Handlers → virtual callbacks
        skel.visitorCount.RegisterGetHandler(
            [this]() -> ::lap::core::Future< UInt32 >
            { return OnGetVisitorCount(); } );
        skel.serverName.RegisterGetHandler(
            [this]() -> ::lap::core::Future< String >
            { return OnGetServerName(); } );
        skel.serverName.RegisterSetHandler(
            [this]( const String& value ) -> ::lap::core::Future< void >
            { return OnSetServerName( value ); } );
        skel.temperature.RegisterGetHandler(
            [this]() -> ::lap::core::Future< Double >
            { return OnGetTemperature(); } );
        skel.temperature.RegisterSetHandler(
            [this]( const Double& value ) -> ::lap::core::Future< void >
            { return OnSetTemperature( value ); } );

        // Phase 9 — OfferService
        auto offerR = skel.OfferService();
        if ( !offerR.HasValue() )
        {
            ::std::cerr << "[ServerApp] OfferService failed: "
                        << offerR.Error().Message() << ::std::endl;
            m_pSkeleton = nullptr;
            pCoreIpc->Shutdown();
            dispatcher.Shutdown();
            if ( dispThread.joinable() ) { dispThread.join(); }
            mgr.Shutdown();
            return 1;
        }
        ::std::cout << "[ServerApp] Service offered." << ::std::endl;

        // Phase 10 — User lifecycle hook
        OnStart();

        // Phase 11 — Main loop (~1s tick)
        UInt32 tickCount = 0;
        while ( m_running.load() )
        {
            if ( !OnTick( ++tickCount ) ) { break; }
            for ( int i = 0; i < 10 && m_running.load(); ++i )
            {
                ::std::this_thread::sleep_for( ::std::chrono::milliseconds( 100 ) );
            }
        }

        // Phase 12 — Shutdown
        OnStop();
        skel.StopOfferService();
        m_pSkeleton = nullptr;
        pCoreIpc->Shutdown();
        dispatcher.Shutdown();
        if ( dispThread.joinable() ) { dispThread.join(); }
        mgr.Shutdown();
        ::std::cout << "[ServerApp] Goodbye." << ::std::endl;
        return 0;
    }

    /// Request graceful shutdown
    void Stop() noexcept { m_running.store( false ); }

    /// Check if server is running
    bool IsRunning() const noexcept { return m_running.load(); }

protected:
    /// Access the underlying skeleton (for advanced use)
    skeleton::HelloWorldSkeleton& GetSkeleton() { return *m_pSkeleton; }

private:
    static void signalHandler_( int ) noexcept
    {
        if ( s_instance_ ) { s_instance_->m_running.store( false ); }
    }

    static inline HelloWorldServerApp* s_instance_ = nullptr;
    ::std::atomic< bool > m_running{ true };
    skeleton::HelloWorldSkeleton* m_pSkeleton = nullptr;
}; // class HelloWorldServerApp

} // namespace server_app

} // namespace examples

#endif // EXAMPLES_HELLOWORLDSERVERAPP_HPP
