/**
 * @file        RegistryInitializer.cpp
 * @author      LightAP Development Team
 * @brief       Registry initialization server implementation
 * @date        2025-11-20
 * @details     Implements Phase 2 of SERVICE_DISCOVERY_ARCHITECTURE.md §2.2.2:
 *              - Creates anonymous memfd via memfd_create()
 *              - Initializes 1024 service slots (256 bytes each)
 *              - Listens on Unix Domain Socket
 *              - Distributes memfd FD to clients via SCM_RIGHTS
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R24-11 Compliance:
 *              - SWS_CM_00001: Service discovery infrastructure
 *              - SWS_CM_00110: Registry lifecycle management
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §2.2.2
 *              memfd_create(2), unix(7), cmsg(3)
 * @version     1.0
 */
#include "RegistryInitializer.hpp"
#include "ComTypes.hpp"
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

// memfd_create support (Linux 3.17+)
// System header should provide this, but define constants if missing
#ifndef MFD_CLOEXEC
    #define MFD_CLOEXEC 0x0001U
#endif
#ifndef MFD_ALLOW_SEALING
    #define MFD_ALLOW_SEALING 0x0002U
#endif

// Ensure memfd_create is available
// Most modern systems provide it via <sys/mman.h>, otherwise use syscall
#if !defined(__GLIBC__) || __GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 27)
    #include <sys/syscall.h>
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
     * @brief Fallback memfd_create for old glibc (< 2.27)
     * @param name Memfd name (for debugging, visible in /proc/PID/fd/)
     * @param flags MFD_CLOEXEC | MFD_ALLOW_SEALING
     * @return File descriptor or -1 on error (errno set)
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
    using lap::com::MakeErrorCode;
    using lap::com::ComErrc;

    RegistryInitializer::RegistryInitializer(RegistryType registry_type, const String& socket_path)
        : registry_type_(registry_type)
        , socket_path_(socket_path)
        , memfd_(-1)
        , socket_fd_(-1)
        , slots_(nullptr)
        , running_(false)
    {
    }

    RegistryInitializer::~RegistryInitializer() noexcept
    {
        Shutdown();
        
        // Cleanup mapped memory
        if (slots_ != nullptr)
        {
            munmap(slots_, RegistryConfig::REGISTRY_SIZE);
            slots_ = nullptr;
        }
        
        // Close memfd
        if (memfd_ >= 0)
        {
            close(memfd_);
            memfd_ = -1;
        }
        
        // Close and cleanup socket
        if (socket_fd_ >= 0)
        {
            close(socket_fd_);
            socket_fd_ = -1;
            unlink(socket_path_.c_str());  // Remove socket file
        }
    }

    Result<void> RegistryInitializer::Initialize() noexcept
    {
        // Create and initialize memfd
        auto memfd_result = createMemfd();
        if (!memfd_result.HasValue())
        {
            return memfd_result;
        }
        
        const char* registry_type_str = (registry_type_ == RegistryType::QM) ? "QM" : "ASIL";
        LAP_COM_LOG_INFO << "RegistryInitializer: Initialized " << registry_type_str 
                         << " registry, memfd=" << memfd_ 
                         << ", size=" << RegistryConfig::REGISTRY_SIZE << " bytes";
        
        return Result<void>();
    }

    Result<void> RegistryInitializer::createMemfd() noexcept
    {
        // Step 1: Create anonymous memfd
        const char* memfd_name = (registry_type_ == RegistryType::QM) 
                                 ? RegistryConfig::QM_MEMFD_NAME 
                                 : RegistryConfig::ASIL_MEMFD_NAME;
        
        memfd_ = memfd_create(memfd_name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
        if (memfd_ < 0)
        {
            LAP_COM_LOG_ERROR << "memfd_create(\"" << memfd_name << "\") failed: " << strerror(errno);
            return Result<void>::FromError(MakeErrorCode(ComErrc::kMemfdCreateFailed));
        }
        
        // Step 2: Resize to 256KB (1024 slots × 256 bytes)
        if (ftruncate(memfd_, RegistryConfig::REGISTRY_SIZE) != 0)
        {
            LAP_COM_LOG_ERROR << "ftruncate(" << RegistryConfig::REGISTRY_SIZE << ") failed: " << strerror(errno);
            close(memfd_);
            memfd_ = -1;
            return Result<void>::FromError(MakeErrorCode(ComErrc::kSharedMemoryResizeFailed));
        }
        
        // Step 3: Map to process address space
        void* addr = mmap(nullptr, RegistryConfig::REGISTRY_SIZE, 
                         PROT_READ | PROT_WRITE, MAP_SHARED, memfd_, 0);
        if (addr == MAP_FAILED)
        {
            LAP_COM_LOG_ERROR << "mmap(" << RegistryConfig::REGISTRY_SIZE << ") failed: " << strerror(errno);
            close(memfd_);
            memfd_ = -1;
            return Result<void>::FromError(MakeErrorCode(ComErrc::kSharedMemoryMappingFailed));
        }
        
        slots_ = static_cast<ServiceSlot*>(addr);
        
        // Step 4: Initialize all slots to IDLE state
        for (uint32_t i = 0; i < RegistryConfig::MAX_SLOTS; ++i)
        {
            new (&slots_[i]) ServiceSlot();
        }
        
        // Step 5: Seal memfd for security (prevents resize/modification)
        if (fcntl(memfd_, F_ADD_SEALS, RegistryConfig::SEALING_FLAGS) != 0)
        {
            LAP_COM_LOG_WARN << "fcntl(F_ADD_SEALS) failed: " << strerror(errno) 
                             << " (non-critical, continuing)";
        }
        
        return Result<void>();
    }

    Result<void> RegistryInitializer::createSocket(bool use_systemd_socket) noexcept
    {
        // TODO: Support systemd socket activation (SD_LISTEN_FDS_START)
        (void)use_systemd_socket;  // Unused for now
        
        // Step 1: Create Unix Domain Socket
        socket_fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (socket_fd_ < 0)
        {
            LAP_COM_LOG_ERROR << "socket(AF_UNIX) failed: " << strerror(errno);
            return Result<void>::FromError(MakeErrorCode(ComErrc::kSocketCreationFailed));
        }
        
        // Step 2: Remove old socket file if exists
        unlink(socket_path_.c_str());
        
        // Step 3: Bind to socket path
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
        
        if (bind(socket_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            LAP_COM_LOG_ERROR << "bind(\"" << socket_path_ << "\") failed: " << strerror(errno);
            close(socket_fd_);
            socket_fd_ = -1;
            return Result<void>::FromError(MakeErrorCode(ComErrc::kSocketBindFailed));
        }
        
        // Step 4: Set socket permissions based on registry type
        mode_t mode = (registry_type_ == RegistryType::QM) 
                      ? RegistryConfig::QM_PERMISSIONS   // 0666: all processes
                      : RegistryConfig::ASIL_PERMISSIONS; // 0640: controlled access
        
        if (chmod(socket_path_.c_str(), mode) != 0)
        {
            LAP_COM_LOG_WARN << "chmod() failed: " << strerror(errno) << " (non-critical)";
        }
        
        // Step 5: Listen for client connections (backlog=128)
        if (listen(socket_fd_, 128) != 0)
        {
            LAP_COM_LOG_ERROR << "listen() failed: " << strerror(errno);
            close(socket_fd_);
            socket_fd_ = -1;
            return Result<void>::FromError(MakeErrorCode(ComErrc::kSocketListenFailed));
        }
        
        LAP_COM_LOG_INFO << "Listening on: " << socket_path_;
        
        return Result<void>();
    }

    Result<void> RegistryInitializer::Run(bool use_systemd_socket) noexcept
    {
        // Create and bind socket
        auto socket_result = createSocket(use_systemd_socket);
        if (!socket_result.HasValue())
        {
            return socket_result;
        }
        
        // Start accept loop
        running_.store(true, std::memory_order_release);
        LAP_COM_LOG_INFO << "Registry server started, waiting for client connections...";
        
        uint64_t client_count = 0;
        
        while (running_.load(std::memory_order_acquire))
        {
            struct sockaddr_un client_addr{};
            socklen_t client_len = sizeof(client_addr);
            
            int client_fd = accept(socket_fd_, 
                                  reinterpret_cast<struct sockaddr*>(&client_addr), 
                                  &client_len);
            
            if (client_fd < 0)
            {
                if (errno == EINTR || errno == EBADF)
                {
                    // Interrupted or socket closed (shutdown triggered)
                    continue;
                }
                LAP_COM_LOG_ERROR << "accept() failed: " << strerror(errno);
                continue;
            }
            
            ++client_count;
            LAP_COM_LOG_DEBUG << "Client #" << client_count << " connected, fd=" << client_fd;
            
            // Handle client request (send memfd)
            handleClient(client_fd);
            
            close(client_fd);
        }
        
        LAP_COM_LOG_INFO << "Registry server stopped, served " << client_count << " clients";
        return Result<void>();
    }

    void RegistryInitializer::Shutdown() noexcept
    {
        // Set shutdown flag (thread-safe)
        bool was_running = running_.exchange(false, std::memory_order_acq_rel);
        
        if (!was_running)
        {
            return;  // Already shut down
        }
        
        LAP_COM_LOG_INFO << "Shutting down registry server...";
        
        // Close socket to unblock accept()
        if (socket_fd_ >= 0)
        {
            shutdown(socket_fd_, SHUT_RDWR);
        }
    }

    Result<void> RegistryInitializer::handleClient(int client_fd) noexcept
    {
        auto result = sendMemfdToClient(client_fd);
        
        if (result.HasValue())
        {
            LAP_COM_LOG_DEBUG << "Successfully sent memfd to client, fd=" << client_fd;
        }
        else
        {
            LAP_COM_LOG_ERROR << "Failed to send memfd to client: " << result.Error().Message();
        }
        
        return result;
    }

    Result<void> RegistryInitializer::sendMemfdToClient(int client_fd) noexcept
    {
        // Prepare message with file descriptor passing (SCM_RIGHTS)
        struct msghdr msg{};
        struct iovec iov{};
        char ctrl_buf[CMSG_SPACE(sizeof(int))];
        char payload = 'R';  // Registry ready marker
        
        // Setup payload (1 byte marker)
        iov.iov_base = &payload;
        iov.iov_len = 1;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        
        // Setup control message for FD passing
        msg.msg_control = ctrl_buf;
        msg.msg_controllen = sizeof(ctrl_buf);
        
        struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
        if (cmsg == nullptr)
        {
            LAP_COM_LOG_ERROR << "CMSG_FIRSTHDR returned nullptr";
            return Result<void>::FromError(MakeErrorCode(ComErrc::kFdPassingFailed));
        }
        
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cmsg), &memfd_, sizeof(int));
        
        // Send message with file descriptor
        ssize_t sent = sendmsg(client_fd, &msg, 0);
        if (sent <= 0)
        {
            LAP_COM_LOG_ERROR << "sendmsg() failed: " << strerror(errno) 
                             << " (sent=" << sent << ")";
            return Result<void>::FromError(MakeErrorCode(ComErrc::kFdPassingFailed));
        }
        
        return Result<void>();
    }

}  // namespace registry
}  // namespace com
}  // namespace lap
