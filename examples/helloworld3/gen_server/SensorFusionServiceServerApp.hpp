/**
 * @file        SensorFusionServiceServerApp.hpp
 * @author      Aii
 * @brief       Auto-generated server application framework for SensorFusionService
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

#ifndef HELLOWORLD3_SENSORFUSIONSERVICESERVERAPP_HPP
#define HELLOWORLD3_SENSORFUSIONSERVICESERVERAPP_HPP

// ==================== Generated Headers ====================
#include "SensorFusionServiceSkeleton.hpp"
#include "SensorFusionServiceDdsAdapter.hpp"

// ==================== Binding / Infrastructure ====================
#include "CoreIPCBinding.hpp"
#include "CRegistryDispatcher.hpp"
#include "DdsBinding.hpp"
#include "BindingManager.hpp"

// ==================== Standard Library ====================
#include <atomic>
#include <chrono>
#include <csignal>
#include <functional>
#include <iostream>
#include <mutex>
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
 * @brief Server application framework for SensorFusionService
 * @details Encapsulates all dual-binding boilerplate (CoreIPC + DDS).
 *          User subclasses and implements only the business logic:
 *          - Method handler callbacks (pure virtual)
 *          - Field getter/setter callbacks (pure virtual)
 *          - Event sending via helper methods
 *          - Lifecycle hooks (OnStart / OnStop / OnTick)
 *
 * Usage:
 *   class MyServer : public SensorFusionServiceServerApp {
 *       // implement pure virtual handlers ...
 *   };
 *   int main() { MyServer s; return s.Run(); }
 */
class SensorFusionServiceServerApp
{
public:
    virtual ~SensorFusionServiceServerApp() = default;

    // ==================== Method Handlers (implement business logic) ====================

    /// Handle GetSensorReading → SensorTypes::SensorReading
    virtual ::lap::core::Future< SensorTypes::SensorReading > OnGetSensorReading( UInt32 sensorId ) = 0;

    /// Handle CalibrateDevice → CalibrateDeviceOutput
    virtual ::lap::core::Future< CalibrateDeviceOutput > OnCalibrateDevice( UInt32 deviceId, Double offset ) = 0;

    /// Handle SubmitTelemetry (fire-and-forget)
    virtual void OnSubmitTelemetry( UInt32 channelId, ::std::vector< UInt8 > payload ) = 0;

    /// Handle ComputeDigest → UInt64
    virtual ::lap::core::Future< UInt64 > OnComputeDigest( ::std::vector< UInt8 > data, String algorithm ) = 0;

    /// Handle GetDiagnostics → SensorTypes::DiagnosticReport
    virtual ::lap::core::Future< SensorTypes::DiagnosticReport > OnGetDiagnostics( UInt32 deviceId ) = 0;


    // ==================== Field Handlers (implement state management) ====================

    /// Get ActiveSensorCount value
    virtual ::lap::core::Future< UInt32 > OnGetActiveSensorCount() = 0;

    /// Get SystemName value
    virtual ::lap::core::Future< String > OnGetSystemName() = 0;

    /// Set SystemName value
    virtual ::lap::core::Future< void > OnSetSystemName( const String& value ) = 0;

    /// Get FusionRate value
    virtual ::lap::core::Future< Double > OnGetFusionRate() = 0;

    /// Get CurrentPosition value
    virtual ::lap::core::Future< SensorTypes::GeoPosition > OnGetCurrentPosition() = 0;

    /// Set CurrentPosition value
    virtual ::lap::core::Future< void > OnSetCurrentPosition( const SensorTypes::GeoPosition& value ) = 0;

    /// Get SystemState value
    virtual ::lap::core::Future< SensorTypes::DeviceState > OnGetSystemState() = 0;

    /// Set SystemState value
    virtual ::lap::core::Future< void > OnSetSystemState( const SensorTypes::DeviceState& value ) = 0;


    // ==================== Lifecycle Hooks (optional overrides) ====================

    /// Called after OfferService succeeds, before main loop
    virtual void OnStart() {}

    /// Called after main loop exits, before shutdown
    virtual void OnStop() {}

    /// Called each tick (~1s). Return false to stop the server.
    virtual bool OnTick( UInt32 /* tickCount */ ) { return true; }

    // ==================== Event Helpers (send events to subscribers) ====================

    /// Send SensorAlert event (pre-built struct)
    void SendSensorAlert( const SensorAlertEvent& data )
    {
        if ( !m_pSkeleton ) { return; }
        auto sample = m_pSkeleton->sensorAlert.Allocate();
        if ( sample.HasValue() )
        {
            *sample.Value() = data;
            m_pSkeleton->sensorAlert.Send( ::std::move( sample ).Value() );
        }
    }

    /// Send SensorAlert event (individual fields)
    void SendSensorAlert( const SensorTypes::AlertInfo& alert )
    {
        if ( !m_pSkeleton ) { return; }
        auto sample = m_pSkeleton->sensorAlert.Allocate();
        if ( sample.HasValue() )
        {
            sample.Value()->alert = alert;
            m_pSkeleton->sensorAlert.Send( ::std::move( sample ).Value() );
        }
    }

    /// Send PositionUpdate event (pre-built struct)
    void SendPositionUpdate( const PositionUpdateEvent& data )
    {
        if ( !m_pSkeleton ) { return; }
        auto sample = m_pSkeleton->positionUpdate.Allocate();
        if ( sample.HasValue() )
        {
            *sample.Value() = data;
            m_pSkeleton->positionUpdate.Send( ::std::move( sample ).Value() );
        }
    }

    /// Send PositionUpdate event (individual fields)
    void SendPositionUpdate( const SensorTypes::GeoPosition& position, const SensorTypes::Angle& heading, Double speed )
    {
        if ( !m_pSkeleton ) { return; }
        auto sample = m_pSkeleton->positionUpdate.Allocate();
        if ( sample.HasValue() )
        {
            sample.Value()->position = position;
            sample.Value()->heading = heading;
            sample.Value()->speed = speed;
            m_pSkeleton->positionUpdate.Send( ::std::move( sample ).Value() );
        }
    }

    /// Send RawTelemetry event (pre-built struct)
    void SendRawTelemetry( const RawTelemetryEvent& data )
    {
        if ( !m_pSkeleton ) { return; }
        auto sample = m_pSkeleton->rawTelemetry.Allocate();
        if ( sample.HasValue() )
        {
            *sample.Value() = data;
            m_pSkeleton->rawTelemetry.Send( ::std::move( sample ).Value() );
        }
    }

    /// Send RawTelemetry event (individual fields)
    void SendRawTelemetry( UInt32 channelId, const ::std::vector< UInt8 >& payload )
    {
        if ( !m_pSkeleton ) { return; }
        auto sample = m_pSkeleton->rawTelemetry.Allocate();
        if ( sample.HasValue() )
        {
            sample.Value()->channelId = channelId;
            sample.Value()->payload = payload;
            m_pSkeleton->rawTelemetry.Send( ::std::move( sample ).Value() );
        }
    }


    // ==================== Field Notifications (push updates to subscribers) ====================

    /// Notify subscribers of FusionRate change
    void UpdateFusionRate( Double value )
    {
        if ( m_pSkeleton ) { m_pSkeleton->fusionRate.Update( value ); }
    }

    /// Notify subscribers of CurrentPosition change
    void UpdateCurrentPosition( const SensorTypes::GeoPosition& value )
    {
        if ( m_pSkeleton ) { m_pSkeleton->currentPosition.Update( value ); }
    }

    /// Notify subscribers of SystemState change
    void UpdateSystemState( const SensorTypes::DeviceState& value )
    {
        if ( m_pSkeleton ) { m_pSkeleton->systemState.Update( value ); }
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
        ::std::signal( SIGINT, &SensorFusionServiceServerApp::signalHandler_ );
        ::std::signal( SIGTERM, &SensorFusionServiceServerApp::signalHandler_ );

        ::std::cout << "=== " << skeleton::SensorFusionServiceSkeleton::kServiceName << " Server (Dual-Binding) ===" << ::std::endl;

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

        // Phase 3 — DDS Binding (graceful fallback if unavailable)
        auto pDds = ::lap::com::MakeShared< ::lap::com::binding::DdsBinding >();
        auto ddsR = pDds->Initialize();
        if ( !ddsR )
        {
            ::std::cerr << "[ServerApp] DDS init failed — CoreIPC-only mode" << ::std::endl;
            pDds.reset();
        }

        // Phase 3.5 — SD-Proxy Bridge (DDS ↔ SD-Proxy cache)
        if ( pDds )
        {
            auto bridge = dispatcher.GetSDProxyBridgeFunc();
            if ( bridge ) { pDds->SetSDProxyBridge( bridge ); }
            auto pDdsCapture = pDds;
            dispatcher.GetSDProxy().SetActiveQueryCallback(
                [pDdsCapture]( uint64_t sid ) -> ::std::vector< uint64_t >
                {
                    auto r = pDdsCapture->FindService( sid );
                    return r.HasValue() ? r.Value() : ::std::vector< uint64_t >{};
                } );
        }

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
        if ( pDds )
        {
            ::lap::com::binding::BindingConfig cfg;
            cfg.name     = "dds-server";
            cfg.priority = ::lap::com::binding::BindingPriority::kDds;
            cfg.enabled  = true;
            auto r = mgr.RegisterBinding( cfg, pDds );
            if ( !r.HasValue() ) { pDds->Shutdown(); pDds.reset(); }
        }

        // Phase 5 — DDS Type Adapters
        if ( pDds )
        {
            dds_adapter::RegisterSensorFusionServiceDdsAdapters(
                skeleton::SensorFusionServiceSkeleton::kServiceId );
        }

        // Phase 6 — Create Skeleton
        skeleton::SensorFusionServiceSkeleton skel(
            ::lap::core::InstanceSpecifier( "SensorFusionService/Provider" ) );
        m_pSkeleton = &skel;

        // Phase 7 — Wire Method Handlers → virtual callbacks
        skel.getSensorReading.RegisterMethodHandler(
            [this]( UInt32 sensorId ) -> ::lap::core::Future< SensorTypes::SensorReading >
            { return OnGetSensorReading( ::std::move( sensorId ) ); } );
        skel.calibrateDevice.RegisterMethodHandler(
            [this]( UInt32 deviceId, Double offset ) -> ::lap::core::Future< CalibrateDeviceOutput >
            { return OnCalibrateDevice( ::std::move( deviceId ), ::std::move( offset ) ); } );
        skel.submitTelemetry.RegisterMethodHandler(
            [this]( UInt32 channelId, ::std::vector< UInt8 > payload )
            { OnSubmitTelemetry( ::std::move( channelId ), ::std::move( payload ) ); } );
        skel.computeDigest.RegisterMethodHandler(
            [this]( ::std::vector< UInt8 > data, String algorithm ) -> ::lap::core::Future< UInt64 >
            { return OnComputeDigest( ::std::move( data ), ::std::move( algorithm ) ); } );
        skel.getDiagnostics.RegisterMethodHandler(
            [this]( UInt32 deviceId ) -> ::lap::core::Future< SensorTypes::DiagnosticReport >
            { return OnGetDiagnostics( ::std::move( deviceId ) ); } );

        // Phase 8 — Wire Field Handlers → virtual callbacks
        skel.activeSensorCount.RegisterGetHandler(
            [this]() -> ::lap::core::Future< UInt32 >
            { return OnGetActiveSensorCount(); } );
        skel.systemName.RegisterGetHandler(
            [this]() -> ::lap::core::Future< String >
            { return OnGetSystemName(); } );
        skel.systemName.RegisterSetHandler(
            [this]( const String& value ) -> ::lap::core::Future< void >
            { return OnSetSystemName( value ); } );
        skel.fusionRate.RegisterGetHandler(
            [this]() -> ::lap::core::Future< Double >
            { return OnGetFusionRate(); } );
        skel.currentPosition.RegisterGetHandler(
            [this]() -> ::lap::core::Future< SensorTypes::GeoPosition >
            { return OnGetCurrentPosition(); } );
        skel.currentPosition.RegisterSetHandler(
            [this]( const SensorTypes::GeoPosition& value ) -> ::lap::core::Future< void >
            { return OnSetCurrentPosition( value ); } );
        skel.systemState.RegisterGetHandler(
            [this]() -> ::lap::core::Future< SensorTypes::DeviceState >
            { return OnGetSystemState(); } );
        skel.systemState.RegisterSetHandler(
            [this]( const SensorTypes::DeviceState& value ) -> ::lap::core::Future< void >
            { return OnSetSystemState( value ); } );

        // Phase 9 — OfferService
        auto offerR = skel.OfferService();
        if ( !offerR.HasValue() )
        {
            ::std::cerr << "[ServerApp] OfferService failed: "
                        << offerR.Error().Message() << ::std::endl;
            m_pSkeleton = nullptr;
            pCoreIpc->Shutdown();
            if ( pDds ) { pDds->Shutdown(); }
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
        if ( pDds ) { pDds->Shutdown(); }
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
    skeleton::SensorFusionServiceSkeleton& GetSkeleton() { return *m_pSkeleton; }

private:
    static void signalHandler_( int ) noexcept
    {
        if ( s_instance_ ) { s_instance_->m_running.store( false ); }
    }

    static inline SensorFusionServiceServerApp* s_instance_ = nullptr;
    ::std::atomic< bool > m_running{ true };
    skeleton::SensorFusionServiceSkeleton* m_pSkeleton = nullptr;
}; // class SensorFusionServiceServerApp

} // namespace server_app

} // namespace helloworld3

#endif // HELLOWORLD3_SENSORFUSIONSERVICESERVERAPP_HPP
