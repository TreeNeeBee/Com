/**
 * @file        ServiceSlot.hpp
 * @author      LightAP Development Team
 * @brief       Fixed-slot service registry slot structure (single-writer optimized)
 * @date        2026/02/06
 * @details     256-byte cache-aligned service slot for service discovery.
 *              Optimized for single-writer (CRegistryDispatcher) + multi-reader access.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 Compliance:
 *              - SWS_CM_00302: Service Instance Identification
 *              - SWS_CM_00303: Service Instance Attributes
 *              - SWS_CM_00110: Service Registry Management
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §2.4 (IPC-based Registry v2.0)
 *              AUTOSAR_AP_SWS_CommunicationManagement.pdf §7.2.1
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/20  <td>1.0      <td>LightAP Team    <td>Initial implementation (zero-daemon architecture)
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style optimization per code_rules.md
 * </table>
 */
#ifndef LAP_COM_SERVICE_SLOT_HPP
#define LAP_COM_SERVICE_SLOT_HPP

// ==================== Cross-Module Headers ====================
#include <lap/core/CTypedef.hpp>

// ==================== Standard Library Headers ====================
#include <atomic>
#include <cstdint>
#include <cstring>

namespace lap
{
namespace com
{
namespace registry
{
    using lap::core::UInt8;
    using lap::core::UInt16;
    using lap::core::UInt32;
    using lap::core::UInt64;
    using lap::core::Int32;
    using lap::core::Bool;

    /**
     * @brief Service slot status enumeration
     * @note  Aligned with AUTOSAR SWS_CM_00310 (Service State)
     */
    enum class SlotStatus : UInt32
    {
        kIdle           = 0,  ///< Slot is empty and available
        kActive         = 1,  ///< Service is registered and alive
        kUnregistering  = 2   ///< Service is being unregistered (transient state)
    };

    /**
     * @brief Fixed-size 256-byte service slot (single-writer optimized)
     *
     * @details Design rationale (from SERVICE_DISCOVERY_ARCHITECTURE.md §2.4):
     *          - 256 bytes = 4 cache lines (64-byte alignment)
     *          - Single-writer updates via CRegistryDispatcher (IPC-based v2.0)
     *          - Fixed slot mapping: SlotIndex = ServiceID & 1023
     *          - Read-only shared memory for clients
     *
     * Memory layout (total 256 bytes):
     *   - [0-31]    service identification (32 bytes)
     *   - [32-127]  network endpoint (96 bytes)
     *   - [128-151] lifecycle control (24 bytes)
     *   - [152-215] metadata (64 bytes)
     *   - [216-255] padding (40 bytes)
     *
     * @note AUTOSAR Requirements:
     *       - SWS_CM_00302: Each slot uniquely identifies a service instance
     *       - SWS_CM_00303: Contains service ID, instance ID, version, endpoint
     *       - SWS_CM_00311: Heartbeat mechanism for service liveness
     * @note Not thread-safe for writes; single-writer model enforced by CRegistryDispatcher
     */
    struct alignas( 64 ) ServiceSlot final
    {
        // ==================== Service Identification (32 bytes) ====================

        /**
         * @brief Service interface ID (AUTOSAR service ID)
         * @note  SWS_CM_00302: Unique identifier for service type
         *        Range allocation:
         *        - 0x0001~0x03FF: QM services
         *        - 0xF001~0xF3FF: ASIL-D services
         *        - 0xFFFF: Broadcast service
         *        - 0x0000/0xF000: Reserved (slot 0, prohibited)
         */
        UInt64          m_serviceId;

        /**
         * @brief Service instance ID (unique per service instance)
         * @note  SWS_CM_00303: Lower 32 bits encode instance metadata:
         *        - [15:0]  service_id (16 bits)
         *        - [23:16] instance_no (8 bits, 0~255)
         *        - [27:24] domain (4 bits, 0=perception, 1=control, ...)
         *        - [30:28] asil_level (3 bits, 0=QM, 1=A, ..., 4=D)
         *        - [31]    redundancy (1 bit, 0=primary, 1=backup)
         *        Upper 32 bits: reserved for future use
         */
        UInt64          m_instanceId;

        /**
         * @brief Service major version number
         * @note  SWS_CM_00304: Major version compatibility check
         */
        UInt32          m_majorVersion;

        /**
         * @brief Service minor version number
         * @note  SWS_CM_00304: Minor version backward compatibility
         */
        UInt32          m_minorVersion;

        // ==================== Network Endpoint (96 bytes) ====================

        /**
         * @brief Transport binding type identifier
         * @note  Valid values: "iceoryx2", "dds", "someip", "custom"
         *        Aligned with AUTOSAR binding specification SWS_CM_00401
         */
        char            m_bindingType[16];

        /**
         * @brief Transport-specific endpoint address
         * @details Format depends on m_bindingType:
         *          - iceoryx2: "shm://service_name/instance_1"
         *          - dds:      "topic://domain_0/service_topic"
         *          - someip:   "tcp://192.168.1.10:30509"
         *          - custom:   "uds:///var/run/lap_service.sock"
         * @note  Max 79 chars + null terminator
         */
        char            m_endpoint[80];

        // ==================== Lifecycle Control (24 bytes) ====================

        /**
         * @brief Last heartbeat timestamp (nanoseconds since epoch)
         * @note  SWS_CM_00311: Used for service liveness detection
         *        Updated by service owner periodically
         */
        UInt64          m_lastHeartbeatNs;

        /**
         * @brief Heartbeat interval in milliseconds
         * @note  Typical value: 100ms for QM, 50ms for ASIL-D
         *        Timeout detection: 3x interval
         */
        UInt32          m_heartbeatIntervalMs;

        /**
         * @brief Slot status (kIdle/kActive/kUnregistering)
         * @see   SlotStatus
         */
        std::atomic< UInt32 > m_status;

        /**
         * @brief Process ID of the service owner
         * @note  Used for cleanup when process crashes (via kill(pid, 0))
         */
        Int32           m_ownerPid;

        // ==================== Metadata (64 bytes) ====================

        /**
         * @brief JSON-encoded extended metadata
         * @details Example: {"qos":{"reliability":"best_effort"},"tags":["sensor"]}
         * @note  Max 63 chars + null terminator
         */
        char            m_metadata[64];

        // ==================== Padding to 256 bytes (40 bytes) ====================

        /**
         * @brief Reserved padding to ensure 256-byte total size
         * @note  Ensures 4x cache-line alignment (4 x 64 = 256 bytes)
         */
        UInt8           m_padding[40];

        // ==================== Constructors & Methods ====================

        /**
         * @brief Default constructor - initializes to kIdle state
         * @note  Not thread-safe
         */
        ServiceSlot() noexcept
            : m_serviceId( 0 )
            , m_instanceId( 0 )
            , m_majorVersion( 0 )
            , m_minorVersion( 0 )
            , m_bindingType{}
            , m_endpoint{}
            , m_lastHeartbeatNs( 0 )
            , m_heartbeatIntervalMs( 0 )
            , m_status( static_cast< UInt32 > ( SlotStatus::kIdle ) )
            , m_ownerPid( 0 )
            , m_metadata{}
            , m_padding{}
        {
            std::memset( m_bindingType, 0, sizeof( m_bindingType ) );
            std::memset( m_endpoint, 0, sizeof( m_endpoint ) );
            std::memset( m_metadata, 0, sizeof( m_metadata ) );
        }

        /**
         * @brief Copy constructor - manually copy non-atomic fields
         * @param other Source slot to copy from
         * @note  Not thread-safe
         */
        ServiceSlot( const ServiceSlot& other ) noexcept
            : m_serviceId( other.m_serviceId )
            , m_instanceId( other.m_instanceId )
            , m_majorVersion( other.m_majorVersion )
            , m_minorVersion( other.m_minorVersion )
            , m_bindingType{}
            , m_endpoint{}
            , m_lastHeartbeatNs( other.m_lastHeartbeatNs )
            , m_heartbeatIntervalMs( other.m_heartbeatIntervalMs )
            , m_status( other.m_status.load( std::memory_order_relaxed ) )
            , m_ownerPid( other.m_ownerPid )
            , m_metadata{}
            , m_padding{}
        {
            std::memcpy( m_bindingType, other.m_bindingType, sizeof( m_bindingType ) );
            std::memcpy( m_endpoint, other.m_endpoint, sizeof( m_endpoint ) );
            std::memcpy( m_metadata, other.m_metadata, sizeof( m_metadata ) );
        }

        /**
         * @brief Copy assignment - manually copy non-atomic fields
         * @param other Source slot to copy from
         * @return Reference to this
         * @note  Not thread-safe
         */
        ServiceSlot& operator=( const ServiceSlot& other ) noexcept
        {
            if ( this != &other )
            {
                m_serviceId = other.m_serviceId;
                m_instanceId = other.m_instanceId;
                m_majorVersion = other.m_majorVersion;
                m_minorVersion = other.m_minorVersion;
                std::memcpy( m_bindingType, other.m_bindingType, sizeof( m_bindingType ) );
                std::memcpy( m_endpoint, other.m_endpoint, sizeof( m_endpoint ) );
                m_lastHeartbeatNs = other.m_lastHeartbeatNs;
                m_heartbeatIntervalMs = other.m_heartbeatIntervalMs;
                m_status.store( other.m_status.load( std::memory_order_relaxed ),
                                std::memory_order_relaxed );
                m_ownerPid = other.m_ownerPid;
                std::memcpy( m_metadata, other.m_metadata, sizeof( m_metadata ) );
            }
            return *this;
        }

        /**
         * @brief Check if slot is currently empty/idle
         * @return true if slot status is kIdle
         * @note  Thread-safe (acquire semantics)
         */
        [[nodiscard]] Bool IsIdle() const noexcept
        {
            return m_status.load( std::memory_order_acquire ) ==
                   static_cast< UInt32 > ( SlotStatus::kIdle );
        }

        /**
         * @brief Check if slot contains an active service
         * @return true if slot status is kActive
         * @note  Thread-safe (acquire semantics)
         */
        [[nodiscard]] Bool IsActive() const noexcept
        {
            return m_status.load( std::memory_order_acquire ) ==
                   static_cast< UInt32 > ( SlotStatus::kActive );
        }

        /**
         * @brief Reset slot to kIdle state (single-writer only)
         * @note  Not thread-safe for concurrent writes
         */
        void Reset() noexcept
        {
            m_serviceId = 0;
            m_instanceId = 0;
            m_majorVersion = 0;
            m_minorVersion = 0;
            std::memset( m_bindingType, 0, sizeof( m_bindingType ) );
            std::memset( m_endpoint, 0, sizeof( m_endpoint ) );
            m_lastHeartbeatNs = 0;
            m_heartbeatIntervalMs = 0;
            m_status.store( static_cast< UInt32 > ( SlotStatus::kIdle ),
                            std::memory_order_release );
            m_ownerPid = 0;
            std::memset( m_metadata, 0, sizeof( m_metadata ) );
        }
    };

    // ==================== Static Assertions (Design Validation) ====================

    /**
     * @brief Enforce 256-byte slot size as per architecture spec
     */
    static_assert( sizeof( ServiceSlot ) == 256,
                   "ServiceSlot must be exactly 256 bytes (4 cache lines)" );

    /**
     * @brief Enforce 64-byte alignment for cache-line optimization
     */
    static_assert( alignof( ServiceSlot ) == 64,
                   "ServiceSlot must be 64-byte aligned" );

} // namespace registry
} // namespace com
} // namespace lap

#endif // LAP_COM_SERVICE_SLOT_HPP
