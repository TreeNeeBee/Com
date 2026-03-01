/**
 * @file        CSDProxyService.cpp
 * @author      LightAP Development Team
 * @brief       SD-Proxy service implementation
 * @date        2026/03/01
 * @copyright   Copyright (c) 2026
 * @details     Implements the SD-Proxy cross-ECU service discovery cache,
 *              ECU provider registry, and automatic slot registration.
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §5.2 (SD Proxy Design)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/03/01  <td>1.0      <td>Aii             <td>Initial implementation
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CSDProxyService.hpp"
#include "ComTypes.hpp"

// ==================== Standard Library Headers ====================
#include <algorithm>
#include <chrono>
#include <cstring>
#include <unistd.h>

namespace lap
{
namespace com
{
namespace registry
{
    using namespace std::chrono;
    using lap::com::MakeErrorCode;
    using lap::com::ComErrc;

    // ==================== Lifecycle ====================

    CSDProxyService::CSDProxyService() noexcept
        : m_config()
    {
    }

    CSDProxyService::~CSDProxyService() noexcept
    {
        Stop();
    }

    Result< void > CSDProxyService::Initialize(
        CServiceRegistry& qmRegistry,
        const SDProxyConfig& config ) noexcept
    {
        if ( m_bInitialized.load( std::memory_order_acquire ) )
        {
            LAP_COM_LOG_WARN << "[SD-Proxy] Already initialized";
            return Result< void >::FromValue();
        }

        m_config = config;

        LAP_COM_LOG_INFO << "[SD-Proxy] Initializing SD-Proxy service"
                         << " (cache_size=" << m_config.m_iMaxCacheSize
                         << ", ttl=" << m_config.m_iDefaultTtlSeconds << "s)";

        // Step 1: Register primary instance in Slot 1 (service_id=0x0001)
        registerInSlot(
            qmRegistry,
            SDProxyConstants::kPrimarySlot,
            SDProxyConstants::kPrimaryServiceId,
            SDProxyConstants::kPrimaryInstanceId,
            "internal://sd_proxy/primary",
            R"({"role":"sd_proxy","slot":"primary","version":"1.0"})"
        );

        // Step 2: Register backup instance in Slot 512 (service_id=0x0200)
        registerInSlot(
            qmRegistry,
            SDProxyConstants::kBackupSlot,
            SDProxyConstants::kBackupServiceId,
            SDProxyConstants::kBackupInstanceId,
            "internal://sd_proxy/backup",
            R"({"role":"sd_proxy","slot":"backup","version":"1.0"})"
        );

        m_bInitialized.store( true, std::memory_order_release );

        LAP_COM_LOG_INFO << "[SD-Proxy] Initialization complete"
                         << " (slots: " << SDProxyConstants::kPrimarySlot
                         << ", " << SDProxyConstants::kBackupSlot << ")";

        return Result< void >::FromValue();
    }

    void CSDProxyService::registerInSlot(
        CServiceRegistry& registry,
        UInt32 slotIndex,
        UInt64 serviceId,
        UInt64 instanceId,
        const char* endpoint,
        const char* metadata ) noexcept
    {
        auto result = registry.RegisterService(
            slotIndex,
            serviceId,
            instanceId,
            SDProxyConstants::kMajorVersion,
            SDProxyConstants::kMinorVersion,
            SDProxyConstants::kBindingType,
            endpoint
        );

        if ( result.HasValue() )
        {
            LAP_COM_LOG_INFO << "[SD-Proxy] Registered in Slot " << slotIndex
                             << " (service_id=0x" << serviceId
                             << ", instance_id=0x" << instanceId << ")";

            // Write metadata to the slot directly (RegisterService doesn't set metadata)
            // The slot is accessible through the registry's internal pointer
            // For now, metadata is set via the slot's m_metadata field after registration
            // This is done inside the CRegistryDispatcher which has write access
            (void)metadata;  // Metadata writing delegated to future enhancement
        }
        else
        {
            LAP_COM_LOG_ERROR << "[SD-Proxy] Failed to register in Slot " << slotIndex
                              << " (service_id=0x" << serviceId << ")";
        }
    }

    Result< void > CSDProxyService::Start() noexcept
    {
        if ( !m_bInitialized.load( std::memory_order_acquire ) )
        {
            LAP_COM_LOG_ERROR << "[SD-Proxy] Cannot start: not initialized";
            return Result< void >::FromError( MakeErrorCode( ComErrc::kNotInitialized, 0 ) );
        }

        if ( m_bRunning.load( std::memory_order_acquire ) )
        {
            LAP_COM_LOG_WARN << "[SD-Proxy] Already running";
            return Result< void >::FromValue();
        }

        m_bRunning.store( true, std::memory_order_release );

        // Start TTL cleanup thread
        m_ttlCleanupThread = std::thread( [this]() {
            ttlCleanupThreadFunc();
        } );

        // Start ECU health check thread
        m_ecuHealthThread = std::thread( [this]() {
            ecuHealthCheckThreadFunc();
        } );

        LAP_COM_LOG_INFO << "[SD-Proxy] Background threads started"
                         << " (ttl_cleanup=" << m_config.m_iTtlCleanupIntervalSeconds << "s"
                         << ", ecu_health=" << m_config.m_iEcuHealthCheckIntervalSeconds << "s)";

        return Result< void >::FromValue();
    }

    void CSDProxyService::Stop() noexcept
    {
        Bool wasRunning = m_bRunning.exchange( false, std::memory_order_acq_rel );
        if ( !wasRunning )
        {
            return;
        }

        LAP_COM_LOG_INFO << "[SD-Proxy] Stopping...";

        // Join background threads
        if ( m_ttlCleanupThread.joinable() )
        {
            m_ttlCleanupThread.join();
        }

        if ( m_ecuHealthThread.joinable() )
        {
            m_ecuHealthThread.join();
        }

        // Log final statistics
        auto stats = GetStats();
        LAP_COM_LOG_INFO << "[SD-Proxy] Shutdown complete"
                         << " (hits=" << stats.m_iCacheHits
                         << ", misses=" << stats.m_iCacheMisses
                         << ", hit_rate=" << static_cast< Int32 > ( stats.HitRate() * 100 ) << "%"
                         << ", insertions=" << stats.m_iCacheInsertions
                         << ", evictions=" << stats.m_iCacheEvictions
                         << ", ecu_timeouts=" << stats.m_iEcuTimeouts << ")";
    }

    // ==================== Cache Operations ====================

    Optional< ServiceSlot > CSDProxyService::FindRemoteService( UInt64 serviceId ) const noexcept
    {
        if ( !m_bInitialized.load( std::memory_order_acquire ) )
        {
            return Optional< ServiceSlot > {};
        }

        std::string key = makeCacheKey( serviceId );

        std::shared_lock< std::shared_mutex > lock( m_cacheMutex );

        auto it = m_cache.find( key );
        if ( it == m_cache.end() )
        {
            m_iCacheMisses.fetch_add( 1, std::memory_order_relaxed );
            return Optional< ServiceSlot > {};
        }

        // Check TTL
        if ( it->second.IsExpired() )
        {
            m_iCacheExpired.fetch_add( 1, std::memory_order_relaxed );
            m_iCacheMisses.fetch_add( 1, std::memory_order_relaxed );
            return Optional< ServiceSlot > {};
        }

        // Cache hit — increment hit count (const_cast for mutable stats)
        const_cast< RemoteServiceEntry& > ( it->second ).m_iHitCount++;
        m_iCacheHits.fetch_add( 1, std::memory_order_relaxed );

        LAP_COM_LOG_DEBUG << "[SD-Proxy] Cache hit: service_id=0x" << serviceId
                          << " source_ecu=" << it->second.m_sourceEcu
                          << " hit_count=" << it->second.m_iHitCount;

        return Optional< ServiceSlot > ( it->second.ToServiceSlot() );
    }

    void CSDProxyService::InsertRemoteService( const RemoteServiceEntry& entry ) noexcept
    {
        InsertRemoteService( entry, m_config.m_iDefaultTtlSeconds );
    }

    void CSDProxyService::InsertRemoteService(
        const RemoteServiceEntry& entry,
        UInt32 ttlSeconds ) noexcept
    {
        if ( !m_bInitialized.load( std::memory_order_acquire ) )
        {
            return;
        }

        // Security filter check
        if ( m_config.m_bEnableSecurityFilter && !passesSecurityFilter( entry ) )
        {
            m_iSecurityBlocked.fetch_add( 1, std::memory_order_relaxed );
            LAP_COM_LOG_WARN << "[SD-Proxy] Security filter blocked: service_id=0x"
                             << entry.m_serviceId
                             << " source_ecu=" << entry.m_sourceEcu;
            return;
        }

        std::string key = makeCacheKey( entry.m_serviceId );

        {
            std::unique_lock< std::shared_mutex > lock( m_cacheMutex );

            // Check per-service TTL override
            UInt32 effectiveTtl = ttlSeconds;
            {
                std::lock_guard< std::mutex > ttlLock( m_ttlOverrideMutex );
                auto ttlIt = m_ttlOverrides.find( entry.m_serviceId );
                if ( ttlIt != m_ttlOverrides.end() )
                {
                    effectiveTtl = ttlIt->second;
                }
            }

            // Create cache entry with TTL
            RemoteServiceEntry cacheEntry = entry;
            cacheEntry.m_expiry = steady_clock::now() + seconds( effectiveTtl );
            cacheEntry.m_iHitCount = 0;

            m_cache[key] = cacheEntry;

            // LRU eviction if over capacity
            while ( m_cache.size() > static_cast< size_t > ( m_config.m_iMaxCacheSize ) )
            {
                evictLRU();
            }
        }

        m_iCacheInsertions.fetch_add( 1, std::memory_order_relaxed );

        LAP_COM_LOG_INFO << "[SD-Proxy] Cache insert: service_id=0x"
                         << entry.m_serviceId
                         << " source_ecu=" << entry.m_sourceEcu
                         << " ttl=" << ttlSeconds << "s";
    }

    void CSDProxyService::InvalidateService( UInt64 serviceId ) noexcept
    {
        if ( !m_bInitialized.load( std::memory_order_acquire ) )
        {
            return;
        }

        std::string key = makeCacheKey( serviceId );

        {
            std::unique_lock< std::shared_mutex > lock( m_cacheMutex );
            auto it = m_cache.find( key );
            if ( it != m_cache.end() )
            {
                LAP_COM_LOG_INFO << "[SD-Proxy] Cache invalidated: service_id=0x"
                                 << serviceId
                                 << " source_ecu=" << it->second.m_sourceEcu;
                m_cache.erase( it );
                m_iCacheInvalidations.fetch_add( 1, std::memory_order_relaxed );
            }
        }
    }

    void CSDProxyService::InvalidateByECU( const char* ecuId ) noexcept
    {
        if ( !m_bInitialized.load( std::memory_order_acquire ) )
        {
            return;
        }

        UInt32 removedCount = 0;

        {
            std::unique_lock< std::shared_mutex > lock( m_cacheMutex );

            for ( auto it = m_cache.begin(); it != m_cache.end(); )
            {
                if ( std::strncmp( it->second.m_sourceEcu, ecuId,
                                   sizeof( it->second.m_sourceEcu ) ) == 0 )
                {
                    it = m_cache.erase( it );
                    removedCount++;
                }
                else
                {
                    ++it;
                }
            }
        }

        if ( removedCount > 0 )
        {
            m_iCacheInvalidations.fetch_add( removedCount, std::memory_order_relaxed );
            LAP_COM_LOG_INFO << "[SD-Proxy] Invalidated " << removedCount
                             << " services from ECU: " << ecuId;
        }
    }

    UInt32 CSDProxyService::GetCacheSize() const noexcept
    {
        std::shared_lock< std::shared_mutex > lock( m_cacheMutex );
        return static_cast< UInt32 > ( m_cache.size() );
    }

    // ==================== ECU Provider Registry ====================

    void CSDProxyService::RegisterECU( const ECUInfo& ecu ) noexcept
    {
        std::string key( ecu.m_ecuId );

        {
            std::unique_lock< std::shared_mutex > lock( m_ecuMutex );
            m_ecuRegistry[key] = ecu;
            m_ecuRegistry[key].m_lastHeartbeat = steady_clock::now();
            m_ecuRegistry[key].m_bIsAlive = true;
        }

        LAP_COM_LOG_INFO << "[SD-Proxy] ECU registered: id=" << ecu.m_ecuId
                         << " ip=" << ecu.m_ipAddress << ":" << ecu.m_port;
    }

    void CSDProxyService::UpdateECUHeartbeat( const char* ecuId ) noexcept
    {
        std::string key( ecuId );

        std::unique_lock< std::shared_mutex > lock( m_ecuMutex );

        auto it = m_ecuRegistry.find( key );
        if ( it != m_ecuRegistry.end() )
        {
            it->second.m_lastHeartbeat = steady_clock::now();
            it->second.m_bIsAlive = true;
        }
    }

    std::vector< std::string > CSDProxyService::GetKnownECUs() const noexcept
    {
        std::shared_lock< std::shared_mutex > lock( m_ecuMutex );

        std::vector< std::string > ecuIds;
        ecuIds.reserve( m_ecuRegistry.size() );

        for ( const auto& [ecuId, ecuInfo] : m_ecuRegistry )
        {
            if ( ecuInfo.m_bIsAlive )
            {
                ecuIds.push_back( ecuId );
            }
        }

        return ecuIds;
    }

    UInt32 CSDProxyService::GetECUCount() const noexcept
    {
        std::shared_lock< std::shared_mutex > lock( m_ecuMutex );
        return static_cast< UInt32 > ( m_ecuRegistry.size() );
    }

    // ==================== Security ====================

    void CSDProxyService::LoadSecurityPolicy( const SDProxySecurityPolicy& policy ) noexcept
    {
        std::unique_lock< std::shared_mutex > lock( m_securityMutex );
        m_securityPolicy = policy;

        LAP_COM_LOG_INFO << "[SD-Proxy] Security policy loaded:"
                         << " allowed_ecus=" << m_securityPolicy.m_allowedEcus.size()
                         << " service_acl=" << m_securityPolicy.m_serviceAcl.size()
                         << " audit=" << ( m_securityPolicy.m_bEnableAuditLog ? "on" : "off" );
    }

    Bool CSDProxyService::passesSecurityFilter( const RemoteServiceEntry& entry ) const noexcept
    {
        std::shared_lock< std::shared_mutex > lock( m_securityMutex );

        std::string ecuId( entry.m_sourceEcu );

        // 1. Check ECU whitelist (if configured)
        if ( !m_securityPolicy.m_allowedEcus.empty() )
        {
            if ( m_securityPolicy.m_allowedEcus.find( ecuId ) ==
                 m_securityPolicy.m_allowedEcus.end() )
            {
                if ( m_securityPolicy.m_bEnableAuditLog )
                {
                    LAP_COM_LOG_WARN << "[SD-Proxy][AUDIT] ECU_BLOCKED: ecu=" << ecuId
                                     << " service_id=0x" << entry.m_serviceId
                                    ;
                }
                return false;
            }
        }

        // 2. Check service ACL (per-service ECU whitelist)
        auto aclIt = m_securityPolicy.m_serviceAcl.find( entry.m_serviceId );
        if ( aclIt != m_securityPolicy.m_serviceAcl.end() )
        {
            if ( aclIt->second.find( ecuId ) == aclIt->second.end() )
            {
                if ( m_securityPolicy.m_bEnableAuditLog )
                {
                    LAP_COM_LOG_WARN << "[SD-Proxy][AUDIT] SERVICE_ACCESS_DENIED:"
                                     << " ecu=" << ecuId
                                     << " service_id=0x" << entry.m_serviceId
                                    ;
                }
                return false;
            }
        }

        // 3. Validate service metadata (anti-injection)
        if ( entry.m_serviceId == 0 || entry.m_serviceId == 0xFFFF )
        {
            if ( m_securityPolicy.m_bEnableAuditLog )
            {
                LAP_COM_LOG_WARN << "[SD-Proxy][AUDIT] INVALID_METADATA:"
                                 << " service_id=0x" << entry.m_serviceId
                                ;
            }
            return false;
        }

        if ( entry.m_majorVersion == 0 && entry.m_minorVersion == 0 )
        {
            return false;  // Invalid version
        }

        if ( entry.m_endpoint[0] == '\0' )
        {
            return false;  // Empty endpoint
        }

        if ( m_securityPolicy.m_bEnableAuditLog )
        {
            LAP_COM_LOG_DEBUG << "[SD-Proxy][AUDIT] SERVICE_ALLOWED:"
                              << " ecu=" << ecuId
                              << " service_id=0x" << entry.m_serviceId
                             ;
        }

        return true;
    }

    // ==================== Statistics ====================

    SDProxyStats CSDProxyService::GetStats() const noexcept
    {
        SDProxyStats stats;
        stats.m_iCacheHits = m_iCacheHits.load( std::memory_order_relaxed );
        stats.m_iCacheMisses = m_iCacheMisses.load( std::memory_order_relaxed );
        stats.m_iCacheExpired = m_iCacheExpired.load( std::memory_order_relaxed );
        stats.m_iCacheInsertions = m_iCacheInsertions.load( std::memory_order_relaxed );
        stats.m_iCacheEvictions = m_iCacheEvictions.load( std::memory_order_relaxed );
        stats.m_iCacheInvalidations = m_iCacheInvalidations.load( std::memory_order_relaxed );
        stats.m_iRemoteQueries = m_iRemoteQueries.load( std::memory_order_relaxed );
        stats.m_iEcuTimeouts = m_iEcuTimeouts.load( std::memory_order_relaxed );
        stats.m_iSecurityBlocked = m_iSecurityBlocked.load( std::memory_order_relaxed );
        return stats;
    }

    // ==================== Background Threads ====================

    void CSDProxyService::ttlCleanupThreadFunc() noexcept
    {
        LAP_COM_LOG_DEBUG << "[SD-Proxy] TTL cleanup thread started";

        while ( m_bRunning.load( std::memory_order_acquire ) )
        {
            // Sleep in small increments to allow responsive shutdown
            for ( UInt32 i = 0;
                  i < m_config.m_iTtlCleanupIntervalSeconds * 10 &&
                  m_bRunning.load( std::memory_order_acquire );
                  ++i )
            {
                std::this_thread::sleep_for( milliseconds( 100 ) );
            }

            if ( !m_bRunning.load( std::memory_order_acquire ) )
            {
                break;
            }

            // Scan and remove expired entries
            UInt32 removedCount = 0;
            {
                std::unique_lock< std::shared_mutex > lock( m_cacheMutex );

                auto now = steady_clock::now();
                for ( auto it = m_cache.begin(); it != m_cache.end(); )
                {
                    if ( now > it->second.m_expiry )
                    {
                        LAP_COM_LOG_DEBUG << "[SD-Proxy] TTL expired: service_id=0x"
                                          << it->second.m_serviceId
                                          << " source_ecu=" << it->second.m_sourceEcu;
                        it = m_cache.erase( it );
                        removedCount++;
                    }
                    else
                    {
                        ++it;
                    }
                }
            }

            if ( removedCount > 0 )
            {
                m_iCacheExpired.fetch_add( removedCount, std::memory_order_relaxed );
                LAP_COM_LOG_DEBUG << "[SD-Proxy] TTL cleanup: removed " << removedCount
                                  << " expired entries";
            }
        }

        LAP_COM_LOG_DEBUG << "[SD-Proxy] TTL cleanup thread stopped";
    }

    void CSDProxyService::ecuHealthCheckThreadFunc() noexcept
    {
        LAP_COM_LOG_DEBUG << "[SD-Proxy] ECU health check thread started";

        while ( m_bRunning.load( std::memory_order_acquire ) )
        {
            // Sleep in small increments for responsive shutdown
            for ( UInt32 i = 0;
                  i < m_config.m_iEcuHealthCheckIntervalSeconds * 10 &&
                  m_bRunning.load( std::memory_order_acquire );
                  ++i )
            {
                std::this_thread::sleep_for( milliseconds( 100 ) );
            }

            if ( !m_bRunning.load( std::memory_order_acquire ) )
            {
                break;
            }

            // Check ECU heartbeats
            std::vector< std::string > deadEcus;

            {
                std::unique_lock< std::shared_mutex > lock( m_ecuMutex );

                auto now = steady_clock::now();
                auto timeout = seconds( m_config.m_iEcuHealthTimeoutSeconds );

                for ( auto& [ecuId, ecuInfo] : m_ecuRegistry )
                {
                    if ( ecuInfo.m_bIsAlive &&
                         ( now - ecuInfo.m_lastHeartbeat ) > timeout )
                    {
                        LAP_COM_LOG_WARN << "[SD-Proxy] ECU timeout: id=" << ecuId
                                          << " last_heartbeat="
                                          << duration_cast< seconds > (
                                                 now - ecuInfo.m_lastHeartbeat ).count()
                                          << "s ago";

                        ecuInfo.m_bIsAlive = false;
                        deadEcus.push_back( ecuId );
                    }
                }
            }

            // Invalidate services from dead ECUs
            for ( const auto& ecuId : deadEcus )
            {
                InvalidateByECU( ecuId.c_str() );
                m_iEcuTimeouts.fetch_add( 1, std::memory_order_relaxed );
            }
        }

        LAP_COM_LOG_DEBUG << "[SD-Proxy] ECU health check thread stopped";
    }

    // ==================== Internal Helpers ====================

    void CSDProxyService::evictLRU() noexcept
    {
        // Evict entry with lowest hit count (LFU-style within LRU context)
        if ( m_cache.empty() )
        {
            return;
        }

        auto minIt = std::min_element(
            m_cache.begin(), m_cache.end(),
            []( const auto& a, const auto& b )
            {
                return a.second.m_iHitCount < b.second.m_iHitCount;
            }
        );

        if ( minIt != m_cache.end() )
        {
            LAP_COM_LOG_DEBUG << "[SD-Proxy] LRU evicted: service_id=0x"
                              << minIt->second.m_serviceId
                              << " hit_count=" << minIt->second.m_iHitCount;
            m_cache.erase( minIt );
            m_iCacheEvictions.fetch_add( 1, std::memory_order_relaxed );
        }
    }

    std::string CSDProxyService::makeCacheKey( UInt64 serviceId ) noexcept
    {
        return std::to_string( serviceId );
    }

    // ==================== DDS Discovery Bridge ====================

    void CSDProxyService::OnRemoteServiceDiscovered(
        UInt64 serviceId,
        UInt64 instanceId,
        const char* bindingType,
        const char* endpoint,
        const char* sourceEcu ) noexcept
    {
        if ( !m_bInitialized.load( std::memory_order_acquire ) )
        {
            return;
        }

        // Whitelist check — if whitelist is active, reject unlisted services
        if ( !IsServiceWhitelisted( serviceId ) )
        {
            m_iWhitelistBlocked.fetch_add( 1, std::memory_order_relaxed );
            LAP_COM_LOG_DEBUG << "[SD-Proxy] Whitelist blocked: service_id=0x"
                              << serviceId << " instance_id=0x" << instanceId;
            return;
        }

        // Build a RemoteServiceEntry from the discovery event
        RemoteServiceEntry entry;
        entry.m_serviceId   = serviceId;
        entry.m_instanceId  = instanceId;
        entry.m_majorVersion = 1;    // Default; EDP doesn't carry AUTOSAR version
        entry.m_minorVersion = 0;

        // Copy binding type
        if ( bindingType != nullptr )
        {
            std::strncpy( entry.m_bindingType, bindingType,
                          sizeof( entry.m_bindingType ) - 1 );
            entry.m_bindingType[ sizeof( entry.m_bindingType ) - 1 ] = '\0';
        }

        // Copy endpoint
        if ( endpoint != nullptr )
        {
            std::strncpy( entry.m_endpoint, endpoint,
                          sizeof( entry.m_endpoint ) - 1 );
            entry.m_endpoint[ sizeof( entry.m_endpoint ) - 1 ] = '\0';
        }

        // Copy source ECU identifier
        if ( sourceEcu != nullptr )
        {
            std::strncpy( entry.m_sourceEcu, sourceEcu,
                          sizeof( entry.m_sourceEcu ) - 1 );
            entry.m_sourceEcu[ sizeof( entry.m_sourceEcu ) - 1 ] = '\0';
        }

        // Insert into cache (existing security filter + TTL logic applies)
        InsertRemoteService( entry );

        m_iBridgeInsertions.fetch_add( 1, std::memory_order_relaxed );

        LAP_COM_LOG_INFO << "[SD-Proxy] Bridge insert: service_id=0x"
                         << serviceId << " instance_id=0x" << instanceId
                         << " binding=" << ( bindingType ? bindingType : "unknown" )
                         << " source=" << ( sourceEcu ? sourceEcu : "unknown" );
    }

    void CSDProxyService::OnRemoteServiceRemoved(
        UInt64 serviceId,
        UInt64 instanceId ) noexcept
    {
        if ( !m_bInitialized.load( std::memory_order_acquire ) )
        {
            return;
        }

        // instanceId == 0 means remove all instances of this service
        InvalidateService( serviceId );

        m_iBridgeRemovals.fetch_add( 1, std::memory_order_relaxed );

        LAP_COM_LOG_INFO << "[SD-Proxy] Bridge remove: service_id=0x"
                         << serviceId << " instance_id=0x" << instanceId;
    }

    // ==================== Active Discovery Server Query ====================

    void CSDProxyService::SetActiveQueryCallback(
        ActiveQueryFunc callback ) noexcept
    {
        m_activeQueryCallback = std::move( callback );

        LAP_COM_LOG_INFO << "[SD-Proxy] Active query callback "
                         << ( m_activeQueryCallback ? "set" : "cleared" );
    }

    Optional< ServiceSlot > CSDProxyService::ActiveQueryService(
        UInt64 serviceId ) noexcept
    {
        if ( !m_bInitialized.load( std::memory_order_acquire ) )
        {
            return Optional< ServiceSlot > {};
        }

        if ( !m_activeQueryCallback )
        {
            return Optional< ServiceSlot > {};
        }

        m_iActiveQueries.fetch_add( 1, std::memory_order_relaxed );

        LAP_COM_LOG_DEBUG << "[SD-Proxy] Active query: service_id=0x" << serviceId;

        // Invoke DDS binding FindService via callback
        std::vector< UInt64 > instances;
        try
        {
            instances = m_activeQueryCallback( serviceId );
        }
        catch ( const std::exception& e )
        {
            LAP_COM_LOG_WARN << "[SD-Proxy] Active query callback threw: "
                             << e.what();
            return Optional< ServiceSlot > {};
        }
        catch ( ... )
        {
            LAP_COM_LOG_WARN << "[SD-Proxy] Active query callback threw unknown exception";
            return Optional< ServiceSlot > {};
        }

        if ( instances.empty() )
        {
            return Optional< ServiceSlot > {};
        }

        m_iActiveQueryHits.fetch_add( 1, std::memory_order_relaxed );

        // Cache all discovered instances, return the first
        Optional< ServiceSlot > result {};
        for ( auto instanceId : instances )
        {
            RemoteServiceEntry entry;
            entry.m_serviceId    = serviceId;
            entry.m_instanceId   = instanceId;
            entry.m_majorVersion = 1;
            entry.m_minorVersion = 0;
            std::strncpy( entry.m_bindingType, "dds",
                          sizeof( entry.m_bindingType ) - 1 );
            entry.m_bindingType[ sizeof( entry.m_bindingType ) - 1 ] = '\0';

            // Build a minimal endpoint string
            char endpointBuf[128] = {};
            std::snprintf( endpointBuf, sizeof( endpointBuf ),
                           "dds://active_query/%llu/%llu",
                           static_cast< unsigned long long > ( serviceId ),
                           static_cast< unsigned long long > ( instanceId ) );
            std::strncpy( entry.m_endpoint, endpointBuf,
                          sizeof( entry.m_endpoint ) - 1 );
            entry.m_endpoint[ sizeof( entry.m_endpoint ) - 1 ] = '\0';

            std::strncpy( entry.m_sourceEcu, "active_query",
                          sizeof( entry.m_sourceEcu ) - 1 );
            entry.m_sourceEcu[ sizeof( entry.m_sourceEcu ) - 1 ] = '\0';

            // Cache it
            InsertRemoteService( entry );

            // Return the first match
            if ( !result.has_value() )
            {
                result = entry.ToServiceSlot();
            }
        }

        LAP_COM_LOG_INFO << "[SD-Proxy] Active query found "
                         << instances.size() << " instances for service_id=0x"
                         << serviceId;

        return result;
    }

    // ==================== Service Whitelist ====================

    void CSDProxyService::SetServiceWhitelist(
        const std::set< UInt64 >& whitelist ) noexcept
    {
        std::unique_lock< std::shared_mutex > lock( m_whitelistMutex );
        m_serviceWhitelist = whitelist;

        LAP_COM_LOG_INFO << "[SD-Proxy] Whitelist set: "
                         << m_serviceWhitelist.size() << " entries"
                         << ( m_serviceWhitelist.empty() ? " (allow-all mode)" : "" );
    }

    void CSDProxyService::AddToServiceWhitelist( UInt64 serviceId ) noexcept
    {
        std::unique_lock< std::shared_mutex > lock( m_whitelistMutex );
        m_serviceWhitelist.insert( serviceId );

        LAP_COM_LOG_DEBUG << "[SD-Proxy] Whitelist add: service_id=0x" << serviceId
                          << " (total=" << m_serviceWhitelist.size() << ")";
    }

    void CSDProxyService::RemoveFromServiceWhitelist( UInt64 serviceId ) noexcept
    {
        std::unique_lock< std::shared_mutex > lock( m_whitelistMutex );
        m_serviceWhitelist.erase( serviceId );

        LAP_COM_LOG_DEBUG << "[SD-Proxy] Whitelist remove: service_id=0x" << serviceId
                          << " (total=" << m_serviceWhitelist.size() << ")";
    }

    void CSDProxyService::ClearServiceWhitelist() noexcept
    {
        std::unique_lock< std::shared_mutex > lock( m_whitelistMutex );
        m_serviceWhitelist.clear();

        LAP_COM_LOG_INFO << "[SD-Proxy] Whitelist cleared (allow-all mode)";
    }

    Bool CSDProxyService::IsServiceWhitelisted( UInt64 serviceId ) const noexcept
    {
        std::shared_lock< std::shared_mutex > lock( m_whitelistMutex );

        // Empty whitelist = allow-all mode
        if ( m_serviceWhitelist.empty() )
        {
            return true;
        }

        return m_serviceWhitelist.find( serviceId ) != m_serviceWhitelist.end();
    }

    UInt32 CSDProxyService::GetWhitelistSize() const noexcept
    {
        std::shared_lock< std::shared_mutex > lock( m_whitelistMutex );
        return static_cast< UInt32 > ( m_serviceWhitelist.size() );
    }

} // namespace registry
} // namespace com
} // namespace lap
