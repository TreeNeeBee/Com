/**
 * @file        ServiceSlot.hpp
 * @author      LightAP Development Team
 * @brief       Fixed-slot service registry slot structure with seqlock synchronization
 * @date        2025-11-20
 * @details     256-byte cache-aligned service slot for zero-daemon service discovery.
 *              Implements lock-free seqlock mechanism for concurrent access.
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R24-11 Compliance:
 *              - SWS_CM_00302: Service Instance Identification
 *              - SWS_CM_00303: Service Instance Attributes
 *              - SWS_CM_00110: Service Registry Management
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §2.1 (Core Data Structures)
 *              AUTOSAR_AP_SWS_CommunicationManagement.pdf §7.2.1
 * sdk:
 * platform:    Linux 5.10+
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/20  <td>1.0      <td>LightAP Team    <td>Initial implementation (zero-daemon architecture)
 * </table>
 */
#ifndef LAP_COM_REGISTRY_SERVICE_SLOT_HPP
#define LAP_COM_REGISTRY_SERVICE_SLOT_HPP

#include <atomic>
#include <cstdint>
#include <cstring>
#include <unistd.h>  // for pid_t

#include <lap/core/CTypedef.hpp>

namespace lap
{
namespace com
{
namespace registry
{
    /**
     * @brief Service slot status enumeration
     * @note Aligned with AUTOSAR SWS_CM_00310 (Service State)
     */
    enum class SlotStatus : uint32_t
    {
        IDLE        = 0,  ///< Slot is empty and available
        ACTIVE      = 1,  ///< Service is registered and alive
        UNREGISTERING = 2  ///< Service is being unregistered (transient state)
    };

    /**
     * @brief Fixed-size 256-byte service slot with seqlock synchronization
     * 
     * @details Design rationale (from SERVICE_DISCOVERY_ARCHITECTURE.md §2.1):
     *          - 256 bytes = 4 cache lines (64-byte alignment)
     *          - seqlock ensures lock-free reads with < 100ns latency
     *          - Fixed slot mapping: SlotIndex = ServiceID & 1023
     *          - Zero-daemon: no RouDi, no central server
     * 
     * Memory layout (total 256 bytes):
     *   - [0-7]     seqlock control (atomic uint64_t)
     *   - [8-39]    service identification (32 bytes)
     *   - [40-135]  network endpoint (96 bytes)
     *   - [136-159] lifecycle control (24 bytes)
     *   - [160-223] metadata (64 bytes)
     *   - [224-255] padding (32 bytes)
     * 
     * @note AUTOSAR Requirements:
     *       - SWS_CM_00302: Each slot uniquely identifies a service instance
     *       - SWS_CM_00303: Contains service ID, instance ID, version, endpoint
     *       - SWS_CM_00311: Heartbeat mechanism for service liveness
     */
    struct alignas(64) ServiceSlot final
    {
        // ========================================================================
        // seqlock Control Field (8 bytes)
        // ========================================================================
        
        /**
         * @brief Sequential lock counter for atomic reads/writes
         * @details - Odd value: write in progress (readers must retry)
         *          - Even value: slot is readable
         *          Readers check sequence before and after reading data.
         *          If values mismatch or odd, retry the read.
         * @note Lock-free read performance: < 100ns (target)
         */
        std::atomic<uint64_t> sequence;

        // ========================================================================
        // Service Identification (32 bytes)
        // ========================================================================
        
        /**
         * @brief Service interface ID (AUTOSAR service ID)
         * @note SWS_CM_00302: Unique identifier for service type
         *       Range allocation:
         *       - 0x0001~0x03FF: QM services
         *       - 0xF001~0xF3FF: ASIL-D services
         *       - 0xFFFF: Broadcast service
         *       - 0x0000/0xF000: Reserved (slot 0, prohibited)
         */
        uint64_t service_id;
        
        /**
         * @brief Service instance ID (unique per service instance)
         * @note SWS_CM_00303: Lower 32 bits encode instance metadata:
         *       - [15:0]  service_id (16 bits)
         *       - [23:16] instance_no (8 bits, 0~255)
         *       - [27:24] domain (4 bits, 0=perception, 1=control, ...)
         *       - [30:28] asil_level (3 bits, 0=QM, 1=A, ..., 4=D)
         *       - [31]    redundancy (1 bit, 0=primary, 1=backup)
         *       Upper 32 bits: reserved for future use
         */
        uint64_t instance_id;
        
        /**
         * @brief Service major version number
         * @note SWS_CM_00304: Major version compatibility check
         */
        uint32_t major_version;
        
        /**
         * @brief Service minor version number
         * @note SWS_CM_00304: Minor version backward compatibility
         */
        uint32_t minor_version;

        // ========================================================================
        // Network Endpoint (96 bytes)
        // ========================================================================
        
        /**
         * @brief Transport binding type identifier
         * @note Valid values: "iceoryx2", "dds", "someip", "custom"
         *       Aligned with AUTOSAR binding specification SWS_CM_00401
         */
        char binding_type[16];
        
        /**
         * @brief Transport-specific endpoint address
         * @details Format depends on binding_type:
         *          - iceoryx2: "shm://service_name/instance_1"
         *          - dds:      "topic://domain_0/service_topic"
         *          - someip:   "tcp://192.168.1.10:30509"
         *          - custom:   "uds:///var/run/lap_service.sock"
         * @note Max 79 chars + null terminator
         */
        char endpoint[80];

        // ========================================================================
        // Lifecycle Control (24 bytes)
        // ========================================================================
        
        /**
         * @brief Last heartbeat timestamp (nanoseconds since epoch)
         * @note SWS_CM_00311: Used for service liveness detection
         *       Updated by service owner periodically
         */
        uint64_t last_heartbeat_ns;
        
        /**
         * @brief Heartbeat interval in milliseconds
         * @note Typical value: 100ms for QM, 50ms for ASIL-D
         *       Timeout detection: 3× interval
         */
        uint32_t heartbeat_interval_ms;
        
        /**
         * @brief Slot status (IDLE/ACTIVE/UNREGISTERING)
         * @see SlotStatus
         */
        uint32_t status;
        
        /**
         * @brief Process ID of the service owner
         * @note Used for cleanup when process crashes (via kill(pid, 0))
         */
        pid_t owner_pid;

        // ========================================================================
        // Metadata (64 bytes)
        // ========================================================================
        
        /**
         * @brief JSON-encoded extended metadata
         * @details Example: {"qos":{"reliability":"best_effort"},"tags":["sensor"]}
         * @note Max 63 chars + null terminator
         */
        char metadata[64];

        // ========================================================================
        // Padding to 256 bytes (32 bytes)
        // ========================================================================
        
        /**
         * @brief Reserved padding to ensure 256-byte total size
         * @note Ensures 4× cache-line alignment (4 × 64 = 256 bytes)
         */
        uint8_t _padding[32];

        // ========================================================================
        // Constructors & Methods
        // ========================================================================
        
        /**
         * @brief Default constructor - initializes to IDLE state
         */
        ServiceSlot() noexcept
            : sequence(0)
            , service_id(0)
            , instance_id(0)
            , major_version(0)
            , minor_version(0)
            , binding_type{}
            , endpoint{}
            , last_heartbeat_ns(0)
            , heartbeat_interval_ms(0)
            , status(static_cast<uint32_t>(SlotStatus::IDLE))
            , owner_pid(0)
            , metadata{}
            , _padding{}
        {
            std::memset(binding_type, 0, sizeof(binding_type));
            std::memset(endpoint, 0, sizeof(endpoint));
            std::memset(metadata, 0, sizeof(metadata));
        }

        /**
         * @brief Copy constructor - manually copy non-atomic fields
         * @note atomic<uint64_t> sequence is copied via load/store
         */
        ServiceSlot(const ServiceSlot& other) noexcept
            : sequence(other.sequence.load(std::memory_order_relaxed))
            , service_id(other.service_id)
            , instance_id(other.instance_id)
            , major_version(other.major_version)
            , minor_version(other.minor_version)
            , binding_type{}
            , endpoint{}
            , last_heartbeat_ns(other.last_heartbeat_ns)
            , heartbeat_interval_ms(other.heartbeat_interval_ms)
            , status(other.status)
            , owner_pid(other.owner_pid)
            , metadata{}
            , _padding{}
        {
            std::memcpy(binding_type, other.binding_type, sizeof(binding_type));
            std::memcpy(endpoint, other.endpoint, sizeof(endpoint));
            std::memcpy(metadata, other.metadata, sizeof(metadata));
        }

        /**
         * @brief Copy assignment - manually copy non-atomic fields
         */
        ServiceSlot& operator=(const ServiceSlot& other) noexcept
        {
            if (this != &other) {
                sequence.store(other.sequence.load(std::memory_order_relaxed), 
                              std::memory_order_relaxed);
                service_id = other.service_id;
                instance_id = other.instance_id;
                major_version = other.major_version;
                minor_version = other.minor_version;
                std::memcpy(binding_type, other.binding_type, sizeof(binding_type));
                std::memcpy(endpoint, other.endpoint, sizeof(endpoint));
                last_heartbeat_ns = other.last_heartbeat_ns;
                heartbeat_interval_ms = other.heartbeat_interval_ms;
                status = other.status;
                owner_pid = other.owner_pid;
                std::memcpy(metadata, other.metadata, sizeof(metadata));
            }
            return *this;
        }

        /**
         * @brief Check if slot is currently empty/idle
         * @return true if slot status is IDLE
         */
        [[nodiscard]] bool IsIdle() const noexcept
        {
            return status == static_cast<uint32_t>(SlotStatus::IDLE);
        }

        /**
         * @brief Check if slot contains an active service
         * @return true if slot status is ACTIVE
         */
        [[nodiscard]] bool IsActive() const noexcept
        {
            return status == static_cast<uint32_t>(SlotStatus::ACTIVE);
        }

        /**
         * @brief Reset slot to IDLE state (non-atomic, use with seqlock)
         */
        void Reset() noexcept
        {
            service_id = 0;
            instance_id = 0;
            major_version = 0;
            minor_version = 0;
            std::memset(binding_type, 0, sizeof(binding_type));
            std::memset(endpoint, 0, sizeof(endpoint));
            last_heartbeat_ns = 0;
            heartbeat_interval_ms = 0;
            status = static_cast<uint32_t>(SlotStatus::IDLE);
            owner_pid = 0;
            std::memset(metadata, 0, sizeof(metadata));
        }
    };

    // ========================================================================
    // Static Assertions (Design Validation)
    // ========================================================================
    
    /**
     * @brief Enforce 256-byte slot size as per architecture spec
     */
    static_assert(sizeof(ServiceSlot) == 256, 
                  "ServiceSlot must be exactly 256 bytes (4 cache lines)");
    
    /**
     * @brief Enforce 64-byte alignment for cache-line optimization
     */
    static_assert(alignof(ServiceSlot) == 64, 
                  "ServiceSlot must be 64-byte aligned");

} // namespace registry
} // namespace com
} // namespace lap

#endif // LAP_COM_REGISTRY_SERVICE_SLOT_HPP
