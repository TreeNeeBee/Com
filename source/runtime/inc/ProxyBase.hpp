/**
 * @file        ProxyBase.hpp
 * @author      Aii
 * @brief       AUTOSAR Adaptive Platform Service Proxy Base Class
 * @date        2026/02/07
 * @details     Base class for all service proxies per [SWS_CM_00004].
 *              Provides service state tracking and proxy lifecycle management.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @reference   AUTOSAR_AP_SWS_CommunicationManagement.pdf §8.3.8
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/10/30  <td>1.0      <td>ddkv587         <td>Initial implementation
 * <tr><td>2026/02/07  <td>2.0      <td>Aii             <td>R25-11, code style cleanup
 * <tr><td>2026/02/07  <td>3.0      <td>Aii             <td>R25-11 SWS_CM spec compliance
 * <tr><td>2026/02/07  <td>4.0      <td>Aii             <td>CBindingContext propagation to sub-components
 * </table>
 */
#ifndef LAP_COM_PROXY_BASE_HPP
#define LAP_COM_PROXY_BASE_HPP

// ==================== Project-Internal Headers ====================
#include "ComTypes.hpp"
#include "CBindingContext.hpp"
#include "ServiceHandleType.hpp"
#include "Runtime.hpp"
#include "BindingManager.hpp"

// ==================== Cross-Module Headers ====================
#include <core/CResult.hpp>
#include <core/CSync.hpp>

// ==================== Standard Library Headers ====================
#include <mutex>

namespace lap
{
namespace com
{
    /**
     * @brief Base class for all service proxies
     * @note [SWS_CM_00004] — Proxy class is final, non-copyable, move-only
     */
    class ProxyBase
    {
    public:
        /**
         * @brief Destructor
         */
        virtual ~ProxyBase() noexcept = default;

        /**
         * @brief Get the current service state
         * @return Result containing ServiceState or error
         * @note [SWS_CM_01073]
         */
        Result< ServiceState > GetServiceState() noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );
            return Result< ServiceState >::FromValue( m_serviceState );
        }

        /**
         * @brief Register a service state change handler
         * @param handler Callback for service state changes
         * @note [SWS_CM_01074]
         */
        void SetServiceStateChangeHandler( ServiceStateHandler handler ) noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );
            m_serviceStateHandler = std::move( handler );
        }

        /**
         * @brief Unregister service state change handler
         * @note [SWS_CM_01078]
         */
        void UnsetServiceStateChangeHandler() noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );
            m_serviceStateHandler = nullptr;
        }

    protected:
        /**
         * @brief Protected constructor
         */
        ProxyBase() noexcept = default;

        // Non-copyable [SWS_CM_11553, SWS_CM_11551]
        ProxyBase( const ProxyBase& ) = delete;
        ProxyBase& operator=( const ProxyBase& ) = delete;

        /**
         * @brief Move constructor [SWS_CM_11554]
         */
        ProxyBase( ProxyBase&& other ) noexcept
            : m_serviceState( other.m_serviceState )
            , m_serviceStateHandler( std::move( other.m_serviceStateHandler ) )
            , m_bindingContext( other.m_bindingContext )
        {
            other.m_serviceState = ServiceState::kNotAvailable;
            other.m_bindingContext = CBindingContext{};
        }

        /**
         * @brief Move assignment [SWS_CM_11552]
         */
        ProxyBase& operator=( ProxyBase&& other ) noexcept
        {
            if ( this != &other )
            {
                m_serviceState = other.m_serviceState;
                m_serviceStateHandler = std::move( other.m_serviceStateHandler );
                m_bindingContext = other.m_bindingContext;
                other.m_serviceState = ServiceState::kNotAvailable;
                other.m_bindingContext = CBindingContext{};
            }
            return *this;
        }

        /**
         * @brief Notify service state change
         * @param state New service state
         */
        void NotifyServiceStateChange( ServiceState state ) noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );
            m_serviceState = state;

            if ( m_serviceStateHandler )
            {
                m_serviceStateHandler( state );
            }
        }

        /**
         * @brief Get the binding context acquired after proxy creation
         * @return Reference to binding context (may be invalid if binding unavailable)
         */
        const CBindingContext& GetBindingContext() const noexcept
        {
            return m_bindingContext;
        }

        /**
         * @brief Hook called after binding context is acquired
         * @param context The binding context with transport binding + service/instance IDs
         * @details Override in generated service proxy to call setBindingContext()
         *          on each sub-component (events, methods, fields, triggers).
         *          Default implementation is empty (no sub-components at base level).
         * @note Called from ServiceProxy::Create() after binding selection.
         */
        virtual void onBindingContextReady( const CBindingContext& context ) noexcept
        {
            static_cast< void > ( context );
        }

        /**
         * @brief Propagate binding context to a sub-component
         * @tparam SubComponent Any proxy sub-component with setBindingContext()
         * @note ProxyBase is a friend of all proxy sub-components, so this
         *       static method has access to their private setBindingContext().
         *       Generated derived classes call this from onBindingContextReady().
         */
        template< typename SubComponent >
        static void PropagateBindingContext(
            SubComponent& component,
            const CBindingContext& context ) noexcept
        {
            component.setBindingContext( context );
        }

        /**
         * @brief Store binding context and notify derived class
         * @param context Binding context to store
         */
        void setBindingContext( const CBindingContext& context ) noexcept
        {
            m_bindingContext = context;
            onBindingContextReady( m_bindingContext );
        }

    private:
        ServiceState        m_serviceState{ ServiceState::kNotAvailable };
        ServiceStateHandler m_serviceStateHandler{ nullptr };
        CBindingContext     m_bindingContext;
        mutable UniqueHandle< Mutex > m_pMutex{ MakeUnique< Mutex >() };
    };

    // ========================================================================
    // Service Proxy template [SWS_CM_00004]
    // ========================================================================

    /**
     * @brief Service Proxy template
     * @tparam ServiceInterface Type of service interface
     * @note [SWS_CM_00004] — Proxy is final, non-copyable, move-only.
     *       Constructed via named constructor Create(HandleType).
     */
    template< typename ServiceInterface  >
    class ServiceProxy final : public ProxyBase
    {
    public:
        using HandleType = ServiceHandleType< ServiceInterface >;

        /**
         * @brief Named constructor — create proxy from service handle
         * @param handle Service handle obtained from FindService
         * @return Result containing proxy instance or error
         * @note [SWS_CM_10438] — Exception-less construction from HandleType
         */
        static Result< ServiceProxy > Create( const HandleType& handle ) noexcept
        {
            if ( !handle.IsValid() )
            {
                return Result< ServiceProxy >::FromError(
                    MakeErrorCode( ComErrc::kServiceNotAvailable, 0 ) );
            }

            ServiceProxy proxy( handle );
            proxy.NotifyServiceStateChange( ServiceState::kAvailable );

            // Acquire binding and build context for sub-components
            auto serviceId = static_cast< lap::core::UInt64 > (
                ServiceInterface::kServiceId );
            auto instanceId = static_cast< lap::core::UInt64 > (
                handle.GetInstanceId() );

            auto& bindingMgr = Runtime::GetBindingManager();
            auto* pBinding = bindingMgr.SelectBinding( serviceId, instanceId );

            CBindingContext context;
            context.pBinding   = pBinding;
            context.serviceId  = serviceId;
            context.instanceId = instanceId;
            context.elementId  = 0;

            proxy.setBindingContext( context );

            return Result< ServiceProxy >::FromValue( std::move( proxy ) );
        }

        ~ServiceProxy() noexcept override = default;

        ServiceProxy( ServiceProxy&& ) noexcept = default;
        ServiceProxy& operator=( ServiceProxy&& ) noexcept = default;

        // Non-copyable [SWS_CM_11553, SWS_CM_11551]
        ServiceProxy( const ServiceProxy& ) = delete;
        ServiceProxy& operator=( const ServiceProxy& ) = delete;

        /**
         * @brief Get the handle used to create this proxy
         * @return Service handle
         * @note [SWS_CM_10383]
         */
        HandleType GetHandle() const noexcept
        {
            return m_handle;
        }

    protected:
        explicit ServiceProxy( const HandleType& handle ) noexcept
            : ProxyBase()
            , m_handle( handle )
        {}

    private:
        HandleType m_handle;
    };

} // namespace com
} // namespace lap

#endif // LAP_COM_PROXY_BASE_HPP
