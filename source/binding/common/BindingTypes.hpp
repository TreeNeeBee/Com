/**
 * @file        BindingTypes.hpp
 * @author      LightAP Development Team
 * @brief       Common types for transport bindings
 * @date        2025-11-21
 * @details     Defines shared data structures and enumerations for all binding implementations
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R24-11 Compliance:
 *              - SWS_CM_00400: Transport Binding Types
 * @reference   ARCHITECTURE_SUMMARY.md §7 Binding Manager
 * sdk:
 * platform:    Linux 5.10+
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/21  <td>1.0      <td>LightAP Team    <td>Initial binding types definition
 * </table>
 */
#ifndef LAP_COM_BINDING_TYPES_HPP
#define LAP_COM_BINDING_TYPES_HPP

#include <cstdint>
#include <string>

namespace lap
{
namespace com
{
namespace binding
{
    /**
     * @brief Binding health status
     * @note Used by BindingManager for fault detection and automatic failover
     */
    struct BindingHealth
    {
        bool is_healthy;                ///< Overall health status
        uint32_t error_count;           ///< Total errors since initialization
        uint32_t consecutive_errors;    ///< Consecutive errors (triggers failover)
        double availability_percent;    ///< Uptime percentage (0.0-100.0)
        uint64_t last_error_timestamp;  ///< Last error time (nanoseconds since epoch)
        std::string last_error_message; ///< Human-readable error description
        
        // Health thresholds
        static constexpr uint32_t MAX_CONSECUTIVE_ERRORS = 10;
        static constexpr double MIN_AVAILABILITY_PERCENT = 95.0;
        
        BindingHealth()
            : is_healthy(true),
              error_count(0),
              consecutive_errors(0),
              availability_percent(100.0),
              last_error_timestamp(0),
              last_error_message("OK") {}
    };

    /**
     * @brief Transport performance metrics
     * @note Used by ITransportBinding::GetMetrics() for monitoring
     */
    struct TransportMetrics
    {
        // Message statistics
        uint64_t messages_sent;         ///< Total messages sent
        uint64_t messages_received;     ///< Total messages received
        uint64_t messages_dropped;      ///< Messages dropped due to errors
        
        // Performance metrics
        uint64_t avg_latency_ns;        ///< Average message latency (nanoseconds)
        uint64_t max_latency_ns;        ///< Maximum observed latency
        uint64_t min_latency_ns;        ///< Minimum observed latency
        
        // Throughput
        uint64_t bytes_sent;            ///< Total bytes transmitted
        uint64_t bytes_received;        ///< Total bytes received
        uint64_t current_bandwidth_bps; ///< Current bandwidth (bytes/sec)
        
        // Connection state
        uint32_t active_connections;    ///< Number of active connections
        uint32_t failed_connections;    ///< Number of failed connection attempts
        
        // Error counters
        uint32_t serialization_errors;  ///< Serialization/deserialization errors
        uint32_t timeout_errors;        ///< Operation timeout errors
        
        TransportMetrics()
            : messages_sent(0),
              messages_received(0),
              messages_dropped(0),
              avg_latency_ns(0),
              max_latency_ns(0),
              min_latency_ns(UINT64_MAX),
              bytes_sent(0),
              bytes_received(0),
              current_bandwidth_bps(0),
              active_connections(0),
              failed_connections(0),
              serialization_errors(0),
              timeout_errors(0) {}
    };

    /**
     * @brief Binding capability flags
     */
    enum class BindingCapability : uint32_t
    {
        ZERO_COPY      = 0x01,  ///< Supports zero-copy communication
        MULTICAST      = 0x02,  ///< Supports multicast/broadcast
        NETWORK        = 0x04,  ///< Supports cross-ECU communication
        LOCAL_ONLY     = 0x08,  ///< Local IPC only
        QOS_AWARE      = 0x10,  ///< Supports QoS policies
        SECURITY       = 0x20   ///< Supports encryption/authentication
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_BINDING_TYPES_HPP
