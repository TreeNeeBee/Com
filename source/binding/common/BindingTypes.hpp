/**
 * @file        BindingTypes.hpp
 * @author      LightAP Development Team
 * @brief       Common types for transport bindings
 * @date        2025-11-21
 * @details     Defines shared data structures and enumerations for all binding implementations
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R25-11 Compliance:
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

#include <lap/core/CTypedef.hpp>
#include <lap/core/CString.hpp>

namespace lap
{
namespace com
{
namespace binding
{
    using lap::core::Bool;
    using lap::core::UInt32;
    using lap::core::UInt64;
    using lap::core::Double;
    using lap::core::String;
    /**
     * @brief Binding health status
     * @note Used by BindingManager for fault detection and automatic failover
     */
    struct BindingHealth
    {
        Bool isHealthy;                 ///< Overall health status
        UInt32 errorCount;              ///< Total errors since initialization
        UInt32 consecutiveErrors;       ///< Consecutive errors (triggers failover)
        Double availabilityPercent;     ///< Uptime percentage (0.0-100.0)
        UInt64 lastErrorTimestamp;      ///< Last error time (nanoseconds since epoch)
        String lastErrorMessage;        ///< Human-readable error description

        /// Health thresholds
        static constexpr UInt32 kMaxConsecutiveErrors = 10;
        static constexpr Double kMinAvailabilityPercent = 95.0;

        BindingHealth()
            : isHealthy( true ),
              errorCount( 0 ),
              consecutiveErrors( 0 ),
              availabilityPercent( 100.0 ),
              lastErrorTimestamp( 0 ),
              lastErrorMessage( "OK" ) {}
    };

    /**
     * @brief Transport performance metrics
     * @note Used by ITransportBinding::GetMetrics() for monitoring
     */
    struct TransportMetrics
    {
        /// Message statistics
        UInt64 messagesSent;            ///< Total messages sent
        UInt64 messagesReceived;        ///< Total messages received
        UInt64 messagesDropped;         ///< Messages dropped due to errors

        /// Performance metrics
        UInt64 avgLatencyNs;            ///< Average message latency (nanoseconds)
        UInt64 maxLatencyNs;            ///< Maximum observed latency
        UInt64 minLatencyNs;            ///< Minimum observed latency

        /// Throughput
        UInt64 bytesSent;              ///< Total bytes transmitted
        UInt64 bytesReceived;          ///< Total bytes received
        UInt64 currentBandwidthBps;    ///< Current bandwidth (bytes/sec)

        /// Connection state
        UInt32 activeConnections;       ///< Number of active connections
        UInt32 failedConnections;       ///< Number of failed connection attempts

        /// Error counters
        UInt32 serializationErrors;     ///< Serialization/deserialization errors
        UInt32 timeoutErrors;           ///< Operation timeout errors

        TransportMetrics()
            : messagesSent( 0 ),
              messagesReceived( 0 ),
              messagesDropped( 0 ),
              avgLatencyNs( 0 ),
              maxLatencyNs( 0 ),
              minLatencyNs( UINT64_MAX ),
              bytesSent( 0 ),
              bytesReceived( 0 ),
              currentBandwidthBps( 0 ),
              activeConnections( 0 ),
              failedConnections( 0 ),
              serializationErrors( 0 ),
              timeoutErrors( 0 ) {}
    };

    /**
     * @brief Binding capability flags
     */
    enum class BindingCapability : UInt32
    {
        kZeroCopy      = 0x01,  ///< Supports zero-copy communication
        kMulticast     = 0x02,  ///< Supports multicast/broadcast
        kNetwork       = 0x04,  ///< Supports cross-ECU communication
        kLocalOnly     = 0x08,  ///< Local IPC only
        kQosAware      = 0x10,  ///< Supports QoS policies
        kSecurity      = 0x20   ///< Supports encryption/authentication
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_BINDING_TYPES_HPP
