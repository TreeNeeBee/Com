/**
 * @file        ProxyTrigger.hpp
 * @author      Aii
 * @brief       AUTOSAR Adaptive Platform Proxy Trigger Class
 * @date        2026/02/07
 * @details     Trigger subscription and receive handling for proxy side.
 *              Triggers are data-less events — they signal occurrence without payload.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @reference   AUTOSAR_AP_SWS_CommunicationManagement.pdf §8.3.7
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Initial implementation per R25-11 spec
 * <tr><td>2026/02/07  <td>2.0      <td>Aii             <td>CBindingContext wiring to ITransportBinding
 * </table>
 */
#ifndef LAP_COM_PROXY_TRIGGER_HPP
#define LAP_COM_PROXY_TRIGGER_HPP

// ==================== Project-Internal Headers ====================
#include "ComTypes.hpp"
#include "CBindingContext.hpp"
#include "ITransportBinding.hpp"

// ==================== Cross-Module Headers ====================
#include <core/CResult.hpp>
#include <core/CSync.hpp>

// ==================== Standard Library Headers ====================
#include <mutex>
#include <functional>

namespace lap
{
namespace com
{
namespace proxy
{
    /**
     * @brief Proxy-side Trigger class
     * @note [SWS_CM_00726] — Triggers are data-less events (no SampleType).
     *       They support Subscribe/Unsubscribe, SubscriptionState, and receive handlers.
     */
    class ProxyTrigger
    {
    public:
        ProxyTrigger() noexcept = default;
        virtual ~ProxyTrigger() noexcept = default;

        // Non-copyable
        ProxyTrigger( const ProxyTrigger& ) = delete;
        ProxyTrigger& operator=( const ProxyTrigger& ) = delete;

        // Moveable
        ProxyTrigger( ProxyTrigger&& ) noexcept = default;
        ProxyTrigger& operator=( ProxyTrigger&& ) noexcept = default;

        /**
         * @brief Subscribe to the trigger
         * @return Result indicating success or error
         * @note [SWS_CM_00723]
         */
        Result< void > Subscribe() noexcept
        {
            ScopedLock< Mutex > lock( m_mutex );
            m_subscriptionState = SubscriptionState::kSubscriptionPending;

            auto result = doSubscribe();
            if ( result.HasValue() )
            {
                m_subscriptionState = SubscriptionState::kSubscribed;
            }
            else
            {
                m_subscriptionState = SubscriptionState::kNotSubscribed;
            }

            return result;
        }

        /**
         * @brief Unsubscribe from the trigger
         * @note [SWS_CM_00810]
         */
        void Unsubscribe() noexcept
        {
            ScopedLock< Mutex > lock( m_mutex );

            if ( m_subscriptionState != SubscriptionState::kNotSubscribed )
            {
                doUnsubscribe();
                m_subscriptionState = SubscriptionState::kNotSubscribed;
            }
        }

        /**
         * @brief Get current subscription state
         * @return Current SubscriptionState
         * @note [SWS_CM_00724]
         */
        SubscriptionState GetSubscriptionState() const noexcept
        {
            ScopedLock< Mutex > lock( m_mutex );
            return m_subscriptionState;
        }

        /**
         * @brief Set subscription state change handler
         * @param handler Callback for state changes
         * @note [SWS_CM_00725]
         */
        void SetSubscriptionStateChangeHandler(
            SubscriptionStateChangeHandler handler ) noexcept
        {
            ScopedLock< Mutex > lock( m_mutex );
            m_stateChangeHandler = std::move( handler );
        }

        /**
         * @brief Unset subscription state change handler
         * @note [SWS_CM_00727]
         */
        void UnsetSubscriptionStateChangeHandler() noexcept
        {
            ScopedLock< Mutex > lock( m_mutex );
            m_stateChangeHandler = nullptr;
        }

        /**
         * @brief Set receive handler for trigger notifications
         * @param handler Callback invoked when trigger fires
         * @note [SWS_CM_00728]
         */
        void SetReceiveHandler( TriggerReceiveHandler handler ) noexcept
        {
            ScopedLock< Mutex > lock( m_mutex );
            m_receiveHandler = std::move( handler );
        }

        /**
         * @brief Unset receive handler
         * @note [SWS_CM_00729]
         */
        void UnsetReceiveHandler() noexcept
        {
            ScopedLock< Mutex > lock( m_mutex );
            m_receiveHandler = nullptr;
        }

    protected:
        /**
         * @brief Set the binding context for transport operations
         * @param context Binding context with transport binding + IDs
         * @note Called by generated proxy's onBindingContextReady() override.
         *       The elementId in context is the trigger's event ID.
         */
        void setBindingContext( const CBindingContext& context ) noexcept
        {
            m_bindingContext = context;
        }

        /**
         * @brief Implementation-specific subscribe via transport binding
         * @details Subscribes to data-less event on the transport layer.
         *          When a trigger fires remotely, the binding invokes the callback
         *          which calls NotifyTriggerReceived().
         */
        virtual Result< void > doSubscribe() noexcept
        {
            if ( !m_bindingContext.IsValid() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure, 0 ) );
            }

            auto* pBinding = m_bindingContext.pBinding;
            return pBinding->template SubscribeEvent< binding::ByteBuffer >(
                m_bindingContext.serviceId,
                m_bindingContext.instanceId,
                m_bindingContext.elementId,
                [this]( lap::core::UInt64 /*serviceId*/,
                        lap::core::UInt64 /*instanceId*/,
                        lap::core::UInt32 /*eventId*/,
                        const binding::ByteBuffer& /*data*/ )
                {
                    // Triggers are data-less — ignore payload
                    NotifyTriggerReceived();
                } );
        }

        /**
         * @brief Implementation-specific unsubscribe via transport binding
         */
        virtual void doUnsubscribe() noexcept
        {
            if ( m_bindingContext.IsValid() )
            {
                auto* pBinding = m_bindingContext.pBinding;
                static_cast< void > (
                    pBinding->UnsubscribeEvent(
                        m_bindingContext.serviceId,
                        m_bindingContext.instanceId,
                        m_bindingContext.elementId ) );
            }
        }

        /**
         * @brief Notify trigger received (called by binding)
         */
        void NotifyTriggerReceived() noexcept
        {
            ScopedLock< Mutex > lock( m_mutex );
            if ( m_receiveHandler )
            {
                m_receiveHandler();
            }
        }

    private:
        SubscriptionState               m_subscriptionState{ SubscriptionState::kNotSubscribed };
        SubscriptionStateChangeHandler  m_stateChangeHandler{ nullptr };
        TriggerReceiveHandler           m_receiveHandler{ nullptr };
        CBindingContext                 m_bindingContext;
        mutable Mutex              m_mutex;
    };

} // namespace proxy
} // namespace com
} // namespace lap

#endif // LAP_COM_PROXY_TRIGGER_HPP
