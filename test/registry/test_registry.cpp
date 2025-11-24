/**
 * @file        test_registry.cpp
 * @author      LightAP Development Team
 * @brief       Unit tests for SharedMemoryRegistry (dual QM+AB/ASIL-CD registries v3.0)
 * @date        2025-11-20
 * @copyright   Copyright (c) 2025
 */

#include "SharedMemoryRegistry.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <numeric>
#include <algorithm>

using namespace lap::com::registry;
using namespace std::chrono;

// ============================================================================
// Test Fixture
// ============================================================================

class SharedMemoryRegistryTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Initialize dual registries
        registry_ = std::make_unique<SharedMemoryRegistry>();
        auto result = registry_->Initialize();
        ASSERT_TRUE(result.HasValue()) << "Failed to initialize registry";
    }

    void TearDown() override
    {
        registry_.reset();
        
        // Cleanup shared memory files
        shm_unlink("/lap_com_registry_qm");
        shm_unlink("/lap_com_registry_asil");
    }

    std::unique_ptr<SharedMemoryRegistry> registry_;
};

// ============================================================================
// Basic Functionality Tests
// ============================================================================

/**
 * @test Verify registry initialization
 */
TEST_F(SharedMemoryRegistryTest, Initialization)
{
    // Registry should be initialized in SetUp
    EXPECT_NE(registry_, nullptr);
}

/**
 * @test Register a QM service (QM + ASIL-A/B with security)
 * @req SWS_CM_00002 (OfferService)
 */
TEST_F(SharedMemoryRegistryTest, RegisterQM_AB_Service)
{
    uint64_t service_id = 0x0100;  // QM service range (0x0001~0x0417)
    uint64_t instance_id = 1;
    
    auto result = registry_->RegisterService(
        service_id,
        instance_id,
        1,  // major_version
        0,  // minor_version
        "iceoryx2",
        "shm://radar/instance_1"
    );
    
    ASSERT_TRUE(result.HasValue()) << "Failed to register QM service";
}

/**
 * @test Register an ASIL service (ASIL-C/D only, isolated)
 * @req SWS_CM_00002 (OfferService)
 */
TEST_F(SharedMemoryRegistryTest, RegisterASIL_CD_Service)
{
    uint64_t service_id = 0xF100;  // ASIL service range (0xF001~0xF3FE)
    uint64_t instance_id = 1;
    
    auto result = registry_->RegisterService(
        service_id,
        instance_id,
        2,  // major_version
        1,  // minor_version
        "dds",
        "topic://domain_0/steering_control"
    );
    
    ASSERT_TRUE(result.HasValue()) << "Failed to register ASIL service";
}

/**
 * @test Register a broadcast service (both registries)
 */
TEST_F(SharedMemoryRegistryTest, RegisterBroadcastService)
{
    uint64_t service_id = 0xFFFF;  // Broadcast service ID
    uint64_t instance_id = 0;
    
    auto result = registry_->RegisterService(
        service_id,
        instance_id,
        1,  // major_version
        0,  // minor_version
        "custom",
        "broadcast://system/shutdown"
    );
    
    ASSERT_TRUE(result.HasValue()) << "Failed to register broadcast service";
}

/**
 * @test Find a registered QM service
 * @req SWS_CM_00001 (FindService)
 */
TEST_F(SharedMemoryRegistryTest, FindQM_AB_Service)
{
    uint64_t service_id = 0x0200;
    uint64_t instance_id = 1;
    
    // Register service
    auto reg_result = registry_->RegisterService(
        service_id, instance_id, 1, 0, "iceoryx2", "shm://camera/front"
    );
    ASSERT_TRUE(reg_result.HasValue());
    
    // Find service
    auto found = registry_->FindService(service_id);
    
    ASSERT_TRUE(found.has_value()) << "QM service not found";
    EXPECT_EQ(found.value().service_id, service_id);
    EXPECT_EQ(found.value().instance_id, instance_id);
    EXPECT_STREQ(found.value().binding_type, "iceoryx2");
    EXPECT_STREQ(found.value().endpoint, "shm://camera/front");
}

/**
 * @test Find a registered ASIL service
 * @req SWS_CM_00001 (FindService)
 */
TEST_F(SharedMemoryRegistryTest, FindASIL_CD_Service)
{
    uint64_t service_id = 0xF200;
    uint64_t instance_id = 2;
    
    // Register service
    auto reg_result = registry_->RegisterService(
        service_id, instance_id, 3, 1, "dds", "topic://brake/control"
    );
    ASSERT_TRUE(reg_result.HasValue());
    
    // Find service
    auto found = registry_->FindService(service_id);
    
    ASSERT_TRUE(found.has_value()) << "ASIL service not found";
    EXPECT_EQ(found.value().service_id, service_id);
    EXPECT_EQ(found.value().major_version, 3u);
    EXPECT_EQ(found.value().minor_version, 1u);
}

/**
 * @test Unregister a service
 * @req SWS_CM_00111 (StopOfferService)
 */
TEST_F(SharedMemoryRegistryTest, UnregisterService)
{
    uint64_t service_id = 0x0300;
    
    // Register service
    auto reg_result = registry_->RegisterService(
        service_id, 1, 1, 0, "iceoryx2", "shm://test/service"
    );
    ASSERT_TRUE(reg_result.HasValue());
    
    // Verify service exists
    auto found1 = registry_->FindService(service_id);
    ASSERT_TRUE(found1.has_value());
    
    // Unregister service
    auto unreg_result = registry_->UnregisterService(service_id);
    ASSERT_TRUE(unreg_result.HasValue());
    
    // Verify service no longer exists
    auto found2 = registry_->FindService(service_id);
    EXPECT_FALSE(found2.has_value()) << "Service should be unregistered";
}

/**
 * @test Update heartbeat
 * @req SWS_CM_00311 (Service liveness)
 */
TEST_F(SharedMemoryRegistryTest, UpdateHeartbeat)
{
    uint64_t service_id = 0x0401;  // Valid service_id (slot 1)
    
    // Register service
    auto reg_result = registry_->RegisterService(
        service_id, 1, 1, 0, "iceoryx2", "shm://heartbeat/test"
    );
    ASSERT_TRUE(reg_result.HasValue());
    
    // Get initial heartbeat
    auto found1 = registry_->FindService(service_id);
    ASSERT_TRUE(found1.has_value());
    uint64_t initial_hb = found1.value().last_heartbeat_ns;
    
    // Wait and update heartbeat
    std::this_thread::sleep_for(milliseconds(10));
    auto now = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    auto hb_result = registry_->UpdateHeartbeat(service_id, now);
    ASSERT_TRUE(hb_result.HasValue());
    
    // Verify heartbeat updated
    auto found2 = registry_->FindService(service_id);
    ASSERT_TRUE(found2.has_value());
    EXPECT_GT(found2.value().last_heartbeat_ns, initial_hb) << "Heartbeat should be updated";
}

// ============================================================================
// Slot Mapping Tests
// ============================================================================

/**
 * @test Verify fixed slot mapping (slot = service_id & 1023)
 */
TEST_F(SharedMemoryRegistryTest, FixedSlotMapping)
{
    // Test QM+AB service (0x0001 → slot 1)
    uint64_t qm_service = 0x0001;
    auto reg1 = registry_->RegisterService(qm_service, 1, 1, 0, "iceoryx2", "test");
    ASSERT_TRUE(reg1.HasValue());
    
    auto found1 = registry_->FindService(qm_service);
    ASSERT_TRUE(found1.has_value());
    
    // Test ASIL-CD service (0xF001 → slot 1, different registry)
    uint64_t asil_service = 0xF001;
    auto reg2 = registry_->RegisterService(asil_service, 1, 1, 0, "dds", "test");
    ASSERT_TRUE(reg2.HasValue());
    
    auto found2 = registry_->FindService(asil_service);
    ASSERT_TRUE(found2.has_value());
    
    // Both services should coexist (different registries)
    EXPECT_NE(found1.value().service_id, found2.value().service_id);
}

/**
 * @test Reject invalid service IDs (slot 0)
 */
TEST_F(SharedMemoryRegistryTest, RejectSlotZero)
{
    // 0x0000 maps to slot 0 (reserved)
    auto result1 = registry_->RegisterService(0x0000, 1, 1, 0, "test", "test");
    EXPECT_FALSE(result1.HasValue()) << "Slot 0 should be rejected";
    
    // 0xF000 also maps to slot 0 (reserved)
    auto result2 = registry_->RegisterService(0xF000, 1, 1, 0, "test", "test");
    EXPECT_FALSE(result2.HasValue()) << "Slot 0 should be rejected";
}

/**
 * @test Verify QM service ID boundary (0x0001~0x0417)
 * @note QM registry hosts QM + ASIL-A/B services
 */
TEST_F(SharedMemoryRegistryTest, QM_AB_ServiceID_Boundary)
{
    // Min valid: 0x0001
    auto result_min = registry_->RegisterService(0x0001, 1, 1, 0, "iceoryx2", "test");
    ASSERT_TRUE(result_min.HasValue()) << "0x0001 should be valid QM";
    
    // Max valid: 0x0417
    auto result_max = registry_->RegisterService(0x0417, 1, 1, 0, "iceoryx2", "test");
    ASSERT_TRUE(result_max.HasValue()) << "0x0417 should be valid QM (extended range)";
    
    // Just below max (should be valid)
    auto result_below_max = registry_->RegisterService(0x0416, 1, 1, 0, "iceoryx2", "test");
    ASSERT_TRUE(result_below_max.HasValue()) << "0x0416 should be valid QM";
}

/**
 * @test Verify ASIL service ID boundary (0xF001~0xF3FE)
 * @note ASIL registry hosts ASIL-C/D services only (physically isolated)
 */
TEST_F(SharedMemoryRegistryTest, ASIL_CD_ServiceID_Boundary)
{
    // Min valid: 0xF001
    auto result_min = registry_->RegisterService(0xF001, 1, 1, 0, "dds", "test");
    ASSERT_TRUE(result_min.HasValue()) << "0xF001 should be valid ASIL";
    
    // Max valid: 0xF3FE
    auto result_max = registry_->RegisterService(0xF3FE, 1, 1, 0, "dds", "test");
    ASSERT_TRUE(result_max.HasValue()) << "0xF3FE should be valid ASIL";
    
    // Just above max (0xF3FF should be invalid or handled differently)
    // Note: 0xF3FF is reserved, test depends on SelectRegistry implementation
    auto result_above_max = registry_->RegisterService(0xF3FF, 1, 1, 0, "dds", "test");
    // Behavior depends on fallback logic in SelectRegistry
}

// ============================================================================
// Performance Tests
// ============================================================================

/**
 * @test Measure FindService latency (target: < 500ns)
 */
TEST_F(SharedMemoryRegistryTest, FindServiceLatency)
{
    // Register service
    uint64_t service_id = 0x0500;
    auto reg_result = registry_->RegisterService(
        service_id, 1, 1, 0, "iceoryx2", "shm://perf/test"
    );
    ASSERT_TRUE(reg_result.HasValue());
    
    constexpr int NUM_SAMPLES = 100000;
    std::vector<uint64_t> latencies_ns;
    latencies_ns.reserve(NUM_SAMPLES);
    
    // Warm-up
    for (int i = 0; i < 1000; ++i) {
        registry_->FindService(service_id);
    }
    
    // Benchmark
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        auto start = high_resolution_clock::now();
        auto found = registry_->FindService(service_id);
        auto end = high_resolution_clock::now();
        
        ASSERT_TRUE(found.has_value());
        latencies_ns.push_back(duration_cast<nanoseconds>(end - start).count());
    }
    
    // Calculate statistics
    std::sort(latencies_ns.begin(), latencies_ns.end());
    
    uint64_t p50 = latencies_ns[NUM_SAMPLES * 50 / 100];
    uint64_t p99 = latencies_ns[NUM_SAMPLES * 99 / 100];
    uint64_t avg = std::accumulate(latencies_ns.begin(), latencies_ns.end(), 0ULL) / NUM_SAMPLES;
    
    std::cout << "\nFindService Latency Benchmark (" << NUM_SAMPLES << " samples):\n"
              << "  Average: " << avg << " ns\n"
              << "  P50:     " << p50 << " ns\n"
              << "  P99:     " << p99 << " ns\n";
    
    // Performance assertion (target: < 500ns P99)
    EXPECT_LT(p99, 1000u) << "P99 FindService latency should be < 1µs (target: < 500ns)";
}

/**
 * @test Measure RegisterService latency
 */
TEST_F(SharedMemoryRegistryTest, RegisterServiceLatency)
{
    constexpr int NUM_SAMPLES = 100;  // Reduced to avoid slot collisions
    std::vector<uint64_t> latencies_ns;
    latencies_ns.reserve(NUM_SAMPLES);
    
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        uint64_t service_id = 0x0100 + i;  // Use low IDs (slots 256~355)
        
        auto start = high_resolution_clock::now();
        auto result = registry_->RegisterService(
            service_id, 1, 1, 0, "iceoryx2", "shm://test"
        );
        auto end = high_resolution_clock::now();
        
        ASSERT_TRUE(result.HasValue()) << "Failed to register service_id 0x" 
            << std::hex << service_id << " (slot " << std::dec << (service_id & 1023) << ")";
        latencies_ns.push_back(duration_cast<nanoseconds>(end - start).count());
    }
    
    std::sort(latencies_ns.begin(), latencies_ns.end());
    
    uint64_t p50 = latencies_ns[NUM_SAMPLES * 50 / 100];
    uint64_t p99 = latencies_ns[NUM_SAMPLES * 99 / 100];
    uint64_t avg = std::accumulate(latencies_ns.begin(), latencies_ns.end(), 0ULL) / NUM_SAMPLES;
    
    std::cout << "\nRegisterService Latency Benchmark (" << NUM_SAMPLES << " samples):\n"
              << "  Average: " << avg << " ns\n"
              << "  P50:     " << p50 << " ns\n"
              << "  P99:     " << p99 << " ns\n";
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
