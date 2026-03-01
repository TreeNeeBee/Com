/**
 * @file        ServiceDiscovery.cpp
 * @author      Aii
 * @brief       AUTOSAR R25-11 Three-Tier Service Discovery Implementation
 * @date        2026/02/07
 * @details     Implements ServiceDiscoveryManager (Static → DiscoveryServer → Dynamic),
 *              StaticServiceConfigLoader (YAML parsing), and FastDdsDiscoveryClient
 *              (DomainParticipant CLIENT mode with USER_DATA service registration).
 *              Conforms to AUTOSAR SWS_CM_02201 (static), EXP 7.2.1 (central),
 *              and SERVICE_DISCOVERY_ARCHITECTURE.md §3.2 (three-tier strategy).
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §3.2 (ServiceDiscoveryManager)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>Initial three-tier implementation
 * <tr><td>2026/02/07  <td>2.0      <td>Aii     <td>Full FastDdsDiscoveryClient with DomainParticipant
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "ServiceDiscovery.hpp"
#include "ComTypes.hpp"

// ==================== Third-Party Headers ====================
#include <yaml-cpp/yaml.h>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipantListener.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/rtps/attributes/RTPSParticipantAttributes.hpp>
#include <fastdds/rtps/common/Locator.hpp>
#include <fastdds/rtps/participant/ParticipantDiscoveryInfo.hpp>
#include <fastdds/utils/IPLocator.hpp>
#include <fastdds/rtps/transport/TCPv4TransportDescriptor.hpp>

// ==================== Cross-Module Headers ====================
#include <core/CSync.hpp>

// ==================== Standard Library Headers ====================
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

namespace lap
{
namespace com
{
namespace discovery
{
    // Re-export UniquePtr alias from header for use in Impl
    template< typename T >
    using UniquePtr = lap::core::UniqueHandle< T >;
    template< typename T >
    using Atomic = lap::core::Atomic< T >;
    using lap::core::Int32;
    using lap::core::Int64;
    using lap::core::MakeUnique;

    // ====================================================================
    // File-local helper utilities
    // ====================================================================
    namespace
    {
        /**
         * @brief Get current time in milliseconds since epoch
         * @return Timestamp in milliseconds
         */
        UInt64 GetCurrentTimestampMs() noexcept
        {
            auto now = std::chrono::steady_clock::now();
            auto ms  = std::chrono::duration_cast< std::chrono::milliseconds > (
                now.time_since_epoch() );
            return static_cast< UInt64 > ( ms.count() );
        }

        /**
         * @brief Check if service instance matches an optional instance filter
         * @param instanceId Service instance ID to check
         * @param filter Optional filter — if empty, matches all
         * @return true if matches
         */
        Bool MatchesFilter( InstanceIdentifierType instanceId,
                            const Optional< InstanceIdentifierType >& filter ) noexcept
        {
            if ( !filter.has_value() )
            {
                return true;
            }
            return instanceId == filter.value();
        }

        /**
         * @brief Check if service version meets minimum requirements
         * @param actualMajor Actual major version
         * @param actualMinor Actual minor version
         * @param requiredMajor Required minimum major (0 = any)
         * @param requiredMinor Required minimum minor (0 = any)
         * @return true if version satisfies requirement
         */
        Bool IsVersionCompatible( UInt32 actualMajor, UInt32 actualMinor,
                                  UInt32 requiredMajor, UInt32 requiredMinor ) noexcept
        {
            if ( requiredMajor == 0 && requiredMinor == 0 )
            {
                return true;
            }
            if ( requiredMajor != 0 && actualMajor != requiredMajor )
            {
                return false;
            }
            if ( requiredMinor != 0 && actualMinor < requiredMinor )
            {
                return false;
            }
            return true;
        }

        /**
         * @brief Default polling interval for push-based discovery
         */
        static constexpr auto kDiscoveryPollingInterval =
            std::chrono::milliseconds( 200 );

        /**
         * @brief Default TTL for registered services (300 seconds)
         */
        static constexpr UInt32 kDefaultTtlSeconds = 300;

    } // anonymous namespace

    // ====================================================================
    // USER_DATA Encoding Helpers (Service → Participant QoS)
    // ====================================================================

    namespace
    {
        /**
         * @brief Encode service instance info into USER_DATA bytes
         * @param info Service instance info to encode
         * @return Byte vector for USER_DATA QoS
         *
         * @details Format (simple pipe-delimited text, easy to debug):
         *          "LAP|<service_name>|<instance_id>|<major>.<minor>|<binding>|<endpoint>"
         *          Prefix "LAP|" is used to identify LightAP participants.
         */
        std::vector< eprosima::fastdds::rtps::octet >
        EncodeServiceUserData( const ServiceInstanceInfo& info ) noexcept
        {
            std::ostringstream oss;
            oss << "LAP|"
                << info.m_strServiceInterfaceName << "|"
                << info.m_iInstanceId << "|"
                << info.m_iMajorVersion << "." << info.m_iMinorVersion << "|"
                << info.m_strBindingType << "|"
                << info.m_strNetworkEndpoint;

            auto str = oss.str();
            std::vector< eprosima::fastdds::rtps::octet > data(
                str.begin(), str.end() );
            return data;
        }

        /**
         * @brief Decode USER_DATA bytes into service instance info
         * @param data USER_DATA QoS bytes
         * @return Optional service instance info (empty if not a LightAP participant)
         */
        Optional< ServiceInstanceInfo >
        DecodeServiceUserData(
            const std::vector< eprosima::fastdds::rtps::octet >& data ) noexcept
        {
            if ( data.empty() )
            {
                return Optional< ServiceInstanceInfo > ();
            }

            String str( data.begin(), data.end() );

            // Must start with "LAP|"
            const String prefix = "LAP|";
            if ( str.rfind( prefix, 0 ) != 0 )
            {
                return Optional< ServiceInstanceInfo > ();
            }

            // Parse pipe-delimited fields after prefix
            String payload = str.substr( prefix.size() );
            Vector< String > fields;
            {
                std::istringstream iss( payload );
                String field;
                while ( std::getline( iss, field, '|' ) )
                {
                    fields.push_back( field );
                }
            }

            // Expect at least 5 fields: name, instanceId, version, binding, endpoint
            if ( fields.size() < 5 )
            {
                return Optional< ServiceInstanceInfo > ();
            }

            ServiceInstanceInfo info;
            info.m_strServiceInterfaceName = fields[ 0 ];

            try
            {
                info.m_iInstanceId = static_cast< InstanceIdentifierType > (
                    std::stoul( fields[ 1 ] ) );
            }
            catch ( const std::exception& )
            {
                return Optional< ServiceInstanceInfo > ();
            }

            // Parse "major.minor"
            const auto dotPos = fields[ 2 ].find( '.' );
            if ( dotPos != String::npos )
            {
                try
                {
                    info.m_iMajorVersion = static_cast< UInt32 > (
                        std::stoul( fields[ 2 ].substr( 0, dotPos ) ) );
                    info.m_iMinorVersion = static_cast< UInt32 > (
                        std::stoul( fields[ 2 ].substr( dotPos + 1 ) ) );
                }
                catch ( const std::exception& )
                {
                    // Leave version at 0.0
                }
            }

            info.m_strBindingType      = fields[ 3 ];
            info.m_strNetworkEndpoint  = fields[ 4 ];
            info.m_iTimestampMs        = GetCurrentTimestampMs();
            info.m_iTtlSeconds         = kDefaultTtlSeconds;

            return Optional< ServiceInstanceInfo > ( std::move( info ) );
        }

    } // anonymous namespace (USER_DATA helpers)

    // ====================================================================
    // StaticServiceConfigLoader Implementation
    // ====================================================================

    /**
     * @brief Load static endpoints from YAML manifest
     * @param configPath Path to static_endpoints.yaml
     * @return Vector of static service endpoints
     * @note SWS_CM_02201 — Static service connection bypass
     *
     * Expected YAML format:
     * @code
     * static_endpoints:
     *   - service_name: "RadarService"
     *     instance_id: 0x0010
     *     binding_type: "coreipc"
     *     endpoint: "shm://radar_service"
     *     transport_config:
     *       reliability: "reliable"
     *       history_depth: "10"
     *   - service_name: "CameraService"
     *     instance_id: 0x0011
     *     binding_type: "someip"
     *     endpoint: "tcp://192.168.1.10:30500"
     * @endcode
     */
    Result< Vector< StaticServiceEndpoint > >
    StaticServiceConfigLoader::LoadFromYAML( const String& configPath ) noexcept
    {
        // Validate input path
        if ( configPath.empty() )
        {
            return Result< Vector< StaticServiceEndpoint > >::FromError(
                MakeErrorCode( ComErrc::kInvalidArgument ) );
        }

        // Check file accessibility
        std::ifstream fileCheck( configPath );
        if ( !fileCheck.good() )
        {
            return Result< Vector< StaticServiceEndpoint > >::FromError(
                MakeErrorCode( ComErrc::kConfigLoadFailed ) );
        }
        fileCheck.close();

        Vector< StaticServiceEndpoint > endpoints;

        try
        {
            YAML::Node rootNode = YAML::LoadFile( configPath );

            if ( !rootNode[ "static_endpoints" ] ||
                 !rootNode[ "static_endpoints" ].IsSequence() )
            {
                // Empty or missing config — return empty vector (not an error)
                return Result< Vector< StaticServiceEndpoint > > ( std::move( endpoints ) );
            }

            const auto& entries = rootNode[ "static_endpoints" ];

            for ( const auto& entry : entries )
            {
                StaticServiceEndpoint ep;

                if ( entry[ "service_name" ] )
                {
                    ep.m_strServiceInterfaceName = entry[ "service_name" ].as< String > ();
                }
                if ( entry[ "instance_id" ] )
                {
                    ep.m_iInstanceId = static_cast< InstanceIdentifierType > (
                        entry[ "instance_id" ].as< unsigned int > () );
                }
                if ( entry[ "binding_type" ] )
                {
                    ep.m_strBindingType = entry[ "binding_type" ].as< String > ();
                }
                if ( entry[ "endpoint" ] )
                {
                    ep.m_strEndpoint = entry[ "endpoint" ].as< String > ();
                }

                // Load optional transport config map
                if ( entry[ "transport_config" ] && entry[ "transport_config" ].IsMap() )
                {
                    for ( const auto& kv : entry[ "transport_config" ] )
                    {
                        ep.m_mapTransportConfig[ kv.first.as< String > () ] =
                            kv.second.as< String > ();
                    }
                }

                // Skip entries with missing required fields
                if ( ep.m_strServiceInterfaceName.empty() ||
                     ep.m_strBindingType.empty() )
                {
                    continue;
                }

                endpoints.push_back( std::move( ep ) );
            }
        }
        catch ( const YAML::Exception& )
        {
            return Result< Vector< StaticServiceEndpoint > >::FromError(
                MakeErrorCode( ComErrc::kConfigLoadFailed ) );
        }

        return Result< Vector< StaticServiceEndpoint > > ( std::move( endpoints ) );
    }

    // ====================================================================
    // ServiceDiscoveryManager::Impl — PIMPL Implementation
    // ====================================================================

    /**
     * @brief Hidden implementation of ServiceDiscoveryManager
     *
     * @details Maintains:
     *   - Static configuration cache (Layer 1)
     *   - FastDdsDiscoveryClient pointer (Layer 2, optional)
     *   - Registered services map (for offer/unregister)
     *   - Active find subscriptions (for StartFindService / StopFindService)
     *   - Statistics counters
     */
    struct ServiceDiscoveryManager::Impl final
    {
        // ============================================================
        // Construction / Destruction
        // ============================================================

        explicit Impl( const Config& config ) noexcept
            : m_config( config )
            , m_bRunning( false )
            , m_iNextFindHandle( 1 )
        {
        }

        ~Impl() noexcept
        {
            Shutdown();
        }

        // Non-copyable, non-movable
        Impl( const Impl& )            = delete;
        Impl& operator=( const Impl& ) = delete;
        Impl( Impl&& )                 = delete;
        Impl& operator=( Impl&& )      = delete;

        // ============================================================
        // Initialization
        // ============================================================

        /**
         * @brief Initialize discovery subsystems
         * @return Result<void>
         *
         * @details Init order:
         *   1. Load static config (Layer 1)
         *   2. Connect to discovery server (Layer 2, optional)
         */
        Result< void > Initialize() noexcept
        {
            // Layer 1: Load static configuration
            if ( m_config.m_bEnableStaticDiscovery )
            {
                auto loadResult = StaticServiceConfigLoader::LoadFromYAML(
                    m_config.m_strStaticConfigPath );

                if ( loadResult.HasValue() )
                {
                    ScopedLock< Mutex > lock( m_staticMutex );
                    m_vecStaticEndpoints = std::move( loadResult ).Value();
                }
                // Non-fatal: static config may not be present
            }

            // Layer 2: Connect to Fast-DDS Discovery Server (optional)
            if ( m_config.m_bEnableCentralDiscovery &&
                 m_config.m_discoveryServerConfig.has_value() )
            {
                auto clientResult = FastDdsDiscoveryClient::Create(
                    m_config.m_discoveryServerConfig.value() );

                if ( clientResult.HasValue() )
                {
                    m_pDiscoveryClient = std::move( clientResult ).Value();
                    auto connectResult = m_pDiscoveryClient->Connect();
                    if ( connectResult.HasValue() )
                    {
                        ScopedLock< Mutex > lock( m_statsMutex );
                        m_statistics.m_bDiscoveryServerAvailable = true;
                    }
                }
                // Non-fatal: discovery server may not be deployed
            }

            m_bRunning.store( true, std::memory_order_release );
            return Result< void >::FromValue();
        }

        // ============================================================
        // Shutdown
        // ============================================================

        /**
         * @brief Shutdown all subsystems
         */
        void Shutdown() noexcept
        {
            Bool expected = true;
            if ( !m_bRunning.compare_exchange_strong( expected, false ) )
            {
                return;
            }

            // Stop all active find subscriptions
            {
                ScopedLock< Mutex > lock( m_findMutex );
                m_mapFindSubscriptions.clear();
            }

            // Stop polling thread
            if ( m_pollingThread.joinable() )
            {
                m_pollingThread.join();
            }

            // Disconnect discovery server
            if ( m_pDiscoveryClient )
            {
                m_pDiscoveryClient->Disconnect();
                m_pDiscoveryClient.reset();
            }
        }

        // ============================================================
        // Find Service (Three-Tier Waterfall)
        // ============================================================

        /**
         * @brief Layer 1: Find in static configuration
         * @param serviceName Service interface name
         * @param instanceFilter Optional instance ID filter
         * @return Matching endpoints converted to ServiceInstanceInfo
         */
        Result< Vector< ServiceInstanceInfo > >
        FindInStaticConfig( const String& serviceName,
                            const Optional< InstanceIdentifierType >& instanceFilter ) noexcept
        {
            Vector< ServiceInstanceInfo > results;
            ScopedLock< Mutex > lock( m_staticMutex );

            for ( const auto& ep : m_vecStaticEndpoints )
            {
                if ( ep.m_strServiceInterfaceName != serviceName )
                {
                    continue;
                }
                if ( !MatchesFilter( ep.m_iInstanceId, instanceFilter ) )
                {
                    continue;
                }

                ServiceInstanceInfo info;
                info.m_strServiceInterfaceName = ep.m_strServiceInterfaceName;
                info.m_iInstanceId             = ep.m_iInstanceId;
                info.m_strBindingType          = ep.m_strBindingType;
                info.m_strNetworkEndpoint      = ep.m_strEndpoint;
                info.m_iTimestampMs            = GetCurrentTimestampMs();
                info.m_iTtlSeconds             = kDefaultTtlSeconds;
                info.m_mapMetadata             = ep.m_mapTransportConfig;

                results.push_back( std::move( info ) );
            }

            return Result< Vector< ServiceInstanceInfo > > ( std::move( results ) );
        }

        /**
         * @brief Layer 2: Find via Fast-DDS Discovery Server
         * @param serviceName Service interface name
         * @param instanceFilter Optional instance ID filter
         * @return Matching services from discovery server
         */
        Result< Vector< ServiceInstanceInfo > >
        FindInDiscoveryServer( const String& serviceName,
                               const Optional< InstanceIdentifierType >& instanceFilter ) noexcept
        {
            if ( !m_pDiscoveryClient || !m_pDiscoveryClient->IsConnected() )
            {
                return Result< Vector< ServiceInstanceInfo > > (
                    Vector< ServiceInstanceInfo > {} );
            }

            return m_pDiscoveryClient->FindService( serviceName, instanceFilter );
        }

        /**
         * @brief Layer 3: Find via dynamic/binding-level discovery
         * @param serviceName Service interface name
         * @param instanceFilter Optional instance ID filter
         * @return Matching services from registered (offered) services
         *
         * @note Falls back to locally registered services (OfferService records)
         */
        Result< Vector< ServiceInstanceInfo > >
        FindInDynamicDiscovery( const String& serviceName,
                                const Optional< InstanceIdentifierType >& instanceFilter ) noexcept
        {
            Vector< ServiceInstanceInfo > results;
            ScopedLock< Mutex > lock( m_registeredMutex );

            for ( const auto& pair : m_mapRegisteredServices )
            {
                const auto& info = pair.second;
                if ( info.m_strServiceInterfaceName != serviceName )
                {
                    continue;
                }
                if ( !MatchesFilter( info.m_iInstanceId, instanceFilter ) )
                {
                    continue;
                }

                results.push_back( info );
            }

            return Result< Vector< ServiceInstanceInfo > > ( std::move( results ) );
        }

        // ============================================================
        // Service Registration (OfferService support)
        // ============================================================

        /**
         * @brief Register a service instance
         * @param serviceInfo Service metadata
         * @return Result<void>
         */
        Result< void > RegisterService( const ServiceInstanceInfo& serviceInfo ) noexcept
        {
            if ( !serviceInfo.IsValid() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kInvalidArgument ) );
            }

            {
                ScopedLock< Mutex > lock( m_registeredMutex );
                m_mapRegisteredServices[ serviceInfo.m_iInstanceId ] = serviceInfo;
            }

            // Also register to discovery server if available
            if ( m_pDiscoveryClient && m_pDiscoveryClient->IsConnected() )
            {
                auto regResult = m_pDiscoveryClient->RegisterService( serviceInfo );
                static_cast< void > ( regResult );  // Non-fatal
            }

            // Notify active find subscriptions
            NotifySubscribers( serviceInfo.m_strServiceInterfaceName );

            return Result< void >::FromValue();
        }

        /**
         * @brief Unregister a service instance
         * @param instanceId Instance identifier
         * @return Result<void>
         */
        Result< void > UnregisterService( InstanceIdentifierType instanceId ) noexcept
        {
            String serviceName;

            {
                ScopedLock< Mutex > lock( m_registeredMutex );
                auto it = m_mapRegisteredServices.find( instanceId );
                if ( it == m_mapRegisteredServices.end() )
                {
                    return Result< void >::FromError(
                        MakeErrorCode( ComErrc::kServiceNotOffered ) );
                }
                serviceName = it->second.m_strServiceInterfaceName;
                m_mapRegisteredServices.erase( it );
            }

            // Notify active find subscriptions of service removal
            if ( !serviceName.empty() )
            {
                NotifySubscribers( serviceName );
            }

            return Result< void >::FromValue();
        }

        // ============================================================
        // Push-Based Discovery (StartFindService / StopFindService)
        // ============================================================

        /**
         * @brief Active find subscription record
         */
        struct FindSubscription final
        {
            UInt64                     m_iHandleId{ 0 };
            String                     m_strServiceName;
            ServiceChangeHandler       m_handler;
            Vector< ServiceInstanceInfo > m_vecLastKnown;
        };

        /**
         * @brief Start push-based service discovery
         * @param serviceName Service to monitor
         * @param handler Callback for changes
         * @return FindServiceHandle
         */
        FindServiceHandle StartFindService( const String& serviceName,
                                            ServiceChangeHandler handler ) noexcept
        {
            UInt64 handleId = m_iNextFindHandle.fetch_add(
                1, std::memory_order_relaxed );

            FindSubscription sub;
            sub.m_iHandleId      = handleId;
            sub.m_strServiceName = serviceName;
            sub.m_handler        = std::move( handler );

            Bool needStartPolling = false;

            {
                ScopedLock< Mutex > lock( m_findMutex );
                needStartPolling = m_mapFindSubscriptions.empty();
                m_mapFindSubscriptions[ handleId ] = std::move( sub );
            }

            // Start polling thread if this is the first subscription
            if ( needStartPolling )
            {
                StartPollingWorker();
            }

            // Immediate initial callback with current results
            auto currentResult = FindService( serviceName,
                Optional< InstanceIdentifierType > (), 0, 0 );
            if ( currentResult.HasValue() && !currentResult.Value().empty() )
            {
                ScopedLock< Mutex > lock( m_findMutex );
                auto it = m_mapFindSubscriptions.find( handleId );
                if ( it != m_mapFindSubscriptions.end() )
                {
                    it->second.m_vecLastKnown = currentResult.Value();
                    it->second.m_handler( currentResult.Value() );
                }
            }

            return FindServiceHandle( handleId );
        }

        /**
         * @brief Stop push-based service discovery
         * @param handle Handle from StartFindService
         */
        void StopFindService( FindServiceHandle handle ) noexcept
        {
            ScopedLock< Mutex > lock( m_findMutex );
            m_mapFindSubscriptions.erase( handle.GetInternalId() );

            // Stop polling thread if no more subscriptions
            // (Thread will self-terminate on next iteration check)
        }

        // ============================================================
        // Three-Tier FindService (orchestrator)
        // ============================================================

        /**
         * @brief Execute three-tier waterfall search
         * @param serviceName Service interface name
         * @param instanceFilter Optional instance ID filter
         * @param requiredMajorVersion Minimum major version (0 = any)
         * @param requiredMinorVersion Minimum minor version (0 = any)
         * @return Aggregated matching service instances
         */
        Result< Vector< ServiceInstanceInfo > >
        FindService( const String& serviceName,
                     const Optional< InstanceIdentifierType >& instanceFilter,
                     UInt32 requiredMajorVersion,
                     UInt32 requiredMinorVersion ) noexcept
        {
            auto startTime = std::chrono::steady_clock::now();
            Vector< ServiceInstanceInfo > aggregatedResults;

            {
                ScopedLock< Mutex > lock( m_statsMutex );
                m_statistics.m_iTotalQueries++;
            }

            // Layer 1: Static configuration (fastest, 0ms)
            if ( m_config.m_bEnableStaticDiscovery )
            {
                auto staticResult = FindInStaticConfig( serviceName, instanceFilter );
                if ( staticResult.HasValue() && !staticResult.Value().empty() )
                {
                    for ( auto& info : staticResult.Value() )
                    {
                        aggregatedResults.push_back( std::move( info ) );
                    }
                    {
                        ScopedLock< Mutex > lock( m_statsMutex );
                        m_statistics.m_iStaticConfigHits++;
                    }
                }
            }

            // Layer 2: Discovery Server (~0.5ms)
            if ( m_config.m_bEnableCentralDiscovery )
            {
                auto serverResult = FindInDiscoveryServer( serviceName, instanceFilter );
                if ( serverResult.HasValue() && !serverResult.Value().empty() )
                {
                    for ( auto& info : serverResult.Value() )
                    {
                        // Deduplicate: skip if instance already found in Layer 1
                        Bool isDuplicate = false;
                        for ( const auto& existing : aggregatedResults )
                        {
                            if ( existing.m_iInstanceId == info.m_iInstanceId )
                            {
                                isDuplicate = true;
                                break;
                            }
                        }
                        if ( !isDuplicate )
                        {
                            aggregatedResults.push_back( std::move( info ) );
                        }
                    }
                    {
                        ScopedLock< Mutex > lock( m_statsMutex );
                        m_statistics.m_iDiscoveryServerHits++;
                    }
                }
            }

            // Layer 3: Dynamic/binding-level discovery (5–100ms)
            if ( m_config.m_bEnableDynamicDiscovery )
            {
                auto dynamicResult = FindInDynamicDiscovery( serviceName, instanceFilter );
                if ( dynamicResult.HasValue() && !dynamicResult.Value().empty() )
                {
                    for ( auto& info : dynamicResult.Value() )
                    {
                        Bool isDuplicate = false;
                        for ( const auto& existing : aggregatedResults )
                        {
                            if ( existing.m_iInstanceId == info.m_iInstanceId )
                            {
                                isDuplicate = true;
                                break;
                            }
                        }
                        if ( !isDuplicate )
                        {
                            aggregatedResults.push_back( std::move( info ) );
                        }
                    }
                    {
                        ScopedLock< Mutex > lock( m_statsMutex );
                        m_statistics.m_iDynamicDiscoveryHits++;
                    }
                }
            }

            // Apply version filter on aggregated results
            if ( requiredMajorVersion != 0 || requiredMinorVersion != 0 )
            {
                Vector< ServiceInstanceInfo > filtered;
                for ( auto& info : aggregatedResults )
                {
                    if ( IsVersionCompatible( info.m_iMajorVersion, info.m_iMinorVersion,
                                              requiredMajorVersion, requiredMinorVersion ) )
                    {
                        filtered.push_back( std::move( info ) );
                    }
                }
                aggregatedResults = std::move( filtered );
            }

            // Update latency statistics
            auto endTime = std::chrono::steady_clock::now();
            auto latencyUs = std::chrono::duration_cast< std::chrono::microseconds > (
                endTime - startTime );
            {
                ScopedLock< Mutex > lock( m_statsMutex );
                // Rolling average
                auto currentAvg = m_statistics.m_avgLatency.count();
                auto total      = m_statistics.m_iTotalQueries;
                if ( total > 0 )
                {
                    auto newAvg = ( currentAvg * static_cast< Int64 > ( total - 1 ) +
                                    latencyUs.count() ) / static_cast< Int64 > ( total );
                    m_statistics.m_avgLatency = std::chrono::microseconds( newAvg );
                }
            }

            if ( aggregatedResults.empty() )
            {
                ScopedLock< Mutex > lock( m_statsMutex );
                m_statistics.m_iFailedQueries++;
            }

            return Result< Vector< ServiceInstanceInfo > > (
                std::move( aggregatedResults ) );
        }

        // ============================================================
        // Statistics & Health
        // ============================================================

        /**
         * @brief Get discovery statistics snapshot
         * @return Current statistics
         */
        ServiceDiscoveryManager::Statistics GetStatistics() const noexcept
        {
            ScopedLock< Mutex > lock( m_statsMutex );
            return m_statistics;
        }

        /**
         * @brief Check if at least one discovery mechanism is healthy
         * @return true if operational
         */
        Bool IsHealthy() const noexcept
        {
            // Static config is always "healthy" if loaded
            if ( m_config.m_bEnableStaticDiscovery )
            {
                ScopedLock< Mutex > lock( m_staticMutex );
                if ( !m_vecStaticEndpoints.empty() )
                {
                    return true;
                }
            }

            // Discovery server is healthy if connected
            if ( m_pDiscoveryClient && m_pDiscoveryClient->IsConnected() )
            {
                return true;
            }

            // Dynamic discovery is always available (local registry)
            if ( m_config.m_bEnableDynamicDiscovery )
            {
                return true;
            }

            return false;
        }

    private:
        // ============================================================
        // Polling Worker (push-based discovery engine)
        // ============================================================

        /**
         * @brief Start background polling thread for change detection
         * @note Polls all layers and compares with last known state
         */
        void StartPollingWorker() noexcept
        {
            // Prevent duplicate threads
            if ( m_pollingThread.joinable() )
            {
                return;
            }

            m_pollingThread = std::thread( [this]()
            {
                while ( m_bRunning.load( std::memory_order_acquire ) )
                {
                    std::this_thread::sleep_for( kDiscoveryPollingInterval );

                    // Check if any subscriptions remain
                    ScopedLock< Mutex > lock( m_findMutex );
                    if ( m_mapFindSubscriptions.empty() )
                    {
                        break;
                    }

                    // Poll each subscription
                    for ( auto& pair : m_mapFindSubscriptions )
                    {
                        auto& sub = pair.second;
                        auto result = FindService( sub.m_strServiceName,
                            Optional< InstanceIdentifierType > (), 0, 0 );

                        if ( !result.HasValue() )
                        {
                            continue;
                        }

                        const auto& currentInstances = result.Value();

                        // Detect changes by comparing instance count and IDs
                        Bool hasChanged = false;
                        if ( currentInstances.size() != sub.m_vecLastKnown.size() )
                        {
                            hasChanged = true;
                        }
                        else
                        {
                            for ( std::size_t i = 0; i < currentInstances.size(); ++i )
                            {
                                if ( currentInstances[ i ].m_iInstanceId !=
                                     sub.m_vecLastKnown[ i ].m_iInstanceId )
                                {
                                    hasChanged = true;
                                    break;
                                }
                            }
                        }

                        if ( hasChanged )
                        {
                            sub.m_vecLastKnown = currentInstances;
                            sub.m_handler( currentInstances );
                        }
                    }
                } // while running
            } );
        }

        /**
         * @brief Notify all relevant find subscriptions of a service change
         * @param serviceName Changed service name
         */
        void NotifySubscribers( const String& serviceName ) noexcept
        {
            ScopedLock< Mutex > lock( m_findMutex );

            for ( auto& pair : m_mapFindSubscriptions )
            {
                auto& sub = pair.second;
                if ( sub.m_strServiceName != serviceName )
                {
                    continue;
                }

                auto result = FindService( serviceName,
                    Optional< InstanceIdentifierType > (), 0, 0 );
                if ( result.HasValue() )
                {
                    sub.m_vecLastKnown = result.Value();
                    sub.m_handler( result.Value() );
                }
            }
        }

    private:
        // ============================================================
        // Member Variables
        // ============================================================

        // Configuration
        Config                                  m_config;

        // Layer 1: Static endpoints cache
        mutable Mutex                      m_staticMutex;
        Vector< StaticServiceEndpoint >         m_vecStaticEndpoints;

        // Layer 2: Discovery server client
        UniquePtr< FastDdsDiscoveryClient >     m_pDiscoveryClient;

        // Layer 3: Locally registered (offered) services
        mutable Mutex                      m_registeredMutex;
        using RegisteredServiceMap = std::unordered_map< InstanceIdentifierType, ServiceInstanceInfo >;
        RegisteredServiceMap                    m_mapRegisteredServices;

        // Runtime state
        Atomic< Bool >                          m_bRunning;

        // Push-based discovery subscriptions
        Atomic< UInt64 >                        m_iNextFindHandle;
        mutable Mutex                      m_findMutex;
        using FindSubscriptionMap = std::unordered_map< UInt64, FindSubscription >;
        FindSubscriptionMap                     m_mapFindSubscriptions;
        std::thread                             m_pollingThread;

        // Statistics
        mutable Mutex                      m_statsMutex;
        ServiceDiscoveryManager::Statistics     m_statistics;
    };

    // ====================================================================
    // ServiceDiscoveryManager — Public Interface Delegation
    // ====================================================================

    ServiceDiscoveryManager::ServiceDiscoveryManager( const Config& config )
        : m_pImpl( MakeUnique< Impl > ( config ) )
    {
    }

    ServiceDiscoveryManager::~ServiceDiscoveryManager() noexcept = default;

    Result< UniquePtr< ServiceDiscoveryManager > >
    ServiceDiscoveryManager::Create( const Config& config ) noexcept
    {
        // Cannot use MakeUnique — constructor is private
        UniquePtr< ServiceDiscoveryManager > manager(
            new ServiceDiscoveryManager( config ) );

        auto initResult = manager->m_pImpl->Initialize();
        if ( !initResult.HasValue() )
        {
            return Result< UniquePtr< ServiceDiscoveryManager > >::FromError(
                initResult.Error() );
        }

        return Result< UniquePtr< ServiceDiscoveryManager > > (
            std::move( manager ) );
    }

    Result< Vector< ServiceInstanceInfo > >
    ServiceDiscoveryManager::FindService(
        const String& serviceInterfaceName,
        const Optional< InstanceIdentifierType >& instanceFilter,
        UInt32 requiredMajorVersion,
        UInt32 requiredMinorVersion ) noexcept
    {
        return m_pImpl->FindService( serviceInterfaceName, instanceFilter,
                                     requiredMajorVersion, requiredMinorVersion );
    }

    Result< void >
    ServiceDiscoveryManager::RegisterService( const ServiceInstanceInfo& serviceInfo ) noexcept
    {
        return m_pImpl->RegisterService( serviceInfo );
    }

    Result< void >
    ServiceDiscoveryManager::UnregisterService( InstanceIdentifierType instanceId ) noexcept
    {
        return m_pImpl->UnregisterService( instanceId );
    }

    FindServiceHandle
    ServiceDiscoveryManager::StartFindService(
        const String& serviceInterfaceName,
        ServiceChangeHandler handler ) noexcept
    {
        return m_pImpl->StartFindService( serviceInterfaceName, std::move( handler ) );
    }

    void
    ServiceDiscoveryManager::StopFindService( FindServiceHandle handle ) noexcept
    {
        m_pImpl->StopFindService( handle );
    }

    ServiceDiscoveryManager::Statistics
    ServiceDiscoveryManager::GetStatistics() const noexcept
    {
        return m_pImpl->GetStatistics();
    }

    Bool
    ServiceDiscoveryManager::IsHealthy() const noexcept
    {
        return m_pImpl->IsHealthy();
    }

    // ====================================================================
    // FastDdsDiscoveryClient::Impl — Full DDS Implementation
    // ====================================================================

    /**
     * @brief Hidden implementation of FastDdsDiscoveryClient
     *
     * @details Uses Fast-DDS DomainParticipant in CLIENT discovery mode:
     *       - Create a DomainParticipant with CLIENT discovery protocol
     *       - Set Discovery Server locators from config
     *       - Encode service info as pipe-delimited text into USER_DATA QoS
     *       - Listen for participant discovery events via DomainParticipantListener
     *       - Parse USER_DATA from discovered participants to extract service info
     *       - Track discovered services for FindService queries
     *
     * @note When Fast-DDS Discovery Server is unreachable, Connect() fails gracefully.
     *       The ServiceDiscoveryManager treats this as non-fatal and falls through
     *       to Layer 3 dynamic discovery.
     */
    struct FastDdsDiscoveryClient::Impl final
        : public eprosima::fastdds::dds::DomainParticipantListener
    {
        // ============================================================
        // Construction
        // ============================================================

        explicit Impl( const DiscoveryServerConfig& config ) noexcept
            : m_config( config )
            , m_bConnected( false )
            , m_pParticipant( nullptr )
        {
        }

        ~Impl() noexcept override
        {
            Disconnect();
        }

        // Non-copyable, non-movable
        Impl( const Impl& )            = delete;
        Impl& operator=( const Impl& ) = delete;
        Impl( Impl&& )                 = delete;
        Impl& operator=( Impl&& )      = delete;

        // ============================================================
        // DomainParticipantListener — Participant Discovery
        // ============================================================

        /**
         * @brief Called when a remote participant is discovered/removed
         * @param participant Local DomainParticipant
         * @param reason Discovery, removal, or QoS change
         * @param info Discovered participant's builtin topic data
         * @param should_be_ignored Output: whether to ignore this participant
         */
        void on_participant_discovery(
            eprosima::fastdds::dds::DomainParticipant* participant,
            eprosima::fastdds::rtps::ParticipantDiscoveryStatus reason,
            const eprosima::fastdds::dds::ParticipantBuiltinTopicData& info,
            bool& should_be_ignored ) override
        {
            static_cast< void > ( participant );
            should_be_ignored = false;

            using PDS = eprosima::fastdds::rtps::ParticipantDiscoveryStatus;

            if ( reason == PDS::DISCOVERED_PARTICIPANT ||
                 reason == PDS::CHANGED_QOS_PARTICIPANT )
            {
                // Decode USER_DATA to extract service info
                auto decoded = DecodeServiceUserData( info.user_data.data_vec() );
                if ( decoded.has_value() )
                {
                    auto& serviceInfo = decoded.value();

                    // Build unique key: serviceName + ":" + instanceId
                    const String key = serviceInfo.m_strServiceInterfaceName +
                        ":" + std::to_string( serviceInfo.m_iInstanceId );

                    {
                        ScopedLock< Mutex > lock( m_discoveredMutex );
                        m_mapDiscoveredServices[ key ] = serviceInfo;
                    }

                    // Notify change handler
                    ScopedLock< Mutex > lock( m_handlerMutex );
                    if ( m_changeHandler )
                    {
                        m_changeHandler( serviceInfo, true );
                    }
                }
            }
            else if ( reason == PDS::REMOVED_PARTICIPANT ||
                      reason == PDS::DROPPED_PARTICIPANT )
            {
                // Try to decode and remove from discovered map
                auto decoded = DecodeServiceUserData( info.user_data.data_vec() );
                if ( decoded.has_value() )
                {
                    auto& serviceInfo = decoded.value();
                    const String key = serviceInfo.m_strServiceInterfaceName +
                        ":" + std::to_string( serviceInfo.m_iInstanceId );

                    {
                        ScopedLock< Mutex > lock( m_discoveredMutex );
                        m_mapDiscoveredServices.erase( key );
                    }

                    // Notify change handler
                    ScopedLock< Mutex > lock( m_handlerMutex );
                    if ( m_changeHandler )
                    {
                        m_changeHandler( serviceInfo, false );
                    }
                }
            }
        }

        // ============================================================
        // Connection Management
        // ============================================================

        /**
         * @brief Connect to Discovery Server(s) via DomainParticipant CLIENT mode
         * @return Result<void> — success if participant created and server reachable
         *
         * @details Creates a DomainParticipant with:
         *   - DiscoveryProtocol::CLIENT
         *   - Discovery server locators from m_config.m_vecServers
         *   - TCP or UDP transport based on server config
         *   - Listener for on_participant_discovery callbacks
         */
        Result< void > Connect() noexcept
        {
            if ( m_pParticipant != nullptr )
            {
                m_bConnected.store( true, std::memory_order_release );
                return Result< void >::FromValue();
            }

            if ( m_config.m_vecServers.empty() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kInvalidArgument ) );
            }

            using namespace eprosima::fastdds;
            using namespace eprosima::fastdds::dds;

            // Configure participant QoS for CLIENT discovery
            DomainParticipantQos pqos;
            pqos.name( "LightAP_DiscoveryClient" );

            // Set CLIENT discovery protocol
            pqos.wire_protocol().builtin.discovery_config.discoveryProtocol =
                rtps::DiscoveryProtocol::CLIENT;
            pqos.wire_protocol().builtin.discovery_config
                .use_SIMPLE_EndpointDiscoveryProtocol = true;
            pqos.wire_protocol().builtin.discovery_config
                .use_STATIC_EndpointDiscoveryProtocol = false;

            Bool bNeedTcpTransport = false;

            // Add discovery server locators
            for ( const auto& server : m_config.m_vecServers )
            {
                rtps::Locator_t locator;

                if ( server.m_strTransport == "tcp" )
                {
                    locator.kind = LOCATOR_KIND_TCPv4;
                    bNeedTcpTransport = true;
                }
                else
                {
                    locator.kind = LOCATOR_KIND_UDPv4;
                }

                locator.port = server.m_iPort;

                if ( !rtps::IPLocator::setIPv4( locator, server.m_strAddress ) )
                {
                    LAP_COM_LOG_WARN << "FastDdsDiscoveryClient: Invalid server address: "
                                     << server.m_strAddress;
                    continue;
                }

                // Set GUID prefix if provided (server identification)
                if ( !server.m_strGuidPrefix.empty() )
                {
                    // Parse hex GUID prefix string (e.g., "44.53.00.5f.45.50.52.4f.53.49.4d.41")
                    std::istringstream guidStream( server.m_strGuidPrefix );
                    rtps::GuidPrefix_t guidPrefix;
                    String token;
                    Int32 idx = 0;
                    while ( std::getline( guidStream, token, '.' ) && idx < 12 )
                    {
                        try
                        {
                            guidPrefix.value[ idx ] = static_cast< rtps::octet > (
                                std::stoul( token, nullptr, 16 ) );
                        }
                        catch ( const std::exception& )
                        {
                            guidPrefix.value[ idx ] = 0;
                        }
                        ++idx;
                    }
                    pqos.wire_protocol().prefix = guidPrefix;
                }

                pqos.wire_protocol().builtin.discovery_config
                    .m_DiscoveryServers.push_back( locator );
            }

            // Configure TCP transport if needed
            if ( bNeedTcpTransport )
            {
                pqos.transport().use_builtin_transports = false;
                auto pTcpTransport = std::make_shared<
                    rtps::TCPv4TransportDescriptor > ();
                pTcpTransport->add_listener_port( 0 );
                pqos.transport().user_transports.push_back( pTcpTransport );
            }

            // Create participant with this listener (discovery events only)
            const auto kDiscoveryMask = StatusMask::none();
            m_pParticipant = DomainParticipantFactory::get_instance()->create_participant(
                0 /* domain 0 */, pqos, this, kDiscoveryMask );

            if ( m_pParticipant == nullptr )
            {
                LAP_COM_LOG_ERROR << "FastDdsDiscoveryClient: Failed to create participant";
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }

            m_bConnected.store( true, std::memory_order_release );
            LAP_COM_LOG_INFO << "FastDdsDiscoveryClient: Connected to "
                             << m_config.m_vecServers.size() << " discovery server(s)";

            return Result< void >::FromValue();
        }

        /**
         * @brief Disconnect from Discovery Server and destroy participant
         */
        void Disconnect() noexcept
        {
            m_bConnected.store( false, std::memory_order_release );

            {
                ScopedLock< Mutex > lock( m_handlerMutex );
                m_changeHandler = nullptr;
            }

            if ( m_pParticipant != nullptr )
            {
                eprosima::fastdds::dds::DomainParticipantFactory::get_instance()
                    ->delete_participant( m_pParticipant );
                m_pParticipant = nullptr;
            }

            {
                ScopedLock< Mutex > lock( m_discoveredMutex );
                m_mapDiscoveredServices.clear();
            }
        }

        // ============================================================
        // Service Operations
        // ============================================================

        /**
         * @brief Register a service by updating participant USER_DATA
         * @param serviceInfo Service metadata to publish
         * @return Result<void>
         *
         * @details Updates the participant's USER_DATA QoS with the encoded
         *          service info. Other discovery clients will receive this
         *          via on_participant_discovery(CHANGED_QOS_PARTICIPANT).
         *
         * @note Limitation: One participant can only advertise one service at a time
         *       via USER_DATA. For multi-service, we'd need to switch to a topic-based
         *       approach. This matches the 1:1 service:skeleton pattern in AUTOSAR.
         */
        Result< void > RegisterService( const ServiceInstanceInfo& serviceInfo ) noexcept
        {
            if ( !m_bConnected.load( std::memory_order_acquire ) ||
                 m_pParticipant == nullptr )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kServiceNotAvailable ) );
            }

            // Encode service info into USER_DATA
            auto userData = EncodeServiceUserData( serviceInfo );

            // Update participant QoS with new USER_DATA
            eprosima::fastdds::dds::DomainParticipantQos pqos;
            m_pParticipant->get_qos( pqos );
            pqos.user_data().data_vec( userData );

            auto ret = m_pParticipant->set_qos( pqos );
            if ( ret != eprosima::fastdds::dds::RETCODE_OK )
            {
                LAP_COM_LOG_ERROR << "FastDdsDiscoveryClient: Failed to set USER_DATA QoS";
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }

            // Also add to local discovered map
            const String key = serviceInfo.m_strServiceInterfaceName +
                ":" + std::to_string( serviceInfo.m_iInstanceId );
            {
                ScopedLock< Mutex > lock( m_discoveredMutex );
                m_mapDiscoveredServices[ key ] = serviceInfo;
            }

            LAP_COM_LOG_INFO << "FastDdsDiscoveryClient: Registered service '"
                             << serviceInfo.m_strServiceInterfaceName
                             << "' instance " << serviceInfo.m_iInstanceId;

            return Result< void >::FromValue();
        }

        /**
         * @brief Find services by name and optional instance filter
         * @param serviceName Service interface name to search
         * @param instanceFilter Optional instance ID filter
         * @return Vector of matching service instances
         */
        Result< Vector< ServiceInstanceInfo > >
        FindService( const String& serviceName,
                     const Optional< InstanceIdentifierType >& instanceFilter ) noexcept
        {
            Vector< ServiceInstanceInfo > results;
            ScopedLock< Mutex > lock( m_discoveredMutex );

            for ( const auto& pair : m_mapDiscoveredServices )
            {
                const auto& info = pair.second;
                if ( info.m_strServiceInterfaceName != serviceName )
                {
                    continue;
                }
                if ( !MatchesFilter( info.m_iInstanceId, instanceFilter ) )
                {
                    continue;
                }
                results.push_back( info );
            }

            return Result< Vector< ServiceInstanceInfo > > ( std::move( results ) );
        }

        /**
         * @brief Subscribe to service availability changes
         * @param handler Callback for individual service changes
         * @return Result<void>
         */
        Result< void > SubscribeServiceChanges( InstanceChangeHandler handler ) noexcept
        {
            if ( !m_bConnected.load( std::memory_order_acquire ) )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kServiceNotAvailable ) );
            }

            ScopedLock< Mutex > lock( m_handlerMutex );
            m_changeHandler = std::move( handler );
            return Result< void >::FromValue();
        }

        Bool IsConnected() const noexcept
        {
            return m_bConnected.load( std::memory_order_acquire );
        }

    private:
        // ============================================================
        // Member Variables
        // ============================================================
        DiscoveryServerConfig                       m_config;
        Atomic< Bool >                              m_bConnected;
        eprosima::fastdds::dds::DomainParticipant*  m_pParticipant;

        /// Discovered services from remote participants (key: "name:instanceId")
        mutable Mutex                          m_discoveredMutex;
        std::unordered_map< String, ServiceInstanceInfo > m_mapDiscoveredServices;

        /// Service change notification handler
        mutable Mutex                          m_handlerMutex;
        InstanceChangeHandler                       m_changeHandler;
    };

    // ====================================================================
    // FastDdsDiscoveryClient — Public Interface Delegation
    // ====================================================================

    FastDdsDiscoveryClient::FastDdsDiscoveryClient( const DiscoveryServerConfig& config )
        : m_pImpl( MakeUnique< Impl > ( config ) )
    {
    }

    FastDdsDiscoveryClient::~FastDdsDiscoveryClient() noexcept = default;

    Result< UniquePtr< FastDdsDiscoveryClient > >
    FastDdsDiscoveryClient::Create( const DiscoveryServerConfig& config ) noexcept
    {
        UniquePtr< FastDdsDiscoveryClient > client(
            new FastDdsDiscoveryClient( config ) );
        return Result< UniquePtr< FastDdsDiscoveryClient > > (
            std::move( client ) );
    }

    Result< void >
    FastDdsDiscoveryClient::Connect() noexcept
    {
        return m_pImpl->Connect();
    }

    Result< void >
    FastDdsDiscoveryClient::RegisterService( const ServiceInstanceInfo& serviceInfo ) noexcept
    {
        return m_pImpl->RegisterService( serviceInfo );
    }

    Result< Vector< ServiceInstanceInfo > >
    FastDdsDiscoveryClient::FindService(
        const String& serviceInterfaceName,
        const Optional< InstanceIdentifierType >& instanceFilter ) noexcept
    {
        return m_pImpl->FindService( serviceInterfaceName, instanceFilter );
    }

    Result< void >
    FastDdsDiscoveryClient::SubscribeServiceChanges( InstanceChangeHandler handler ) noexcept
    {
        return m_pImpl->SubscribeServiceChanges( std::move( handler ) );
    }

    Bool
    FastDdsDiscoveryClient::IsConnected() const noexcept
    {
        return m_pImpl->IsConnected();
    }

    void
    FastDdsDiscoveryClient::Disconnect() noexcept
    {
        m_pImpl->Disconnect();
    }

} // namespace discovery
} // namespace com
} // namespace lap
