/**
 * @file        test_iceoryx2_binding.cpp
 * @author      LightAP Development Team
 * @brief       Unit tests for iceoryx2 binding
 * @date        2025-11-22
 * @copyright   Copyright (c) 2025
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "Iceoryx2Binding.hpp"

using namespace lap::com::binding;
using namespace testing;

// ========================================================================
// Test Fixture
// ========================================================================

class Iceoryx2BindingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        binding_ = std::make_unique<Iceoryx2Binding>();
    }

    void TearDown() override
    {
        binding_.reset();
    }

    std::unique_ptr<Iceoryx2Binding> binding_;
};

// ========================================================================
// Lifecycle Tests
// ========================================================================

TEST_F(Iceoryx2BindingTest, Initialize_Success)
{
    auto result = binding_->Initialize();
    ASSERT_TRUE(result.HasValue()) << "Initialize should succeed";
}

TEST_F(Iceoryx2BindingTest, Initialize_Idempotent)
{
    // First initialization
    auto result1 = binding_->Initialize();
    ASSERT_TRUE(result1.HasValue());

    // Second initialization (should succeed but log warning)
    auto result2 = binding_->Initialize();
    ASSERT_TRUE(result2.HasValue()) << "Double initialize should be idempotent";
}

TEST_F(Iceoryx2BindingTest, Shutdown_WithoutInitialize)
{
    // Should not crash
    auto result = binding_->Shutdown();
    ASSERT_TRUE(result.HasValue()) << "Shutdown without initialize should succeed";
}

TEST_F(Iceoryx2BindingTest, Shutdown_AfterInitialize)
{
    binding_->Initialize();
    
    auto result = binding_->Shutdown();
    ASSERT_TRUE(result.HasValue()) << "Shutdown should succeed";
}

// ========================================================================
// Service Management Tests
// ========================================================================

TEST_F(Iceoryx2BindingTest, OfferService_WithoutInitialize)
{
    auto result = binding_->OfferService(0x1234, 0x0001);
    ASSERT_FALSE(result.HasValue()) << "OfferService should fail without initialize";
}

TEST_F(Iceoryx2BindingTest, OfferService_Success)
{
    binding_->Initialize();
    
    auto result = binding_->OfferService(0x1234, 0x0001);
    ASSERT_TRUE(result.HasValue()) << "OfferService should succeed after initialize";
}

TEST_F(Iceoryx2BindingTest, OfferService_Duplicate)
{
    binding_->Initialize();
    
    // First offer
    auto result1 = binding_->OfferService(0x1234, 0x0001);
    ASSERT_TRUE(result1.HasValue());

    // Duplicate offer (should succeed but log warning)
    auto result2 = binding_->OfferService(0x1234, 0x0001);
    ASSERT_TRUE(result2.HasValue()) << "Duplicate offer should be idempotent";
}

TEST_F(Iceoryx2BindingTest, StopOfferService_NotOffered)
{
    binding_->Initialize();
    
    auto result = binding_->StopOfferService(0x1234, 0x0001);
    ASSERT_TRUE(result.HasValue()) << "StopOffer on non-offered service should succeed (no-op)";
}

TEST_F(Iceoryx2BindingTest, StopOfferService_Success)
{
    binding_->Initialize();
    binding_->OfferService(0x1234, 0x0001);
    
    auto result = binding_->StopOfferService(0x1234, 0x0001);
    ASSERT_TRUE(result.HasValue()) << "StopOfferService should succeed";
}

TEST_F(Iceoryx2BindingTest, FindService_NoOp)
{
    binding_->Initialize();
    
    FindServiceHandler handler = [](const ServiceInfo&) {};
    auto result = binding_->FindService(0x1234, handler);
    ASSERT_TRUE(result.HasValue()) << "FindService should succeed (no-op for iceoryx2)";
}

// ========================================================================
// Event Communication Tests
// ========================================================================

TEST_F(Iceoryx2BindingTest, SendEvent_WithoutOffer)
{
    binding_->Initialize();
    
    EventData data;
    data.service_id = 0x1234;
    data.instance_id = 0x0001;
    data.event_id = 0x0100;
    data.payload = {0x01, 0x02, 0x03, 0x04};

    auto result = binding_->SendEvent(data);
    ASSERT_FALSE(result.HasValue()) << "SendEvent should fail without OfferService";
}

TEST_F(Iceoryx2BindingTest, SendEvent_Success)
{
    binding_->Initialize();
    binding_->OfferService(0x1234, 0x0001);
    
    EventData data;
    data.service_id = 0x1234;
    data.instance_id = 0x0001;
    data.event_id = 0x0100;
    data.payload = {0x01, 0x02, 0x03, 0x04};

    auto result = binding_->SendEvent(data);
    ASSERT_TRUE(result.HasValue()) << "SendEvent should succeed after OfferService";
}

TEST_F(Iceoryx2BindingTest, SubscribeEvent_Success)
{
    binding_->Initialize();
    
    bool handler_called = false;
    EventReceiveHandler handler = [&](const EventData& data) {
        handler_called = true;
    };

    auto result = binding_->SubscribeEvent(0x1234, 0x0001, 0x0100, handler);
    ASSERT_TRUE(result.HasValue()) << "SubscribeEvent should succeed";
}

TEST_F(Iceoryx2BindingTest, SubscribeEvent_Duplicate)
{
    binding_->Initialize();
    
    EventReceiveHandler handler = [](const EventData&) {};

    // First subscribe
    auto result1 = binding_->SubscribeEvent(0x1234, 0x0001, 0x0100, handler);
    ASSERT_TRUE(result1.HasValue());

    // Duplicate subscribe (should succeed but log warning)
    auto result2 = binding_->SubscribeEvent(0x1234, 0x0001, 0x0100, handler);
    ASSERT_TRUE(result2.HasValue()) << "Duplicate subscribe should be idempotent";
}

TEST_F(Iceoryx2BindingTest, UnsubscribeEvent_NotSubscribed)
{
    binding_->Initialize();
    
    auto result = binding_->UnsubscribeEvent(0x1234, 0x0001, 0x0100);
    ASSERT_TRUE(result.HasValue()) << "Unsubscribe on non-subscribed service should succeed (no-op)";
}

TEST_F(Iceoryx2BindingTest, UnsubscribeEvent_Success)
{
    binding_->Initialize();
    
    EventReceiveHandler handler = [](const EventData&) {};
    binding_->SubscribeEvent(0x1234, 0x0001, 0x0100, handler);
    
    auto result = binding_->UnsubscribeEvent(0x1234, 0x0001, 0x0100);
    ASSERT_TRUE(result.HasValue()) << "UnsubscribeEvent should succeed";
}

// ========================================================================
// Method Communication Tests (Not Supported)
// ========================================================================

TEST_F(Iceoryx2BindingTest, CallMethod_NotSupported)
{
    binding_->Initialize();
    
    MethodRequest request;
    request.service_id = 0x1234;
    request.instance_id = 0x0001;
    request.method_id = 0x0200;

    auto result = binding_->CallMethod(request);
    ASSERT_FALSE(result.HasValue()) << "CallMethod should fail (not supported)";
}

TEST_F(Iceoryx2BindingTest, RegisterMethod_NotSupported)
{
    binding_->Initialize();
    
    MethodCallHandler handler = [](const MethodRequest&) -> MethodResponse {
        return MethodResponse{};
    };

    auto result = binding_->RegisterMethod(0x1234, 0x0001, 0x0200, handler);
    ASSERT_FALSE(result.HasValue()) << "RegisterMethod should fail (not supported)";
}

// ========================================================================
// Field Communication Tests (Not Supported)
// ========================================================================

TEST_F(Iceoryx2BindingTest, GetField_NotSupported)
{
    binding_->Initialize();
    
    auto result = binding_->GetField(0x1234, 0x0001, 0x0300);
    ASSERT_FALSE(result.HasValue()) << "GetField should fail (not supported)";
}

TEST_F(Iceoryx2BindingTest, SetField_NotSupported)
{
    binding_->Initialize();
    
    FieldData data;
    data.service_id = 0x1234;
    data.instance_id = 0x0001;
    data.field_id = 0x0300;

    auto result = binding_->SetField(0x1234, 0x0001, 0x0300, data);
    ASSERT_FALSE(result.HasValue()) << "SetField should fail (not supported)";
}

// ========================================================================
// Capability Tests
// ========================================================================

TEST_F(Iceoryx2BindingTest, GetName)
{
    EXPECT_STREQ(binding_->GetName(), "iceoryx2");
}

TEST_F(Iceoryx2BindingTest, GetPriority)
{
    EXPECT_EQ(binding_->GetPriority(), 100u) << "iceoryx2 should have highest priority";
}

TEST_F(Iceoryx2BindingTest, SupportsZeroCopy)
{
    EXPECT_TRUE(binding_->SupportsZeroCopy()) << "iceoryx2 always supports zero-copy";
}

TEST_F(Iceoryx2BindingTest, SupportsService_AllLocal)
{
    EXPECT_TRUE(binding_->SupportsService(0x0001));
    EXPECT_TRUE(binding_->SupportsService(0x1234));
    EXPECT_TRUE(binding_->SupportsService(0xFFFF));
}

// ========================================================================
// Metrics Tests
// ========================================================================

TEST_F(Iceoryx2BindingTest, GetMetrics_Initial)
{
    binding_->Initialize();
    
    auto metrics = binding_->GetMetrics();
    EXPECT_EQ(metrics.messages_sent, 0u);
    EXPECT_EQ(metrics.messages_received, 0u);
    EXPECT_EQ(metrics.bytes_sent, 0u);
    EXPECT_EQ(metrics.bytes_received, 0u);
}

TEST_F(Iceoryx2BindingTest, GetMetrics_AfterSend)
{
    binding_->Initialize();
    binding_->OfferService(0x1234, 0x0001);
    
    EventData data;
    data.service_id = 0x1234;
    data.instance_id = 0x0001;
    data.event_id = 0x0100;
    data.payload = {0x01, 0x02, 0x03, 0x04};  // 4 bytes

    binding_->SendEvent(data);
    
    auto metrics = binding_->GetMetrics();
    EXPECT_EQ(metrics.messages_sent, 1u);
    EXPECT_EQ(metrics.bytes_sent, 4u);
    EXPECT_GT(metrics.avg_latency_ns, 0u) << "Average latency should be measured";
}

// ========================================================================
// Integration Tests
// ========================================================================

TEST_F(Iceoryx2BindingTest, PubSub_MultipleServices)
{
    binding_->Initialize();
    
    // Offer multiple services
    ASSERT_TRUE(binding_->OfferService(0x1234, 0x0001).HasValue());
    ASSERT_TRUE(binding_->OfferService(0x5678, 0x0002).HasValue());
    ASSERT_TRUE(binding_->OfferService(0xABCD, 0x0003).HasValue());
    
    // Send events to different services
    EventData data1{0x1234, 0x0001, 0x0100, {0x01}};
    EventData data2{0x5678, 0x0002, 0x0100, {0x02}};
    EventData data3{0xABCD, 0x0003, 0x0100, {0x03}};
    
    ASSERT_TRUE(binding_->SendEvent(data1).HasValue());
    ASSERT_TRUE(binding_->SendEvent(data2).HasValue());
    ASSERT_TRUE(binding_->SendEvent(data3).HasValue());
    
    auto metrics = binding_->GetMetrics();
    EXPECT_EQ(metrics.messages_sent, 3u);
}

TEST_F(Iceoryx2BindingTest, CleanShutdown_WithActiveSubscribers)
{
    binding_->Initialize();
    
    // Create subscriber
    EventReceiveHandler handler = [](const EventData&) {};
    binding_->SubscribeEvent(0x1234, 0x0001, 0x0100, handler);
    
    // Shutdown should stop listener thread cleanly
    auto result = binding_->Shutdown();
    ASSERT_TRUE(result.HasValue()) << "Shutdown with active subscribers should succeed";
}

// ========================================================================
// C Export Function Tests
// ========================================================================

TEST(Iceoryx2BindingExportTest, CreateAndDestroy)
{
    auto* instance = CreateBindingInstance();
    ASSERT_NE(instance, nullptr) << "CreateBindingInstance should return valid pointer";
    
    EXPECT_STREQ(instance->GetName(), "iceoryx2");
    EXPECT_EQ(instance->GetPriority(), 100u);
    
    DestroyBindingInstance(instance);
    // Should not crash
}

// ========================================================================
// Main
// ========================================================================

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
