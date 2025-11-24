/**
 * @file        SharedMemoryRegistry.cpp
 * @author      LightAP Development Team
 * @brief       Anonymous shared memory registry with memfd_create + UDS (v4.0)
 * @date        2025-11-20
 * @copyright   Copyright (c) 2025
 * @note        Architecture alignment: Using memfd_create instead of shm_open
 *              - No /dev/shm filesystem pollution
 *              - Memory sealing for security (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL)
 *              - File descriptor passing via Unix Domain Socket + SCM_RIGHTS
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §2.2.2
 */

#include "ComTypes.hpp"
#include "SharedMemoryRegistry.hpp"
#include <chrono>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// memfd_create support (Linux 3.17+)
// Define flags if system headers don't provide them
#ifndef MFD_CLOEXEC
    #define MFD_CLOEXEC 0x0001U
#endif
#ifndef MFD_ALLOW_SEALING
    #define MFD_ALLOW_SEALING 0x0002U
#endif

// Fallback for old glibc (< 2.27) that lacks memfd_create()
#if !defined(__GLIBC__) || __GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 27)
    #ifndef __NR_memfd_create
        #if defined(__x86_64__)
            #define __NR_memfd_create 319
        #elif defined(__aarch64__)
            #define __NR_memfd_create 385
        #elif defined(__arm__)
            #define __NR_memfd_create 356
        #elif defined(__riscv) && (__riscv_xlen == 64)
            #define __NR_memfd_create 279
        #else
            #error "Unsupported architecture for memfd_create"
        #endif
    #endif
    
    /**
     * @brief Fallback memfd_create for old glibc
     * @param name Memfd name (visible in /proc/PID/fd/)
     * @param flags MFD_CLOEXEC | MFD_ALLOW_SEALING
     * @return File descriptor or -1 on error
     */
    static inline int memfd_create(const char* name, unsigned int flags)
    {
        return static_cast<int>(syscall(__NR_memfd_create, name, flags));
    }
#endif

namespace lap
{
namespace com
{
namespace registry
{
    using namespace std::chrono;
    using lap::com::MakeErrorCode;
    using lap::com::ComErrc;

    // ========================================================================
    // SingleRegistry Implementation
    // ========================================================================

    Result<void> SingleRegistry::Initialize() noexcept
    {
        if (IsInitialized()) {
            // Already initialized
            return Result<void>::FromValue();  // Success
        }

        const char* memfd_name = GetMemfdName();

        // Step 1: Create anonymous shared memory with memfd_create
        // MFD_CLOEXEC: Close-on-exec (prevent FD leaks to child processes)
        // MFD_ALLOW_SEALING: Allow sealing to prevent resizing
        memfd_ = memfd_create(memfd_name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
        if (memfd_ < 0) {
            return Result<void>::FromError(MakeErrorCode(ComErrc::kInternal, 0));
        }

        // Step 2: Set shared memory size (256KB = 1024 slots × 256 bytes)
        if (ftruncate(memfd_, RegistryConfig::REGISTRY_SIZE) < 0) {
            close(memfd_);
            memfd_ = -1;
            return Result<void>::FromError(MakeErrorCode(ComErrc::kInternal, 0));
        }

        // Step 3: Map shared memory to process address space
        void* addr = mmap(nullptr,
                          RegistryConfig::REGISTRY_SIZE,
                          PROT_READ | PROT_WRITE,
                          MAP_SHARED,
                          memfd_,
                          0);
        
        if (addr == MAP_FAILED) {
            close(memfd_);
            memfd_ = -1;
            return Result<void>::FromError(MakeErrorCode(ComErrc::kInternal, 0));
        }

        slots_ = static_cast<ServiceSlot*>(addr);

        // Step 4: Initialize all slots to IDLE state (first-time initialization only)
        // Check if slot 1 is already initialized (as a sentinel)
        if (slots_[1].sequence.load(std::memory_order_relaxed) == 0 &&
            slots_[1].service_id == 0) {
            // First-time initialization
            for (uint32_t i = 0; i < RegistryConfig::MAX_SLOTS; ++i) {
                slots_[i] = ServiceSlot();  // Reset to default state
            }
        }

        // Step 5: Seal the memory to prevent resizing (security hardening)
        // F_SEAL_SHRINK: Prevent shrinking
        // F_SEAL_GROW: Prevent growing
        // F_SEAL_SEAL: Prevent removing seals
        if (fcntl(memfd_, F_ADD_SEALS, RegistryConfig::SEALING_FLAGS) < 0) {
            // Sealing failed, but continue (non-fatal, just a security feature)
            // In production, you might want to log this as a warning
        }

        return Result<void>::FromValue();  // Success
    }

    Result<void> SingleRegistry::InitializeFromSocket(const String& socket_path) noexcept
    {
        if (IsInitialized()) {
            return Result<void>::FromValue();
        }

        // Step 1: Receive memfd FD from server via UDS socket
        auto fd_result = receiveMemfdFromSocket(socket_path);
        if (!fd_result.HasValue()) {
            return Result<void>::FromError(fd_result.Error());
        }

        memfd_ = fd_result.Value();

        // Step 2: Map shared memory to process address space
        void* addr = mmap(nullptr,
                          RegistryConfig::REGISTRY_SIZE,
                          PROT_READ | PROT_WRITE,
                          MAP_SHARED,
                          memfd_,
                          0);
        
        if (addr == MAP_FAILED) {
            close(memfd_);
            memfd_ = -1;
            return Result<void>::FromError(MakeErrorCode(ComErrc::kSharedMemoryMappingFailed, 0));
        }

        slots_ = static_cast<ServiceSlot*>(addr);

        // Note: No slot initialization needed - server already initialized
        // Note: No sealing needed - server already sealed
        return Result<void>::FromValue();
    }

    Result<int> SingleRegistry::receiveMemfdFromSocket(const String& socket_path) noexcept
    {
        // Step 1: Create Unix domain socket
        int sock_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (sock_fd < 0) {
            return Result<int>::FromError(MakeErrorCode(ComErrc::kSocketCreationFailed, errno));
        }

        // Step 2: Connect to server
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(sock_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
            close(sock_fd);
            return Result<int>::FromError(MakeErrorCode(ComErrc::kSocketConnectFailed, errno));
        }

        // Step 3: Receive message with FD
        struct msghdr msg{};
        struct iovec iov{};
        char ctrl_buf[CMSG_SPACE(sizeof(int))];
        char payload;

        iov.iov_base = &payload;
        iov.iov_len = 1;

        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = ctrl_buf;
        msg.msg_controllen = sizeof(ctrl_buf);

        ssize_t received = recvmsg(sock_fd, &msg, 0);
        close(sock_fd);

        if (received <= 0) {
            return Result<int>::FromError(MakeErrorCode(ComErrc::kFdReceiveFailed, errno));
        }

        // Step 4: Extract memfd FD from control message
        struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
        if (cmsg == nullptr || 
            cmsg->cmsg_level != SOL_SOCKET || 
            cmsg->cmsg_type != SCM_RIGHTS) {
            return Result<int>::FromError(MakeErrorCode(ComErrc::kFdReceiveFailed, 0));
        }

        int memfd;
        memcpy(&memfd, CMSG_DATA(cmsg), sizeof(int));

        return Result<int>::FromValue(memfd);
    }

    void SingleRegistry::Cleanup() noexcept
    {
        if (slots_ != nullptr) {
            munmap(slots_, RegistryConfig::REGISTRY_SIZE);
            slots_ = nullptr;
        }

        if (memfd_ >= 0) {
            close(memfd_);
            memfd_ = -1;
        }
        // Note: No shm_unlink needed - memfd is anonymous and auto-cleaned
    }

    Result<void> SingleRegistry::RegisterService(
        uint32_t slot_index,
        uint64_t service_id,
        uint64_t instance_id,
        uint32_t major_version,
        uint32_t minor_version,
        const char* binding_type,
        const char* endpoint) noexcept
    {
        if (!IsInitialized()) {
            return Result<void>::FromError(MakeErrorCode(ComErrc::kNotInitialized, 0));
        }

        if (!IsValidSlotIndex(slot_index)) {
            return Result<void>::FromError(MakeErrorCode(ComErrc::kInvalidArgument, 0));
        }

        ServiceSlot& slot = slots_[slot_index];

        // Check if slot is already occupied
        if (slot.IsActive()) {
            return Result<void>::FromError(MakeErrorCode(ComErrc::kServiceNotOffered, 0));
        }

        // Write service information with seqlock protection
        {
            SeqLockWriter writer(slot.sequence);
            
            slot.service_id = service_id;
            slot.instance_id = instance_id;
            slot.major_version = major_version;
            slot.minor_version = minor_version;
            
            // Copy binding type (max 15 chars + null terminator)
            std::strncpy(slot.binding_type, binding_type, sizeof(slot.binding_type) - 1);
            slot.binding_type[sizeof(slot.binding_type) - 1] = '\0';
            
            // Copy endpoint (max 79 chars + null terminator)
            std::strncpy(slot.endpoint, endpoint, sizeof(slot.endpoint) - 1);
            slot.endpoint[sizeof(slot.endpoint) - 1] = '\0';
            
            // Set initial heartbeat
            auto now = steady_clock::now();
            slot.last_heartbeat_ns = duration_cast<nanoseconds>(now.time_since_epoch()).count();
            slot.heartbeat_interval_ms = 100;  // Default: 100ms
            
            slot.owner_pid = getpid();
            slot.status = static_cast<uint32_t>(SlotStatus::ACTIVE);
        }

        return Result<void>::FromValue();
    }

    Result<void> SingleRegistry::UnregisterService(uint32_t slot_index) noexcept
    {
        if (!IsInitialized()) {
            return Result<void>::FromError(MakeErrorCode(ComErrc::kNotInitialized, 0));
        }

        if (!IsValidSlotIndex(slot_index)) {
            return Result<void>::FromError(MakeErrorCode(ComErrc::kInvalidArgument, 0));
        }

        ServiceSlot& slot = slots_[slot_index];

        // Reset slot with seqlock protection
        {
            SeqLockWriter writer(slot.sequence);
            slot.Reset();
        }

        return Result<void>::FromValue();
    }

    Optional<ServiceSlot> SingleRegistry::FindService(uint64_t service_id) const noexcept
    {
        if (!IsInitialized()) {
            return Optional<ServiceSlot>{};
        }

        // Calculate slot index using fixed mapping
        uint32_t slot_index = static_cast<uint32_t>(service_id & 1023);
        
        if (!IsValidSlotIndex(slot_index)) {
            return Optional<ServiceSlot>{};
        }

        const ServiceSlot& slot = slots_[slot_index];

        // Use seqlock to read slot atomically
        auto opt_slot = SeqLockReader::Read(slot, [service_id](const ServiceSlot& s) {
            // Verify service ID matches and slot is active
            if (s.service_id == service_id && s.IsActive()) {
                return s;  // Return the slot
            }
            return ServiceSlot{};  // Return empty slot (service_id == 0)
        });

        // Filter out empty slots (service_id == 0 means not found)
        if (opt_slot.has_value() && opt_slot.value().service_id == 0) {
            return Optional<ServiceSlot>{};
        }

        return opt_slot;
    }

    Optional<ServiceSlot> SingleRegistry::ReadSlot(uint32_t slot_index) const noexcept
    {
        if (!IsInitialized() || !IsValidSlotIndex(slot_index)) {
            return Optional<ServiceSlot>{};
        }

        const ServiceSlot& slot = slots_[slot_index];
        return SeqLockReader::ReadSlot(slot);
    }

    Result<void> SingleRegistry::UpdateHeartbeat(uint32_t slot_index, uint64_t timestamp_ns) noexcept
    {
        if (!IsInitialized()) {
            return Result<void>::FromError(MakeErrorCode(ComErrc::kNotInitialized, 0));
        }

        if (!IsValidSlotIndex(slot_index)) {
            return Result<void>::FromError(MakeErrorCode(ComErrc::kInvalidArgument, 0));
        }

        ServiceSlot& slot = slots_[slot_index];

        // Update heartbeat with seqlock protection
        {
            SeqLockWriter writer(slot.sequence);
            slot.last_heartbeat_ns = timestamp_ns;
        }

        return Result<void>::FromValue();
    }

    // ========================================================================
    // SharedMemoryRegistry Implementation
    // ========================================================================

    Result<void> SharedMemoryRegistry::Initialize() noexcept
    {
        // Initialize QM registry (QM + ASIL-A/B services)
        auto qm_result = qm_registry_.Initialize();
        if (qm_result.HasValue() == false) {
            return qm_result;
        }

        // Initialize ASIL registry (ASIL-C/D services)
        auto asil_result = asil_registry_.Initialize();
        if (asil_result.HasValue() == false) {
            return asil_result;
        }

        return Result<void>::FromValue();
    }

    Result<void> SharedMemoryRegistry::InitializeFromSocket(
        const String& qm_socket_path,
        const String& asil_socket_path) noexcept
    {
        // Initialize QM registry from systemd socket
        auto qm_result = qm_registry_.InitializeFromSocket(qm_socket_path);
        if (!qm_result.HasValue()) {
            return qm_result;
        }

        // Initialize ASIL registry from systemd socket
        auto asil_result = asil_registry_.InitializeFromSocket(asil_socket_path);
        if (!asil_result.HasValue()) {
            return asil_result;
        }

        return Result<void>::FromValue();
    }


    Result<void> SharedMemoryRegistry::RegisterService(
        uint64_t service_id,
        uint64_t instance_id,
        uint32_t major_version,
        uint32_t minor_version,
        const char* binding_type,
        const char* endpoint) noexcept
    {
        // Calculate slot index
        uint32_t slot_index = CalculateSlot(service_id);
        if (slot_index == 0) {
            return Result<void>::FromError(MakeErrorCode(ComErrc::kInvalidArgument, 0));
        }

        // Select registry based on service ID
        RegistryType reg_type = SelectRegistry(service_id);

        if (reg_type == RegistryType::BOTH) {
            // Broadcast service: register in both QM and ASIL registries
            auto qm_result = qm_registry_.RegisterService(
                slot_index, service_id, instance_id, 
                major_version, minor_version, binding_type, endpoint);
            
            auto asil_result = asil_registry_.RegisterService(
                slot_index, service_id, instance_id, 
                major_version, minor_version, binding_type, endpoint);
            
            // Return first error if any
            if (qm_result.HasValue() == false) {
                return qm_result;
            }
            return asil_result;
        } else if (reg_type == RegistryType::ASIL) {
            // ASIL-C/D service
            return asil_registry_.RegisterService(
                slot_index, service_id, instance_id, 
                major_version, minor_version, binding_type, endpoint);
        } else {
            // QM + ASIL-A/B service
            return qm_registry_.RegisterService(
                slot_index, service_id, instance_id, 
                major_version, minor_version, binding_type, endpoint);
        }
    }

    Result<void> SharedMemoryRegistry::UnregisterService(uint64_t service_id) noexcept
    {
        uint32_t slot_index = CalculateSlot(service_id);
        if (slot_index == 0) {
            return Result<void>::FromError(MakeErrorCode(ComErrc::kInvalidArgument, 0));
        }

        RegistryType reg_type = SelectRegistry(service_id);

        if (reg_type == RegistryType::BOTH) {
            qm_registry_.UnregisterService(slot_index);
            return asil_registry_.UnregisterService(slot_index);
        } else if (reg_type == RegistryType::ASIL) {
            return asil_registry_.UnregisterService(slot_index);
        } else {
            return qm_registry_.UnregisterService(slot_index);
        }
    }

    Optional<ServiceSlot> SharedMemoryRegistry::FindService(uint64_t service_id) const noexcept
    {
        RegistryType reg_type = SelectRegistry(service_id);

        if (reg_type == RegistryType::ASIL) {
            return asil_registry_.FindService(service_id);
        } else {
            // QM or BOTH: search QM registry first
            return qm_registry_.FindService(service_id);
        }
    }

    Result<void> SharedMemoryRegistry::UpdateHeartbeat(uint64_t service_id, uint64_t timestamp_ns) noexcept
    {
        uint32_t slot_index = CalculateSlot(service_id);
        if (slot_index == 0) {
            return Result<void>::FromError(MakeErrorCode(ComErrc::kInvalidArgument, 0));
        }

        RegistryType reg_type = SelectRegistry(service_id);

        if (reg_type == RegistryType::BOTH) {
            qm_registry_.UpdateHeartbeat(slot_index, timestamp_ns);
            return asil_registry_.UpdateHeartbeat(slot_index, timestamp_ns);
        } else if (reg_type == RegistryType::ASIL) {
            return asil_registry_.UpdateHeartbeat(slot_index, timestamp_ns);
        } else {
            return qm_registry_.UpdateHeartbeat(slot_index, timestamp_ns);
        }
    }

} // namespace registry
} // namespace com
} // namespace lap
