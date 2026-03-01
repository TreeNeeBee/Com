/**
 * @file        BindingManager.hpp
 * @author      LightAP Development Team
 * @brief       Dynamic binding manager for ara::com transport layer
 * @date        2025-11-21
 * @details     Manages multiple transport bindings (CoreIPC, DDS, SOME/IP, Socket, D-Bus)
 *              with dynamic loading and priority-based selection.
 *              Supports YAML configuration for binding priority and static mapping.
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R25-11 Compliance:
 *              - SWS_CM_00401: Transport Binding Selection
 *              - SWS_CM_00402: Dynamic Binding Management
 *              - SWS_CM_00403: Binding Configuration
 * @reference   IMPLEMENTATION_PLAN_UPDATED.md Phase 2
 *              SERVICE_DISCOVERY_ARCHITECTURE.md §3.2 Binding层
 *              AUTOSAR_AP_SWS_CommunicationManagement.pdf §8.3
 * sdk:
 * platform:    Linux 5.10+
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/21  <td>1.0      <td>LightAP Team    <td>Initial binding manager implementation
 * <tr><td>2026/02/09  <td>2.0      <td>Aii             <td>code_rules compliance: m_ prefix, camelCase, spaced formatting
 * </table>
 */
#ifndef LAP_COM_BINDING_MANAGER_HPP
#define LAP_COM_BINDING_MANAGER_HPP

#include "../common/ITransportBinding.hpp"
#include "../common/BindingTypes.hpp"
#include "ComTypes.hpp"

#include <lap/core/CTypedef.hpp>
#include <lap/core/CSync.hpp>
#include <lap/core/CResult.hpp>
#include <lap/core/COptional.hpp>
#include <lap/core/CString.hpp>

#include <unordered_map>
#include <dlfcn.h>

namespace lap
{
namespace com
{
namespace binding
{
    using lap::core::Result;
    using lap::core::Optional;
    using lap::core::String;
    using lap::core::Bool;
    using lap::core::UInt32;
    using lap::core::UInt64;
    using lap::core::Double;
    using lap::core::Vector;
    using lap::core::Map;
    using lap::core::SharedHandle;
    using lap::core::Mutex;
    using lap::core::LockGuard;

    /**
     * @brief Binding priority enumeration (higher value = higher priority)
     * @note Default priority order (IMPLEMENTATION_PLAN_UPDATED.md):
     *       1. CoreIPC (priority 100) - lowest latency for IPC
     *       2. DDS (priority 80) - network communication
     *       3. SOME/IP (priority 60) - automotive standard
     *       4. Socket (priority 40) - fallback for testing
     *       5. D-Bus (priority 20) - legacy integration
     */
    enum class BindingPriority : UInt32
    {
        kCoreIpc  = 100,  ///< CoreIPC zero-copy shared-memory IPC (< 1µs latency)
        kDds      = 80,   ///< DDS via FastDDS (< 15µs latency)
        kSomeip   = 60,   ///< SOME/IP automotive binding
        kSocket   = 40,   ///< Socket-based fallback
        kDbus     = 20,   ///< D-Bus legacy binding
        kCustom   = 10    ///< Custom protocol binding
    };

    /**
     * @brief Binding configuration structure
     * @note Parsed from YAML configuration file
     */
    struct BindingConfig
    {
        String name;                ///< Binding name ("coreipc", "dds", "someip", etc.)
        BindingPriority priority;   ///< Selection priority
        String libraryPath;         ///< Shared library path (e.g., "liblap_binding_coreipc.so")
        Bool enabled;               ///< Enable/disable flag
        Map< String, String > parameters;  ///< Binding-specific parameters

        BindingConfig()
            : name( "" ),
              priority( BindingPriority::kCustom ),
              libraryPath( "" ),
              enabled( false ) {}
    };

    /**
     * @brief Static service-to-binding mapping entry
     * @note Allows override default priority-based selection for specific services
     */
    struct StaticBindingMapping
    {
        UInt64 serviceId;          ///< Service ID (AUTOSAR service identifier)
        UInt64 instanceId;         ///< Instance ID (default 0 = all instances)
        String bindingName;        ///< Forced binding name

        StaticBindingMapping()
            : serviceId( 0 ), instanceId( 0 ), bindingName( "" ) {}
    };

    /**
     * @brief Binding plugin factory function types
     */
    using CreateBindingFunc = ITransportBinding* (*)();
    using DestroyBindingFunc = void (*)(ITransportBinding*);
    using GetBindingNameFunc = const char* (*)();
    using GetBindingVersionFunc = UInt32 (*)();

    /**
     * @brief Dynamic transport binding manager
     * 
     * @details Design rationale:
     *          - Plugin architecture: Bindings loaded as .so files
     *          - Priority-based selection: Automatic fallback if preferred binding unavailable
     *          - Static mapping: Override priority for specific services (e.g., safety-critical)
     *          - Thread-safe: Mutex-protected binding registry
     * 
     * @note Singleton pattern: Use GetInstance() to access
     * 
     * @example Usage:
     *          auto& manager = BindingManager::GetInstance();
     *          manager.LoadConfiguration("/etc/lap/com/bindings.yaml");
     *          auto* binding = manager.SelectBinding(0x1234, 0x0001);
     *          binding->SendEvent(...);
     */
    class LAP_COM_API BindingManager final
    {
    public:
        /**
         * @brief Get singleton instance
         * @return Reference to global BindingManager
         */
        static BindingManager& GetInstance() noexcept;

        // Delete copy/move constructors (singleton)
        BindingManager(const BindingManager&) = delete;
        BindingManager& operator=(const BindingManager&) = delete;
        BindingManager(BindingManager&&) = delete;
        BindingManager& operator=(BindingManager&&) = delete;

        /**
         * @brief Load binding configuration from YAML file
         * @param configPath Path to YAML configuration (e.g., "/etc/lap/com/bindings.yaml")
         * @return Result< void > Success or error code
         * 
         * @note YAML format example:
         *       bindings:
         *         - name: coreipc
         *           priority: 100
         *           library: /usr/lib/lap/com/liblap_binding_coreipc.so
         *           enabled: true
         *         - name: dds
         *           priority: 80
         *           library: /usr/lib/lap/com/liblap_binding_dds.so
         *           enabled: true
         *       static_mappings:
         *         - serviceId: 0xF001
         *           binding: coreipc  # Force ASIL-D to use CoreIPC
         */
        Result< void > LoadConfiguration( const String& configPath ) noexcept;

        /**
         * @brief Manually register a binding (without dynamic loading)
         * @param config Binding configuration
         * @param binding Pre-constructed binding instance
         * @return Result<void> Success or error code
         * 
         * @note For unit testing or statically linked bindings
         */
        Result< void > RegisterBinding(
            const BindingConfig& config,
            SharedHandle< ITransportBinding > binding
        ) noexcept;

        /**
         * @brief Load a binding from shared library
         * @param config Binding configuration (must include libraryPath)
         * @return Result< void > Success or error code
         * 
         * @details Steps:
         *          1. dlopen(config.libraryPath, RTLD_LAZY | RTLD_LOCAL)
         *          2. dlsym("CreateBindingInstance")
         *          3. Call factory function to create instance
         *          4. binding->Initialize(config.parameters)
         *          5. Store in registry with priority key
         */
        Result< void > LoadBinding( const BindingConfig& config ) noexcept;

        /**
         * @brief Unload a binding and close library handle
         * @param name Binding name
         * @return Result<void> Success or error code
         */
        Result< void > UnloadBinding( const String& name ) noexcept;

        /**
         * @brief Select binding for a service (priority-based or static mapping)
         * @param serviceId AUTOSAR service ID
         * @param instanceId AUTOSAR instance ID (default 0 = any instance)
         * @return ITransportBinding* Pointer to selected binding or nullptr
         * 
         * @details Selection algorithm:
         *          1. Check m_staticMappings for explicit serviceId match
         *          2. If no match, iterate m_bindings by priority (descending)
         *          3. Return first enabled binding
         *          4. Return nullptr if no binding available
         * 
         * @note Thread-safe (read lock on m_mutex)
         */
        ITransportBinding* SelectBinding(
            UInt64 serviceId,
            UInt64 instanceId = 0
        ) noexcept;

        /**
         * @brief Get binding by name
         * @param name Binding name
         * @return Optional<ITransportBinding*> Binding pointer or nullopt
         */
        Optional< ITransportBinding* > GetBinding( const String& name ) const noexcept;

        /**
         * @brief Get all loaded bindings (for diagnostics)
         * @return std::vector<std::string> List of binding names
         */
        Vector< String > GetLoadedBindings() const noexcept;

        /**
         * @brief Check health status of a specific binding
         * @param name Binding name
         * @return Optional<BindingHealth> Health status if binding exists
         * 
         * @note Used for fault detection and automatic failover
         * @note Returns metrics like error_rate, latency, availability
         */
        Optional< BindingHealth > GetBindingHealth( const String& name ) const noexcept;

        /**
         * @brief Get performance metrics for a specific binding
         * @param name Binding name
         * @return Optional<TransportMetrics> Metrics if binding exists
         * 
         * @note ARCHITECTURE_SUMMARY.md §7.2: Performance monitoring
         * @note Includes message counts, latency statistics, bandwidth
         */
        Optional< TransportMetrics > GetBindingMetrics( const String& name ) const noexcept;

        /**
         * @brief Get aggregated metrics for all loaded bindings
         * @return std::map<std::string, TransportMetrics> Metrics per binding
         * 
         * @note Used by diagnostic tools and monitoring systems
         */
        Map< String, TransportMetrics > GetAllMetrics() const noexcept;

        /**
         * @brief Reload configuration file and update bindings
         * @param configPath Path to YAML configuration file
         * @return Result< void > Success or error code
         * 
         * @note Hot reload: unload disabled bindings, load new enabled bindings
         * @note Existing connections are preserved if binding remains enabled
         * @warning Thread-safe but may cause brief service disruption
         */
        Result< void > ReloadConfiguration( const String& configPath ) noexcept;

        /**
         * @brief Check if a binding supports zero-copy communication
         * @param name Binding name
         * @return bool True if supports zero-copy (e.g., CoreIPC)
         */
        Bool SupportsZeroCopy( const String& name ) const noexcept;

        /**
         * @brief Get priority of a specific binding
         * @param name Binding name
         * @return Optional<uint32_t> Priority value (100=highest, 20=lowest)
         */
        Optional< UInt32 > GetBindingPriority( const String& name ) const noexcept;

        /**
         * @brief Shutdown all bindings and unload libraries
         * @return Result<void> Success or error code
         */
        Result< void > Shutdown() noexcept;

    private:
        /**
         * @brief Private constructor (singleton)
         */
        BindingManager() noexcept = default;

        /**
         * @brief Destructor (calls Shutdown)
         */
        ~BindingManager() noexcept;

        /**
         * @brief Parse YAML configuration file
         * @param configPath YAML file path
         * @return Result< Vector< BindingConfig > > Parsed configurations
         */
        Result< Vector< BindingConfig > > parseYamlConfig(
            const String& configPath,
            Vector< StaticBindingMapping >& outMappings
        ) noexcept;

        /**
         * @brief Find static binding mapping for service
         * @param serviceId Service ID
         * @param instanceId Instance ID
         * @return Optional< String > Binding name or nullopt
         */
        Optional< String > findStaticMapping(
            UInt64 serviceId,
            UInt64 instanceId
        ) const noexcept;

        // ========================================================================
        // Member Variables
        // ========================================================================

        /// Mutex for thread-safe access
        mutable Mutex m_mutex;

        /// Binding registry (sorted by priority, descending)
        /// Key: priority (UInt32), Value: binding pointer
        /// Note: std::multimap allows multiple bindings with same priority
        std::multimap< UInt32, SharedHandle< ITransportBinding >, std::greater< UInt32 > > m_bindings;

        /// Binding lookup by name
        std::unordered_map< String, SharedHandle< ITransportBinding > > m_bindingsByName;

        /// Library handles (for dlclose on shutdown)
        std::unordered_map< String, void* > m_libraryHandles;

        /// Static service-to-binding mappings
        Vector< StaticBindingMapping > m_staticMappings;
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_BINDING_MANAGER_HPP
