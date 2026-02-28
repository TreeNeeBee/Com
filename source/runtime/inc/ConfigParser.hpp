/**
 * @file        ConfigParser.hpp
 * @author      Aii
 * @brief       AUTOSAR R25-11 Unified Configuration Parser for Com Module
 * @date        2026/02/16
 * @details     Unified configuration parser for the Com module.
 *              Parses binding configuration, static endpoints, QoS profiles,
 *              and discovery settings from a single or multiple YAML files.
 *              Integrates with BindingManager, StaticInstanceManager, and
 *              ServiceDiscoveryManager.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/16  <td>1.0      <td>Aii     <td>Initial implementation
 * </table>
 */
#ifndef LAP_COM_CONFIG_PARSER_HPP
#define LAP_COM_CONFIG_PARSER_HPP

// ==================== Project-Internal Headers ====================
#include "ComTypes.hpp"
#include "ServiceDiscovery.hpp"
#include "StaticInstanceManager.hpp"

// ==================== Cross-Module Headers ====================
#include <core/CResult.hpp>
#include <core/CString.hpp>
#include <core/CTypedef.hpp>
#include <core/COptional.hpp>

// ==================== Standard Library Headers ====================
#include <memory>

namespace lap
{
namespace com
{
    // ========================================================================
    // Type aliases
    // ========================================================================
    using lap::core::Result;
    using lap::core::Optional;
    using lap::core::String;
    using lap::core::Bool;
    using lap::core::UInt16;
    using lap::core::UInt32;
    using lap::core::Vector;
    using lap::core::Map;

    // ========================================================================
    // QoS Profile (AUTOSAR SWS_CM)
    // ========================================================================

    /**
     * @brief Quality of Service profile for service communication
     * @note Maps to AUTOSAR SWS_CM QoS parameters
     */
    struct QosProfile final
    {
        String              m_strName;                   ///< Profile name (e.g., "automotive", "best-effort")
        String              m_strReliability;             ///< "reliable" or "best-effort"
        UInt32              m_iHistoryDepth{ 1 };         ///< History depth for DDS-like transports
        UInt32              m_iMaxBlockingTimeMs{ 100 };  ///< Max blocking time in milliseconds
        Optional< UInt32 >  m_iBandwidthLimitKbps;       ///< Optional bandwidth limit
    };

    // ========================================================================
    // Unified Com Configuration
    // ========================================================================

    /**
     * @brief Parsed Com module configuration
     * @details Contains all configuration data needed by the Com runtime:
     *          - Binding configuration (library paths, priorities)
     *          - Static endpoints (SWS_CM_02201)
     *          - Discovery settings (three-tier config)
     *          - QoS profiles
     */
    struct ComConfiguration final
    {
        // Binding configuration
        String              m_strBindingConfigPath;
        Bool                m_bBindingsLoaded{ false };

        // Static endpoints configuration
        String              m_strStaticEndpointsPath;
        Bool                m_bStaticEndpointsLoaded{ false };

        // Discovery settings
        discovery::ServiceDiscoveryManager::Config m_discoveryConfig;

        // QoS profiles
        Vector< QosProfile > m_vecQosProfiles;

        // Static instance manager config
        discovery::StaticInstanceManager::Config m_staticInstanceConfig;
    };

    // ========================================================================
    // Unified Config Parser
    // ========================================================================

    /**
     * @brief Unified configuration parser for Com module
     *
     * @details Supports two modes:
     *   1. **Unified mode**: Single YAML file with all sections
     *   2. **Split mode**: Separate files for bindings, static endpoints, etc.
     *
     * Unified YAML format:
     * @code
     * com:
     *   bindings:
     *     config_path: "/etc/lap/com/bindings.yaml"
     *   static_endpoints:
     *     config_path: "/etc/lap/com/static_endpoints.yaml"
     *     activate_on_load: true
     *   discovery:
     *     enable_static: true
     *     enable_central: true
     *     enable_dynamic: true
     *     static_config_path: "/etc/lap/com/static_endpoints.yaml"
     *   qos_profiles:
     *     - name: "automotive"
     *       reliability: "reliable"
     *       history_depth: 10
     *       max_blocking_time_ms: 100
     *     - name: "best-effort"
     *       reliability: "best-effort"
     *       history_depth: 1
     * @endcode
     */
    class ConfigParser final
    {
    public:
        ConfigParser()  = default;
        ~ConfigParser() = default;

        // Non-copyable
        ConfigParser( const ConfigParser& )            = delete;
        ConfigParser& operator=( const ConfigParser& ) = delete;

        // Movable
        ConfigParser( ConfigParser&& )                 = default;
        ConfigParser& operator=( ConfigParser&& )      = default;

        /**
         * @brief Parse a unified Com configuration file
         * @param configPath Path to unified YAML config
         * @return Result containing parsed configuration
         */
        static Result< ComConfiguration >
        ParseUnified( const String& configPath ) noexcept;

        /**
         * @brief Parse individual configuration files (split mode)
         * @param bindingConfigPath Path to bindings.yaml (empty to skip)
         * @param staticEndpointsPath Path to static_endpoints.yaml (empty to skip)
         * @return Result containing parsed configuration
         */
        static Result< ComConfiguration >
        ParseSplit( const String& bindingConfigPath,
                    const String& staticEndpointsPath ) noexcept;

        /**
         * @brief Parse QoS profiles from a YAML node
         * @param configPath Path to config containing qos_profiles section
         * @return Result containing vector of QoS profiles
         */
        static Result< Vector< QosProfile > >
        ParseQosProfiles( const String& configPath ) noexcept;

        /**
         * @brief Get default configuration
         * @return ComConfiguration with default paths and settings
         */
        static ComConfiguration GetDefaultConfig() noexcept;
    };

} // namespace com
} // namespace lap

#endif // LAP_COM_CONFIG_PARSER_HPP
