/**
 * @file        CQosLoader.hpp
 * @author      Aii
 * @brief       QoS configuration loader — parses com_config.yaml and service_deploy.yaml
 * @date        2026/02/09
 * @details     Provides 3-level QoS resolution:
 *                Level 1 (highest priority): per-element overrides in service_deploy.yaml
 *                Level 2:                    named profile in com_config.yaml
 *                Level 3 (lowest):           built-in defaults (RELIABLE / VOLATILE / KEEP_LAST-10)
 *
 *              Implements a zero-external-dependency minimal YAML parser sufficient
 *              for the fixed schemas of com_config.yaml and service_deploy.yaml.
 *              Only standard C++17 library is used.
 *
 *              See GENERATOR.md §11.3.6–§11.3.7 for full resolution rules.
 *
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>init version
 * </table>
 */

#ifndef LAP_COM_GENERATOR_CQOSLOADER_HPP
#define LAP_COM_GENERATOR_CQOSLOADER_HPP

// ==================== Project-Internal Headers ====================
#include "CFidlAst.hpp"

// ==================== Standard Library Headers ====================
#include <unordered_map>
#include <vector>

namespace lap
{
namespace com
{
namespace generator
{

    // ==================== QoS Parameters ====================

    /**
     * @brief Resolved QoS parameters for a single DDS entity
     */
    struct QosParams {
        String reliability  = "RELIABLE";       ///< RELIABLE | BEST_EFFORT
        String durability   = "VOLATILE";        ///< VOLATILE | TRANSIENT_LOCAL | PERSISTENT
        String history      = "KEEP_LAST";       ///< KEEP_LAST | KEEP_ALL
        Int32  historyDepth = 10;                ///< History depth (used when history=KEEP_LAST)
        Int32  deadlineMs   = 0;                 ///< 0 = infinite / no deadline
        Int32  timeoutMs    = 5000;              ///< Service request timeout in ms
        Int32  retryCount   = 0;                 ///< Method retry count (0 = no retry)
    };

    // ==================== Element Kind ====================

    /**
     * @brief FIDL interface element kinds
     */
    enum class ElementKind : UInt8 {
        kEvent         = 0U,   ///< Franca broadcast
        kMethod        = 1U,   ///< Franca method (request–response)
        kFireAndForget = 2U,   ///< Franca fire-and-forget method
        kField         = 3U    ///< Franca attribute (field)
    };

    // ==================== Element QoS Binding ====================

    /**
     * @brief Per-element QoS binding from service_deploy.yaml
     */
    struct ElementQosBinding {
        String      elementName;               ///< Franca element name
        ElementKind kind        = ElementKind::kEvent;
        String      profileName;               ///< Named profile; if empty, use overrides
        QosParams   overrides;                 ///< Direct QoS field overrides
        Bool        hasOverrides = false;      ///< True when overrides are set
    };

    // ==================== Service Deploy Entry ====================

    /**
     * @brief Per-interface deployment information from service_deploy.yaml
     */
    struct ServiceDeployEntry {
        String                            interfaceName;
        UInt16                            instanceId = 1U;  ///< Default instance = 0x0001
        ::std::vector< ElementQosBinding > elements;
    };

    // ==================== CQosLoader ====================

    /**
     * @brief Loads and resolves QoS parameters from YAML config files
     *
     * @details Rule of Five — non-copyable, non-movable (pure loader utility).
     *          Load() must be called before ResolveQoS(). If no config files are
     *          provided, ResolveQoS() always returns built-in defaults.
     */
    class CQosLoader final {
    public:
        CQosLoader()  = default;
        ~CQosLoader() = default;

        CQosLoader( const CQosLoader& )            = delete;
        CQosLoader& operator=( const CQosLoader& ) = delete;
        CQosLoader( CQosLoader&& )                 = delete;
        CQosLoader& operator=( CQosLoader&& )      = delete;

        // ==================== Public Interface ====================

        /**
         * @brief  Load QoS configuration files
         * @param  comConfigPath     Path to com_config.yaml (empty = skip)
         * @param  serviceDeployPath Path to service_deploy.yaml (empty = skip)
         * @return true on success; false if a non-empty path cannot be read
         */
        Bool Load( const String& comConfigPath,
                   const String& serviceDeployPath );

        /**
         * @brief  Resolve QoS parameters for one interface element
         * @details Priority: per-element override > named profile > built-in default
         * @param  interfaceName  Franca interface name
         * @param  elementName    Franca element name (broadcast / method / attribute)
         * @param  kind           Element kind (for default selection)
         * @return Fully resolved QosParams
         */
        QosParams ResolveQoS( const String&  interfaceName,
                              const String&  elementName,
                              ElementKind    kind ) const noexcept;

        /**
         * @brief  Resolve instance ID for an interface
         * @param  interfaceName  Franca interface name
         * @param  cliOverride    Value from --instance-id CLI flag (0 = not set)
         * @return Resolved UInt16 instance ID (CLI > service_deploy > default 1)
         */
        UInt16 ResolveInstanceId( const String& interfaceName,
                                  UInt16         cliOverride ) const noexcept;

    private:
        // ==================== Internal Data ====================
        ::std::unordered_map< String, QosParams >  mProfiles;       ///< from com_config.yaml
        ::std::vector< ServiceDeployEntry >        mServiceDeploys; ///< from service_deploy.yaml

        // ==================== Private Helpers ====================
        Bool loadComConfig( const String& path );
        Bool loadServiceDeploy( const String& path );

        /**
         * @brief Apply a single key–value pair to a QosParams struct
         * @return true if the key was recognised
         */
        static Bool ApplyQosField( QosParams&    params,
                                   const String& key,
                                   const String& value ) noexcept;

        /** @brief Kind-specific built-in defaults (Level 3 fallback, see GENERATOR.md §11.3.6) */
        static QosParams DefaultQosParams( ElementKind kind ) noexcept;

        /** @brief Strip leading / trailing whitespace */
        static String Trim( const String& s ) noexcept;

        /** @brief Remove surrounding quotes (single or double) */
        static String Unquote( const String& s ) noexcept;

        /**
         * @brief Split "key: value" into {key, value}; returns {line, ""} on failure
         */
        static ::std::pair< String, String > SplitKV( const String& line ) noexcept;

        /** @brief Count leading spaces (indentation level in 2-space units) */
        static Int32 IndentLevel( const String& line ) noexcept;

        /** @brief Parse ElementKind from string ("event","method","fire_and_forget","field") */
        static ElementKind ParseElementKind( const String& s ) noexcept;
    };

} // namespace generator
} // namespace com
} // namespace lap

#endif // LAP_COM_GENERATOR_CQOSLOADER_HPP
