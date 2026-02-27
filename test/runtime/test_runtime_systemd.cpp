/**
 * @file        test_runtime_systemd.cpp
 * @author      LightAP Development Team
 * @brief       Test Runtime systemd socket activation integration
 * @date        2026/02/08
 * @details     Validates Runtime behaviour with systemd socket-activated
 *              QM/ASIL registry endpoints using R25-11 API
 *              (Runtime::GetInstance().RegisterService/FindServiceById/UnregisterService).
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 *
 * Test scenarios:
 * 1. Initialize Runtime (systemd sockets provide shared-memory FDs)
 * 2. Register QM service via Runtime::GetInstance().RegisterService()
 * 3. Register ASIL service via Runtime::GetInstance().RegisterService()
 * 4. Find QM service via Runtime::GetInstance().FindServiceById()
 * 5. Find ASIL service via Runtime::GetInstance().FindServiceById()
 * 6. Verify physical isolation (different service ID ranges)
 * 7. Unregister services
 * 8. Deinitialize Runtime
 *
 * Prerequisites:
 *   sudo systemctl start lap-registry-qm.socket
 *   sudo systemctl start lap-registry-asil.socket
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/21  <td>1.0      <td>LightAP Team    <td>Initial version (free-function API)
 * <tr><td>2026/02/08  <td>2.0      <td>Aii             <td>Rewrite for R25-11 member-function API
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "Runtime.hpp"
#include "ServiceSlot.hpp"
#include "ComTypes.hpp"

// ==================== Third-Party Headers ====================
#include <gtest/gtest.h>

// ==================== Standard Library Headers ====================
#include <iostream>
#include <iomanip>

using namespace lap::com;

// ============================================================================
// Test Fixture
// ============================================================================

/**
 * @brief Test fixture for systemd socket-activated runtime tests
 * @details Ensures clean Runtime state before/after each test.
 *          All tests are GTEST_SKIP-safe when systemd sockets are absent.
 */
class RuntimeSystemdTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if ( Runtime::IsInitialized() )
        {
            Runtime::Deinitialize();
        }
    }

    void TearDown() override
    {
        if ( Runtime::IsInitialized() )
        {
            Runtime::Deinitialize();
        }
    }

    /**
     * @brief Try to initialize Runtime; return false if infra unavailable.
     */
    Bool TryInitializeOrSkip()
    {
        auto result = Runtime::Initialize();
        return result.HasValue();
    }
};

/**
 * @brief Macro: skip the test if shared-memory registry is not available.
 */
#define SYSTEMD_INITIALIZE_OR_SKIP()                                             \
    do {                                                                          \
        if ( !TryInitializeOrSkip() )                                             \
        {                                                                         \
            GTEST_SKIP() << "Shared memory registry not available (no systemd "   \
                            "sockets). Skipping systemd integration test.";       \
        }                                                                         \
    } while ( 0 )

// ============================================================================
// Test Suite: Systemd Socket Activation (SWS_CM_00400)
// ============================================================================

/**
 * @test Initialize Runtime from systemd sockets
 */
TEST_F( RuntimeSystemdTest, InitializeFromSystemdSockets )
{
    SYSTEMD_INITIALIZE_OR_SKIP();

    EXPECT_TRUE( Runtime::IsInitialized() );
    std::cout << "Runtime initialized (systemd socket activation path)\n";
}

/**
 * @test Register QM-domain service (service ID range 0x0001–0x03FF)
 */
TEST_F( RuntimeSystemdTest, RegisterQmService )
{
    SYSTEMD_INITIALIZE_OR_SKIP();

    auto& runtime = Runtime::GetInstance();
    const lap::core::UInt16 serviceId  = 0x0001;  // QM range
    const lap::core::UInt16 instanceId = 0x1234;

    auto result = runtime.RegisterService( serviceId, instanceId, 0 );
    ASSERT_TRUE( result.HasValue() ) << "QM service registration should succeed";

    // Verify findable
    auto found = runtime.FindServiceById( serviceId );
    ASSERT_TRUE( found.has_value() ) << "QM service should be found after registration";

    const auto& slot = found.value();
    EXPECT_EQ( static_cast< lap::core::UInt16 > ( slot.m_serviceId ), serviceId );
    EXPECT_EQ( static_cast< lap::core::UInt16 > ( slot.m_instanceId ), instanceId );

    std::cout << "QM service registered: serviceId=0x"
              << std::hex << slot.m_serviceId
              << " instanceId=0x" << slot.m_instanceId
              << std::dec << "\n";
}

/**
 * @test Register ASIL-domain service (service ID range 0xF001–0xF3FF)
 */
TEST_F( RuntimeSystemdTest, RegisterAsilService )
{
    SYSTEMD_INITIALIZE_OR_SKIP();

    auto& runtime = Runtime::GetInstance();
    const lap::core::UInt16 serviceId  = 0xF002;  // ASIL range
    const lap::core::UInt16 instanceId = 0x5678;

    auto result = runtime.RegisterService( serviceId, instanceId, 0 );
    ASSERT_TRUE( result.HasValue() ) << "ASIL service registration should succeed";

    auto found = runtime.FindServiceById( serviceId );
    ASSERT_TRUE( found.has_value() ) << "ASIL service should be found after registration";

    const auto& slot = found.value();
    EXPECT_EQ( static_cast< lap::core::UInt16 > ( slot.m_serviceId ), serviceId );
    EXPECT_EQ( static_cast< lap::core::UInt16 > ( slot.m_instanceId ), instanceId );

    std::cout << "ASIL service registered: serviceId=0x"
              << std::hex << slot.m_serviceId
              << " instanceId=0x" << slot.m_instanceId
              << std::dec << "\n";
}

/**
 * @test QM and ASIL services coexist in registry
 */
TEST_F( RuntimeSystemdTest, QmAndAsilCoexistence )
{
    SYSTEMD_INITIALIZE_OR_SKIP();

    auto& runtime = Runtime::GetInstance();

    // Register QM
    const lap::core::UInt16 qmServiceId   = 0x0001;
    const lap::core::UInt16 qmInstanceId  = 0x1234;
    ASSERT_TRUE( runtime.RegisterService( qmServiceId, qmInstanceId, 0 ).HasValue() );

    // Register ASIL
    const lap::core::UInt16 asilServiceId  = 0xF002;
    const lap::core::UInt16 asilInstanceId = 0x5678;
    ASSERT_TRUE( runtime.RegisterService( asilServiceId, asilInstanceId, 0 ).HasValue() );

    // Both should be findable
    auto qmResult = runtime.FindServiceById( qmServiceId );
    ASSERT_TRUE( qmResult.has_value() ) << "QM service should exist";

    auto asilResult = runtime.FindServiceById( asilServiceId );
    ASSERT_TRUE( asilResult.has_value() ) << "ASIL service should exist";

    // Verify isolation: different service ID ranges
    const auto& qmSlot   = qmResult.value();
    const auto& asilSlot = asilResult.value();
    EXPECT_NE( qmSlot.m_serviceId, asilSlot.m_serviceId )
        << "QM and ASIL services must have different service IDs";

    // QM: 0x0001–0x03FF, ASIL: 0xF001–0xF3FF
    EXPECT_LT( qmSlot.m_serviceId, 0x0400u )   << "QM service ID out of range";
    EXPECT_GE( asilSlot.m_serviceId, 0xF000u )  << "ASIL service ID out of range";

    std::cout << "QM/ASIL coexistence verified (isolation by service ID range)\n";
}

/**
 * @test Unregister QM service then verify ASIL remains
 */
TEST_F( RuntimeSystemdTest, UnregisterQmDoesNotAffectAsil )
{
    SYSTEMD_INITIALIZE_OR_SKIP();

    auto& runtime = Runtime::GetInstance();

    // Register both
    ASSERT_TRUE( runtime.RegisterService( 0x0001, 0x1234, 0 ).HasValue() );
    ASSERT_TRUE( runtime.RegisterService( 0xF002, 0x5678, 0 ).HasValue() );

    // Unregister QM
    auto unregResult = runtime.UnregisterService( 0x0001 );
    ASSERT_TRUE( unregResult.HasValue() );

    // QM gone
    EXPECT_FALSE( runtime.FindServiceById( 0x0001 ).has_value() )
        << "QM service should be unregistered";

    // ASIL still present
    EXPECT_TRUE( runtime.FindServiceById( 0xF002 ).has_value() )
        << "ASIL service should not be affected by QM unregistration";
}

/**
 * @test Full lifecycle: init → register QM+ASIL → find → unregister → deinit
 */
TEST_F( RuntimeSystemdTest, FullLifecycle )
{
    SYSTEMD_INITIALIZE_OR_SKIP();

    auto& runtime = Runtime::GetInstance();

    // Register
    ASSERT_TRUE( runtime.RegisterService( 0x0001, 0x1234, 0 ).HasValue() );
    ASSERT_TRUE( runtime.RegisterService( 0xF002, 0x5678, 0 ).HasValue() );

    // Find
    ASSERT_TRUE( runtime.FindServiceById( 0x0001 ).has_value() );
    ASSERT_TRUE( runtime.FindServiceById( 0xF002 ).has_value() );

    // Unregister
    ASSERT_TRUE( runtime.UnregisterService( 0x0001 ).HasValue() );
    ASSERT_TRUE( runtime.UnregisterService( 0xF002 ).HasValue() );

    // Verify gone
    EXPECT_FALSE( runtime.FindServiceById( 0x0001 ).has_value() );
    EXPECT_FALSE( runtime.FindServiceById( 0xF002 ).has_value() );

    // Deinit
    auto deinit = Runtime::Deinitialize();
    ASSERT_TRUE( deinit.HasValue() );
    EXPECT_FALSE( Runtime::IsInitialized() );

    std::cout << "Full systemd lifecycle completed successfully\n";
}

/**
 * @test Deinitialize and reinitialize cleanly
 */
TEST_F( RuntimeSystemdTest, ReinitializeAfterDeinitialize )
{
    SYSTEMD_INITIALIZE_OR_SKIP();

    // Deinit
    ASSERT_TRUE( Runtime::Deinitialize().HasValue() );
    EXPECT_FALSE( Runtime::IsInitialized() );

    // Reinit
    auto reinit = Runtime::Initialize();
    ASSERT_TRUE( reinit.HasValue() ) << "Reinitialization should succeed";
    EXPECT_TRUE( Runtime::IsInitialized() );

    // Registry should be usable again
    auto& runtime = Runtime::GetInstance();
    ASSERT_TRUE( runtime.RegisterService( 0x0010, 0x0001, 0 ).HasValue() );
    EXPECT_TRUE( runtime.FindServiceById( 0x0010 ).has_value() );
}

// ============================================================================
// Main
// ============================================================================

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
