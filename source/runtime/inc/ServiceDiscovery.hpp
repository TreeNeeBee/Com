/**
 * @file        ServiceDiscovery.hpp
 * @author      Aii
 * @brief       AUTOSAR R25-11 Service Discovery — Three-Tier Architecture
 * @date        2026/02/07
 * @details     Three-tier service discovery: Static Config → Discovery Server → Dynamic Discovery.
 *              Conforms to AUTOSAR EXP 7.2.1 (Central Service Discovery) and
 *              SWS_CM_02201 (Static Service Connection).
 *              Refactored to use lap::core AUTOSAR types exclusively.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §3 (Three-Tier Discovery)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author                   <th>Description
 * <tr><td>2025/11/19  <td>1.0      <td>Architecture Optimizer   <td>Initial implementation
 * <tr><td>2026/02/07  <td>2.0      <td>Aii                      <td>AUTOSAR types, code style cleanup
 * </table>
 */
#ifndef LAP_COM_SERVICE_DISCOVERY_HPP
#define LAP_COM_SERVICE_DISCOVERY_HPP

// ==================== Project-Internal Headers ====================
#include "ComTypes.hpp"
#include "ServiceHandleType.hpp"

// ==================== Cross-Module Headers ====================
#include <core/CInstanceSpecifier.hpp>
#include <core/CResult.hpp>
#include <core/COptional.hpp>
#include <core/CString.hpp>
#include <core/CTypedef.hpp>

// ==================== Standard Library Headers ====================
#include <chrono>
#include <functional>
#include <memory>

namespace lap
{
namespace com
{
namespace discovery
{
    // ========================================================================
    // Type aliases (prefer lap::core types)
    // ========================================================================
    using lap::core::Result;
    using lap::core::Optional;
    using lap::core::String;
    using lap::core::UInt16;
    using lap::core::UInt32;
    using lap::core::UInt64;
    using lap::core::Bool;
    using lap::core::Vector;
    using lap::core::Map;

    /**
     * @brief Unique pointer alias (template alias for lap::core::UniqueHandle)
     */
    template< typename T  >
    using UniquePtr = lap::core::UniqueHandle< T >;

    // ========================================================================
    // Service Instance Information (AUTOSAR Compliant)
    // ========================================================================

    /**
     * @brief Service instance metadata for discovery
     * @note Maps to AUTOSAR ServiceInstanceManifest (TPS_MANI_03312)
     */
    struct ServiceInstanceInfo final
    {
        // Service identification
        String              m_strServiceInterfaceName;   ///< e.g., "RadarService"
        InstanceIdentifierType m_iInstanceId{ 0 };       ///< e.g., 0x1234

        // Version information (AUTOSAR major.minor)
        UInt32              m_iMajorVersion{ 0 };
        UInt32              m_iMinorVersion{ 0 };

        // Binding information
        String              m_strBindingType;            ///< "coreipc", "dds", "someip"
        String              m_strNetworkEndpoint;        ///< IP:Port / Topic / Service Name

        // Lifecycle
        UInt64              m_iTimestampMs{ 0 };         ///< Registration timestamp
        UInt32              m_iTtlSeconds{ 0 };          ///< Time-to-live

        // Metadata (extensible)
        Map< String, String > m_mapMetadata;

        /**
         * @brief Check validity
         * @return true if all required fields are populated
         */
        Bool IsValid() const noexcept
        {
            return !m_strServiceInterfaceName.empty() &&
                   !m_strBindingType.empty() &&
                   m_iTtlSeconds > 0;
        }
    };

    // ========================================================================
    // Discovery Server Configuration (AUTOSAR EXP 7.2.1)
    // ========================================================================

    /**
     * @brief Fast-DDS Discovery Server endpoint configuration
     * @note AUTOSAR EXP 7.2.1 — Central Service Discovery
     */
    struct DiscoveryServerEndpoint final
    {
        String              m_strAddress;                ///< e.g., "192.168.1.100"
        UInt16              m_iPort{ 11811 };            ///< Default: 11811
        String              m_strTransport;              ///< "tcp", "udp", "shm"
        String              m_strGuidPrefix;             ///< Server GUID (optional)

        /**
         * @brief Format as URI string
         * @return Formatted endpoint string
         */
        String ToString() const noexcept
        {
            return m_strTransport + "://" + m_strAddress + ":" + std::to_string( m_iPort );
        }
    };

    /**
     * @brief Discovery Server client configuration
     */
    struct DiscoveryServerConfig final
    {
        Vector< DiscoveryServerEndpoint > m_vecServers;                         ///< Primary + Backup servers
        std::chrono::milliseconds         m_connectTimeout{ 1000 };             ///< Connection timeout
        std::chrono::milliseconds         m_queryTimeout{ 10 };                 ///< Query timeout (< 1ms target)
        UInt32                            m_iMaxRetryCount{ 3 };
        Bool                              m_bEnableFallback{ true };            ///< Auto fallback to dynamic
    };

    // ========================================================================
    // Static Service Configuration (AUTOSAR R25-11 SWS_CM_02201)
    // ========================================================================

    /**
     * @brief Static service endpoint configuration
     * @note TPS_MANI_03312–03315 — Static service connection manifest
     */
    struct StaticServiceEndpoint final
    {
        String              m_strServiceInterfaceName;
        InstanceIdentifierType m_iInstanceId{ 0 };
        String              m_strBindingType;
        String              m_strEndpoint;               ///< e.g., "tcp://192.168.1.10:30500"

        // QoS / Transport specific
        Map< String, String > m_mapTransportConfig;
    };

    /**
     * @brief Static service configuration loader
     * @note Conforms to AUTOSAR SWS_CM_02201 — Bypasses service discovery
     */
    class StaticServiceConfigLoader final
    {
    public:
        /**
         * @brief Load static endpoints from YAML manifest
         * @param configPath Path to static_endpoints.yaml
         * @return Result containing loaded endpoints
         */
        static Result< Vector< StaticServiceEndpoint > >
        LoadFromYAML( const String& configPath ) noexcept;
    };

    // ========================================================================
    // Service Discovery Manager (Three-Tier Strategy)
    // ========================================================================

    /**
     * @brief Service availability change callback
     */
    using ServiceChangeHandler =
        Function< void( const Vector< ServiceInstanceInfo >& ) >;

    /**
     * @brief Core service discovery manager
     * @note Implements AUTOSAR R25-11 three-tier discovery:
     *       1. Static configuration (0ms)
     *       2. Fast-DDS Discovery Server (0.5ms)
     *       3. Dynamic discovery fallback (5–100ms)
     */
    class ServiceDiscoveryManager final
    {
    public:
        /**
         * @brief Discovery configuration
         */
        struct Config final
        {
            String              m_strStaticConfigPath{ "/etc/lap/com/static_endpoints.yaml" };
            Optional< DiscoveryServerConfig > m_discoveryServerConfig;
            Bool                m_bEnableStaticDiscovery{ true };
            Bool                m_bEnableCentralDiscovery{ true };
            Bool                m_bEnableDynamicDiscovery{ true };
        };

        /**
         * @brief Discovery statistics (for monitoring)
         */
        struct Statistics final
        {
            UInt64              m_iStaticConfigHits{ 0 };       ///< Layer 1 hits
            UInt64              m_iDiscoveryServerHits{ 0 };    ///< Layer 2 hits
            UInt64              m_iDynamicDiscoveryHits{ 0 };   ///< Layer 3 hits
            UInt64              m_iTotalQueries{ 0 };
            UInt64              m_iFailedQueries{ 0 };
            std::chrono::microseconds m_avgLatency{ 0 };

            // Discovery Server health
            Bool                m_bDiscoveryServerAvailable{ false };
            UInt64              m_iDiscoveryServerFailures{ 0 };
        };

    public:
        ~ServiceDiscoveryManager() noexcept;

        // Non-copyable, non-movable
        ServiceDiscoveryManager( const ServiceDiscoveryManager& )            = delete;
        ServiceDiscoveryManager& operator=( const ServiceDiscoveryManager& ) = delete;
        ServiceDiscoveryManager( ServiceDiscoveryManager&& )                 = delete;
        ServiceDiscoveryManager& operator=( ServiceDiscoveryManager&& )      = delete;

        /**
         * @brief Create service discovery manager instance
         * @param config Discovery configuration
         * @return Result containing manager instance
         */
        static Result< UniquePtr< ServiceDiscoveryManager > >
        Create( const Config& config ) noexcept;

        /**
         * @brief Find service instances (synchronous, three-tier)
         * @param serviceInterfaceName Service interface name
         * @param instanceFilter Optional instance ID filter
         * @param requiredMajorVersion Minimum major version (0 = any)
         * @param requiredMinorVersion Minimum minor version (0 = any)
         * @return Result containing found service instances
         */
        Result< Vector< ServiceInstanceInfo > >
        FindService( const String& serviceInterfaceName,
                     const Optional< InstanceIdentifierType >& instanceFilter = Optional< InstanceIdentifierType > (),
                     UInt32 requiredMajorVersion = 0,
                     UInt32 requiredMinorVersion = 0 ) noexcept;

        /**
         * @brief Register service instance
         * @param serviceInfo Service metadata
         * @return Result indicating success
         */
        Result< void > RegisterService( const ServiceInstanceInfo& serviceInfo ) noexcept;

        /**
         * @brief Unregister service instance
         * @param instanceId Instance identifier
         * @return Result indicating success
         */
        Result< void > UnregisterService( InstanceIdentifierType instanceId ) noexcept;

        /**
         * @brief Subscribe to service availability changes
         * @param serviceInterfaceName Service to monitor
         * @param handler Callback for service state changes
         * @return FindServiceHandle for managing subscription
         */
        FindServiceHandle
        StartFindService( const String& serviceInterfaceName,
                          ServiceChangeHandler handler ) noexcept;

        /**
         * @brief Stop service monitoring
         * @param handle Handle returned by StartFindService
         */
        void StopFindService( FindServiceHandle handle ) noexcept;

        /**
         * @brief Get discovery statistics
         * @return Current statistics snapshot
         */
        Statistics GetStatistics() const noexcept;

        /**
         * @brief Health check
         * @return true if at least one discovery mechanism is available
         */
        Bool IsHealthy() const noexcept;

    private:
        explicit ServiceDiscoveryManager( const Config& config );

        // Layer 1: Static configuration
        Result< Vector< ServiceInstanceInfo > >
        findInStaticConfig( const String& serviceInterfaceName,
                            const Optional< InstanceIdentifierType >& instanceFilter );

        // Layer 2: Fast-DDS Discovery Server
        Result< Vector< ServiceInstanceInfo > >
        findInDiscoveryServer( const String& serviceInterfaceName,
                               const Optional< InstanceIdentifierType >& instanceFilter );

        // Layer 3: Dynamic discovery (Binding-specific)
        Result< Vector< ServiceInstanceInfo > >
        findInDynamicDiscovery( const String& serviceInterfaceName,
                                const Optional< InstanceIdentifierType >& instanceFilter );

    private:
        struct Impl;
        UniquePtr< Impl > m_pImpl;
    };

    // ========================================================================
    // Fast-DDS Discovery Client Interface
    // ========================================================================

    /**
     * @brief Single-instance availability change callback
     */
    using InstanceChangeHandler =
        Function< void( const ServiceInstanceInfo&, Bool /*available*/ ) >;

    /**
     * @brief Fast-DDS Discovery Server client
     * @note Implements AUTOSAR EXP 7.2.1 central service registry
     */
    class FastDdsDiscoveryClient final
    {
    public:
        ~FastDdsDiscoveryClient() noexcept;

        // Non-copyable, non-movable
        FastDdsDiscoveryClient( const FastDdsDiscoveryClient& )            = delete;
        FastDdsDiscoveryClient& operator=( const FastDdsDiscoveryClient& ) = delete;
        FastDdsDiscoveryClient( FastDdsDiscoveryClient&& )                 = delete;
        FastDdsDiscoveryClient& operator=( FastDdsDiscoveryClient&& )      = delete;

        /**
         * @brief Create Discovery Server client
         * @param config Server configuration
         * @return Result containing client instance
         */
        static Result< UniquePtr< FastDdsDiscoveryClient > >
        Create( const DiscoveryServerConfig& config ) noexcept;

        /**
         * @brief Connect to Discovery Server
         * @return Result indicating success
         */
        Result< void > Connect() noexcept;

        /**
         * @brief Register service to Discovery Server
         * @param serviceInfo Service metadata
         * @return Result indicating success
         */
        Result< void > RegisterService( const ServiceInstanceInfo& serviceInfo ) noexcept;

        /**
         * @brief Find services via Discovery Server
         * @param serviceInterfaceName Service to find
         * @param instanceFilter Optional instance filter
         * @return Result containing found services
         */
        Result< Vector< ServiceInstanceInfo > >
        FindService( const String& serviceInterfaceName,
                     const Optional< InstanceIdentifierType >& instanceFilter = Optional< InstanceIdentifierType > () ) noexcept;

        /**
         * @brief Subscribe to service changes via DDS Built-in Topics
         * @param handler Callback for service state changes
         * @return Result indicating success
         */
        Result< void > SubscribeServiceChanges( InstanceChangeHandler handler ) noexcept;

        /**
         * @brief Check if connected to Discovery Server
         * @return true if connected
         */
        Bool IsConnected() const noexcept;

        /**
         * @brief Disconnect from Discovery Server
         */
        void Disconnect() noexcept;

    private:
        explicit FastDdsDiscoveryClient( const DiscoveryServerConfig& config );

    private:
        struct Impl;
        UniquePtr< Impl > m_pImpl;
    };

} // namespace discovery
} // namespace com
} // namespace lap

#endif // LAP_COM_SERVICE_DISCOVERY_HPP
