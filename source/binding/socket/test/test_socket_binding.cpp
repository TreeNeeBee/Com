/**
 * @file        test_socket_binding.cpp
 * @brief       Socket binding unit tests — Unix/TCP socket implementation
 * @date        2026/02/28
 */

#include "SocketBinding.hpp"
#include "ComTypes.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>

using namespace lap::com::binding;

class SocketBindingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        binding_ = std::make_unique< SocketBinding >();
    }

    void TearDown() override
    {
        if ( binding_ )
        {
            binding_->Shutdown();
            binding_.reset();
        }
    }

    std::unique_ptr< SocketBinding > binding_;
};

// ====================================================================
// Phase 0 — Capability Queries
// ====================================================================

TEST_F( SocketBindingTest, CapabilityQueries )
{
    EXPECT_STREQ( binding_->GetName(), "Socket" );
    EXPECT_EQ( binding_->GetVersion(), 0x00010000U );
    EXPECT_EQ( binding_->GetPriority(), 40U );
    EXPECT_FALSE( binding_->SupportsZeroCopy() );
}

// ====================================================================
// Phase 1 — Lifecycle
// ====================================================================

TEST_F( SocketBindingTest, InitializeSucceeds )
{
    auto result = binding_->Initialize();
    ASSERT_TRUE( result.HasValue() ) << "Initialize() should succeed";
}

TEST_F( SocketBindingTest, InitializeIdempotent )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );
    ASSERT_TRUE( binding_->Initialize().HasValue() );
}

TEST_F( SocketBindingTest, ShutdownBeforeInit )
{
    auto result = binding_->Shutdown();
    EXPECT_TRUE( result.HasValue() );
}

TEST_F( SocketBindingTest, ShutdownIdempotent )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );
    ASSERT_TRUE( binding_->Shutdown().HasValue() );
    ASSERT_TRUE( binding_->Shutdown().HasValue() );
}

// ====================================================================
// Phase 2 — Service Management
// ====================================================================

TEST_F( SocketBindingTest, OfferAndFindService )
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

TEST_F( SocketBindingTest, OfferServiceIdempotent )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );
    ASSERT_TRUE( binding_->OfferService( 0x100, 0x01 ).HasValue() );
    ASSERT_TRUE( binding_->OfferService( 0x100, 0x01 ).HasValue() );
}

TEST_F( SocketBindingTest, StopOfferService )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    ASSERT_TRUE( binding_->OfferService( 0x100, 0x01 ).HasValue() );
    ASSERT_TRUE( binding_->StopOfferService( 0x100, 0x01 ).HasValue() );

    auto findResult = binding_->FindService( 0x100 );
    ASSERT_TRUE( findResult.HasValue() );
    EXPECT_TRUE( findResult.Value().empty() );
}

TEST_F( SocketBindingTest, StartFindServiceCallback )
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

TEST_F( SocketBindingTest, StopFindServiceSucceeds )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );
    EXPECT_TRUE( binding_->StopFindService( 42 ).HasValue() );
}

TEST_F( SocketBindingTest, ServiceNotInitializedError )
{
    auto offer = binding_->OfferService( 0x100, 0x01 );
    EXPECT_FALSE( offer.HasValue() );
}

// ====================================================================
// Phase 3 — Event Communication (local dispatch)
// ====================================================================

TEST_F( SocketBindingTest, SendEventSucceeds )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    ByteBuffer data = { 0x01, 0x02, 0x03, 0x04 };
    auto result = binding_->SendEvent( 0x1234, 0x01, 100, data );
    EXPECT_TRUE( result.HasValue() );
}

TEST_F( SocketBindingTest, EventLocalDelivery )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    std::atomic< int > received{ 0 };

    ASSERT_TRUE( binding_->SubscribeEvent< ByteBuffer >(
        0x1234, 0x01, 100,
        [&received](
            uint64_t /* sid */, uint64_t /* iid */,
            uint32_t /* eid */, const ByteBuffer& /* data */ )
        {
            ++received;
        }
    ).HasValue() );

    ByteBuffer data = { 0xAA, 0xBB, 0xCC };
    ASSERT_TRUE( binding_->SendEvent( 0x1234, 0x01, 100, data ).HasValue() );

    // Local delivery is synchronous
    EXPECT_EQ( received.load(), 1 );
}

TEST_F( SocketBindingTest, SubscribeAndUnsubscribeEvent )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    ASSERT_TRUE( binding_->SubscribeEvent< ByteBuffer >(
        0x1234, 0x01, 100,
        []( uint64_t, uint64_t, uint32_t, const ByteBuffer& ) {}
    ).HasValue() );

    EXPECT_TRUE( binding_->UnsubscribeEvent( 0x1234, 0x01, 100 ).HasValue() );
}

TEST_F( SocketBindingTest, SendEventUpdatesMetrics )
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

TEST_F( SocketBindingTest, RegisterMethodSucceeds )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    auto result = binding_->RegisterMethod< ByteBuffer, ByteBuffer >(
        0x1234, 0x01, 1,
        []( uint64_t, uint64_t, uint32_t, const ByteBuffer& req )
        {
            return req;
        }
    );
    EXPECT_TRUE( result.HasValue() );
}

TEST_F( SocketBindingTest, MethodLocalDispatch )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    // Register a local handler
    auto regResult = binding_->RegisterMethod< ByteBuffer, ByteBuffer >(
        0x1234, 0x01, 1,
        []( uint64_t, uint64_t, uint32_t, const ByteBuffer& req )
        {
            ByteBuffer resp( req.rbegin(), req.rend() );
            return resp;
        }
    );
    ASSERT_TRUE( regResult.HasValue() );

    // Call — uses local handler
    ByteBuffer request = { 0x01, 0x02, 0x03 };
    auto result = binding_->CallMethod< ByteBuffer >( 0x1234, 0x01, 1, request );
    ASSERT_TRUE( result.HasValue() ) << "CallMethod with local handler should succeed";
}

// ====================================================================
// Phase 5 — Field Communication
// ====================================================================

TEST_F( SocketBindingTest, SubscribeFieldNotificationSucceeds )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    auto result = binding_->SubscribeFieldNotification< ByteBuffer >(
        0x1234, 0x01, 50,
        []( uint64_t, uint64_t, uint32_t, const ByteBuffer& ) {}
    );
    EXPECT_TRUE( result.HasValue() );
}

TEST_F( SocketBindingTest, UnsubscribeFieldNotificationSucceeds )
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

TEST_F( SocketBindingTest, MetricsAfterInit )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    auto metrics = binding_->GetMetrics();
    EXPECT_EQ( metrics.messagesSent, 0U );
    EXPECT_EQ( metrics.messagesReceived, 0U );
}

TEST_F( SocketBindingTest, MetricsAfterMethodCall )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

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

TEST_F( SocketBindingTest, ReinitializeAfterShutdown )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );
    ASSERT_TRUE( binding_->Shutdown().HasValue() );
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    ASSERT_TRUE( binding_->OfferService( 0x100, 0x01 ).HasValue() );
    auto find = binding_->FindService( 0x100 );
    ASSERT_TRUE( find.HasValue() );
    EXPECT_FALSE( find.Value().empty() );
}
