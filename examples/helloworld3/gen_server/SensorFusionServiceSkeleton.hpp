/**
 * @file        SensorFusionServiceSkeleton.hpp
 * @author      Aii
 * @brief       Auto-generated service skeleton for SensorFusionService [SWS_CM_00002]
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

#ifndef HELLOWORLD3_SENSORFUSIONSERVICESKELETON_HPP
#define HELLOWORLD3_SENSORFUSIONSERVICESKELETON_HPP

// ==================== Project-Internal Headers ====================
#include "SensorFusionServiceTypes.hpp"

// ==================== Runtime Headers ====================
#include "ComTypes.hpp"
#include "SkeletonBase.hpp"
#include "Runtime.hpp"
#include "BindingManager.hpp"
#include "skeleton/SkeletonEvent.hpp"
#include "skeleton/SkeletonMethod.hpp"
#include "skeleton/SkeletonField.hpp"

// ==================== Cross-Module Headers ====================
#include <core/CInstanceSpecifier.hpp>
#include <core/CFuture.hpp>

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


// [SWS_CM_01006] — skeleton inner namespace
namespace skeleton
{

// [SWS_CM_00003] — skeleton events sub-namespace
namespace events
{

    /**
     * @brief SensorAlert event [SWS_CM_00003]
     */
    class SensorAlert final : public ::lap::com::SkeletonEvent< SensorAlertEvent > {
    public:
        using SampleType = SensorAlertEvent;
        using ::lap::com::SkeletonEvent< SensorAlertEvent >::SkeletonEvent;
    };

    /**
     * @brief PositionUpdate event [SWS_CM_00003]
     */
    class PositionUpdate final : public ::lap::com::SkeletonEvent< PositionUpdateEvent > {
    public:
        using SampleType = PositionUpdateEvent;
        using ::lap::com::SkeletonEvent< PositionUpdateEvent >::SkeletonEvent;
    };

    /**
     * @brief RawTelemetry event [SWS_CM_00003]
     */
    class RawTelemetry final : public ::lap::com::SkeletonEvent< RawTelemetryEvent > {
    public:
        using SampleType = RawTelemetryEvent;
        using ::lap::com::SkeletonEvent< RawTelemetryEvent >::SkeletonEvent;
    };

} // namespace events

// skeleton methods sub-namespace
namespace methods
{

    /**
     * @brief GetSensorReading method
     */
    class GetSensorReading final : public ::lap::com::SkeletonMethod< SensorTypes::SensorReading, UInt32 > {
    public:
        using ::lap::com::SkeletonMethod< SensorTypes::SensorReading, UInt32 >::SkeletonMethod;
    };

    /**
     * @brief CalibrateDevice method
     */
    class CalibrateDevice final : public ::lap::com::SkeletonMethod< CalibrateDeviceOutput, UInt32, Double > {
    public:
        using ::lap::com::SkeletonMethod< CalibrateDeviceOutput, UInt32, Double >::SkeletonMethod;
    };

    /**
     * @brief SubmitTelemetry method
     */
    class SubmitTelemetry final : public ::lap::com::SkeletonFireAndForgetMethod< UInt32, ::std::vector< UInt8 > > {
    public:
        using ::lap::com::SkeletonFireAndForgetMethod< UInt32, ::std::vector< UInt8 > >::SkeletonFireAndForgetMethod;
    };

    /**
     * @brief ComputeDigest method
     */
    class ComputeDigest final : public ::lap::com::SkeletonMethod< UInt64, ::std::vector< UInt8 >, String > {
    public:
        using ::lap::com::SkeletonMethod< UInt64, ::std::vector< UInt8 >, String >::SkeletonMethod;
    };

    /**
     * @brief GetDiagnostics method
     */
    class GetDiagnostics final : public ::lap::com::SkeletonMethod< SensorTypes::DiagnosticReport, UInt32 > {
    public:
        using ::lap::com::SkeletonMethod< SensorTypes::DiagnosticReport, UInt32 >::SkeletonMethod;
    };

} // namespace methods

// skeleton fields sub-namespace
namespace fields
{

    /**
     * @brief ActiveSensorCount field [SWS_CM_00007]
     */
    class ActiveSensorCount final : public ::lap::com::SkeletonField< UInt32 > {
    public:
        using ::lap::com::SkeletonField< UInt32 >::SkeletonField;
    };

    /**
     * @brief SystemName field [SWS_CM_00007]
     */
    class SystemName final : public ::lap::com::SkeletonField< String > {
    public:
        using ::lap::com::SkeletonField< String >::SkeletonField;
    };

    /**
     * @brief FusionRate field [SWS_CM_00007]
     */
    class FusionRate final : public ::lap::com::SkeletonField< Double > {
    public:
        using ::lap::com::SkeletonField< Double >::SkeletonField;
    };

    /**
     * @brief CurrentPosition field [SWS_CM_00007]
     */
    class CurrentPosition final : public ::lap::com::SkeletonField< SensorTypes::GeoPosition > {
    public:
        using ::lap::com::SkeletonField< SensorTypes::GeoPosition >::SkeletonField;
    };

    /**
     * @brief SystemState field [SWS_CM_00007]
     */
    class SystemState final : public ::lap::com::SkeletonField< SensorTypes::DeviceState > {
    public:
        using ::lap::com::SkeletonField< SensorTypes::DeviceState >::SkeletonField;
    };

} // namespace fields

    /**
     * @brief Service skeleton for SensorFusionService [SWS_CM_00002]
     * @note Auto-generated — non-copyable, move-only
     * @version 1.0.0
     */
    class SensorFusionServiceSkeleton : public ::lap::com::SkeletonBase {
    public:
        // ==================== Service Identification ====================
        static constexpr UInt16 kServiceId = 0x017a;
        static constexpr const Char* kServiceName = "SensorFusionService";
        static constexpr const Char* kSchemaHash  = "64e50584ec9f900e";

        /**
         * @brief Constructor [SWS_CM_00130]
         * @param instanceSpec Instance specifier for the service
         * @param mode Method call processing mode
         */
        explicit SensorFusionServiceSkeleton(
            ::lap::core::InstanceSpecifier instanceSpec,
            MethodCallProcessingMode mode = MethodCallProcessingMode::kEvent ) noexcept
            : ::lap::com::SkeletonBase( ::std::move( instanceSpec ), mode )
        {}

        /**
         * @brief Destructor — auto-stops offering [SWS_CM_11549]
         */
        ~SensorFusionServiceSkeleton() noexcept override {
            if ( IsOffered() ) {
                StopOfferService();
            }
        }

        // Move-only [SWS_CM_11547, SWS_CM_11545]
        SensorFusionServiceSkeleton( SensorFusionServiceSkeleton&& ) noexcept = default;
        SensorFusionServiceSkeleton& operator=( SensorFusionServiceSkeleton&& ) noexcept = default;

        // Non-copyable [SWS_CM_11546, SWS_CM_11544]
        SensorFusionServiceSkeleton( const SensorFusionServiceSkeleton& ) = delete;
        SensorFusionServiceSkeleton& operator=( const SensorFusionServiceSkeleton& ) = delete;

        // ==================== Events [SWS_CM_99557] ====================
        events::SensorAlert sensorAlert;
        events::PositionUpdate positionUpdate;
        events::RawTelemetry rawTelemetry;

        // ==================== Methods ====================
        methods::GetSensorReading getSensorReading;
        methods::CalibrateDevice calibrateDevice;
        methods::SubmitTelemetry submitTelemetry;  ///< (fire-and-forget)
        methods::ComputeDigest computeDigest;
        methods::GetDiagnostics getDiagnostics;

        // ==================== Fields [SWS_CM_99558] ====================
        fields::ActiveSensorCount activeSensorCount{ true, false, false };  ///< @note readonly
        fields::SystemName systemName{ true, true, false };
        fields::FusionRate fusionRate{ true, false, true };  ///< @note readonly
        fields::CurrentPosition currentPosition{ true, true, true };
        fields::SystemState systemState{ true, true, true };

    protected:
        /**
         * @brief Offer service via Runtime → BindingManager [SWS_CM_00101]
         */
        Result< void > doOfferService() noexcept override {
            // Register via BindingManager
            auto serviceId  = static_cast< ::lap::core::UInt64 >( kServiceId );
            auto instanceId = serviceId & 0xFFFFU;

            auto& bindingMgr = ::lap::com::Runtime::GetBindingManager();
            auto* pBinding = bindingMgr.SelectBinding( serviceId, instanceId );

            if ( pBinding == nullptr ) {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kNoBindingAvailable, 0 ) );
            }

            auto result = pBinding->OfferService( serviceId, instanceId );
            if ( result.HasValue() ) {
                ::lap::com::CBindingContext context;
                context.pBinding   = pBinding;
                context.serviceId  = serviceId;
                context.instanceId = instanceId;
                context.elementId  = 0;
                setBindingContext( context );
            }

            return result;
        }

        /**
         * @brief Stop offering service [SWS_CM_00111]
         */
        void doStopOfferService() noexcept override {
            const auto& ctx = GetBindingContext();
            if ( ctx.IsValid() ) {
                ctx.pBinding->StopOfferService( ctx.serviceId, ctx.instanceId );
            }
            setBindingContext( ::lap::com::CBindingContext{} );
        }

        /**
         * @brief Propagate binding context to all sub-components
         */
        void onBindingContextReady( const ::lap::com::CBindingContext& context ) noexcept override {
            ::lap::com::CBindingContext subCtx = context;

            subCtx.elementId = 1;
            PropagateBindingContext( sensorAlert, subCtx );
            subCtx.elementId = 2;
            PropagateBindingContext( positionUpdate, subCtx );
            subCtx.elementId = 3;
            PropagateBindingContext( rawTelemetry, subCtx );
            subCtx.elementId = 0x0100;
            PropagateBindingContext( getSensorReading, subCtx );
            subCtx.elementId = 0x0101;
            PropagateBindingContext( calibrateDevice, subCtx );
            subCtx.elementId = 0x0102;
            PropagateBindingContext( submitTelemetry, subCtx );
            subCtx.elementId = 0x0103;
            PropagateBindingContext( computeDigest, subCtx );
            subCtx.elementId = 0x0104;
            PropagateBindingContext( getDiagnostics, subCtx );
            subCtx.elementId = 0x0200;
            PropagateBindingContext( activeSensorCount, subCtx );
            subCtx.elementId = 0x0201;
            PropagateBindingContext( systemName, subCtx );
            subCtx.elementId = 0x0202;
            PropagateBindingContext( fusionRate, subCtx );
            subCtx.elementId = 0x0203;
            PropagateBindingContext( currentPosition, subCtx );
            subCtx.elementId = 0x0204;
            PropagateBindingContext( systemState, subCtx );
        }
    };

} // namespace skeleton

} // namespace helloworld3

#endif // HELLOWORLD3_SENSORFUSIONSERVICESKELETON_HPP
