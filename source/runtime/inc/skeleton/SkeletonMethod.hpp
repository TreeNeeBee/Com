/**
 * @file        SkeletonMethod.hpp
 * @author      Aii
 * @brief       AUTOSAR Adaptive Platform Skeleton-Side Method Communication
 * @date        2026/02/07
 * @details     Skeleton-side method handler: request/response and fire-and-forget
 *              (SWS_CM Section 9.4). Split from Method.hpp following SRP.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>Split from Method.hpp (SRP refactoring)
 * </table>
 */
#ifndef LAP_COM_SKELETON_METHOD_HPP
#define LAP_COM_SKELETON_METHOD_HPP

// ==================== Project-Internal Headers ====================
#include "ComTypes.hpp"
#include "CBindingContext.hpp"
#include "MethodTraits.hpp"
#include "serialization/CBinarySerializer.hpp"
#include "serialization/CBinaryDeserializer.hpp"
#include "serialization/CSerializationTraits.hpp"
#include "binding/common/ITransportBinding.hpp"

// ==================== Cross-Module Headers ====================
#include <core/CResult.hpp>
#include <core/CFuture.hpp>
#include <core/CSpan.hpp>
#include <core/CSync.hpp>

// ==================== Standard Library Headers ====================
#include <functional>
#include <mutex>
#include <tuple>

class ProxySkeletonTestAccessor;

namespace lap
{
namespace com
{
    // ========================================================================
    // Type Aliases (prefer lap::core project types)
    // ========================================================================
    using lap::core::Bool;

    // Forward declarations
    class SkeletonBase;

    /**
     * @brief Skeleton-side method for handling remote calls
     * @tparam Output Return type of the method
     * @tparam Args   Argument types for the method
     * @note SWS_CM_00820 - Method request handling
     */
    template< typename Output, typename... Args >
    class SkeletonMethod
    {
    public:
        using HandlerType = Function< lap::core::Future< Output > ( Args... ) >;

        /**
         * @brief Constructor
         * @note SWS_CM_00821
         */
        SkeletonMethod() noexcept = default;

        /**
         * @brief Destructor
         * @note SWS_CM_00822
         */
        ~SkeletonMethod() noexcept = default;

        /**
         * @brief Register method implementation handler (Strategy pattern)
         * @param handler Function to handle method calls
         * @return Result indicating success or error
         * @note SWS_CM_00823
         */
        Result< void > RegisterMethodHandler( HandlerType handler ) noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );

            if ( m_handler )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kFieldSetHandlerNotSet, 0 ) );
            }

            m_handler = std::move( handler );

            // Wire to binding if context is already set
            if ( m_bindingContext.IsValid() )
            {
                registerWithBinding();
            }

            return Result< void >::FromValue();
        }

        /**
         * @brief Unregister method handler
         * @note SWS_CM_00824
         */
        void UnregisterMethodHandler() noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );
            m_handler = nullptr;

            // Unregister from binding by passing empty callback
            if ( m_bindingContext.IsValid() )
            {
                m_bindingContext.pBinding->template RegisterMethod< binding::ByteBuffer, binding::ByteBuffer >(
                    m_bindingContext.serviceId,
                    m_bindingContext.instanceId,
                    m_bindingContext.elementId,
                    nullptr );
            }
        }

        /**
         * @brief Check if handler is registered
         * @return true if handler is set, false otherwise
         * @note SWS_CM_00825
         */
        Bool HasHandler() const noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );
            return ( m_handler != nullptr );
        }

        // Move-only type (AUTOSAR C++ A12-8-6)
        SkeletonMethod( SkeletonMethod&& ) noexcept = default;
        SkeletonMethod& operator=( SkeletonMethod&& ) noexcept = default;
        SkeletonMethod( const SkeletonMethod& )            = delete;
        SkeletonMethod& operator=( const SkeletonMethod& ) = delete;

    private:
        mutable UniqueHandle< Mutex > m_pMutex{ MakeUnique< Mutex >() };
        HandlerType        m_handler{ nullptr };
        CBindingContext    m_bindingContext;     ///< Transport binding context

        /**
         * @brief Internal: Process incoming method call
         * @param args Deserialized arguments from request
         * @return Future containing method result
         */
        lap::core::Future< Output > processCall( Args... args ) noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );

            if ( !m_handler )
            {
                lap::core::Promise< Output > promise;
                promise.SetError( MakeErrorCode( ComErrc::kFieldSetHandlerNotSet, 0 ) );
                return promise.GetFuture();
            }

            auto result = m_handler( std::forward< Args > ( args )... );
            return result;
        }

        /**
         * @brief Internal: Set binding context (called by SkeletonBase)
         * @param context Binding context with transport, service/instance/element IDs
         */
        void setBindingContext( const CBindingContext& context ) noexcept
        {
            m_bindingContext = context;

            // Wire to binding if handler is already registered
            if ( m_handler && m_bindingContext.IsValid() )
            {
                registerWithBinding();
            }
        }

        /**
         * @brief Internal: Register the method callback with the transport binding
         *
         * @details Phase 3 direct path (detail::kMethodDirectPath == true):
         *            - Registers RegisterMethod<ArgsTuple, Output> — zero serialization
         *            - Handler receives native ArgsTuple, returns native Output
         *          Phase 1 ByteBuffer path (fallback):
         *            - Registers RegisterMethod<ByteBuffer, ByteBuffer>
         *            - Deserializes request → Args, serializes Output → response
         *
         * @pre m_handler is set AND m_bindingContext is valid
         * @note Caller must hold m_mutex (or be in setBindingContext which
         *       is called from SkeletonBase under its own lock)
         */
        void registerWithBinding() noexcept
        {
            // Capture a raw pointer to this for the lambda.
            // Lifetime is safe: SkeletonBase owns both the method and the binding context.
            // When skeleton is destroyed, UnregisterMethodHandler or binding Shutdown
            // ensures no dangling callbacks remain.
            auto* self = this;
            const auto svcId  = m_bindingContext.serviceId;
            const auto instId = m_bindingContext.instanceId;
            const auto methId = m_bindingContext.elementId;

            if constexpr ( detail::kMethodDirectPath< Output, Args... > )
            {
                // Phase 3: Direct typed path — all types trivially copyable.
                // RegisterMethod<ArgsTuple, Output> passes native types through
                // the binding with no serialization overhead.
                using ArgsTuple = std::tuple< std::decay_t< Args >... >;

                auto callback =
                    [self]( lap::core::UInt64 /*serviceId*/,
                            lap::core::UInt64 /*instanceId*/,
                            lap::core::UInt32 /*methodId*/,
                            const ArgsTuple& request ) -> Output
                    {
                        return self->handleIncomingCallTyped(
                            request, std::index_sequence_for< Args... >{} );
                    };

                m_bindingContext.pBinding->template RegisterMethod< ArgsTuple, Output >(
                    svcId, instId, methId, std::move( callback ) );
            }
            else
            {
                // Phase 1: ByteBuffer-based serialization bridge
                auto callback =
                    [self]( lap::core::UInt64 /*serviceId*/,
                            lap::core::UInt64 /*instanceId*/,
                            lap::core::UInt32 /*methodId*/,
                            const binding::ByteBuffer& request ) -> binding::ByteBuffer
                    {
                        return self->HandleIncomingCall( request );
                    };

                m_bindingContext.pBinding->template RegisterMethod<
                    binding::ByteBuffer, binding::ByteBuffer >(
                    svcId, instId, methId, std::move( callback ) );
            }
        }

        /**
         * @brief Deserialize request → call handler → serialize response
         * @param request Serialized request bytes
         * @return Serialized response bytes (empty on error)
         */
        binding::ByteBuffer HandleIncomingCall(
            const binding::ByteBuffer& request ) noexcept
        {
            // Deserialize arguments
            auto args = DeserializeArgs( request );
            if ( !args.HasValue() )
            {
                return binding::ByteBuffer{};
            }

            // Extract into mutable local (Value() returns const T&)
            auto argsTuple = std::move( args ).Value();

            // Call handler with unpacked arguments
            auto future = CallWithTuple(
                argsTuple,
                std::index_sequence_for< Args... > {} );

            // Block for result (skeleton methods are dispatched synchronously
            // by the binding's I/O thread)
            auto result = future.GetResult();
            if ( !result.HasValue() )
            {
                return binding::ByteBuffer{};
            }

            // Serialize output
            auto responseBytes = serializeOutput( result.Value() );
            if ( !responseBytes.HasValue() )
            {
                return binding::ByteBuffer{};
            }

            return std::move( responseBytes ).Value();
        }

        /**
         * @brief Deserialize ByteBuffer into a tuple of Args
         * @param data Serialized request data
         * @return Result containing tuple of deserialized arguments
         */
        Result< std::tuple< typename std::decay< Args >::type... > >
        DeserializeArgs( const binding::ByteBuffer& data ) noexcept
        {
            using ArgsTuple = std::tuple< typename std::decay< Args >::type... >;

            auto span = lap::core::MakeSpan(
                reinterpret_cast< const lap::core::UInt8* > ( data.data() ),
                data.size() );
            serialization::CBinaryDeserializer deserializer( span );

            ArgsTuple argsTuple{};
            Bool success = true;

            deserializeEach( deserializer, argsTuple, success,
                std::index_sequence_for< Args... > {} );

            if ( !success )
            {
                return Result< ArgsTuple >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }

            return Result< ArgsTuple >::FromValue( std::move( argsTuple ) );
        }

        /**
         * @brief Helper: deserialize each tuple element via index_sequence
         */
        template< typename Tuple, std::size_t... Is  >
        void deserializeEach( serialization::CBinaryDeserializer& deserializer,
                              Tuple& t, Bool& success,
                              std::index_sequence< Is... > ) noexcept
        {
            int dummy[] = { 0, ( [&]() {
                if ( success )
                {
                    auto r = serialization::DeserializeValue(
                        deserializer, std::get< Is > ( t ) );
                    if ( !r.HasValue() ) { success = false; }
                }
            }(), 0 )... };
            static_cast< void > ( dummy );
        }

        /**
         * @brief Invoke handler with tuple-unpacked arguments
         */
        template< std::size_t... Is >
        lap::core::Future< Output > CallWithTuple(
            std::tuple< typename std::decay< Args >::type... >& argsTuple,
            std::index_sequence< Is... > ) noexcept
        {
            return processCall( std::move( std::get< Is > ( argsTuple ) )... );
        }

        /**
         * @brief Serialize Output into ByteBuffer
         * @param output The method return value
         * @return Result containing serialized bytes
         */
        Result< binding::ByteBuffer > serializeOutput(
            const Output& output ) noexcept
        {
            serialization::CBinarySerializer serializer;

            auto r = serialization::SerializeValue( serializer, output );
            if ( !r.HasValue() )
            {
                return Result< binding::ByteBuffer >::FromError(
                    MakeErrorCode( ComErrc::kSerializationError, 0 ) );
            }

            auto data = serializer.GetData();
            return Result< binding::ByteBuffer >::FromValue(
                binding::ByteBuffer( data.data(), data.data() + data.size() ) );
        }

        /**
         * @brief Phase 3: Direct typed dispatch — unpack ArgsTuple, call handler,
         *        return Output. No serialization involved.
         * @pre   Only instantiated when detail::kMethodDirectPath<Output, Args...>
         *        is true (all types trivially copyable, at least one arg).
         */
        template< std::size_t... Is >
        Output handleIncomingCallTyped(
            const std::tuple< std::decay_t< Args >... >& req,
            std::index_sequence< Is... > ) noexcept
        {
            auto future = processCall( std::get< Is >( req )... );
            auto result = future.GetResult();
            if ( !result.HasValue() )
            {
                return Output{};
            }
            return std::move( result ).Value();
        }

        friend class SkeletonBase;
        friend class ::ProxySkeletonTestAccessor;
    };

    // ========================================================================
    // Fire-and-Forget Handler (SWS_CM_00850)
    // ========================================================================

    /**
     * @brief Skeleton-side fire-and-forget method handler
     * @tparam Args Argument types for the method
     * @note SWS_CM_00850
     */
    template< typename... Args >
    class SkeletonFireAndForgetMethod
    {
    public:
        using HandlerType = Function< void( Args... ) >;

        /**
         * @brief Constructor
         * @note SWS_CM_00851
         */
        SkeletonFireAndForgetMethod() noexcept = default;

        /**
         * @brief Destructor
         * @note SWS_CM_00852
         */
        ~SkeletonFireAndForgetMethod() noexcept = default;

        /**
         * @brief Register method handler (Strategy pattern)
         * @param handler Function to handle method calls
         * @return Result indicating success or error
         * @note SWS_CM_00853
         */
        Result< void > RegisterMethodHandler( HandlerType handler ) noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );

            if ( m_handler )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kFieldSetHandlerNotSet, 0 ) );
            }

            m_handler = std::move( handler );

            // Wire to binding if context is already set
            if ( m_bindingContext.IsValid() )
            {
                registerWithBinding();
            }

            return Result< void >::FromValue();
        }

        /**
         * @brief Unregister method handler
         * @note SWS_CM_00854
         */
        void UnregisterMethodHandler() noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );
            m_handler = nullptr;

            // Unregister from binding by passing null handler
            if ( m_bindingContext.IsValid() )
            {
                m_bindingContext.pBinding->template RegisterMethod<
                    binding::ByteBuffer, binding::ByteBuffer >(
                    m_bindingContext.serviceId,
                    m_bindingContext.instanceId,
                    m_bindingContext.elementId,
                    nullptr );
            }
        }

        // Move-only type (AUTOSAR C++ A12-8-6)
        SkeletonFireAndForgetMethod( SkeletonFireAndForgetMethod&& ) noexcept = default;
        SkeletonFireAndForgetMethod& operator=( SkeletonFireAndForgetMethod&& ) noexcept = default;
        SkeletonFireAndForgetMethod( const SkeletonFireAndForgetMethod& )            = delete;
        SkeletonFireAndForgetMethod& operator=( const SkeletonFireAndForgetMethod& ) = delete;

    private:
        mutable UniqueHandle< Mutex > m_pMutex{ MakeUnique< Mutex >() };
        HandlerType        m_handler{ nullptr };
        CBindingContext    m_bindingContext;     ///< Transport binding context

        /**
         * @brief Internal: Process incoming call
         * @param args Deserialized arguments
         */
        void processCall( Args... args ) noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );

            if ( m_handler )
            {
                m_handler( std::forward< Args > ( args )... );
            }
        }

        /**
         * @brief Internal: Set binding context (called by SkeletonBase)
         * @param context Binding context with transport, service/instance/element IDs
         */
        void setBindingContext( const CBindingContext& context ) noexcept
        {
            m_bindingContext = context;

            // Wire to binding if handler is already registered
            if ( m_handler && m_bindingContext.IsValid() )
            {
                registerWithBinding();
            }
        }

        /**
         * @brief Internal: Register fire-and-forget callback with the transport binding
         *
         * @details Phase 3 direct path (detail::kFnFDirectPath == true):
         *            - Registers RegisterMethod<ArgsTuple, UInt8> — zero serialization
         *            - UInt8 dummy return type (fire-and-forget: response ignored by proxy)
         *          Phase 1 ByteBuffer path (fallback):
         *            - Registers RegisterMethod<ByteBuffer, ByteBuffer>
         *            - Deserializes request, calls handler, returns empty ByteBuffer
         *
         * @pre m_handler is set AND m_bindingContext is valid
         */
        void registerWithBinding() noexcept
        {
            auto* self = this;
            const auto svcId  = m_bindingContext.serviceId;
            const auto instId = m_bindingContext.instanceId;
            const auto methId = m_bindingContext.elementId;

            if constexpr ( detail::kFnFDirectPath< Args... > )
            {
                // Phase 3: Direct typed path — all arg types trivially copyable.
                using ArgsTuple = std::tuple< std::decay_t< Args >... >;

                auto callback =
                    [self]( lap::core::UInt64 /*serviceId*/,
                            lap::core::UInt64 /*instanceId*/,
                            lap::core::UInt32 /*methodId*/,
                            const ArgsTuple& request ) -> lap::core::UInt8
                    {
                        self->handleIncomingCallTyped(
                            request, std::index_sequence_for< Args... >{} );
                        return lap::core::UInt8{ 0 };  // Fire-and-forget: dummy
                    };

                m_bindingContext.pBinding->template RegisterMethod<
                    ArgsTuple, lap::core::UInt8 >(
                    svcId, instId, methId, std::move( callback ) );
            }
            else
            {
                // Phase 1: ByteBuffer-based serialization bridge
                auto callback =
                    [self]( lap::core::UInt64 /*serviceId*/,
                            lap::core::UInt64 /*instanceId*/,
                            lap::core::UInt32 /*methodId*/,
                            const binding::ByteBuffer& request ) -> binding::ByteBuffer
                    {
                        self->HandleIncomingCall( request );
                        return binding::ByteBuffer{};  // Fire-and-forget: empty response
                    };

                m_bindingContext.pBinding->template RegisterMethod<
                    binding::ByteBuffer, binding::ByteBuffer >(
                    svcId, instId, methId, std::move( callback ) );
            }
        }

        /**
         * @brief Deserialize request → call handler (no response)
         * @param request Serialized request bytes
         */
        void HandleIncomingCall(
            const binding::ByteBuffer& request ) noexcept
        {
            auto args = DeserializeArgs( request );
            if ( !args.HasValue() )
            {
                return;
            }

            // Extract into mutable local (Value() returns const T&)
            auto argsTuple = std::move( args ).Value();

            CallWithTuple(
                argsTuple,
                std::index_sequence_for< Args... > {} );
        }

        /**
         * @brief Deserialize ByteBuffer into a tuple of Args
         * @param data Serialized request data
         * @return Result containing tuple of deserialized arguments
         */
        Result< std::tuple< typename std::decay< Args >::type... > >
        DeserializeArgs( const binding::ByteBuffer& data ) noexcept
        {
            using ArgsTuple = std::tuple< typename std::decay< Args >::type... >;

            auto span = lap::core::MakeSpan(
                reinterpret_cast< const lap::core::UInt8* > ( data.data() ),
                data.size() );
            serialization::CBinaryDeserializer deserializer( span );

            ArgsTuple argsTuple{};
            Bool success = true;

            deserializeEach( deserializer, argsTuple, success,
                std::index_sequence_for< Args... > {} );

            if ( !success )
            {
                return Result< ArgsTuple >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }

            return Result< ArgsTuple >::FromValue( std::move( argsTuple ) );
        }

        /**
         * @brief Helper: deserialize each tuple element via index_sequence
         */
        template< typename Tuple, std::size_t... Is  >
        void deserializeEach( serialization::CBinaryDeserializer& deserializer,
                              Tuple& t, Bool& success,
                              std::index_sequence< Is... > ) noexcept
        {
            int dummy[] = { 0, ( [&]() {
                if ( success )
                {
                    auto r = serialization::DeserializeValue(
                        deserializer, std::get< Is > ( t ) );
                    if ( !r.HasValue() ) { success = false; }
                }
            }(), 0 )... };
            static_cast< void > ( dummy );
        }

        /**
         * @brief Invoke handler with tuple-unpacked arguments
         */
        template< std::size_t... Is >
        void CallWithTuple(
            std::tuple< typename std::decay< Args >::type... >& argsTuple,
            std::index_sequence< Is... > ) noexcept
        {
            processCall( std::move( std::get< Is > ( argsTuple ) )... );
        }

        /**
         * @brief Phase 3: Direct typed dispatch (fire-and-forget variant) —
         *        unpack ArgsTuple, call handler. No return value.
         * @pre   Only instantiated when detail::kFnFDirectPath<Args...> is true.
         */
        template< std::size_t... Is >
        void handleIncomingCallTyped(
            const std::tuple< std::decay_t< Args >... >& req,
            std::index_sequence< Is... > ) noexcept
        {
            processCall( std::get< Is >( req )... );
        }

        friend class SkeletonBase;
        friend class ::ProxySkeletonTestAccessor;
    };

} // namespace com
} // namespace lap

#endif // LAP_COM_SKELETON_METHOD_HPP
