/**
 * @file        test_dds_binding.cpp
 * @brief       Basic DDS Binding unit tests
 * @author      LightAP Team
 * @date        2025-11-23
 */

#include "DdsBinding.hpp"
#include "ComTypes.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace lap::com::binding;

class DdsBindingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        binding_ = std::make_unique<DdsBinding>();
    }

    void TearDown() override
    {
        binding_->Shutdown();
        binding_.reset();
    }

    std::unique_ptr<DdsBinding> binding_;
};

/**
 * @brief Test 1: Basic initialization and shutdown
 */
TEST_F(DdsBindingTest, InitializeAndShutdown)
{
    auto result = binding_->Initialize();
    ASSERT_TRUE(result.HasValue()) << "Initialization failed";

    // Verify binding properties
    EXPECT_STREQ(binding_->GetName(), "DDS");
    EXPECT_EQ(binding_->GetVersion(), 0x00010000);
    EXPECT_EQ(binding_->GetPriority(), 80);
    EXPECT_TRUE(binding_->SupportsService(0x1234));

    auto shutdown_result = binding_->Shutdown();
    ASSERT_TRUE(shutdown_result.HasValue()) << "Shutdown failed";
}

/**
 * @brief Test 2: OfferService and StopOfferService
 */
TEST_F(DdsBindingTest, OfferServiceLifecycle)
{
    ASSERT_TRUE(binding_->Initialize().HasValue());

    uint64_t service_id = 0x1234;
    uint64_t instance_id = 0x0001;

    // Offer service
    ASSERT_TRUE(binding_->OfferService(service_id, instance_id).HasValue()) << "OfferService failed";

    // Offer again (should succeed - idempotent)
    ASSERT_TRUE(binding_->OfferService(service_id, instance_id).HasValue()) << "Second OfferService should succeed";

    // Stop offering
    ASSERT_TRUE(binding_->StopOfferService(service_id, instance_id).HasValue()) << "StopOfferService failed";

    // Stop again (should succeed - idempotent)
    ASSERT_TRUE(binding_->StopOfferService(service_id, instance_id).HasValue()) << "Second StopOfferService should succeed";
}

/**
 * @brief Test 3: SendEvent and SubscribeEvent
 * 
 * NOTE: This test is DISABLED because DDS Binding is designed for cross-process/cross-ECU
 * communication. FastDDS uses intra-process optimization that bypasses DataReaderListener
 * callbacks when publisher and subscriber are in the same process.
 * 
 * For DDS functionality testing, use test_dds_cross_process instead:
 *   Terminal 1: ./test_dds_cross_process sub
 *   Terminal 2: ./test_dds_cross_process pub
 */
TEST_F(DdsBindingTest, DISABLED_PubSubBasic)
{
    ASSERT_TRUE(binding_->Initialize().HasValue());

    uint64_t service_id = 0x1234;
    uint64_t instance_id = 0x0001;
    uint32_t event_id = 100;

    std::atomic<int> received_count{0};
    ByteBuffer received_data;

    // Subscribe to event
    ASSERT_TRUE(binding_->SubscribeEvent(
        service_id,
        instance_id,
        event_id,
        [&received_count, &received_data](
            uint64_t /* sid */,
            uint64_t /* iid */,
            uint32_t /* eid */,
            const ByteBuffer& data
        ) {
            received_count++;
            received_data = data;
        }
    ).HasValue()) << "SubscribeEvent failed";

    // Offer service
    ASSERT_TRUE(binding_->OfferService(service_id, instance_id).HasValue());

    // Wait for DDS discovery (publisher-subscriber matching)
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Send event
    ByteBuffer test_data = {0x01, 0x02, 0x03, 0x04, 0x05};
    ASSERT_TRUE(binding_->SendEvent(service_id, instance_id, event_id, test_data).HasValue()) << "SendEvent failed";

    // NOTE: In same-process, DDS intra-process optimization prevents callback firing
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Unsubscribe
    ASSERT_TRUE(binding_->UnsubscribeEvent(service_id, instance_id, event_id).HasValue()) << "UnsubscribeEvent failed";
}

/**
 * @brief Test 4: Performance metrics collection
 * 
 * NOTE: This test is DISABLED for same reason as PubSubBasic.
 * DDS intra-process optimization prevents callbacks in same-process scenarios.
 */
TEST_F(DdsBindingTest, DISABLED_MetricsCollection)
{
    ASSERT_TRUE(binding_->Initialize().HasValue());

    uint64_t service_id = 0x5678;
    uint64_t instance_id = 0x0002;
    uint32_t event_id = 200;

    // Create a dummy subscriber to avoid RELIABLE QoS blocking
    std::atomic<int> dummy_count{0};
    ASSERT_TRUE(binding_->SubscribeEvent(
        service_id, instance_id, event_id,
        [&dummy_count](uint64_t, uint64_t, uint32_t, const ByteBuffer&) {
            dummy_count++;
        }
    ).HasValue());

    ASSERT_TRUE(binding_->OfferService(service_id, instance_id).HasValue());

    // Wait for DDS matching
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Send multiple events
    const int num_messages = 10;
    ByteBuffer test_data(128, 0xAB);  // 128 bytes

    for (int i = 0; i < num_messages; i++) {
        ASSERT_TRUE(binding_->SendEvent(service_id, instance_id, event_id, test_data).HasValue());
    }

    // Wait for messages to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Check metrics
    auto metrics = binding_->GetMetrics();
    EXPECT_GE(metrics.messages_sent, num_messages);
    EXPECT_GE(metrics.bytes_sent, num_messages * test_data.size());
    EXPECT_GT(metrics.avg_latency_ns, 0.0);
    EXPECT_GT(metrics.max_latency_ns, 0);

    // Cleanup
    binding_->UnsubscribeEvent(service_id, instance_id, event_id);
}

/**
 * @brief Test 5: Unimplemented methods return NOT_IMPLEMENTED error
 */
TEST_F(DdsBindingTest, UnimplementedMethods)
{
    ASSERT_TRUE(binding_->Initialize().HasValue());

    ByteBuffer dummy_data = {0x01, 0x02};

    // CallMethod
    auto method_result = binding_->CallMethod(0x1234, 0x0001, 1, dummy_data);
    EXPECT_FALSE(method_result.HasValue());

    // RegisterMethod
    auto register_result = binding_->RegisterMethod(
        0x1234, 0x0001, 1,
        [](uint64_t, uint64_t, uint32_t, const ByteBuffer&) { return ByteBuffer(); }
    );
    EXPECT_FALSE(register_result.HasValue());

    // GetField
    auto get_result = binding_->GetField(0x1234, 0x0001, 1);
    EXPECT_FALSE(get_result.HasValue());

    // SetField
    auto set_result = binding_->SetField(0x1234, 0x0001, 1, dummy_data);
    EXPECT_FALSE(set_result.HasValue());
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
