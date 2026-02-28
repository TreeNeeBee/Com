/**
 * @file        test_someip_binding.cpp
 * @brief       SOME/IP binding unit tests — lightweight UDP implementation
 * @date        2026/02/28
 */

#include "SomeIpBinding.hpp"
#include "ComTypes.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>

using namespace lap::com::binding;

class SomeIpBindingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        binding_ = std::make_unique< SomeIpBinding >();
    }

    void TearDown() override
    {
        if ( binding_ )
        {
            binding_->Shutdown();
            binding_.reset();
        }
    }

    std::unique_ptr< SomeIpBinding > binding_;
};

// ====================================================================
// Phase 0 — Capability Queries
// ====================================================================

TEST_F( SomeIpBindingTest, CapabilityQueries )
{
    EXPECT_STREQ( binding_->GetName(), "SOME/IP" );
    EXPECT_EQ( binding_->GetVersion(), 0x00010000U );
    EXPECT_EQ( binding_->GetPriority(), 60U );
    EXPECT_FALSE( binding_->SupportsZeroCopy() );
}

// ====================================================================
// Phase 1 — Lifecycle
// ====================================================================

TEST_F( SomeIpBindingTest, InitializeSucceeds )
{
    auto result = binding_->Initialize();
    ASSERT_TRUE( result.HasValue() ) << "Initialize() should succeed";
}

TEST_F( SomeIpBindingTest, InitializeIdempotent )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );
    ASSERT_TRUE( binding_->Initialize().HasValue() );
}

TEST_F( SomeIpBindingTest, ShutdownBeforeInit )
{
    auto result = binding_->Shutdown();
    EXPECT_TRUE( result.HasValue() ) << "Shutdown without init should succeed";
}

TEST_F( SomeIpBindingTest, ShutdownIdempotent )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );
    ASSERT_TRUE( binding_->Shutdown().HasValue() );
    ASSERT_TRUE( binding_->Shutdown().HasValue() );
}

// ====================================================================
// Phase 2 — Service Management
// ====================================================================

TEST_F( SomeIpBindingTest, OfferAndFindService )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    uint64_t serviceId  = 0x1234;
    uint64_t instanceId = 0x0001;

    // Offer service
    ASSERT_TRUE( binding_->OfferService( serviceId, instanceId ).HasValue() );

    // Find service — should find our offered instance
    auto findResult = binding_->FindService( serviceId );
    ASSERT_TRUE( findResult.HasValue() );
    EXPECT_FALSE( findResult.Value().empty() );
    EXPECT_EQ( findResult.Value().front(), instanceId );
}

TEST_F( SomeIpBindingTest, OfferServiceIdempotent )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );
    ASSERT_TRUE( binding_->OfferService( 0x100, 0x01 ).HasValue() );
    ASSERT_TRUE( binding_->OfferService( 0x100, 0x01 ).HasValue() );
}

TEST_F( SomeIpBindingTest, StopOfferService )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    ASSERT_TRUE( binding_->OfferService( 0x100, 0x01 ).HasValue() );
    ASSERT_TRUE( binding_->StopOfferService( 0x100, 0x01 ).HasValue() );

    // After stop, find should return empty
    auto findResult = binding_->FindService( 0x100 );
    ASSERT_TRUE( findResult.HasValue() );
    EXPECT_TRUE( findResult.Value().empty() );
}

TEST_F( SomeIpBindingTest, StartFindServiceCallback )
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

TEST_F( SomeIpBindingTest, StopFindServiceSucceeds )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );
    EXPECT_TRUE( binding_->StopFindService( 42 ).HasValue() );
}

TEST_F( SomeIpBindingTest, SupportsServiceAfterOffer )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );
    // Before offering — still returns true (initialized = supports discovery)
    EXPECT_TRUE( binding_->SupportsService( 0x5678 ) );
}

TEST_F( SomeIpBindingTest, ServiceNotInitializedError )
{
    // Service operations before init should fail
    auto offer = binding_->OfferService( 0x100, 0x01 );
    EXPECT_FALSE( offer.HasValue() );
}

// ====================================================================
// Phase 3 — Event Communication
// ====================================================================

TEST_F( SomeIpBindingTest, SendEventSucceeds )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );
    ASSERT_TRUE( binding_->OfferService( 0x1234, 0x01 ).HasValue() );

    ByteBuffer data = { 0x01, 0x02, 0x03, 0x04 };
    auto result = binding_->SendEvent( 0x1234, 0x01, 100, data );
    EXPECT_TRUE( result.HasValue() ) << "SendEvent should succeed";
}

TEST_F( SomeIpBindingTest, SubscribeEventSucceeds )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    auto result = binding_->SubscribeEvent< ByteBuffer >(
        0x1234, 0x01, 100,
        []( uint64_t, uint64_t, uint32_t, const ByteBuffer& ) {}
    );
    EXPECT_TRUE( result.HasValue() ) << "SubscribeEvent should succeed";
}

TEST_F( SomeIpBindingTest, UnsubscribeEventSucceeds )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    // Subscribe first
    ASSERT_TRUE( binding_->SubscribeEvent< ByteBuffer >(
        0x1234, 0x01, 100,
        []( uint64_t, uint64_t, uint32_t, const ByteBuffer& ) {}
    ).HasValue() );

    // Unsubscribe
    EXPECT_TRUE( binding_->UnsubscribeEvent( 0x1234, 0x01, 100 ).HasValue() );
}

TEST_F( SomeIpBindingTest, SendEventUpdatesMetrics )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    auto metricsBefore = binding_->GetMetrics();

    ByteBuffer data( 64, 0xAB );
    ASSERT_TRUE( binding_->SendEvent( 0x1234, 0x01, 1, data ).HasValue() );

    auto metricsAfter = binding_->GetMetrics();
    EXPECT_GT( metricsAfter.messagesSent, metricsBefore.messagesSent );
    EXPECT_GT( metricsAfter.bytesSent, metricsBefore.bytesSent );
}

// ====================================================================
// Phase 4 — Method Communication
// ====================================================================

TEST_F( SomeIpBindingTest, RegisterMethodSucceeds )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    auto result = binding_->RegisterMethod< ByteBuffer, ByteBuffer >(
        0x1234, 0x01, 1,
        []( uint64_t, uint64_t, uint32_t, const ByteBuffer& req )
        {
            return req;  // echo
        }
    );
    EXPECT_TRUE( result.HasValue() ) << "RegisterMethod should succeed";
}

// ====================================================================
// Phase 5 — Field Communication
// ====================================================================

TEST_F( SomeIpBindingTest, SubscribeFieldNotificationSucceeds )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    auto result = binding_->SubscribeFieldNotification< ByteBuffer >(
        0x1234, 0x01, 50,
        []( uint64_t, uint64_t, uint32_t, const ByteBuffer& ) {}
    );
    EXPECT_TRUE( result.HasValue() ) << "SubscribeFieldNotification should succeed";
}

TEST_F( SomeIpBindingTest, UnsubscribeFieldNotificationSucceeds )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    ASSERT_TRUE( binding_->SubscribeFieldNotification< ByteBuffer >(
        0x1234, 0x01, 50,
        []( uint64_t, uint64_t, uint32_t, const ByteBuffer& ) {}
    ).HasValue() );

    auto result = binding_->UnsubscribeFieldNotification( 0x1234, 0x01, 50 );
    EXPECT_TRUE( result.HasValue() );
}

// ====================================================================
// Phase 6 — Metrics
// ====================================================================

TEST_F( SomeIpBindingTest, MetricsAfterInit )
{
    ASSERT_TRUE( binding_->Initialize().HasValue() );

    auto metrics = binding_->GetMetrics();
    EXPECT_EQ( metrics.messagesSent, 0U );
    EXPECT_EQ( metrics.messagesReceived, 0U );
    EXPECT_EQ( metrics.bytesSent, 0U );
    EXPECT_EQ( metrics.bytesReceived, 0U );
}

// ====================================================================
// Phase 7 — Reinitialize After Shutdown
// ====================================================================

TEST_F( SomeIpBindingTest, ReinitializeAfterShutdown )
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
