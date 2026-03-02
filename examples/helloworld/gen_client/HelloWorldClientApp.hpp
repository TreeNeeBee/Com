/**
 * @file        HelloWorldClientApp.hpp
 * @author      Aii
 * @brief       Auto-generated client application framework for HelloWorld
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

#ifndef EXAMPLES_HELLOWORLDCLIENTAPP_HPP
#define EXAMPLES_HELLOWORLDCLIENTAPP_HPP

// ==================== Generated Headers ====================
#include "HelloWorldProxy.hpp"

// ==================== Binding / Infrastructure ====================
#include "CoreIPCBinding.hpp"
#include "BindingManager.hpp"

// ==================== Standard Library ====================
#include <atomic>
#include <chrono>
#include <csignal>
#include <functional>
#include <iostream>
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


namespace client_app
{

/**
 * @brief Client application framework for HelloWorld
 * @details Encapsulates all CoreIPC binding boilerplate.
 *          User subclasses and overrides only the needed callbacks:
 *          - Event handlers (virtual, no-op default)
 *          - Lifecycle hooks (OnConnected / OnDisconnected / OnTick)
 *          Uses convenience wrappers to call methods and access fields.
 *
 * Usage:
 *   class MyClient : public HelloWorldClientApp {
 *       void OnGreeting( ... ) override { ... }
 *   };
 *   int main() { MyClient c; return c.Run(); }
 */
class HelloWorldClientApp
{
public:
    virtual ~HelloWorldClientApp() = default;

    // ==================== Event Handlers (override to receive events) ====================

    /// Handle Greeting event
    virtual void OnGreeting( const GreetingEvent& /* data */ ) {}

    /// Handle StatusChanged event
    virtual void OnStatusChanged( const StatusChangedEvent& /* data */ ) {}

    /// Handle DataStream event
    virtual void OnDataStream( const DataStreamEvent& /* data */ ) {}


    // ==================== Field Notifications (override to receive updates) ====================

    /// Notification: Temperature changed
    virtual void OnTemperatureChanged( Double /* value */ ) {}

    // ==================== Lifecycle Hooks (optional overrides) ====================

    /// Called after proxy is created and events are subscribed
    virtual void OnConnected() {}

    /// Called before unsubscription and shutdown
    virtual void OnDisconnected() {}

    /// Called each tick (~1s). Return false to stop the client.
    virtual bool OnTick( UInt32 /* tickCount */ ) { return true; }

    // ==================== Method Calls (convenience wrappers) ====================

    /// Call SayHello → String
    ::lap::core::Result< String > SayHello( const String& name )
    {
        if ( !m_pProxy ) { return ::lap::core::Result< String >::FromError( ::lap::com::MakeErrorCode( ::lap::com::ComErrc::kServiceNotAvailable ) ); }
        return m_pProxy->sayHello( name );
    }

    /// Call Add → UInt32
    ::lap::core::Result< UInt32 > Add( UInt32 a, UInt32 b )
    {
        if ( !m_pProxy ) { return ::lap::core::Result< UInt32 >::FromError( ::lap::com::MakeErrorCode( ::lap::com::ComErrc::kServiceNotAvailable ) ); }
        return m_pProxy->add( a, b );
    }

    /// Call NotifyLog (fire-and-forget)
    void NotifyLog( const String& message )
    {
        if ( m_pProxy ) { m_pProxy->notifyLog( message ); }
    }

    /// Call ComputeHash → UInt64
    ::lap::core::Result< UInt64 > ComputeHash( const ::std::vector< UInt8 >& data )
    {
        if ( !m_pProxy ) { return ::lap::core::Result< UInt64 >::FromError( ::lap::com::MakeErrorCode( ::lap::com::ComErrc::kServiceNotAvailable ) ); }
        return m_pProxy->computeHash( data );
    }


    // ==================== Field Operations (convenience wrappers) ====================

    /// Get VisitorCount value
    ::lap::core::Result< UInt32 > GetVisitorCount()
    {
        if ( !m_pProxy ) { return ::lap::core::Result< UInt32 >::FromError( ::lap::com::MakeErrorCode( ::lap::com::ComErrc::kServiceNotAvailable ) ); }
        return m_pProxy->visitorCount.Get();
    }

    /// Get ServerName value
    ::lap::core::Result< String > GetServerName()
    {
        if ( !m_pProxy ) { return ::lap::core::Result< String >::FromError( ::lap::com::MakeErrorCode( ::lap::com::ComErrc::kServiceNotAvailable ) ); }
        return m_pProxy->serverName.Get();
    }

    /// Set ServerName value
    void SetServerName( const String& value )
    {
        if ( m_pProxy ) { m_pProxy->serverName.Set( value ); }
    }

    /// Get Temperature value
    ::lap::core::Result< Double > GetTemperature()
    {
        if ( !m_pProxy ) { return ::lap::core::Result< Double >::FromError( ::lap::com::MakeErrorCode( ::lap::com::ComErrc::kServiceNotAvailable ) ); }
        return m_pProxy->temperature.Get();
    }

    /// Set Temperature value
    void SetTemperature( Double value )
    {
        if ( m_pProxy ) { m_pProxy->temperature.Set( value ); }
    }


    // ==================== Framework Entry Point ====================

    /**
     * @brief Run the client (blocks until Stop() or SIGINT/SIGTERM)
     * @return 0 on success, non-zero on error
     */
    int Run( int /* argc */ = 0, char** /* argv */ = nullptr )
    {
        // Install signal handler
        s_instance_ = this;
        ::std::signal( SIGINT, &HelloWorldClientApp::signalHandler_ );
        ::std::signal( SIGTERM, &HelloWorldClientApp::signalHandler_ );

        ::std::cout << "=== " << proxy::HelloWorldProxy::kServiceName << " Client (CoreIPC) ===" << ::std::endl;

        // Phase 1 — CoreIPC Binding
        auto pCoreIpc = ::lap::com::MakeShared< ::lap::com::binding::CoreIPCBinding >();
        auto ipcR = pCoreIpc->Initialize();
        if ( !ipcR )
        {
            ::std::cerr << "[ClientApp] CoreIPC init failed: "
                        << ipcR.Error().Message() << ::std::endl;
            return 1;
        }
        ::std::this_thread::sleep_for( ::std::chrono::milliseconds( 200 ) );

        // Phase 3 — BindingManager Registration
        auto& mgr = ::lap::com::binding::BindingManager::GetInstance();
        {
            ::lap::com::binding::BindingConfig cfg;
            cfg.name     = "coreipc-client";
            cfg.priority = ::lap::com::binding::BindingPriority::kCoreIpc;
            cfg.enabled  = true;
            auto r = mgr.RegisterBinding( cfg, pCoreIpc );
            if ( !r.HasValue() )
            {
                ::std::cerr << "[ClientApp] RegisterBinding(CoreIPC) failed" << ::std::endl;
                pCoreIpc->Shutdown();
                return 1;
            }
        }

        // Phase 5 — Service Discovery (unified 3-step)
        ::std::cout << "[ClientApp] Discovering service ..." << ::std::endl;
        bool found = false;
        for ( int attempt = 0; attempt < 30 && !found && m_running.load(); ++attempt )
        {
            auto result = pCoreIpc->FindService( proxy::HelloWorldProxy::kServiceId );
            if ( result.HasValue() && !result.Value().empty() ) { found = true; }
            if ( !found ) { ::std::this_thread::sleep_for( ::std::chrono::milliseconds( 200 ) ); }
        }
        if ( !found )
        {
            ::std::cerr << "[ClientApp] Service not found. Is the server running?" << ::std::endl;
            pCoreIpc->Shutdown();
            mgr.Shutdown();
            return 1;
        }

        // Phase 6 — Create Proxy
        using HandleType = proxy::HelloWorldProxy::HandleType;
        HandleType handle( static_cast< ::lap::com::InstanceIdentifierType >(
            proxy::HelloWorldProxy::kServiceId & 0xFFFFU ) );
        auto proxyResult = proxy::HelloWorldProxy::Create( handle );
        if ( !proxyResult.HasValue() )
        {
            ::std::cerr << "[ClientApp] Proxy::Create failed" << ::std::endl;
            pCoreIpc->Shutdown();
            mgr.Shutdown();
            return 1;
        }
        auto proxy = ::std::move( proxyResult ).Value();
        m_pProxy = &proxy;
        ::std::cout << "[ClientApp] Connected." << ::std::endl;
        ::std::this_thread::sleep_for( ::std::chrono::milliseconds( 100 ) );

        // Phase 7 — Subscribe to Events → virtual handlers
        proxy.greeting.Subscribe();
        proxy.greeting.SetReceiveHandler( [this, &proxy]() {
            auto s = proxy.greeting.GetNextSample();
            if ( s.HasValue() && s.Value() ) { OnGreeting( *s.Value() ); }
        } );
        proxy.statusChanged.Subscribe();
        proxy.statusChanged.SetReceiveHandler( [this, &proxy]() {
            auto s = proxy.statusChanged.GetNextSample();
            if ( s.HasValue() && s.Value() ) { OnStatusChanged( *s.Value() ); }
        } );
        proxy.dataStream.Subscribe();
        proxy.dataStream.SetReceiveHandler( [this, &proxy]() {
            auto s = proxy.dataStream.GetNextSample();
            if ( s.HasValue() && s.Value() ) { OnDataStream( *s.Value() ); }
        } );

        proxy.temperature.Subscribe();
        proxy.temperature.SetReceiveHandler( [this, &proxy]() {
            auto s = proxy.temperature.GetNextSample();
            if ( s.HasValue() && s.Value() ) { OnTemperatureChanged( *s.Value() ); }
        } );

        // Phase 8 — User lifecycle hook
        OnConnected();

        // Phase 9 — Main loop (~1s tick)
        UInt32 tickCount = 0;
        while ( m_running.load() )
        {
            if ( !OnTick( ++tickCount ) ) { break; }
            for ( int i = 0; i < 10 && m_running.load(); ++i )
            {
                ::std::this_thread::sleep_for( ::std::chrono::milliseconds( 100 ) );
            }
        }

        // Phase 10 — Shutdown
        OnDisconnected();
        proxy.greeting.Unsubscribe();
        proxy.statusChanged.Unsubscribe();
        proxy.dataStream.Unsubscribe();
        proxy.temperature.Unsubscribe();
        m_pProxy = nullptr;
        pCoreIpc->Shutdown();
        mgr.Shutdown();
        ::std::cout << "[ClientApp] Goodbye." << ::std::endl;
        return 0;
    }

    /// Request graceful shutdown
    void Stop() noexcept { m_running.store( false ); }

    /// Check if client is running
    bool IsRunning() const noexcept { return m_running.load(); }

protected:
    /// Access the underlying proxy (for advanced use)
    proxy::HelloWorldProxy* GetProxy() { return m_pProxy; }

private:
    static void signalHandler_( int ) noexcept
    {
        if ( s_instance_ ) { s_instance_->m_running.store( false ); }
    }

    static inline HelloWorldClientApp* s_instance_ = nullptr;
    ::std::atomic< bool > m_running{ true };
    proxy::HelloWorldProxy* m_pProxy = nullptr;
}; // class HelloWorldClientApp

} // namespace client_app

} // namespace examples

#endif // EXAMPLES_HELLOWORLDCLIENTAPP_HPP
