/**
 * @file        SkeletonField.hpp
 * @author      Aii
 * @brief       AUTOSAR Adaptive Platform Skeleton-Side Field Communication
 * @date        2026/02/07
 * @details     Skeleton-side field value management with getter/setter handlers
 *              and subscriber notification (SWS_CM Section 9.5).
 *              Composes SkeletonEvent for notification support (Composition pattern).
 *              Split from Field.hpp following SRP.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>Split from Field.hpp (SRP refactoring)
 * </table>
 */
#ifndef LAP_COM_SKELETON_FIELD_HPP
#define LAP_COM_SKELETON_FIELD_HPP

// ==================== Project-Internal Headers ====================
#include "ComTypes.hpp"
#include "CBindingContext.hpp"
#include "SkeletonEvent.hpp"
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
     * @brief Skeleton-side field for managing remote-accessible data
     * @tparam FieldType Type of field data
     * @note SWS_CM_00920 - Field value management
     *
     * @details Design patterns used:
     *          - Composition: Delegates notification to SkeletonEvent< FieldType >
     *          - Strategy:    Getter/Setter handlers are user-provided strategies
     */
    template< typename FieldType >
    class SkeletonField
    {
    public:
        using GetterHandlerType = Function< lap::core::Future< FieldType > () >;
        using SetterHandlerType = Function< lap::core::Future< void > ( const FieldType& ) >;

        /**
         * @brief Constructor
         * @param hasGetter  Field supports Get operation
         * @param hasSetter  Field supports Set operation
         * @param hasNotifier Field supports event notification
         * @note SWS_CM_00921
         */
        explicit SkeletonField( Bool hasGetter  = true,
                                Bool hasSetter  = false,
                                Bool hasNotifier = false ) noexcept
            : m_hasGetter( hasGetter )
            , m_hasSetter( hasSetter )
            , m_hasNotifier( hasNotifier )
        {}

        /**
         * @brief Destructor
         * @note SWS_CM_00922
         */
        ~SkeletonField() noexcept = default;

        // ================================================================
        // Handler Registration (Strategy pattern)
        // ================================================================

        /**
         * @brief Register getter handler
         * @param handler Function to provide field value
         * @return Result indicating success or error
         * @note SWS_CM_00923
         */
        Result< void > RegisterGetHandler( GetterHandlerType handler ) noexcept
        {
            if ( !m_hasGetter )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kInvalidArgument, 0 ) );
            }

            ScopedLock< Mutex > lock( *m_pMutex );
            m_getterHandler = std::move( handler );

            // Wire to binding if context is already set
            if ( m_getterHandler && m_bindingContext.IsValid() )
            {
                registerGetWithBinding();
            }

            return Result< void >::FromValue();
        }

        /**
         * @brief Register setter handler
         * @param handler Function to update field value
         * @return Result indicating success or error
         * @note SWS_CM_00924
         */
        Result< void > RegisterSetHandler( SetterHandlerType handler ) noexcept
        {
            if ( !m_hasSetter )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kInvalidArgument, 0 ) );
            }

            ScopedLock< Mutex > lock( *m_pMutex );
            m_setterHandler = std::move( handler );

            // Wire to binding if context is already set
            if ( m_setterHandler && m_bindingContext.IsValid() )
            {
                registerSetWithBinding();
            }

            return Result< void >::FromValue();
        }

        // ================================================================
        // Notification (delegated to SkeletonEvent via Composition)
        // ================================================================

        /**
         * @brief Update field value and notify subscribers
         * @param value New field value
         * @return Result indicating success or error
         * @note SWS_CM_00925
         */
        Result< void > Update( const FieldType& value ) noexcept
        {
            if ( !m_hasNotifier )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kInvalidArgument, 0 ) );
            }

            ScopedLock< Mutex > lock( *m_pMutex );

            auto sampleResult = m_event.Allocate();
            if ( !sampleResult.HasValue() )
            {
                return Result< void >::FromError( sampleResult.Error() );
            }

            auto sample = std::move( sampleResult ).Value();
            *sample = value;

            return m_event.Send( std::move( sample ) );
        }

        /**
         * @brief Get number of subscribers
         * @return Subscriber count
         * @note SWS_CM_00926
         */
        lap::core::UInt32 GetSubscriberCount() const noexcept
        {
            return m_event.GetSubscriberCount();
        }

        // ================================================================
        // Capability Queries
        // ================================================================

        Bool HasGetter()   const noexcept { return m_hasGetter; }
        Bool HasSetter()   const noexcept { return m_hasSetter; }
        Bool HasNotifier() const noexcept { return m_hasNotifier; }

        // Move-only type (AUTOSAR C++ A12-8-6)
        SkeletonField( SkeletonField&& ) noexcept = default;
        SkeletonField& operator=( SkeletonField&& ) noexcept = default;
        SkeletonField( const SkeletonField& )            = delete;
        SkeletonField& operator=( const SkeletonField& ) = delete;

    private:
        mutable UniqueHandle< Mutex > m_pMutex{ MakeUnique< Mutex >() };
        Bool                    m_hasGetter;
        Bool                    m_hasSetter;
        Bool                    m_hasNotifier;
        GetterHandlerType       m_getterHandler{ nullptr };
        SetterHandlerType       m_setterHandler{ nullptr };
        SkeletonEvent< FieldType > m_event;          ///< Composition: notification delegation
        CBindingContext         m_bindingContext;   ///< Transport binding context for Get/Set

        /// @brief Field Get method ID encoding: fieldId | 0x10000U
        static constexpr lap::core::UInt32 kGetFieldIdMask = 0x10000U;
        /// @brief Field Set method ID encoding: fieldId | 0x20000U
        static constexpr lap::core::UInt32 kSetFieldIdMask = 0x20000U;

        /**
         * @brief Internal: Process getter request
         * @return Future containing field value
         */
        lap::core::Future< FieldType > ProcessGet() noexcept
        {
            std::function< lap::core::Future< FieldType >() > handlerCopy;
            {
                ScopedLock< Mutex > lock( *m_pMutex );
                if ( !m_getterHandler )
                {
                    lap::core::Promise< FieldType > promise;
                    promise.SetError( MakeErrorCode( ComErrc::kFieldSetHandlerNotSet, 0 ) );
                    return promise.GetFuture();
                }
                handlerCopy = m_getterHandler;
            }
            // Lock released — handler may call Update() which re-locks m_pMutex
            return handlerCopy();
        }

        /**
         * @brief Internal: Process setter request
         * @param value New field value
         * @return Future for operation result
         */
        lap::core::Future< void > ProcessSet( const FieldType& value ) noexcept
        {
            std::function< lap::core::Future< void >( const FieldType& ) > handlerCopy;
            {
                ScopedLock< Mutex > lock( *m_pMutex );
                if ( !m_setterHandler )
                {
                    lap::core::Promise< void > promise;
                    promise.SetError( MakeErrorCode( ComErrc::kFieldSetHandlerNotSet, 0 ) );
                    return promise.GetFuture();
                }
                handlerCopy = m_setterHandler;
            }
            // Lock released — handler may call Update() which re-locks m_pMutex
            return handlerCopy( value );
        }

        /**
         * @brief Internal: Set binding context (called by SkeletonBase)
         * @param context Binding context with transport, service/instance/element IDs
         * @note Propagates to composed SkeletonEvent for notification transport.
         *       Also wires Get/Set handlers to the binding if already registered.
         */
        void setBindingContext( const CBindingContext& context ) noexcept
        {
            m_bindingContext = context;

            // Propagate to the composed SkeletonEvent for notification
            m_event.setBindingContext( context );

            // Pre-open the event channel for field notifications (if notifier enabled).
            // This avoids a deadlock when Update() is called from within a DDS
            // listener callback (e.g. from a method handler) — create_topic()
            // needs a Fast DDS participant lock that the listener already holds.
            if ( m_hasNotifier && m_bindingContext.IsValid() &&
                 m_bindingContext.pBinding != nullptr )
            {
                m_bindingContext.pBinding->PrepareEventChannel(
                    m_bindingContext.serviceId,
                    m_bindingContext.instanceId,
                    m_bindingContext.elementId );
            }

            // Wire Get/Set handlers to binding if already registered
            if ( m_bindingContext.IsValid() )
            {
                if ( m_getterHandler )
                {
                    registerGetWithBinding();
                }
                if ( m_setterHandler )
                {
                    registerSetWithBinding();
                }
            }
        }

        /**
         * @brief Register getter callback with transport binding
         *
         * @details Creates a TypedMethodHandler bridge that:
         *          1. Ignores request bytes (getter takes no arguments)
         *          2. Invokes the user-registered get handler
         *          3. Serializes FieldType → ByteBuffer response
         *
         * @pre m_getterHandler is set AND m_bindingContext is valid
         * @note Getter uses method ID = fieldId | 0x10000U
         */
        void registerGetWithBinding() noexcept
        {
            auto* self = this;
            const auto svcId  = m_bindingContext.serviceId;
            const auto instId = m_bindingContext.instanceId;
            const auto getMethodId =
                m_bindingContext.elementId | kGetFieldIdMask;

            // Phase 1: ByteBuffer-based serialization bridge
            auto callback =
                [self]( lap::core::UInt64 /*serviceId*/,
                        lap::core::UInt64 /*instanceId*/,
                        lap::core::UInt32 /*methodId*/,
                        const binding::ByteBuffer& /*request*/ ) -> binding::ByteBuffer
                {
                    return self->HandleGetRequest();
                };

            m_bindingContext.pBinding->template RegisterMethod<
                binding::ByteBuffer, binding::ByteBuffer >(
                svcId, instId, getMethodId, std::move( callback ) );
        }

        /**
         * @brief Handle incoming Get request
         * @return Serialized field value (empty on error)
         */
        binding::ByteBuffer HandleGetRequest() noexcept
        {
            auto future = ProcessGet();
            auto result = future.GetResult();
            if ( !result.HasValue() )
            {
                return binding::ByteBuffer{};
            }

            auto responseBytes = serializeFieldValue( result.Value() );
            if ( !responseBytes.HasValue() )
            {
                return binding::ByteBuffer{};
            }

            return std::move( responseBytes ).Value();
        }

        /**
         * @brief Register setter callback with transport binding
         *
         * @details Creates a TypedMethodHandler bridge that:
         *          1. Deserializes ByteBuffer → FieldType
         *          2. Invokes the user-registered set handler
         *          3. Returns empty ByteBuffer (set returns void)
         *
         * @pre m_setterHandler is set AND m_bindingContext is valid
         * @note Setter uses method ID = fieldId | 0x20000U
         */
        void registerSetWithBinding() noexcept
        {
            auto* self = this;
            const auto svcId  = m_bindingContext.serviceId;
            const auto instId = m_bindingContext.instanceId;
            const auto setMethodId =
                m_bindingContext.elementId | kSetFieldIdMask;

            // Phase 1: ByteBuffer-based serialization bridge
            auto callback =
                [self]( lap::core::UInt64 /*serviceId*/,
                        lap::core::UInt64 /*instanceId*/,
                        lap::core::UInt32 /*methodId*/,
                        const binding::ByteBuffer& request ) -> binding::ByteBuffer
                {
                    return self->HandleSetRequest( request );
                };

            m_bindingContext.pBinding->template RegisterMethod<
                binding::ByteBuffer, binding::ByteBuffer >(
                svcId, instId, setMethodId, std::move( callback ) );
        }

        /**
         * @brief Handle incoming Set request
         * @param request Serialized field value
         * @return Empty ByteBuffer (set has no response payload)
         */
        binding::ByteBuffer HandleSetRequest(
            const binding::ByteBuffer& request ) noexcept
        {
            auto fieldValue = deserializeFieldValue( request );
            if ( !fieldValue.HasValue() )
            {
                return binding::ByteBuffer{};
            }

            auto future = ProcessSet( fieldValue.Value() );
            // Block for completion (dispatched synchronously by binding I/O thread)
            future.GetResult();

            return binding::ByteBuffer{};
        }

        /**
         * @brief Serialize FieldType into ByteBuffer
         * @param value The field value to serialize
         * @return Result containing serialized bytes
         */
        Result< binding::ByteBuffer > serializeFieldValue(
            const FieldType& value ) noexcept
        {
            serialization::CBinarySerializer serializer;

            auto r = serialization::SerializeValue( serializer, value );
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
         * @brief Deserialize ByteBuffer into FieldType
         * @param data Serialized field data
         * @return Result containing deserialized field value
         */
        Result< FieldType > deserializeFieldValue(
            const binding::ByteBuffer& data ) noexcept
        {
            auto span = lap::core::MakeSpan(
                reinterpret_cast< const lap::core::UInt8* > ( data.data() ),
                data.size() );
            serialization::CBinaryDeserializer deserializer( span );

            FieldType value{};
            auto r = serialization::DeserializeValue( deserializer, value );
            if ( !r.HasValue() )
            {
                return Result< FieldType >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }

            return Result< FieldType >::FromValue( std::move( value ) );
        }

        friend class SkeletonBase;
        friend class ::ProxySkeletonTestAccessor;
    };

} // namespace com
} // namespace lap

#endif // LAP_COM_SKELETON_FIELD_HPP
