/**
 * @file        ConfigParser.cpp
 * @author      Aii
 * @brief       AUTOSAR R25-11 Unified Configuration Parser Implementation
 * @date        2026/02/16
 * @details     Implements parsing of binding, static endpoint, discovery, and
 *              QoS configuration from YAML files.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/16  <td>1.0      <td>Aii     <td>Initial implementation
 * </table>
 */

#include "ConfigParser.hpp"

#include <fstream>
#include <yaml-cpp/yaml.h>

namespace lap
{
namespace com
{
    // ====================================================================
    // Unified Configuration Parsing
    // ====================================================================

    Result< ComConfiguration >
    ConfigParser::ParseUnified( const String& configPath ) noexcept
    {
        if ( configPath.empty() )
        {
            return Result< ComConfiguration >::FromError(
                MakeErrorCode( ComErrc::kInvalidArgument ) );
        }

        std::ifstream fileCheck( configPath );
        if ( !fileCheck.good() )
        {
            return Result< ComConfiguration >::FromError(
                MakeErrorCode( ComErrc::kConfigLoadFailed ) );
        }
        fileCheck.close();

        ComConfiguration config;

        try
        {
            YAML::Node root = YAML::LoadFile( configPath );

            if ( !root[ "com" ] )
            {
                // No 'com' section — return default config
                return Result< ComConfiguration >::FromValue( GetDefaultConfig() );
            }

            const auto& comNode = root[ "com" ];

            // ── Bindings section ──
            if ( comNode[ "bindings" ] )
            {
                const auto& bindNode = comNode[ "bindings" ];
                if ( bindNode[ "config_path" ] )
                {
                    config.m_strBindingConfigPath =
                        bindNode[ "config_path" ].as< String >();
                    config.m_bBindingsLoaded = true;
                }
            }

            // ── Static Endpoints section ──
            if ( comNode[ "static_endpoints" ] )
            {
                const auto& staticNode = comNode[ "static_endpoints" ];
                if ( staticNode[ "config_path" ] )
                {
                    config.m_strStaticEndpointsPath =
                        staticNode[ "config_path" ].as< String >();
                    config.m_bStaticEndpointsLoaded = true;

                    config.m_staticInstanceConfig.m_strManifestPath =
                        config.m_strStaticEndpointsPath;
                }
                if ( staticNode[ "activate_on_load" ] )
                {
                    config.m_staticInstanceConfig.m_bActivateOnLoad =
                        staticNode[ "activate_on_load" ].as< bool >();
                }
                if ( staticNode[ "max_activation_retries" ] )
                {
                    config.m_staticInstanceConfig.m_iMaxActivationRetries =
                        staticNode[ "max_activation_retries" ].as< UInt32 >();
                }
            }

            // ── Discovery section ──
            if ( comNode[ "discovery" ] )
            {
                const auto& discNode = comNode[ "discovery" ];

                if ( discNode[ "enable_static" ] )
                {
                    config.m_discoveryConfig.m_bEnableStaticDiscovery =
                        discNode[ "enable_static" ].as< bool >();
                }
                if ( discNode[ "enable_central" ] )
                {
                    config.m_discoveryConfig.m_bEnableCentralDiscovery =
                        discNode[ "enable_central" ].as< bool >();
                }
                if ( discNode[ "enable_dynamic" ] )
                {
                    config.m_discoveryConfig.m_bEnableDynamicDiscovery =
                        discNode[ "enable_dynamic" ].as< bool >();
                }
                if ( discNode[ "static_config_path" ] )
                {
                    config.m_discoveryConfig.m_strStaticConfigPath =
                        discNode[ "static_config_path" ].as< String >();
                }
            }

            // ── QoS Profiles section ──
            if ( comNode[ "qos_profiles" ] &&
                 comNode[ "qos_profiles" ].IsSequence() )
            {
                for ( const auto& qosEntry : comNode[ "qos_profiles" ] )
                {
                    QosProfile profile;
                    if ( qosEntry[ "name" ] )
                    {
                        profile.m_strName = qosEntry[ "name" ].as< String >();
                    }
                    if ( qosEntry[ "reliability" ] )
                    {
                        profile.m_strReliability =
                            qosEntry[ "reliability" ].as< String >();
                    }
                    if ( qosEntry[ "history_depth" ] )
                    {
                        profile.m_iHistoryDepth =
                            qosEntry[ "history_depth" ].as< UInt32 >();
                    }
                    if ( qosEntry[ "max_blocking_time_ms" ] )
                    {
                        profile.m_iMaxBlockingTimeMs =
                            qosEntry[ "max_blocking_time_ms" ].as< UInt32 >();
                    }
                    if ( qosEntry[ "bandwidth_limit_kbps" ] )
                    {
                        profile.m_iBandwidthLimitKbps =
                            qosEntry[ "bandwidth_limit_kbps" ].as< UInt32 >();
                    }

                    if ( !profile.m_strName.empty() )
                    {
                        config.m_vecQosProfiles.push_back( std::move( profile ) );
                    }
                }
            }
        }
        catch ( const YAML::Exception& )
        {
            return Result< ComConfiguration >::FromError(
                MakeErrorCode( ComErrc::kConfigLoadFailed ) );
        }

        return Result< ComConfiguration >::FromValue( std::move( config ) );
    }

    // ====================================================================
    // Split Configuration Parsing
    // ====================================================================

    Result< ComConfiguration >
    ConfigParser::ParseSplit( const String& bindingConfigPath,
                              const String& staticEndpointsPath ) noexcept
    {
        ComConfiguration config = GetDefaultConfig();

        if ( !bindingConfigPath.empty() )
        {
            std::ifstream bCheck( bindingConfigPath );
            if ( bCheck.good() )
            {
                config.m_strBindingConfigPath = bindingConfigPath;
                config.m_bBindingsLoaded = true;
            }
        }

        if ( !staticEndpointsPath.empty() )
        {
            std::ifstream sCheck( staticEndpointsPath );
            if ( sCheck.good() )
            {
                config.m_strStaticEndpointsPath = staticEndpointsPath;
                config.m_bStaticEndpointsLoaded = true;
                config.m_staticInstanceConfig.m_strManifestPath =
                    staticEndpointsPath;
            }
        }

        return Result< ComConfiguration >::FromValue( std::move( config ) );
    }

    // ====================================================================
    // QoS Profile Parsing
    // ====================================================================

    Result< Vector< QosProfile > >
    ConfigParser::ParseQosProfiles( const String& configPath ) noexcept
    {
        if ( configPath.empty() )
        {
            return Result< Vector< QosProfile > >::FromError(
                MakeErrorCode( ComErrc::kInvalidArgument ) );
        }

        std::ifstream fileCheck( configPath );
        if ( !fileCheck.good() )
        {
            return Result< Vector< QosProfile > >::FromError(
                MakeErrorCode( ComErrc::kConfigLoadFailed ) );
        }
        fileCheck.close();

        Vector< QosProfile > profiles;

        try
        {
            YAML::Node root = YAML::LoadFile( configPath );
            YAML::Node qosSection;

            // Support both top-level and nested 'com.qos_profiles'
            if ( root[ "qos_profiles" ] )
            {
                qosSection = root[ "qos_profiles" ];
            }
            else if ( root[ "com" ] && root[ "com" ][ "qos_profiles" ] )
            {
                qosSection = root[ "com" ][ "qos_profiles" ];
            }

            if ( qosSection && qosSection.IsSequence() )
            {
                for ( const auto& entry : qosSection )
                {
                    QosProfile profile;
                    if ( entry[ "name" ] )
                    {
                        profile.m_strName = entry[ "name" ].as< String >();
                    }
                    if ( entry[ "reliability" ] )
                    {
                        profile.m_strReliability =
                            entry[ "reliability" ].as< String >();
                    }
                    if ( entry[ "history_depth" ] )
                    {
                        profile.m_iHistoryDepth =
                            entry[ "history_depth" ].as< UInt32 >();
                    }
                    if ( entry[ "max_blocking_time_ms" ] )
                    {
                        profile.m_iMaxBlockingTimeMs =
                            entry[ "max_blocking_time_ms" ].as< UInt32 >();
                    }
                    if ( entry[ "bandwidth_limit_kbps" ] )
                    {
                        profile.m_iBandwidthLimitKbps =
                            entry[ "bandwidth_limit_kbps" ].as< UInt32 >();
                    }

                    if ( !profile.m_strName.empty() )
                    {
                        profiles.push_back( std::move( profile ) );
                    }
                }
            }
        }
        catch ( const YAML::Exception& )
        {
            return Result< Vector< QosProfile > >::FromError(
                MakeErrorCode( ComErrc::kConfigLoadFailed ) );
        }

        return Result< Vector< QosProfile > >::FromValue( std::move( profiles ) );
    }

    // ====================================================================
    // Default Configuration
    // ====================================================================

    ComConfiguration ConfigParser::GetDefaultConfig() noexcept
    {
        ComConfiguration config;
        config.m_strBindingConfigPath = "/etc/lap/com/bindings.yaml";
        config.m_strStaticEndpointsPath = "/etc/lap/com/static_endpoints.yaml";
        config.m_discoveryConfig.m_bEnableStaticDiscovery = true;
        config.m_discoveryConfig.m_bEnableCentralDiscovery = true;
        config.m_discoveryConfig.m_bEnableDynamicDiscovery = true;
        config.m_staticInstanceConfig.m_strManifestPath =
            "/etc/lap/com/static_endpoints.yaml";
        config.m_staticInstanceConfig.m_bActivateOnLoad = true;
        return config;
    }

} // namespace com
} // namespace lap
