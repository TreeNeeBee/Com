/**
 * @file        CServiceRegistry.hpp
 * @author      LightAP Development Team
 * @brief       Single registry manager (QM or ASIL)
 * @date        2026/02/06
 * @details     Manages one shared memory registry with 1024 fixed slots.
 *              Each slot is 256 bytes, cache-line aligned.
 *              Single-writer updates via CRegistryDispatcher.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 Compliance:
 *              - SWS_CM_00001: Service discovery infrastructure
 *              - SWS_CM_00110: Registry lifecycle management
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §2.2.2
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/05  <td>1.0      <td>LightAP Team    <td>Initial implementation
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style optimization per code_rules.md
 * </table>
 */
#ifndef LAP_COM_CSERVICE_REGISTRY_HPP
#define LAP_COM_CSERVICE_REGISTRY_HPP

// ==================== Project-Internal Headers ====================
#include "ServiceSlot.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/CResult.hpp>
#include <lap/core/COptional.hpp>
#include <lap/core/CString.hpp>
#include <lap/core/CTypedef.hpp>

// ==================== Standard Library Headers ====================
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
    using lap::core::UInt8;
    using lap::core::UInt16;
    using lap::core::UInt32;
    using lap::core::UInt64;
    using lap::core::Bool;
    using lap::core::Size;

    /**
     * @brief Registry type enumeration (QM or ASIL)
     * @note  Safety level mapping:
     *        - QM Registry: QM + ASIL-A/B services (security enhanced, shared registry)
     *        - ASIL Registry: ASIL-C/D services (physically isolated registry)
     */
    enum class RegistryType : UInt8
    {
        kQM     = 0,  ///< QM registry (QM/ASIL-A/B services with security enhancement)
        kASIL   = 1,  ///< ASIL registry (ASIL-C/D services, physically isolated)
        kBoth   = 2   ///< Broadcast service (written to both registries)
    };

    /**
     * @brief Error codes for registry operations
     */
    enum class RegistryError : UInt32
    {
        kSuccess            = 0,
        kShmCreateFailed    = 1,  ///< Failed to create shared memory file
        kShmResizeFailed    = 2,  ///< Failed to resize shared memory
        kShmMmapFailed      = 3,  ///< Failed to mmap shared memory
        kSlotIndexInvalid   = 4,  ///< Slot index out of range or reserved
        kSlotOccupied       = 5,  ///< Slot already occupied by another service
        kServiceNotFound    = 6,  ///< Service not found in registry
        kPermissionDenied   = 7   ///< Insufficient permissions
    };

    /**
     * @brief Constants for registry configuration
     * @note  Thread-safe: All values are constexpr compile-time constants
     */
    struct RegistryConfig
    {
        /// Maximum number of service slots per registry
        static constexpr UInt32 kMaxSlots = 1024;

        /// Size of each slot (256 bytes)
        static constexpr Size kSlotSize = sizeof( ServiceSlot );

        /// Total registry size (256KB = 1024 slots x 256 bytes)
        static constexpr Size kRegistrySize = kMaxSlots * kSlotSize;

        /// Reserved slot index (prohibited)
        static constexpr UInt32 kReservedSlot = 0;

        /// Broadcast slot index (slot 1023)
        static constexpr UInt32 kBroadcastSlot = 1023;

        /// QM registry memfd name (QM/ASIL-A/B services)
        static constexpr const char* kQmMemfdName = "lap_com_registry_qm";

        /// ASIL registry memfd name (ASIL-C/D services, isolated)
        static constexpr const char* kAsilMemfdName = "lap_com_registry_asil";

        /// Unix Domain Socket path for FD passing
        static constexpr const char* kUdsSocketPath = "/var/run/lap_com_registry.sock";

        /// QM registry permissions (all processes can read/write)
        static constexpr mode_t kQmPermissions = 0666;

        /// ASIL registry permissions (controlled access for ASIL-C/D)
        static constexpr mode_t kAsilPermissions = 0640;

        /// memfd sealing flags for security
        static constexpr Int32 kSealingFlags = F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL;

        /// Service ID range for QM services (includes QM + ASIL-A/B with security)
        static constexpr UInt16 kQmServiceIdMin = 0x0001;
        static constexpr UInt16 kQmServiceIdMax = 0x0417;  // Extended to 1047 slots

        /// Service ID range for ASIL services (ASIL-C/D only, physically isolated)
        static constexpr UInt16 kAsilServiceIdMin = 0xF001;
        static constexpr UInt16 kAsilServiceIdMax = 0xF3FE;  // Adjusted to avoid 0xF3FF

        /// Broadcast service ID
        static constexpr UInt16 kBroadcastServiceId = 0xFFFF;

        /// Invalid service ID (slot 0 mapping)
        static constexpr UInt16 kInvalidServiceId1 = 0x0000;
        static constexpr UInt16 kInvalidServiceId2 = 0xF000;
    };

    /**
     * @brief Single registry manager (QM or ASIL)
     *
     * @details Manages one shared memory registry with 1024 fixed slots.
     *          Each slot is 256 bytes, cache-line aligned.
     *          Single-writer updates via CRegistryDispatcher.
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
     * @note Not thread-safe: Single-writer model, caller must ensure exclusive access
     */
    class CServiceRegistry final
    {
    public:
        /**
         * @brief Constructor
         * @param type Registry type (kQM or kASIL)
         */
        explicit CServiceRegistry( RegistryType type ) noexcept
            : m_type( type )
            , m_memfd( -1 )
            , m_pSlots( nullptr )
        {
        }

        /**
         * @brief Destructor - unmaps and closes memfd
         */
        ~CServiceRegistry() noexcept
        {
            Cleanup();
        }

        // Disable copy and move
        CServiceRegistry( const CServiceRegistry& ) = delete;
        CServiceRegistry& operator=( const CServiceRegistry& ) = delete;
        CServiceRegistry( CServiceRegistry&& ) = delete;
        CServiceRegistry& operator=( CServiceRegistry&& ) = delete;

    public:
        /**
         * @brief Initialize registry (create anonymous shared memory with memfd_create)
         * @return Result< void > Success or error code
         *
         * @note This creates anonymous shared memory using memfd_create():
         *       - QM: memfd "lap_com_registry_qm" (QM + ASIL-A/B services)
         *       - ASIL: memfd "lap_com_registry_asil" (ASIL-C/D services only)
         *       - Flags: MFD_CLOEXEC | MFD_ALLOW_SEALING
         *       - No filesystem pollution (no /dev/shm files)
         *       - Memory sealing for security (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL)
         *       - File descriptor passed via Unix Domain Socket (SCM_RIGHTS)
         * @note Not thread-safe
         *
         * @reference SERVICE_DISCOVERY_ARCHITECTURE.md §2.2.2
         */
        Result< void > Initialize() noexcept;

        /**
         * @brief Initialize registry by receiving memfd from server (Phase 2)
         * @param socketPath Path to Unix domain socket (e.g., /run/lap/registry_qm.sock)
         * @return Result< void > Success or error code
         *
         * @details Client-side initialization:
         *          1. Connect to UDS socket
         *          2. Receive memfd FD via SCM_RIGHTS
         *          3. mmap memfd to process space
         *          4. All clients share same physical memory
         *
         * @note This is the recommended initialization method for multi-process setups
         * @note Not thread-safe
         * @reference SERVICE_DISCOVERY_ARCHITECTURE.md §2.2.2 (UDS FD Passing)
         */
        Result< void > InitializeFromSocket( const String& socketPath ) noexcept;

        /**
         * @brief Register a service in a specific slot
         * @param slotIndex Target slot index (1~1022, or 1023 for broadcast)
         * @param serviceId Service interface ID
         * @param instanceId Service instance ID
         * @param majorVersion Service major version
         * @param minorVersion Service minor version
         * @param bindingType Transport binding type ("iceoryx2", "dds", etc.)
         * @param endpoint Transport-specific endpoint address
         * @return Result< void > Success or error code
         *
         * @note AUTOSAR SWS_CM_00002 (OfferService) implementation
         * @note Slot 0 is reserved and will return SLOT_INDEX_INVALID
         * @note Not thread-safe
         */
        Result< void > RegisterService(
            UInt32 slotIndex,
            UInt64 serviceId,
            UInt64 instanceId,
            UInt32 majorVersion,
            UInt32 minorVersion,
            const char* bindingType,
            const char* endpoint ) noexcept;

        /**
         * @brief Unregister a service from a slot
         * @param slotIndex Slot index to clear
         * @return Result< void > Success or error code
         *
         * @note AUTOSAR SWS_CM_00111 (StopOfferService) implementation
         * @note Not thread-safe
         */
        Result< void > UnregisterService( UInt32 slotIndex ) noexcept;

        /**
         * @brief Find a service by service ID (O(1) lookup)
         * @param serviceId Service ID to search for
         * @return Optional< ServiceSlot > Service info if found
         *
         * @note AUTOSAR SWS_CM_00001 (FindService) implementation
         * @note Uses fixed slot mapping: slot = service_id & 1023
         * @note Thread-safe for reads (acquire semantics)
         */
        Optional< ServiceSlot > FindService( UInt64 serviceId ) const noexcept;

        /**
         * @brief Read a specific slot atomically
         * @param slotIndex Slot index to read
         * @return Optional< ServiceSlot > Slot contents if successful
         * @note Thread-safe for reads (acquire semantics)
         */
        Optional< ServiceSlot > ReadSlot( UInt32 slotIndex ) const noexcept;

        /**
         * @brief Update heartbeat timestamp for a service
         * @param slotIndex Slot index
         * @param timestampNs Current timestamp in nanoseconds
         * @return Result< void > Success or error code
         * @note Not thread-safe
         */
        Result< void > UpdateHeartbeat( UInt32 slotIndex, UInt64 timestampNs ) noexcept;

        /**
         * @brief Check if registry is initialized
         * @return true if shared memory is mapped
         * @note Thread-safe
         */
        [[nodiscard]] Bool IsInitialized() const noexcept
        {
            return ( m_pSlots != nullptr );
        }

        /**
         * @brief Get registry type
         * @return RegistryType (kQM or kASIL)
         * @note Thread-safe
         */
        [[nodiscard]] RegistryType GetType() const noexcept
        {
            return m_type;
        }

        /**
         * @brief Get memfd file descriptor (for testing)
         * @return memfd FD or -1 if not initialized
         * @note Thread-safe
         */
        [[nodiscard]] Int32 GetMemfd() const noexcept
        {
            return m_memfd;
        }

    private:
        /**
         * @brief Cleanup shared memory resources
         */
        void Cleanup() noexcept;

        /**
         * @brief Connect to UDS socket and receive memfd FD
         * @param socketPath Path to Unix domain socket
         * @return Result< Int32 > memfd FD or error
         */
        Result< Int32 > receiveMemfdFromSocket( const String& socketPath ) noexcept;

        /**
         * @brief Validate slot index
         * @param slotIndex Slot index to validate
         * @return true if valid (1~1023)
         */
        [[nodiscard]] Bool isValidSlotIndex( UInt32 slotIndex ) const noexcept
        {
            return ( slotIndex > 0 && slotIndex < RegistryConfig::kMaxSlots );
        }

        /**
         * @brief Get memfd name for registry type
         * @return Memfd name string
         */
        [[nodiscard]] const char* getMemfdName() const noexcept
        {
            return ( m_type == RegistryType::kQM )
                   ? RegistryConfig::kQmMemfdName
                   : RegistryConfig::kAsilMemfdName;
        }

        /**
         * @brief Get permissions for registry type
         * @return Permission mode
         */
        [[nodiscard]] mode_t getPermissions() const noexcept
        {
            return ( m_type == RegistryType::kQM )
                   ? RegistryConfig::kQmPermissions
                   : RegistryConfig::kAsilPermissions;
        }

    private:
        RegistryType    m_type;      ///< Registry type (kQM or kASIL)
        Int32           m_memfd;     ///< Anonymous shared memory file descriptor (memfd_create)
        ServiceSlot*    m_pSlots;    ///< Pointer to mapped slot array
    };

} // namespace registry
} // namespace com
} // namespace lap

#endif // LAP_COM_CSERVICE_REGISTRY_HPP
