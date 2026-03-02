/**
 * @file        HelloWorld2ServiceClientApp.hpp
 * @author      Aii
 * @brief       Auto-generated client application framework for HelloWorld2Service
 * @date        2026/02/09
 * @details     Auto-generated from examples/helloworld2/HelloWorld2.fidl by lap-sidl-gen v1.0
 * @copyright   Copyright (c) 2026
 * @note        DO NOT EDIT — This file is auto-generated
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>Auto-generated
 * </table>
 */

#ifndef HELLOWORLD2_HELLOWORLD2SERVICECLIENTAPP_HPP
#define HELLOWORLD2_HELLOWORLD2SERVICECLIENTAPP_HPP

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
#include <functional>
#include <iostream>
#include <thread>

namespace helloworld2
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
 * @brief Client application framework for HelloWorld2Service
 * @details Encapsulates all dual-binding boilerplate (CoreIPC + DDS).
 *          User subclasses and overrides only the needed callbacks:
 *          - Event handlers (virtual, no-op default)
 *          - Lifecycle hooks (OnConnected / OnDisconnected / OnTick)
 *          Uses convenience wrappers to call methods and access fields.
 *
 * Usage:
 *   class MyClient : public HelloWorld2ServiceClientApp {
 *       void OnGreeting( ... ) override { ... }
 *   };
 *   int main() { MyClient c; return c.Run(); }
 */
class HelloWorld2ServiceClientApp
{
public:
    virtual ~HelloWorld2ServiceClientApp() = default;

    // ==================== Event Handlers (override to receive events) ====================

    /// Handle Greeting event
    virtual void OnGreeting( const GreetingEvent& /* data */ ) {}

    /// Handle StatusChanged event
    virtual void OnStatusChanged( const StatusChangedEvent& /* data */ ) {}

    /// Handle DataStream event
    virtual void OnDataStream( const DataStreamEvent& /* data */ ) {}


    // ==================== Field Notifications (override to receive updates) ====================

    /// Notification: VisitorCount changed
    virtual void OnVisitorCountChanged( UInt32 /* value */ ) {}

    /// Notification: ServerName changed
    virtual void OnServerNameChanged( const String& /* value */ ) {}

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
    ::lap::core::Result< String > SayHello( const String& visitorName )
    {
        if ( !m_pProxy ) { return ::lap::core::Result< String >::FromError( ::lap::com::MakeErrorCode( ::lap::com::ComErrc::kServiceNotAvailable ) ); }
        return m_pProxy->sayHello( visitorName );
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
        ::std::signal( SIGINT, &HelloWorld2ServiceClientApp::signalHandler_ );
        ::std::signal( SIGTERM, &HelloWorld2ServiceClientApp::signalHandler_ );

        ::std::cout << "=== " << proxy::HelloWorld2ServiceProxy::kServiceName << " Client (Dual-Binding) ===" << ::std::endl;

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

        // Phase 2 — DDS Binding (graceful fallback if unavailable)
        auto pDds = ::lap::com::MakeShared< ::lap::com::binding::DdsBinding >();
        auto ddsR = pDds->Initialize();
        if ( !ddsR )
        {
            ::std::cerr << "[ClientApp] DDS init failed — CoreIPC-only mode" << ::std::endl;
            pDds.reset();
        }

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
        if ( pDds )
        {
            ::lap::com::binding::BindingConfig cfg;
            cfg.name     = "dds-client";
            cfg.priority = ::lap::com::binding::BindingPriority::kDds;
            cfg.enabled  = true;
            auto r = mgr.RegisterBinding( cfg, pDds );
            if ( !r.HasValue() ) { pDds->Shutdown(); pDds.reset(); }
        }

        // Phase 4 — DDS Type Adapters
        if ( pDds )
        {
            dds_adapter::RegisterHelloWorld2ServiceDdsAdapters(
                proxy::HelloWorld2ServiceProxy::kServiceId );
        }

        // Phase 5 — Service Discovery (unified 3-step)
        ::std::cout << "[ClientApp] Discovering service ..." << ::std::endl;
        bool found = false;
        for ( int attempt = 0; attempt < 30 && !found && m_running.load(); ++attempt )
        {
            auto result = pCoreIpc->FindService( proxy::HelloWorld2ServiceProxy::kServiceId );
            if ( result.HasValue() && !result.Value().empty() ) { found = true; }
            if ( !found ) { ::std::this_thread::sleep_for( ::std::chrono::milliseconds( 200 ) ); }
        }
        if ( !found )
        {
            ::std::cerr << "[ClientApp] Service not found. Is the server running?" << ::std::endl;
            pCoreIpc->Shutdown();
            if ( pDds ) { pDds->Shutdown(); }
            mgr.Shutdown();
            return 1;
        }

        // Phase 6 — Create Proxy
        using HandleType = proxy::HelloWorld2ServiceProxy::HandleType;
        HandleType handle( static_cast< ::lap::com::InstanceIdentifierType >(
            proxy::HelloWorld2ServiceProxy::kServiceId & 0xFFFFU ) );
        auto proxyResult = proxy::HelloWorld2ServiceProxy::Create( handle );
        if ( !proxyResult.HasValue() )
        {
            ::std::cerr << "[ClientApp] Proxy::Create failed" << ::std::endl;
            pCoreIpc->Shutdown();
            if ( pDds ) { pDds->Shutdown(); }
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

        proxy.visitorCount.Subscribe();
        proxy.visitorCount.SetReceiveHandler( [this, &proxy]() {
            auto s = proxy.visitorCount.GetNextSample();
            if ( s.HasValue() && s.Value() ) { OnVisitorCountChanged( *s.Value() ); }
        } );
        proxy.serverName.Subscribe();
        proxy.serverName.SetReceiveHandler( [this, &proxy]() {
            auto s = proxy.serverName.GetNextSample();
            if ( s.HasValue() && s.Value() ) { OnServerNameChanged( *s.Value() ); }
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
        proxy.visitorCount.Unsubscribe();
        proxy.serverName.Unsubscribe();
        proxy.temperature.Unsubscribe();
        m_pProxy = nullptr;
        pCoreIpc->Shutdown();
        if ( pDds ) { pDds->Shutdown(); }
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
    proxy::HelloWorld2ServiceProxy* GetProxy() { return m_pProxy; }

private:
    static void signalHandler_( int ) noexcept
    {
        if ( s_instance_ ) { s_instance_->m_running.store( false ); }
    }

    static inline HelloWorld2ServiceClientApp* s_instance_ = nullptr;
    ::std::atomic< bool > m_running{ true };
    proxy::HelloWorld2ServiceProxy* m_pProxy = nullptr;
}; // class HelloWorld2ServiceClientApp

} // namespace client_app

} // namespace helloworld2

#endif // HELLOWORLD2_HELLOWORLD2SERVICECLIENTAPP_HPP
