/**
 * @file        SharedMemoryRegistry.hpp
 * @author      LightAP Development Team
 * @brief       Dual registry implementation with QM/ASIL-D physical isolation
 * @date        2025-11-20
 * @details     Zero-daemon service registry using fixed-slot mapping in shared memory.
 *              QM Registry: /dev/shm/lap_com_registry_qm (all processes read/write)
 *              ASIL-D Registry: /dev/shm/lap_com_registry_asil (controlled access)
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R24-11 Compliance:
 *              - SWS_CM_00001: FindService implementation
 *              - SWS_CM_00002: OfferService implementation
 *              - SWS_CM_00110: Service registry synchronization
 *              - SWS_CM_00111: Service lifecycle management
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §1.4, §2.1
 *              AUTOSAR_AP_SWS_CommunicationManagement.pdf §7.1
 * sdk:
 * platform:    Linux 5.10+
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/20  <td>1.0      <td>LightAP Team    <td>Initial dual-registry implementation
 * </table>
 */
#ifndef LAP_COM_REGISTRY_SHARED_MEMORY_REGISTRY_HPP
#define LAP_COM_REGISTRY_SHARED_MEMORY_REGISTRY_HPP

#include "ServiceSlot.hpp"
#include "SeqLock.hpp"

#include <lap/core/CResult.hpp>
#include <lap/core/COptional.hpp>
#include <lap/core/CString.hpp>

#include <cstdint>
#include <cstring>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace lap
{
namespace com
{
namespace registry
{
    using lap::core::Result;
    using lap::core::Optional;
    using lap::core::String;

    /**
     * @brief Registry type enumeration (QM or ASIL)
     * @note Safety level mapping:
     *       - QM Registry: QM + ASIL-A/B services (security enhanced, shared registry)
     *       - ASIL Registry: ASIL-C/D services (physically isolated registry)
     */
    enum class RegistryType : uint8_t
    {
        QM      = 0,  ///< QM registry (QM/ASIL-A/B services with security enhancement)
        ASIL    = 1,  ///< ASIL registry (ASIL-C/D services, physically isolated)
        BOTH    = 2   ///< Broadcast service (written to both registries)
    };

    /**
     * @brief Error codes for registry operations
     */
    enum class RegistryError : uint32_t
    {
        SUCCESS            = 0,
        SHM_CREATE_FAILED  = 1,  ///< Failed to create shared memory file
        SHM_RESIZE_FAILED  = 2,  ///< Failed to resize shared memory
        SHM_MMAP_FAILED    = 3,  ///< Failed to mmap shared memory
        SLOT_INDEX_INVALID = 4,  ///< Slot index out of range or reserved
        SLOT_OCCUPIED      = 5,  ///< Slot already occupied by another service
        SERVICE_NOT_FOUND  = 6,  ///< Service not found in registry
        PERMISSION_DENIED  = 7   ///< Insufficient permissions
    };

    /**
     * @brief Constants for registry configuration
     */
    struct RegistryConfig
    {
        /// Maximum number of service slots per registry
        static constexpr uint32_t MAX_SLOTS = 1024;
        
        /// Size of each slot (256 bytes)
        static constexpr size_t SLOT_SIZE = sizeof(ServiceSlot);
        
        /// Total registry size (256KB = 1024 slots × 256 bytes)
        static constexpr size_t REGISTRY_SIZE = MAX_SLOTS * SLOT_SIZE;
        
        /// Reserved slot index (prohibited)
        static constexpr uint32_t RESERVED_SLOT = 0;
        
        /// Broadcast slot index (slot 1023)
        static constexpr uint32_t BROADCAST_SLOT = 1023;
        
        /// QM registry memfd name (QM/ASIL-A/B services)
        static constexpr const char* QM_MEMFD_NAME = "lap_com_registry_qm";
        
        /// ASIL registry memfd name (ASIL-C/D services, isolated)
        static constexpr const char* ASIL_MEMFD_NAME = "lap_com_registry_asil";
        
        /// Unix Domain Socket path for FD passing
        static constexpr const char* UDS_SOCKET_PATH = "/var/run/lap_com_registry.sock";
        
        /// QM registry permissions (all processes can read/write)
        static constexpr mode_t QM_PERMISSIONS = 0666;
        
        /// ASIL registry permissions (controlled access for ASIL-C/D)
        static constexpr mode_t ASIL_PERMISSIONS = 0640;
        
        /// memfd sealing flags for security
        static constexpr int SEALING_FLAGS = F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL;
        
        /// Service ID range for QM services (includes QM + ASIL-A/B with security)
        static constexpr uint16_t QM_SERVICE_ID_MIN = 0x0001;
        static constexpr uint16_t QM_SERVICE_ID_MAX = 0x0417;  // Extended to 1047 slots
        
        /// Service ID range for ASIL services (ASIL-C/D only, physically isolated)
        static constexpr uint16_t ASIL_SERVICE_ID_MIN = 0xF001;
        static constexpr uint16_t ASIL_SERVICE_ID_MAX = 0xF3FE;  // Adjusted to avoid 0xF3FF
        
        /// Broadcast service ID
        static constexpr uint16_t BROADCAST_SERVICE_ID = 0xFFFF;
        
        /// Invalid service ID (slot 0 mapping)
        static constexpr uint16_t INVALID_SERVICE_ID_1 = 0x0000;
        static constexpr uint16_t INVALID_SERVICE_ID_2 = 0xF000;
    };

    /**
     * @brief Single registry manager (QM or ASIL)
     * 
     * @details Manages one shared memory registry with 1024 fixed slots.
     *          Each slot is 256 bytes, cache-line aligned.
     *          Uses seqlock for lock-free concurrent access.
     * 
     * @note Design rationale (from SERVICE_DISCOVERY_ARCHITECTURE.md §2.2.2):
     *       - Anonymous shared memory: memfd_create (no /dev/shm files)
     *       - File descriptor passing: Unix Domain Socket + SCM_RIGHTS
     *       - Memory sealing: F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL
     *       - Fixed slot mapping: SlotIndex = ServiceID & 1023
     *       - Zero-daemon: processes self-register on startup
     *       - Physical isolation: separate memfd for QM and ASIL registries
     *       - Slot 0: reserved (prohibited, error detection)
     *       - Slot 1023: broadcast slot (bidirectional cross-registry)
     * 
     * @note Safety level mapping:
     *       - QM Registry: QM + ASIL-A/B (security enhanced, shared)
     *       - ASIL Registry: ASIL-C/D only (physically isolated)
     */
    class SingleRegistry final
    {
    public:
        /**
         * @brief Constructor
         * @param type Registry type (QM or ASIL)
         */
        explicit SingleRegistry(RegistryType type) noexcept
            : type_(type)
            , memfd_(-1)
            , slots_(nullptr)
        {
        }

        /**
         * @brief Destructor - unmaps and closes memfd
         */
        ~SingleRegistry() noexcept
        {
            Cleanup();
        }

        // Disable copy and move
        SingleRegistry(const SingleRegistry&) = delete;
        SingleRegistry& operator=(const SingleRegistry&) = delete;
        SingleRegistry(SingleRegistry&&) = delete;
        SingleRegistry& operator=(SingleRegistry&&) = delete;

        /**
         * @brief Initialize registry (create anonymous shared memory with memfd_create)
         * @return Result<void> Success or error code
         * 
         * @note This creates anonymous shared memory using memfd_create():
         *       - QM: memfd "lap_com_registry_qm" (QM + ASIL-A/B services)
         *       - ASIL: memfd "lap_com_registry_asil" (ASIL-C/D services only)
         *       - Flags: MFD_CLOEXEC | MFD_ALLOW_SEALING
         *       - No filesystem pollution (no /dev/shm files)
         *       - Memory sealing for security (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL)
         *       - File descriptor passed via Unix Domain Socket (SCM_RIGHTS)
         * 
         * @reference SERVICE_DISCOVERY_ARCHITECTURE.md §2.2.2
         */
        Result<void> Initialize() noexcept;

        /**
         * @brief Initialize registry by receiving memfd from server (Phase 2)
         * @param socket_path Path to Unix domain socket (e.g., /run/lap/registry_qm.sock)
         * @return Result<void> Success or error code
         * 
         * @details Client-side initialization:
         *          1. Connect to UDS socket
         *          2. Receive memfd FD via SCM_RIGHTS
         *          3. mmap memfd to process space
         *          4. All clients share same physical memory
         * 
         * @note This is the recommended initialization method for multi-process setups
         * @reference SERVICE_DISCOVERY_ARCHITECTURE.md §2.2.2 (UDS FD Passing)
         */
        Result<void> InitializeFromSocket(const String& socket_path) noexcept;

        /**
         * @brief Register a service in a specific slot
         * @param slot_index Target slot index (1~1022, or 1023 for broadcast)
         * @param service_id Service interface ID
         * @param instance_id Service instance ID
         * @param major_version Service major version
         * @param minor_version Service minor version
         * @param binding_type Transport binding type ("iceoryx2", "dds", etc.)
         * @param endpoint Transport-specific endpoint address
         * @return Result<void> Success or error code
         * 
         * @note AUTOSAR SWS_CM_00002 (OfferService) implementation
         * @note Slot 0 is reserved and will return SLOT_INDEX_INVALID
         */
        Result<void> RegisterService(
            uint32_t slot_index,
            uint64_t service_id,
            uint64_t instance_id,
            uint32_t major_version,
            uint32_t minor_version,
            const char* binding_type,
            const char* endpoint) noexcept;

        /**
         * @brief Unregister a service from a slot
         * @param slot_index Slot index to clear
         * @return Result<void> Success or error code
         * 
         * @note AUTOSAR SWS_CM_00111 (StopOfferService) implementation
         */
        Result<void> UnregisterService(uint32_t slot_index) noexcept;

        /**
         * @brief Find a service by service ID (O(1) lookup)
         * @param service_id Service ID to search for
         * @return Optional<ServiceSlot> Service info if found
         * 
         * @note AUTOSAR SWS_CM_00001 (FindService) implementation
         * @note Uses fixed slot mapping: slot = service_id & 1023
         */
        Optional<ServiceSlot> FindService(uint64_t service_id) const noexcept;

        /**
         * @brief Read a specific slot atomically
         * @param slot_index Slot index to read
         * @return Optional<ServiceSlot> Slot contents if successful
         */
        Optional<ServiceSlot> ReadSlot(uint32_t slot_index) const noexcept;

        /**
         * @brief Update heartbeat timestamp for a service
         * @param slot_index Slot index
         * @param timestamp_ns Current timestamp in nanoseconds
         * @return Result<void> Success or error code
         */
        Result<void> UpdateHeartbeat(uint32_t slot_index, uint64_t timestamp_ns) noexcept;

        /**
         * @brief Check if registry is initialized
         * @return true if shared memory is mapped
         */
        [[nodiscard]] bool IsInitialized() const noexcept
        {
            return (slots_ != nullptr);
        }

        /**
         * @brief Get registry type
         * @return RegistryType (QM or ASIL)
         */
        [[nodiscard]] RegistryType GetType() const noexcept
        {
            return type_;
        }

        /**
         * @brief Get memfd file descriptor (for testing)
         * @return memfd FD or -1 if not initialized
         */
        [[nodiscard]] int GetMemfd() const noexcept
        {
            return memfd_;
        }

    private:
        /**
         * @brief Cleanup shared memory resources
         */
        void Cleanup() noexcept;

        /**
         * @brief Connect to UDS socket and receive memfd FD
         * @param socket_path Path to Unix domain socket
         * @return Result<int> memfd FD or error
         */
        Result<int> receiveMemfdFromSocket(const String& socket_path) noexcept;

        /**
         * @brief Validate slot index
         * @param slot_index Slot index to validate
         * @return true if valid (1~1023)
         */
        [[nodiscard]] bool IsValidSlotIndex(uint32_t slot_index) const noexcept
        {
            return (slot_index > 0 && slot_index < RegistryConfig::MAX_SLOTS);
        }

        /**
         * @brief Get memfd name for registry type
         * @return Memfd name string
         */
        [[nodiscard]] const char* GetMemfdName() const noexcept
        {
            return (type_ == RegistryType::QM) 
                   ? RegistryConfig::QM_MEMFD_NAME 
                   : RegistryConfig::ASIL_MEMFD_NAME;
        }

        /**
         * @brief Get permissions for registry type
         * @return Permission mode
         */
        [[nodiscard]] mode_t GetPermissions() const noexcept
        {
            return (type_ == RegistryType::QM) 
                   ? RegistryConfig::QM_PERMISSIONS 
                   : RegistryConfig::ASIL_PERMISSIONS;
        }

    private:
        RegistryType type_;      ///< Registry type (QM or ASIL)
        int memfd_;              ///< Anonymous shared memory file descriptor (memfd_create)
        ServiceSlot* slots_;     ///< Pointer to mapped slot array
    };

    /**
     * @brief Dual registry manager (QM + ASIL)
     * 
     * @details Manages both QM and ASIL registries with automatic routing.
     *          Service ID determines which registry to use:
     *          - 0x0001~0x0417: QM registry (QM + ASIL-A/B services)
     *          - 0xF001~0xF3FE: ASIL registry (ASIL-C/D services only)
     *          - 0xFFFF: Both registries (broadcast, bidirectional)
     * 
     * @note Physical isolation ensures QM services cannot corrupt ASIL services
     * @note Safety level mapping:
     *       - QM Registry: Hosts QM + ASIL-A/B services (security enhanced)
     *       - ASIL Registry: Hosts ASIL-C/D services only (physically isolated)
     */
    class SharedMemoryRegistry final
    {
    public:
        /**
         * @brief Constructor
         */
        SharedMemoryRegistry() noexcept
            : qm_registry_(RegistryType::QM)
            , asil_registry_(RegistryType::ASIL)
        {
        }

        /**
         * @brief Destructor
         */
        ~SharedMemoryRegistry() noexcept = default;

        // Disable copy and move
        SharedMemoryRegistry(const SharedMemoryRegistry&) = delete;
        SharedMemoryRegistry& operator=(const SharedMemoryRegistry&) = delete;
        SharedMemoryRegistry(SharedMemoryRegistry&&) = delete;
        SharedMemoryRegistry& operator=(SharedMemoryRegistry&&) = delete;

        /**
         * @brief Initialize both QM and ASIL registries
         * @return Result<void> Success or error code
         */
        Result<void> Initialize() noexcept;

        /**
         * @brief Initialize from systemd socket activation (client-side)
         * @param qm_socket_path Path to QM registry socket (e.g., /run/lap/registry_qm.sock)
         * @param asil_socket_path Path to ASIL registry socket (e.g., /run/lap/registry_asil.sock)
         * @return Result<void> Success or error code
         * 
         * @note Client-side initialization using systemd socket activation
         * @note Receives memfd FDs from RegistryInitializer via SCM_RIGHTS
         * @note Phase 2: systemd socket activation integration
         */
        Result<void> InitializeFromSocket(
            const String& qm_socket_path,
            const String& asil_socket_path) noexcept;

        /**
         * @brief Register a service (automatically routes to correct registry)
         * @param service_id Service ID (determines registry selection)
         * @param instance_id Instance ID
         * @param major_version Major version
         * @param minor_version Minor version
         * @param binding_type Binding type string
         * @param endpoint Endpoint address
         * @return Result<void> Success or error code
         * 
         * @note Routing logic (v3.0 updated ranges):
         *       - 0x0001~0x0417 → QM+AB registry (QM/ASIL-A/B)
         *       - 0xF001~0xF3FE → ASIL-CD registry (ASIL-C/D)
         *       - 0xFFFF → Both registries (broadcast, bidirectional)
         */
        Result<void> RegisterService(
            uint64_t service_id,
            uint64_t instance_id,
            uint32_t major_version,
            uint32_t minor_version,
            const char* binding_type,
            const char* endpoint) noexcept;

        /**
         * @brief Unregister a service
         * @param service_id Service ID
         * @return Result<void> Success or error code
         */
        Result<void> UnregisterService(uint64_t service_id) noexcept;

        /**
         * @brief Find a service by service ID
         * @param service_id Service ID to find
         * @return Optional<ServiceSlot> Service info if found
         */
        Optional<ServiceSlot> FindService(uint64_t service_id) const noexcept;

        /**
         * @brief Update heartbeat for a service
         * @param service_id Service ID
         * @param timestamp_ns Current timestamp (nanoseconds)
         * @return Result<void> Success or error code
         */
        Result<void> UpdateHeartbeat(uint64_t service_id, uint64_t timestamp_ns) noexcept;

    private:
        /**
         * @brief Calculate slot index from service ID (zero-collision design)
         * @param service_id Service ID
         * @return Slot index (1~1023, or 0 if invalid)
         * 
         * @note Algorithm: slot = service_id & 1023 (low 10 bits)
         * @note Slot 0 is reserved (0x0000 and 0xF000 are invalid)
         */
        static uint32_t CalculateSlot(uint64_t service_id) noexcept
        {
            uint32_t slot = static_cast<uint32_t>(service_id & 1023);
            
            // Slot 0 is reserved (invalid service IDs)
            if (slot == 0) {
                return 0;  // Invalid
            }
            
            return slot;
        }

        /**
         * @brief Select registry based on service ID
         * @param service_id Service ID
         * @return RegistryType (QM_AB, ASIL_CD, or BOTH)
         * @note v3.0 design: Updated service ID ranges
         */
        static RegistryType SelectRegistry(uint64_t service_id) noexcept
        {
            uint16_t sid = static_cast<uint16_t>(service_id & 0xFFFF);
            
            if (sid == RegistryConfig::BROADCAST_SERVICE_ID) {
                return RegistryType::BOTH;  // Broadcast to both registries (bidirectional)
            } else if (sid >= RegistryConfig::ASIL_SERVICE_ID_MIN && 
                       sid <= RegistryConfig::ASIL_SERVICE_ID_MAX) {
                return RegistryType::ASIL;  // ASIL-C/D services
            } else if (sid >= RegistryConfig::QM_SERVICE_ID_MIN && 
                       sid <= RegistryConfig::QM_SERVICE_ID_MAX) {
                return RegistryType::QM;  // QM + ASIL-A/B services
            } else {
                // Invalid service ID (out of range or reserved)
                return RegistryType::QM;  // Fallback to QM
            }
        }

    private:
        SingleRegistry qm_registry_;    ///< QM registry (QM + ASIL-A/B)
        SingleRegistry asil_registry_;  ///< ASIL registry (ASIL-C/D only)
    };

} // namespace registry
} // namespace com
} // namespace lap

#endif // LAP_COM_REGISTRY_SHARED_MEMORY_REGISTRY_HPP
