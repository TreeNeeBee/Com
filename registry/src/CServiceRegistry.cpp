/**
 * @file        CServiceRegistry.cpp
 * @author      LightAP Development Team
 * @brief       Single registry manager implementation (IPC-based v2.0)
 * @date        2026/02/06
 * @copyright   Copyright (c) 2026
 * @note        Architecture changes (v2.0):
 *              - Shared memory: READ-ONLY for clients
 *              - Modifications: Via IPC request/response (MPSC REQ + SPMC RESP)
 *              - CRegistryDispatcher: Central service managing all writes
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §2.4 (IPC-based Registry)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/05  <td>1.0      <td>LightAP Team    <td>Initial implementation
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style optimization per code_rules.md
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "ComTypes.hpp"
#include "CServiceRegistry.hpp"

// ==================== Standard Library Headers ====================
#include <chrono>
#include <cerrno>
#include <cstring>
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
#if !defined( __GLIBC__ ) || __GLIBC__ < 2 || ( __GLIBC__ == 2 && __GLIBC_MINOR__ < 27 )
    #ifndef __NR_memfd_create
        #if defined( __x86_64__ )
            #define __NR_memfd_create 319
        #elif defined( __aarch64__ )
            #define __NR_memfd_create 385
        #elif defined( __arm__ )
            #define __NR_memfd_create 356
        #elif defined( __riscv ) && ( __riscv_xlen == 64 )
            #define __NR_memfd_create 279
        #else
            #error "Unsupported architecture for memfd_create"
        #endif
    #endif

    /**
     * @brief Fallback memfd_create for old glibc
     * @param name  Memfd name (visible in /proc/PID/fd/)
     * @param flags MFD_CLOEXEC | MFD_ALLOW_SEALING
     * @return File descriptor or -1 on error
     */
    static inline int memfd_create( const char* name, unsigned int flags )
    {
        return static_cast< int > ( syscall( __NR_memfd_create, name, flags ) );
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

    // ==================== CServiceRegistry Implementation ====================

    Result< void > CServiceRegistry::Initialize() noexcept
    {
        if ( IsInitialized() )
        {
            return Result< void >::FromValue();
        }

        const char* memfdName = getMemfdName();

        // Step 1: Create anonymous shared memory with memfd_create
        // MFD_CLOEXEC: Close-on-exec (prevent FD leaks to child processes)
        // MFD_ALLOW_SEALING: Allow sealing to prevent resizing
        m_memfd = memfd_create( memfdName, MFD_CLOEXEC | MFD_ALLOW_SEALING );
        if ( m_memfd < 0 )
        {
            return Result< void >::FromError( MakeErrorCode( ComErrc::kInternal, 0 ) );
        }

        // Step 2: Set shared memory size (256KB = 1024 slots x 256 bytes)
        if ( ftruncate( m_memfd, static_cast< off_t > ( RegistryConfig::kRegistrySize ) ) < 0 )
        {
            close( m_memfd );
            m_memfd = -1;
            return Result< void >::FromError( MakeErrorCode( ComErrc::kInternal, 0 ) );
        }

        // Step 3: Map shared memory to process address space
        void* addr = mmap( nullptr,
                           RegistryConfig::kRegistrySize,
                           PROT_READ | PROT_WRITE,
                           MAP_SHARED,
                           m_memfd,
                           0 );

        if ( addr == MAP_FAILED )
        {
            close( m_memfd );
            m_memfd = -1;
            return Result< void >::FromError( MakeErrorCode( ComErrc::kInternal, 0 ) );
        }

        m_pSlots = static_cast< ServiceSlot* > ( addr );

        // Step 4: Initialize all slots to kIdle state (first-time initialization only)
        // Check if slot 1 is already initialized (as a sentinel)
        if ( m_pSlots[1].m_status.load( std::memory_order_relaxed ) ==
                static_cast< UInt32 > ( SlotStatus::kIdle ) &&
             m_pSlots[1].m_serviceId == 0 )
        {
            for ( UInt32 i = 0; i < RegistryConfig::kMaxSlots; ++i )
            {
                m_pSlots[i] = ServiceSlot();
            }
        }

        // Step 5: Seal the memory to prevent resizing (security hardening)
        if ( fcntl( m_memfd, F_ADD_SEALS, RegistryConfig::kSealingFlags ) < 0 )
        {
            // Sealing failed, but continue (non-fatal)
        }

        return Result< void >::FromValue();
    }

    Result< void > CServiceRegistry::InitializeFromSocket( const String& socketPath ) noexcept
    {
        if ( IsInitialized() )
        {
            return Result< void >::FromValue();
        }

        auto fdResult = receiveMemfdFromSocket( socketPath );
        if ( !fdResult.HasValue() )
        {
            return Result< void >::FromError( fdResult.Error() );
        }

        m_memfd = fdResult.Value();

        void* addr = mmap( nullptr,
                           RegistryConfig::kRegistrySize,
                           PROT_READ | PROT_WRITE,
                           MAP_SHARED,
                           m_memfd,
                           0 );

        if ( addr == MAP_FAILED )
        {
            close( m_memfd );
            m_memfd = -1;
            return Result< void >::FromError( MakeErrorCode( ComErrc::kSharedMemoryMappingFailed, 0 ) );
        }

        m_pSlots = static_cast< ServiceSlot* > ( addr );
        return Result< void >::FromValue();
    }

    Result< Int32 > CServiceRegistry::receiveMemfdFromSocket( const String& socketPath ) noexcept
    {
        Int32 sockFd = socket( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0 );
        if ( sockFd < 0 )
        {
            return Result< Int32 >::FromError( MakeErrorCode( ComErrc::kSocketCreationFailed, errno ) );
        }

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy( addr.sun_path, socketPath.c_str(), sizeof( addr.sun_path ) - 1 );

        if ( connect( sockFd, reinterpret_cast< struct sockaddr* > ( &addr ), sizeof( addr ) ) != 0 )
        {
            close( sockFd );
            return Result< Int32 >::FromError( MakeErrorCode( ComErrc::kSocketConnectFailed, errno ) );
        }

        struct msghdr msg{};
        struct iovec iov{};
        char ctrlBuf[CMSG_SPACE( sizeof( Int32 ) )];
        char payload;

        iov.iov_base = &payload;
        iov.iov_len = 1;

        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = ctrlBuf;
        msg.msg_controllen = sizeof( ctrlBuf );

        ssize_t received = recvmsg( sockFd, &msg, 0 );
        close( sockFd );

        if ( received <= 0 )
        {
            return Result< Int32 >::FromError( MakeErrorCode( ComErrc::kFdPassingFailed, errno ) );
        }

        struct cmsghdr* cmsg = CMSG_FIRSTHDR( &msg );
        if ( cmsg == nullptr ||
             cmsg->cmsg_level != SOL_SOCKET ||
             cmsg->cmsg_type != SCM_RIGHTS )
        {
            return Result< Int32 >::FromError( MakeErrorCode( ComErrc::kFdPassingFailed, 0 ) );
        }

        Int32 memfd;
        memcpy( &memfd, CMSG_DATA( cmsg ), sizeof( Int32 ) );

        return Result< Int32 >::FromValue( memfd );
    }

    void CServiceRegistry::Cleanup() noexcept
    {
        if ( m_pSlots != nullptr )
        {
            munmap( m_pSlots, RegistryConfig::kRegistrySize );
            m_pSlots = nullptr;
        }

        if ( m_memfd >= 0 )
        {
            close( m_memfd );
            m_memfd = -1;
        }
    }

    Result< void > CServiceRegistry::RegisterService(
        UInt32 slotIndex,
        UInt64 serviceId,
        UInt64 instanceId,
        UInt32 majorVersion,
        UInt32 minorVersion,
        const char* bindingType,
        const char* endpoint ) noexcept
    {
        if ( !IsInitialized() )
        {
            return Result< void >::FromError( MakeErrorCode( ComErrc::kNotInitialized, 0 ) );
        }

        if ( !isValidSlotIndex( slotIndex ) )
        {
            return Result< void >::FromError( MakeErrorCode( ComErrc::kInvalidArgument, 0 ) );
        }

        ServiceSlot& slot = m_pSlots[slotIndex];

        if ( slot.IsActive() )
        {
            return Result< void >::FromError( MakeErrorCode( ComErrc::kServiceNotOffered, 0 ) );
        }

        slot.m_status.store( static_cast< UInt32 > ( SlotStatus::kUnregistering ),
                             std::memory_order_release );

        slot.m_serviceId = serviceId;
        slot.m_instanceId = instanceId;
        slot.m_majorVersion = majorVersion;
        slot.m_minorVersion = minorVersion;

        std::strncpy( slot.m_bindingType, bindingType, sizeof( slot.m_bindingType ) - 1 );
        slot.m_bindingType[sizeof( slot.m_bindingType ) - 1] = '\0';

        std::strncpy( slot.m_endpoint, endpoint, sizeof( slot.m_endpoint ) - 1 );
        slot.m_endpoint[sizeof( slot.m_endpoint ) - 1] = '\0';

        auto now = steady_clock::now();
        slot.m_lastHeartbeatNs = static_cast< UInt64 > (
            duration_cast< nanoseconds > ( now.time_since_epoch() ).count() );
        slot.m_heartbeatIntervalMs = 100;
        slot.m_ownerPid = static_cast< Int32 > ( getpid() );

        slot.m_status.store( static_cast< UInt32 > ( SlotStatus::kActive ),
                             std::memory_order_release );

        return Result< void >::FromValue();
    }

    Result< void > CServiceRegistry::UnregisterService( UInt32 slotIndex ) noexcept
    {
        if ( !IsInitialized() )
        {
            return Result< void >::FromError( MakeErrorCode( ComErrc::kNotInitialized, 0 ) );
        }

        if ( !isValidSlotIndex( slotIndex ) )
        {
            return Result< void >::FromError( MakeErrorCode( ComErrc::kInvalidArgument, 0 ) );
        }

        ServiceSlot& slot = m_pSlots[slotIndex];

        slot.m_status.store( static_cast< UInt32 > ( SlotStatus::kUnregistering ),
                             std::memory_order_release );
        slot.Reset();

        return Result< void >::FromValue();
    }

    Optional< ServiceSlot > CServiceRegistry::FindService( UInt64 serviceId ) const noexcept
    {
        if ( !IsInitialized() )
        {
            return Optional< ServiceSlot > {};
        }

        UInt32 slotIndex = static_cast< UInt32 > ( serviceId & 1023 );
        if ( !isValidSlotIndex( slotIndex ) )
        {
            return Optional< ServiceSlot > {};
        }

        const ServiceSlot& slot = m_pSlots[slotIndex];
        if ( slot.m_status.load( std::memory_order_acquire ) !=
             static_cast< UInt32 > ( SlotStatus::kActive ) )
        {
            return Optional< ServiceSlot > {};
        }

        ServiceSlot snapshot( slot );
        if ( snapshot.m_serviceId != serviceId )
        {
            return Optional< ServiceSlot > {};
        }

        if ( slot.m_status.load( std::memory_order_acquire ) !=
             static_cast< UInt32 > ( SlotStatus::kActive ) )
        {
            return Optional< ServiceSlot > {};
        }

        return Optional< ServiceSlot > ( snapshot );
    }

    Optional< ServiceSlot > CServiceRegistry::ReadSlot( UInt32 slotIndex ) const noexcept
    {
        if ( !IsInitialized() || !isValidSlotIndex( slotIndex ) )
        {
            return Optional< ServiceSlot > {};
        }

        const ServiceSlot& slot = m_pSlots[slotIndex];
        if ( slot.m_status.load( std::memory_order_acquire ) !=
             static_cast< UInt32 > ( SlotStatus::kActive ) )
        {
            return Optional< ServiceSlot > {};
        }

        return Optional< ServiceSlot > ( ServiceSlot( slot ) );
    }

    Result< void > CServiceRegistry::UpdateHeartbeat( UInt32 slotIndex, UInt64 timestampNs ) noexcept
    {
        if ( !IsInitialized() )
        {
            return Result< void >::FromError( MakeErrorCode( ComErrc::kNotInitialized, 0 ) );
        }

        if ( !isValidSlotIndex( slotIndex ) )
        {
            return Result< void >::FromError( MakeErrorCode( ComErrc::kInvalidArgument, 0 ) );
        }

        ServiceSlot& slot = m_pSlots[slotIndex];
        if ( slot.m_status.load( std::memory_order_acquire ) !=
             static_cast< UInt32 > ( SlotStatus::kActive ) )
        {
            return Result< void >::FromError( MakeErrorCode( ComErrc::kServiceNotOffered, 0 ) );
        }

        slot.m_lastHeartbeatNs = timestampNs;
        return Result< void >::FromValue();
    }

} // namespace registry
} // namespace com
} // namespace lap
