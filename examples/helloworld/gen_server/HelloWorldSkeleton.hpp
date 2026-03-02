/**
 * @file        HelloWorldSkeleton.hpp
 * @author      Aii
 * @brief       Auto-generated service skeleton for HelloWorld [SWS_CM_00002]
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

#ifndef EXAMPLES_HELLOWORLDSKELETON_HPP
#define EXAMPLES_HELLOWORLDSKELETON_HPP

// ==================== Project-Internal Headers ====================
#include "HelloWorldTypes.hpp"

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


// [SWS_CM_01006] — skeleton inner namespace
namespace skeleton
{

// [SWS_CM_00003] — skeleton events sub-namespace
namespace events
{

    /**
     * @brief Greeting event [SWS_CM_00003]
     */
    class Greeting final : public ::lap::com::SkeletonEvent< GreetingEvent > {
    public:
        using SampleType = GreetingEvent;
        using ::lap::com::SkeletonEvent< GreetingEvent >::SkeletonEvent;
    };

    /**
     * @brief StatusChanged event [SWS_CM_00003]
     */
    class StatusChanged final : public ::lap::com::SkeletonEvent< StatusChangedEvent > {
    public:
        using SampleType = StatusChangedEvent;
        using ::lap::com::SkeletonEvent< StatusChangedEvent >::SkeletonEvent;
    };

    /**
     * @brief DataStream event [SWS_CM_00003]
     */
    class DataStream final : public ::lap::com::SkeletonEvent< DataStreamEvent > {
    public:
        using SampleType = DataStreamEvent;
        using ::lap::com::SkeletonEvent< DataStreamEvent >::SkeletonEvent;
    };

} // namespace events

// skeleton methods sub-namespace
namespace methods
{

    /**
     * @brief SayHello method
     */
    class SayHello final : public ::lap::com::SkeletonMethod< String, String > {
    public:
        using ::lap::com::SkeletonMethod< String, String >::SkeletonMethod;
    };

    /**
     * @brief Add method
     */
    class Add final : public ::lap::com::SkeletonMethod< UInt32, UInt32, UInt32 > {
    public:
        using ::lap::com::SkeletonMethod< UInt32, UInt32, UInt32 >::SkeletonMethod;
    };

    /**
     * @brief NotifyLog method
     */
    class NotifyLog final : public ::lap::com::SkeletonFireAndForgetMethod< String > {
    public:
        using ::lap::com::SkeletonFireAndForgetMethod< String >::SkeletonFireAndForgetMethod;
    };

    /**
     * @brief ComputeHash method
     */
    class ComputeHash final : public ::lap::com::SkeletonMethod< UInt64, ::std::vector< UInt8 > > {
    public:
        using ::lap::com::SkeletonMethod< UInt64, ::std::vector< UInt8 > >::SkeletonMethod;
    };

} // namespace methods

// skeleton fields sub-namespace
namespace fields
{

    /**
     * @brief VisitorCount field [SWS_CM_00007]
     */
    class VisitorCount final : public ::lap::com::SkeletonField< UInt32 > {
    public:
        using ::lap::com::SkeletonField< UInt32 >::SkeletonField;
    };

    /**
     * @brief ServerName field [SWS_CM_00007]
     */
    class ServerName final : public ::lap::com::SkeletonField< String > {
    public:
        using ::lap::com::SkeletonField< String >::SkeletonField;
    };

    /**
     * @brief Temperature field [SWS_CM_00007]
     */
    class Temperature final : public ::lap::com::SkeletonField< Double > {
    public:
        using ::lap::com::SkeletonField< Double >::SkeletonField;
    };

} // namespace fields

    /**
     * @brief Service skeleton for HelloWorld [SWS_CM_00002]
     * @note Auto-generated — non-copyable, move-only
     * @version 2.0.0
     */
    class HelloWorldSkeleton : public ::lap::com::SkeletonBase {
    public:
        // ==================== Service Identification ====================
        static constexpr UInt16 kServiceId = 0x02e0;
        static constexpr const Char* kServiceName = "HelloWorld";
        static constexpr const Char* kSchemaHash  = "5a1b660e3ef06eb8";

        /**
         * @brief Constructor [SWS_CM_00130]
         * @param instanceSpec Instance specifier for the service
         * @param mode Method call processing mode
         */
        explicit HelloWorldSkeleton(
            ::lap::core::InstanceSpecifier instanceSpec,
            MethodCallProcessingMode mode = MethodCallProcessingMode::kEvent ) noexcept
            : ::lap::com::SkeletonBase( ::std::move( instanceSpec ), mode )
        {}

        /**
         * @brief Destructor — auto-stops offering [SWS_CM_11549]
         */
        ~HelloWorldSkeleton() noexcept override {
            if ( IsOffered() ) {
                StopOfferService();
            }
        }

        // Move-only [SWS_CM_11547, SWS_CM_11545]
        HelloWorldSkeleton( HelloWorldSkeleton&& ) noexcept = default;
        HelloWorldSkeleton& operator=( HelloWorldSkeleton&& ) noexcept = default;

        // Non-copyable [SWS_CM_11546, SWS_CM_11544]
        HelloWorldSkeleton( const HelloWorldSkeleton& ) = delete;
        HelloWorldSkeleton& operator=( const HelloWorldSkeleton& ) = delete;

        // ==================== Events [SWS_CM_99557] ====================
        events::Greeting greeting;
        events::StatusChanged statusChanged;
        events::DataStream dataStream;

        // ==================== Methods ====================
        methods::SayHello sayHello;
        methods::Add add;
        methods::NotifyLog notifyLog;  ///< (fire-and-forget)
        methods::ComputeHash computeHash;

        // ==================== Fields [SWS_CM_99558] ====================
        fields::VisitorCount visitorCount{ true, false, false };  ///< @note readonly
        fields::ServerName serverName{ true, true, false };
        fields::Temperature temperature{ true, true, true };

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
            PropagateBindingContext( greeting, subCtx );
            subCtx.elementId = 2;
            PropagateBindingContext( statusChanged, subCtx );
            subCtx.elementId = 3;
            PropagateBindingContext( dataStream, subCtx );
            subCtx.elementId = 0x0100;
            PropagateBindingContext( sayHello, subCtx );
            subCtx.elementId = 0x0101;
            PropagateBindingContext( add, subCtx );
            subCtx.elementId = 0x0102;
            PropagateBindingContext( notifyLog, subCtx );
            subCtx.elementId = 0x0103;
            PropagateBindingContext( computeHash, subCtx );
            subCtx.elementId = 0x0200;
            PropagateBindingContext( visitorCount, subCtx );
            subCtx.elementId = 0x0201;
            PropagateBindingContext( serverName, subCtx );
            subCtx.elementId = 0x0202;
            PropagateBindingContext( temperature, subCtx );
        }
    };

} // namespace skeleton

} // namespace examples

#endif // EXAMPLES_HELLOWORLDSKELETON_HPP
