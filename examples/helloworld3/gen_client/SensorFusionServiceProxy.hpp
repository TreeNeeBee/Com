/**
 * @file        SensorFusionServiceProxy.hpp
 * @author      Aii
 * @brief       Auto-generated service proxy for SensorFusionService [SWS_CM_00004]
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

#ifndef HELLOWORLD3_SENSORFUSIONSERVICEPROXY_HPP
#define HELLOWORLD3_SENSORFUSIONSERVICEPROXY_HPP

// ==================== Project-Internal Headers ====================
#include "SensorFusionServiceTypes.hpp"

// ==================== Runtime Headers ====================
#include "ComTypes.hpp"
#include "ProxyBase.hpp"
#include "ServiceHandleType.hpp"
#include "Runtime.hpp"
#include "BindingManager.hpp"
#include "proxy/ProxyEvent.hpp"
#include "proxy/ProxyMethod.hpp"
#include "proxy/ProxyField.hpp"

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


// [SWS_CM_01007] — proxy inner namespace
namespace proxy
{

// [SWS_CM_98447] — events sub-namespace
namespace events
{

    /**
     * @brief SensorAlert event [SWS_CM_00005]
     */
    class SensorAlert final : public ::lap::com::ProxyEvent< SensorAlertEvent > {
    public:
        using SampleType = SensorAlertEvent;
        using ::lap::com::ProxyEvent< SensorAlertEvent >::ProxyEvent;
    };

    /**
     * @brief PositionUpdate event [SWS_CM_00005]
     */
    class PositionUpdate final : public ::lap::com::ProxyEvent< PositionUpdateEvent > {
    public:
        using SampleType = PositionUpdateEvent;
        using ::lap::com::ProxyEvent< PositionUpdateEvent >::ProxyEvent;
    };

    /**
     * @brief RawTelemetry event [SWS_CM_00005]
     */
    class RawTelemetry final : public ::lap::com::ProxyEvent< RawTelemetryEvent > {
    public:
        using SampleType = RawTelemetryEvent;
        using ::lap::com::ProxyEvent< RawTelemetryEvent >::ProxyEvent;
    };

} // namespace events

// [SWS_CM_01015] — methods sub-namespace
namespace methods
{

    /**
     * @brief GetSensorReading method [SWS_CM_00191]
     */
    class GetSensorReading final : public ::lap::com::ProxyMethod< SensorTypes::SensorReading, UInt32 > {
    public:
        using ::lap::com::ProxyMethod< SensorTypes::SensorReading, UInt32 >::ProxyMethod;
    };

    /**
     * @brief CalibrateDevice method [SWS_CM_00191]
     */
    class CalibrateDevice final : public ::lap::com::ProxyMethod< CalibrateDeviceOutput, UInt32, Double > {
    public:
        using ::lap::com::ProxyMethod< CalibrateDeviceOutput, UInt32, Double >::ProxyMethod;
    };

    /**
     * @brief SubmitTelemetry method [SWS_CM_00191]
     */
    class SubmitTelemetry final : public ::lap::com::ProxyFireAndForgetMethod< UInt32, ::std::vector< UInt8 > > {
    public:
        using ::lap::com::ProxyFireAndForgetMethod< UInt32, ::std::vector< UInt8 > >::ProxyFireAndForgetMethod;
    };

    /**
     * @brief ComputeDigest method [SWS_CM_00191]
     */
    class ComputeDigest final : public ::lap::com::ProxyMethod< UInt64, ::std::vector< UInt8 >, String > {
    public:
        using ::lap::com::ProxyMethod< UInt64, ::std::vector< UInt8 >, String >::ProxyMethod;
    };

    /**
     * @brief GetDiagnostics method [SWS_CM_00191]
     */
    class GetDiagnostics final : public ::lap::com::ProxyMethod< SensorTypes::DiagnosticReport, UInt32 > {
    public:
        using ::lap::com::ProxyMethod< SensorTypes::DiagnosticReport, UInt32 >::ProxyMethod;
    };

} // namespace methods

// [SWS_CM_98444] — fields sub-namespace
namespace fields
{

    /**
     * @brief ActiveSensorCount field [SWS_CM_00007]
     */
    class ActiveSensorCount final : public ::lap::com::ProxyField< UInt32 > {
    public:
        using ::lap::com::ProxyField< UInt32 >::ProxyField;
    };

    /**
     * @brief SystemName field [SWS_CM_00007]
     */
    class SystemName final : public ::lap::com::ProxyField< String > {
    public:
        using ::lap::com::ProxyField< String >::ProxyField;
    };

    /**
     * @brief FusionRate field [SWS_CM_00007]
     */
    class FusionRate final : public ::lap::com::ProxyField< Double > {
    public:
        using ::lap::com::ProxyField< Double >::ProxyField;
    };

    /**
     * @brief CurrentPosition field [SWS_CM_00007]
     */
    class CurrentPosition final : public ::lap::com::ProxyField< SensorTypes::GeoPosition > {
    public:
        using ::lap::com::ProxyField< SensorTypes::GeoPosition >::ProxyField;
    };

    /**
     * @brief SystemState field [SWS_CM_00007]
     */
    class SystemState final : public ::lap::com::ProxyField< SensorTypes::DeviceState > {
    public:
        using ::lap::com::ProxyField< SensorTypes::DeviceState >::ProxyField;
    };

} // namespace fields

    /**
     * @brief Service proxy for SensorFusionService [SWS_CM_00004]
     * @note Auto-generated — non-copyable, move-only, named constructor
     * @version 1.0.0
     */
    class SensorFusionServiceProxy final : public ::lap::com::ProxyBase {
    public:
        using HandleType = ::lap::com::ServiceHandleType< SensorFusionServiceProxy >;

        // ==================== Service Identification ====================
        static constexpr UInt16 kServiceId = 0x017a;
        static constexpr const Char* kServiceName = "SensorFusionService";
        static constexpr const Char* kSchemaHash  = "64e50584ec9f900e";
        static constexpr UInt32 kVersionMajor = 1;
        static constexpr UInt32 kVersionMinor = 0;

        /**
         * @brief Named constructor — create proxy from service handle [SWS_CM_10438]
         * @param handle Service handle obtained from FindService
         * @return Result containing proxy instance or error
         */
        static Result< SensorFusionServiceProxy > Create( const HandleType& handle ) noexcept {
            if ( !handle.IsValid() ) {
                return Result< SensorFusionServiceProxy >::FromError(
                    MakeErrorCode( ComErrc::kServiceNotAvailable, 0 ) );
            }

            SensorFusionServiceProxy proxy( handle );
            proxy.NotifyServiceStateChange( ServiceState::kAvailable );

            // Acquire binding and build context
            auto serviceId  = static_cast< ::lap::core::UInt64 >( kServiceId );
            auto instanceId = static_cast< ::lap::core::UInt64 >( handle.GetInstanceId() );
            auto& bindingMgr = ::lap::com::Runtime::GetBindingManager();
            auto* pBinding = bindingMgr.SelectBinding( serviceId, instanceId );

            ::lap::com::CBindingContext context;
            context.pBinding   = pBinding;
            context.serviceId  = serviceId;
            context.instanceId = instanceId;
            context.elementId  = 0;

            proxy.setBindingContext( context );

            return Result< SensorFusionServiceProxy >::FromValue( ::std::move( proxy ) );
        }

        ~SensorFusionServiceProxy() noexcept override = default;

        // Move-only [SWS_CM_11554, SWS_CM_11552]
        SensorFusionServiceProxy( SensorFusionServiceProxy&& ) noexcept = default;
        SensorFusionServiceProxy& operator=( SensorFusionServiceProxy&& ) noexcept = default;

        // Non-copyable [SWS_CM_11553, SWS_CM_11551]
        SensorFusionServiceProxy( const SensorFusionServiceProxy& ) = delete;
        SensorFusionServiceProxy& operator=( const SensorFusionServiceProxy& ) = delete;

        /**
         * @brief Get the handle used to create this proxy [SWS_CM_10383]
         */
        HandleType GetHandle() const noexcept { return m_handle; }

        // ==================== Events [SWS_CM_99445] ====================
        events::SensorAlert sensorAlert;  ///< Event ID 1
        events::PositionUpdate positionUpdate;  ///< Event ID 2
        events::RawTelemetry rawTelemetry;  ///< Event ID 3

        // ==================== Methods [SWS_CM_99447] ====================
        methods::GetSensorReading getSensorReading;
        methods::CalibrateDevice calibrateDevice;
        methods::SubmitTelemetry submitTelemetry;  ///< (fire-and-forget)
        methods::ComputeDigest computeDigest;
        methods::GetDiagnostics getDiagnostics;

        // ==================== Fields [SWS_CM_99446] ====================
        fields::ActiveSensorCount activeSensorCount{ true, false, false }; ///< @note readonly
        fields::SystemName systemName{ true, true, false };
        fields::FusionRate fusionRate{ true, false, true }; ///< @note readonly
        fields::CurrentPosition currentPosition{ true, true, true };
        fields::SystemState systemState{ true, true, true };

    protected:
        /**
         * @brief Protected constructor (use Create() factory)
         */
        explicit SensorFusionServiceProxy( const HandleType& handle ) noexcept
            : ::lap::com::ProxyBase()
            , m_handle( handle )
        {}

        /**
         * @brief Propagate binding context to all sub-components
         */
        void onBindingContextReady( const ::lap::com::CBindingContext& context ) noexcept override {
            ::lap::com::CBindingContext subCtx = context;

            subCtx.elementId = 1;  // SensorAlert
            PropagateBindingContext( sensorAlert, subCtx );
            subCtx.elementId = 2;  // PositionUpdate
            PropagateBindingContext( positionUpdate, subCtx );
            subCtx.elementId = 3;  // RawTelemetry
            PropagateBindingContext( rawTelemetry, subCtx );
            subCtx.elementId = 0x0100;  // GetSensorReading
            PropagateBindingContext( getSensorReading, subCtx );
            subCtx.elementId = 0x0101;  // CalibrateDevice
            PropagateBindingContext( calibrateDevice, subCtx );
            subCtx.elementId = 0x0102;  // SubmitTelemetry
            PropagateBindingContext( submitTelemetry, subCtx );
            subCtx.elementId = 0x0103;  // ComputeDigest
            PropagateBindingContext( computeDigest, subCtx );
            subCtx.elementId = 0x0104;  // GetDiagnostics
            PropagateBindingContext( getDiagnostics, subCtx );
            subCtx.elementId = 0x0200;  // ActiveSensorCount
            PropagateBindingContext( activeSensorCount, subCtx );
            subCtx.elementId = 0x0201;  // SystemName
            PropagateBindingContext( systemName, subCtx );
            subCtx.elementId = 0x0202;  // FusionRate
            PropagateBindingContext( fusionRate, subCtx );
            subCtx.elementId = 0x0203;  // CurrentPosition
            PropagateBindingContext( currentPosition, subCtx );
            subCtx.elementId = 0x0204;  // SystemState
            PropagateBindingContext( systemState, subCtx );
        }
    private:
        HandleType m_handle;
    };

} // namespace proxy

} // namespace helloworld3

#endif // HELLOWORLD3_SENSORFUSIONSERVICEPROXY_HPP
