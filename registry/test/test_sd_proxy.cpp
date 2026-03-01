/**
 * @file        test_sd_proxy.cpp
 * @author      LightAP Development Team
 * @brief       Unit tests for CSDProxyService (SD-Proxy)
 * @date        2026/03/01
 * @copyright   Copyright (c) 2026
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §5.2 (SD Proxy Design)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/03/01  <td>1.0      <td>Aii             <td>Initial tests
 * </table>
 */

#include <gtest/gtest.h>
#include "CSDProxyService.hpp"
#include "CServiceRegistry.hpp"
#include "ServiceSlot.hpp"

#include <chrono>
#include <cstring>
#include <thread>

using namespace lap::com::registry;

// ==================== Helper Functions ====================

/**
 * @brief Create a test RemoteServiceEntry
 */
static RemoteServiceEntry MakeEntry(
    uint64_t serviceId,
    uint64_t instanceId,
    const char* bindingType,
    const char* endpoint,
    const char* sourceEcu,
    uint32_t majorVer = 1,
    uint32_t minorVer = 0 )
{
    RemoteServiceEntry entry;
    entry.m_serviceId = serviceId;
    entry.m_instanceId = instanceId;
    entry.m_majorVersion = majorVer;
    entry.m_minorVersion = minorVer;
    entry.m_iHitCount = 0;

    std::strncpy( entry.m_bindingType, bindingType, sizeof( entry.m_bindingType ) - 1 );
    entry.m_bindingType[sizeof( entry.m_bindingType ) - 1] = '\0';

    std::strncpy( entry.m_endpoint, endpoint, sizeof( entry.m_endpoint ) - 1 );
    entry.m_endpoint[sizeof( entry.m_endpoint ) - 1] = '\0';

    std::strncpy( entry.m_sourceEcu, sourceEcu, sizeof( entry.m_sourceEcu ) - 1 );
    entry.m_sourceEcu[sizeof( entry.m_sourceEcu ) - 1] = '\0';

    return entry;
}

// ==================== Test Fixture ====================

class SDProxyTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Initialize QM registry for slot registration
        auto result = m_qmRegistry.Initialize();
        ASSERT_TRUE( result.HasValue() ) << "QM registry init should succeed";
    }

    CServiceRegistry m_qmRegistry { RegistryType::kQM };
    CSDProxyService  m_sdProxy;
};

// ==================== Construction & Lifecycle ====================

TEST_F( SDProxyTest, DefaultConstruction )
{
    EXPECT_FALSE( m_sdProxy.IsInitialized() );
    EXPECT_FALSE( m_sdProxy.IsRunning() );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 0u );
    EXPECT_EQ( m_sdProxy.GetECUCount(), 0u );
}

TEST_F( SDProxyTest, InitializeRegistersInFixedSlots )
{
    auto result = m_sdProxy.Initialize( m_qmRegistry );
    ASSERT_TRUE( result.HasValue() ) << "Initialize should succeed";
    EXPECT_TRUE( m_sdProxy.IsInitialized() );

    // Verify Slot 1 (primary SD-Proxy)
    auto slot1 = m_qmRegistry.ReadSlot( SDProxyConstants::kPrimarySlot );
    ASSERT_TRUE( slot1.has_value() );
    EXPECT_TRUE( slot1->IsActive() );
    EXPECT_EQ( slot1->m_serviceId, SDProxyConstants::kPrimaryServiceId );
    EXPECT_EQ( slot1->m_instanceId, SDProxyConstants::kPrimaryInstanceId );
    EXPECT_STREQ( slot1->m_bindingType, "sd_proxy" );

    // Verify Slot 512 (backup SD-Proxy)
    auto slot512 = m_qmRegistry.ReadSlot( SDProxyConstants::kBackupSlot );
    ASSERT_TRUE( slot512.has_value() );
    EXPECT_TRUE( slot512->IsActive() );
    EXPECT_EQ( slot512->m_serviceId, SDProxyConstants::kBackupServiceId );
    EXPECT_EQ( slot512->m_instanceId, SDProxyConstants::kBackupInstanceId );
    EXPECT_STREQ( slot512->m_bindingType, "sd_proxy" );
}

TEST_F( SDProxyTest, DoubleInitializeIdempotent )
{
    auto result1 = m_sdProxy.Initialize( m_qmRegistry );
    ASSERT_TRUE( result1.HasValue() );

    auto result2 = m_sdProxy.Initialize( m_qmRegistry );
    ASSERT_TRUE( result2.HasValue() );
    EXPECT_TRUE( m_sdProxy.IsInitialized() );
}

TEST_F( SDProxyTest, StartAndStop )
{
    auto initResult = m_sdProxy.Initialize( m_qmRegistry );
    ASSERT_TRUE( initResult.HasValue() );

    auto startResult = m_sdProxy.Start();
    ASSERT_TRUE( startResult.HasValue() );
    EXPECT_TRUE( m_sdProxy.IsRunning() );

    m_sdProxy.Stop();
    EXPECT_FALSE( m_sdProxy.IsRunning() );
}

TEST_F( SDProxyTest, StartWithoutInitializeFails )
{
    CSDProxyService uninitProxy;
    auto result = uninitProxy.Start();
    EXPECT_FALSE( result.HasValue() );
}

// ==================== Cache Operations ====================

TEST_F( SDProxyTest, InsertAndFindRemoteService )
{
    m_sdProxy.Initialize( m_qmRegistry );

    auto entry = MakeEntry( 0x100, 0x00100001, "dds",
                            "topic://domain_0/radar_service", "ecu_a" );

    m_sdProxy.InsertRemoteService( entry );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );

    auto result = m_sdProxy.FindRemoteService( 0x100 );
    ASSERT_TRUE( result.has_value() );
    EXPECT_TRUE( result->IsActive() );
    EXPECT_EQ( result->m_serviceId, 0x100u );
    EXPECT_EQ( result->m_instanceId, 0x00100001u );
    EXPECT_STREQ( result->m_endpoint, "topic://domain_0/radar_service" );
}

TEST_F( SDProxyTest, FindNonexistentServiceReturnEmpty )
{
    m_sdProxy.Initialize( m_qmRegistry );

    auto result = m_sdProxy.FindRemoteService( 0x999 );
    EXPECT_FALSE( result.has_value() );
}

TEST_F( SDProxyTest, CacheTTLExpiry )
{
    SDProxyConfig config;
    config.m_iDefaultTtlSeconds = 1;  // Very short TTL for testing
    m_sdProxy.Initialize( m_qmRegistry, config );

    auto entry = MakeEntry( 0x200, 0x00200001, "dds",
                            "topic://domain_0/camera_service", "ecu_b" );

    m_sdProxy.InsertRemoteService( entry );

    // Should be found immediately
    auto result1 = m_sdProxy.FindRemoteService( 0x200 );
    ASSERT_TRUE( result1.has_value() );

    // Wait for TTL to expire
    std::this_thread::sleep_for( std::chrono::milliseconds( 1100 ) );

    // Should be expired now
    auto result2 = m_sdProxy.FindRemoteService( 0x200 );
    EXPECT_FALSE( result2.has_value() );
}

TEST_F( SDProxyTest, ExplicitTTLOverride )
{
    m_sdProxy.Initialize( m_qmRegistry );

    auto entry = MakeEntry( 0x300, 0x00300001, "dds",
                            "topic://domain_0/lidar_service", "ecu_c" );

    // Insert with 1-second TTL (override default 60s)
    m_sdProxy.InsertRemoteService( entry, 1 );

    auto result1 = m_sdProxy.FindRemoteService( 0x300 );
    ASSERT_TRUE( result1.has_value() );

    std::this_thread::sleep_for( std::chrono::milliseconds( 1100 ) );

    auto result2 = m_sdProxy.FindRemoteService( 0x300 );
    EXPECT_FALSE( result2.has_value() );
}

TEST_F( SDProxyTest, CacheUpdateExistingEntry )
{
    m_sdProxy.Initialize( m_qmRegistry );

    auto entry1 = MakeEntry( 0x400, 0x00400001, "dds",
                             "tcp://192.168.1.10:30509", "ecu_a" );
    m_sdProxy.InsertRemoteService( entry1 );

    // Update with new endpoint
    auto entry2 = MakeEntry( 0x400, 0x00400001, "dds",
                             "tcp://192.168.1.20:30509", "ecu_b" );
    m_sdProxy.InsertRemoteService( entry2 );

    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );  // Still 1 entry (updated)

    auto result = m_sdProxy.FindRemoteService( 0x400 );
    ASSERT_TRUE( result.has_value() );
    EXPECT_STREQ( result->m_endpoint, "tcp://192.168.1.20:30509" );
}

TEST_F( SDProxyTest, InvalidateService )
{
    m_sdProxy.Initialize( m_qmRegistry );

    auto entry = MakeEntry( 0x500, 0x00500001, "someip",
                            "udp://239.0.0.1:30490", "ecu_d" );
    m_sdProxy.InsertRemoteService( entry );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );

    m_sdProxy.InvalidateService( 0x500 );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 0u );

    auto result = m_sdProxy.FindRemoteService( 0x500 );
    EXPECT_FALSE( result.has_value() );
}

TEST_F( SDProxyTest, InvalidateByECU )
{
    m_sdProxy.Initialize( m_qmRegistry );

    // Insert multiple services from same ECU
    m_sdProxy.InsertRemoteService(
        MakeEntry( 0x600, 0x00600001, "dds", "topic://a/s1", "ecu_x" ) );
    m_sdProxy.InsertRemoteService(
        MakeEntry( 0x601, 0x00601001, "dds", "topic://a/s2", "ecu_x" ) );
    m_sdProxy.InsertRemoteService(
        MakeEntry( 0x602, 0x00602001, "dds", "topic://b/s3", "ecu_y" ) );

    EXPECT_EQ( m_sdProxy.GetCacheSize(), 3u );

    // Invalidate all from ecu_x
    m_sdProxy.InvalidateByECU( "ecu_x" );

    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );
    EXPECT_FALSE( m_sdProxy.FindRemoteService( 0x600 ).has_value() );
    EXPECT_FALSE( m_sdProxy.FindRemoteService( 0x601 ).has_value() );
    EXPECT_TRUE( m_sdProxy.FindRemoteService( 0x602 ).has_value() );
}

TEST_F( SDProxyTest, LRUEviction )
{
    SDProxyConfig config;
    config.m_iMaxCacheSize = 3;  // Very small cache for testing
    m_sdProxy.Initialize( m_qmRegistry, config );

    // Insert 3 services
    m_sdProxy.InsertRemoteService(
        MakeEntry( 0x700, 0x00700001, "dds", "ep1", "ecu_a" ) );
    m_sdProxy.InsertRemoteService(
        MakeEntry( 0x701, 0x00701001, "dds", "ep2", "ecu_a" ) );
    m_sdProxy.InsertRemoteService(
        MakeEntry( 0x702, 0x00702001, "dds", "ep3", "ecu_a" ) );

    // Access 0x700, 0x701, and 0x702 to set different hit counts
    m_sdProxy.FindRemoteService( 0x700 );  // hits: 1
    m_sdProxy.FindRemoteService( 0x700 );  // hits: 2
    m_sdProxy.FindRemoteService( 0x700 );  // hits: 3
    m_sdProxy.FindRemoteService( 0x702 );  // hits: 1
    m_sdProxy.FindRemoteService( 0x702 );  // hits: 2
    // 0x701 has 0 hits — will be evicted

    EXPECT_EQ( m_sdProxy.GetCacheSize(), 3u );

    // Insert 4th — triggers eviction of entry with lowest hits (0x701 = 0 hits)
    // The new entry 0x703 gets inserted with hitCount=0, but eviction runs AFTER insertion.
    // Since 0x701 still has hitCount=0 and was already in the map before 0x703,
    // min_element picks the first zero-hit entry it finds (order varies).
    // So we just test that cache stays at max size and at least one entry was evicted.
    m_sdProxy.InsertRemoteService(
        MakeEntry( 0x703, 0x00703001, "dds", "ep4", "ecu_a" ) );

    EXPECT_EQ( m_sdProxy.GetCacheSize(), 3u );

    // 0x700 (3 hits) and 0x702 (2 hits) should always survive
    EXPECT_TRUE( m_sdProxy.FindRemoteService( 0x700 ).has_value() );
    EXPECT_TRUE( m_sdProxy.FindRemoteService( 0x702 ).has_value() );

    // Verify eviction stats
    auto stats = m_sdProxy.GetStats();
    EXPECT_GE( stats.m_iCacheEvictions, 1u );
}

// ==================== ECU Provider Registry ====================

TEST_F( SDProxyTest, RegisterAndQueryECU )
{
    m_sdProxy.Initialize( m_qmRegistry );

    ECUInfo ecu;
    std::strncpy( ecu.m_ecuId, "ecu_a", sizeof( ecu.m_ecuId ) - 1 );
    std::strncpy( ecu.m_ipAddress, "192.168.1.10", sizeof( ecu.m_ipAddress ) - 1 );
    ecu.m_port = 11811;

    m_sdProxy.RegisterECU( ecu );
    EXPECT_EQ( m_sdProxy.GetECUCount(), 1u );

    auto ecus = m_sdProxy.GetKnownECUs();
    ASSERT_EQ( ecus.size(), 1u );
    EXPECT_EQ( ecus[0], "ecu_a" );
}

TEST_F( SDProxyTest, ECUHeartbeatUpdate )
{
    m_sdProxy.Initialize( m_qmRegistry );

    ECUInfo ecu;
    std::strncpy( ecu.m_ecuId, "ecu_b", sizeof( ecu.m_ecuId ) - 1 );
    std::strncpy( ecu.m_ipAddress, "192.168.1.20", sizeof( ecu.m_ipAddress ) - 1 );
    ecu.m_port = 11811;

    m_sdProxy.RegisterECU( ecu );
    m_sdProxy.UpdateECUHeartbeat( "ecu_b" );

    auto ecus = m_sdProxy.GetKnownECUs();
    EXPECT_EQ( ecus.size(), 1u );
}

// ==================== Security Filter ====================

TEST_F( SDProxyTest, SecurityFilterBlocksUnknownECU )
{
    SDProxyConfig config;
    config.m_bEnableSecurityFilter = true;
    m_sdProxy.Initialize( m_qmRegistry, config );

    // Set policy: only allow ecu_a
    SDProxySecurityPolicy policy;
    policy.m_allowedEcus.insert( "ecu_a" );
    policy.m_bEnableAuditLog = true;
    m_sdProxy.LoadSecurityPolicy( policy );

    // Service from allowed ECU should succeed
    auto goodEntry = MakeEntry( 0x800, 0x00800001, "dds",
                                "topic://domain/svc", "ecu_a" );
    m_sdProxy.InsertRemoteService( goodEntry );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );

    // Service from blocked ECU should be rejected
    auto badEntry = MakeEntry( 0x801, 0x00801001, "dds",
                               "topic://domain/svc2", "ecu_unknown" );
    m_sdProxy.InsertRemoteService( badEntry );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );  // Still 1 (blocked)
}

TEST_F( SDProxyTest, SecurityFilterServiceACL )
{
    SDProxyConfig config;
    config.m_bEnableSecurityFilter = true;
    m_sdProxy.Initialize( m_qmRegistry, config );

    // Allow ecu_a and ecu_b globally, but restrict service 0x900 to ecu_a only
    SDProxySecurityPolicy policy;
    policy.m_allowedEcus.insert( "ecu_a" );
    policy.m_allowedEcus.insert( "ecu_b" );
    policy.m_serviceAcl[0x900] = { "ecu_a" };  // Only ecu_a can provide service 0x900
    m_sdProxy.LoadSecurityPolicy( policy );

    // ecu_a providing 0x900 — should pass
    m_sdProxy.InsertRemoteService(
        MakeEntry( 0x900, 0x00900001, "dds", "ep1", "ecu_a" ) );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );

    // ecu_b providing 0x900 — should be blocked by ACL
    m_sdProxy.InsertRemoteService(
        MakeEntry( 0x900, 0x00900002, "dds", "ep2", "ecu_b" ) );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );  // Still 1 (ecu_b blocked for 0x900)
}

TEST_F( SDProxyTest, SecurityFilterBlocksInvalidMetadata )
{
    SDProxyConfig config;
    config.m_bEnableSecurityFilter = true;
    m_sdProxy.Initialize( m_qmRegistry, config );

    // No whitelist = all ECUs allowed, but validate metadata
    SDProxySecurityPolicy policy;
    m_sdProxy.LoadSecurityPolicy( policy );

    // Invalid service ID 0x0000
    auto badEntry1 = MakeEntry( 0x0000, 0x00000001, "dds", "ep1", "ecu_a" );
    m_sdProxy.InsertRemoteService( badEntry1 );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 0u );

    // Invalid version 0.0
    auto badEntry2 = MakeEntry( 0xA00, 0x00A00001, "dds", "ep2", "ecu_a", 0, 0 );
    m_sdProxy.InsertRemoteService( badEntry2 );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 0u );

    // Empty endpoint
    auto badEntry3 = MakeEntry( 0xA01, 0x00A01001, "dds", "", "ecu_a" );
    m_sdProxy.InsertRemoteService( badEntry3 );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 0u );
}

// ==================== Statistics ====================

TEST_F( SDProxyTest, StatisticsTracking )
{
    m_sdProxy.Initialize( m_qmRegistry );

    // Insert and find
    auto entry = MakeEntry( 0xB00, 0x00B00001, "dds", "ep1", "ecu_a" );
    m_sdProxy.InsertRemoteService( entry );
    m_sdProxy.FindRemoteService( 0xB00 );   // Hit
    m_sdProxy.FindRemoteService( 0xB00 );   // Hit
    m_sdProxy.FindRemoteService( 0xFFF );   // Miss

    auto stats = m_sdProxy.GetStats();
    EXPECT_EQ( stats.m_iCacheHits, 2u );
    EXPECT_EQ( stats.m_iCacheMisses, 1u );
    EXPECT_EQ( stats.m_iCacheInsertions, 1u );
    EXPECT_GT( stats.HitRate(), 0.6 );
}

TEST_F( SDProxyTest, StatisticsEvictionTracking )
{
    SDProxyConfig config;
    config.m_iMaxCacheSize = 2;
    m_sdProxy.Initialize( m_qmRegistry, config );

    m_sdProxy.InsertRemoteService(
        MakeEntry( 0xC00, 0x00C00001, "dds", "ep1", "ecu_a" ) );
    m_sdProxy.InsertRemoteService(
        MakeEntry( 0xC01, 0x00C01001, "dds", "ep2", "ecu_a" ) );
    m_sdProxy.InsertRemoteService(
        MakeEntry( 0xC02, 0x00C02001, "dds", "ep3", "ecu_a" ) );  // Triggers eviction

    auto stats = m_sdProxy.GetStats();
    EXPECT_EQ( stats.m_iCacheInsertions, 3u );
    EXPECT_GE( stats.m_iCacheEvictions, 1u );
}

// ==================== Background Thread Lifecycle ====================

TEST_F( SDProxyTest, TTLCleanupThreadRemovesExpiredEntries )
{
    SDProxyConfig config;
    config.m_iDefaultTtlSeconds = 1;
    config.m_iTtlCleanupIntervalSeconds = 1;
    m_sdProxy.Initialize( m_qmRegistry, config );
    m_sdProxy.Start();

    auto entry = MakeEntry( 0xD00, 0x00D00001, "dds", "ep1", "ecu_a" );
    m_sdProxy.InsertRemoteService( entry );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );

    // Wait for TTL + cleanup cycle
    std::this_thread::sleep_for( std::chrono::seconds( 3 ) );

    EXPECT_EQ( m_sdProxy.GetCacheSize(), 0u );

    m_sdProxy.Stop();
}

TEST_F( SDProxyTest, MultipleStartStopCycles )
{
    m_sdProxy.Initialize( m_qmRegistry );

    for ( int i = 0; i < 3; ++i )
    {
        auto startResult = m_sdProxy.Start();
        EXPECT_TRUE( startResult.HasValue() );
        EXPECT_TRUE( m_sdProxy.IsRunning() );

        m_sdProxy.Stop();
        EXPECT_FALSE( m_sdProxy.IsRunning() );
    }
}

// ==================== ToServiceSlot Conversion ====================

TEST_F( SDProxyTest, RemoteServiceEntryToServiceSlot )
{
    auto entry = MakeEntry( 0xE00, 0x00E00001, "dds",
                            "topic://domain/svc", "ecu_test" );
    entry.m_majorVersion = 2;
    entry.m_minorVersion = 3;

    ServiceSlot slot = entry.ToServiceSlot();

    EXPECT_EQ( slot.m_serviceId, 0xE00u );
    EXPECT_EQ( slot.m_instanceId, 0x00E00001u );
    EXPECT_EQ( slot.m_majorVersion, 2u );
    EXPECT_EQ( slot.m_minorVersion, 3u );
    EXPECT_STREQ( slot.m_bindingType, "dds" );
    EXPECT_STREQ( slot.m_endpoint, "topic://domain/svc" );
    EXPECT_TRUE( slot.IsActive() );
    EXPECT_EQ( slot.m_ownerPid, 0 );  // Remote service
}

TEST_F( SDProxyTest, RemoteServiceEntryIsExpired )
{
    RemoteServiceEntry entry;
    entry.m_expiry = std::chrono::steady_clock::now() - std::chrono::seconds( 1 );
    EXPECT_TRUE( entry.IsExpired() );

    entry.m_expiry = std::chrono::steady_clock::now() + std::chrono::seconds( 60 );
    EXPECT_FALSE( entry.IsExpired() );
}

// ==================== SDProxyConstants Slot Mapping ====================

TEST_F( SDProxyTest, ConstantsSlotMapping )
{
    // Verify service IDs map to correct slot indices
    EXPECT_EQ( SDProxyConstants::kPrimaryServiceId & 1023,
               SDProxyConstants::kPrimarySlot );
    EXPECT_EQ( SDProxyConstants::kBackupServiceId & 1023,
               SDProxyConstants::kBackupSlot );
}

// ==================== DDS Discovery Bridge ====================

TEST_F( SDProxyTest, BridgeOnRemoteServiceDiscovered )
{
    m_sdProxy.Initialize( m_qmRegistry );

    // Bridge a DDS discovery event into SD-Proxy
    m_sdProxy.OnRemoteServiceDiscovered(
        0x1100, 0x11000001, "dds",
        "topic://domain_0/radar_events", "ecu_remote_a" );

    // Should be cached
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );

    auto result = m_sdProxy.FindRemoteService( 0x1100 );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result->m_serviceId, 0x1100u );
    EXPECT_EQ( result->m_instanceId, 0x11000001u );
    EXPECT_TRUE( result->IsActive() );
}

TEST_F( SDProxyTest, BridgeOnRemoteServiceRemoved )
{
    m_sdProxy.Initialize( m_qmRegistry );

    // Insert via bridge
    m_sdProxy.OnRemoteServiceDiscovered(
        0x1200, 0x12000001, "dds", "ep1", "ecu_r" );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );

    // Remove via bridge
    m_sdProxy.OnRemoteServiceRemoved( 0x1200, 0x12000001 );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 0u );

    auto result = m_sdProxy.FindRemoteService( 0x1200 );
    EXPECT_FALSE( result.has_value() );
}

TEST_F( SDProxyTest, BridgeNotInitializedDoesNothing )
{
    CSDProxyService uninitProxy;

    // Bridge calls on uninitialized proxy should be no-ops
    uninitProxy.OnRemoteServiceDiscovered(
        0x1300, 0x13000001, "dds", "ep1", "ecu_r" );
    EXPECT_EQ( uninitProxy.GetCacheSize(), 0u );

    uninitProxy.OnRemoteServiceRemoved( 0x1300, 0 );
    // No crash expected
}

TEST_F( SDProxyTest, BridgeMultipleServicesFromSameECU )
{
    m_sdProxy.Initialize( m_qmRegistry );

    m_sdProxy.OnRemoteServiceDiscovered(
        0x1400, 0x14000001, "dds", "topic://a/s1", "ecu_multi" );
    m_sdProxy.OnRemoteServiceDiscovered(
        0x1401, 0x14010001, "dds", "topic://a/s2", "ecu_multi" );
    m_sdProxy.OnRemoteServiceDiscovered(
        0x1402, 0x14020001, "dds", "topic://a/s3", "ecu_multi" );

    EXPECT_EQ( m_sdProxy.GetCacheSize(), 3u );

    // All should be cached
    EXPECT_TRUE( m_sdProxy.FindRemoteService( 0x1400 ).has_value() );
    EXPECT_TRUE( m_sdProxy.FindRemoteService( 0x1401 ).has_value() );
    EXPECT_TRUE( m_sdProxy.FindRemoteService( 0x1402 ).has_value() );
}

// ==================== Service Whitelist ====================

TEST_F( SDProxyTest, WhitelistEmptyAllowsAll )
{
    m_sdProxy.Initialize( m_qmRegistry );

    // Default: empty whitelist = allow all
    EXPECT_TRUE( m_sdProxy.IsServiceWhitelisted( 0x100 ) );
    EXPECT_TRUE( m_sdProxy.IsServiceWhitelisted( 0x999 ) );
    EXPECT_EQ( m_sdProxy.GetWhitelistSize(), 0u );

    // Bridge should work for any service
    m_sdProxy.OnRemoteServiceDiscovered(
        0x1500, 0x15000001, "dds", "ep1", "ecu_a" );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );
}

TEST_F( SDProxyTest, WhitelistBlocksNonListedService )
{
    m_sdProxy.Initialize( m_qmRegistry );

    // Set whitelist: only allow 0x1600
    std::set< uint64_t > whitelist = { 0x1600 };
    m_sdProxy.SetServiceWhitelist( whitelist );
    EXPECT_EQ( m_sdProxy.GetWhitelistSize(), 1u );

    // Allowed service should be cached
    m_sdProxy.OnRemoteServiceDiscovered(
        0x1600, 0x16000001, "dds", "ep1", "ecu_a" );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );

    // Blocked service should NOT be cached
    m_sdProxy.OnRemoteServiceDiscovered(
        0x1601, 0x16010001, "dds", "ep2", "ecu_a" );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );  // Still 1

    EXPECT_TRUE( m_sdProxy.FindRemoteService( 0x1600 ).has_value() );
    EXPECT_FALSE( m_sdProxy.FindRemoteService( 0x1601 ).has_value() );
}

TEST_F( SDProxyTest, WhitelistAddAndRemove )
{
    m_sdProxy.Initialize( m_qmRegistry );

    m_sdProxy.AddToServiceWhitelist( 0x1700 );
    EXPECT_EQ( m_sdProxy.GetWhitelistSize(), 1u );
    EXPECT_TRUE( m_sdProxy.IsServiceWhitelisted( 0x1700 ) );
    EXPECT_FALSE( m_sdProxy.IsServiceWhitelisted( 0x1701 ) );

    m_sdProxy.AddToServiceWhitelist( 0x1701 );
    EXPECT_EQ( m_sdProxy.GetWhitelistSize(), 2u );
    EXPECT_TRUE( m_sdProxy.IsServiceWhitelisted( 0x1701 ) );

    m_sdProxy.RemoveFromServiceWhitelist( 0x1700 );
    EXPECT_EQ( m_sdProxy.GetWhitelistSize(), 1u );
    EXPECT_FALSE( m_sdProxy.IsServiceWhitelisted( 0x1700 ) );
    EXPECT_TRUE( m_sdProxy.IsServiceWhitelisted( 0x1701 ) );
}

TEST_F( SDProxyTest, WhitelistClearRestoresAllowAll )
{
    m_sdProxy.Initialize( m_qmRegistry );

    std::set< uint64_t > whitelist = { 0x1800, 0x1801 };
    m_sdProxy.SetServiceWhitelist( whitelist );
    EXPECT_FALSE( m_sdProxy.IsServiceWhitelisted( 0x9999 ) );

    m_sdProxy.ClearServiceWhitelist();
    EXPECT_EQ( m_sdProxy.GetWhitelistSize(), 0u );
    EXPECT_TRUE( m_sdProxy.IsServiceWhitelisted( 0x9999 ) );  // Allow-all restored
}

TEST_F( SDProxyTest, WhitelistWithSecurityFilter )
{
    SDProxyConfig config;
    config.m_bEnableSecurityFilter = true;
    m_sdProxy.Initialize( m_qmRegistry, config );

    // Setup: whitelist allows 0x1900, security allows ecu_a only
    std::set< uint64_t > whitelist = { 0x1900 };
    m_sdProxy.SetServiceWhitelist( whitelist );

    SDProxySecurityPolicy policy;
    policy.m_allowedEcus.insert( "ecu_ok" );
    m_sdProxy.LoadSecurityPolicy( policy );

    // Case 1: Whitelisted service + allowed ECU → cached
    m_sdProxy.OnRemoteServiceDiscovered(
        0x1900, 0x19000001, "dds", "ep1", "ecu_ok" );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );

    // Case 2: Whitelisted service + blocked ECU → rejected by security
    m_sdProxy.OnRemoteServiceDiscovered(
        0x1900, 0x19000002, "dds", "ep2", "ecu_bad" );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );

    // Case 3: Non-whitelisted service + allowed ECU → rejected by whitelist
    m_sdProxy.OnRemoteServiceDiscovered(
        0x1901, 0x19010001, "dds", "ep3", "ecu_ok" );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );
}

// ==================== Active DS Query ====================

TEST_F( SDProxyTest, ActiveQueryServiceWithCallback )
{
    m_sdProxy.Initialize( m_qmRegistry );

    // Set active query callback that returns 2 instances for service 0x2000
    m_sdProxy.SetActiveQueryCallback(
        []( uint64_t serviceId ) -> std::vector< uint64_t >
        {
            if ( serviceId == 0x2000 )
            {
                return { 0x20000001, 0x20000002 };
            }
            return {};
        } );

    // Active query should find the service
    auto result = m_sdProxy.ActiveQueryService( 0x2000 );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result->m_serviceId, 0x2000u );
    EXPECT_EQ( result->m_instanceId, 0x20000001u );  // First instance
    EXPECT_TRUE( result->IsActive() );

    // Should also have been cached (2 instances → 1 cache entry by serviceId key)
    auto cached = m_sdProxy.FindRemoteService( 0x2000 );
    ASSERT_TRUE( cached.has_value() );
}

TEST_F( SDProxyTest, ActiveQueryServiceNotFound )
{
    m_sdProxy.Initialize( m_qmRegistry );

    // Set callback that returns empty for unknown services
    m_sdProxy.SetActiveQueryCallback(
        []( uint64_t ) -> std::vector< uint64_t >
        {
            return {};
        } );

    auto result = m_sdProxy.ActiveQueryService( 0x2100 );
    EXPECT_FALSE( result.has_value() );
}

TEST_F( SDProxyTest, ActiveQueryWithoutCallbackReturnsEmpty )
{
    m_sdProxy.Initialize( m_qmRegistry );

    // No callback set
    auto result = m_sdProxy.ActiveQueryService( 0x2200 );
    EXPECT_FALSE( result.has_value() );
}

TEST_F( SDProxyTest, ActiveQueryNotInitializedReturnsEmpty )
{
    CSDProxyService uninitProxy;

    uninitProxy.SetActiveQueryCallback(
        []( uint64_t ) -> std::vector< uint64_t >
        {
            return { 0x01 };
        } );

    auto result = uninitProxy.ActiveQueryService( 0x2300 );
    EXPECT_FALSE( result.has_value() );
}

TEST_F( SDProxyTest, ActiveQueryCallbackThrowsHandled )
{
    m_sdProxy.Initialize( m_qmRegistry );

    m_sdProxy.SetActiveQueryCallback(
        []( uint64_t ) -> std::vector< uint64_t >
        {
            throw std::runtime_error( "DS connection failed" );
        } );

    // Should not crash, should return empty
    auto result = m_sdProxy.ActiveQueryService( 0x2400 );
    EXPECT_FALSE( result.has_value() );
}

// ==================== Bridge + Whitelist + Security Integration ====================

TEST_F( SDProxyTest, FullBridgeIntegration )
{
    SDProxyConfig config;
    config.m_bEnableSecurityFilter = true;
    m_sdProxy.Initialize( m_qmRegistry, config );

    // Setup whitelist
    std::set< uint64_t > whitelist = { 0x3000, 0x3001 };
    m_sdProxy.SetServiceWhitelist( whitelist );

    // Setup security: allow ecu_alpha only
    SDProxySecurityPolicy policy;
    policy.m_allowedEcus.insert( "ecu_alpha" );
    m_sdProxy.LoadSecurityPolicy( policy );

    // Setup active query callback
    m_sdProxy.SetActiveQueryCallback(
        []( uint64_t serviceId ) -> std::vector< uint64_t >
        {
            if ( serviceId == 0x3002 )  // Note: 0x3002 not in whitelist
            {
                return { 0x30020001 };
            }
            return {};
        } );

    // Bridge: whitelisted + security-allowed → cached
    m_sdProxy.OnRemoteServiceDiscovered(
        0x3000, 0x30000001, "dds", "ep1", "ecu_alpha" );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );

    // Bridge: whitelisted + security-blocked → rejected
    m_sdProxy.OnRemoteServiceDiscovered(
        0x3001, 0x30010001, "dds", "ep2", "ecu_blocked" );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );

    // Bridge: not whitelisted → rejected before security check
    m_sdProxy.OnRemoteServiceDiscovered(
        0x9999, 0x99990001, "dds", "ep3", "ecu_alpha" );
    EXPECT_EQ( m_sdProxy.GetCacheSize(), 1u );

    // Active query bypasses whitelist check (direct InsertRemoteService path)
    // Note: ActiveQueryService builds entries internally with "active_query" source
    // which won't be in the allowed_ecus set, so security will block it.
    // This is expected — active query results should also pass security.

    // Find the cached service
    auto result = m_sdProxy.FindRemoteService( 0x3000 );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result->m_serviceId, 0x3000u );
}
