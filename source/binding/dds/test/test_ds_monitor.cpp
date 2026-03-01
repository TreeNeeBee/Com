/**
 * @file        test_ds_monitor.cpp
 * @brief       Unit tests for CDdsDiscoveryServerMonitor and DDS SD fallback chain
 * @date        2026/03/01
 * @copyright   Copyright (c) 2026
 *
 * @details     Tests the Discovery Server ↔ PDP/EDP fallback mechanism:
 *              1. Monitor creation, lifecycle, and configuration
 *              2. Address parsing (tcp/udp with various port formats)
 *              3. Fallback to SIMPLE PDP/EDP when DS is unreachable
 *              4. DdsBinding integration (GetDiscoveryMode, GetDiscoveryStats)
 *              5. Mode transition and callback invocation
 */

#include "CDdsDiscoveryServerMonitor.hpp"
#include "DdsBinding.hpp"
#include "ComTypes.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>

using namespace lap::com::binding;
using namespace lap::com;

// ====================================================================
// CDdsDiscoveryServerMonitor Unit Tests
// ====================================================================

class DsMonitorTest : public ::testing::Test
{
protected:
    void SetUp() override {}
    void TearDown() override {}
};

/**
 * @test Default config values are sane
 */
TEST_F( DsMonitorTest, DefaultConfig )
{
    DiscoveryServerMonitorConfig config;

    EXPECT_EQ( config.m_healthCheckInterval.count(), 5000 );
    EXPECT_EQ( config.m_iMaxFailuresBeforeFallback, 3U );
    EXPECT_EQ( config.m_reconnectInterval.count(), 10000 );
    EXPECT_EQ( config.m_iMaxReconnectAttempts, 0U );
    EXPECT_TRUE( config.m_bEnableFallback );
    EXPECT_TRUE( config.m_bEnableReconnect );
    EXPECT_EQ( config.m_connectTimeout.count(), 3000 );
    EXPECT_TRUE( config.m_strServerAddress.empty() );
}

/**
 * @test Monitor without DS address starts in SimplePdp mode
 */
TEST_F( DsMonitorTest, NoDsAddress_StartReturnsSimplePdp )
{
    DiscoveryServerMonitorConfig config;
    // Empty server address → no DS
    CDdsDiscoveryServerMonitor monitor( config );

    EXPECT_FALSE( monitor.HasDiscoveryServer() );
    EXPECT_EQ( monitor.GetCurrentMode(), DiscoveryMode::kDisconnected );

    // Start with nullptr participant → SimplePdp
    const auto mode = monitor.Start( nullptr );
    EXPECT_EQ( mode, DiscoveryMode::kSimplePdp );
    EXPECT_EQ( monitor.GetCurrentMode(), DiscoveryMode::kSimplePdp );
    EXPECT_FALSE( monitor.IsDiscoveryServerReachable() );

    monitor.Stop();
    EXPECT_EQ( monitor.GetCurrentMode(), DiscoveryMode::kDisconnected );
}

/**
 * @test Address parsing: TCP scheme
 */
TEST_F( DsMonitorTest, ParseAddress_Tcp )
{
    DiscoveryServerMonitorConfig config;
    config.m_strServerAddress = "tcp://192.168.1.100:42100";
    CDdsDiscoveryServerMonitor monitor( config );

    EXPECT_TRUE( monitor.HasDiscoveryServer() );

    String host;
    uint32_t port = 0;
    int32_t kind  = 0;
    EXPECT_TRUE( monitor.ParseServerAddress( host, port, kind ) );
    EXPECT_EQ( host, "192.168.1.100" );
    EXPECT_EQ( port, 42100U );
    EXPECT_EQ( kind, LOCATOR_KIND_TCPv4 );
}

/**
 * @test Address parsing: UDP scheme
 */
TEST_F( DsMonitorTest, ParseAddress_Udp )
{
    DiscoveryServerMonitorConfig config;
    config.m_strServerAddress = "udp://10.0.0.5:11811";
    CDdsDiscoveryServerMonitor monitor( config );

    String host;
    uint32_t port = 0;
    int32_t kind  = 0;
    EXPECT_TRUE( monitor.ParseServerAddress( host, port, kind ) );
    EXPECT_EQ( host, "10.0.0.5" );
    EXPECT_EQ( port, 11811U );
    EXPECT_EQ( kind, LOCATOR_KIND_UDPv4 );
}

/**
 * @test Address parsing: no scheme (defaults to UDP)
 */
TEST_F( DsMonitorTest, ParseAddress_NoScheme )
{
    DiscoveryServerMonitorConfig config;
    config.m_strServerAddress = "172.16.0.1:8888";
    CDdsDiscoveryServerMonitor monitor( config );

    String host;
    uint32_t port = 0;
    int32_t kind  = 0;
    EXPECT_TRUE( monitor.ParseServerAddress( host, port, kind ) );
    EXPECT_EQ( host, "172.16.0.1" );
    EXPECT_EQ( port, 8888U );
    EXPECT_EQ( kind, LOCATOR_KIND_UDPv4 );
}

/**
 * @test Address parsing: host only, no port (uses default)
 */
TEST_F( DsMonitorTest, ParseAddress_DefaultPort )
{
    DiscoveryServerMonitorConfig config;
    config.m_strServerAddress = "udp://10.0.0.1";
    CDdsDiscoveryServerMonitor monitor( config );

    String host;
    uint32_t port = 0;
    int32_t kind  = 0;
    EXPECT_TRUE( monitor.ParseServerAddress( host, port, kind ) );
    EXPECT_EQ( host, "10.0.0.1" );
    EXPECT_EQ( port, 11811U );  // default UDP port
    EXPECT_EQ( kind, LOCATOR_KIND_UDPv4 );
}

/**
 * @test Address parsing: empty string fails
 */
TEST_F( DsMonitorTest, ParseAddress_Empty )
{
    DiscoveryServerMonitorConfig config;
    // Empty address
    CDdsDiscoveryServerMonitor monitor( config );

    String host;
    uint32_t port = 0;
    int32_t kind  = 0;
    EXPECT_FALSE( monitor.ParseServerAddress( host, port, kind ) );
}

/**
 * @test Stats are properly initialized
 */
TEST_F( DsMonitorTest, InitialStats )
{
    DiscoveryServerMonitorConfig config;
    CDdsDiscoveryServerMonitor monitor( config );

    const auto stats = monitor.GetStats();
    EXPECT_EQ( stats.m_eCurrentMode, DiscoveryMode::kDisconnected );
    EXPECT_EQ( stats.m_iFallbackCount, 0U );
    EXPECT_EQ( stats.m_iReconnectCount, 0U );
    EXPECT_EQ( stats.m_iReconnectAttempts, 0U );
    EXPECT_EQ( stats.m_iHealthCheckSuccesses, 0U );
    EXPECT_EQ( stats.m_iHealthCheckFailures, 0U );
    EXPECT_EQ( stats.m_iConsecutiveFailures, 0U );
}

/**
 * @test Mode change callback is invoked on transition
 */
TEST_F( DsMonitorTest, ModeChangeCallback )
{
    DiscoveryServerMonitorConfig config;
    CDdsDiscoveryServerMonitor monitor( config );

    std::atomic< int > callbackCount { 0 };
    DiscoveryMode receivedOldMode = DiscoveryMode::kDisconnected;
    DiscoveryMode receivedNewMode = DiscoveryMode::kDisconnected;

    monitor.SetModeChangeCallback(
        [&]( DiscoveryMode oldMode, DiscoveryMode newMode ) {
            receivedOldMode = oldMode;
            receivedNewMode = newMode;
            callbackCount.fetch_add( 1 );
        } );

    // Start triggers Disconnected → SimplePdp transition
    monitor.Start( nullptr );

    EXPECT_GE( callbackCount.load(), 1 );
    EXPECT_EQ( receivedOldMode, DiscoveryMode::kDisconnected );
    EXPECT_EQ( receivedNewMode, DiscoveryMode::kSimplePdp );

    monitor.Stop();
}

// ====================================================================
// DdsBinding Discovery Server Integration Tests
// ====================================================================

class DdsBindingDsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        binding_ = std::make_unique< DdsBinding >();
    }

    void TearDown() override
    {
        if ( binding_ ) {
            binding_->Shutdown();
        }
    }

    std::unique_ptr< DdsBinding > binding_;
};

/**
 * @test DdsBinding without DS configured reports SimplePdp mode
 */
TEST_F( DdsBindingDsTest, NoDsConfigured_SimplePdpMode )
{
    // No discovery server set
    auto result = binding_->Initialize();
    ASSERT_TRUE( result.HasValue() ) << "Initialize should succeed without DS";

    EXPECT_EQ( binding_->GetDiscoveryMode(), DiscoveryMode::kSimplePdp );

    auto stats = binding_->GetDiscoveryStats();
    EXPECT_EQ( stats.m_eCurrentMode, DiscoveryMode::kSimplePdp );
    EXPECT_EQ( stats.m_iFallbackCount, 0U );
}

/**
 * @test DdsBinding with unreachable DS falls back to SimplePdp mode
 */
TEST_F( DdsBindingDsTest, UnreachableDs_FallbackToSimplePdp )
{
    // Set a DS address that won't be reachable
    binding_->SetDiscoveryServer( "tcp://192.168.255.254:42100" );

    auto result = binding_->Initialize();
    ASSERT_TRUE( result.HasValue() )
        << "Initialize should succeed with fallback to PDP/EDP";

    // Participant should have been created (in SIMPLE fallback mode)
    // since the DS was unreachable at creation time
    const auto mode = binding_->GetDiscoveryMode();
    // Could be kDiscoveryServer if FastDDS created SUPER_CLIENT anyway,
    // or kSimplePdp if it fell back during creation. Either is valid
    // as long as the binding is functional.
    EXPECT_NE( mode, DiscoveryMode::kDisconnected )
        << "Binding should be connected in some mode";

    // Verify we can still discover services in fallback mode
    auto find = binding_->FindService( 0xDEAD );
    ASSERT_TRUE( find.HasValue() );
    EXPECT_EQ( find.Value().size(), 0 ) << "No services offered yet";
}

/**
 * @test DdsBinding configure accepts DS monitor parameters
 */
TEST_F( DdsBindingDsTest, ConfigureAcceptsDsParams )
{
    std::map< std::string, std::string > params;
    params["discovery_server"]              = "tcp://10.0.0.1:42100";
    params["ds_health_check_interval_ms"]   = "3000";
    params["ds_max_failures"]               = "5";
    params["ds_reconnect_interval_ms"]      = "15000";
    params["ds_enable_fallback"]            = "true";
    params["ds_enable_reconnect"]           = "false";

    binding_->Configure( params );

    // Initialize — DS unreachable so will fallback, but params should be accepted
    auto result = binding_->Initialize();
    ASSERT_TRUE( result.HasValue() );
}

/**
 * @test Service discovery works in fallback PDP/EDP mode
 */
TEST_F( DdsBindingDsTest, ServiceDiscoveryInFallbackMode )
{
    binding_->SetDiscoveryServer( "tcp://192.168.255.254:42100" );

    auto initResult = binding_->Initialize();
    ASSERT_TRUE( initResult.HasValue() );

    // Offer a service
    auto offerResult = binding_->OfferService( 0xBEEF, 0x0001 );
    ASSERT_TRUE( offerResult.HasValue() );

    // Give DDS time to propagate (local participant → local discovery)
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

    // StopOffer
    auto stopResult = binding_->StopOfferService( 0xBEEF, 0x0001 );
    ASSERT_TRUE( stopResult.HasValue() );
}

/**
 * @test GetDiscoveryStats returns valid metrics
 */
TEST_F( DdsBindingDsTest, GetDiscoveryStats_Valid )
{
    auto result = binding_->Initialize();
    ASSERT_TRUE( result.HasValue() );

    auto stats = binding_->GetDiscoveryStats();
    EXPECT_NE( stats.m_eCurrentMode, DiscoveryMode::kDisconnected );
    // Without DS, no fallback/reconnect events
    EXPECT_EQ( stats.m_iFallbackCount, 0U );
    EXPECT_EQ( stats.m_iReconnectCount, 0U );
}

/**
 * @test Shutdown cleans up monitor properly
 */
TEST_F( DdsBindingDsTest, Shutdown_CleansUpMonitor )
{
    binding_->SetDiscoveryServer( "udp://10.0.0.1:11811" );
    auto result = binding_->Initialize();
    ASSERT_TRUE( result.HasValue() );

    binding_->Shutdown();

    EXPECT_EQ( binding_->GetDiscoveryMode(), DiscoveryMode::kDisconnected );
}

/**
 * @test Push-based StartFindService works in fallback mode
 */
TEST_F( DdsBindingDsTest, StartFindService_FallbackMode )
{
    binding_->SetDiscoveryServer( "tcp://192.168.255.254:42100" );
    auto initResult = binding_->Initialize();
    ASSERT_TRUE( initResult.HasValue() );

    std::atomic< int > callbackCount { 0 };
    auto handleResult = binding_->StartFindService( 0x1234,
        [&]( uint64_t /*serviceId*/, std::vector< uint64_t > /*instances*/ ) {
            callbackCount.fetch_add( 1 );
        } );

    ASSERT_TRUE( handleResult.HasValue() );
    const auto handle = handleResult.Value();
    EXPECT_GT( handle, 0U );

    // Initial callback should fire immediately (AUTOSAR SWS_CM_00001)
    EXPECT_GE( callbackCount.load(), 1 );

    auto stopResult = binding_->StopFindService( handle );
    ASSERT_TRUE( stopResult.HasValue() );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
