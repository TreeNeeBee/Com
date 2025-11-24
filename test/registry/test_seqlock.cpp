/**
 * @file        test_seqlock.cpp
 * @author      LightAP Development Team
 * @brief       Unit tests for seqlock synchronization mechanism
 * @date        2025-11-20
 * @details     Validates seqlock read/write correctness under concurrent access.
 *              Performance target: < 100ns read latency (P99)
 * @copyright   Copyright (c) 2025
 * @note        Tests AUTOSAR SWS_CM_00110 (thread-safe registry access)
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §2.1.2
 * sdk:
 * platform:    Linux 5.10+
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/20  <td>1.0      <td>LightAP Team    <td>Initial test suite
 * </table>
 */

#include "ServiceSlot.hpp"
#include "SeqLock.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <numeric>
#include <algorithm>

using namespace lap::com::registry;
using namespace std::chrono;

// ============================================================================
// Test Fixture
// ============================================================================

class SeqLockTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Initialize slot to known state
        slot_.Reset();
        slot_.sequence.store(0, std::memory_order_release);
    }

    void TearDown() override
    {
        // Cleanup
    }

    ServiceSlot slot_;
};

// ============================================================================
// Basic Functionality Tests
// ============================================================================

/**
 * @test Verify ServiceSlot structure size and alignment
 * @req SWS_CM_00302, SWS_CM_00303
 */
TEST_F(SeqLockTest, SlotSizeAndAlignment)
{
    // Verify 256-byte size (4 cache lines)
    EXPECT_EQ(sizeof(ServiceSlot), 256u);
    
    // Verify 64-byte alignment
    EXPECT_EQ(alignof(ServiceSlot), 64u);
    
    // Verify slot address is cache-line aligned
    uintptr_t addr = reinterpret_cast<uintptr_t>(&slot_);
    EXPECT_EQ(addr % 64, 0u) << "Slot must be 64-byte aligned";
}

/**
 * @test Verify initial slot state
 */
TEST_F(SeqLockTest, InitialState)
{
    EXPECT_EQ(slot_.sequence.load(), 0u);
    EXPECT_EQ(slot_.service_id, 0u);
    EXPECT_EQ(slot_.instance_id, 0u);
    EXPECT_EQ(slot_.status, static_cast<uint32_t>(SlotStatus::IDLE));
    EXPECT_TRUE(slot_.IsIdle());
    EXPECT_FALSE(slot_.IsActive());
}

/**
 * @test Basic seqlock write operation
 */
TEST_F(SeqLockTest, BasicWrite)
{
    {
        SeqLockWriter writer(slot_.sequence);
        
        // Sequence should be odd (write in progress)
        EXPECT_EQ(slot_.sequence.load() & 1, 1u) << "Sequence must be odd during write";
        
        // Write data
        slot_.service_id = 0x1234;
        slot_.instance_id = 0x5678;
        slot_.major_version = 1;
        slot_.minor_version = 0;
    }
    
    // After writer destruction, sequence should be even
    EXPECT_EQ(slot_.sequence.load() & 1, 0u) << "Sequence must be even after write";
    EXPECT_EQ(slot_.sequence.load(), 2u) << "Sequence incremented twice (0→1→2)";
    
    // Verify written data
    EXPECT_EQ(slot_.service_id, 0x1234u);
    EXPECT_EQ(slot_.instance_id, 0x5678u);
}

/**
 * @test Basic seqlock read operation
 */
TEST_F(SeqLockTest, BasicRead)
{
    // Setup: write test data
    {
        SeqLockWriter writer(slot_.sequence);
        slot_.service_id = 0xABCD;
        slot_.instance_id = 0xEF01;
        std::strcpy(slot_.endpoint, "tcp://192.168.1.10:30509");
    }
    
    // Test: read service_id
    auto result = SeqLockReader::Read(slot_, [](const ServiceSlot& s) {
        return s.service_id;
    });
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 0xABCDu);
    
    // Test: read endpoint
    auto endpoint_result = SeqLockReader::Read(slot_, [](const ServiceSlot& s) {
        return std::string(s.endpoint);
    });
    
    ASSERT_TRUE(endpoint_result.has_value());
    EXPECT_EQ(endpoint_result.value(), "tcp://192.168.1.10:30509");
}

/**
 * @test Read entire slot atomically
 */
TEST_F(SeqLockTest, ReadFullSlot)
{
    // Setup
    {
        SeqLockWriter writer(slot_.sequence);
        slot_.service_id = 0x1111;
        slot_.instance_id = 0x2222;
        slot_.major_version = 3;
        slot_.minor_version = 4;
        slot_.status = static_cast<uint32_t>(SlotStatus::ACTIVE);
    }
    
    // Read entire slot
    auto result = SeqLockReader::ReadSlot(slot_);
    
    ASSERT_TRUE(result.has_value());
    const auto& slot_copy = result.value();
    
    EXPECT_EQ(slot_copy.service_id, 0x1111u);
    EXPECT_EQ(slot_copy.instance_id, 0x2222u);
    EXPECT_EQ(slot_copy.major_version, 3u);
    EXPECT_EQ(slot_copy.minor_version, 4u);
    EXPECT_EQ(slot_copy.status, static_cast<uint32_t>(SlotStatus::ACTIVE));
}

// ============================================================================
// Concurrency Tests
// ============================================================================

/**
 * @test Concurrent reads should not block each other
 * @note Target: < 100ns read latency without contention
 */
TEST_F(SeqLockTest, ConcurrentReads)
{
    // Setup: write initial data
    {
        SeqLockWriter writer(slot_.sequence);
        slot_.service_id = 0x9999;
    }
    
    constexpr int NUM_READERS = 10;
    constexpr int READS_PER_THREAD = 10000;
    
    std::atomic<uint64_t> total_reads{0};
    std::atomic<uint64_t> successful_reads{0};
    
    auto reader_func = [&]() {
        for (int i = 0; i < READS_PER_THREAD; ++i) {
            auto result = SeqLockReader::Read(slot_, [](const ServiceSlot& s) {
                return s.service_id;
            });
            
            total_reads.fetch_add(1, std::memory_order_relaxed);
            
            if (result.has_value()) {
                EXPECT_EQ(result.value(), 0x9999u);
                successful_reads.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };
    
    // Launch reader threads
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_READERS; ++i) {
        threads.emplace_back(reader_func);
    }
    
    // Wait for completion
    for (auto& t : threads) {
        t.join();
    }
    
    // All reads should succeed (no write contention)
    EXPECT_EQ(total_reads.load(), NUM_READERS * READS_PER_THREAD);
    EXPECT_EQ(successful_reads.load(), NUM_READERS * READS_PER_THREAD);
}

/**
 * @test Readers should retry during concurrent write
 */
TEST_F(SeqLockTest, ConcurrentReadWrite)
{
    constexpr int NUM_READERS = 8;
    constexpr int NUM_WRITERS = 2;
    constexpr int ITERATIONS = 1000;
    
    std::atomic<bool> stop_flag{false};
    std::atomic<uint64_t> successful_reads{0};
    std::atomic<uint64_t> successful_writes{0};
    
    // Reader threads
    auto reader_func = [&]() {
        while (!stop_flag.load(std::memory_order_acquire)) {
            auto result = SeqLockReader::Read(slot_, [](const ServiceSlot& s) {
                return s.service_id;
            });
            
            if (result.has_value()) {
                successful_reads.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };
    
    // Writer threads
    auto writer_func = [&]() {
        for (int i = 0; i < ITERATIONS; ++i) {
            {
                SeqLockWriter writer(slot_.sequence);
                slot_.service_id = 0x1000 + i;
                // Simulate some work
                std::this_thread::sleep_for(microseconds(1));
            }
            successful_writes.fetch_add(1, std::memory_order_relaxed);
        }
    };
    
    // Launch threads
    std::vector<std::thread> threads;
    
    for (int i = 0; i < NUM_READERS; ++i) {
        threads.emplace_back(reader_func);
    }
    
    for (int i = 0; i < NUM_WRITERS; ++i) {
        threads.emplace_back(writer_func);
    }
    
    // Wait for writers to complete
    for (size_t i = NUM_READERS; i < threads.size(); ++i) {
        threads[i].join();
    }
    
    // Stop readers
    stop_flag.store(true, std::memory_order_release);
    
    for (size_t i = 0; i < NUM_READERS; ++i) {
        threads[i].join();
    }
    
    // Verify all writes completed
    EXPECT_EQ(successful_writes.load(), NUM_WRITERS * ITERATIONS);
    
    // Verify reads occurred (some may have failed due to contention)
    EXPECT_GT(successful_reads.load(), 0u) << "At least some reads should succeed";
    
    std::cout << "Concurrent R/W test: " 
              << successful_reads.load() << " successful reads, "
              << successful_writes.load() << " successful writes" << std::endl;
}

// ============================================================================
// Performance Tests
// ============================================================================

/**
 * @test Measure seqlock read latency (target: < 100ns P99)
 * @note Performance benchmark, may vary by hardware
 */
TEST_F(SeqLockTest, ReadLatencyBenchmark)
{
    // Setup
    {
        SeqLockWriter writer(slot_.sequence);
        slot_.service_id = 0xBEEF;
    }
    
    constexpr int NUM_SAMPLES = 100000;
    std::vector<uint64_t> latencies_ns;
    latencies_ns.reserve(NUM_SAMPLES);
    
    // Warm-up
    for (int i = 0; i < 1000; ++i) {
        SeqLockReader::Read(slot_, [](const ServiceSlot& s) { return s.service_id; });
    }
    
    // Benchmark
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        auto start = high_resolution_clock::now();
        
        auto result = SeqLockReader::Read(slot_, [](const ServiceSlot& s) {
            return s.service_id;
        });
        
        auto end = high_resolution_clock::now();
        
        ASSERT_TRUE(result.has_value());
        latencies_ns.push_back(duration_cast<nanoseconds>(end - start).count());
    }
    
    // Calculate statistics
    std::sort(latencies_ns.begin(), latencies_ns.end());
    
    uint64_t p50 = latencies_ns[NUM_SAMPLES * 50 / 100];
    uint64_t p99 = latencies_ns[NUM_SAMPLES * 99 / 100];
    uint64_t p999 = latencies_ns[NUM_SAMPLES * 999 / 1000];
    uint64_t avg = std::accumulate(latencies_ns.begin(), latencies_ns.end(), 0ULL) / NUM_SAMPLES;
    
    std::cout << "\nseqlock Read Latency Benchmark (" << NUM_SAMPLES << " samples):\n"
              << "  Average: " << avg << " ns\n"
              << "  P50:     " << p50 << " ns\n"
              << "  P99:     " << p99 << " ns\n"
              << "  P99.9:   " << p999 << " ns\n";
    
    // Performance assertion (target: < 100ns P99)
    // Note: May fail on slow hardware, adjust if needed
    EXPECT_LT(p99, 200u) << "P99 read latency should be < 200ns (target: < 100ns)";
}

/**
 * @test Measure write latency
 */
TEST_F(SeqLockTest, WriteLatencyBenchmark)
{
    constexpr int NUM_SAMPLES = 10000;
    std::vector<uint64_t> latencies_ns;
    latencies_ns.reserve(NUM_SAMPLES);
    
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        auto start = high_resolution_clock::now();
        
        {
            SeqLockWriter writer(slot_.sequence);
            slot_.service_id = 0x1000 + i;
            slot_.instance_id = 0x2000 + i;
        }
        
        auto end = high_resolution_clock::now();
        latencies_ns.push_back(duration_cast<nanoseconds>(end - start).count());
    }
    
    std::sort(latencies_ns.begin(), latencies_ns.end());
    
    uint64_t p50 = latencies_ns[NUM_SAMPLES * 50 / 100];
    uint64_t p99 = latencies_ns[NUM_SAMPLES * 99 / 100];
    uint64_t avg = std::accumulate(latencies_ns.begin(), latencies_ns.end(), 0ULL) / NUM_SAMPLES;
    
    std::cout << "\nseqlock Write Latency Benchmark (" << NUM_SAMPLES << " samples):\n"
              << "  Average: " << avg << " ns\n"
              << "  P50:     " << p50 << " ns\n"
              << "  P99:     " << p99 << " ns\n";
}

// ============================================================================
// Edge Cases
// ============================================================================

/**
 * @test Read retry on max contention
 */
TEST_F(SeqLockTest, ReadRetryLimit)
{
    // Hold write lock continuously
    SeqLockWriter writer(slot_.sequence);
    
    // Try to read (should fail after max retries)
    auto result = SeqLockReader::Read(slot_, [](const ServiceSlot& s) {
        return s.service_id;
    });
    
    // Read should eventually fail (max retries exceeded)
    EXPECT_FALSE(result.has_value()) << "Read should fail when write lock is held";
}

/**
 * @test Slot reset functionality
 */
TEST_F(SeqLockTest, SlotReset)
{
    // Write data
    {
        SeqLockWriter writer(slot_.sequence);
        slot_.service_id = 0xFFFF;
        slot_.instance_id = 0xEEEE;
        slot_.status = static_cast<uint32_t>(SlotStatus::ACTIVE);
        std::strcpy(slot_.endpoint, "test_endpoint");
    }
    
    // Reset slot
    {
        SeqLockWriter writer(slot_.sequence);
        slot_.Reset();
    }
    
    // Verify reset
    EXPECT_EQ(slot_.service_id, 0u);
    EXPECT_EQ(slot_.instance_id, 0u);
    EXPECT_TRUE(slot_.IsIdle());
    EXPECT_EQ(slot_.endpoint[0], '\0');
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
