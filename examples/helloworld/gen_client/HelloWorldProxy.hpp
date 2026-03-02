/**
 * @file        HelloWorldProxy.hpp
 * @author      Aii
 * @brief       Auto-generated service proxy for HelloWorld [SWS_CM_00004]
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

#ifndef EXAMPLES_HELLOWORLDPROXY_HPP
#define EXAMPLES_HELLOWORLDPROXY_HPP

// ==================== Project-Internal Headers ====================
#include "HelloWorldTypes.hpp"

// ==================== Runtime Headers ====================
#include "ComTypes.hpp"
#include "ProxyBase.hpp"
#include "ServiceHandleType.hpp"
#include "Runtime.hpp"
#include "BindingManager.hpp"
#include "proxy/ProxyEvent.hpp"
#include "proxy/ProxyMethod.hpp"
#include "proxy/ProxyField.hpp"

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


// [SWS_CM_01007] — proxy inner namespace
namespace proxy
{

// [SWS_CM_98447] — events sub-namespace
namespace events
{

    /**
     * @brief Greeting event [SWS_CM_00005]
     */
    class Greeting final : public ::lap::com::ProxyEvent< GreetingEvent > {
    public:
        using SampleType = GreetingEvent;
        using ::lap::com::ProxyEvent< GreetingEvent >::ProxyEvent;
    };

    /**
     * @brief StatusChanged event [SWS_CM_00005]
     */
    class StatusChanged final : public ::lap::com::ProxyEvent< StatusChangedEvent > {
    public:
        using SampleType = StatusChangedEvent;
        using ::lap::com::ProxyEvent< StatusChangedEvent >::ProxyEvent;
    };

    /**
     * @brief DataStream event [SWS_CM_00005]
     */
    class DataStream final : public ::lap::com::ProxyEvent< DataStreamEvent > {
    public:
        using SampleType = DataStreamEvent;
        using ::lap::com::ProxyEvent< DataStreamEvent >::ProxyEvent;
    };

} // namespace events

// [SWS_CM_01015] — methods sub-namespace
namespace methods
{

    /**
     * @brief SayHello method [SWS_CM_00191]
     */
    class SayHello final : public ::lap::com::ProxyMethod< String, String > {
    public:
        using ::lap::com::ProxyMethod< String, String >::ProxyMethod;
    };

    /**
     * @brief Add method [SWS_CM_00191]
     */
    class Add final : public ::lap::com::ProxyMethod< UInt32, UInt32, UInt32 > {
    public:
        using ::lap::com::ProxyMethod< UInt32, UInt32, UInt32 >::ProxyMethod;
    };

    /**
     * @brief NotifyLog method [SWS_CM_00191]
     */
    class NotifyLog final : public ::lap::com::ProxyFireAndForgetMethod< String > {
    public:
        using ::lap::com::ProxyFireAndForgetMethod< String >::ProxyFireAndForgetMethod;
    };

    /**
     * @brief ComputeHash method [SWS_CM_00191]
     */
    class ComputeHash final : public ::lap::com::ProxyMethod< UInt64, ::std::vector< UInt8 > > {
    public:
        using ::lap::com::ProxyMethod< UInt64, ::std::vector< UInt8 > >::ProxyMethod;
    };

} // namespace methods

// [SWS_CM_98444] — fields sub-namespace
namespace fields
{

    /**
     * @brief VisitorCount field [SWS_CM_00007]
     */
    class VisitorCount final : public ::lap::com::ProxyField< UInt32 > {
    public:
        using ::lap::com::ProxyField< UInt32 >::ProxyField;
    };

    /**
     * @brief ServerName field [SWS_CM_00007]
     */
    class ServerName final : public ::lap::com::ProxyField< String > {
    public:
        using ::lap::com::ProxyField< String >::ProxyField;
    };

    /**
     * @brief Temperature field [SWS_CM_00007]
     */
    class Temperature final : public ::lap::com::ProxyField< Double > {
    public:
        using ::lap::com::ProxyField< Double >::ProxyField;
    };

} // namespace fields

    /**
     * @brief Service proxy for HelloWorld [SWS_CM_00004]
     * @note Auto-generated — non-copyable, move-only, named constructor
     * @version 2.0.0
     */
    class HelloWorldProxy final : public ::lap::com::ProxyBase {
    public:
        using HandleType = ::lap::com::ServiceHandleType< HelloWorldProxy >;

        // ==================== Service Identification ====================
        static constexpr UInt16 kServiceId = 0x02e0;
        static constexpr const Char* kServiceName = "HelloWorld";
        static constexpr const Char* kSchemaHash  = "5a1b660e3ef06eb8";
        static constexpr UInt32 kVersionMajor = 2;
        static constexpr UInt32 kVersionMinor = 0;

        /**
         * @brief Named constructor — create proxy from service handle [SWS_CM_10438]
         * @param handle Service handle obtained from FindService
         * @return Result containing proxy instance or error
         */
        static Result< HelloWorldProxy > Create( const HandleType& handle ) noexcept {
            if ( !handle.IsValid() ) {
                return Result< HelloWorldProxy >::FromError(
                    MakeErrorCode( ComErrc::kServiceNotAvailable, 0 ) );
            }

            HelloWorldProxy proxy( handle );
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

            return Result< HelloWorldProxy >::FromValue( ::std::move( proxy ) );
        }

        ~HelloWorldProxy() noexcept override = default;

        // Move-only [SWS_CM_11554, SWS_CM_11552]
        HelloWorldProxy( HelloWorldProxy&& ) noexcept = default;
        HelloWorldProxy& operator=( HelloWorldProxy&& ) noexcept = default;

        // Non-copyable [SWS_CM_11553, SWS_CM_11551]
        HelloWorldProxy( const HelloWorldProxy& ) = delete;
        HelloWorldProxy& operator=( const HelloWorldProxy& ) = delete;

        /**
         * @brief Get the handle used to create this proxy [SWS_CM_10383]
         */
        HandleType GetHandle() const noexcept { return m_handle; }

        // ==================== Events [SWS_CM_99445] ====================
        events::Greeting greeting;  ///< Event ID 1
        events::StatusChanged statusChanged;  ///< Event ID 2
        events::DataStream dataStream;  ///< Event ID 3

        // ==================== Methods [SWS_CM_99447] ====================
        methods::SayHello sayHello;
        methods::Add add;
        methods::NotifyLog notifyLog;  ///< (fire-and-forget)
        methods::ComputeHash computeHash;

        // ==================== Fields [SWS_CM_99446] ====================
        fields::VisitorCount visitorCount{ true, false, false }; ///< @note readonly
        fields::ServerName serverName{ true, true, false };
        fields::Temperature temperature{ true, true, true };

    protected:
        /**
         * @brief Protected constructor (use Create() factory)
         */
        explicit HelloWorldProxy( const HandleType& handle ) noexcept
            : ::lap::com::ProxyBase()
            , m_handle( handle )
        {}

        /**
         * @brief Propagate binding context to all sub-components
         */
        void onBindingContextReady( const ::lap::com::CBindingContext& context ) noexcept override {
            ::lap::com::CBindingContext subCtx = context;

            subCtx.elementId = 1;  // Greeting
            PropagateBindingContext( greeting, subCtx );
            subCtx.elementId = 2;  // StatusChanged
            PropagateBindingContext( statusChanged, subCtx );
            subCtx.elementId = 3;  // DataStream
            PropagateBindingContext( dataStream, subCtx );
            subCtx.elementId = 0x0100;  // SayHello
            PropagateBindingContext( sayHello, subCtx );
            subCtx.elementId = 0x0101;  // Add
            PropagateBindingContext( add, subCtx );
            subCtx.elementId = 0x0102;  // NotifyLog
            PropagateBindingContext( notifyLog, subCtx );
            subCtx.elementId = 0x0103;  // ComputeHash
            PropagateBindingContext( computeHash, subCtx );
            subCtx.elementId = 0x0200;  // VisitorCount
            PropagateBindingContext( visitorCount, subCtx );
            subCtx.elementId = 0x0201;  // ServerName
            PropagateBindingContext( serverName, subCtx );
            subCtx.elementId = 0x0202;  // Temperature
            PropagateBindingContext( temperature, subCtx );
        }
    private:
        HandleType m_handle;
    };

} // namespace proxy

} // namespace examples

#endif // EXAMPLES_HELLOWORLDPROXY_HPP
