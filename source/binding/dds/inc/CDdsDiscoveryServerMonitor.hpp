/**
 * @file        CDdsDiscoveryServerMonitor.hpp
 * @author      LightAP Development Team
 * @brief       Fast-DDS Discovery Server health monitor with automatic PDP/EDP fallback
 * @date        2026/03/01
 * @copyright   Copyright (c) 2026
 *
 * @details     Monitors the Fast-DDS Discovery Server connection health and
 *              provides automatic fallback to standard PDP/EDP when the
 *              Discovery Server is unavailable.
 *
 *              Discovery chain:
 *                1. SUPER_CLIENT → Discovery Server (centralized, < 1ms)
 *                2. If DS unreachable → SIMPLE PDP/EDP (decentralized, 5-100ms)
 *                3. Periodic reconnect attempts to Discovery Server
 *
 * @note        AUTOSAR R25-11 Compliance:
 *              - SWS_CM_11001-11014: DDS Service Discovery
 *              - EXP 7.2.1: Central Service Discovery
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/03/01  <td>1.0      <td>Aii             <td>Initial implementation
 * </table>
 */

#ifndef LAP_COM_DDS_DISCOVERY_SERVER_MONITOR_HPP
#define LAP_COM_DDS_DISCOVERY_SERVER_MONITOR_HPP

// ==================== Project-Internal Headers ====================
#include "DdsTypes.hpp"

// ==================== Third-Party Headers ====================
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantListener.hpp>
#include <fastdds/rtps/participant/ParticipantDiscoveryInfo.hpp>

// ==================== Standard Library Headers ====================
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

namespace lap
{
namespace com
{
namespace binding
{

    // ====================================================================
    // Discovery Mode Enumeration
    // ====================================================================

    /**
     * @brief Current discovery protocol mode
     */
    enum class DiscoveryMode : UInt8
    {
        kDiscoveryServer = 0,   ///< Using Discovery Server (SUPER_CLIENT)
        kSimplePdp       = 1,   ///< Using standard PDP/EDP (SIMPLE)
        kDisconnected    = 2    ///< Not connected to any discovery mechanism
    };

    // ====================================================================
    // Discovery Server Monitor Configuration
    // ====================================================================

    /**
     * @brief Configuration for Discovery Server monitoring and fallback
     */
    struct DiscoveryServerMonitorConfig
    {
        /// Discovery Server address (e.g., "tcp://192.168.1.1:42100", "udp://host:port")
        String  m_strServerAddress;

        /// Health check interval (how often to check Discovery Server liveness)
        std::chrono::milliseconds   m_healthCheckInterval { 5000 };

        /// Maximum consecutive health check failures before triggering fallback
        UInt32  m_iMaxFailuresBeforeFallback { 3 };

        /// Reconnect attempt interval when in PDP/EDP fallback mode
        std::chrono::milliseconds   m_reconnectInterval { 10000 };

        /// Maximum reconnect attempts before giving up (0 = infinite)
        UInt32  m_iMaxReconnectAttempts { 0 };

        /// Whether to automatically fallback to PDP/EDP on DS failure
        Bool    m_bEnableFallback { true };

        /// Whether to automatically attempt reconnection to DS
        Bool    m_bEnableReconnect { true };

        /// Timeout for initial Discovery Server connection
        std::chrono::milliseconds   m_connectTimeout { 3000 };
    };

    // ====================================================================
    // Discovery Server Health Statistics
    // ====================================================================

    /**
     * @brief Runtime statistics for the Discovery Server connection
     */
    struct DiscoveryServerStats
    {
        DiscoveryMode   m_eCurrentMode { DiscoveryMode::kDisconnected };

        UInt64  m_iFallbackCount { 0 };         ///< Times fell back to PDP/EDP
        UInt64  m_iReconnectCount { 0 };         ///< Times reconnected to DS
        UInt64  m_iReconnectAttempts { 0 };      ///< Total reconnect attempts
        UInt64  m_iHealthCheckSuccesses { 0 };
        UInt64  m_iHealthCheckFailures { 0 };
        UInt64  m_iConsecutiveFailures { 0 };

        std::chrono::steady_clock::time_point   m_lastHealthCheck {};
        std::chrono::steady_clock::time_point   m_lastFallbackTime {};
        std::chrono::steady_clock::time_point   m_lastReconnectTime {};
    };

    // ====================================================================
    // CDdsDiscoveryServerMonitor
    // ====================================================================

    /**
     * @brief   Monitors Fast-DDS Discovery Server health with PDP/EDP fallback
     *
     * @details This class manages the Discovery Server connection lifecycle:
     *
     *          **Startup:**
     *          1. Attempt SUPER_CLIENT connection to Discovery Server
     *          2. If fails → fallback to SIMPLE PDP/EDP
     *          3. Start health monitoring thread
     *
     *          **Runtime:**
     *          - Periodically check DS connection via participant liveness
     *          - On DS failure → signal DdsBinding to recreate participant in SIMPLE mode
     *          - On DS recovery → signal DdsBinding to recreate participant in SUPER_CLIENT mode
     *
     *          **Callbacks:**
     *          - OnFallbackToPdp:    DS connection lost, switch to PDP/EDP
     *          - OnReconnectToDs:    DS connection restored, switch to SUPER_CLIENT
     *          - OnDiscoveryModeChanged: Generic mode change notification
     *
     * @note    Thread-safe. Health monitoring runs in a background thread.
     */
    class CDdsDiscoveryServerMonitor
    {
    public:
        // ================================================================
        // Callback Types
        // ================================================================

        /**
         * @brief Called when discovery mode changes (DS → PDP or PDP → DS)
         * @param oldMode Previous discovery mode
         * @param newMode New discovery mode
         */
        using ModeChangeCallback = Function< void(
            DiscoveryMode oldMode, DiscoveryMode newMode ) >;

    public:
        explicit CDdsDiscoveryServerMonitor(
            const DiscoveryServerMonitorConfig& config ) noexcept;

        ~CDdsDiscoveryServerMonitor() noexcept;

        // Non-copyable, non-movable
        CDdsDiscoveryServerMonitor( const CDdsDiscoveryServerMonitor& )             = delete;
        CDdsDiscoveryServerMonitor& operator=( const CDdsDiscoveryServerMonitor& )  = delete;
        CDdsDiscoveryServerMonitor( CDdsDiscoveryServerMonitor&& )                  = delete;
        CDdsDiscoveryServerMonitor& operator=( CDdsDiscoveryServerMonitor&& )       = delete;

        // ================================================================
        // Lifecycle
        // ================================================================

        /**
         * @brief   Start monitoring the Discovery Server
         * @param   pParticipant    DDS participant to monitor (owned by DdsBinding)
         * @return  Initial discovery mode (kDiscoveryServer or kSimplePdp)
         *
         * @note    Must be called after DdsBinding creates its participant.
         *          Does NOT own the participant pointer.
         */
        DiscoveryMode Start(
            eprosima::fastdds::dds::DomainParticipant* pParticipant ) noexcept;

        /**
         * @brief   Stop monitoring and cleanup
         */
        void Stop() noexcept;

        // ================================================================
        // Queries
        // ================================================================

        /**
         * @brief   Get current discovery mode
         */
        DiscoveryMode GetCurrentMode() const noexcept;

        /**
         * @brief   Check if Discovery Server is currently reachable
         */
        Bool IsDiscoveryServerReachable() const noexcept;

        /**
         * @brief   Get runtime statistics
         */
        DiscoveryServerStats GetStats() const noexcept;

        /**
         * @brief   Check if Discovery Server address is configured
         */
        Bool HasDiscoveryServer() const noexcept;

        // ================================================================
        // Configuration
        // ================================================================

        /**
         * @brief   Set callback for discovery mode changes
         */
        void SetModeChangeCallback( ModeChangeCallback callback ) noexcept;

        /**
         * @brief   Get the configured Discovery Server address
         */
        const String& GetServerAddress() const noexcept;

        /**
         * @brief Parse the server address into host, port, and locator kind
         * @param[out] host     Parsed hostname/IP
         * @param[out] port     Parsed port number
         * @param[out] kind     Locator kind (LOCATOR_KIND_TCPv4 or LOCATOR_KIND_UDPv4)
         * @return true if parsing succeeded
         */
        Bool ParseServerAddress(
            String& host, UInt32& port, Int32& kind ) const noexcept;

    private:
        // ================================================================
        // Health Monitoring
        // ================================================================

        /**
         * @brief   Background health monitoring thread function
         */
        void MonitorThreadFunc() noexcept;

        /**
         * @brief   Perform a single health check on the Discovery Server
         * @return  true if Discovery Server is reachable
         */
        Bool CheckDiscoveryServerHealth() const noexcept;

        /**
         * @brief   Probe the Discovery Server via a temporary CLIENT participant
         * @return  true if Discovery Server is reachable
         */
        Bool ProbeDiscoveryServer() const noexcept;

        /**
         * @brief   Signal discovery mode change
         * @param   newMode New discovery mode
         */
        void TransitionMode( DiscoveryMode newMode ) noexcept;

    private:
        // ================================================================
        // Member Variables
        // ================================================================

        DiscoveryServerMonitorConfig    m_config;
        mutable Mutex                   m_mutex;

        /// Current discovery mode (atomic for lock-free reads)
        Atomic< UInt8 >     m_eCurrentMode { static_cast< UInt8 >( DiscoveryMode::kDisconnected ) };

        /// Monitoring state
        Atomic< Bool >      m_bRunning { false };
        std::thread         m_monitorThread;

        /// DDS participant reference (not owned)
        eprosima::fastdds::dds::DomainParticipant*  m_pParticipant { nullptr };

        /// Statistics (protected by m_mutex)
        DiscoveryServerStats    m_stats;

        /// Mode change callback
        ModeChangeCallback      m_modeChangeCallback;
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_DDS_DISCOVERY_SERVER_MONITOR_HPP
