/**
 * @file        ITransportBinding.hpp
 * @author      LightAP Development Team
 * @brief       Strongly-typed transport binding interface for lap::com
 * @date        2025-11-21
 * @details     Abstract interface for all transport bindings (Core IPC, DDS, etc.)
 *              Uses NVI (Non-Virtual Interface) pattern: public template methods
 *              delegate to protected type-erased virtual Do* methods.
 *
 *              Generic ByteBuffer tunnel has been removed.  Each binding receives
 *              typed data via const void* and uses its own TypeRegistry (e.g.
 *              CDdsTypeRegistry / IDdsTypeAdapter) for native serialization.
 *
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R25-11 Compliance:
 *              - SWS_CM_00400: Transport Binding Interface
 *              - SWS_CM_00401: Binding Lifecycle Management
 * @reference   IMPLEMENTATION_PLAN_UPDATED.md Phase 2
 *              BINDING_ARCHITECTURE.md §2 / §5
 *              AUTOSAR_AP_SWS_CommunicationManagement.pdf §8.3
 * sdk:
 * platform:    Linux 5.10+
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/21  <td>1.0      <td>LightAP Team    <td>Initial transport binding interface
 * <tr><td>2026/02/24  <td>2.0      <td>LightAP Team    <td>Strong-typed NVI refactor: remove ByteBuffer tunnel
 * </table>
 */
#ifndef LAP_COM_BINDING_ITRANSPORT_BINDING_HPP
#define LAP_COM_BINDING_ITRANSPORT_BINDING_HPP

#include "BindingTypes.hpp"

#include <lap/core/CTypedef.hpp>
#include <lap/core/CResult.hpp>
#include <lap/core/COptional.hpp>
#include <lap/core/CFuture.hpp>
#include <lap/core/CPromise.hpp>
#include <lap/core/CFunction.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace lap
{
namespace com
{
namespace binding
{
    using lap::core::Result;
    using lap::core::ErrorCode;
    using lap::core::Optional;
    using lap::core::Future;
    using lap::core::Promise;
    using lap::core::Function;
    using lap::core::Bool;
    using lap::core::UInt32;
    using lap::core::UInt64;
    using lap::core::Byte;
    using lap::core::Size;
    using lap::core::Vector;

    /**
     * @brief Byte buffer type for raw data / legacy internal use
     * @note  No longer used on the ITransportBinding public API.
     *        Retained for utility code and backward compatibility.
     */
    using ByteBuffer = Vector< Byte >;

    /**
     * @brief Helper: Wire-size for the NVI template layer
     *
     * @details Returns sizeof(T) for plain types so that Do* implementations
     *          can use memcpy with the correct size.  Returns 0 for
     *          ByteBuffer (the Phase-1 transport container) so that Do*
     *          implementations fall through to the legacy ByteBuffer* path.
     */
    template< typename T >
    constexpr Size WireSize() noexcept
    {
        if constexpr ( ::std::is_same_v< ::std::decay_t< T >, ByteBuffer > )
        {
            return Size{ 0 };
        }
        else
        {
            return sizeof( T );
        }
    }

    // ====================================================================
    // Type-Erased Callback Types (binding implementations use these)
    // ====================================================================

    /**
     * @brief Event callback (type-erased)
     * @param serviceId  AUTOSAR service ID
     * @param instanceId AUTOSAR instance ID
     * @param eventId    Event identifier
     * @param pData      Pointer to typed event data (binding casts via TypeRegistry)
     */
    using EventCallback = Function< void(
        UInt64      serviceId,
        UInt64      instanceId,
        UInt32      eventId,
        const void* pData
    ) >;

    /**
     * @brief Method handler (type-erased)
     * @param serviceId  AUTOSAR service ID
     * @param instanceId AUTOSAR instance ID
     * @param methodId   Method identifier
     * @param pRequest   Pointer to typed request data
     * @param pResponse  Pointer to pre-allocated response (handler fills it)
     */
    using MethodHandler = Function< void(
        UInt64      serviceId,
        UInt64      instanceId,
        UInt32      methodId,
        const void* pRequest,
        void*       pResponse
    ) >;

    /**
     * @brief Service discovery callback for push-based notification
     * @param serviceId AUTOSAR service ID that was searched
     * @param instances Currently available instance IDs
     *
     * @note AUTOSAR SWS_CM_00001: StartFindService callback
     */
    using ServiceDiscoveryCallback = Function< void(
        UInt64              serviceId,
        Vector< UInt64 >    instances
    ) >;

    /**
     * @brief Field change notification callback (type-erased)
     * @param serviceId  AUTOSAR service ID
     * @param instanceId AUTOSAR instance ID
     * @param fieldId    Field identifier
     * @param pValue     Pointer to typed field value
     */
    using FieldNotificationCallback = Function< void(
        UInt64      serviceId,
        UInt64      instanceId,
        UInt32      fieldId,
        const void* pValue
    ) >;

    // ====================================================================
    // Typed Callback Aliases (callers use these via template API)
    // ====================================================================

    /**
     * @brief Strongly-typed event callback
     * @tparam T Event data type
     */
    template< typename T >
    using TypedEventCallback = Function< void(
        UInt64      serviceId,
        UInt64      instanceId,
        UInt32      eventId,
        const T&    data
    ) >;

    /**
     * @brief Strongly-typed method handler
     * @tparam TReq  Request type
     * @tparam TResp Response type
     */
    template< typename TReq, typename TResp >
    using TypedMethodHandler = Function< TResp(
        UInt64      serviceId,
        UInt64      instanceId,
        UInt32      methodId,
        const TReq& request
    ) >;

    /**
     * @brief Strongly-typed field notification callback
     * @tparam T Field value type
     */
    template< typename T >
    using TypedFieldCallback = Function< void(
        UInt64      serviceId,
        UInt64      instanceId,
        UInt32      fieldId,
        const T&    value
    ) >;

    /**
     * @brief Abstract transport binding interface (NVI pattern)
     *
     * @details Public API uses template methods for compile-time type safety.
     *          Binding plugins override protected Do* virtual methods with
     *          type-erased (const void*) parameters.  The service + element IDs
     *          passed alongside the data pointer allow each binding to look up
     *          the concrete type via its own TypeRegistry.
     *
     *          The former ByteBuffer generic tunnel has been removed.  Each
     *          binding is responsible for native serialization of the typed
     *          data (e.g. FastDDS auto-CDR via CDdsTypeRegistry / IDdsTypeAdapter).
     *
     * @par Serialization Policy
     *          Do* implementations MUST follow this precedence when handling
     *          incoming void* data:
     *          1. **Type Registry lookup** — Use (serviceId, elementId) to find
     *             a registered type adapter (e.g. CDdsTypeRegistry::FindAdapter,
     *             CoreIPCTypeMap).  If an adapter exists, use it for serialization.
     *          2. **memcpy fallback** — If no adapter is registered, treat the
     *             void* as raw bytes and use memcpy with the provided dataSize.
     *             The dataSize parameter (WireSize<T>()) is passed from the
     *             template layer for this purpose.  When T is ByteBuffer,
     *             WireSize returns 0, indicating Phase-1 ByteBuffer passthrough.
     *
     * @note Thread-safety: Implementations must be thread-safe for
     *       concurrent OfferService/FindService/Send operations.
     *
     * @example Plugin implementation:
     *          extern "C" {
     *              ITransportBinding* CreateBindingInstance() {
     *                  return new MyBinding();
     *              }
     *
     *              void DestroyBindingInstance(ITransportBinding* instance) {
     *                  delete instance;
     *              }
     *          }
     */
    class ITransportBinding
    {
    public:
        /**
         * @brief Virtual destructor
         */
        virtual ~ITransportBinding() = default;

        // ====================================================================
        // Lifecycle Management
        // ====================================================================

        /**
         * @brief Initialize binding with configuration
         * @return Result<void> Success or error code
         *
         * @note Called once after binding is loaded
         * @note Must be idempotent (safe to call multiple times)
         */
        virtual Result< void > Initialize() noexcept = 0;

        /**
         * @brief Configure binding with key-value parameters
         * @param params  Binding-specific parameters from YAML config
         *
         * @details Called by BindingManager between construction and
         *          Initialize().  Subclasses may override to read
         *          parameters such as "discovery_server", "domain_id",
         *          "interface", etc.  Default implementation is a no-op.
         *
         * @note AUTOSAR SWS_CM_00401: Binding Lifecycle Management
         */
        virtual void Configure(
            const ::std::map< ::std::string, ::std::string >& params
                [[maybe_unused]] ) noexcept {}

        /**
         * @brief Shutdown binding and release resources
         * @return Result<void> Success or error code
         *
         * @note Called before unloading binding
         * @note Must cleanup all offered/subscribed services
         */
        virtual Result< void > Shutdown() noexcept = 0;

        // ====================================================================
        // Binding Capabilities
        // ====================================================================

        /**
         * @brief Query whether this binding supports typed DDS adapters
         * @return true if the binding uses CDdsTypeRegistry adapters for
         *         non-trivially-copyable types (e.g., DDS binding).
         *         false if the binding uses binary serialization fallback
         *         (e.g., CoreIPC, Socket bindings).
         *
         * @details When true, SkeletonEvent/ProxyEvent will pass non-trivially-
         *          copyable event data directly (typed pointer + sizeof(T)),
         *          relying on a registered IDdsTypeAdapter to perform CDR
         *          serialization.  When false, the runtime serializes to
         *          ByteBuffer before calling the binding.
         */
        virtual Bool SupportsTypedAdapters() const noexcept { return false; }

        /**
         * @brief Check if a typed adapter is registered for a specific event
         * @param serviceId  AUTOSAR service ID
         * @param eventId    Event identifier
         * @return true if a typed adapter exists for this (service, event) pair
         *
         * @details SkeletonEvent::doSend() and ProxyEvent::doSubscribe() use
         *          this to decide between the typed-adapter path (direct pointer)
         *          and the serialization/ByteBuffer fallback.  A binding may
         *          advertise SupportsTypedAdapters() == true at the class level,
         *          but individual events (e.g., field notifications) may not
         *          have an adapter registered — those must use serialization.
         */
        virtual Bool HasEventAdapter(
            UInt64 serviceId, UInt32 eventId ) const noexcept
        {
            (void)serviceId; (void)eventId;
            return false;
        }

        // ====================================================================
        // Service Management (Provider Side)
        // ====================================================================

        /**
         * @brief Offer a service instance
         * @param serviceId  AUTOSAR service ID
         * @param instanceId AUTOSAR instance ID
         * @return Result<void> Success or error code
         *
         * @note AUTOSAR SWS_CM_00002: OfferService
         * @note Makes service discoverable to consumers
         */
        virtual Result< void > OfferService(
            UInt64 serviceId,
            UInt64 instanceId
        ) noexcept = 0;

        /**
         * @brief Stop offering a service instance
         * @param serviceId  AUTOSAR service ID
         * @param instanceId AUTOSAR instance ID
         * @return Result<void> Success or error code
         *
         * @note AUTOSAR SWS_CM_00003: StopOfferService
         */
        virtual Result< void > StopOfferService(
            UInt64 serviceId,
            UInt64 instanceId
        ) noexcept = 0;

        // ====================================================================
        // Service Discovery (Consumer Side)
        // ====================================================================

        /**
         * @brief Find available service instances
         * @param serviceId AUTOSAR service ID
         * @return Result<Vector<UInt64>> List of available instance IDs
         *
         * @note AUTOSAR SWS_CM_00001: FindService
         * @note Returns all instances currently offered
         */
        virtual Result< Vector< UInt64 > > FindService(
            UInt64 serviceId
        ) noexcept = 0;

        /**
         * @brief Start push-based service discovery
         * @param serviceId AUTOSAR service ID to monitor
         * @param callback  Callback invoked when service availability changes
         * @return Result<UInt64> Discovery handle for cancellation, or error
         *
         * @note AUTOSAR SWS_CM_00001: StartFindService (push mode)
         * @note Callback fires immediately with current state, then on changes
         * @note Use StopFindService() with returned handle to cancel
         */
        virtual Result< UInt64 > StartFindService(
            UInt64 serviceId,
            ServiceDiscoveryCallback callback
        ) noexcept = 0;

        /**
         * @brief Stop push-based service discovery
         * @param handle Discovery handle returned by StartFindService()
         * @return Result<void> Success or error code
         *
         * @note AUTOSAR SWS_CM_00002: StopFindService
         */
        virtual Result< void > StopFindService(
            UInt64 handle
        ) noexcept = 0;

        // ====================================================================
        // Event Communication — Typed Public API
        // ====================================================================

        /**
         * @brief Eagerly prepare event channel (topic + writer)
         * @param serviceId  AUTOSAR service ID
         * @param instanceId AUTOSAR instance ID
         * @param eventId    Event identifier
         * @return Result<void> Success or error code
         *
         * @note Must be called from outside DDS callback context.
         *       Prevents deadlock when SendEvent is later called from
         *       within a DDS listener (e.g. method handler calling field Update).
         */
        Result< void > PrepareEventChannel(
            UInt64 serviceId,
            UInt64 instanceId,
            UInt32 eventId ) noexcept
        {
            return DoPrepareEventChannel( serviceId, instanceId, eventId );
        }

        /**
         * @brief Send typed event to subscribers
         * @tparam T Event data type
         * @param serviceId  AUTOSAR service ID
         * @param instanceId AUTOSAR instance ID
         * @param eventId    Event identifier
         * @param data       Event data (strongly-typed)
         * @return Result<void> Success or error code
         *
         * @note AUTOSAR SWS_CM_00103: Send event
         * @note Called by service provider (SkeletonEvent)
         */
        template< typename T >
        Result< void > SendEvent(
            UInt64      serviceId,
            UInt64      instanceId,
            UInt32      eventId,
            const T&    data ) noexcept
        {
            return DoSendEvent( serviceId, instanceId, eventId,
                                static_cast< const void* >( &data ),
                                WireSize< T >() );
        }

        /**
         * @brief Subscribe to typed service events
         * @tparam T Event data type
         * @param serviceId  AUTOSAR service ID
         * @param instanceId AUTOSAR instance ID
         * @param eventId    Event identifier
         * @param callback   Typed event notification callback
         * @return Result<void> Success or error code
         *
         * @note AUTOSAR SWS_CM_00141: Subscribe
         * @note Called by service consumer (ProxyEvent)
         */
        template< typename T >
        Result< void > SubscribeEvent(
            UInt64                      serviceId,
            UInt64                      instanceId,
            UInt32                      eventId,
            TypedEventCallback< T >     callback ) noexcept
        {
            auto erased = [ cb = ::std::move( callback ) ](
                UInt64 sid, UInt64 iid, UInt32 eid, const void* p )
            {
                cb( sid, iid, eid, *static_cast< const T* >( p ) );
            };
            return DoSubscribeEvent( serviceId, instanceId, eventId,
                                     ::std::move( erased ),
                                     WireSize< T >() );
        }

        /**
         * @brief Unsubscribe from service events
         * @param serviceId  AUTOSAR service ID
         * @param instanceId AUTOSAR instance ID
         * @param eventId    Event identifier
         * @return Result<void> Success or error code
         *
         * @note AUTOSAR SWS_CM_00151: Unsubscribe
         */
        virtual Result< void > UnsubscribeEvent(
            UInt64 serviceId,
            UInt64 instanceId,
            UInt32 eventId
        ) noexcept = 0;

        // ====================================================================
        // Method Communication — Typed Public API
        // ====================================================================

        /**
         * @brief Synchronous typed method call
         * @tparam TResp Response type (must be default-constructible)
         * @tparam TReq  Request type
         * @param serviceId  AUTOSAR service ID
         * @param instanceId AUTOSAR instance ID
         * @param methodId   Method identifier
         * @param request    Typed request data
         * @return Result<TResp> Typed response or error
         *
         * @note AUTOSAR SWS_CM_00191: Method call
         * @note Called by service consumer (ProxyMethod)
         * @note Blocks until response received or timeout
         */
        template< typename TResp, typename TReq >
        Result< TResp > CallMethod(
            UInt64      serviceId,
            UInt64      instanceId,
            UInt32      methodId,
            const TReq& request ) noexcept
        {
            TResp response{};
            auto result = DoCallMethod( serviceId, instanceId, methodId,
                                        static_cast< const void* >( &request ),
                                        static_cast< void* >( &response ),
                                        WireSize< TReq >(),
                                        WireSize< TResp >() );
            if ( !result )
            {
                return Result< TResp >::FromError( ::std::move( result ).Error() );
            }
            return Result< TResp >( ::std::move( response ) );
        }

        /**
         * @brief Asynchronous typed method call
         * @tparam TResp Response type (must be default-constructible)
         * @tparam TReq  Request type (must be copy-constructible)
         * @param serviceId  AUTOSAR service ID
         * @param instanceId AUTOSAR instance ID
         * @param methodId   Method identifier
         * @param request    Typed request data (copied for async execution)
         * @return Future<TResp> Future holding the typed response
         *
         * @note AUTOSAR SWS_CM_00192: Asynchronous method call
         * @note Default implementation wraps synchronous CallMethod in a
         *       detached thread.  Bindings with native async support should
         *       override DoCallMethod to be internally non-blocking.
         */
        template< typename TResp, typename TReq >
        Future< TResp > CallMethodAsync(
            UInt64      serviceId,
            UInt64      instanceId,
            UInt32      methodId,
            const TReq& request ) noexcept
        {
            auto pPromise = ::std::make_shared< Promise< TResp > >();
            auto future   = pPromise->GetFuture();

            ::std::thread( [ this, pPromise, serviceId, instanceId, methodId,
                             req = request ]() mutable
            {
                auto result = this->template CallMethod< TResp >(
                    serviceId, instanceId, methodId, req );
                pPromise->SetResult( ::std::move( result ) );
            } ).detach();

            return future;
        }

        /**
         * @brief Register typed method handler (provider side)
         * @tparam TReq  Request type
         * @tparam TResp Response type
         * @param serviceId  AUTOSAR service ID
         * @param instanceId AUTOSAR instance ID
         * @param methodId   Method identifier
         * @param handler    Typed method implementation (nullptr to unregister)
         * @return Result<void> Success or error code
         *
         * @note Called by service provider (SkeletonMethod)
         * @note Passing a null handler unregisters the method
         */
        template< typename TReq, typename TResp >
        Result< void > RegisterMethod(
            UInt64                                  serviceId,
            UInt64                                  instanceId,
            UInt32                                  methodId,
            TypedMethodHandler< TReq, TResp >       handler ) noexcept
        {
            // Null handler = unregister the method
            if ( !handler )
            {
                return DoRegisterMethod( serviceId, instanceId, methodId,
                                         MethodHandler{ nullptr },
                                         WireSize< TReq >(),
                                         WireSize< TResp >() );
            }

            auto erased = [ h = ::std::move( handler ) ](
                UInt64 sid, UInt64 iid, UInt32 mid,
                const void* pReq, void* pResp )
            {
                auto resp = h( sid, iid, mid,
                               *static_cast< const TReq* >( pReq ) );
                *static_cast< TResp* >( pResp ) = ::std::move( resp );
            };
            return DoRegisterMethod( serviceId, instanceId, methodId,
                                     ::std::move( erased ),
                                     WireSize< TReq >(),
                                     WireSize< TResp >() );
        }

        // ====================================================================
        // Field Communication — Typed Public API
        // ====================================================================

        /**
         * @brief Get typed field value
         * @tparam T Field value type (must be default-constructible)
         * @param serviceId  AUTOSAR service ID
         * @param instanceId AUTOSAR instance ID
         * @param fieldId    Field identifier
         * @return Result<T> Typed field value or error
         *
         * @note AUTOSAR SWS_CM_00120: Get field
         */
        template< typename T >
        Result< T > GetField(
            UInt64 serviceId,
            UInt64 instanceId,
            UInt32 fieldId ) noexcept
        {
            T value{};
            auto result = DoGetField( serviceId, instanceId, fieldId,
                                      static_cast< void* >( &value ),
                                      WireSize< T >() );
            if ( !result )
            {
                return Result< T >::FromError( ::std::move( result ).Error() );
            }
            return Result< T >( ::std::move( value ) );
        }

        /**
         * @brief Set typed field value
         * @tparam T Field value type
         * @param serviceId  AUTOSAR service ID
         * @param instanceId AUTOSAR instance ID
         * @param fieldId    Field identifier
         * @param value      Typed field value
         * @return Result<void> Success or error code
         *
         * @note AUTOSAR SWS_CM_00121: Set field
         */
        template< typename T >
        Result< void > SetField(
            UInt64      serviceId,
            UInt64      instanceId,
            UInt32      fieldId,
            const T&    value ) noexcept
        {
            return DoSetField( serviceId, instanceId, fieldId,
                               static_cast< const void* >( &value ),
                               WireSize< T >() );
        }

        /**
         * @brief Subscribe to typed field change notifications
         * @tparam T Field value type
         * @param serviceId  AUTOSAR service ID
         * @param instanceId AUTOSAR instance ID
         * @param fieldId    Field identifier
         * @param callback   Typed notification callback
         * @return Result<void> Success or error code
         *
         * @note AUTOSAR SWS_CM_00122: Field notification subscription
         */
        template< typename T >
        Result< void > SubscribeFieldNotification(
            UInt64                      serviceId,
            UInt64                      instanceId,
            UInt32                      fieldId,
            TypedFieldCallback< T >     callback ) noexcept
        {
            auto erased = [ cb = ::std::move( callback ) ](
                UInt64 sid, UInt64 iid, UInt32 fid, const void* p )
            {
                cb( sid, iid, fid, *static_cast< const T* >( p ) );
            };
            return DoSubscribeFieldNotification( serviceId, instanceId, fieldId,
                                                 ::std::move( erased ),
                                                 WireSize< T >() );
        }

        /**
         * @brief Unsubscribe from field change notifications
         * @param serviceId  AUTOSAR service ID
         * @param instanceId AUTOSAR instance ID
         * @param fieldId    Field identifier
         * @return Result<void> Success or error code
         *
         * @note AUTOSAR SWS_CM_00123: Field notification unsubscription
         */
        virtual Result< void > UnsubscribeFieldNotification(
            UInt64 serviceId,
            UInt64 instanceId,
            UInt32 fieldId
        ) noexcept = 0;

        // ====================================================================
        // Diagnostics and Monitoring
        // ====================================================================

        /**
         * @brief Get binding name (for debugging)
         * @return Binding name string
         */
        virtual const char* GetName() const noexcept = 0;

        /**
         * @brief Get binding version
         * @return Version as uint32_t (e.g., 0x00010000 for 1.0.0)
         */
        virtual UInt32 GetVersion() const noexcept = 0;

        // ====================================================================
        // Performance and Capability Queries
        // ====================================================================

        /**
         * @brief Get binding priority for selection algorithm
         * @return Priority value (higher = preferred)
         *
         * @note Priority scale:
         *       - 100: Core IPC (zero-copy shared memory)
         *       - 80:  DDS (strong-typed, cross-ECU)
         *       - 60:  SOME/IP (automotive standard)
         *       - 40:  Socket (fallback)
         *       - 20:  D-Bus (legacy)
         */
        virtual UInt32 GetPriority() const noexcept = 0;

        /**
         * @brief Check if binding supports zero-copy communication
         * @return true if zero-copy capable (e.g. Core IPC)
         *
         * @note Used by BindingSelector for optimization decisions
         */
        virtual Bool SupportsZeroCopy() const noexcept = 0;

        /**
         * @brief Check if binding can handle a specific service
         * @param serviceId AUTOSAR service ID
         * @return true if binding supports this service
         *
         * @note Used by BindingManager::SelectBestBinding()
         * @example Core IPC may only support local services;
         *          DDS supports cross-ECU services
         */
        virtual Bool SupportsService( UInt64 serviceId ) const noexcept = 0;

        /**
         * @brief Get transport performance metrics
         * @return TransportMetrics Performance statistics
         *
         * @note Metrics structure defined in BindingTypes.hpp
         */
        virtual TransportMetrics GetMetrics() const noexcept = 0;

    protected:
        // ====================================================================
        // Virtual Implementation — binding plugins override these
        // ====================================================================
        //
        // Data parameters use const void* (input) and void* (output).
        // The binding resolves the concrete type via (serviceId, elementId)
        // using its TypeRegistry (e.g. CDdsTypeRegistry, CoreIPCTypeMap).
        // ====================================================================

        /** @brief Send event data (type-erased)
         *  @param pData  Pointer to the caller's typed object
         *  @note  Binding looks up writer via composite key
         *         (serviceId, instanceId, eventId):
         *         auto it = writer_map_.find({serviceId, instanceId, eventId});
         *         it->second->Write(pData);  // CDR serialization inside
         */
        virtual Result< void > DoSendEvent(
            UInt64      serviceId,
            UInt64      instanceId,
            UInt32      eventId,
            const void* pData,
            Size        dataSize = 0 ) noexcept = 0;

        /** @brief Eagerly prepare event channel (topic + writer)
         *  @note  Default: no-op for bindings without deferred channel creation
         */
        virtual Result< void > DoPrepareEventChannel(
            UInt64 serviceId,
            UInt64 instanceId,
            UInt32 eventId ) noexcept
        {
            static_cast< void >( serviceId );
            static_cast< void >( instanceId );
            static_cast< void >( eventId );
            return Result< void >::FromValue();
        }

        /** @brief Subscribe to events (type-erased callback)
         *  @note  Binding creates reader via (serviceId, instanceId, eventId)
         */
        virtual Result< void > DoSubscribeEvent(
            UInt64          serviceId,
            UInt64          instanceId,
            UInt32          eventId,
            EventCallback   callback,
            Size            dataSize = 0 ) noexcept = 0;

        /** @brief Synchronous method call (type-erased)
         *  @param pRequest  Pointer to the caller's typed request object
         *  @param pResponse Pointer to a default-constructed response;
         *                   the binding fills it before returning
         *  @note  Binding looks up writer/reader via
         *         (serviceId, instanceId, methodId)
         */
        virtual Result< void > DoCallMethod(
            UInt64      serviceId,
            UInt64      instanceId,
            UInt32      methodId,
            const void* pRequest,
            void*       pResponse,
            Size        requestSize  = 0,
            Size        responseSize = 0 ) noexcept = 0;

        /** @brief Register method handler (type-erased)
         *  @note  Binding registers reader/writer via
         *         (serviceId, instanceId, methodId)
         */
        virtual Result< void > DoRegisterMethod(
            UInt64          serviceId,
            UInt64          instanceId,
            UInt32          methodId,
            MethodHandler   handler,
            Size            requestSize  = 0,
            Size            responseSize = 0 ) noexcept = 0;

        /** @brief Get field value (type-erased)
         *  @param pOutValue Pointer to a default-constructed T;
         *                   the binding fills it before returning
         *  @note  Binding looks up reader via (serviceId, instanceId, fieldId)
         */
        virtual Result< void > DoGetField(
            UInt64  serviceId,
            UInt64  instanceId,
            UInt32  fieldId,
            void*   pOutValue,
            Size    valueSize = 0 ) noexcept = 0;

        /** @brief Set field value (type-erased)
         *  @note  Binding looks up writer via (serviceId, instanceId, fieldId)
         */
        virtual Result< void > DoSetField(
            UInt64      serviceId,
            UInt64      instanceId,
            UInt32      fieldId,
            const void* pValue,
            Size        valueSize = 0 ) noexcept = 0;

        /** @brief Subscribe to field notifications (type-erased callback)
         *  @note  Binding creates reader via (serviceId, instanceId, fieldId)
         */
        virtual Result< void > DoSubscribeFieldNotification(
            UInt64                      serviceId,
            UInt64                      instanceId,
            UInt32                      fieldId,
            FieldNotificationCallback   callback,
            Size                        valueSize = 0 ) noexcept = 0;
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_BINDING_ITRANSPORT_BINDING_HPP
