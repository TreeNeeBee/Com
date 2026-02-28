/**
 * @file        ProxyMethod.hpp
 * @author      Aii
 * @brief       AUTOSAR Adaptive Platform Proxy-Side Method Communication
 * @date        2026/02/07
 * @details     Proxy-side method invocation: request/response and fire-and-forget
 *              (SWS_CM Section 9.4). Split from Method.hpp following SRP.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>Split from Method.hpp (SRP refactoring)
 * </table>
 */
#ifndef LAP_COM_PROXY_METHOD_HPP
#define LAP_COM_PROXY_METHOD_HPP

// ==================== Project-Internal Headers ====================
#include "ComTypes.hpp"
#include "CBindingContext.hpp"
#include "MethodTraits.hpp"
#include "serialization/CBinarySerializer.hpp"
#include "serialization/CBinaryDeserializer.hpp"
#include "serialization/CSerializationTraits.hpp"

// ==================== Binding Headers ====================
#include "ITransportBinding.hpp"

// ==================== Cross-Module Headers ====================
#include <core/CResult.hpp>
#include <core/CFuture.hpp>
#include <core/CPromise.hpp>
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
    class ProxyBase;

    // ========================================================================
    // Request/Response Method (SWS_CM_00800)
    // ========================================================================

    /**
     * @brief Proxy-side method for calling remote functions
     * @tparam Output Return type of the method
     * @tparam Args   Argument types for the method
     * @note SWS_CM_00800 - Remote method invocation (request/response)
     */
    template< typename Output, typename... Args >
    class ProxyMethod
    {
    public:
        /**
         * @brief Constructor
         * @note SWS_CM_00801
         */
        ProxyMethod() noexcept = default;

        /**
         * @brief Destructor
         * @note SWS_CM_00802
         */
        ~ProxyMethod() noexcept = default;

        /**
         * @brief Call method synchronously (blocking)
         * @param args Method arguments
         * @return Result containing method output or error
         * @note SWS_CM_00803
         */
        Result< Output > operator()( Args... args ) noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );

            if ( !m_isConnected )
            {
                return Result< Output >::FromError(
                    MakeErrorCode( ComErrc::kServiceNotAvailable, 0 ) );
            }

            return doSyncCall( std::forward< Args > ( args )... );
        }

        /**
         * @brief Call method asynchronously (non-blocking)
         * @param args Method arguments
         * @return Future containing method output
         * @note SWS_CM_00804
         */
        lap::core::Future< Output > CallAsync( Args... args ) noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );

            if ( !m_isConnected )
            {
                lap::core::Promise< Output > promise;
                promise.SetError( MakeErrorCode( ComErrc::kServiceNotAvailable, 0 ) );
                return promise.GetFuture();
            }

            return doAsyncCall( std::forward< Args > ( args )... );
        }

        /**
         * @brief Check if method is connected to a service instance
         * @return true if connected, false otherwise
         * @note SWS_CM_00805
         */
        Bool IsConnected() const noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );
            return m_isConnected;
        }

        // Move-only type (AUTOSAR C++ A12-8-6)
        ProxyMethod( ProxyMethod&& ) noexcept = default;
        ProxyMethod& operator=( ProxyMethod&& ) noexcept = default;
        ProxyMethod( const ProxyMethod& )            = delete;
        ProxyMethod& operator=( const ProxyMethod& ) = delete;

    private:
        mutable UniqueHandle< Mutex > m_pMutex{ MakeUnique< Mutex >() };
        Bool               m_isConnected{ false };
        CBindingContext    m_bindingContext;     ///< Transport binding context

        /**
         * @brief Serialize variadic arguments into a ByteBuffer
         * @param args Method arguments
         * @return Result containing serialized buffer or error
         */
        Result< binding::ByteBuffer > serializeArgs( Args... args ) noexcept
        {
            serialization::CBinarySerializer serializer;
            Bool success = true;

            // Fold expression emulation for C++14/17: serialize each arg
            int dummy[] = { 0, ( [&]() {
                if ( success )
                {
                    auto r = serialization::SerializeValue( serializer, args );
                    if ( !r.HasValue() ) { success = false; }
                }
            }(), 0 )... };
            static_cast< void > ( dummy );

            if ( !success )
            {
                return Result< binding::ByteBuffer >::FromError(
                    MakeErrorCode( ComErrc::kSerializationError, 0 ) );
            }

            auto data = serializer.GetData();
            return Result< binding::ByteBuffer >::FromValue(
                binding::ByteBuffer( data.data(), data.data() + data.size() ) );
        }

        /**
         * @brief Deserialize response buffer into Output type
         * @param responseData Serialized response (envelope: UInt32 status + payload)
         * @return Result containing deserialized output, or error propagated from server
         *
         * @details Matches the envelope written by SkeletonMethod::HandleIncomingCall:
         *   [UInt32 status=0 → OK | ComErrc value → application error] [Output if OK]
         */
        Result< Output > deserializeResponse(
            const binding::ByteBuffer& responseData ) noexcept
        {
            if ( responseData.empty() )
            {
                return Result< Output >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }

            auto span = lap::core::MakeSpan(
                reinterpret_cast< const lap::core::UInt8* > ( responseData.data() ),
                responseData.size() );
            serialization::CBinaryDeserializer deserializer( span );

            // Read 4-byte status prefix
            lap::core::UInt32 status = 0u;
            auto statusResult = serialization::DeserializeValue< lap::core::UInt32 >(
                deserializer, status );
            if ( !statusResult.HasValue() )
            {
                return Result< Output >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }

            if ( status != 0u )
            {
                // Application error returned by the server handler
                return Result< Output >::FromError(
                    MakeErrorCode( static_cast< ComErrc >( status ), 0 ) );
            }

            // Success — deserialize the output payload
            Output output{};
            auto desResult = serialization::DeserializeValue< Output > (
                deserializer, output );
            if ( !desResult.HasValue() )
            {
                return Result< Output >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }

            return Result< Output >::FromValue( std::move( output ) );
        }

        /**
         * @brief Synchronous call via network binding (Strategy pattern hook)
         * @param args Method arguments
         * @return Result containing output or error
         *
         * @details Phase 3 direct path (detail::kMethodDirectPath == true):
         *            Pack Args into ArgsTuple, CallMethod<Output, ArgsTuple> — no serialization.
         *          Phase 1 ByteBuffer path (fallback):
         *            Serialize args → ByteBuffer, call, deserialize response.
         */
        Result< Output > doSyncCall( Args... args ) noexcept
        {
            if ( !m_bindingContext.IsValid() )
            {
                return Result< Output >::FromError(
                    MakeErrorCode( ComErrc::kServiceNotAvailable, 0 ) );
            }

            if constexpr ( detail::kMethodDirectPath< Output, Args... > )
            {
                // Phase 3: Direct typed path — skip CBinarySerializer entirely.
                using ArgsTuple = std::tuple< std::decay_t< Args >... >;
                const ArgsTuple req{ std::forward< Args >( args )... };

                return m_bindingContext.pBinding->template CallMethod< Output >(
                    m_bindingContext.serviceId,
                    m_bindingContext.instanceId,
                    m_bindingContext.elementId,
                    req );
            }
            else
            {
                // Phase 1: serialize → ByteBuffer → deserialize
                auto serResult = serializeArgs( std::forward< Args > ( args )... );
                if ( !serResult.HasValue() )
                {
                    return Result< Output >::FromError( serResult.Error() );
                }

                auto callResult = m_bindingContext.pBinding->
                    template CallMethod< binding::ByteBuffer >(
                    m_bindingContext.serviceId,
                    m_bindingContext.instanceId,
                    m_bindingContext.elementId,
                    serResult.Value() );
                if ( !callResult.HasValue() )
                {
                    return Result< Output >::FromError(
                        MakeErrorCode( ComErrc::kCommunicationFailure, 0 ) );
                }

                return deserializeResponse( callResult.Value() );
            }
        }

        /**
         * @brief Asynchronous call via network binding (Strategy pattern hook)
         * @param args Method arguments
         * @return Future for result
         *
         * @details Phase 3 direct path (detail::kMethodDirectPath == true):
         *            Pack Args into ArgsTuple, CallMethodAsync<Output, ArgsTuple>.
         *          Phase 1 ByteBuffer path (fallback):
         *            Serialize → ByteBuffer, async call, chain deserialize.
         */
        lap::core::Future< Output > doAsyncCall( Args... args ) noexcept
        {
            lap::core::Promise< Output > promise;

            if ( !m_bindingContext.IsValid() )
            {
                promise.SetError( MakeErrorCode( ComErrc::kServiceNotAvailable, 0 ) );
                return promise.GetFuture();
            }

            if constexpr ( detail::kMethodDirectPath< Output, Args... > )
            {
                // Phase 3: Direct typed path — no serialization.
                using ArgsTuple = std::tuple< std::decay_t< Args >... >;
                const ArgsTuple req{ std::forward< Args >( args )... };

                auto asyncFuture = m_bindingContext.pBinding->
                    template CallMethodAsync< Output >(
                    m_bindingContext.serviceId,
                    m_bindingContext.instanceId,
                    m_bindingContext.elementId,
                    req );

                // Chain via Future::Then() — non-blocking continuation
                return asyncFuture.Then(
                    []( lap::core::Future< Output > f ) -> lap::core::Result< Output >
                    {
                        auto rawResult = f.GetResult();
                        if ( rawResult.HasValue() )
                        {
                            return lap::core::Result< Output >::FromValue(
                                std::move( rawResult ).Value() );
                        }
                        return lap::core::Result< Output >::FromError(
                            MakeErrorCode( ComErrc::kCommunicationFailure, 0 ) );
                    } );
            }
            else
            {
                // Phase 1: serialize → ByteBuffer → async call → deserialize
                auto serResult = serializeArgs( std::forward< Args > ( args )... );
                if ( !serResult.HasValue() )
                {
                    promise.SetError( MakeErrorCode( ComErrc::kSerializationError, 0 ) );
                    return promise.GetFuture();
                }

                auto asyncFuture = m_bindingContext.pBinding->
                    template CallMethodAsync< binding::ByteBuffer >(
                    m_bindingContext.serviceId,
                    m_bindingContext.instanceId,
                    m_bindingContext.elementId,
                    serResult.Value() );

                // Chain deserialization via Future::Then() — non-blocking continuation
                return asyncFuture.Then(
                    [this]( lap::core::Future< binding::ByteBuffer > f )
                        -> lap::core::Result< Output >
                    {
                        auto rawResult = f.GetResult();
                        if ( !rawResult.HasValue() )
                        {
                            return lap::core::Result< Output >::FromError(
                                MakeErrorCode( ComErrc::kCommunicationFailure, 0 ) );
                        }

                        auto desResult = deserializeResponse( rawResult.Value() );
                        if ( desResult.HasValue() )
                        {
                            return lap::core::Result< Output >::FromValue(
                                std::move( desResult ).Value() );
                        }
                        return lap::core::Result< Output >::FromError(
                            desResult.Error() );
                    } );
            }
        }

        /**
         * @brief Internal: Set connection state (called by ProxyBase)
         * @param connected Connection state
         */
        void setConnected( Bool connected ) noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );
            m_isConnected = connected;
        }

        /**
         * @brief Internal: Set binding context (called by ProxyBase)
         * @param context Binding context with transport, service/instance/element IDs
         */
        void setBindingContext( const CBindingContext& context ) noexcept
        {
            m_bindingContext = context;
            m_isConnected = context.IsValid();
        }

        friend class ProxyBase;
        friend class ::ProxySkeletonTestAccessor;
    };

    // ========================================================================
    // Fire-and-Forget Method (SWS_CM_00840)
    // ========================================================================

    /**
     * @brief Proxy-side fire-and-forget method (no response expected)
     * @tparam Args Argument types for the method
     * @note SWS_CM_00840
     */
    template< typename... Args >
    class ProxyFireAndForgetMethod
    {
    public:
        /**
         * @brief Constructor
         * @note SWS_CM_00841
         */
        ProxyFireAndForgetMethod() noexcept = default;

        /**
         * @brief Destructor
         * @note SWS_CM_00842
         */
        ~ProxyFireAndForgetMethod() noexcept = default;

        /**
         * @brief Call fire-and-forget method
         * @param args Method arguments
         * @return Result indicating transmission success or error
         * @note SWS_CM_00843
         */
        Result< void > operator()( Args... args ) noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );

            if ( !m_isConnected )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kServiceNotAvailable, 0 ) );
            }

            return doCall( std::forward< Args > ( args )... );
        }

        /**
         * @brief Check if method is connected
         * @return true if connected, false otherwise
         * @note SWS_CM_00844
         */
        Bool IsConnected() const noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );
            return m_isConnected;
        }

        // Move-only type (AUTOSAR C++ A12-8-6)
        ProxyFireAndForgetMethod( ProxyFireAndForgetMethod&& ) noexcept = default;
        ProxyFireAndForgetMethod& operator=( ProxyFireAndForgetMethod&& ) noexcept = default;
        ProxyFireAndForgetMethod( const ProxyFireAndForgetMethod& )            = delete;
        ProxyFireAndForgetMethod& operator=( const ProxyFireAndForgetMethod& ) = delete;

    private:
        mutable UniqueHandle< Mutex > m_pMutex{ MakeUnique< Mutex >() };
        Bool               m_isConnected{ false };
        CBindingContext    m_bindingContext;     ///< Transport binding context

        /**
         * @brief Transmit without waiting for response (Strategy pattern hook)
         * @param args Method arguments
         * @return Result indicating transmission success
         *
         * @details Phase 3 direct path (detail::kFnFDirectPath == true):
         *            Pack Args into ArgsTuple, CallMethod<UInt8, ArgsTuple> —
         *            dummy UInt8 response type (fire-and-forget: response discarded).
         *          Phase 1 ByteBuffer path (fallback):
         *            Serialize args → ByteBuffer, call, discard ByteBuffer response.
         */
        Result< void > doCall( Args... args ) noexcept
        {
            if ( !m_bindingContext.IsValid() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kServiceNotAvailable, 0 ) );
            }

            if constexpr ( detail::kFnFDirectPath< Args... > )
            {
                // Phase 3: Direct typed path — pack args into tuple, dummy response.
                using ArgsTuple = std::tuple< std::decay_t< Args >... >;
                const ArgsTuple req{ std::forward< Args >( args )... };

                auto callResult = m_bindingContext.pBinding->
                    template CallMethod< lap::core::UInt8 >(
                    m_bindingContext.serviceId,
                    m_bindingContext.instanceId,
                    m_bindingContext.elementId,
                    req );

                if ( !callResult.HasValue() )
                {
                    return Result< void >::FromError(
                        MakeErrorCode( ComErrc::kCommunicationFailure, 0 ) );
                }

                return Result< void >::FromValue();
            }
            else
            {
                // Phase 1: serialize → ByteBuffer, fire-and-forget
                serialization::CBinarySerializer serializer;
                Bool success = true;

                int dummy[] = { 0, ( [&]() {
                    if ( success )
                    {
                        auto r = serialization::SerializeValue( serializer, args );
                        if ( !r.HasValue() ) { success = false; }
                    }
                }(), 0 )... };
                static_cast< void > ( dummy );

                if ( !success )
                {
                    return Result< void >::FromError(
                        MakeErrorCode( ComErrc::kSerializationError, 0 ) );
                }

                auto data = serializer.GetData();
                binding::ByteBuffer buffer( data.data(), data.data() + data.size() );

                // Fire-and-forget: call method but ignore response
                auto callResult = m_bindingContext.pBinding->
                    template CallMethod< binding::ByteBuffer >(
                    m_bindingContext.serviceId,
                    m_bindingContext.instanceId,
                    m_bindingContext.elementId,
                    buffer );

                if ( !callResult.HasValue() )
                {
                    return Result< void >::FromError(
                        MakeErrorCode( ComErrc::kCommunicationFailure, 0 ) );
                }

                return Result< void >::FromValue();
            }
        }

        /**
         * @brief Internal: Set connection state (called by ProxyBase)
         * @param connected Connection state
         */
        void setConnected( Bool connected ) noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );
            m_isConnected = connected;
        }

        /**
         * @brief Internal: Set binding context (called by ProxyBase)
         * @param context Binding context with transport, service/instance/element IDs
         */
        void setBindingContext( const CBindingContext& context ) noexcept
        {
            m_bindingContext = context;
            m_isConnected = context.IsValid();
        }

        friend class ProxyBase;
        friend class ::ProxySkeletonTestAccessor;
    };

} // namespace com
} // namespace lap

#endif // LAP_COM_PROXY_METHOD_HPP
