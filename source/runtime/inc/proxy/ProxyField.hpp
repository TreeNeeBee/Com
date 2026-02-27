/**
 * @file        ProxyField.hpp
 * @author      Aii
 * @brief       AUTOSAR Adaptive Platform Proxy-Side Field Communication
 * @date        2026/02/07
 * @details     Proxy-side field getter/setter/notification (SWS_CM Section 9.5).
 *              Composes ProxyEvent for notification support (Composition pattern).
 *              Split from Field.hpp following SRP.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>Split from Field.hpp (SRP refactoring)
 * </table>
 */
#ifndef LAP_COM_PROXY_FIELD_HPP
#define LAP_COM_PROXY_FIELD_HPP

// ==================== Project-Internal Headers ====================
#include "ComTypes.hpp"
#include "CBindingContext.hpp"
#include "ProxyEvent.hpp"
#include "serialization/CBinarySerializer.hpp"
#include "serialization/CBinaryDeserializer.hpp"
#include "serialization/CSerializationTraits.hpp"

// ==================== Binding Headers ====================
#include "ITransportBinding.hpp"

// ==================== Cross-Module Headers ====================
#include <core/CResult.hpp>
#include <core/CFuture.hpp>
#include <core/CSync.hpp>

// ==================== Standard Library Headers ====================
#include <functional>
#include <mutex>

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

    /**
     * @brief Proxy-side field for accessing remote data
     * @tparam FieldType Type of field data
     * @note SWS_CM_00900 - Field getter/setter/notification
     *
     * @details Design patterns used:
     *          - Composition: Delegates notification to ProxyEvent< FieldType >
     *          - Strategy:    doGet/doSet are hooks for binding-specific transport
     */
    template< typename FieldType >
    class ProxyField
    {
    public:
        /**
         * @brief Constructor
         * @param hasGetter  Field supports Get operation
         * @param hasSetter  Field supports Set operation
         * @param hasNotifier Field supports event notification
         * @note SWS_CM_00901
         */
        explicit ProxyField( Bool hasGetter  = true,
                             Bool hasSetter  = false,
                             Bool hasNotifier = false ) noexcept
            : m_hasGetter( hasGetter )
            , m_hasSetter( hasSetter )
            , m_hasNotifier( hasNotifier )
        {}

        /**
         * @brief Destructor
         * @note SWS_CM_00902
         */
        ~ProxyField() noexcept = default;

        // ================================================================
        // Getter Operations
        // ================================================================

        /**
         * @brief Get field value synchronously
         * @return Result containing field value or error
         * @note SWS_CM_00903
         */
        Result< FieldType > Get() noexcept
        {
            if ( !m_hasGetter )
            {
                return Result< FieldType >::FromError(
                    MakeErrorCode( ComErrc::kInvalidArgument, 0 ) );
            }

            ScopedLock< Mutex > lock( *m_pMutex );

            if ( !m_isConnected )
            {
                return Result< FieldType >::FromError(
                    MakeErrorCode( ComErrc::kServiceNotAvailable, 0 ) );
            }

            return doGet();
        }

        /**
         * @brief Get field value asynchronously
         * @return Future containing field value
         * @note SWS_CM_00904
         */
        lap::core::Future< FieldType > GetAsync() noexcept
        {
            if ( !m_hasGetter )
            {
                lap::core::Promise< FieldType > promise;
                promise.SetError( MakeErrorCode( ComErrc::kInvalidArgument, 0 ) );
                return promise.GetFuture();
            }

            ScopedLock< Mutex > lock( *m_pMutex );

            if ( !m_isConnected )
            {
                lap::core::Promise< FieldType > promise;
                promise.SetError( MakeErrorCode( ComErrc::kServiceNotAvailable, 0 ) );
                return promise.GetFuture();
            }

            return DoGetAsync();
        }

        // ================================================================
        // Setter Operations
        // ================================================================

        /**
         * @brief Set field value synchronously
         * @param value New field value
         * @return Result indicating success or error
         * @note SWS_CM_00905
         */
        Result< void > Set( const FieldType& value ) noexcept
        {
            if ( !m_hasSetter )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kInvalidArgument, 0 ) );
            }

            ScopedLock< Mutex > lock( *m_pMutex );

            if ( !m_isConnected )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kServiceNotAvailable, 0 ) );
            }

            return doSet( value );
        }

        /**
         * @brief Set field value asynchronously
         * @param value New field value
         * @return Future for operation result
         * @note SWS_CM_00906
         */
        lap::core::Future< void > SetAsync( const FieldType& value ) noexcept
        {
            if ( !m_hasSetter )
            {
                lap::core::Promise< void > promise;
                promise.SetError( MakeErrorCode( ComErrc::kInvalidArgument, 0 ) );
                return promise.GetFuture();
            }

            ScopedLock< Mutex > lock( *m_pMutex );

            if ( !m_isConnected )
            {
                lap::core::Promise< void > promise;
                promise.SetError( MakeErrorCode( ComErrc::kServiceNotAvailable, 0 ) );
                return promise.GetFuture();
            }

            return DoSetAsync( value );
        }

        // ================================================================
        // Notification Operations (delegated to ProxyEvent via Composition)
        // ================================================================

        /**
         * @brief Subscribe to field change notifications
         * @param maxSampleCount Maximum number of cached updates
         * @return Result indicating success or error
         * @note SWS_CM_00907
         */
        Result< void > Subscribe( lap::core::UInt32 maxSampleCount = 1 ) noexcept
        {
            if ( !m_hasNotifier )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kInvalidArgument, 0 ) );
            }

            return m_event.Subscribe( maxSampleCount );
        }

        /**
         * @brief Unsubscribe from field change notifications
         * @note SWS_CM_00908
         */
        void Unsubscribe() noexcept
        {
            if ( m_hasNotifier )
            {
                m_event.Unsubscribe();
            }
        }

        /**
         * @brief Get subscription state
         * @return Subscription state
         * @note SWS_CM_00909
         */
        SubscriptionState GetSubscriptionState() const noexcept
        {
            return m_event.GetSubscriptionState();
        }

        /**
         * @brief Get number of available update notifications
         * @return Number of cached updates
         * @note SWS_CM_00910
         */
        lap::core::UInt32 GetNewSamples() const noexcept
        {
            return m_event.GetNewSamples();
        }

        /**
         * @brief Get next field update notification
         * @param timeout Maximum wait time
         * @return Result containing updated field value or error
         * @note SWS_CM_00911
         */
        Result< SamplePtr< FieldType > > GetNextSample(
            std::chrono::milliseconds timeout = std::chrono::milliseconds( 0 ) ) noexcept
        {
            return m_event.GetNextSample( timeout );
        }

        /**
         * @brief Set handler for field update notifications (Observer pattern)
         * @param handler Callback for field changes
         * @return Result indicating success or error
         * @note SWS_CM_00912
         */
        Result< void > SetReceiveHandler( FieldReceiveHandler handler ) noexcept
        {
            if ( !m_hasNotifier )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kInvalidArgument, 0 ) );
            }

            return m_event.SetReceiveHandler( std::move( handler ) );
        }

        /**
         * @brief Unset field update handler
         * @note SWS_CM_00913
         */
        void UnsetReceiveHandler() noexcept
        {
            m_event.UnsetReceiveHandler();
        }

        // ================================================================
        // Capability Queries
        // ================================================================

        Bool HasGetter()   const noexcept { return m_hasGetter; }
        Bool HasSetter()   const noexcept { return m_hasSetter; }
        Bool HasNotifier() const noexcept { return m_hasNotifier; }

        // Move-only type (AUTOSAR C++ A12-8-6)
        ProxyField( ProxyField&& ) noexcept = default;
        ProxyField& operator=( ProxyField&& ) noexcept = default;
        ProxyField( const ProxyField& )            = delete;
        ProxyField& operator=( const ProxyField& ) = delete;

    private:
        mutable UniqueHandle< Mutex > m_pMutex{ MakeUnique< Mutex >() };
        Bool                    m_hasGetter;
        Bool                    m_hasSetter;
        Bool                    m_hasNotifier;
        Bool                    m_isConnected{ false };
        ProxyEvent< FieldType >   m_event;            ///< Composition: notification delegation
        CBindingContext         m_bindingContext;    ///< Transport binding context

        // ================================================================
        // Strategy hooks — connected to ITransportBinding
        // ================================================================

        /**
         * @brief Get field value synchronously via binding
         * @return Result containing deserialized field value
         *
         * @details Flow:
         *          1. ITransportBinding::GetField() → ByteBuffer
         *          2. Deserialize ByteBuffer → FieldType
         */
        Result< FieldType > doGet() noexcept
        {
            if ( !m_bindingContext.IsValid() )
            {
                return Result< FieldType >::FromError(
                    MakeErrorCode( ComErrc::kServiceNotAvailable, 0 ) );
            }

            auto getResult = m_bindingContext.pBinding->
                template GetField< binding::ByteBuffer >(
                m_bindingContext.serviceId,
                m_bindingContext.instanceId,
                m_bindingContext.elementId );
            if ( !getResult.HasValue() )
            {
                return Result< FieldType >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure, 0 ) );
            }

            // Deserialize
            auto& responseData = getResult.Value();
            auto span = lap::core::MakeSpan(
                reinterpret_cast< const lap::core::UInt8* > ( responseData.data() ),
                responseData.size() );
            serialization::CBinaryDeserializer deserializer( span );

            FieldType value{};
            auto desResult = serialization::DeserializeValue< FieldType > (
                deserializer, value );
            if ( !desResult.HasValue() )
            {
                return Result< FieldType >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }

            return Result< FieldType >::FromValue( std::move( value ) );
        }

        /**
         * @brief Get field value asynchronously via binding
         * @return Future containing field value
         */
        lap::core::Future< FieldType > DoGetAsync() noexcept
        {
            lap::core::Promise< FieldType > promise;

            // Delegate to sync Get() and wrap in future
            // TODO: True async via binding when core Future::Then() available
            auto result = doGet();
            if ( result.HasValue() )
            {
                promise.SetValue( std::move( result ).Value() );
            }
            else
            {
                promise.SetError( result.Error() );
            }

            return promise.GetFuture();
        }

        /**
         * @brief Set field value synchronously via binding
         * @param value New field value
         * @return Result indicating success or error
         *
         * @details Flow:
         *          1. Serialize FieldType → ByteBuffer
         *          2. ITransportBinding::SetField()
         */
        Result< void > doSet( const FieldType& value ) noexcept
        {
            if ( !m_bindingContext.IsValid() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kServiceNotAvailable, 0 ) );
            }

            // Serialize value
            serialization::CBinarySerializer serializer;
            auto serResult = serialization::SerializeValue< FieldType > (
                serializer, value );
            if ( !serResult.HasValue() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kSerializationError, 0 ) );
            }

            auto data = serializer.GetData();
            binding::ByteBuffer buffer( data.data(), data.data() + data.size() );

            return m_bindingContext.pBinding->SetField(
                m_bindingContext.serviceId,
                m_bindingContext.instanceId,
                m_bindingContext.elementId,
                buffer );
        }

        /**
         * @brief Set field value asynchronously via binding
         * @param value New field value
         * @return Future for operation result
         */
        lap::core::Future< void > DoSetAsync( const FieldType& value ) noexcept
        {
            lap::core::Promise< void > promise;

            // Delegate to sync doSet() and wrap in future
            // TODO: True async via binding when core Future::Then() available
            auto result = doSet( value );
            if ( result.HasValue() )
            {
                promise.SetValue();
            }
            else
            {
                promise.SetError( result.Error() );
            }

            return promise.GetFuture();
        }

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

            // Propagate to the composed ProxyEvent for notification transport
            m_event.setBindingContext( context );
        }

        friend class ProxyBase;
        friend class ::ProxySkeletonTestAccessor;
    };

} // namespace com
} // namespace lap

#endif // LAP_COM_PROXY_FIELD_HPP
