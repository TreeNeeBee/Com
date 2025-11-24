/**
 * @file        BindingManager.hpp
 * @author      LightAP Development Team
 * @brief       Dynamic binding manager for ara::com transport layer
 * @date        2025-11-21
 * @details     Manages multiple transport bindings (iceoryx2, DDS, SOME/IP, Socket, D-Bus)
 *              with dynamic loading and priority-based selection.
 *              Supports YAML configuration for binding priority and static mapping.
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R24-11 Compliance:
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
 * </table>
 */
#ifndef LAP_COM_BINDING_MANAGER_HPP
#define LAP_COM_BINDING_MANAGER_HPP

#include "../common/ITransportBinding.hpp"
#include "../common/BindingTypes.hpp"

#include <lap/core/CResult.hpp>
#include <lap/core/COptional.hpp>
#include <lap/core/CString.hpp>

#include <map>
#include <unordered_map>
#include <string>
#include <memory>
#include <vector>
#include <mutex>
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

    /**
     * @brief Binding priority enumeration (higher value = higher priority)
     * @note Default priority order (IMPLEMENTATION_PLAN_UPDATED.md):
     *       1. iceoryx2 (priority 100) - lowest latency for IPC
     *       2. DDS (priority 80) - network communication
     *       3. SOME/IP (priority 60) - automotive standard
     *       4. Socket (priority 40) - fallback for testing
     *       5. D-Bus (priority 20) - legacy integration
     */
    enum class BindingPriority : uint32_t
    {
        ICEORYX2 = 100,  ///< iceoryx2 zero-copy IPC (< 1µs latency)
        DDS      = 80,   ///< DDS over AF_XDP (< 15µs latency)
        SOMEIP   = 60,   ///< SOME/IP automotive binding
        SOCKET   = 40,   ///< Socket-based fallback
        DBUS     = 20,   ///< D-Bus legacy binding
        CUSTOM   = 10    ///< Custom protocol binding
    };

    /**
     * @brief Binding configuration structure
     * @note Parsed from YAML configuration file
     */
    struct BindingConfig
    {
        std::string name;           ///< Binding name ("iceoryx2", "dds", "someip", etc.)
        BindingPriority priority;   ///< Selection priority
        std::string library_path;   ///< Shared library path (e.g., "liblap_binding_iceoryx2.so")
        bool enabled;               ///< Enable/disable flag
        std::map<std::string, std::string> parameters;  ///< Binding-specific parameters

        BindingConfig()
            : name(""),
              priority(BindingPriority::CUSTOM),
              library_path(""),
              enabled(false) {}
    };

    /**
     * @brief Static service-to-binding mapping entry
     * @note Allows override default priority-based selection for specific services
     */
    struct StaticBindingMapping
    {
        uint64_t service_id;        ///< Service ID (AUTOSAR service identifier)
        uint64_t instance_id;       ///< Instance ID (default 0 = all instances)
        std::string binding_name;   ///< Forced binding name

        StaticBindingMapping()
            : service_id(0), instance_id(0), binding_name("") {}
    };

    /**
     * @brief Binding plugin factory function types
     */
    using CreateBindingFunc = ITransportBinding* (*)();
    using DestroyBindingFunc = void (*)(ITransportBinding*);
    using GetBindingNameFunc = const char* (*)();
    using GetBindingVersionFunc = uint32_t (*)();

    /**
     * @brief Binding manager errors
     */
    enum class BindingManagerError : uint32_t
    {
        SUCCESS               = 0,
        CONFIG_LOAD_FAILED    = 1,  ///< Failed to load YAML configuration
        LIBRARY_LOAD_FAILED   = 2,  ///< dlopen() failed
        SYMBOL_NOT_FOUND      = 3,  ///< Required symbol not exported
        BINDING_INIT_FAILED   = 4,  ///< Binding Initialize() returned error
        NO_BINDING_AVAILABLE  = 5,  ///< No suitable binding found
        BINDING_NOT_FOUND     = 6   ///< Requested binding doesn't exist
    };

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
    class BindingManager final
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
         * @param config_path Path to YAML configuration (e.g., "/etc/lap/com/bindings.yaml")
         * @return Result<void> Success or error code
         * 
         * @note YAML format example:
         *       bindings:
         *         - name: iceoryx2
         *           priority: 100
         *           library: /usr/lib/lap/com/liblap_binding_iceoryx2.so
         *           enabled: true
         *         - name: dds
         *           priority: 80
         *           library: /usr/lib/lap/com/liblap_binding_dds.so
         *           enabled: true
         *       static_mappings:
         *         - service_id: 0xF001
         *           binding: iceoryx2  # Force ASIL-D to use iceoryx2
         */
        Result<void> LoadConfiguration(const std::string& config_path) noexcept;

        /**
         * @brief Manually register a binding (without dynamic loading)
         * @param config Binding configuration
         * @param binding Pre-constructed binding instance
         * @return Result<void> Success or error code
         * 
         * @note For unit testing or statically linked bindings
         */
        Result<void> RegisterBinding(
            const BindingConfig& config,
            std::shared_ptr<ITransportBinding> binding
        ) noexcept;

        /**
         * @brief Load a binding from shared library
         * @param config Binding configuration (must include library_path)
         * @return Result<void> Success or error code
         * 
         * @details Steps:
         *          1. dlopen(config.library_path, RTLD_LAZY | RTLD_LOCAL)
         *          2. dlsym("CreateBindingInstance")
         *          3. Call factory function to create instance
         *          4. binding->Initialize(config.parameters)
         *          5. Store in registry with priority key
         */
        Result<void> LoadBinding(const BindingConfig& config) noexcept;

        /**
         * @brief Unload a binding and close library handle
         * @param name Binding name
         * @return Result<void> Success or error code
         */
        Result<void> UnloadBinding(const std::string& name) noexcept;

        /**
         * @brief Select binding for a service (priority-based or static mapping)
         * @param service_id AUTOSAR service ID
         * @param instance_id AUTOSAR instance ID (default 0 = any instance)
         * @return ITransportBinding* Pointer to selected binding or nullptr
         * 
         * @details Selection algorithm:
         *          1. Check static_mappings_ for explicit service_id match
         *          2. If no match, iterate bindings_ by priority (descending)
         *          3. Return first enabled binding
         *          4. Return nullptr if no binding available
         * 
         * @note Thread-safe (read lock on mutex_)
         */
        ITransportBinding* SelectBinding(
            uint64_t service_id,
            uint64_t instance_id = 0
        ) noexcept;

        /**
         * @brief Get binding by name
         * @param name Binding name
         * @return Optional<ITransportBinding*> Binding pointer or nullopt
         */
        Optional<ITransportBinding*> GetBinding(const std::string& name) const noexcept;

        /**
         * @brief Get all loaded bindings (for diagnostics)
         * @return std::vector<std::string> List of binding names
         */
        std::vector<std::string> GetLoadedBindings() const noexcept;

        /**
         * @brief Check health status of a specific binding
         * @param name Binding name
         * @return Optional<BindingHealth> Health status if binding exists
         * 
         * @note Used for fault detection and automatic failover
         * @note Returns metrics like error_rate, latency, availability
         */
        Optional<BindingHealth> GetBindingHealth(const std::string& name) const noexcept;

        /**
         * @brief Get performance metrics for a specific binding
         * @param name Binding name
         * @return Optional<TransportMetrics> Metrics if binding exists
         * 
         * @note ARCHITECTURE_SUMMARY.md §7.2: Performance monitoring
         * @note Includes message counts, latency statistics, bandwidth
         */
        Optional<TransportMetrics> GetBindingMetrics(const std::string& name) const noexcept;

        /**
         * @brief Get aggregated metrics for all loaded bindings
         * @return std::map<std::string, TransportMetrics> Metrics per binding
         * 
         * @note Used by diagnostic tools and monitoring systems
         */
        std::map<std::string, TransportMetrics> GetAllMetrics() const noexcept;

        /**
         * @brief Reload configuration file and update bindings
         * @param config_path Path to YAML configuration file
         * @return Result<void> Success or error code
         * 
         * @note Hot reload: unload disabled bindings, load new enabled bindings
         * @note Existing connections are preserved if binding remains enabled
         * @warning Thread-safe but may cause brief service disruption
         */
        Result<void> ReloadConfiguration(const std::string& config_path) noexcept;

        /**
         * @brief Check if a binding supports zero-copy communication
         * @param name Binding name
         * @return bool True if supports zero-copy (e.g., iceoryx2)
         */
        bool SupportsZeroCopy(const std::string& name) const noexcept;

        /**
         * @brief Get priority of a specific binding
         * @param name Binding name
         * @return Optional<uint32_t> Priority value (100=highest, 20=lowest)
         */
        Optional<uint32_t> GetBindingPriority(const std::string& name) const noexcept;

        /**
         * @brief Shutdown all bindings and unload libraries
         * @return Result<void> Success or error code
         */
        Result<void> Shutdown() noexcept;

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
         * @param config_path YAML file path
         * @return Result<std::vector<BindingConfig>> Parsed configurations
         */
        Result<std::vector<BindingConfig>> parseYamlConfig(
            const std::string& config_path
        ) const noexcept;

        /**
         * @brief Find static binding mapping for service
         * @param service_id Service ID
         * @param instance_id Instance ID
         * @return Optional<std::string> Binding name or nullopt
         */
        Optional<std::string> findStaticMapping(
            uint64_t service_id,
            uint64_t instance_id
        ) const noexcept;

        // ========================================================================
        // Member Variables
        // ========================================================================

        /// Mutex for thread-safe access
        mutable std::mutex mutex_;

        /// Binding registry (sorted by priority, descending)
        /// Key: priority (uint32_t), Value: binding pointer
        /// Note: std::multimap allows multiple bindings with same priority
        std::multimap<uint32_t, std::shared_ptr<ITransportBinding>, std::greater<uint32_t>> bindings_;

        /// Binding lookup by name
        std::unordered_map<std::string, std::shared_ptr<ITransportBinding>> bindings_by_name_;

        /// Library handles (for dlclose on shutdown)
        std::unordered_map<std::string, void*> library_handles_;

        /// Static service-to-binding mappings
        std::vector<StaticBindingMapping> static_mappings_;
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_BINDING_MANAGER_HPP
