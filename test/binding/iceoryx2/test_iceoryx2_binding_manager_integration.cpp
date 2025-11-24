/**
 * @file        test_iceoryx2_binding_manager_integration.cpp
 * @author      LightAP Development Team
 * @brief       Integration test for iceoryx2 Binding with BindingManager
 * @date        2025-11-23
 * @copyright   Copyright (c) 2025
 * 
 * @details     Tests the complete integration of iceoryx2 Binding with BindingManager
 */

#include <gtest/gtest.h>

#include "../../../source/binding/iceoryx2/inc/Iceoryx2Binding.hpp"
#include "../../../source/binding/common/ITransportBinding.hpp"

#include <lap/core/CResult.hpp>
#include <lap/log/CLogger.hpp>

#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <sstream>

using namespace lap::com::binding;
using namespace lap::core;

namespace {

// Test constants
constexpr uint64_t TEST_SERVICE_ID = 0x1234;
constexpr uint64_t TEST_INSTANCE_ID = 0x0001;
constexpr uint32_t TEST_EVENT_ID = 0x01;

// ============================================================================
// Test Fixture
// ============================================================================

class Iceoryx2BindingManagerIntegrationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        LAP_COM_LOG_INFO << "========================================";
        LAP_COM_LOG_INFO << "  iceoryx2 BindingManager Integration Test";
        LAP_COM_LOG_INFO << "========================================";
    }

    void TearDown() override
    {
        LAP_COM_LOG_INFO << "Test completed, cleaning up...";
    }
};

// ============================================================================
// Test 1: Direct Binding Creation
// ============================================================================

TEST_F(Iceoryx2BindingManagerIntegrationTest, DirectBindingCreation)
{
    LAP_COM_LOG_INFO << "\n=== Test 1: Direct Binding Creation ===";
    
    auto binding = std::make_unique<Iceoryx2Binding>();
    ASSERT_NE(binding, nullptr);
    
    // Initialize
    auto init_result = binding->Initialize();
    ASSERT_TRUE(init_result.HasValue()) << "Failed to initialize binding";
    LAP_COM_LOG_INFO << "\u2713 Binding initialized";
    
    // Check capabilities
    EXPECT_STREQ(binding->GetName(), "iceoryx2");
    EXPECT_EQ(binding->GetPriority(), 100);
    EXPECT_TRUE(binding->SupportsZeroCopy());
    EXPECT_TRUE(binding->SupportsService(TEST_SERVICE_ID));
    LAP_COM_LOG_INFO << "\u2713 Capabilities verified: name=iceoryx2, priority=100, zero_copy=true";
    
    // Shutdown
    auto shutdown_result = binding->Shutdown();
    ASSERT_TRUE(shutdown_result.HasValue()) << "Failed to shutdown binding";
    LAP_COM_LOG_INFO << "\u2713 Binding shutdown complete";
}

// ============================================================================
// Test 2: Complete Pub/Sub Communication Flow
// ============================================================================

TEST_F(Iceoryx2BindingManagerIntegrationTest, CompletePubSubFlow)
{
    LAP_COM_LOG_INFO << "\n=== Test 2: Complete Pub/Sub Communication Flow ===";
    
    auto binding = std::make_unique<Iceoryx2Binding>();
    auto init_result = binding->Initialize();
    ASSERT_TRUE(init_result.HasValue());
    
    // 1. Offer service
    auto offer_result = binding->OfferService(TEST_SERVICE_ID, TEST_INSTANCE_ID);
    ASSERT_TRUE(offer_result.HasValue()) << "Failed to offer service";
    LAP_COM_LOG_INFO << "\u2713 Service offered";
    
    // 2. Subscribe to events
    std::atomic<int> received_count{0};
    std::vector<ByteBuffer> received_data;
    std::mutex data_mutex;
    
    EventCallback callback = [&](uint64_t service_id, uint64_t instance_id, 
                                  uint32_t event_id, const ByteBuffer& data) {
        std::lock_guard<std::mutex> lock(data_mutex);
        received_count++;
        received_data.push_back(data);
        
        std::ostringstream oss;
        oss << "Event received: service=0x" << std::hex << service_id
            << ", instance=0x" << instance_id
            << ", event=0x" << event_id << std::dec
            << ", size=" << data.size() << " bytes";
        LAP_COM_LOG_DEBUG << oss.str();
    };
    
    auto subscribe_result = binding->SubscribeEvent(
        TEST_SERVICE_ID, TEST_INSTANCE_ID, TEST_EVENT_ID, callback);
    ASSERT_TRUE(subscribe_result.HasValue()) << "Failed to subscribe";
    LAP_COM_LOG_INFO << "\u2713 Subscribed to events";
    
    // Wait for subscriber to be ready
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 3. Send multiple events
    const int num_messages = 10;
    for (int i = 0; i < num_messages; ++i)
    {
        ByteBuffer data = {
            static_cast<uint8_t>(i),
            static_cast<uint8_t>((i >> 8) & 0xFF),
            0xAA, 0xBB, 0xCC, 0xDD
        };
        
        auto send_result = binding->SendEvent(
            TEST_SERVICE_ID, TEST_INSTANCE_ID, TEST_EVENT_ID, data);
        ASSERT_TRUE(send_result.HasValue()) << "Failed to send event " << i;
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    LAP_COM_LOG_INFO << "\u2713 Sent " << num_messages << " events";
    
    // Wait for all messages
    auto wait_start = std::chrono::steady_clock::now();
    while (received_count.load() < num_messages)
    {
        auto elapsed = std::chrono::steady_clock::now() - wait_start;
        if (elapsed > std::chrono::seconds(5))
        {
            FAIL() << "Timeout waiting for messages. Received: " << received_count.load() 
                   << "/" << num_messages;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    int final_received = received_count.load();
    EXPECT_EQ(final_received, num_messages);
    LAP_COM_LOG_INFO << "\u2713 Received " << final_received << "/" << num_messages << " events";
    
    // Verify data
    {
        std::lock_guard<std::mutex> lock(data_mutex);
        for (int i = 0; i < num_messages; ++i)
        {
            ASSERT_LT(i, received_data.size());
            EXPECT_EQ(received_data[i][0], static_cast<uint8_t>(i));
            EXPECT_EQ(received_data[i][2], 0xAA);
        }
    }
    LAP_COM_LOG_INFO << "\u2713 Data integrity verified";
    
    // 4. Cleanup
    auto unsubscribe_result = binding->UnsubscribeEvent(
        TEST_SERVICE_ID, TEST_INSTANCE_ID, TEST_EVENT_ID);
    ASSERT_TRUE(unsubscribe_result.HasValue());
    
    auto stop_result = binding->StopOfferService(TEST_SERVICE_ID, TEST_INSTANCE_ID);
    ASSERT_TRUE(stop_result.HasValue());
    
    binding->Shutdown();
    LAP_COM_LOG_INFO << "\u2713 Cleanup complete";
}

// ============================================================================
// Test 3: Performance Metrics Collection
// ============================================================================

TEST_F(Iceoryx2BindingManagerIntegrationTest, PerformanceMetrics)
{
    LAP_COM_LOG_INFO << "\n=== Test 3: Performance Metrics Collection ===";
    
    auto binding = std::make_unique<Iceoryx2Binding>();
    binding->Initialize();
    
    // Get initial metrics
    auto metrics_before = binding->GetMetrics();
    EXPECT_EQ(metrics_before.messages_sent, 0);
    EXPECT_EQ(metrics_before.messages_received, 0);
    LAP_COM_LOG_INFO << "\u2713 Initial metrics: sent=0, received=0";
    
    // Setup communication
    binding->OfferService(TEST_SERVICE_ID, TEST_INSTANCE_ID);
    
    std::atomic<int> received{0};
    EventCallback callback = [&](uint64_t, uint64_t, uint32_t, const ByteBuffer&) {
        received++;
    };
    binding->SubscribeEvent(TEST_SERVICE_ID, TEST_INSTANCE_ID, TEST_EVENT_ID, callback);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Send messages
    const int num_messages = 20;
    for (int i = 0; i < num_messages; ++i)
    {
        ByteBuffer data(128, static_cast<uint8_t>(i));
        binding->SendEvent(TEST_SERVICE_ID, TEST_INSTANCE_ID, TEST_EVENT_ID, data);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    // Wait for messages
    auto wait_start = std::chrono::steady_clock::now();
    while (received.load() < num_messages)
    {
        if (std::chrono::steady_clock::now() - wait_start > std::chrono::seconds(3))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Get final metrics
    auto metrics_after = binding->GetMetrics();
    EXPECT_EQ(metrics_after.messages_sent, num_messages);
    EXPECT_EQ(metrics_after.messages_received, num_messages);
    EXPECT_EQ(metrics_after.bytes_sent, 128 * num_messages);
    EXPECT_EQ(metrics_after.bytes_received, 128 * num_messages);
    EXPECT_GT(metrics_after.avg_latency_ns, 0);
    
    LAP_COM_LOG_INFO << "\u2713 Metrics after test:";
    LAP_COM_LOG_INFO << "  - Messages sent: " << metrics_after.messages_sent;
    LAP_COM_LOG_INFO << "  - Messages received: " << metrics_after.messages_received;
    LAP_COM_LOG_INFO << "  - Bytes sent: " << metrics_after.bytes_sent;
    LAP_COM_LOG_INFO << "  - Bytes received: " << metrics_after.bytes_received;
    LAP_COM_LOG_INFO << "  - Avg latency: " << metrics_after.avg_latency_ns << " ns";
    
    // Cleanup
    binding->UnsubscribeEvent(TEST_SERVICE_ID, TEST_INSTANCE_ID, TEST_EVENT_ID);
    binding->StopOfferService(TEST_SERVICE_ID, TEST_INSTANCE_ID);
    binding->Shutdown();
}

} // anonymous namespace

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
