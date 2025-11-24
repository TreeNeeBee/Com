/**
 * @file        test_runtime.cpp
 * @author      LightAP Development Team
 * @brief       Unit tests for Runtime service discovery integration
 * @date        2025-11-20
 * @details     Tests Runtime::Initialize/Deinitialize, RegisterService, FindService APIs.
 *              Validates integration with SharedMemoryRegistry backend.
 * @copyright   Copyright (c) 2025
 * @note        Week 3: Runtime API integration testing
 * sdk:
 * platform:    Linux 5.10+
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/20  <td>1.0      <td>LightAP Team    <td>Initial Runtime integration tests
 * </table>
 */

#include "Runtime.hpp"
#include "ServiceSlot.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <numeric>
#include <algorithm>

using namespace lap::com;

/**
 * @brief Test fixture for Runtime integration tests
 */
class RuntimeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Ensure clean state before each test
        if (Runtime::IsInitialized())
        {
            Runtime::Deinitialize();
        }
    }
    
    void TearDown() override
    {
        // Cleanup after each test
        if (Runtime::IsInitialized())
        {
            Runtime::Deinitialize();
        }
    }
};

// ============================================================================
// Test: Runtime Initialization
// ============================================================================

TEST_F(RuntimeTest, InitializeSuccess)
{
    EXPECT_FALSE(Runtime::IsInitialized());
    
    auto result = Runtime::Initialize();
    ASSERT_TRUE(result.HasValue()) << "Runtime initialization should succeed";
    
    EXPECT_TRUE(Runtime::IsInitialized());
}

TEST_F(RuntimeTest, InitializeTwiceFails)
{
    auto result1 = Runtime::Initialize();
    ASSERT_TRUE(result1.HasValue());
    
    // Second initialization should fail
    auto result2 = Runtime::Initialize();
    EXPECT_FALSE(result2.HasValue()) << "Second Initialize should fail";
}

TEST_F(RuntimeTest, DeinitializeWithoutInitFails)
{
    EXPECT_FALSE(Runtime::IsInitialized());
    
    auto result = Runtime::Deinitialize();
    EXPECT_FALSE(result.HasValue()) << "Deinitialize without Init should fail";
}

TEST_F(RuntimeTest, InitializeDeinitializeCycle)
{
    // Multiple init/deinit cycles
    for (int i = 0; i < 3; ++i)
    {
        auto init_result = Runtime::Initialize();
        ASSERT_TRUE(init_result.HasValue()) << "Cycle " << i << " Init failed";
        EXPECT_TRUE(Runtime::IsInitialized());
        
        auto deinit_result = Runtime::Deinitialize();
        ASSERT_TRUE(deinit_result.HasValue()) << "Cycle " << i << " Deinit failed";
        EXPECT_FALSE(Runtime::IsInitialized());
    }
}

// ============================================================================
// Test: Service Registration
// ============================================================================

TEST_F(RuntimeTest, RegisterServiceBeforeInitFails)
{
    EXPECT_FALSE(Runtime::IsInitialized());
    
    auto result = RegisterService(0x1234, 0x0001, 0);
    EXPECT_FALSE(result.HasValue()) << "RegisterService before Init should fail";
}

TEST_F(RuntimeTest, RegisterServiceSuccess)
{
    ASSERT_TRUE(Runtime::Initialize().HasValue());
    
    auto result = RegisterService(0x1234, 0x0001, 0); // iceoryx2 binding
    EXPECT_TRUE(result.HasValue()) << "RegisterService should succeed";
}

TEST_F(RuntimeTest, RegisterServiceInvalidID)
{
    ASSERT_TRUE(Runtime::Initialize().HasValue());
    
    // Service ID 0 is invalid
    auto result1 = RegisterService(0, 0x0001, 0);
    EXPECT_FALSE(result1.HasValue()) << "Service ID 0 should be rejected";
    
    // Service ID > 0x3fff is invalid for QM+AB
    auto result2 = RegisterService(0x4000, 0x0001, 0);
    EXPECT_FALSE(result2.HasValue()) << "Service ID > 0x3fff should be rejected";
    
    // Instance ID 0 is invalid
    auto result3 = RegisterService(0x1234, 0, 0);
    EXPECT_FALSE(result3.HasValue()) << "Instance ID 0 should be rejected";
    
    // Instance ID 0xffff is reserved
    auto result4 = RegisterService(0x1234, 0xffff, 0);
    EXPECT_FALSE(result4.HasValue()) << "Instance ID 0xffff should be rejected";
}

TEST_F(RuntimeTest, RegisterMultipleServices)
{
    ASSERT_TRUE(Runtime::Initialize().HasValue());
    
    // Register 10 different services
    for (lap::core::UInt16 i = 1; i <= 10; ++i)
    {
        auto result = RegisterService(0x1000 + i, 0x0001, 0);
        EXPECT_TRUE(result.HasValue()) << "Service " << i << " registration failed";
    }
}

// ============================================================================
// Test: Service Discovery
// ============================================================================

TEST_F(RuntimeTest, FindServiceBeforeInitReturnsEmpty)
{
    EXPECT_FALSE(Runtime::IsInitialized());
    
    auto result = FindService(0x1234);
    EXPECT_FALSE(result.has_value()) << "FindService before Init should return empty";
}

TEST_F(RuntimeTest, FindNonExistentServiceReturnsEmpty)
{
    ASSERT_TRUE(Runtime::Initialize().HasValue());
    
    auto result = FindService(0x9999);
    EXPECT_FALSE(result.has_value()) << "Non-existent service should return empty";
}

TEST_F(RuntimeTest, FindRegisteredService)
{
    ASSERT_TRUE(Runtime::Initialize().HasValue());
    
    const lap::core::UInt16 service_id = 0x1234;
    const lap::core::UInt16 instance_id = 0x0001;
    
    // Register service
    auto reg_result = RegisterService(service_id, instance_id, 0);
    ASSERT_TRUE(reg_result.HasValue());
    
    // Find service
    auto find_result = FindService(service_id);
    ASSERT_TRUE(find_result.has_value()) << "Registered service should be found";
    
    const auto& slot = find_result.value();
    EXPECT_EQ(slot.service_id, service_id);
    EXPECT_EQ(slot.instance_id, instance_id);
}

TEST_F(RuntimeTest, FindServiceAfterUnregister)
{
    ASSERT_TRUE(Runtime::Initialize().HasValue());
    
    const lap::core::UInt16 service_id = 0x1234;
    
    // Register
    ASSERT_TRUE(RegisterService(service_id, 0x0001, 0).HasValue());
    
    // Verify exists
    ASSERT_TRUE(FindService(service_id).has_value());
    
    // Unregister
    auto unreg_result = UnregisterService(service_id);
    ASSERT_TRUE(unreg_result.HasValue());
    
    // Should not be found
    auto find_result = FindService(service_id);
    EXPECT_FALSE(find_result.has_value()) << "Unregistered service should not be found";
}

// ============================================================================
// Test: Concurrent Access
// ============================================================================

TEST_F(RuntimeTest, ConcurrentRegisterFind)
{
    ASSERT_TRUE(Runtime::Initialize().HasValue());
    
    std::atomic<int> registration_count{0};
    std::atomic<int> find_count{0};
    
    auto register_worker = [&]()
    {
        for (int i = 0; i < 100; ++i)
        {
            lap::core::UInt16 service_id = 0x2000 + (i % 50);
            if (RegisterService(service_id, 0x0001, 0).HasValue())
            {
                registration_count.fetch_add(1);
            }
        }
    };
    
    auto find_worker = [&]()
    {
        for (int i = 0; i < 100; ++i)
        {
            lap::core::UInt16 service_id = 0x2000 + (i % 50);
            if (FindService(service_id).has_value())
            {
                find_count.fetch_add(1);
            }
        }
    };
    
    std::thread t1(register_worker);
    std::thread t2(find_worker);
    std::thread t3(find_worker);
    
    t1.join();
    t2.join();
    t3.join();
    
    std::cout << "Concurrent test: " 
              << registration_count << " registrations, "
              << find_count << " finds\n";
    
    EXPECT_GT(registration_count.load(), 0);
}

// ============================================================================
// Test: Performance Benchmarks
// ============================================================================

TEST_F(RuntimeTest, InitializePerformance)
{
    using namespace std::chrono;
    
    auto start = high_resolution_clock::now();
    auto result = Runtime::Initialize();
    auto end = high_resolution_clock::now();
    
    ASSERT_TRUE(result.HasValue());
    
    auto duration_us = duration_cast<microseconds>(end - start).count();
    
    std::cout << "Initialize latency: " << duration_us << " µs\n";
    
    EXPECT_LT(duration_us, 1000) << "Initialize should complete in < 1ms";
}

TEST_F(RuntimeTest, FindServiceLatency)
{
    using namespace std::chrono;
    
    ASSERT_TRUE(Runtime::Initialize().HasValue());
    
    // Register a service
    const lap::core::UInt16 service_id = 0x3333;
    ASSERT_TRUE(RegisterService(service_id, 0x0001, 0).HasValue());
    
    // Benchmark FindService
    constexpr int SAMPLES = 10000;
    std::vector<long> latencies;
    latencies.reserve(SAMPLES);
    
    for (int i = 0; i < SAMPLES; ++i)
    {
        auto start = high_resolution_clock::now();
        auto result = FindService(service_id);
        auto end = high_resolution_clock::now();
        
        ASSERT_TRUE(result.has_value());
        latencies.push_back(duration_cast<nanoseconds>(end - start).count());
    }
    
    std::sort(latencies.begin(), latencies.end());
    long p50 = latencies[SAMPLES / 2];
    long p99 = latencies[(SAMPLES * 99) / 100];
    long avg = std::accumulate(latencies.begin(), latencies.end(), 0L) / SAMPLES;
    
    std::cout << "\nFindService Latency Benchmark (" << SAMPLES << " samples):\n"
              << "  Average: " << avg << " ns\n"
              << "  P50:     " << p50 << " ns\n"
              << "  P99:     " << p99 << " ns\n";
    
    EXPECT_LT(p99, 500) << "FindService P99 should be < 500ns";
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
