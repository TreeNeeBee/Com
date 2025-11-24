/**
 * @file        BindingManager.cpp
 * @author      LightAP Development Team
 * @brief       Dynamic binding manager implementation
 * @date        2025-11-21
 * @copyright   Copyright (c) 2025
 */

#include "BindingManager.hpp"
#include "ComTypes.hpp"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <algorithm>
#include <set>
#include <map>
#include <sstream>

namespace lap
{
namespace com
{
namespace binding
{
    // ========================================================================
    // Singleton Instance
    // ========================================================================

    BindingManager& BindingManager::GetInstance() noexcept
    {
        static BindingManager instance;
        return instance;
    }

    // ========================================================================
    // Destructor
    // ========================================================================

    BindingManager::~BindingManager() noexcept
    {
        Shutdown();
    }

    // ========================================================================
    // Configuration Loading
    // ========================================================================

    Result<void> BindingManager::LoadConfiguration(const std::string& config_path) noexcept
    {
        LAP_COM_LOG_INFO << "BindingManager: Loading binding configuration from: " << config_path;

        // Parse YAML configuration
        auto parse_result = parseYamlConfig(config_path);
        if (!parse_result.HasValue())
        {
            LAP_COM_LOG_ERROR << "BindingManager: Failed to parse binding configuration: " << config_path;
            return Result<void>::FromError(parse_result.Error());
        }

        auto configs = parse_result.Value();
        LAP_COM_LOG_INFO << "BindingManager: Found " << configs.size() << " binding configurations in YAML";

        // Load each binding
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& config : configs)
        {
            if (!config.enabled)
            {
                LAP_COM_LOG_INFO << "Skipping disabled binding: " << config.name;
                continue;
            }

            auto load_result = LoadBinding(config);
            if (!load_result.HasValue())
            {
                LAP_COM_LOG_WARN << "Failed to load binding '" << config.name 
                                 << "': error code " << static_cast<uint32_t>(load_result.Error().Value());
                // Continue loading other bindings (non-fatal)
            }
        }

        LAP_COM_LOG_INFO << "Binding manager initialization complete. Loaded " 
                         << bindings_by_name_.size() << " bindings";

        return Result<void>::FromValue();
    }

    Result<std::vector<BindingConfig>> BindingManager::parseYamlConfig(
        const std::string& config_path) const noexcept
    {
        try
        {
            // Load YAML file
            YAML::Node root = YAML::LoadFile(config_path);

            std::vector<BindingConfig> configs;

            // Parse "bindings" array
            if (root["bindings"] && root["bindings"].IsSequence())
            {
                for (const auto& node : root["bindings"])
                {
                    BindingConfig config;

                    config.name = node["name"].as<std::string>("");
                    config.library_path = node["library"].as<std::string>("");
                    config.enabled = node["enabled"].as<bool>(false);

                    // Parse priority
                    uint32_t priority_val = node["priority"].as<uint32_t>(0);
                    config.priority = static_cast<BindingPriority>(priority_val);

                    // Parse parameters (optional)
                    if (node["parameters"] && node["parameters"].IsMap())
                    {
                        for (const auto& param : node["parameters"])
                        {
                            config.parameters[param.first.as<std::string>()] =
                                param.second.as<std::string>();
                        }
                    }

                    configs.push_back(config);
                }
            }

            // Parse "static_mappings" array (optional)
            if (root["static_mappings"] && root["static_mappings"].IsSequence())
            {
                for (const auto& node : root["static_mappings"])
                {
                    StaticBindingMapping mapping;

                    // Parse service_id (hex or decimal)
                    std::string sid_str = node["service_id"].as<std::string>("");
                    if (sid_str.rfind("0x", 0) == 0 || sid_str.rfind("0X", 0) == 0)
                    {
                        mapping.service_id = std::stoull(sid_str, nullptr, 16);
                    }
                    else
                    {
                        mapping.service_id = std::stoull(sid_str);
                    }

                    // Parse instance_id (optional, default 0 = all instances)
                    if (node["instance_id"])
                    {
                        std::string iid_str = node["instance_id"].as<std::string>("0");
                        if (iid_str.rfind("0x", 0) == 0 || iid_str.rfind("0X", 0) == 0)
                        {
                            mapping.instance_id = std::stoull(iid_str, nullptr, 16);
                        }
                        else
                        {
                            mapping.instance_id = std::stoull(iid_str);
                        }
                    }
                    else
                    {
                        mapping.instance_id = 0;  // Match all instances
                    }

                    mapping.binding_name = node["binding"].as<std::string>("");

                    // Store in member variable (const_cast safe here since we're in non-const context)
                    const_cast<BindingManager*>(this)->static_mappings_.push_back(mapping);
                }
            }

            return Result<std::vector<BindingConfig>>::FromValue(configs);
        }
        catch (const YAML::Exception& e)
        {
            LAP_COM_LOG_ERROR << "YAML parsing error: " << e.what();
            return Result<std::vector<BindingConfig>>::FromError(
                lap::core::ErrorCode(static_cast<int>(BindingManagerError::CONFIG_LOAD_FAILED), 0));
        }
        catch (const std::exception& e)
        {
            LAP_COM_LOG_ERROR << "Configuration parsing error: " << e.what();
            return Result<std::vector<BindingConfig>>::FromError(
                lap::core::ErrorCode(static_cast<int>(BindingManagerError::CONFIG_LOAD_FAILED), 0));
        }
    }

    // ========================================================================
    // Binding Registration
    // ========================================================================

    Result<void> BindingManager::RegisterBinding(
        const BindingConfig& config,
        std::shared_ptr<ITransportBinding> binding) noexcept
    {
        if (!binding)
        {
            LAP_COM_LOG_ERROR << "Cannot register null binding: " << config.name;
            return Result<void>::FromError(
                lap::core::ErrorCode(static_cast<int>(BindingManagerError::BINDING_INIT_FAILED), 0));
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Store in priority-sorted multimap
        bindings_.emplace(static_cast<uint32_t>(config.priority), binding);

        // Store in name lookup map
        bindings_by_name_[config.name] = binding;

        LAP_COM_LOG_INFO << "Registered binding: name=" << config.name 
                         << ", priority=" << static_cast<uint32_t>(config.priority);

        return Result<void>::FromValue();
    }

    // ========================================================================
    // Dynamic Binding Loading
    // ========================================================================

    Result<void> BindingManager::LoadBinding(const BindingConfig& config) noexcept
    {
        LAP_COM_LOG_INFO << "Loading binding: name=" << config.name 
                         << ", library=" << config.library_path;

        // 1. Open shared library
        void* handle = dlopen(config.library_path.c_str(), RTLD_LAZY | RTLD_LOCAL);
        if (!handle)
        {
            const char* error = dlerror();
            LAP_COM_LOG_ERROR << "dlopen failed for '" << config.library_path << "': " << error;
            return Result<void>::FromError(
                lap::core::ErrorCode(static_cast<int>(BindingManagerError::LIBRARY_LOAD_FAILED), 0));
        }

        // Clear previous dlerror
        dlerror();

        // 2. Get factory function symbol
        auto create_func = reinterpret_cast<CreateBindingFunc>(
            dlsym(handle, "CreateBindingInstance"));

        const char* dlsym_error = dlerror();
        if (dlsym_error || !create_func)
        {
            LAP_COM_LOG_ERROR << "Symbol 'CreateBindingInstance' not found in '" << config.library_path 
                              << "': " << (dlsym_error ? dlsym_error : "unknown");
            dlclose(handle);
            return Result<void>::FromError(
                lap::core::ErrorCode(static_cast<int>(BindingManagerError::SYMBOL_NOT_FOUND), 0));
        }

        // 3. Create binding instance
        ITransportBinding* raw_binding = create_func();
        if (!raw_binding)
        {
            LAP_COM_LOG_ERROR << "CreateBindingInstance returned nullptr for '" << config.name << "'";
            dlclose(handle);
            return Result<void>::FromError(
                lap::core::ErrorCode(static_cast<int>(BindingManagerError::BINDING_INIT_FAILED), 0));
        }

        // Wrap in shared_ptr with custom deleter
        auto destroy_func = reinterpret_cast<DestroyBindingFunc>(
            dlsym(handle, "DestroyBindingInstance"));

        std::shared_ptr<ITransportBinding> binding;
        if (destroy_func)
        {
            binding = std::shared_ptr<ITransportBinding>(
                raw_binding,
                [destroy_func](ITransportBinding* ptr) {
                    if (ptr)
                    {
                        destroy_func(ptr);
                    }
                }
            );
        }
        else
        {
            // Fallback to delete
            binding = std::shared_ptr<ITransportBinding>(raw_binding);
        }

        // 4. Initialize binding with parameters
        // TODO: Convert config.parameters to YAML::Node for Initialize()
        // For now, pass empty config
        auto init_result = binding->Initialize();
        if (!init_result.HasValue())
        {
            LAP_COM_LOG_ERROR << "Binding '" << config.name << "' initialization failed: error code " 
                              << init_result.Error().Value();
            dlclose(handle);
            return Result<void>::FromError(init_result.Error());
        }

        // 5. Store in registry
        std::lock_guard<std::mutex> lock(mutex_);

        bindings_.emplace(static_cast<uint32_t>(config.priority), binding);
        bindings_by_name_[config.name] = binding;
        library_handles_[config.name] = handle;

        LAP_COM_LOG_INFO << "Successfully loaded binding '" << config.name 
                         << "' with priority " << static_cast<uint32_t>(config.priority);

        return Result<void>::FromValue();
    }

    // ========================================================================
    // Binding Unloading
    // ========================================================================

    Result<void> BindingManager::UnloadBinding(const std::string& name) noexcept
    {
        LAP_COM_LOG_INFO << "Unloading binding: " << name;

        std::lock_guard<std::mutex> lock(mutex_);

        // Find binding in name map
        auto it = bindings_by_name_.find(name);
        if (it == bindings_by_name_.end())
        {
            LAP_COM_LOG_WARN << "Binding '" << name << "' not found";
            return Result<void>::FromError(
                lap::core::ErrorCode(static_cast<int>(BindingManagerError::BINDING_NOT_FOUND), 0));
        }

        // Shutdown binding
        auto shutdown_result = it->second->Shutdown();
        if (!shutdown_result.HasValue())
        {
            LAP_COM_LOG_WARN << "Binding '" << name << "' shutdown returned error: " 
                             << shutdown_result.Error().Value();
        }

        // Remove from priority map
        for (auto map_it = bindings_.begin(); map_it != bindings_.end(); )
        {
            if (map_it->second == it->second)
            {
                map_it = bindings_.erase(map_it);
            }
            else
            {
                ++map_it;
            }
        }

        // Remove from name map
        bindings_by_name_.erase(it);

        // Close library handle
        auto handle_it = library_handles_.find(name);
        if (handle_it != library_handles_.end())
        {
            dlclose(handle_it->second);
            library_handles_.erase(handle_it);
        }

        LAP_COM_LOG_INFO << "Binding '" << name << "' unloaded successfully";
        return Result<void>::FromValue();
    }

    // ========================================================================
    // Binding Selection
    // ========================================================================

    ITransportBinding* BindingManager::SelectBinding(
        uint64_t service_id,
        uint64_t instance_id) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 1. Check static mappings first
        auto static_binding_name = findStaticMapping(service_id, instance_id);
        if (static_binding_name.has_value())
        {
            auto it = bindings_by_name_.find(static_binding_name.value());
            if (it != bindings_by_name_.end())
            {
                LAP_COM_LOG_DEBUG << "Selected binding '" << static_binding_name.value() 
                                  << "' via static mapping for service 0x" << std::hex << service_id << std::dec;
                return it->second.get();
            }
            else
            {
                LAP_COM_LOG_WARN << "Static mapping refers to non-existent binding '" 
                                 << static_binding_name.value() << "'";
            }
        }

        // 2. Select by priority (bindings_ is sorted descending)
        // Check if binding supports this service (ARCHITECTURE_SUMMARY.md §7.3)
        for (const auto& [priority, binding] : bindings_)
        {
            if (binding->SupportsService(service_id))
            {
                LAP_COM_LOG_DEBUG << "Selected binding '" << binding->GetName() 
                                  << "' (priority=" << priority << ") for service 0x" 
                                  << std::hex << service_id << std::dec;
                return binding.get();
            }
        }

        LAP_COM_LOG_WARN << "No binding available for service 0x" << std::hex << service_id << std::dec;
        return nullptr;
    }

    Optional<std::string> BindingManager::findStaticMapping(
        uint64_t service_id,
        uint64_t instance_id) const noexcept
    {
        for (const auto& mapping : static_mappings_)
        {
            // Match service_id
            if (mapping.service_id != service_id)
            {
                continue;
            }

            // Match instance_id (0 = wildcard for all instances)
            if (mapping.instance_id == 0 || mapping.instance_id == instance_id)
            {
                return Optional<std::string>(mapping.binding_name);
            }
        }

        return Optional<std::string>();  // Not found
    }

    // ========================================================================
    // Binding Queries
    // ========================================================================

    Optional<ITransportBinding*> BindingManager::GetBinding(
        const std::string& name) const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = bindings_by_name_.find(name);
        if (it != bindings_by_name_.end())
        {
            return Optional<ITransportBinding*>(it->second.get());
        }

        return Optional<ITransportBinding*>();
    }

    std::vector<std::string> BindingManager::GetLoadedBindings() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<std::string> names;
        names.reserve(bindings_by_name_.size());

        for (const auto& pair : bindings_by_name_)
        {
            names.push_back(pair.first);
        }

        return names;
    }

    // ========================================================================
    // Shutdown
    // ========================================================================

    Result<void> BindingManager::Shutdown() noexcept
    {
        LAP_COM_LOG_INFO << "Shutting down BindingManager";

        std::lock_guard<std::mutex> lock(mutex_);

        // Shutdown all bindings
        for (auto& pair : bindings_by_name_)
        {
            LAP_COM_LOG_INFO << "Shutting down binding: " << pair.first;
            auto result = pair.second->Shutdown();
            if (!result.HasValue())
            {
                LAP_COM_LOG_WARN << "Binding '" << pair.first << "' shutdown error: " 
                                 << result.Error().Value();
            }
        }

        // Close all library handles
        for (auto& pair : library_handles_)
        {
            LAP_COM_LOG_DEBUG << "Closing library: " << pair.first;
            dlclose(pair.second);
        }

        // Clear all containers
        bindings_.clear();
        bindings_by_name_.clear();
        library_handles_.clear();
        static_mappings_.clear();

        LAP_COM_LOG_INFO << "BindingManager shutdown complete";
        return Result<void>::FromValue();
    }

    // ========================================================================
    // Health Monitoring
    // ========================================================================

    Optional<BindingHealth> BindingManager::GetBindingHealth(
        const std::string& name) const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = bindings_by_name_.find(name);
        if (it == bindings_by_name_.end())
        {
            return Optional<BindingHealth>();
        }

        // Query binding for current metrics
        auto metrics = it->second->GetMetrics();

        // Calculate health status
        BindingHealth health;
        health.error_count = metrics.serialization_errors + metrics.timeout_errors;
        
        // Estimate consecutive errors from recent error rate
        health.consecutive_errors = (metrics.timeout_errors > 0) ? 
            std::min(health.error_count, static_cast<uint32_t>(10)) : 0;
        
        // Calculate availability (messages_sent > 0 means active)
        uint64_t total_messages = metrics.messages_sent + metrics.messages_received;
        if (total_messages > 0)
        {
            uint64_t successful_messages = total_messages - metrics.messages_dropped;
            health.availability_percent = 
                (static_cast<double>(successful_messages) / total_messages) * 100.0;
        }
        else
        {
            health.availability_percent = 100.0;  // No traffic yet
        }

        // Overall health check
        health.is_healthy = 
            (health.consecutive_errors < BindingHealth::MAX_CONSECUTIVE_ERRORS) &&
            (health.availability_percent >= BindingHealth::MIN_AVAILABILITY_PERCENT);

        health.last_error_timestamp = 0;
        health.last_error_message = health.is_healthy ? "OK" : "Degraded performance";

        return Optional<BindingHealth>(health);
    }

    // ========================================================================
    // Performance Monitoring
    // ========================================================================

    Optional<TransportMetrics> BindingManager::GetBindingMetrics(
        const std::string& name) const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = bindings_by_name_.find(name);
        if (it == bindings_by_name_.end())
        {
            return Optional<TransportMetrics>();
        }

        return Optional<TransportMetrics>(it->second->GetMetrics());
    }

    std::map<std::string, TransportMetrics> BindingManager::GetAllMetrics() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::map<std::string, TransportMetrics> all_metrics;

        for (const auto& [name, binding] : bindings_by_name_)
        {
            all_metrics[name] = binding->GetMetrics();
        }

        return all_metrics;
    }

    // ========================================================================
    // Configuration Hot Reload
    // ========================================================================

    Result<void> BindingManager::ReloadConfiguration(const std::string& config_path) noexcept
    {
        LAP_COM_LOG_INFO << "BindingManager: Reloading configuration from: " << config_path;

        // Parse new configuration
        auto parse_result = parseYamlConfig(config_path);
        if (!parse_result.HasValue())
        {
            LAP_COM_LOG_ERROR << "BindingManager: Failed to parse new configuration during reload";
            return Result<void>::FromError(parse_result.Error());
        }

        auto new_configs = parse_result.Value();

        // Build set of new binding names
        std::set<std::string> new_binding_names;
        for (const auto& config : new_configs)
        {
            if (config.enabled)
            {
                new_binding_names.insert(config.name);
            }
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Step 1: Identify bindings to unload
        std::vector<std::string> to_unload;
        for (const auto& [name, binding] : bindings_by_name_)
        {
            if (new_binding_names.find(name) == new_binding_names.end())
            {
                to_unload.push_back(name);
            }
        }

        // Step 2: Unload removed bindings
        for (const auto& name : to_unload)
        {
            LAP_COM_LOG_INFO << "ReloadConfiguration: Unloading binding '" << name << "'";
            
            // Shutdown binding
            auto it = bindings_by_name_.find(name);
            if (it != bindings_by_name_.end())
            {
                it->second->Shutdown();
                
                // Remove from priority map
                for (auto map_it = bindings_.begin(); map_it != bindings_.end(); )
                {
                    if (map_it->second == it->second)
                    {
                        map_it = bindings_.erase(map_it);
                    }
                    else
                    {
                        ++map_it;
                    }
                }
                
                // Close library handle
                auto handle_it = library_handles_.find(name);
                if (handle_it != library_handles_.end())
                {
                    dlclose(handle_it->second);
                    library_handles_.erase(handle_it);
                }
                
                bindings_by_name_.erase(it);
            }
        }

        // Step 3: Load new bindings
        for (const auto& config : new_configs)
        {
            if (!config.enabled)
            {
                continue;
            }

            // Skip if already loaded
            if (bindings_by_name_.find(config.name) != bindings_by_name_.end())
            {
                LAP_COM_LOG_DEBUG << "ReloadConfiguration: Binding '" << config.name << "' already loaded, skipping";
                continue;
            }

            LAP_COM_LOG_INFO << "ReloadConfiguration: Loading new binding '" << config.name << "'";
            
            // Inline loading logic (avoid recursive lock)
            void* handle = dlopen(config.library_path.c_str(), RTLD_LAZY | RTLD_LOCAL);
            if (!handle)
            {
                LAP_COM_LOG_ERROR << "dlopen failed for '" << config.library_path << "': " << dlerror();
                continue;
            }

            dlerror();
            auto create_func = reinterpret_cast<CreateBindingFunc>(
                dlsym(handle, "CreateBindingInstance"));

            const char* dlsym_error = dlerror();
            if (dlsym_error || !create_func)
            {
                LAP_COM_LOG_ERROR << "Symbol 'CreateBindingInstance' not found in '" << config.library_path << "'";
                dlclose(handle);
                continue;
            }

            ITransportBinding* raw_binding = create_func();
            if (!raw_binding)
            {
                LAP_COM_LOG_ERROR << "CreateBindingInstance returned nullptr for '" << config.name << "'";
                dlclose(handle);
                continue;
            }

            auto destroy_func = reinterpret_cast<DestroyBindingFunc>(
                dlsym(handle, "DestroyBindingInstance"));

            std::shared_ptr<ITransportBinding> binding;
            if (destroy_func)
            {
                binding = std::shared_ptr<ITransportBinding>(
                    raw_binding,
                    [destroy_func](ITransportBinding* ptr) {
                        if (ptr) destroy_func(ptr);
                    }
                );
            }
            else
            {
                binding = std::shared_ptr<ITransportBinding>(raw_binding);
            }

            auto init_result = binding->Initialize();
            if (!init_result.HasValue())
            {
                LAP_COM_LOG_ERROR << "Binding '" << config.name << "' initialization failed";
                dlclose(handle);
                continue;
            }

            bindings_.emplace(static_cast<uint32_t>(config.priority), binding);
            bindings_by_name_[config.name] = binding;
            library_handles_[config.name] = handle;
            
            LAP_COM_LOG_INFO << "Successfully loaded binding '" << config.name << "' during reload";
        }

        LAP_COM_LOG_INFO << "BindingManager: Configuration reload complete. Active bindings: " 
                         << bindings_by_name_.size();

        return Result<void>::FromValue();
    }

    // ========================================================================
    // Capability Queries
    // ========================================================================

    bool BindingManager::SupportsZeroCopy(const std::string& name) const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = bindings_by_name_.find(name);
        if (it == bindings_by_name_.end())
        {
            return false;
        }

        return it->second->SupportsZeroCopy();
    }

    Optional<uint32_t> BindingManager::GetBindingPriority(
        const std::string& name) const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = bindings_by_name_.find(name);
        if (it == bindings_by_name_.end())
        {
            return Optional<uint32_t>();
        }

        return Optional<uint32_t>(it->second->GetPriority());
    }

} // namespace binding
} // namespace com
} // namespace lap
