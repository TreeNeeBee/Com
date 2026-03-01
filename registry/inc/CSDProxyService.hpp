/**
 * @file        CSDProxyService.hpp
 * @author      LightAP Development Team
 * @brief       SD-Proxy service for cross-ECU service discovery
 * @date        2026/03/01
 * @details     Implements the SD-Proxy component from the architecture design (§5.2).
 *              The SD-Proxy maintains an LRU cache of remote services discovered via
 *              DDS Discovery Server, and registers itself in fixed registry slots
 *              (Slot 1 primary, Slot 512 backup) for transparent cross-ECU discovery.
 *
 *              Architecture Overview:
 *              ┌───────────────────────────────────┐
 *              │  CSDProxyService                   │
 *              │  ┌─────────────────────────────┐  │
 *              │  │ Remote Service Cache (LRU)   │  │
 *              │  │  max: 1024 entries           │  │
 *              │  │  TTL: 60s (configurable)     │  │
 *              │  └─────────────────────────────┘  │
 *              │  ┌─────────────────────────────┐  │
 *              │  │ ECU Provider Registry        │  │
 *              │  │  health check: 10s timeout   │  │
 *              │  └─────────────────────────────┘  │
 *              │  ┌─────────────────────────────┐  │
 *              │  │ Security Filter              │  │
 *              │  │  ECU whitelist + service ACL │  │
 *              │  └─────────────────────────────┘  │
 *              │  Fixed Slots: 1 (primary)         │
 *              │               512 (backup)        │
 *              └───────────────────────────────────┘
 *
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 Compliance:
 *              - SWS_CM_00001: Service discovery infrastructure
 *              - SWS_CM_00002: Cross-ECU service discovery
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §5.2 (SD Proxy Design)
 *              SERVICE_DISCOVERY_ARCHITECTURE.md §5.3 (Query Flow)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/03/01  <td>1.0      <td>Aii             <td>Initial implementation
 * </table>
 */
#ifndef LAP_COM_CSD_PROXY_SERVICE_HPP
#define LAP_COM_CSD_PROXY_SERVICE_HPP

// ==================== Project-Internal Headers ====================
#include "CServiceRegistry.hpp"
#include "ServiceSlot.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/CResult.hpp>
#include <lap/core/COptional.hpp>
#include <lap/core/CTypedef.hpp>

// ==================== Standard Library Headers ====================
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <set>
#include <map>

namespace lap
{
namespace com
{
namespace registry
{
    using lap::core::Result;
    using lap::core::Optional;
    using lap::core::Bool;
    using lap::core::UInt8;
    using lap::core::UInt16;
    using lap::core::UInt32;
    using lap::core::UInt64;
    using lap::core::Int32;

    // ==================== SD-Proxy Constants ====================

    /**
     * @brief SD-Proxy fixed slot and service ID constants
     * @reference SERVICE_DISCOVERY_ARCHITECTURE.md §5.2.1
     */
    struct SDProxyConstants
    {
        /// Primary instance slot index
        static constexpr UInt32 kPrimarySlot = 1;

        /// Backup instance slot index
        static constexpr UInt32 kBackupSlot = 512;

        /// Primary service ID (maps to Slot 1: 0x0001 & 1023 = 1)
        static constexpr UInt64 kPrimaryServiceId = 0x0001;

        /// Backup service ID (maps to Slot 512: 0x0200 & 1023 = 512)
        static constexpr UInt64 kBackupServiceId = 0x0200;

        /// Primary instance ID (encoding: service_id=0x0001, instance_no=1)
        static constexpr UInt64 kPrimaryInstanceId = 0x00010001;

        /// Backup instance ID (encoding: service_id=0x0001, instance_no=2)
        static constexpr UInt64 kBackupInstanceId = 0x00010002;

        /// SD-Proxy binding type identifier
        static constexpr const char* kBindingType = "sd_proxy";

        /// SD-Proxy major version
        static constexpr UInt32 kMajorVersion = 1;

        /// SD-Proxy minor version
        static constexpr UInt32 kMinorVersion = 0;
    };

    // ==================== Configuration ====================

    /**
     * @brief SD-Proxy service configuration
     * @reference SERVICE_DISCOVERY_ARCHITECTURE.md §5.2.2
     */
    struct SDProxyConfig
    {
        /// Maximum number of cached remote service entries
        UInt32 m_iMaxCacheSize = 1024;

        /// Default TTL for cached entries (seconds)
        UInt32 m_iDefaultTtlSeconds = 60;

        /// TTL cleanup thread interval (seconds)
        UInt32 m_iTtlCleanupIntervalSeconds = 5;

        /// ECU health check timeout (seconds)
        UInt32 m_iEcuHealthTimeoutSeconds = 10;

        /// ECU health check interval (seconds)
        UInt32 m_iEcuHealthCheckIntervalSeconds = 5;

        /// Enable security filter (ECU whitelist + service ACL)
        Bool m_bEnableSecurityFilter = false;

        /// Enable remote discovery (cross-ECU queries)
        Bool m_bEnableRemoteDiscovery = true;
    };

    // ==================== Data Structures ====================

    /**
     * @brief Remote service cache entry
     * @details Cached metadata for a service discovered on a remote ECU.
     *          Stored in LRU cache with configurable TTL.
     */
    struct RemoteServiceEntry
    {
        UInt64      m_serviceId;              ///< Service interface ID
        UInt64      m_instanceId;             ///< Service instance ID
        UInt32      m_majorVersion;           ///< Major version
        UInt32      m_minorVersion;           ///< Minor version
        char        m_bindingType[16];        ///< Transport binding type
        char        m_endpoint[80];           ///< Transport endpoint address
        char        m_sourceEcu[32];          ///< Source ECU identifier

        std::chrono::steady_clock::time_point m_expiry;   ///< TTL expiry time
        UInt64      m_iHitCount;              ///< Cache hit count (for LRU eviction)

        /**
         * @brief Default constructor
         */
        RemoteServiceEntry() noexcept
            : m_serviceId( 0 )
            , m_instanceId( 0 )
            , m_majorVersion( 0 )
            , m_minorVersion( 0 )
            , m_bindingType{}
            , m_endpoint{}
            , m_sourceEcu{}
            , m_expiry()
            , m_iHitCount( 0 )
        {
            std::memset( m_bindingType, 0, sizeof( m_bindingType ) );
            std::memset( m_endpoint, 0, sizeof( m_endpoint ) );
            std::memset( m_sourceEcu, 0, sizeof( m_sourceEcu ) );
        }

        /**
         * @brief Check if entry has expired
         * @return true if current time is past expiry
         */
        [[nodiscard]] Bool IsExpired() const noexcept
        {
            return std::chrono::steady_clock::now() > m_expiry;
        }

        /**
         * @brief Convert to ServiceSlot for query response
         * @return Populated ServiceSlot
         */
        [[nodiscard]] ServiceSlot ToServiceSlot() const noexcept
        {
            ServiceSlot slot;
            slot.m_serviceId = m_serviceId;
            slot.m_instanceId = m_instanceId;
            slot.m_majorVersion = m_majorVersion;
            slot.m_minorVersion = m_minorVersion;
            std::strncpy( slot.m_bindingType, m_bindingType,
                          sizeof( slot.m_bindingType ) - 1 );
            slot.m_bindingType[sizeof( slot.m_bindingType ) - 1] = '\0';
            std::strncpy( slot.m_endpoint, m_endpoint,
                          sizeof( slot.m_endpoint ) - 1 );
            slot.m_endpoint[sizeof( slot.m_endpoint ) - 1] = '\0';

            auto now = std::chrono::steady_clock::now();
            slot.m_lastHeartbeatNs = static_cast< UInt64 > (
                std::chrono::duration_cast< std::chrono::nanoseconds > (
                    now.time_since_epoch() ).count() );
            slot.m_heartbeatIntervalMs = 1000;
            slot.m_status.store( static_cast< UInt32 > ( SlotStatus::kActive ),
                                 std::memory_order_relaxed );
            slot.m_ownerPid = 0;  // Remote service, no local PID

            std::snprintf( slot.m_metadata, sizeof( slot.m_metadata ),
                           R"({"remote":true,"ecu":"%s"})", m_sourceEcu );

            return slot;
        }
    };

    /**
     * @brief Remote ECU information
     * @details Tracks known remote ECUs with heartbeat-based health detection.
     */
    struct ECUInfo
    {
        char        m_ecuId[32];              ///< ECU unique identifier
        char        m_ipAddress[48];          ///< ECU IP address
        UInt16      m_port;                   ///< ECU port
        std::chrono::steady_clock::time_point m_lastHeartbeat;  ///< Last heartbeat time
        Bool        m_bIsAlive;               ///< ECU alive flag

        /**
         * @brief Default constructor
         */
        ECUInfo() noexcept
            : m_ecuId{}
            , m_ipAddress{}
            , m_port( 0 )
            , m_lastHeartbeat( std::chrono::steady_clock::now() )
            , m_bIsAlive( true )
        {
            std::memset( m_ecuId, 0, sizeof( m_ecuId ) );
            std::memset( m_ipAddress, 0, sizeof( m_ipAddress ) );
        }
    };

    /**
     * @brief SD-Proxy statistics
     * @details Monitoring counters for cache performance and ECU health.
     */
    struct SDProxyStats
    {
        UInt64 m_iCacheHits = 0;              ///< Cache hit count
        UInt64 m_iCacheMisses = 0;            ///< Cache miss count
        UInt64 m_iCacheExpired = 0;           ///< Expired entries encountered
        UInt64 m_iCacheInsertions = 0;        ///< Total insertions
        UInt64 m_iCacheEvictions = 0;         ///< LRU evictions
        UInt64 m_iCacheInvalidations = 0;     ///< Explicit invalidations
        UInt64 m_iRemoteQueries = 0;          ///< Cross-ECU query attempts
        UInt64 m_iEcuTimeouts = 0;            ///< ECU heartbeat timeouts
        UInt64 m_iSecurityBlocked = 0;        ///< Blocked by security filter

        /**
         * @brief Calculate cache hit rate
         * @return Hit rate (0.0 ~ 1.0)
         */
        [[nodiscard]] double HitRate() const noexcept
        {
            UInt64 total = m_iCacheHits + m_iCacheMisses;
            return ( total > 0 )
                   ? static_cast< double > ( m_iCacheHits ) / static_cast< double > ( total )
                   : 0.0;
        }
    };

    /**
     * @brief Security policy for SD-Proxy
     * @details Configures ECU whitelist and service access control.
     */
    struct SDProxySecurityPolicy
    {
        std::set< std::string >                          m_allowedEcus;    ///< ECU whitelist
        std::map< UInt64, std::set< std::string > >      m_serviceAcl;    ///< Service ACL
        Bool m_bEnableAuditLog = false;                                    ///< Audit logging
    };

    // ==================== SD-Proxy Service Class ====================

    /**
     * @brief SD-Proxy service for cross-ECU service discovery
     *
     * @details Embedded in CRegistryDispatcher, automatically registered on startup.
     *          Provides:
     *          1. LRU cache (1024 entries) with TTL for remote services
     *          2. ECU provider registry with health monitoring
     *          3. Security filter (ECU whitelist + service ACL)
     *          4. Background threads for TTL cleanup and ECU health checks
     *
     *          Query flow (from CRegistryDispatcher::handleQueryService):
     *          1. Local slot lookup → direct return (< 500ns)
     *          2. SD-Proxy cache lookup → return cached result (< 1ms)
     *          3. Active query via DDS binding → Discovery Server (< 100ms)
     *
     *          Data ingestion (push from DDS binding discovery callbacks):
     *          - DdsBinding::OnDiscoveryChange() → bridge callback → OnRemoteServiceDiscovered()
     *          - SD-Proxy does NOT handle PDP/EDP itself
     *
     * @note Thread model:
     *       - Initialize/Stop: Called from CRegistryDispatcher init/shutdown (single thread)
     *       - FindRemoteService: Called from CRegistryDispatcher event loop (single thread)
     *       - OnRemoteServiceDiscovered/Removed: Thread-safe (called from DDS discovery thread)
     *       - InsertRemoteService/InvalidateService: Thread-safe (called from DDS callbacks)
     *       - TTL cleanup thread: Background, accesses cache under shared_mutex
     *       - ECU health thread: Background, accesses ECU registry under shared_mutex
     *
     * @reference SERVICE_DISCOVERY_ARCHITECTURE.md §5.2.2-5.2.4
     */
    class CSDProxyService final
    {
    public:
        /**
         * @brief Constructor
         */
        CSDProxyService() noexcept;

        /**
         * @brief Destructor (calls Stop if running)
         */
        ~CSDProxyService() noexcept;

        // Disable copy and move
        CSDProxyService( const CSDProxyService& ) = delete;
        CSDProxyService& operator=( const CSDProxyService& ) = delete;
        CSDProxyService( CSDProxyService&& ) = delete;
        CSDProxyService& operator=( CSDProxyService&& ) = delete;

    public:
        // ==================== Lifecycle ====================

        /**
         * @brief Initialize SD-Proxy and register in fixed registry slots
         * @param qmRegistry QM registry to register SD-Proxy slots in
         * @param config SD-Proxy configuration (optional, uses defaults)
         * @return Result< void > Success or error code
         *
         * @details Initialization sequence:
         *          1. Store configuration
         *          2. Register primary instance in Slot 1 (service_id=0x0001)
         *          3. Register backup instance in Slot 512 (service_id=0x0200)
         *          4. Set initialized flag
         *
         * @note Must be called from dispatcher init thread before event loop starts
         * @note Not thread-safe
         */
        Result< void > Initialize(
            CServiceRegistry& qmRegistry,
            const SDProxyConfig& config = SDProxyConfig{} ) noexcept;

        /**
         * @brief Start background threads (TTL cleanup, ECU health check)
         * @return Result< void > Success or error code
         *
         * @note Must be called after Initialize()
         * @note Not thread-safe
         */
        Result< void > Start() noexcept;

        /**
         * @brief Stop background threads and clean up
         * @note Thread-safe
         */
        void Stop() noexcept;

        /**
         * @brief Check if SD-Proxy is initialized
         * @return true if Initialize() succeeded
         * @note Thread-safe
         */
        [[nodiscard]] Bool IsInitialized() const noexcept
        {
            return m_bInitialized.load( std::memory_order_acquire );
        }

        /**
         * @brief Check if SD-Proxy is running (background threads active)
         * @return true if Start() was called and not yet stopped
         * @note Thread-safe
         */
        [[nodiscard]] Bool IsRunning() const noexcept
        {
            return m_bRunning.load( std::memory_order_acquire );
        }

        // ==================== DDS Discovery Bridge ====================

        /**
         * @brief Callback type for active service query via DDS binding
         * @details Called by SD-Proxy to query DDS binding's discovery state.
         *          Returns vector of discovered instance IDs for the given service.
         *          The binding queries DdsDiscoveryListener + Discovery Server.
         * @param serviceId Service ID to query
         * @return Vector of instance IDs currently known to the DDS binding
         */
        using ActiveQueryFunc = std::function< std::vector< UInt64 >( UInt64 serviceId ) >;

        /**
         * @brief Bridge: DDS binding discovered a remote service
         * @param serviceId  AUTOSAR service ID
         * @param instanceId Instance ID
         * @param bindingType Transport binding type (e.g., "dds")
         * @param endpoint   Endpoint address (e.g., DDS topic name)
         * @param sourceEcu  Source ECU identifier (participant GUID prefix)
         *
         * @details Called from DdsBinding's discovery change handler.
         *          Checks whitelist, then inserts into cache with default TTL.
         *          This is the primary data ingestion path for SD-Proxy.
         *
         * @note Thread-safe (called from DDS discovery thread)
         */
        void OnRemoteServiceDiscovered(
            UInt64 serviceId,
            UInt64 instanceId,
            const char* bindingType,
            const char* endpoint,
            const char* sourceEcu ) noexcept;

        /**
         * @brief Bridge: remote service removed (writer undiscovered)
         * @param serviceId  AUTOSAR service ID
         * @param instanceId Instance ID (0 = all instances of this service)
         *
         * @note Thread-safe (called from DDS discovery thread)
         */
        void OnRemoteServiceRemoved(
            UInt64 serviceId,
            UInt64 instanceId ) noexcept;

        /**
         * @brief Set the active query callback (to reach DDS binding on demand)
         * @param callback Function that queries DDS binding's discovered services
         *
         * @note Call before Start(). Not thread-safe (setup phase only).
         */
        void SetActiveQueryCallback( ActiveQueryFunc callback ) noexcept;

        /**
         * @brief Actively query Discovery Server via the DDS binding callback
         * @param serviceId Service ID to look up
         * @return Optional< ServiceSlot > if found via active query
         *
         * @details 1. Invoke m_activeQueryCallback to get instance list from DDS binding
         *          2. For each instance, create a RemoteServiceEntry and cache it
         *          3. Return the first match as ServiceSlot
         *
         * @note Thread-safe. Only used when cache misses and active query is available.
         */
        Optional< ServiceSlot > ActiveQueryService( UInt64 serviceId ) noexcept;

        // ==================== Service Whitelist ====================

        /**
         * @brief Set service ID whitelist (empty = allow all)
         * @param whitelist Set of allowed service IDs
         *
         * @details When non-empty, only services whose ID is in the whitelist
         *          will be accepted by OnRemoteServiceDiscovered() and cached.
         *          This is independent of the ECU-level SecurityFilter.
         *
         * @note Thread-safe (exclusive lock)
         */
        void SetServiceWhitelist( const std::set< UInt64 >& whitelist ) noexcept;

        /**
         * @brief Add a single service ID to the whitelist
         * @param serviceId Service ID to allow
         * @note Thread-safe
         */
        void AddToServiceWhitelist( UInt64 serviceId ) noexcept;

        /**
         * @brief Remove a service ID from the whitelist
         * @param serviceId Service ID to remove
         * @note Thread-safe
         */
        void RemoveFromServiceWhitelist( UInt64 serviceId ) noexcept;

        /**
         * @brief Clear the whitelist (allow all services)
         * @note Thread-safe
         */
        void ClearServiceWhitelist() noexcept;

        /**
         * @brief Check if a service ID is whitelisted
         * @param serviceId Service ID to check
         * @return true if whitelisted or whitelist is empty (allow-all mode)
         * @note Thread-safe
         */
        [[nodiscard]] Bool IsServiceWhitelisted( UInt64 serviceId ) const noexcept;

        /**
         * @brief Get current whitelist size
         * @return Number of entries in whitelist (0 = allow-all mode)
         * @note Thread-safe
         */
        [[nodiscard]] UInt32 GetWhitelistSize() const noexcept;

        // ==================== Cache Operations ====================

        /**
         * @brief Find a remote service in the SD-Proxy cache
         * @param serviceId Service ID to look up
         * @return Optional< ServiceSlot > Service info if cached and not expired
         *
         * @details Lookup flow:
         *          1. Compute cache key from serviceId
         *          2. Acquire shared lock on cache mutex
         *          3. Check if entry exists and not expired
         *          4. Increment hit counter, update stats
         *          5. Convert RemoteServiceEntry to ServiceSlot
         *
         * @note Thread-safe (shared lock)
         */
        Optional< ServiceSlot > FindRemoteService( UInt64 serviceId ) const noexcept;

        /**
         * @brief Insert a remote service into the SD-Proxy cache
         * @param entry Remote service entry to cache
         *
         * @details Insert flow:
         *          1. Acquire exclusive lock on cache mutex
         *          2. Compute TTL (per-service or default)
         *          3. Insert/update entry in cache
         *          4. Apply LRU eviction if over capacity
         *          5. Apply security filter if enabled
         *          6. Update stats
         *
         * @note Thread-safe (exclusive lock)
         */
        void InsertRemoteService( const RemoteServiceEntry& entry ) noexcept;

        /**
         * @brief Insert a remote service with explicit TTL
         * @param entry Remote service entry to cache
         * @param ttlSeconds TTL override in seconds
         *
         * @note Thread-safe (exclusive lock)
         */
        void InsertRemoteService( const RemoteServiceEntry& entry,
                                  UInt32 ttlSeconds ) noexcept;

        /**
         * @brief Invalidate (remove) a cached remote service
         * @param serviceId Service ID to invalidate
         *
         * @note Thread-safe (exclusive lock)
         */
        void InvalidateService( UInt64 serviceId ) noexcept;

        /**
         * @brief Invalidate all cached services from a specific ECU
         * @param ecuId ECU identifier string
         *
         * @note Thread-safe (exclusive lock)
         */
        void InvalidateByECU( const char* ecuId ) noexcept;

        /**
         * @brief Get current cache size
         * @return Number of entries in cache
         * @note Thread-safe (shared lock)
         */
        [[nodiscard]] UInt32 GetCacheSize() const noexcept;

        // ==================== ECU Provider Registry ====================

        /**
         * @brief Register a remote ECU
         * @param ecu ECU information to register
         *
         * @note Thread-safe (exclusive lock on ECU registry)
         */
        void RegisterECU( const ECUInfo& ecu ) noexcept;

        /**
         * @brief Update ECU heartbeat timestamp
         * @param ecuId ECU identifier
         *
         * @note Thread-safe (exclusive lock on ECU registry)
         */
        void UpdateECUHeartbeat( const char* ecuId ) noexcept;

        /**
         * @brief Get list of known alive ECUs
         * @return Vector of ECU identifiers
         * @note Thread-safe (shared lock on ECU registry)
         */
        [[nodiscard]] std::vector< std::string > GetKnownECUs() const noexcept;

        /**
         * @brief Get number of known ECUs
         * @return ECU count
         * @note Thread-safe (shared lock)
         */
        [[nodiscard]] UInt32 GetECUCount() const noexcept;

        // ==================== Security ====================

        /**
         * @brief Load security policy
         * @param policy Security policy configuration
         * @note Thread-safe (exclusive lock)
         */
        void LoadSecurityPolicy( const SDProxySecurityPolicy& policy ) noexcept;

        // ==================== Statistics ====================

        /**
         * @brief Get SD-Proxy statistics snapshot
         * @return Stats snapshot (non-atomic copy)
         * @note Thread-safe
         */
        [[nodiscard]] SDProxyStats GetStats() const noexcept;

        /**
         * @brief Get SD-Proxy configuration (read-only)
         * @return Configuration reference
         * @note Thread-safe after Initialize()
         */
        [[nodiscard]] const SDProxyConfig& GetConfig() const noexcept
        {
            return m_config;
        }

    private:
        // ==================== Internal Methods ====================

        /**
         * @brief Register SD-Proxy in a specific registry slot
         * @param registry Target registry
         * @param slotIndex Slot index
         * @param serviceId Service ID
         * @param instanceId Instance ID
         * @param endpoint Endpoint string
         * @param metadata JSON metadata string
         */
        void registerInSlot(
            CServiceRegistry& registry,
            UInt32 slotIndex,
            UInt64 serviceId,
            UInt64 instanceId,
            const char* endpoint,
            const char* metadata ) noexcept;

        /**
         * @brief TTL cleanup background thread function
         * @details Periodically scans cache and removes expired entries
         */
        void ttlCleanupThreadFunc() noexcept;

        /**
         * @brief ECU health check background thread function
         * @details Periodically checks ECU heartbeats and marks dead ECUs
         */
        void ecuHealthCheckThreadFunc() noexcept;

        /**
         * @brief Evict the least-recently-used cache entry
         * @note Must be called with exclusive cache lock held
         */
        void evictLRU() noexcept;

        /**
         * @brief Create a cache key from service ID
         * @param serviceId Service ID
         * @return String cache key
         */
        static std::string makeCacheKey( UInt64 serviceId ) noexcept;

        /**
         * @brief Apply security filter to a remote service entry
         * @param entry Entry to check
         * @return true if entry passes security checks
         * @note Must be called with shared security policy lock held
         */
        Bool passesSecurityFilter( const RemoteServiceEntry& entry ) const noexcept;

    private:
        // ==================== Configuration ====================
        SDProxyConfig       m_config;                   ///< Service configuration

        // ==================== Remote Service Cache (LRU + TTL) ====================
        std::unordered_map< std::string, RemoteServiceEntry > m_cache;
        mutable std::shared_mutex  m_cacheMutex;        ///< Cache r/w lock

        // ==================== Per-Service TTL Overrides ====================
        std::unordered_map< UInt64, UInt32 > m_ttlOverrides;  ///< service_id → TTL seconds
        mutable std::mutex m_ttlOverrideMutex;

        // ==================== ECU Provider Registry ====================
        std::unordered_map< std::string, ECUInfo > m_ecuRegistry;
        mutable std::shared_mutex  m_ecuMutex;          ///< ECU registry r/w lock

        // ==================== Security ====================
        SDProxySecurityPolicy m_securityPolicy;
        mutable std::shared_mutex  m_securityMutex;     ///< Security policy r/w lock

        // ==================== Statistics (atomic for lock-free reads) ====================
        mutable std::atomic< UInt64 > m_iCacheHits       { 0 };
        mutable std::atomic< UInt64 > m_iCacheMisses     { 0 };
        mutable std::atomic< UInt64 > m_iCacheExpired     { 0 };
        mutable std::atomic< UInt64 > m_iCacheInsertions  { 0 };
        mutable std::atomic< UInt64 > m_iCacheEvictions   { 0 };
        mutable std::atomic< UInt64 > m_iCacheInvalidations { 0 };
        mutable std::atomic< UInt64 > m_iRemoteQueries    { 0 };
        mutable std::atomic< UInt64 > m_iEcuTimeouts      { 0 };
        mutable std::atomic< UInt64 > m_iSecurityBlocked   { 0 };

        // ==================== DDS Discovery Bridge ====================
        ActiveQueryFunc     m_activeQueryCallback;       ///< Callback to DDS binding

        // ==================== Service Whitelist ====================
        std::set< UInt64 >  m_serviceWhitelist;          ///< Allowed service IDs (empty = all)
        mutable std::shared_mutex m_whitelistMutex;      ///< Whitelist r/w lock

        // ==================== Statistics (bridge-specific) ====================
        mutable std::atomic< UInt64 > m_iBridgeInsertions  { 0 }; ///< Bridge discoveries cached
        mutable std::atomic< UInt64 > m_iBridgeRemovals    { 0 }; ///< Bridge removals processed
        mutable std::atomic< UInt64 > m_iWhitelistBlocked  { 0 }; ///< Blocked by whitelist
        mutable std::atomic< UInt64 > m_iActiveQueries     { 0 }; ///< Active DS queries performed
        mutable std::atomic< UInt64 > m_iActiveQueryHits   { 0 }; ///< Active queries that found results

        // ==================== Background Threads ====================
        std::thread         m_ttlCleanupThread;         ///< TTL cleanup background thread
        std::thread         m_ecuHealthThread;           ///< ECU health check background thread
        std::atomic< Bool > m_bRunning    { false };    ///< Running flag
        std::atomic< Bool > m_bInitialized { false };   ///< Initialized flag
    };

} // namespace registry
} // namespace com
} // namespace lap

#endif // LAP_COM_CSD_PROXY_SERVICE_HPP
