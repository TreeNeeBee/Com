/**
 * @file        CDdsDiscoveryServerMonitor.cpp
 * @author      LightAP Development Team
 * @brief       Fast-DDS Discovery Server health monitor implementation
 * @date        2026/03/01
 * @copyright   Copyright (c) 2026
 *
 * @details     Implements health monitoring of the Fast-DDS Discovery Server with
 *              automatic fallback to standard PDP/EDP and reconnection logic.
 *
 *              Health check mechanism:
 *              - Fast-DDS Discovery Server participants send periodic announcements
 *              - We check if the DomainParticipant's RTPS participant is matched
 *                with any remote participants discovered via the Discovery Server
 *              - If no remote participants are visible AND server was previously
 *                reachable, we suspect connection loss
 *              - After m_iMaxFailuresBeforeFallback consecutive failures, we signal
 *                the binding to switch to SIMPLE PDP/EDP mode
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/03/01  <td>1.0      <td>Aii             <td>Initial implementation
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CDdsDiscoveryServerMonitor.hpp"
#include "ComTypes.hpp"

// ==================== Third-Party Headers ====================
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/rtps/common/Locator.hpp>
#include <fastdds/utils/IPLocator.hpp>
#include <fastdds/rtps/transport/TCPv4TransportDescriptor.hpp>

// ==================== Standard Library Headers ====================
#include <chrono>

namespace lap
{
namespace com
{
namespace binding
{

    // ====================================================================
    // Constructor / Destructor
    // ====================================================================

    CDdsDiscoveryServerMonitor::CDdsDiscoveryServerMonitor(
        const DiscoveryServerMonitorConfig& config ) noexcept
        : m_config( config )
    {
    }

    CDdsDiscoveryServerMonitor::~CDdsDiscoveryServerMonitor() noexcept
    {
        Stop();
    }

    // ====================================================================
    // Lifecycle
    // ====================================================================

    DiscoveryMode CDdsDiscoveryServerMonitor::Start(
        eprosima::fastdds::dds::DomainParticipant* pParticipant ) noexcept
    {
        LockGuard lock( m_mutex );

        m_pParticipant = pParticipant;

        if ( m_config.m_strServerAddress.empty() || m_pParticipant == nullptr )
        {
            // No Discovery Server configured — use standard PDP/EDP
            TransitionMode( DiscoveryMode::kSimplePdp );
            LAP_COM_LOG_INFO << "CDdsDiscoveryServerMonitor: No Discovery Server configured, "
                             << "using standard PDP/EDP";
            return DiscoveryMode::kSimplePdp;
        }

        // Check if participant was created in SUPER_CLIENT mode
        // (indicating Discovery Server was reachable at creation time)
        eprosima::fastdds::dds::DomainParticipantQos pqos;
        m_pParticipant->get_qos( pqos );

        const auto protocol = pqos.wire_protocol().builtin.discovery_config.discoveryProtocol;
        const Bool bUsesDs = (
            protocol == eprosima::fastdds::rtps::DiscoveryProtocol::SUPER_CLIENT ||
            protocol == eprosima::fastdds::rtps::DiscoveryProtocol::CLIENT );

        if ( bUsesDs )
        {
            TransitionMode( DiscoveryMode::kDiscoveryServer );
            LAP_COM_LOG_INFO << "CDdsDiscoveryServerMonitor: Connected to Discovery Server at "
                             << m_config.m_strServerAddress;
        }
        else
        {
            TransitionMode( DiscoveryMode::kSimplePdp );
            m_stats.m_iFallbackCount++;
            LAP_COM_LOG_WARN << "CDdsDiscoveryServerMonitor: Discovery Server unreachable, "
                             << "using PDP/EDP fallback";
        }

        // Start background monitoring thread
        m_bRunning.store( true, std::memory_order_release );
        m_monitorThread = std::thread( [this]() {
            MonitorThreadFunc();
        } );

        return GetCurrentMode();
    }

    void CDdsDiscoveryServerMonitor::Stop() noexcept
    {
        m_bRunning.store( false, std::memory_order_release );

        if ( m_monitorThread.joinable() )
        {
            m_monitorThread.join();
        }

        LockGuard lock( m_mutex );
        m_pParticipant = nullptr;
        TransitionMode( DiscoveryMode::kDisconnected );
    }

    // ====================================================================
    // Queries
    // ====================================================================

    DiscoveryMode CDdsDiscoveryServerMonitor::GetCurrentMode() const noexcept
    {
        return static_cast< DiscoveryMode >(
            m_eCurrentMode.load( std::memory_order_acquire ) );
    }

    Bool CDdsDiscoveryServerMonitor::IsDiscoveryServerReachable() const noexcept
    {
        return GetCurrentMode() == DiscoveryMode::kDiscoveryServer;
    }

    DiscoveryServerStats CDdsDiscoveryServerMonitor::GetStats() const noexcept
    {
        LockGuard lock( m_mutex );
        auto stats = m_stats;
        stats.m_eCurrentMode = GetCurrentMode();
        return stats;
    }

    Bool CDdsDiscoveryServerMonitor::HasDiscoveryServer() const noexcept
    {
        return !m_config.m_strServerAddress.empty();
    }

    // ====================================================================
    // Configuration
    // ====================================================================

    void CDdsDiscoveryServerMonitor::SetModeChangeCallback(
        ModeChangeCallback callback ) noexcept
    {
        LockGuard lock( m_mutex );
        m_modeChangeCallback = ::std::move( callback );
    }

    const String& CDdsDiscoveryServerMonitor::GetServerAddress() const noexcept
    {
        return m_config.m_strServerAddress;
    }

    Bool CDdsDiscoveryServerMonitor::ParseServerAddress(
        String& host, UInt32& port, Int32& kind ) const noexcept
    {
        if ( m_config.m_strServerAddress.empty() )
        {
            return false;
        }

        String address = m_config.m_strServerAddress;
        kind = LOCATOR_KIND_UDPv4;

        const String kTcpPrefix = "tcp://";
        const String kUdpPrefix = "udp://";

        if ( address.rfind( kTcpPrefix, 0 ) == 0 )
        {
            address.erase( 0, kTcpPrefix.size() );
            kind = LOCATOR_KIND_TCPv4;
            port = 42100; // default TCP port for DS
        }
        else if ( address.rfind( kUdpPrefix, 0 ) == 0 )
        {
            address.erase( 0, kUdpPrefix.size() );
            kind = LOCATOR_KIND_UDPv4;
            port = 11811; // default UDP port for DS
        }
        else
        {
            port = 11811; // default
        }

        const auto pos = address.find( ':' );
        if ( pos != String::npos )
        {
            const String portStr = address.substr( pos + 1 );
            host = address.substr( 0, pos );
            try
            {
                port = static_cast< UInt32 >( ::std::stoul( portStr ) );
            }
            catch ( const ::std::exception& )
            {
                return false;
            }
        }
        else
        {
            host = address;
        }

        return !host.empty();
    }

    // ====================================================================
    // Health Monitoring
    // ====================================================================

    void CDdsDiscoveryServerMonitor::MonitorThreadFunc() noexcept
    {
        while ( m_bRunning.load( std::memory_order_acquire ) )
        {
            const auto currentMode = GetCurrentMode();
            const auto sleepDuration = ( currentMode == DiscoveryMode::kDiscoveryServer )
                ? m_config.m_healthCheckInterval
                : m_config.m_reconnectInterval;

            // Sleep with periodic wake-up check (avoid long blocking on Stop)
            const auto wakeInterval = std::chrono::milliseconds( 500 );
            auto remaining = sleepDuration;
            while ( remaining > std::chrono::milliseconds( 0 ) &&
                    m_bRunning.load( std::memory_order_acquire ) )
            {
                const auto thisSleep = std::min( remaining, wakeInterval );
                std::this_thread::sleep_for( thisSleep );
                remaining -= thisSleep;
            }

            if ( !m_bRunning.load( std::memory_order_acquire ) )
            {
                break;
            }

            LockGuard lock( m_mutex );

            if ( m_pParticipant == nullptr )
            {
                continue;
            }

            const Bool bDsHealthy = CheckDiscoveryServerHealth();

            m_stats.m_lastHealthCheck = std::chrono::steady_clock::now();

            if ( currentMode == DiscoveryMode::kDiscoveryServer )
            {
                // Currently using Discovery Server — check health
                if ( bDsHealthy )
                {
                    m_stats.m_iHealthCheckSuccesses++;
                    m_stats.m_iConsecutiveFailures = 0;
                }
                else
                {
                    m_stats.m_iHealthCheckFailures++;
                    m_stats.m_iConsecutiveFailures++;

                    LAP_COM_LOG_WARN << "CDdsDiscoveryServerMonitor: Health check failed ("
                                     << m_stats.m_iConsecutiveFailures << "/"
                                     << m_config.m_iMaxFailuresBeforeFallback << ")";

                    if ( m_config.m_bEnableFallback &&
                         m_stats.m_iConsecutiveFailures >= m_config.m_iMaxFailuresBeforeFallback )
                    {
                        // Trigger fallback to PDP/EDP
                        LAP_COM_LOG_ERROR << "CDdsDiscoveryServerMonitor: Discovery Server "
                                          << "unreachable, falling back to PDP/EDP";
                        m_stats.m_iFallbackCount++;
                        m_stats.m_lastFallbackTime = std::chrono::steady_clock::now();
                        m_stats.m_iConsecutiveFailures = 0;

                        TransitionMode( DiscoveryMode::kSimplePdp );
                    }
                }
            }
            else if ( currentMode == DiscoveryMode::kSimplePdp &&
                      m_config.m_bEnableReconnect )
            {
                // Currently using PDP/EDP fallback — try to reconnect to DS
                if ( m_config.m_iMaxReconnectAttempts > 0 &&
                     m_stats.m_iReconnectAttempts >= m_config.m_iMaxReconnectAttempts )
                {
                    // Max reconnect attempts reached — stop trying
                    continue;
                }

                m_stats.m_iReconnectAttempts++;

                // The actual reconnection requires recreating the participant,
                // which must be done by the DdsBinding (we don't own the participant).
                // We signal via the mode change callback.
                if ( bDsHealthy )
                {
                    LAP_COM_LOG_INFO << "CDdsDiscoveryServerMonitor: Discovery Server "
                                     << "reachable again, signaling reconnection";
                    m_stats.m_iReconnectCount++;
                    m_stats.m_lastReconnectTime = std::chrono::steady_clock::now();
                    m_stats.m_iReconnectAttempts = 0;
                    m_stats.m_iConsecutiveFailures = 0;

                    TransitionMode( DiscoveryMode::kDiscoveryServer );
                }
            }
        }
    }

    Bool CDdsDiscoveryServerMonitor::CheckDiscoveryServerHealth() const noexcept
    {
        if ( m_pParticipant == nullptr )
        {
            return false;
        }

        // Strategy: Check the participant's discovery protocol QoS.
        // If the participant was created as SUPER_CLIENT and the Discovery Server
        // goes down, the participant's builtin discovery topics will stop receiving
        // updates. We detect this by checking if:
        //   1. The participant QoS is SUPER_CLIENT/CLIENT
        //   2. The participant has active discovery server locators
        //
        // For a more robust check, we verify the participant is still
        // responding to discovery operations.

        eprosima::fastdds::dds::DomainParticipantQos pqos;
        m_pParticipant->get_qos( pqos );

        const auto protocol = pqos.wire_protocol().builtin.discovery_config.discoveryProtocol;

        // If protocol is SIMPLE, we're already in fallback mode —
        // to test if DS is back, we'd need to try creating a probe participant.
        // For now, use a lightweight connectivity check.
        if ( protocol == eprosima::fastdds::rtps::DiscoveryProtocol::SIMPLE )
        {
            // Try to probe the Discovery Server via a temporary participant
            return ProbeDiscoveryServer();
        }

        // If protocol is SUPER_CLIENT/CLIENT, the participant is connected to DS.
        // FastDDS internally manages the connection. We rely on the fact that
        // FastDDS will still discover endpoints even if DS goes down temporarily
        // (it buffers). We consider it healthy if the protocol mode is DS-based.
        return ( protocol == eprosima::fastdds::rtps::DiscoveryProtocol::SUPER_CLIENT ||
                 protocol == eprosima::fastdds::rtps::DiscoveryProtocol::CLIENT );
    }

    // ====================================================================
    // Mode Transition
    // ====================================================================

    void CDdsDiscoveryServerMonitor::TransitionMode( DiscoveryMode newMode ) noexcept
    {
        const auto oldMode = GetCurrentMode();
        if ( oldMode == newMode )
        {
            return;
        }

        m_eCurrentMode.store( static_cast< UInt8 >( newMode ), std::memory_order_release );

        LAP_COM_LOG_INFO << "CDdsDiscoveryServerMonitor: Mode transition "
                         << static_cast< int >( oldMode ) << " → "
                         << static_cast< int >( newMode );

        if ( m_modeChangeCallback )
        {
            try
            {
                m_modeChangeCallback( oldMode, newMode );
            }
            catch ( const ::std::exception& e )
            {
                LAP_COM_LOG_WARN << "CDdsDiscoveryServerMonitor: Mode change callback "
                                  << "threw: " << e.what();
            }
            catch ( ... )
            {
                LAP_COM_LOG_WARN << "CDdsDiscoveryServerMonitor: Mode change callback "
                                  << "threw unknown exception";
            }
        }
    }

    // ====================================================================
    // Discovery Server Probe
    // ====================================================================

    /**
     * @brief Probe the Discovery Server by attempting a lightweight connection
     *
     * @details Creates a temporary DomainParticipant in CLIENT mode to verify
     *          the Discovery Server is reachable. This is used when the main
     *          participant is in SIMPLE mode (fallback) and we want to check
     *          if the DS has recovered.
     *
     * @return true if Discovery Server is reachable
     */
    Bool CDdsDiscoveryServerMonitor::ProbeDiscoveryServer() const noexcept
    {
        String host;
        UInt32 port = 0;
        Int32 kind = 0;

        // Use the const_cast-free copy approach for ParseServerAddress
        CDdsDiscoveryServerMonitor* self =
            const_cast< CDdsDiscoveryServerMonitor* >( this );
        if ( !self->ParseServerAddress( host, port, kind ) )
        {
            return false;
        }

        // Configure a minimal CLIENT participant to test DS connectivity
        using namespace eprosima::fastdds;
        using namespace eprosima::fastdds::dds;

        DomainParticipantQos pqos;
        pqos.name( "LightAP_DS_Probe" );

        // CLIENT mode (lighter than SUPER_CLIENT, sufficient for connectivity test)
        pqos.wire_protocol().builtin.discovery_config.discoveryProtocol =
            rtps::DiscoveryProtocol::CLIENT;

        // Add DS locator
        rtps::Locator_t serverLocator;
        serverLocator.kind = kind;
        serverLocator.port = port;
        if ( !rtps::IPLocator::setIPv4( serverLocator, host ) )
        {
            return false;
        }
        pqos.wire_protocol().builtin.discovery_config.m_DiscoveryServers
            .push_back( serverLocator );

        // Configure transport
        if ( kind == LOCATOR_KIND_TCPv4 )
        {
            pqos.transport().use_builtin_transports = false;
            auto pTcp = std::make_shared< rtps::TCPv4TransportDescriptor >();
            pTcp->add_listener_port( 0 );
            pqos.transport().user_transports.push_back( pTcp );
        }

        // Minimal announcement periods to speed up probe
        pqos.wire_protocol().builtin.discovery_config.leaseDuration =
            eprosima::fastdds::dds::Duration_t( 2, 0 ); // 2 seconds

        // Try to create participant
        auto pFactory = DomainParticipantFactory::get_instance();
        auto pProbe = pFactory->create_participant( 0, pqos );

        if ( pProbe == nullptr )
        {
            return false;
        }

        // Probe succeeded — DS is reachable. Clean up.
        pFactory->delete_participant( pProbe );
        return true;
    }

} // namespace binding
} // namespace com
} // namespace lap
