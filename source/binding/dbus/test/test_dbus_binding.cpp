/**
 * @file        test_dbus_binding.cpp
 * @brief       D-Bus binding unit tests — sd-bus implementation (offline-safe)
 * @date        2026/02/28
 */

#include "DbusBinding.hpp"
#include "ComTypes.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>

using namespace lap::com::binding;

class DbusBindingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        binding_ = std::make_unique< DbusBinding >();
    }

    void TearDown() override
    {
        if ( binding_ )
        {
            binding_->Shutdown();
            binding_.reset();
        }
    }

    std::unique_ptr< DbusBinding > binding_;
};

// ====================================================================
// Phase 0 — Capability Queries
// ====================================================================

TEST_F( DbusBindingTest, CapabilityQueries )
{
    EXPECT_STREQ( binding_->GetName(), "DBus" );
    EXPECT_EQ( binding_->GetVersion(), 0x00010000U );
    EXPECT_EQ( binding_->GetPriority(), 20U );
    EXPECT_FALSE( binding_->SupportsZeroCopy() );
}

// ====================================================================
// Phase 1 — Lifecycle
// ====================================================================

TEST_F( DbusBindingTest, InitializeSucceeds )
{
    // May connect to bus or fall back to offline mode — either way succeeds
    auto result = binding_->Initialize();
    ASSERT_TRUE( result.HasValue() ) << "Initialize() should succeed";
}

TEST_F( DbusBindingTest, InitializeIdempotent )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );
    ASSERT_TRUE( binding_->Initialize().HasValue() );
}

TEST_F( DbusBindingTest, ShutdownBeforeInit )
{
    auto result = binding_->Shutdown();
    EXPECT_TRUE( result.HasValue() );
}

TEST_F( DbusBindingTest, ShutdownIdempotent )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );
    ASSERT_TRUE( binding_->Shutdown().HasValue() );
    ASSERT_TRUE( binding_->Shutdown().HasValue() );
}

// ====================================================================
// Phase 2 — Service Management
// ====================================================================

TEST_F( DbusBindingTest, OfferAndFindService )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    uint64_t serviceId  = 0x1234;
    uint64_t instanceId = 0x0001;

    ASSERT_TRUE( binding_->OfferService( serviceId, instanceId ).HasValue() );

    auto findResult = binding_->FindService( serviceId );
    ASSERT_TRUE( findResult.HasValue() );
    EXPECT_FALSE( findResult.Value().empty() );
    EXPECT_EQ( findResult.Value().front(), instanceId );
}

TEST_F( DbusBindingTest, StopOfferService )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    ASSERT_TRUE( binding_->OfferService( 0x100, 0x01 ).HasValue() );
    ASSERT_TRUE( binding_->StopOfferService( 0x100, 0x01 ).HasValue() );

    auto findResult = binding_->FindService( 0x100 );
    ASSERT_TRUE( findResult.HasValue() );
    EXPECT_TRUE( findResult.Value().empty() );
}

TEST_F( DbusBindingTest, StartFindServiceCallback )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );
    ASSERT_TRUE( binding_->OfferService( 0x200, 0x01 ).HasValue() );

    bool callbackFired = false;
    auto result = binding_->StartFindService( 0x200,
        [&callbackFired]( uint64_t svc, const std::vector< uint64_t >& instances )
        {
            callbackFired = true;
            EXPECT_EQ( svc, 0x200U );
            EXPECT_FALSE( instances.empty() );
        }
    );
    EXPECT_TRUE( result.HasValue() );
    EXPECT_TRUE( callbackFired );
}

TEST_F( DbusBindingTest, StopFindServiceSucceeds )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );
    EXPECT_TRUE( binding_->StopFindService( 42 ).HasValue() );
}

TEST_F( DbusBindingTest, ServiceNotInitializedError )
{
    auto offer = binding_->OfferService( 0x100, 0x01 );
    EXPECT_FALSE( offer.HasValue() );
}

// ====================================================================
// Phase 3 — Event Communication (local dispatch)
// ====================================================================

TEST_F( DbusBindingTest, SendEventSucceeds )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    ByteBuffer data = { 0x01, 0x02, 0x03, 0x04 };
    auto result = binding_->SendEvent( 0x1234, 0x01, 100, data );
    EXPECT_TRUE( result.HasValue() );
}

TEST_F( DbusBindingTest, EventLocalDelivery )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    std::atomic< int > received{ 0 };
    ByteBuffer receivedData;

    ASSERT_TRUE( binding_->SubscribeEvent< ByteBuffer >(
        0x1234, 0x01, 100,
        [&received, &receivedData](
            uint64_t /* sid */, uint64_t /* iid */,
            uint32_t /* eid */, const ByteBuffer& data )
        {
            ++received;
            receivedData = data;
        }
    ).HasValue() );

    // Send event — should be delivered locally
    ByteBuffer data = { 0xAA, 0xBB, 0xCC };
    ASSERT_TRUE( binding_->SendEvent( 0x1234, 0x01, 100, data ).HasValue() );

    // Local delivery is synchronous
    EXPECT_EQ( received.load(), 1 );
}

TEST_F( DbusBindingTest, SubscribeAndUnsubscribeEvent )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    ASSERT_TRUE( binding_->SubscribeEvent< ByteBuffer >(
        0x1234, 0x01, 100,
        []( uint64_t, uint64_t, uint32_t, const ByteBuffer& ) {}
    ).HasValue() );

    EXPECT_TRUE( binding_->UnsubscribeEvent( 0x1234, 0x01, 100 ).HasValue() );
}

TEST_F( DbusBindingTest, SendEventUpdatesMetrics )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    auto metricsBefore = binding_->GetMetrics();

    ByteBuffer data( 32, 0xAB );
    ASSERT_TRUE( binding_->SendEvent( 0x1234, 0x01, 1, data ).HasValue() );

    auto metricsAfter = binding_->GetMetrics();
    EXPECT_GT( metricsAfter.messagesSent, metricsBefore.messagesSent );
    // Note: bytesSent may be 0 because WireSize<ByteBuffer>() == 0 (NVI design)
}

// ====================================================================
// Phase 4 — Method Communication (local handler dispatch)
// ====================================================================

TEST_F( DbusBindingTest, RegisterMethodSucceeds )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    auto result = binding_->RegisterMethod< ByteBuffer, ByteBuffer >(
        0x1234, 0x01, 1,
        []( uint64_t, uint64_t, uint32_t, const ByteBuffer& req )
        {
            return req;  // echo
        }
    );
    EXPECT_TRUE( result.HasValue() );
}

TEST_F( DbusBindingTest, MethodLocalDispatch )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    // Register a method handler
    auto regResult = binding_->RegisterMethod< ByteBuffer, ByteBuffer >(
        0x1234, 0x01, 1,
        []( uint64_t, uint64_t, uint32_t, const ByteBuffer& req )
        {
            ByteBuffer resp( req.rbegin(), req.rend() );
            return resp;
        }
    );
    ASSERT_TRUE( regResult.HasValue() );

    // Call the method — will use local handler
    ByteBuffer request = { 0x01, 0x02, 0x03 };
    auto result = binding_->CallMethod< ByteBuffer >( 0x1234, 0x01, 1, request );
    ASSERT_TRUE( result.HasValue() ) << "CallMethod with local handler should succeed";
}

// ====================================================================
// Phase 5 — Field Communication
// ====================================================================

TEST_F( DbusBindingTest, SubscribeFieldNotificationSucceeds )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    auto result = binding_->SubscribeFieldNotification< ByteBuffer >(
        0x1234, 0x01, 50,
        []( uint64_t, uint64_t, uint32_t, const ByteBuffer& ) {}
    );
    EXPECT_TRUE( result.HasValue() );
}

TEST_F( DbusBindingTest, UnsubscribeFieldNotificationSucceeds )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    ASSERT_TRUE( binding_->SubscribeFieldNotification< ByteBuffer >(
        0x1234, 0x01, 50,
        []( uint64_t, uint64_t, uint32_t, const ByteBuffer& ) {}
    ).HasValue() );

    EXPECT_TRUE( binding_->UnsubscribeFieldNotification(
        0x1234, 0x01, 50 ).HasValue() );
}

// ====================================================================
// Phase 6 — Metrics
// ====================================================================

TEST_F( DbusBindingTest, MetricsAfterInit )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    auto metrics = binding_->GetMetrics();
    EXPECT_EQ( metrics.messagesSent, 0U );
    EXPECT_EQ( metrics.messagesReceived, 0U );
}

TEST_F( DbusBindingTest, MetricsAfterMethodCall )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    // Register local handler
    auto regResult = binding_->RegisterMethod< ByteBuffer, ByteBuffer >(
        0x1234, 0x01, 1,
        []( uint64_t, uint64_t, uint32_t, const ByteBuffer& req )
        { return req; }
    );
    ASSERT_TRUE( regResult.HasValue() );

    ByteBuffer req = { 0x01 };
    ASSERT_TRUE( binding_->CallMethod< ByteBuffer >(
        0x1234, 0x01, 1, req ).HasValue() );

    auto metrics = binding_->GetMetrics();
    EXPECT_GE( metrics.messagesSent, 1U );
    EXPECT_GE( metrics.messagesReceived, 1U );
}

// ====================================================================
// Phase 7 — Reinitialize After Shutdown
// ====================================================================

TEST_F( DbusBindingTest, ReinitializeAfterShutdown )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );
    ASSERT_TRUE( binding_->Shutdown().HasValue() );
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    // Should work after re-init
    ASSERT_TRUE( binding_->OfferService( 0x100, 0x01 ).HasValue() );
    auto find = binding_->FindService( 0x100 );
    ASSERT_TRUE( find.HasValue() );
    EXPECT_FALSE( find.Value().empty() );
}
