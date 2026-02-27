/**
 * @file        BindingManager.cpp
 * @author      LightAP Development Team
 * @brief       Dynamic binding manager implementation
 * @date        2025-11-21
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R25-11 Compliance:
 *              - SWS_CM_00401: Transport Binding Selection
 *              - SWS_CM_00402: Dynamic Binding Management
 *              - SWS_CM_00403: Binding Configuration
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

#include "BindingManager.hpp"
#include "ComTypes.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <set>
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

    Result< void > BindingManager::LoadConfiguration( const String& configPath ) noexcept
    {
        LAP_COM_LOG_INFO << "BindingManager: Loading binding configuration from: " << configPath;

        // Parse YAML configuration
        Vector< StaticBindingMapping > newMappings;
        auto parseResult = parseYamlConfig( configPath, newMappings );
        if ( !parseResult.HasValue() )
        {
            LAP_COM_LOG_ERROR << "BindingManager: Failed to parse binding configuration: " << configPath;
            return Result< void >::FromError( parseResult.Error() );
        }

        auto configs = parseResult.Value();

        // Apply static mappings under lock
        {
            LockGuard lock( m_mutex );
            m_staticMappings = std::move( newMappings );
        }

        LAP_COM_LOG_INFO << "BindingManager: Found " << configs.size() << " binding configurations in YAML";

        // Load each binding (no lock here — LoadBinding acquires its own lock)
        for ( const auto& config : configs )
        {
            if ( !config.enabled )
            {
                LAP_COM_LOG_INFO << "Skipping disabled binding: " << config.name;
                continue;
            }

            auto loadResult = LoadBinding( config );
            if ( !loadResult.HasValue() )
            {
                LAP_COM_LOG_WARN << "Failed to load binding '" << config.name
                                 << "': error code " << static_cast< UInt32 > ( loadResult.Error().Value() );
                // Continue loading other bindings (non-fatal)
            }
        }

        LAP_COM_LOG_INFO << "Binding manager initialization complete. Loaded "
                         << m_bindingsByName.size() << " bindings";

        return Result< void >::FromValue();
    }

    Result< Vector< BindingConfig > > BindingManager::parseYamlConfig(
        const String& configPath,
        Vector< StaticBindingMapping >& outMappings ) noexcept
    {
        try
        {
            // Load YAML file
            YAML::Node root = YAML::LoadFile( configPath );

            Vector< BindingConfig > configs;

            // Parse "bindings" array
            if ( root["bindings"] && root["bindings"].IsSequence() )
            {
                for ( const auto& node : root["bindings"] )
                {
                    BindingConfig config;

                    config.name = node["name"].as< std::string > ( "" );
                    config.libraryPath = node["library"].as< std::string > ( "" );
                    config.enabled = node["enabled"].as< bool > ( false );

                    // Parse priority
                    UInt32 priorityVal = node["priority"].as< UInt32 > ( 0 );
                    config.priority = static_cast< BindingPriority > ( priorityVal );

                    // Parse parameters (optional)
                    if ( node["parameters"] && node["parameters"].IsMap() )
                    {
                        for ( const auto& param : node["parameters"] )
                        {
                            config.parameters[param.first.as< std::string > ()] =
                                param.second.as< std::string > ();
                        }
                    }

                    configs.push_back( config );
                }
            }

            // Parse "static_mappings" array (optional)
            if ( root["static_mappings"] && root["static_mappings"].IsSequence() )
            {
                for ( const auto& node : root["static_mappings"] )
                {
                    StaticBindingMapping mapping;

                    // Parse serviceId (hex or decimal)
                    String sidStr = node["serviceId"].as< std::string > ( "" );
                    if ( sidStr.rfind( "0x", 0 ) == 0 || sidStr.rfind( "0X", 0 ) == 0 )
                    {
                        mapping.serviceId = std::stoull( sidStr, nullptr, 16 );
                    }
                    else
                    {
                        mapping.serviceId = std::stoull( sidStr );
                    }

                    // Parse instanceId (optional, default 0 = all instances)
                    if ( node["instanceId"] )
                    {
                        String iidStr = node["instanceId"].as< std::string > ( "0" );
                        if ( iidStr.rfind( "0x", 0 ) == 0 || iidStr.rfind( "0X", 0 ) == 0 )
                        {
                            mapping.instanceId = std::stoull( iidStr, nullptr, 16 );
                        }
                        else
                        {
                            mapping.instanceId = std::stoull( iidStr );
                        }
                    }
                    else
                    {
                        mapping.instanceId = 0;  // Match all instances
                    }

                    mapping.bindingName = node["binding"].as< std::string > ( "" );

                    outMappings.push_back( mapping );
                }
            }

            return Result< Vector< BindingConfig > >::FromValue( configs );
        }
        catch ( const YAML::Exception& e )
        {
            LAP_COM_LOG_ERROR << "YAML parsing error: " << e.what();
            return Result< Vector< BindingConfig > >::FromError(
                MakeErrorCode( ComErrc::kConfigLoadFailed ) );
        }
        catch ( const std::exception& e )
        {
            LAP_COM_LOG_ERROR << "Configuration parsing error: " << e.what();
            return Result< Vector< BindingConfig > >::FromError(
                MakeErrorCode( ComErrc::kConfigLoadFailed ) );
        }
    }

    // ========================================================================
    // Binding Registration
    // ========================================================================

    Result< void > BindingManager::RegisterBinding(
        const BindingConfig& config,
        SharedHandle< ITransportBinding > binding ) noexcept
    {
        if ( !binding )
        {
            LAP_COM_LOG_ERROR << "Cannot register null binding: " << config.name;
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kBindingInitFailed ) );
        }

        LockGuard lock( m_mutex );

        // Store in priority-sorted multimap
        m_bindings.emplace( static_cast< UInt32 > ( config.priority ), binding );

        // Store in name lookup map
        m_bindingsByName[config.name] = binding;

        LAP_COM_LOG_INFO << "Registered binding: name=" << config.name
                         << ", priority=" << static_cast< UInt32 > ( config.priority );

        return Result< void >::FromValue();
    }

    // ========================================================================
    // Dynamic Binding Loading
    // ========================================================================

    Result< void > BindingManager::LoadBinding( const BindingConfig& config ) noexcept
    {
        LAP_COM_LOG_INFO << "Loading binding: name=" << config.name
                         << ", library=" << config.libraryPath;

        // 1. Open shared library
        void* handle = dlopen( config.libraryPath.c_str(), RTLD_LAZY | RTLD_LOCAL );
        if ( !handle )
        {
            const char* error = dlerror();
            LAP_COM_LOG_ERROR << "dlopen failed for '" << config.libraryPath << "': " << error;
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kLibraryLoadFailed ) );
        }

        // Clear previous dlerror
        dlerror();

        // 2. Get factory function symbol
        auto createFunc = reinterpret_cast< CreateBindingFunc > (
            dlsym( handle, "CreateBindingInstance" ) );

        const char* dlsymError = dlerror();
        if ( dlsymError || !createFunc )
        {
            LAP_COM_LOG_ERROR << "Symbol 'CreateBindingInstance' not found in '" << config.libraryPath
                              << "': " << ( dlsymError ? dlsymError : "unknown" );
            dlclose( handle );
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kSymbolNotFound ) );
        }

        // 3. Create binding instance
        ITransportBinding* rawBinding = createFunc();
        if ( !rawBinding )
        {
            LAP_COM_LOG_ERROR << "CreateBindingInstance returned nullptr for '" << config.name << "'";
            dlclose( handle );
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kBindingInitFailed ) );
        }

        // Wrap in SharedHandle with custom deleter
        auto destroyFunc = reinterpret_cast< DestroyBindingFunc > (
            dlsym( handle, "DestroyBindingInstance" ) );

        SharedHandle< ITransportBinding > binding;
        if ( destroyFunc )
        {
            binding = SharedHandle< ITransportBinding > (
                rawBinding,
                [destroyFunc]( ITransportBinding* ptr ) {
                    if ( ptr )
                    {
                        destroyFunc( ptr );
                    }
                }
            );
        }
        else
        {
            // Fallback to delete
            binding = SharedHandle< ITransportBinding > ( rawBinding );
        }

        // 4. Configure and initialize binding
        if ( !config.parameters.empty() )
        {
            binding->Configure( config.parameters );
        }
        auto initResult = binding->Initialize();
        if ( !initResult.HasValue() )
        {
            LAP_COM_LOG_ERROR << "Binding '" << config.name << "' initialization failed: error code "
                              << initResult.Error().Value();
            dlclose( handle );
            return Result< void >::FromError( initResult.Error() );
        }

        // 5. Store in registry
        LockGuard lock( m_mutex );

        m_bindings.emplace( static_cast< UInt32 > ( config.priority ), binding );
        m_bindingsByName[config.name] = binding;
        m_libraryHandles[config.name] = handle;

        LAP_COM_LOG_INFO << "Successfully loaded binding '" << config.name
                         << "' with priority " << static_cast< UInt32 > ( config.priority );

        return Result< void >::FromValue();
    }

    // ========================================================================
    // Binding Unloading
    // ========================================================================

    Result< void > BindingManager::UnloadBinding( const String& name ) noexcept
    {
        LAP_COM_LOG_INFO << "Unloading binding: " << name;

        LockGuard lock( m_mutex );

        // Find binding in name map
        auto it = m_bindingsByName.find( name );
        if ( it == m_bindingsByName.end() )
        {
            LAP_COM_LOG_WARN << "Binding '" << name << "' not found";
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNoBindingAvailable ) );
        }

        // Shutdown binding
        auto shutdownResult = it->second->Shutdown();
        if ( !shutdownResult.HasValue() )
        {
            LAP_COM_LOG_WARN << "Binding '" << name << "' shutdown returned error: "
                             << shutdownResult.Error().Value();
        }

        // Remove from priority map
        for ( auto mapIt = m_bindings.begin(); mapIt != m_bindings.end(); )
        {
            if ( mapIt->second == it->second )
            {
                mapIt = m_bindings.erase( mapIt );
            }
            else
            {
                ++mapIt;
            }
        }

        // Remove from name map
        m_bindingsByName.erase( it );

        // Close library handle
        auto handleIt = m_libraryHandles.find( name );
        if ( handleIt != m_libraryHandles.end() )
        {
            dlclose( handleIt->second );
            m_libraryHandles.erase( handleIt );
        }

        LAP_COM_LOG_INFO << "Binding '" << name << "' unloaded successfully";
        return Result< void >::FromValue();
    }

    // ========================================================================
    // Binding Selection
    // ========================================================================

    ITransportBinding* BindingManager::SelectBinding(
        UInt64 serviceId,
        UInt64 instanceId ) noexcept
    {
        LockGuard lock( m_mutex );

        // 1. Check static mappings first
        auto staticBindingName = findStaticMapping( serviceId, instanceId );
        if ( staticBindingName.has_value() )
        {
            auto it = m_bindingsByName.find( staticBindingName.value() );
            if ( it != m_bindingsByName.end() )
            {
                LAP_COM_LOG_DEBUG << "Selected binding '" << staticBindingName.value()
                                  << "' via static mapping for service " << serviceId;
                return it->second.get();
            }
            else
            {
                LAP_COM_LOG_WARN << "Static mapping refers to non-existent binding '"
                                 << staticBindingName.value() << "'";
            }
        }

        // 2. Select by priority (m_bindings is sorted descending)
        // Check if binding supports this service (ARCHITECTURE_SUMMARY.md §7.3)
        for ( const auto& [priority, binding] : m_bindings )
        {
            if ( binding->SupportsService( serviceId ) )
            {
                LAP_COM_LOG_DEBUG << "Selected binding '" << binding->GetName()
                                  << "' (priority=" << priority << ") for service "
                                  << serviceId;
                return binding.get();
            }
        }

        LAP_COM_LOG_WARN << "No binding available for service " << serviceId;
        return nullptr;
    }

    Optional< String > BindingManager::findStaticMapping(
        UInt64 serviceId,
        UInt64 instanceId ) const noexcept
    {
        for ( const auto& mapping : m_staticMappings )
        {
            // Match serviceId
            if ( mapping.serviceId != serviceId )
            {
                continue;
            }

            // Match instanceId (0 = wildcard for all instances)
            if ( mapping.instanceId == 0 || mapping.instanceId == instanceId )
            {
                return Optional< String > ( mapping.bindingName );
            }
        }

        return Optional< String > ();  // Not found
    }

    // ========================================================================
    // Binding Queries
    // ========================================================================

    Optional< ITransportBinding* > BindingManager::GetBinding(
        const String& name ) const noexcept
    {
        LockGuard lock( m_mutex );

        auto it = m_bindingsByName.find( name );
        if ( it != m_bindingsByName.end() )
        {
            return Optional< ITransportBinding* > ( it->second.get() );
        }

        return Optional< ITransportBinding* > ();
    }

    Vector< String > BindingManager::GetLoadedBindings() const noexcept
    {
        LockGuard lock( m_mutex );

        Vector< String > names;
        names.reserve( m_bindingsByName.size() );

        for ( const auto& pair : m_bindingsByName )
        {
            names.push_back( pair.first );
        }

        return names;
    }

    // ========================================================================
    // Shutdown
    // ========================================================================

    Result< void > BindingManager::Shutdown() noexcept
    {
        LAP_COM_LOG_INFO << "Shutting down BindingManager";

        LockGuard lock( m_mutex );

        // Shutdown all bindings
        for ( auto& pair : m_bindingsByName )
        {
            LAP_COM_LOG_INFO << "Shutting down binding: " << pair.first;
            auto result = pair.second->Shutdown();
            if ( !result.HasValue() )
            {
                LAP_COM_LOG_WARN << "Binding '" << pair.first << "' shutdown error: "
                                 << result.Error().Value();
            }
        }

        // Close all library handles
        for ( auto& pair : m_libraryHandles )
        {
            LAP_COM_LOG_DEBUG << "Closing library: " << pair.first;
            dlclose( pair.second );
        }

        // Clear all containers
        m_bindings.clear();
        m_bindingsByName.clear();
        m_libraryHandles.clear();
        m_staticMappings.clear();

        LAP_COM_LOG_INFO << "BindingManager shutdown complete";
        return Result< void >::FromValue();
    }

    // ========================================================================
    // Health Monitoring
    // ========================================================================

    Optional< BindingHealth > BindingManager::GetBindingHealth(
        const String& name ) const noexcept
    {
        LockGuard lock( m_mutex );

        auto it = m_bindingsByName.find( name );
        if ( it == m_bindingsByName.end() )
        {
            return Optional< BindingHealth > ();
        }

        // Query binding for current metrics
        auto metrics = it->second->GetMetrics();

        // Calculate health status
        BindingHealth health;
        health.errorCount = metrics.serializationErrors + metrics.timeoutErrors;

        // Estimate consecutive errors from recent error rate
        health.consecutiveErrors = ( metrics.timeoutErrors > 0 ) ?
            std::min( health.errorCount, static_cast< UInt32 > ( 10 ) ) : 0;

        // Calculate availability (messagesSent > 0 means active)
        UInt64 totalMessages = metrics.messagesSent + metrics.messagesReceived;
        if ( totalMessages > 0 )
        {
            UInt64 successfulMessages = totalMessages - metrics.messagesDropped;
            health.availabilityPercent =
                ( static_cast< Double > ( successfulMessages ) / totalMessages ) * 100.0;
        }
        else
        {
            health.availabilityPercent = 100.0;  // No traffic yet
        }

        // Overall health check
        health.isHealthy =
            ( health.consecutiveErrors < BindingHealth::kMaxConsecutiveErrors ) &&
            ( health.availabilityPercent >= BindingHealth::kMinAvailabilityPercent );

        health.lastErrorTimestamp = 0;
        health.lastErrorMessage = health.isHealthy ? "OK" : "Degraded performance";

        return Optional< BindingHealth > ( health );
    }

    // ========================================================================
    // Performance Monitoring
    // ========================================================================

    Optional< TransportMetrics > BindingManager::GetBindingMetrics(
        const String& name ) const noexcept
    {
        LockGuard lock( m_mutex );

        auto it = m_bindingsByName.find( name );
        if ( it == m_bindingsByName.end() )
        {
            return Optional< TransportMetrics > ();
        }

        return Optional< TransportMetrics > ( it->second->GetMetrics() );
    }

    Map< String, TransportMetrics > BindingManager::GetAllMetrics() const noexcept
    {
        LockGuard lock( m_mutex );

        Map< String, TransportMetrics > allMetrics;

        for ( const auto& [name, binding] : m_bindingsByName )
        {
            allMetrics[name] = binding->GetMetrics();
        }

        return allMetrics;
    }

    // ========================================================================
    // Configuration Hot Reload
    // ========================================================================

    Result< void > BindingManager::ReloadConfiguration( const String& configPath ) noexcept
    {
        LAP_COM_LOG_INFO << "BindingManager: Reloading configuration from: " << configPath;

        // Parse new configuration
        Vector< StaticBindingMapping > newMappings;
        auto parseResult = parseYamlConfig( configPath, newMappings );
        if ( !parseResult.HasValue() )
        {
            LAP_COM_LOG_ERROR << "BindingManager: Failed to parse new configuration during reload";
            return Result< void >::FromError( parseResult.Error() );
        }

        auto newConfigs = parseResult.Value();

        // Build set of new binding names
        std::set< String > newBindingNames;
        for ( const auto& config : newConfigs )
        {
            if ( config.enabled )
            {
                newBindingNames.insert( config.name );
            }
        }

        LockGuard lock( m_mutex );

        // Replace static mappings atomically
        m_staticMappings = std::move( newMappings );

        // Step 1: Identify bindings to unload
        Vector< String > toUnload;
        for ( const auto& [name, binding] : m_bindingsByName )
        {
            if ( newBindingNames.find( name ) == newBindingNames.end() )
            {
                toUnload.push_back( name );
            }
        }

        // Step 2: Unload removed bindings
        for ( const auto& name : toUnload )
        {
            LAP_COM_LOG_INFO << "ReloadConfiguration: Unloading binding '" << name << "'";

            auto it = m_bindingsByName.find( name );
            if ( it != m_bindingsByName.end() )
            {
                it->second->Shutdown();

                // Remove from priority map
                for ( auto mapIt = m_bindings.begin(); mapIt != m_bindings.end(); )
                {
                    if ( mapIt->second == it->second )
                    {
                        mapIt = m_bindings.erase( mapIt );
                    }
                    else
                    {
                        ++mapIt;
                    }
                }

                // Close library handle
                auto handleIt = m_libraryHandles.find( name );
                if ( handleIt != m_libraryHandles.end() )
                {
                    dlclose( handleIt->second );
                    m_libraryHandles.erase( handleIt );
                }

                m_bindingsByName.erase( it );
            }
        }

        // Step 3: Load new bindings (inline to avoid recursive lock)
        for ( const auto& config : newConfigs )
        {
            if ( !config.enabled )
            {
                continue;
            }

            // Skip if already loaded
            if ( m_bindingsByName.find( config.name ) != m_bindingsByName.end() )
            {
                LAP_COM_LOG_DEBUG << "ReloadConfiguration: Binding '" << config.name
                                  << "' already loaded, skipping";
                continue;
            }

            LAP_COM_LOG_INFO << "ReloadConfiguration: Loading new binding '" << config.name << "'";

            void* handle = dlopen( config.libraryPath.c_str(), RTLD_LAZY | RTLD_LOCAL );
            if ( !handle )
            {
                LAP_COM_LOG_ERROR << "dlopen failed for '" << config.libraryPath << "': " << dlerror();
                continue;
            }

            dlerror();
            auto createFunc = reinterpret_cast< CreateBindingFunc > (
                dlsym( handle, "CreateBindingInstance" ) );

            const char* dlsymError = dlerror();
            if ( dlsymError || !createFunc )
            {
                LAP_COM_LOG_ERROR << "Symbol 'CreateBindingInstance' not found in '"
                                  << config.libraryPath << "'";
                dlclose( handle );
                continue;
            }

            ITransportBinding* rawBinding = createFunc();
            if ( !rawBinding )
            {
                LAP_COM_LOG_ERROR << "CreateBindingInstance returned nullptr for '"
                                  << config.name << "'";
                dlclose( handle );
                continue;
            }

            auto destroyFunc = reinterpret_cast< DestroyBindingFunc > (
                dlsym( handle, "DestroyBindingInstance" ) );

            SharedHandle< ITransportBinding > binding;
            if ( destroyFunc )
            {
                binding = SharedHandle< ITransportBinding > (
                    rawBinding,
                    [destroyFunc]( ITransportBinding* ptr ) {
                        if ( ptr )
                        {
                            destroyFunc( ptr );
                        }
                    }
                );
            }
            else
            {
                binding = SharedHandle< ITransportBinding > ( rawBinding );
            }

            auto initResult = binding->Initialize();
            if ( !initResult.HasValue() )
            {
                LAP_COM_LOG_ERROR << "Binding '" << config.name << "' initialization failed";
                dlclose( handle );
                continue;
            }

            m_bindings.emplace( static_cast< UInt32 > ( config.priority ), binding );
            m_bindingsByName[config.name] = binding;
            m_libraryHandles[config.name] = handle;

            LAP_COM_LOG_INFO << "Successfully loaded binding '" << config.name << "' during reload";
        }

        LAP_COM_LOG_INFO << "BindingManager: Configuration reload complete. Active bindings: "
                         << m_bindingsByName.size();

        return Result< void >::FromValue();
    }

    // ========================================================================
    // Capability Queries
    // ========================================================================

    Bool BindingManager::SupportsZeroCopy( const String& name ) const noexcept
    {
        LockGuard lock( m_mutex );

        auto it = m_bindingsByName.find( name );
        if ( it == m_bindingsByName.end() )
        {
            return false;
        }

        return it->second->SupportsZeroCopy();
    }

    Optional< UInt32 > BindingManager::GetBindingPriority(
        const String& name ) const noexcept
    {
        LockGuard lock( m_mutex );

        auto it = m_bindingsByName.find( name );
        if ( it == m_bindingsByName.end() )
        {
            return Optional< UInt32 > ();
        }

        return Optional< UInt32 > ( it->second->GetPriority() );
    }

} // namespace binding
} // namespace com
} // namespace lap
