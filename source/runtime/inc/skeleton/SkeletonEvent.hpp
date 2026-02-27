/**
 * @file        SkeletonEvent.hpp
 * @author      Aii
 * @brief       AUTOSAR Adaptive Platform Skeleton-Side Event Communication
 * @date        2026/02/07
 * @details     Skeleton-side event transmission (SWS_CM Section 9.3).
 *              Split from Event.hpp following Single Responsibility Principle.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>Split from Event.hpp (SRP refactoring)
 * </table>
 */
#ifndef LAP_COM_SKELETON_EVENT_HPP
#define LAP_COM_SKELETON_EVENT_HPP

// ==================== Project-Internal Headers ====================
#include "ComTypes.hpp"
#include "CBindingContext.hpp"
#include "serialization/CBinarySerializer.hpp"
#include "serialization/CSerializationTraits.hpp"

// ==================== Binding Headers ====================
#include "ITransportBinding.hpp"

// ==================== Cross-Module Headers ====================
#include <core/CResult.hpp>
#include <core/CSync.hpp>

// ==================== Standard Library Headers ====================
#include <memory>
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
     * @brief Skeleton-side event for sending data
     * @tparam SampleType Type of event data
     * @note SWS_CM_00720 - Event transmission
     */
    template< typename SampleType >
    class SkeletonEvent
    {
    public:
        /**
         * @brief Constructor
         * @note SWS_CM_00721
         */
        SkeletonEvent() noexcept = default;

        /**
         * @brief Destructor
         * @note SWS_CM_00722
         */
        ~SkeletonEvent() noexcept = default;

        /**
         * @brief Allocate sample for sending
         * @return Result containing allocated sample or error
         * @note SWS_CM_00723
         */
        Result< SampleAllocateePtr< SampleType > > Allocate() noexcept
        {
            auto sample = MakeUnique< SampleType > ();
            if ( !sample )
            {
                return Result< SampleAllocateePtr< SampleType > >::FromError(
                    MakeErrorCode( ComErrc::kMaxSamplesExceeded, 0 ) );
            }

            return Result< SampleAllocateePtr< SampleType > >::FromValue(
                std::move( sample ) );
        }

        /**
         * @brief Send allocated sample
         * @param sample Sample to send (ownership transferred)
         * @return Result indicating success or error
         * @note SWS_CM_00724
         */
        Result< void > Send( SampleAllocateePtr< SampleType > sample ) noexcept
        {
            if ( !sample )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kInvalidArgument, 0 ) );
            }

            ScopedLock< Mutex > lock( *m_pMutex );

            if ( !m_isOffered )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kServiceNotOffered, 0 ) );
            }

            return doSend( std::move( sample ) );
        }

        /**
         * @brief Get number of connected subscribers
         * @return Number of subscribers
         * @note SWS_CM_00725
         */
        lap::core::UInt32 GetSubscriberCount() const noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );
            return m_subscriberCount;
        }

        // Move-only type (AUTOSAR C++ A12-8-6)
        SkeletonEvent( SkeletonEvent&& ) noexcept = default;
        SkeletonEvent& operator=( SkeletonEvent&& ) noexcept = default;
        SkeletonEvent( const SkeletonEvent& )            = delete;
        SkeletonEvent& operator=( const SkeletonEvent& ) = delete;

    private:
        mutable UniqueHandle< Mutex > m_pMutex{ MakeUnique< Mutex >() };
        Bool                m_isOffered{ false };
        lap::core::UInt32   m_subscriberCount{ 0 };
        CBindingContext     m_bindingContext;    ///< Transport binding context

        /**
         * @brief Transmit via network binding (Strategy pattern hook)
         * @param sample Sample to transmit
         * @return Result indicating success or error
         *
         * @details Serialization policy:
         *          - Trivially-copyable types: pass directly via SendEvent<T>.
         *            WireSize<T>() = sizeof(T), binding uses memcpy / adapter path.
         *          - Non-trivially-copyable types (String, std::vector, etc.):
         *            a) If binding supports typed adapters (DDS): pass directly,
         *               the registered DDS adapter handles CDR serialization.
         *            b) Otherwise: serialize via CBinarySerializer → ByteBuffer →
         *               SendEvent<ByteBuffer>.  WireSize<ByteBuffer>() = 0.
         */
        Result< void > doSend( SampleAllocateePtr< SampleType > sample ) noexcept
        {
            if ( !m_bindingContext.IsValid() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kServiceNotOffered, 0 ) );
            }

            if constexpr ( std::is_trivially_copyable_v< SampleType > )
            {
                // Direct typed path — binding receives sizeof(T) via WireSize
                return m_bindingContext.pBinding->SendEvent(
                    m_bindingContext.serviceId,
                    m_bindingContext.instanceId,
                    m_bindingContext.elementId,
                    *sample );
            }
            else
            {
                // Non-trivially-copyable: check if a typed adapter exists
                // for this specific event (not just binding-level support).
                // Field notifications have no adapter → must serialize.
                if ( m_bindingContext.pBinding->HasEventAdapter(
                         m_bindingContext.serviceId,
                         m_bindingContext.elementId ) )
                {
                    // Phase 6b: DDS typed adapter path — pass typed pointer,
                    // the adapter maps app fields → DDS struct for CDR send
                    return m_bindingContext.pBinding->SendEvent(
                        m_bindingContext.serviceId,
                        m_bindingContext.instanceId,
                        m_bindingContext.elementId,
                        *sample );
                }

                // Fallback: Serialize → ByteBuffer → binding
                serialization::CBinarySerializer serializer;
                auto serResult = serialization::SerializeValue< SampleType > (
                    serializer, *sample );
                if ( !serResult.HasValue() )
                {
                    return Result< void >::FromError(
                        MakeErrorCode( ComErrc::kSerializationError, 0 ) );
                }

                auto data = serializer.GetData();
                binding::ByteBuffer buffer(
                    data.data(), data.data() + data.size() );

                return m_bindingContext.pBinding->SendEvent(
                    m_bindingContext.serviceId,
                    m_bindingContext.instanceId,
                    m_bindingContext.elementId,
                    buffer );
            }
        }

        /**
         * @brief Internal: Set offered state (called by SkeletonBase)
         * @param offered Offering state
         */
        void setOffered( Bool offered ) noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );
            m_isOffered = offered;
        }

        /**
         * @brief Internal: Set binding context (called by SkeletonBase)
         * @param context Binding context with transport, service/instance/element IDs
         */
        void setBindingContext( const CBindingContext& context ) noexcept
        {
            m_bindingContext = context;
            m_isOffered = context.IsValid();
        }

        friend class SkeletonBase;
        template< typename > friend class SkeletonField;
        friend class ::ProxySkeletonTestAccessor;
    };

} // namespace com
} // namespace lap

#endif // LAP_COM_SKELETON_EVENT_HPP
