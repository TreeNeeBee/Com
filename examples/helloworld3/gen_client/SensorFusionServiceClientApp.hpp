/**
 * @file        SensorFusionServiceClientApp.hpp
 * @author      Aii
 * @brief       Auto-generated client application framework for SensorFusionService
 * @date        2026/02/09
 * @details     Auto-generated from examples/helloworld3/HelloWorld3.fidl by lap-sidl-gen v1.0
 * @copyright   Copyright (c) 2026
 * @note        DO NOT EDIT — This file is auto-generated
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>Auto-generated
 * </table>
 */

#ifndef HELLOWORLD3_SENSORFUSIONSERVICECLIENTAPP_HPP
#define HELLOWORLD3_SENSORFUSIONSERVICECLIENTAPP_HPP

// ==================== Generated Headers ====================
#include "SensorFusionServiceProxy.hpp"
#include "SensorFusionServiceDdsAdapter.hpp"

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

namespace helloworld3
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
 * @brief Client application framework for SensorFusionService
 * @details Encapsulates all dual-binding boilerplate (CoreIPC + DDS).
 *          User subclasses and overrides only the needed callbacks:
 *          - Event handlers (virtual, no-op default)
 *          - Lifecycle hooks (OnConnected / OnDisconnected / OnTick)
 *          Uses convenience wrappers to call methods and access fields.
 *
 * Usage:
 *   class MyClient : public SensorFusionServiceClientApp {
 *       void OnGreeting( ... ) override { ... }
 *   };
 *   int main() { MyClient c; return c.Run(); }
 */
class SensorFusionServiceClientApp
{
public:
    virtual ~SensorFusionServiceClientApp() = default;

    // ==================== Event Handlers (override to receive events) ====================

    /// Handle SensorAlert event
    virtual void OnSensorAlert( const SensorAlertEvent& /* data */ ) {}

    /// Handle PositionUpdate event
    virtual void OnPositionUpdate( const PositionUpdateEvent& /* data */ ) {}

    /// Handle RawTelemetry event
    virtual void OnRawTelemetry( const RawTelemetryEvent& /* data */ ) {}


    // ==================== Field Notifications (override to receive updates) ====================

    /// Notification: FusionRate changed
    virtual void OnFusionRateChanged( Double /* value */ ) {}

    /// Notification: CurrentPosition changed
    virtual void OnCurrentPositionChanged( const SensorTypes::GeoPosition& /* value */ ) {}

    /// Notification: SystemState changed
    virtual void OnSystemStateChanged( const SensorTypes::DeviceState& /* value */ ) {}

    // ==================== Lifecycle Hooks (optional overrides) ====================

    /// Called after proxy is created and events are subscribed
    virtual void OnConnected() {}

    /// Called before unsubscription and shutdown
    virtual void OnDisconnected() {}

    /// Called each tick (~1s). Return false to stop the client.
    virtual bool OnTick( UInt32 /* tickCount */ ) { return true; }

    // ==================== Method Calls (convenience wrappers) ====================

    /// Call GetSensorReading → SensorTypes::SensorReading
    ::lap::core::Result< SensorTypes::SensorReading > GetSensorReading( UInt32 sensorId )
    {
        if ( !m_pProxy ) { return ::lap::core::Result< SensorTypes::SensorReading >::FromError( ::lap::com::MakeErrorCode( ::lap::com::ComErrc::kServiceNotAvailable ) ); }
        return m_pProxy->getSensorReading( sensorId );
    }

    /// Call CalibrateDevice → CalibrateDeviceOutput
    ::lap::core::Result< CalibrateDeviceOutput > CalibrateDevice( UInt32 deviceId, Double offset )
    {
        if ( !m_pProxy ) { return ::lap::core::Result< CalibrateDeviceOutput >::FromError( ::lap::com::MakeErrorCode( ::lap::com::ComErrc::kServiceNotAvailable ) ); }
        return m_pProxy->calibrateDevice( deviceId, offset );
    }

    /// Call SubmitTelemetry (fire-and-forget)
    void SubmitTelemetry( UInt32 channelId, const ::std::vector< UInt8 >& payload )
    {
        if ( m_pProxy ) { m_pProxy->submitTelemetry( channelId, payload ); }
    }

    /// Call ComputeDigest → UInt64
    ::lap::core::Result< UInt64 > ComputeDigest( const ::std::vector< UInt8 >& data, const String& algorithm )
    {
        if ( !m_pProxy ) { return ::lap::core::Result< UInt64 >::FromError( ::lap::com::MakeErrorCode( ::lap::com::ComErrc::kServiceNotAvailable ) ); }
        return m_pProxy->computeDigest( data, algorithm );
    }

    /// Call GetDiagnostics → SensorTypes::DiagnosticReport
    ::lap::core::Result< SensorTypes::DiagnosticReport > GetDiagnostics( UInt32 deviceId )
    {
        if ( !m_pProxy ) { return ::lap::core::Result< SensorTypes::DiagnosticReport >::FromError( ::lap::com::MakeErrorCode( ::lap::com::ComErrc::kServiceNotAvailable ) ); }
        return m_pProxy->getDiagnostics( deviceId );
    }


    // ==================== Field Operations (convenience wrappers) ====================

    /// Get ActiveSensorCount value
    ::lap::core::Result< UInt32 > GetActiveSensorCount()
    {
        if ( !m_pProxy ) { return ::lap::core::Result< UInt32 >::FromError( ::lap::com::MakeErrorCode( ::lap::com::ComErrc::kServiceNotAvailable ) ); }
        return m_pProxy->activeSensorCount.Get();
    }

    /// Get SystemName value
    ::lap::core::Result< String > GetSystemName()
    {
        if ( !m_pProxy ) { return ::lap::core::Result< String >::FromError( ::lap::com::MakeErrorCode( ::lap::com::ComErrc::kServiceNotAvailable ) ); }
        return m_pProxy->systemName.Get();
    }

    /// Set SystemName value
    void SetSystemName( const String& value )
    {
        if ( m_pProxy ) { m_pProxy->systemName.Set( value ); }
    }

    /// Get FusionRate value
    ::lap::core::Result< Double > GetFusionRate()
    {
        if ( !m_pProxy ) { return ::lap::core::Result< Double >::FromError( ::lap::com::MakeErrorCode( ::lap::com::ComErrc::kServiceNotAvailable ) ); }
        return m_pProxy->fusionRate.Get();
    }

    /// Get CurrentPosition value
    ::lap::core::Result< SensorTypes::GeoPosition > GetCurrentPosition()
    {
        if ( !m_pProxy ) { return ::lap::core::Result< SensorTypes::GeoPosition >::FromError( ::lap::com::MakeErrorCode( ::lap::com::ComErrc::kServiceNotAvailable ) ); }
        return m_pProxy->currentPosition.Get();
    }

    /// Set CurrentPosition value
    void SetCurrentPosition( const SensorTypes::GeoPosition& value )
    {
        if ( m_pProxy ) { m_pProxy->currentPosition.Set( value ); }
    }

    /// Get SystemState value
    ::lap::core::Result< SensorTypes::DeviceState > GetSystemState()
    {
        if ( !m_pProxy ) { return ::lap::core::Result< SensorTypes::DeviceState >::FromError( ::lap::com::MakeErrorCode( ::lap::com::ComErrc::kServiceNotAvailable ) ); }
        return m_pProxy->systemState.Get();
    }

    /// Set SystemState value
    void SetSystemState( const SensorTypes::DeviceState& value )
    {
        if ( m_pProxy ) { m_pProxy->systemState.Set( value ); }
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
        ::std::signal( SIGINT, &SensorFusionServiceClientApp::signalHandler_ );
        ::std::signal( SIGTERM, &SensorFusionServiceClientApp::signalHandler_ );

        ::std::cout << "=== " << proxy::SensorFusionServiceProxy::kServiceName << " Client (Dual-Binding) ===" << ::std::endl;

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
            dds_adapter::RegisterSensorFusionServiceDdsAdapters(
                proxy::SensorFusionServiceProxy::kServiceId );
        }

        // Phase 5 — Service Discovery (unified 3-step)
        ::std::cout << "[ClientApp] Discovering service ..." << ::std::endl;
        bool found = false;
        for ( int attempt = 0; attempt < 30 && !found && m_running.load(); ++attempt )
        {
            auto result = pCoreIpc->FindService( proxy::SensorFusionServiceProxy::kServiceId );
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
        using HandleType = proxy::SensorFusionServiceProxy::HandleType;
        HandleType handle( static_cast< ::lap::com::InstanceIdentifierType >(
            proxy::SensorFusionServiceProxy::kServiceId & 0xFFFFU ) );
        auto proxyResult = proxy::SensorFusionServiceProxy::Create( handle );
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
        proxy.sensorAlert.Subscribe();
        proxy.sensorAlert.SetReceiveHandler( [this, &proxy]() {
            auto s = proxy.sensorAlert.GetNextSample();
            if ( s.HasValue() && s.Value() ) { OnSensorAlert( *s.Value() ); }
        } );
        proxy.positionUpdate.Subscribe();
        proxy.positionUpdate.SetReceiveHandler( [this, &proxy]() {
            auto s = proxy.positionUpdate.GetNextSample();
            if ( s.HasValue() && s.Value() ) { OnPositionUpdate( *s.Value() ); }
        } );
        proxy.rawTelemetry.Subscribe();
        proxy.rawTelemetry.SetReceiveHandler( [this, &proxy]() {
            auto s = proxy.rawTelemetry.GetNextSample();
            if ( s.HasValue() && s.Value() ) { OnRawTelemetry( *s.Value() ); }
        } );

        proxy.fusionRate.Subscribe();
        proxy.fusionRate.SetReceiveHandler( [this, &proxy]() {
            auto s = proxy.fusionRate.GetNextSample();
            if ( s.HasValue() && s.Value() ) { OnFusionRateChanged( *s.Value() ); }
        } );
        proxy.currentPosition.Subscribe();
        proxy.currentPosition.SetReceiveHandler( [this, &proxy]() {
            auto s = proxy.currentPosition.GetNextSample();
            if ( s.HasValue() && s.Value() ) { OnCurrentPositionChanged( *s.Value() ); }
        } );
        proxy.systemState.Subscribe();
        proxy.systemState.SetReceiveHandler( [this, &proxy]() {
            auto s = proxy.systemState.GetNextSample();
            if ( s.HasValue() && s.Value() ) { OnSystemStateChanged( *s.Value() ); }
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
        proxy.sensorAlert.Unsubscribe();
        proxy.positionUpdate.Unsubscribe();
        proxy.rawTelemetry.Unsubscribe();
        proxy.fusionRate.Unsubscribe();
        proxy.currentPosition.Unsubscribe();
        proxy.systemState.Unsubscribe();
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
    proxy::SensorFusionServiceProxy* GetProxy() { return m_pProxy; }

private:
    static void signalHandler_( int ) noexcept
    {
        if ( s_instance_ ) { s_instance_->m_running.store( false ); }
    }

    static inline SensorFusionServiceClientApp* s_instance_ = nullptr;
    ::std::atomic< bool > m_running{ true };
    proxy::SensorFusionServiceProxy* m_pProxy = nullptr;
}; // class SensorFusionServiceClientApp

} // namespace client_app

} // namespace helloworld3

#endif // HELLOWORLD3_SENSORFUSIONSERVICECLIENTAPP_HPP
