/**
 * @file        Runtime.hpp
 * @author      Aii
 * @brief       AUTOSAR Adaptive Platform Communication Management Runtime
 * @date        2026/02/07
 * @details     Service discovery and lifecycle management (SWS_CM Section 8.2, 10.1).
 *              PIMPL idiom hides implementation details (registry, heartbeat, binding).
 *              All low-level registry operations are encapsulated as member methods.
 *              Runtime ↔ BindingManager ↔ ServiceDiscoveryManager integration.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §2.4 (Runtime lifecycle)
 *              AUTOSAR_AP_SWS_CommunicationManagement.pdf §8.2
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/10/30  <td>1.0      <td>ddkv587         <td>Initial implementation
 * <tr><td>2025/11/20  <td>2.0      <td>LightAP Team    <td>Registry integration
 * <tr><td>2026/02/07  <td>3.0      <td>Aii             <td>PIMPL refactoring, internalize registry APIs
 * <tr><td>2026/02/07  <td>4.0      <td>Aii             <td>BindingManager + ServiceDiscovery integration
 * </table>
 */
#ifndef LAP_COM_RUNTIME_HPP
#define LAP_COM_RUNTIME_HPP

// ==================== Project-Internal Headers ====================
#include "ComTypes.hpp"
#include "ServiceHandleType.hpp"

// ==================== Cross-Module Headers ====================
#include <core/CInstanceSpecifier.hpp>
#include <core/CMacroDefine.hpp>
#include <core/CResult.hpp>
#include <core/COptional.hpp>
#include <core/CTypedef.hpp>
#include <core/CSync.hpp>
#include <core/CFunction.hpp>

// ==================== Standard Library Headers ====================
#include <memory>
#include <string>
#include <cstdlib>

namespace lap
{
namespace com
{
    // Forward declarations
    namespace registry
    {
        class CRegistryProxy;
        struct ServiceSlot;
    } // namespace registry

    namespace binding
    {
        class ITransportBinding;
        class BindingManager;
    } // namespace binding

    // ========================================================================
    // Runtime Singleton (PIMPL idiom — SWS_CM_00400)
    // ========================================================================

    /**
     * @brief Communication Management Runtime (Singleton + PIMPL)
     *
     * @details Central class for ara::com initialization, service discovery,
     *          and registry operations.
     *          Uses PIMPL idiom to:
     *          - Hide implementation details (registry, heartbeat thread, etc.)
     *          - Reduce compile-time dependencies
     *          - Eliminate global statics anti-pattern
     *
     *          Integration chain:
     *          Application → Runtime → BindingManager → ITransportBinding → Network
     *                                 └→ CRegistryProxy → SharedMemory Registry
     *
     * @note SWS_CM_00400
     * @note Thread-safe: All public methods are safe for concurrent access
     */
    class LAP_COM_API Runtime final
    {
    public:
        // ================================================================
        // Lifecycle Management (SWS_CM_00122)
        // ================================================================

        /**
         * @brief Initialize the Communication Management Runtime
         * @return Result indicating success or error
         * @note SWS_CM_00401
         * @note Must be called before any other Runtime APIs
         * @note Returns kInvalidState if already initialized
         * @note Initializes: CRegistryProxy → BindingManager → Heartbeat
         */
        static Result< void > Initialize() noexcept;

        /**
         * @brief Initialize with explicit configuration path
         * @param configPath Path to binding configuration YAML
         * @return Result indicating success or error
         */
        static Result< void > Initialize( const lap::core::String& configPath ) noexcept;

        /**
         * @brief Deinitialize the Communication Management Runtime
         * @return Result indicating success or error
         * @note SWS_CM_00402
         * @note Blocks until heartbeat thread terminates (< 100ms)
         * @note Shutdown order: Heartbeat → BindingManager → CRegistryProxy
         */
        static Result< void > Deinitialize() noexcept;

        /**
         * @brief Check if runtime is initialized
         * @return true if initialized, false otherwise
         */
        static Bool IsInitialized() noexcept;

        /**
         * @brief Get the singleton Runtime instance
         * @return Reference to Runtime instance
         * @note Internal API for subsystem access
         */
        static Runtime& GetInstance() noexcept;

        // ================================================================
        // Service Discovery APIs (SWS_CM Section 8.2)
        // ================================================================

        /**
         * @brief Find service instances (synchronous, template)
         * @tparam ServiceInterface Type of service interface (must expose kServiceId)
         * @param instanceIdentifier Instance specifier for the service
         * @return Container of service handles
         * @note [SWS_CM_00122] — Three-tier lookup:
         *       1. Registry shared-memory (< 500ns)
         *       2. BindingManager.FindService per active binding
         *       3. Combine results into HandleContainer
         */
        template< typename ServiceInterface  >
        static ServiceHandleContainer< typename ServiceInterface::HandleType > FindService(
            lap::core::InstanceSpecifier instanceIdentifier ) noexcept
        {
            using HandleType = typename ServiceInterface::HandleType;
            ServiceHandleContainer< HandleType > result;

            if ( !IsInitialized() )
            {
                return result;
            }

            // Resolve InstanceSpecifier to optional numeric instance filter.
            // Per SWS_CM_00122, the InstanceSpecifier selects which service instances
            // are visible. Convention: if the last path segment is a pure integer,
            // it is treated as an instance-ID filter; otherwise all instances match.
            const auto specStr = instanceIdentifier.ToString();
            lap::core::Optional< InstanceIdentifierType > instanceFilter;

            {
                auto pos = specStr.rfind( '/' );
                auto lastSeg = ( pos != lap::core::StringView::npos )
                                   ? specStr.substr( pos + 1 )
                                   : specStr;
                if ( !lastSeg.empty() )
                {
                    Bool allDigits = true;
                    for ( auto ch : lastSeg )
                    {
                        if ( ch < '0' || ch > '9' )
                        {
                            allDigits = false;
                            break;
                        }
                    }
                    if ( allDigits )
                    {
                        // String temporary for strtoul
                        String numStr( lastSeg.data(), lastSeg.size() );
                        instanceFilter = static_cast< InstanceIdentifierType > (
                            std::strtoul( numStr.c_str(), nullptr, 10 ) );
                    }
                }
            }

            // Layer 1: Registry shared-memory O(1) lookup
            auto& runtime = GetInstance();
            auto slotOpt = runtime.FindServiceById( ServiceInterface::kServiceId );
            if ( slotOpt.has_value() )
            {
                auto& slot = slotOpt.value();
                auto instId = static_cast< InstanceIdentifierType > ( slot.m_instanceId );

                if ( !instanceFilter.has_value() || instanceFilter.value() == instId )
                {
                    HandleType handle( instId,
                                       ServiceVersionType( static_cast< lap::core::UInt8 > ( slot.m_majorVersion ),
                                                           slot.m_minorVersion ) );
                    result.push_back( handle );
                }
            }

            // Layer 2: Ask active bindings for additional instances
            auto bindingResults = runtime.findServiceViaBindings(
                static_cast< lap::core::UInt64 > ( ServiceInterface::kServiceId ) );
            for ( auto rawInstanceId : bindingResults )
            {
                auto instId = static_cast< InstanceIdentifierType > ( rawInstanceId );

                // Apply instance filter
                if ( instanceFilter.has_value() && instanceFilter.value() != instId )
                {
                    continue;
                }

                HandleType handle( instId );
                // Deduplicate
                Bool duplicate = false;
                for ( const auto& existing : result )
                {
                    if ( existing.GetInstanceId() == handle.GetInstanceId() )
                    {
                        duplicate = true;
                        break;
                    }
                }
                if ( !duplicate )
                {
                    result.push_back( handle );
                }
            }

            return result;
        }

        /**
         * @brief Find service instances (asynchronous with callback)
         * @tparam ServiceInterface Type of service interface
         * @param handler Callback function for service availability
         * @param instanceIdentifier Instance specifier for the service
         * @return Result containing FindServiceHandle for managing the search
         * @note [SWS_CM_00623] — StartFindService with InstanceSpecifier
         */
        template< typename ServiceInterface  >
        static Result< FindServiceHandle > StartFindService(
            FindServiceHandler< typename ServiceInterface::HandleType > handler,
            lap::core::InstanceSpecifier instanceIdentifier ) noexcept
        {
            if ( !IsInitialized() )
            {
                return Result< FindServiceHandle >::FromError(
                    MakeErrorCode( ComErrc::kNotInitialized, 0 ) );
            }

            auto& runtime = GetInstance();
            return runtime.startFindServiceImpl(
                ServiceInterface::kServiceId,
                std::move( instanceIdentifier ),
                [handler]( lap::core::UInt64 serviceId,
                           lap::core::Vector< lap::core::UInt64 > instances,
                           FindServiceHandle fsh ) {
                    using HandleType = typename ServiceInterface::HandleType;
                    ServiceHandleContainer< HandleType > handles;
                    for ( auto instId : instances )
                    {
                        handles.emplace_back(
                            static_cast< InstanceIdentifierType > ( instId ) );
                    }
                    handler( std::move( handles ), fsh );
                } );
        }

        /**
         * @brief Stop finding service instances
         * @param handle FindServiceHandle returned by StartFindService
         * @note [SWS_CM_00125]
         */
        static void StopFindService( FindServiceHandle handle ) noexcept;

        // ================================================================
        // Runtime Utility APIs [SWS_CM_00118]
        // ================================================================

        /**
         * @brief Resolve InstanceSpecifier to InstanceIdentifierContainer
         * @param metaModelIdentifier The InstanceSpecifier to resolve
         * @return Container of resolved InstanceIdentifiers, or error
         * @note [SWS_CM_00118] — ara::com::runtime::ResolveInstanceIDs
         */
        static Result< InstanceIdentifierContainer > ResolveInstanceIDs(
            lap::core::InstanceSpecifier metaModelIdentifier ) noexcept;

        // ================================================================
        // Service Offering APIs (SWS_CM Section 8.3)
        // ================================================================

        /**
         * @brief Offer a service instance
         * @tparam ServiceInterface Type of service interface (must expose kServiceId)
         * @param instanceIdentifier Instance specifier for the service
         * @return Result indicating success or error
         * @note [SWS_CM_00101]
         * @note Flow: Register to Registry → OfferService on best binding
         */
        template< typename ServiceInterface  >
        static Result< void > OfferService(
            lap::core::InstanceSpecifier instanceIdentifier ) noexcept
        {
            if ( !IsInitialized() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kNotInitialized, 0 ) );
            }

            auto& runtime = GetInstance();
            return runtime.offerServiceImpl(
                static_cast< lap::core::UInt64 > ( ServiceInterface::kServiceId ),
                std::move( instanceIdentifier ) );
        }

        /**
         * @brief Stop offering a service instance
         * @tparam ServiceInterface Type of service interface
         * @param instanceIdentifier Instance specifier for the service
         * @note [SWS_CM_00111]
         * @note Flow: StopOffer on binding → Unregister from Registry
         */
        template< typename ServiceInterface  >
        static void StopOfferService(
            lap::core::InstanceSpecifier instanceIdentifier ) noexcept
        {
            if ( !IsInitialized() )
            {
                return;
            }

            auto& runtime = GetInstance();
            runtime.stopOfferServiceImpl(
                static_cast< lap::core::UInt64 > ( ServiceInterface::kServiceId ),
                std::move( instanceIdentifier ) );
        }

        // ================================================================
        // Registry Integration (internal, used by binding layer)
        // ================================================================

        /**
         * @brief Register a service instance to the registry
         * @param serviceId Service ID (0x0001–0x3FFF QM+AB, 0xF000–0xFFFF ASIL)
         * @param instanceId Instance ID (0x0001–0xFFFE)
         * @param networkBinding Network binding type (0=coreipc, 1=dds, 2=socket, 3=dbus, 4=someip)
         * @return Result indicating success or error
         * @note SWS_CM_00001 OfferService backend
         */
        Result< void > RegisterService(
            lap::core::UInt16 serviceId,
            lap::core::UInt16 instanceId,
            lap::core::UInt8 networkBinding ) noexcept;

        /**
         * @brief Find a service by ID (lock-free shared memory lookup)
         * @param serviceId Service ID to search for
         * @return Optional containing ServiceSlot if found, empty otherwise
         * @note SWS_CM_00002 FindService backend
         * @note Performance: O(1) lookup, P99 < 500ns
         */
        lap::core::Optional< registry::ServiceSlot > FindServiceById(
            lap::core::UInt16 serviceId ) noexcept;

        /**
         * @brief Unregister a service instance from the registry
         * @param serviceId Service ID
         * @return Result indicating success or error
         * @note SWS_CM_00003 StopOfferService backend
         */
        Result< void > UnregisterService( lap::core::UInt16 serviceId ) noexcept;

        /**
         * @brief Get registry proxy for direct subsystem access
         * @return Pointer to CRegistryProxy, or nullptr if not initialized
         */
        registry::CRegistryProxy* GetRegistry() noexcept;

        /**
         * @brief Get binding manager for direct subsystem access
         * @return Reference to the global BindingManager singleton
         */
        static binding::BindingManager& GetBindingManager() noexcept;

        // ================================================================
        // Deleted special members (AUTOSAR C++ A12-8-6)
        // ================================================================
        Runtime( const Runtime& )            = delete;
        Runtime& operator=( const Runtime& ) = delete;
        Runtime( Runtime&& )                 = delete;
        Runtime& operator=( Runtime&& )      = delete;

    private:
        Runtime() noexcept;
        ~Runtime() noexcept;

        // ================================================================
        // Internal implementation methods (non-template, in .cpp)
        // ================================================================

        /**
         * @brief Initialize with configuration path (called by static Initialize)
         */
        Result< void > doInitialize( const lap::core::String& configPath ) noexcept;

        /**
         * @brief Synchronous binding-level FindService
         * @param serviceId AUTOSAR service ID
         * @return Vector of discovered instance IDs
         */
        lap::core::Vector< lap::core::UInt64 > findServiceViaBindings(
            lap::core::UInt64 serviceId ) noexcept;

        /**
         * @brief Type-erased StartFindService implementation
         * @param serviceId AUTOSAR service ID
         * @param instanceSpec Instance specifier
         * @param callback Type-erased callback
         * @return Result containing FindServiceHandle
         */
        using InternalFindCallback = Function< void(
            lap::core::UInt64,
            lap::core::Vector< lap::core::UInt64 >,
            FindServiceHandle ) >;

        Result< FindServiceHandle > startFindServiceImpl(
            lap::core::UInt16 serviceId,
            lap::core::InstanceSpecifier instanceSpec,
            InternalFindCallback callback ) noexcept;

        /**
         * @brief Type-erased OfferService implementation
         */
        Result< void > offerServiceImpl(
            lap::core::UInt64 serviceId,
            lap::core::InstanceSpecifier instanceSpec ) noexcept;

        /**
         * @brief Type-erased StopOfferService implementation
         */
        void stopOfferServiceImpl(
            lap::core::UInt64 serviceId,
            lap::core::InstanceSpecifier instanceSpec ) noexcept;

        struct Impl;                              ///< PIMPL — defined in Runtime.cpp
        UniqueHandle< Impl > m_pImpl;             ///< Owning pointer to implementation

        static Mutex           s_mutex;
        static Atomic< Bool >      s_initialized;
    };

} // namespace com
} // namespace lap

#endif // LAP_COM_RUNTIME_HPP
