/**
 * @file        StaticInstanceManager.hpp
 * @author      Aii
 * @brief       AUTOSAR R25-11 Static Instance Manager (SWS_CM_02201–02203)
 * @date        2026/02/16
 * @details     Manages static service instances configured via YAML manifest.
 *              Static instances bypass Service Discovery and are available
 *              immediately at startup with zero latency.
 *              - SWS_CM_02201: Static service connection via manifest
 *              - SWS_CM_02202: Bypass SD for statically configured instances
 *              - SWS_CM_02203: No runtime version check for static connections
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/16  <td>1.0      <td>Aii     <td>Initial implementation
 * </table>
 */
#ifndef LAP_COM_STATIC_INSTANCE_MANAGER_HPP
#define LAP_COM_STATIC_INSTANCE_MANAGER_HPP

// ==================== Project-Internal Headers ====================
#include "ComTypes.hpp"
#include "ServiceDiscovery.hpp"

// ==================== Cross-Module Headers ====================
#include <core/CResult.hpp>
#include <core/CSync.hpp>
#include <core/CTypedef.hpp>
#include <core/CString.hpp>
#include <core/COptional.hpp>

// ==================== Standard Library Headers ====================
#include <chrono>
#include <memory>

namespace lap
{
namespace com
{
namespace discovery
{
    // ========================================================================
    // Type aliases
    // ========================================================================
    using lap::core::Result;
    using lap::core::Optional;
    using lap::core::String;
    using lap::core::Bool;
    using lap::core::UInt32;
    using lap::core::UInt64;
    using lap::core::Vector;
    using lap::core::Mutex;
    using lap::core::ScopedLock;

    // ========================================================================
    // Static Instance State
    // ========================================================================

    /**
     * @brief Lifecycle state of a static service instance
     */
    enum class StaticInstanceState : lap::core::UInt8
    {
        kConfigured = 0,    ///< Loaded from manifest, not yet activated
        kActive     = 1,    ///< Binding resolved, ready for proxy/skeleton use
        kFailed     = 2,    ///< Binding resolution or activation failed
        kSuspended  = 3,    ///< Temporarily suspended (e.g., health check failed)
    };

    /**
     * @brief Runtime information for a managed static instance
     */
    struct StaticInstanceRecord final
    {
        StaticServiceEndpoint       m_endpoint;                 ///< Configuration from manifest
        StaticInstanceState         m_state{ StaticInstanceState::kConfigured };
        std::chrono::steady_clock::time_point m_activationTime; ///< When activated
        UInt32                      m_iActivationAttempts{ 0 }; ///< Retry count
        String                      m_strLastError;             ///< Last error message
    };

    // ========================================================================
    // Static Instance Manager (SWS_CM_02201–02203)
    // ========================================================================

    /**
     * @brief Manages lifecycle of statically configured service instances
     *
     * @details Per AUTOSAR R25-11:
     *   - SWS_CM_02201: Static service connection via manifest
     *   - SWS_CM_02202: Bypass SD for statically configured instances
     *   - SWS_CM_02203: No runtime version check for static connections
     *
     * Lifecycle: LoadManifest() → ActivateAll() → Find/Query → Shutdown()
     *
     * Thread-safety: All public methods are thread-safe.
     */
    class StaticInstanceManager final
    {
    public:
        /**
         * @brief Configuration for the StaticInstanceManager
         */
        struct Config final
        {
            String              m_strManifestPath{ "/etc/lap/com/static_endpoints.yaml" };
            UInt32              m_iMaxActivationRetries{ 3 };
            Bool                m_bActivateOnLoad{ true };      ///< Auto-activate after load
        };

        /**
         * @brief Statistics for monitoring
         */
        struct Statistics final
        {
            UInt64              m_iTotalInstances{ 0 };
            UInt64              m_iActiveInstances{ 0 };
            UInt64              m_iFailedInstances{ 0 };
            UInt64              m_iSuspendedInstances{ 0 };
            UInt64              m_iLookupCount{ 0 };
            UInt64              m_iLookupHits{ 0 };
        };

    public:
        ~StaticInstanceManager() noexcept;

        // Non-copyable, non-movable
        StaticInstanceManager( const StaticInstanceManager& )            = delete;
        StaticInstanceManager& operator=( const StaticInstanceManager& ) = delete;
        StaticInstanceManager( StaticInstanceManager&& )                 = delete;
        StaticInstanceManager& operator=( StaticInstanceManager&& )      = delete;

        /**
         * @brief Create a StaticInstanceManager
         * @param config Configuration
         * @return Result containing manager instance
         */
        static Result< std::unique_ptr< StaticInstanceManager > >
        Create( const Config& config ) noexcept;

        /**
         * @brief Create a StaticInstanceManager with default config
         * @return Result containing manager instance
         */
        static Result< std::unique_ptr< StaticInstanceManager > >
        Create() noexcept
        {
            return Create( Config{} );
        }

        // ============================================================
        // Manifest Loading
        // ============================================================

        /**
         * @brief Load static endpoints from YAML manifest
         * @param manifestPath Override path (empty = use config path)
         * @return Result with number of endpoints loaded
         * @note SWS_CM_02201: Static service connection manifest
         */
        Result< UInt32 > LoadManifest(
            const String& manifestPath = "" ) noexcept;

        /**
         * @brief Reload manifest (hot-reload support)
         * @return Result with number of endpoints reloaded
         */
        Result< UInt32 > ReloadManifest() noexcept;

        // ============================================================
        // Instance Activation (SWS_CM_02202)
        // ============================================================

        /**
         * @brief Activate all loaded static instances
         * @return Result with count of successfully activated instances
         * @note SWS_CM_02202: Bypasses Service Discovery
         */
        Result< UInt32 > ActivateAll() noexcept;

        /**
         * @brief Activate a specific instance by service name + instance ID
         * @param serviceName Service interface name
         * @param instanceId Instance identifier
         * @return Result indicating success
         */
        Result< void > ActivateInstance(
            const String& serviceName,
            InstanceIdentifierType instanceId ) noexcept;

        /**
         * @brief Suspend a specific instance (e.g., for maintenance)
         * @param serviceName Service interface name
         * @param instanceId Instance identifier
         * @return Result indicating success
         */
        Result< void > SuspendInstance(
            const String& serviceName,
            InstanceIdentifierType instanceId ) noexcept;

        // ============================================================
        // Lookup (SWS_CM_02203: No version check)
        // ============================================================

        /**
         * @brief Find static instances by service name
         * @param serviceName Service interface name
         * @param instanceFilter Optional instance ID filter
         * @return Vector of matching ServiceInstanceInfo
         * @note SWS_CM_02203: No runtime version check for static connections
         */
        Result< Vector< ServiceInstanceInfo > >
        FindInstances( const String& serviceName,
                       const Optional< InstanceIdentifierType >& instanceFilter =
                           Optional< InstanceIdentifierType >() ) noexcept;

        /**
         * @brief Check if a specific instance is statically configured
         * @param serviceName Service interface name
         * @param instanceId Instance identifier
         * @return true if the instance exists in static configuration
         */
        Bool IsStaticInstance( const String& serviceName,
                               InstanceIdentifierType instanceId ) const noexcept;

        // ============================================================
        // Monitoring
        // ============================================================

        /**
         * @brief Get all managed instance records
         * @return Vector of instance records
         */
        Vector< StaticInstanceRecord > GetAllRecords() const noexcept;

        /**
         * @brief Get statistics
         * @return Current statistics snapshot
         */
        Statistics GetStatistics() const noexcept;

        /**
         * @brief Health check — at least one instance is Active
         * @return true if healthy
         */
        Bool IsHealthy() const noexcept;

        /**
         * @brief Shutdown: deactivate all instances
         */
        void Shutdown() noexcept;

    private:
        explicit StaticInstanceManager( const Config& config );

        /**
         * @brief Try to activate a single record (resolve binding)
         * @param record Instance record to activate
         * @return Result indicating success
         */
        Result< void > activateRecord( StaticInstanceRecord& record ) noexcept;

        /**
         * @brief Convert StaticServiceEndpoint → ServiceInstanceInfo
         * @param endpoint Static endpoint configuration
         * @return ServiceInstanceInfo ready for use
         * @note SWS_CM_02203: version fields left at 0 (no version check)
         */
        static ServiceInstanceInfo
        toServiceInstanceInfo( const StaticServiceEndpoint& endpoint ) noexcept;

    private:
        Config                                  m_config;
        Vector< StaticInstanceRecord >          m_vecRecords;
        mutable Mutex                           m_mutex;
        mutable Mutex                           m_statsMutex;
        mutable Statistics                      m_statistics;
        Bool                                    m_bInitialized{ false };
    };

} // namespace discovery
} // namespace com
} // namespace lap

#endif // LAP_COM_STATIC_INSTANCE_MANAGER_HPP
