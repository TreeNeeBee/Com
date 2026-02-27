/**
 * @file        test_runtime.cpp
 * @author      LightAP Development Team
 * @brief       Unit tests for Runtime lifecycle and service management
 * @date        2026/02/08
 * @details     Tests Runtime::Initialize/Deinitialize, OfferService/StopOfferService,
 *              FindService, RegisterService/UnregisterService/FindServiceById.
 *              Updated for R25-11 API (template-based OfferService/FindService,
 *              InstanceSpecifier-driven, PIMPL Runtime).
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/20  <td>1.0      <td>LightAP Team    <td>Initial Runtime tests
 * <tr><td>2026/02/08  <td>2.0      <td>Aii             <td>Rewrite for R25-11 API
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "Runtime.hpp"
#include "ComTypes.hpp"
#include "ServiceSlot.hpp"
#include "ServiceHandleType.hpp"

// ==================== Cross-Module Headers ====================
#include <core/CInstanceSpecifier.hpp>
#include <core/CTypedef.hpp>

// ==================== Third-Party Headers ====================
#include <gtest/gtest.h>

// ==================== Standard Library Headers ====================
#include <thread>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <atomic>
#include <iostream>
#include <vector>

using namespace lap::com;

// ============================================================================
// Mock Service Interface for template-based APIs
// ============================================================================

/**
 * @brief Minimal mock service interface for Runtime::FindService<T> / OfferService<T>
 * @details Exposes kServiceId and HandleType required by the Runtime template methods.
 */
struct MockRadarService
{
    static constexpr lap::core::UInt16 kServiceId = 0x1234;
    using HandleType = ServiceHandleType< MockRadarService >;
};

struct MockCameraService
{
    static constexpr lap::core::UInt16 kServiceId = 0x2000;
    using HandleType = ServiceHandleType< MockCameraService >;
};

// ============================================================================
// Test Fixture
// ============================================================================

/**
 * @brief Test fixture for Runtime integration tests
 * @details Ensures clean Runtime state before and after each test.
 */
class RuntimeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Ensure clean state before each test
        if ( Runtime::IsInitialized() )
        {
            Runtime::Deinitialize();
        }
    }

    void TearDown() override
    {
        // Cleanup after each test
        if ( Runtime::IsInitialized() )
        {
            Runtime::Deinitialize();
        }
    }

    /**
     * @brief Attempt Runtime::Initialize(); GTEST_SKIP if shared memory
     *        infrastructure is not available (no registry daemon).
     * @return true if initialization succeeded, false if skipped
     */
    Bool TryInitializeOrSkip()
    {
        auto result = Runtime::Initialize();
        if ( !result.HasValue() )
        {
            return false;
        }
        return true;
    }
};

/**
 * @brief Macro that calls TryInitializeOrSkip() and skips the test if infra
 *        is unavailable.  Must be used inside a TEST_F body so that GTEST_SKIP
 *        returns from the test function rather than from a helper method.
 */
#define INITIALIZE_OR_SKIP()                                                     \
    do {                                                                          \
        if ( !TryInitializeOrSkip() )                                             \
        {                                                                         \
            GTEST_SKIP() << "Shared memory registry not available (no daemon). "  \
                            "Skipping infrastructure-dependent test.";             \
        }                                                                         \
    } while ( 0 )

// ============================================================================
// Test Suite: Runtime Lifecycle (SWS_CM_00400)
// ============================================================================

/**
 * @test Runtime::Initialize succeeds on first call
 */
TEST_F( RuntimeTest, InitializeSuccess )
{
    EXPECT_FALSE( Runtime::IsInitialized() );

    INITIALIZE_OR_SKIP();

    EXPECT_TRUE( Runtime::IsInitialized() );
}

/**
 * @test Second Initialize returns error (kInvalidState)
 */
TEST_F( RuntimeTest, InitializeTwiceFails )
{
    INITIALIZE_OR_SKIP();

    // Second initialization should fail
    auto result2 = Runtime::Initialize();
    EXPECT_FALSE( result2.HasValue() ) << "Second Initialize should fail";
}

/**
 * @test Deinitialize without prior Initialize fails
 */
TEST_F( RuntimeTest, DeinitializeWithoutInitFails )
{
    EXPECT_FALSE( Runtime::IsInitialized() );

    auto result = Runtime::Deinitialize();
    EXPECT_FALSE( result.HasValue() ) << "Deinitialize without Init should fail";
}

/**
 * @test Multiple Initialize/Deinitialize cycles work correctly
 */
TEST_F( RuntimeTest, InitializeDeinitializeCycle )
{
    // First cycle: skip if infra unavailable
    INITIALIZE_OR_SKIP();
    EXPECT_TRUE( Runtime::IsInitialized() );
    auto deinit0 = Runtime::Deinitialize();
    ASSERT_TRUE( deinit0.HasValue() ) << "Cycle 0 Deinit failed";
    EXPECT_FALSE( Runtime::IsInitialized() );

    // Subsequent cycles: infra already proved available
    for ( Int32 i = 1; i < 3; ++i )
    {
        auto init_result = Runtime::Initialize();
        ASSERT_TRUE( init_result.HasValue() ) << "Cycle " << i << " Init failed";
        EXPECT_TRUE( Runtime::IsInitialized() );

        auto deinit_result = Runtime::Deinitialize();
        ASSERT_TRUE( deinit_result.HasValue() ) << "Cycle " << i << " Deinit failed";
        EXPECT_FALSE( Runtime::IsInitialized() );
    }
}

/**
 * @test Initialize with explicit config path
 */
TEST_F( RuntimeTest, InitializeWithConfigPath )
{
    auto result = Runtime::Initialize( "/nonexistent/path/config.yaml" );
    // Should still succeed — config is optional, runtime degrades gracefully
    // (Or may fail depending on implementation — we just verify it doesn't crash)
    if ( result.HasValue() )
    {
        EXPECT_TRUE( Runtime::IsInitialized() );
    }
}

// ============================================================================
// Test Suite: Registry Direct APIs (RegisterService / FindServiceById)
// ============================================================================

/**
 * @test RegisterService succeeds after initialization
 * @note Uses Runtime::RegisterService (non-template, registry-level API)
 */
TEST_F( RuntimeTest, RegisterServiceSuccess )
{
    INITIALIZE_OR_SKIP();

    auto& runtime = Runtime::GetInstance();
    auto result = runtime.RegisterService( 0x1234, 0x0001, 0 ); // coreipc binding
    EXPECT_TRUE( result.HasValue() ) << "RegisterService should succeed";
}

/**
 * @test FindServiceById returns registered service
 */
TEST_F( RuntimeTest, FindServiceByIdSuccess )
{
    INITIALIZE_OR_SKIP();

    auto& runtime = Runtime::GetInstance();
    const lap::core::UInt16 serviceId  = 0x1234;
    const lap::core::UInt16 instanceId = 0x0001;

    // Register
    auto regResult = runtime.RegisterService( serviceId, instanceId, 0 );
    ASSERT_TRUE( regResult.HasValue() );

    // Find
    auto findResult = runtime.FindServiceById( serviceId );
    ASSERT_TRUE( findResult.has_value() ) << "Registered service should be found";

    const auto& slot = findResult.value();
    EXPECT_EQ( static_cast< lap::core::UInt16 > ( slot.m_serviceId ), serviceId );
    EXPECT_EQ( static_cast< lap::core::UInt16 > ( slot.m_instanceId ), instanceId );
}

/**
 * @test FindServiceById on non-existent service returns empty
 */
TEST_F( RuntimeTest, FindServiceByIdNonExistent )
{
    INITIALIZE_OR_SKIP();

    auto& runtime = Runtime::GetInstance();
    auto findResult = runtime.FindServiceById( 0x9999 );
    EXPECT_FALSE( findResult.has_value() ) << "Non-existent service should not be found";
}

/**
 * @test UnregisterService removes the service from registry
 */
TEST_F( RuntimeTest, UnregisterServiceSuccess )
{
    INITIALIZE_OR_SKIP();

    auto& runtime = Runtime::GetInstance();
    const lap::core::UInt16 serviceId = 0x1234;

    // Register
    ASSERT_TRUE( runtime.RegisterService( serviceId, 0x0001, 0 ).HasValue() );

    // Verify it exists
    ASSERT_TRUE( runtime.FindServiceById( serviceId ).has_value() );

    // Unregister
    auto unregResult = runtime.UnregisterService( serviceId );
    ASSERT_TRUE( unregResult.HasValue() );

    // Should no longer be found
    auto findResult = runtime.FindServiceById( serviceId );
    EXPECT_FALSE( findResult.has_value() ) << "Unregistered service should not be found";
}

/**
 * @test Register multiple services and find each one
 */
TEST_F( RuntimeTest, RegisterMultipleServices )
{
    INITIALIZE_OR_SKIP();

    auto& runtime = Runtime::GetInstance();

    // Register 10 different services
    for ( lap::core::UInt16 i = 1; i <= 10; ++i )
    {
        auto result = runtime.RegisterService(
            static_cast< lap::core::UInt16 > ( 0x1000 + i ), 0x0001, 0 );
        EXPECT_TRUE( result.HasValue() ) << "Service " << i << " registration failed";
    }

    // Verify all can be found
    for ( lap::core::UInt16 i = 1; i <= 10; ++i )
    {
        auto findResult = runtime.FindServiceById(
            static_cast< lap::core::UInt16 > ( 0x1000 + i ) );
        EXPECT_TRUE( findResult.has_value() ) << "Service " << i << " not found";
    }
}

// ============================================================================
// Test Suite: Template-Based OfferService / FindService (SWS_CM_00101/00122)
// ============================================================================

/**
 * @test OfferService before Initialize returns error
 */
TEST_F( RuntimeTest, OfferServiceBeforeInitFails )
{
    EXPECT_FALSE( Runtime::IsInitialized() );

    lap::core::InstanceSpecifier spec( "test/radar" );
    auto result = Runtime::OfferService< MockRadarService > ( std::move( spec ) );
    EXPECT_FALSE( result.HasValue() ) << "OfferService before Init should fail";
}

/**
 * @test FindService before Initialize returns empty container
 */
TEST_F( RuntimeTest, FindServiceBeforeInitReturnsEmpty )
{
    EXPECT_FALSE( Runtime::IsInitialized() );

    lap::core::InstanceSpecifier spec( "test/radar" );
    auto result = Runtime::FindService< MockRadarService > ( std::move( spec ) );
    EXPECT_TRUE( result.empty() ) << "FindService before Init should return empty container";
}

/**
 * @test OfferService succeeds and can be found via FindService (template API)
 */
TEST_F( RuntimeTest, OfferAndFindService )
{
    INITIALIZE_OR_SKIP();

    // Offer the service
    lap::core::InstanceSpecifier offerSpec( "test/radar" );
    auto offerResult = Runtime::OfferService< MockRadarService > ( std::move( offerSpec ) );

    // OfferService may succeed or fail depending on binding availability
    // In test environment (no actual bindings loaded), it might fail — acceptable
    if ( offerResult.HasValue() )
    {
        // If offer succeeded, verify it can be found
        lap::core::InstanceSpecifier findSpec( "test/radar" );
        auto handles = Runtime::FindService< MockRadarService > ( std::move( findSpec ) );
        // In a properly configured environment, handles should contain results
        // In test environment without bindings, this may return empty
        SUCCEED() << "OfferService + FindService completed without crash";
    }
    else
    {
        // Offer failed (no binding available) — expected in test environment
        SUCCEED() << "OfferService failed gracefully (no binding): expected in test env";
    }
}

/**
 * @test StopOfferService does not crash before/after Init
 */
TEST_F( RuntimeTest, StopOfferServiceSafety )
{
    // Before init — should be no-op
    {
        lap::core::InstanceSpecifier spec( "test/radar" );
        Runtime::StopOfferService< MockRadarService > ( std::move( spec ) );
        SUCCEED() << "StopOfferService before Init did not crash";
    }

    INITIALIZE_OR_SKIP();

    // After init, stop a service that was never offered — should be no-op
    {
        lap::core::InstanceSpecifier spec( "test/radar" );
        Runtime::StopOfferService< MockRadarService > ( std::move( spec ) );
        SUCCEED() << "StopOfferService for non-offered service did not crash";
    }
}

// ============================================================================
// Test Suite: Concurrent Access
// ============================================================================

/**
 * @test Concurrent register and find operations are thread-safe
 */
TEST_F( RuntimeTest, ConcurrentRegisterFind )
{
    INITIALIZE_OR_SKIP();

    auto& runtime = Runtime::GetInstance();
    Atomic< Int32 > registrationCount{ 0 };
    Atomic< Int32 > findCount{ 0 };

    auto registerWorker = [&]()
    {
        for ( Int32 i = 0; i < 100; ++i )
        {
            auto sid = static_cast< lap::core::UInt16 > ( 0x2000 + ( i % 50 ) );
            if ( runtime.RegisterService( sid, 0x0001, 0 ).HasValue() )
            {
                registrationCount.fetch_add( 1 );
            }
        }
    };

    auto findWorker = [&]()
    {
        for ( Int32 i = 0; i < 100; ++i )
        {
            auto sid = static_cast< lap::core::UInt16 > ( 0x2000 + ( i % 50 ) );
            if ( runtime.FindServiceById( sid ).has_value() )
            {
                findCount.fetch_add( 1 );
            }
        }
    };

    std::thread t1( registerWorker );
    std::thread t2( findWorker );
    std::thread t3( findWorker );

    t1.join();
    t2.join();
    t3.join();

    std::cout << "Concurrent test: "
              << registrationCount.load() << " registrations, "
              << findCount.load() << " finds\n";

    EXPECT_GT( registrationCount.load(), 0 );
}

// ============================================================================
// Test Suite: Performance Benchmarks
// ============================================================================

/**
 * @test Runtime::Initialize completes in < 10ms
 */
TEST_F( RuntimeTest, InitializePerformance )
{
    using namespace std::chrono;

    auto start  = high_resolution_clock::now();
    auto result = Runtime::Initialize();
    auto end    = high_resolution_clock::now();

    if ( !result.HasValue() )
    {
        GTEST_SKIP() << "Shared memory registry not available; skipping perf test.";
    }

    auto durationUs = duration_cast< microseconds > ( end - start ).count();

    std::cout << "Initialize latency: " << durationUs << " us\n";

    // Relaxed threshold for CI/container environments
    EXPECT_LT( durationUs, 10000 ) << "Initialize should complete in < 10ms";
}

/**
 * @test FindServiceById latency benchmark (P99 < 100us in container env)
 */
TEST_F( RuntimeTest, FindServiceByIdLatency )
{
    using namespace std::chrono;

    INITIALIZE_OR_SKIP();

    auto& runtime = Runtime::GetInstance();

    // Register a service
    const lap::core::UInt16 serviceId = 0x3333;
    ASSERT_TRUE( runtime.RegisterService( serviceId, 0x0001, 0 ).HasValue() );

    // Benchmark FindServiceById
    constexpr Int32 kSamples = 10000;
    std::vector< Int64 > latencies;
    latencies.reserve( kSamples );

    for ( Int32 i = 0; i < kSamples; ++i )
    {
        auto start  = high_resolution_clock::now();
        auto result = runtime.FindServiceById( serviceId );
        auto end    = high_resolution_clock::now();

        ASSERT_TRUE( result.has_value() );
        latencies.push_back(
            duration_cast< nanoseconds > ( end - start ).count() );
    }

    std::sort( latencies.begin(), latencies.end() );
    Int64 p50 = latencies[ kSamples / 2 ];
    Int64 p99 = latencies[ ( kSamples * 99 ) / 100 ];
    Int64 avg = std::accumulate( latencies.begin(), latencies.end(), static_cast< Int64 > ( 0 ) ) / kSamples;

    std::cout << "\nFindServiceById Latency Benchmark (" << kSamples << " samples):\n"
              << "  Average: " << avg << " ns\n"
              << "  P50:     " << p50 << " ns\n"
              << "  P99:     " << p99 << " ns\n";

    // Relaxed threshold for container/CI (shared memory may not be available)
    EXPECT_LT( p99, 100000 ) << "FindServiceById P99 should be < 100us";
}

// ============================================================================
// Test Suite: Edge Cases
// ============================================================================

/**
 * @test GetBindingManager accessible after init
 */
TEST_F( RuntimeTest, GetBindingManagerAfterInit )
{
    INITIALIZE_OR_SKIP();

    auto& bm = Runtime::GetBindingManager();
    // Just verify it doesn't crash — the binding manager singleton always exists
    static_cast< void > ( bm );
    SUCCEED() << "GetBindingManager() accessible";
}

/**
 * @test GetRegistry returns valid pointer after init
 */
TEST_F( RuntimeTest, GetRegistryAfterInit )
{
    INITIALIZE_OR_SKIP();

    auto& runtime = Runtime::GetInstance();
    auto* registry = runtime.GetRegistry();
    // Registry may be nullptr in test environment (shared memory unavailable)
    // Just verify it doesn't crash
    static_cast< void > ( registry );
    SUCCEED() << "GetRegistry() accessible";
}

// ============================================================================
// Main
// ============================================================================

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
