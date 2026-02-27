/**
 * @file        SkeletonBase.hpp
 * @author      Aii
 * @brief       AUTOSAR Adaptive Platform Service Skeleton Base Class
 * @date        2026/02/07
 * @details     Base class for all service skeletons per [SWS_CM_00002].
 *              Provides service offering lifecycle and method call processing.
 *              ServiceSkeleton<T>::doOfferService/doStopOfferService delegate
 *              to Runtime::OfferService<T> / Runtime::StopOfferService<T>.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @reference   AUTOSAR_AP_SWS_CommunicationManagement.pdf §8.4.5
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/10/30  <td>1.0      <td>ddkv587         <td>Initial implementation
 * <tr><td>2026/02/07  <td>2.0      <td>Aii             <td>R25-11, code style cleanup
 * <tr><td>2026/02/07  <td>3.0      <td>Aii             <td>R25-11 SWS_CM spec compliance
 * <tr><td>2026/02/07  <td>4.0      <td>Aii             <td>Wire doOfferService to Runtime BindingManager
 * <tr><td>2026/02/07  <td>5.0      <td>Aii             <td>CBindingContext propagation to sub-components
 * </table>
 */
#ifndef LAP_COM_SKELETON_BASE_HPP
#define LAP_COM_SKELETON_BASE_HPP

// ==================== Project-Internal Headers ====================
#include "ComTypes.hpp"
#include "CBindingContext.hpp"
#include "Runtime.hpp"
#include "BindingManager.hpp"

// ==================== Cross-Module Headers ====================
#include <core/CResult.hpp>
#include <core/CInstanceSpecifier.hpp>
#include <core/CSync.hpp>

// ==================== Standard Library Headers ====================
#include <mutex>

namespace lap
{
namespace com
{
    /**
     * @brief Base class for all service skeletons
     * @note [SWS_CM_00002] — Skeleton class is non-copyable, move-only
     */
    class SkeletonBase
    {
    public:
        /**
         * @brief Destructor — automatically stops offering if needed
         * @note [SWS_CM_11549]
         */
        virtual ~SkeletonBase() noexcept = default;

        /**
         * @brief Offer the service
         * @return Result indicating success or error
         * @note [SWS_CM_00101] — May return kFieldValueNotInitialized, kFieldSetHandlerNotSet, kCommunicationFailure
         */
        Result< void > OfferService() noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );

            if ( m_bOffered )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kInvalidState, 0 ) );
            }

            auto result = doOfferService();
            if ( result.HasValue() )
            {
                m_bOffered = true;
            }

            return result;
        }

        /**
         * @brief Stop offering the service
         * @note [SWS_CM_00111]
         */
        void StopOfferService() noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );

            if ( m_bOffered )
            {
                doStopOfferService();
                m_bOffered = false;
            }
        }

        /**
         * @brief Check if service is offered
         * @return true if service is offered
         */
        Bool IsOffered() const noexcept
        {
            ScopedLock< Mutex > lock( *m_pMutex );
            return m_bOffered;
        }

        /**
         * @brief Process next pending method call (polling mode)
         * @return true if there is at least one pending invocation, false otherwise
         * @note [SWS_CM_00199] — Return type is bool per R25-11 spec.
         *       The pending method is executed in the context of ProcessNextMethodCall().
         *       Violation if called in kEvent or kEventSingleThread mode.
         */
        Bool ProcessNextMethodCall() noexcept
        {
            if ( !m_bOffered )
            {
                return false;
            }

            if ( m_processingMode != MethodCallProcessingMode::kPoll )
            {
                // WrongMethodCallProcessingModeViolation per spec
                return false;
            }

            return doProcessNextMethodCall();
        }

    protected:
        /**
         * @brief Protected constructor
         * @param instanceSpec Instance specifier for the service
         * @param mode Method call processing mode
         * @note [SWS_CM_00130] — Skeleton constructors take InstanceIdentifier/InstanceSpecifier + MethodCallProcessingMode
         */
        explicit SkeletonBase(
            lap::core::InstanceSpecifier instanceSpec,
            MethodCallProcessingMode mode = MethodCallProcessingMode::kEvent ) noexcept
            : m_instanceSpecifier( std::move( instanceSpec ) )
            , m_processingMode( mode )
            , m_bOffered( false )
        {}

        // Non-copyable [SWS_CM_11546, SWS_CM_11544]
        SkeletonBase( const SkeletonBase& ) = delete;
        SkeletonBase& operator=( const SkeletonBase& ) = delete;

        /**
         * @brief Move constructor [SWS_CM_11547]
         */
        SkeletonBase( SkeletonBase&& other ) noexcept
            : m_instanceSpecifier( std::move( other.m_instanceSpecifier ) )
            , m_processingMode( other.m_processingMode )
            , m_bOffered( other.m_bOffered )
            , m_bindingContext( other.m_bindingContext )
        {
            other.m_bOffered = false;
            other.m_bindingContext = CBindingContext{};
        }

        /**
         * @brief Move assignment [SWS_CM_11545]
         */
        SkeletonBase& operator=( SkeletonBase&& other ) noexcept
        {
            if ( this != &other )
            {
                if ( m_bOffered )
                {
                    StopOfferService();
                }

                m_instanceSpecifier = std::move( other.m_instanceSpecifier );
                m_processingMode = other.m_processingMode;
                m_bOffered = other.m_bOffered;
                m_bindingContext = other.m_bindingContext;
                other.m_bOffered = false;
                other.m_bindingContext = CBindingContext{};
            }
            return *this;
        }

        /**
         * @brief Get instance specifier
         * @return Instance specifier
         */
        const lap::core::InstanceSpecifier& GetInstanceSpecifier() const noexcept
        {
            return m_instanceSpecifier;
        }

        /**
         * @brief Get method call processing mode
         * @return Processing mode
         */
        MethodCallProcessingMode GetProcessingMode() const noexcept
        {
            return m_processingMode;
        }

        /**
         * @brief Get the binding context acquired after OfferService
         * @return Reference to binding context (may be invalid if not yet offered)
         */
        const CBindingContext& GetBindingContext() const noexcept
        {
            return m_bindingContext;
        }

        /**
         * @brief Implementation-specific service offering
         * @return Result indicating success or error
         */
        virtual Result< void > doOfferService() noexcept = 0;

        /**
         * @brief Implementation-specific service stop
         */
        virtual void doStopOfferService() noexcept = 0;

        /**
         * @brief Implementation-specific method call processing
         * @return true if there is at least one pending invocation
         */
        virtual Bool doProcessNextMethodCall() noexcept
        {
            return false;
        }

        /**
         * @brief Hook called after binding context is acquired
         * @param context The binding context with transport binding + service/instance IDs
         * @details Override in generated service skeleton to call setBindingContext()
         *          on each sub-component (events, methods, fields, triggers).
         *          Default implementation is empty (no sub-components at base level).
         * @note Called from doOfferService() after binding selection succeeds.
         */
        virtual void onBindingContextReady( const CBindingContext& context ) noexcept
        {
            static_cast< void > ( context );
        }

        /**
         * @brief Propagate binding context to a sub-component
         * @tparam SubComponent Any skeleton sub-component with setBindingContext()
         * @note SkeletonBase is a friend of all skeleton sub-components, so
         *       this static method has access to their private setBindingContext().
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
         * @brief Store binding context (called by derived doOfferService)
         * @param context Binding context to store
         */
        void setBindingContext( const CBindingContext& context ) noexcept
        {
            m_bindingContext = context;
            onBindingContextReady( m_bindingContext );
        }

    private:
        lap::core::InstanceSpecifier    m_instanceSpecifier;
        MethodCallProcessingMode        m_processingMode;
        Bool                            m_bOffered;
        CBindingContext                 m_bindingContext;
        mutable UniqueHandle< Mutex > m_pMutex{ MakeUnique< Mutex >() };
    };

    // ========================================================================
    // Service Skeleton template [SWS_CM_00002]
    // ========================================================================

    /**
     * @brief Service Skeleton template
     * @tparam ServiceInterface Type of service interface
     * @note [SWS_CM_00002] — Skeleton is non-copyable, move-only.
     *       Constructed with InstanceSpecifier + MethodCallProcessingMode.
     */
    template< typename ServiceInterface  >
    class ServiceSkeleton : public SkeletonBase
    {
    public:
        /**
         * @brief Constructor
         * @param instanceSpec Instance specifier for the service
         * @param mode Method call processing mode
         * @note [SWS_CM_00130]
         */
        explicit ServiceSkeleton(
            lap::core::InstanceSpecifier instanceSpec,
            MethodCallProcessingMode mode = MethodCallProcessingMode::kEvent ) noexcept
            : SkeletonBase( std::move( instanceSpec ), mode )
        {}

        /**
         * @brief Destructor — automatically stops offering
         * @note [SWS_CM_11549]
         */
        ~ServiceSkeleton() noexcept override
        {
            if ( IsOffered() )
            {
                StopOfferService();
            }
        }

        ServiceSkeleton( ServiceSkeleton&& ) noexcept = default;
        ServiceSkeleton& operator=( ServiceSkeleton&& ) noexcept = default;

        // Non-copyable
        ServiceSkeleton( const ServiceSkeleton& ) = delete;
        ServiceSkeleton& operator=( const ServiceSkeleton& ) = delete;

    protected:
        /**
         * @brief Offer service via Runtime → BindingManager → transport binding
         * @return Result< void >
         * @note Flow: Runtime::OfferService<ServiceInterface>()
         *       → BindingManager::SelectBinding → binding->OfferService
         *       → CRegistryProxy::RegisterService
         *       After success: acquire binding context and propagate to sub-components.
         */
        Result< void > doOfferService() noexcept override
        {
            auto result = Runtime::OfferService< ServiceInterface > (
                GetInstanceSpecifier() );

            if ( result.HasValue() )
            {
                // Acquire binding and build context for sub-components
                auto serviceId = static_cast< lap::core::UInt64 > (
                    ServiceInterface::kServiceId );
                auto instanceId = serviceId & 0xFFFFU;

                auto& bindingMgr = Runtime::GetBindingManager();
                auto* pBinding = bindingMgr.SelectBinding( serviceId, instanceId );

                CBindingContext context;
                context.pBinding   = pBinding;
                context.serviceId  = serviceId;
                context.instanceId = instanceId;
                context.elementId  = 0;

                setBindingContext( context );
            }

            return result;
        }

        /**
         * @brief Stop offering service via Runtime → BindingManager
         * @note Flow: Runtime::StopOfferService<ServiceInterface>()
         *       → binding->StopOfferService → CRegistryProxy::UnregisterService
         *       Clears binding context after stopping.
         */
        void doStopOfferService() noexcept override
        {
            Runtime::StopOfferService< ServiceInterface > (
                GetInstanceSpecifier() );

            // Clear binding context
            setBindingContext( CBindingContext{} );
        }
    };

} // namespace com
} // namespace lap

#endif // LAP_COM_SKELETON_BASE_HPP
