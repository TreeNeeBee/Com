/**
 * @file        StaticInstanceManager.cpp
 * @author      Aii
 * @brief       AUTOSAR R25-11 Static Instance Manager Implementation
 * @date        2026/02/16
 * @details     Implements lifecycle management for statically configured
 *              service instances. Loaded from YAML manifest, bypasses SD,
 *              available immediately at startup.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/16  <td>1.0      <td>Aii     <td>Initial implementation
 * </table>
 */

#include "StaticInstanceManager.hpp"

#include <chrono>
#include <algorithm>

namespace lap
{
namespace com
{
namespace discovery
{
    // ====================================================================
    // Construction / Destruction
    // ====================================================================

    StaticInstanceManager::StaticInstanceManager( const Config& config )
        : m_config( config )
        , m_bInitialized( false )
    {
    }

    StaticInstanceManager::~StaticInstanceManager() noexcept
    {
        Shutdown();
    }

    // ====================================================================
    // Factory
    // ====================================================================

    Result< std::unique_ptr< StaticInstanceManager > >
    StaticInstanceManager::Create( const Config& config ) noexcept
    {
        try
        {
            auto manager = std::unique_ptr< StaticInstanceManager >(
                new StaticInstanceManager( config ) );

            // Optionally load + activate on creation
            if ( !config.m_strManifestPath.empty() )
            {
                auto loadResult = manager->LoadManifest();
                if ( !loadResult.HasValue() )
                {
                    // Non-fatal: static config may not be present at deployment
                    // Manager is still usable (can reload later)
                }
            }

            return Result< std::unique_ptr< StaticInstanceManager > >::FromValue(
                std::move( manager ) );
        }
        catch ( ... )
        {
            return Result< std::unique_ptr< StaticInstanceManager > >::FromError(
                MakeErrorCode( ComErrc::kInternal ) );
        }
    }

    // ====================================================================
    // Manifest Loading (SWS_CM_02201)
    // ====================================================================

    Result< UInt32 >
    StaticInstanceManager::LoadManifest( const String& manifestPath ) noexcept
    {
        const String& path = manifestPath.empty()
            ? m_config.m_strManifestPath
            : manifestPath;

        // Use existing StaticServiceConfigLoader
        auto loadResult = StaticServiceConfigLoader::LoadFromYAML( path );
        if ( !loadResult.HasValue() )
        {
            return Result< UInt32 >::FromError( loadResult.Error() );
        }

        auto endpoints = std::move( loadResult ).Value();

        UInt32 loadedCount = 0;

        {
            ScopedLock< Mutex > lock( m_mutex );

            m_vecRecords.clear();
            m_vecRecords.reserve( endpoints.size() );

            for ( auto& ep : endpoints )
            {
                StaticInstanceRecord record;
                record.m_endpoint = std::move( ep );
                record.m_state = StaticInstanceState::kConfigured;
                record.m_iActivationAttempts = 0;
                m_vecRecords.push_back( std::move( record ) );
            }

            m_bInitialized = true;
            loadedCount = static_cast< UInt32 >( m_vecRecords.size() );

            // Update statistics
            {
                ScopedLock< Mutex > statsLock( m_statsMutex );
                m_statistics.m_iTotalInstances = loadedCount;
            }
        } // m_mutex released here

        // Auto-activate if configured (outside lock — ActivateAll takes its own lock)
        if ( m_config.m_bActivateOnLoad && loadedCount > 0 )
        {
            auto activateResult = ActivateAll();
            if ( !activateResult.HasValue() )
            {
                // Non-fatal: some instances may fail to activate
            }
        }

        return Result< UInt32 >::FromValue( loadedCount );
    }

    Result< UInt32 >
    StaticInstanceManager::ReloadManifest() noexcept
    {
        return LoadManifest( m_config.m_strManifestPath );
    }

    // ====================================================================
    // Instance Activation (SWS_CM_02202: Bypass SD)
    // ====================================================================

    Result< UInt32 >
    StaticInstanceManager::ActivateAll() noexcept
    {
        ScopedLock< Mutex > lock( m_mutex );

        UInt32 activatedCount = 0;
        UInt64 failedCount = 0;

        for ( auto& record : m_vecRecords )
        {
            if ( record.m_state == StaticInstanceState::kActive )
            {
                ++activatedCount;
                continue;
            }

            auto result = activateRecord( record );
            if ( result.HasValue() )
            {
                ++activatedCount;
            }
            else
            {
                ++failedCount;
            }
        }

        // Update statistics
        {
            ScopedLock< Mutex > statsLock( m_statsMutex );
            m_statistics.m_iActiveInstances = activatedCount;
            m_statistics.m_iFailedInstances = failedCount;
        }

        return Result< UInt32 >::FromValue( activatedCount );
    }

    Result< void >
    StaticInstanceManager::ActivateInstance(
        const String& serviceName,
        InstanceIdentifierType instanceId ) noexcept
    {
        ScopedLock< Mutex > lock( m_mutex );

        for ( auto& record : m_vecRecords )
        {
            if ( record.m_endpoint.m_strServiceInterfaceName == serviceName &&
                 record.m_endpoint.m_iInstanceId == instanceId )
            {
                return activateRecord( record );
            }
        }

        return Result< void >::FromError(
            MakeErrorCode( ComErrc::kServiceNotOffered ) );
    }

    Result< void >
    StaticInstanceManager::SuspendInstance(
        const String& serviceName,
        InstanceIdentifierType instanceId ) noexcept
    {
        ScopedLock< Mutex > lock( m_mutex );

        for ( auto& record : m_vecRecords )
        {
            if ( record.m_endpoint.m_strServiceInterfaceName == serviceName &&
                 record.m_endpoint.m_iInstanceId == instanceId )
            {
                record.m_state = StaticInstanceState::kSuspended;

                {
                    ScopedLock< Mutex > statsLock( m_statsMutex );
                    if ( m_statistics.m_iActiveInstances > 0 )
                    {
                        m_statistics.m_iActiveInstances--;
                    }
                    m_statistics.m_iSuspendedInstances++;
                }

                return Result< void >::FromValue();
            }
        }

        return Result< void >::FromError(
            MakeErrorCode( ComErrc::kServiceNotOffered ) );
    }

    // ====================================================================
    // Lookup (SWS_CM_02203: No version check)
    // ====================================================================

    Result< Vector< ServiceInstanceInfo > >
    StaticInstanceManager::FindInstances(
        const String& serviceName,
        const Optional< InstanceIdentifierType >& instanceFilter ) noexcept
    {
        ScopedLock< Mutex > lock( m_mutex );

        Vector< ServiceInstanceInfo > results;

        // Update lookup stats
        {
            ScopedLock< Mutex > statsLock( m_statsMutex );
            m_statistics.m_iLookupCount++;
        }

        for ( const auto& record : m_vecRecords )
        {
            // Only return Active instances
            if ( record.m_state != StaticInstanceState::kActive )
            {
                continue;
            }

            if ( record.m_endpoint.m_strServiceInterfaceName != serviceName )
            {
                continue;
            }

            if ( instanceFilter.has_value() &&
                 record.m_endpoint.m_iInstanceId != instanceFilter.value() )
            {
                continue;
            }

            results.push_back( toServiceInstanceInfo( record.m_endpoint ) );
        }

        if ( !results.empty() )
        {
            ScopedLock< Mutex > statsLock( m_statsMutex );
            m_statistics.m_iLookupHits++;
        }

        return Result< Vector< ServiceInstanceInfo > >::FromValue(
            std::move( results ) );
    }

    Bool
    StaticInstanceManager::IsStaticInstance(
        const String& serviceName,
        InstanceIdentifierType instanceId ) const noexcept
    {
        ScopedLock< Mutex > lock( m_mutex );

        for ( const auto& record : m_vecRecords )
        {
            if ( record.m_endpoint.m_strServiceInterfaceName == serviceName &&
                 record.m_endpoint.m_iInstanceId == instanceId )
            {
                return true;
            }
        }

        return false;
    }

    // ====================================================================
    // Monitoring
    // ====================================================================

    Vector< StaticInstanceRecord >
    StaticInstanceManager::GetAllRecords() const noexcept
    {
        ScopedLock< Mutex > lock( m_mutex );
        return m_vecRecords;
    }

    StaticInstanceManager::Statistics
    StaticInstanceManager::GetStatistics() const noexcept
    {
        ScopedLock< Mutex > lock( m_statsMutex );
        return m_statistics;
    }

    Bool
    StaticInstanceManager::IsHealthy() const noexcept
    {
        ScopedLock< Mutex > lock( m_mutex );

        for ( const auto& record : m_vecRecords )
        {
            if ( record.m_state == StaticInstanceState::kActive )
            {
                return true;
            }
        }

        return m_vecRecords.empty();  // Empty = healthy (no static config expected)
    }

    void
    StaticInstanceManager::Shutdown() noexcept
    {
        ScopedLock< Mutex > lock( m_mutex );

        for ( auto& record : m_vecRecords )
        {
            record.m_state = StaticInstanceState::kConfigured;
        }

        m_bInitialized = false;

        {
            ScopedLock< Mutex > statsLock( m_statsMutex );
            m_statistics.m_iActiveInstances = 0;
            m_statistics.m_iSuspendedInstances = 0;
        }
    }

    // ====================================================================
    // Private Helpers
    // ====================================================================

    Result< void >
    StaticInstanceManager::activateRecord( StaticInstanceRecord& record ) noexcept
    {
        record.m_iActivationAttempts++;

        // Validate required fields
        if ( record.m_endpoint.m_strServiceInterfaceName.empty() ||
             record.m_endpoint.m_strBindingType.empty() )
        {
            record.m_state = StaticInstanceState::kFailed;
            record.m_strLastError = "Missing required fields (service_name or binding_type)";
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kInvalidArgument ) );
        }

        // Mark as active (SWS_CM_02202: No SD required, bypass discovery)
        // The binding resolution is deferred to first use via proxy/skeleton.
        // We only validate that the static configuration is well-formed.
        record.m_state = StaticInstanceState::kActive;
        record.m_activationTime = std::chrono::steady_clock::now();

        return Result< void >::FromValue();
    }

    ServiceInstanceInfo
    StaticInstanceManager::toServiceInstanceInfo(
        const StaticServiceEndpoint& endpoint ) noexcept
    {
        ServiceInstanceInfo info;
        info.m_strServiceInterfaceName = endpoint.m_strServiceInterfaceName;
        info.m_iInstanceId = endpoint.m_iInstanceId;
        info.m_strBindingType = endpoint.m_strBindingType;
        info.m_strNetworkEndpoint = endpoint.m_strEndpoint;

        // SWS_CM_02203: No runtime version check for static connections
        // Leave version fields at default (0)
        info.m_iMajorVersion = 0;
        info.m_iMinorVersion = 0;

        // Static instances have infinite TTL
        info.m_iTtlSeconds = 0xFFFFFFFF;

        // Copy transport config as metadata
        for ( const auto& kv : endpoint.m_mapTransportConfig )
        {
            info.m_mapMetadata[ kv.first ] = kv.second;
        }

        // Set activation timestamp
        info.m_iTimestampMs = static_cast< UInt64 >(
            std::chrono::duration_cast< std::chrono::milliseconds >(
                std::chrono::steady_clock::now().time_since_epoch() ).count() );

        return info;
    }

} // namespace discovery
} // namespace com
} // namespace lap
