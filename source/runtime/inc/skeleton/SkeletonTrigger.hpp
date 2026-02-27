/**
 * @file        SkeletonTrigger.hpp
 * @author      Aii
 * @brief       AUTOSAR Adaptive Platform Skeleton Trigger Class
 * @date        2026/02/07
 * @details     Trigger sending for skeleton side.
 *              Triggers are data-less events — the skeleton fires a trigger to notify
 *              subscribed proxies without transmitting any payload data.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @reference   AUTOSAR_AP_SWS_CommunicationManagement.pdf §8.4.4
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Initial implementation per R25-11 spec
 * <tr><td>2026/02/07  <td>2.0      <td>Aii             <td>CBindingContext wiring to ITransportBinding
 * </table>
 */
#ifndef LAP_COM_SKELETON_TRIGGER_HPP
#define LAP_COM_SKELETON_TRIGGER_HPP

// ==================== Project-Internal Headers ====================
#include "ComTypes.hpp"
#include "CBindingContext.hpp"
#include "ITransportBinding.hpp"

// ==================== Cross-Module Headers ====================
#include <core/CResult.hpp>
#include <core/CSync.hpp>

// ==================== Standard Library Headers ====================
#include <mutex>

namespace lap
{
namespace com
{
namespace skeleton
{
    /**
     * @brief Skeleton-side Trigger class
     * @note [SWS_CM_00726] — Triggers are data-less events.
     *       Skeleton fires triggers to notify subscribed proxies.
     */
    class SkeletonTrigger
    {
    public:
        SkeletonTrigger() noexcept = default;
        virtual ~SkeletonTrigger() noexcept = default;

        // Non-copyable
        SkeletonTrigger( const SkeletonTrigger& ) = delete;
        SkeletonTrigger& operator=( const SkeletonTrigger& ) = delete;

        // Moveable
        SkeletonTrigger( SkeletonTrigger&& ) noexcept = default;
        SkeletonTrigger& operator=( SkeletonTrigger&& ) noexcept = default;

        /**
         * @brief Fire the trigger — notify all subscribed proxies
         * @return Result indicating success or error
         * @note [SWS_CM_00730] — Sends a data-less notification
         */
        Result< void > Trigger() noexcept
        {
            ScopedLock< Mutex > lock( m_mutex );
            return doTrigger();
        }

        /**
         * @brief Get subscription state (skeleton perspective)
         * @return kSubscribed if at least one client is subscribed
         * @note [SWS_CM_00731]
         */
        SubscriptionState GetSubscriptionState() const noexcept
        {
            ScopedLock< Mutex > lock( m_mutex );
            return m_subscriptionState;
        }

        /**
         * @brief Set subscription state change handler
         * @param handler Callback for state changes
         * @note [SWS_CM_00732]
         */
        void SetSubscriptionStateChangeHandler(
            SubscriptionStateChangeHandler handler ) noexcept
        {
            ScopedLock< Mutex > lock( m_mutex );
            m_stateChangeHandler = std::move( handler );
        }

        /**
         * @brief Unset subscription state change handler
         * @note [SWS_CM_00733]
         */
        void UnsetSubscriptionStateChangeHandler() noexcept
        {
            ScopedLock< Mutex > lock( m_mutex );
            m_stateChangeHandler = nullptr;
        }

    protected:
        /**
         * @brief Set the binding context for transport operations
         * @param context Binding context with transport binding + IDs
         * @note Called by generated skeleton's onBindingContextReady() override.
         *       The elementId in context is the trigger's event ID.
         */
        void setBindingContext( const CBindingContext& context ) noexcept
        {
            m_bindingContext = context;
        }

        /**
         * @brief Implementation-specific trigger sending via transport binding
         * @details Sends a data-less event (empty ByteBuffer) through the
         *          transport binding to notify all subscribed proxies.
         */
        virtual Result< void > doTrigger() noexcept
        {
            if ( !m_bindingContext.IsValid() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure, 0 ) );
            }

            auto* pBinding = m_bindingContext.pBinding;
            binding::ByteBuffer emptyPayload;
            return pBinding->SendEvent(
                m_bindingContext.serviceId,
                m_bindingContext.instanceId,
                m_bindingContext.elementId,
                emptyPayload );
        }

        /**
         * @brief Update subscription state (called by binding)
         * @param state New subscription state
         */
        void UpdateSubscriptionState( SubscriptionState state ) noexcept
        {
            ScopedLock< Mutex > lock( m_mutex );
            m_subscriptionState = state;

            if ( m_stateChangeHandler )
            {
                m_stateChangeHandler( state );
            }
        }

    private:
        SubscriptionState               m_subscriptionState{ SubscriptionState::kNotSubscribed };
        SubscriptionStateChangeHandler  m_stateChangeHandler{ nullptr };
        CBindingContext                 m_bindingContext;
        mutable Mutex              m_mutex;
    };

} // namespace skeleton
} // namespace com
} // namespace lap

#endif // LAP_COM_SKELETON_TRIGGER_HPP
