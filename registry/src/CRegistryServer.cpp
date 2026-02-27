/**
 * @file        CRegistryServer.cpp
 * @author      LightAP Development Team
 * @brief       Registry initialization server implementation
 * @date        2026/02/06
 * @details     Implements Phase 2 of SERVICE_DISCOVERY_ARCHITECTURE.md §2.2.2:
 *              - Creates anonymous memfd via memfd_create()
 *              - Initializes 1024 service slots (256 bytes each)
 *              - Listens on Unix Domain Socket
 *              - Distributes memfd FD to clients via SCM_RIGHTS
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 Compliance:
 *              - SWS_CM_00001: Service discovery infrastructure
 *              - SWS_CM_00110: Registry lifecycle management
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §2.2.2
 *              memfd_create(2), unix(7), cmsg(3)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/20  <td>1.0      <td>LightAP Team    <td>Initial implementation
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style optimization per code_rules.md
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CRegistryServer.hpp"
#include "ComTypes.hpp"

// ==================== Standard Library Headers ====================
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
#if !defined( __GLIBC__ ) || __GLIBC__ < 2 || ( __GLIBC__ == 2 && __GLIBC_MINOR__ < 27 )
    #include <sys/syscall.h>
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
     * @brief Fallback memfd_create for old glibc (< 2.27)
     * @param name  Memfd name (for debugging, visible in /proc/PID/fd/)
     * @param flags MFD_CLOEXEC | MFD_ALLOW_SEALING
     * @return File descriptor or -1 on error (errno set)
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
    using lap::com::MakeErrorCode;
    using lap::com::ComErrc;

    CRegistryServer::CRegistryServer( RegistryType registryType, const String& socketPath )
        : m_registryType( registryType )
        , m_strSocketPath( socketPath )
        , m_memfd( -1 )
        , m_socketFd( -1 )
        , m_pSlots( nullptr )
        , m_bRunning( false )
    {
    }

    CRegistryServer::~CRegistryServer()
    {
        Shutdown();

        // Cleanup mapped memory
        if ( m_pSlots != nullptr )
        {
            munmap( m_pSlots, RegistryConfig::kRegistrySize );
            m_pSlots = nullptr;
        }

        // Close memfd
        if ( m_memfd >= 0 )
        {
            close( m_memfd );
            m_memfd = -1;
        }

        // Close and cleanup socket
        if ( m_socketFd >= 0 )
        {
            close( m_socketFd );
            m_socketFd = -1;
            unlink( m_strSocketPath.c_str() );  // Remove socket file
        }
    }

    Result< void > CRegistryServer::Initialize() noexcept
    {
        // Create and initialize memfd
        auto memfdResult = createMemfd();
        if ( !memfdResult.HasValue() )
        {
            return memfdResult;
        }

        const char* registryTypeStr = ( m_registryType == RegistryType::kQM ) ? "QM" : "ASIL";
        LAP_COM_LOG_INFO << "CRegistryServer: Initialized " << registryTypeStr
                         << " registry, memfd=" << m_memfd
                         << ", size=" << RegistryConfig::kRegistrySize << " bytes";

        return Result< void > ();
    }

    Result< void > CRegistryServer::createMemfd() noexcept
    {
        // Step 1: Create anonymous memfd
        const char* memfdName = ( m_registryType == RegistryType::kQM )
                                ? RegistryConfig::kQmMemfdName
                                : RegistryConfig::kAsilMemfdName;

        m_memfd = memfd_create( memfdName, MFD_CLOEXEC | MFD_ALLOW_SEALING );
        if ( m_memfd < 0 )
        {
            LAP_COM_LOG_ERROR << "memfd_create(\"" << memfdName << "\") failed: " << strerror( errno );
            return Result< void >::FromError( MakeErrorCode( ComErrc::kSharedMemoryCreationFailed ) );
        }

        // Step 2: Resize to 256KB (1024 slots x 256 bytes)
        if ( ftruncate( m_memfd, static_cast< off_t > ( RegistryConfig::kRegistrySize ) ) != 0 )
        {
            LAP_COM_LOG_ERROR << "ftruncate(" << RegistryConfig::kRegistrySize << ") failed: "
                              << strerror( errno );
            close( m_memfd );
            m_memfd = -1;
            return Result< void >::FromError( MakeErrorCode( ComErrc::kSharedMemoryCreationFailed ) );
        }

        // Step 3: Map to process address space
        void* addr = mmap( nullptr, RegistryConfig::kRegistrySize,
                           PROT_READ | PROT_WRITE, MAP_SHARED, m_memfd, 0 );
        if ( addr == MAP_FAILED )
        {
            LAP_COM_LOG_ERROR << "mmap(" << RegistryConfig::kRegistrySize << ") failed: "
                              << strerror( errno );
            close( m_memfd );
            m_memfd = -1;
            return Result< void >::FromError( MakeErrorCode( ComErrc::kSharedMemoryMappingFailed ) );
        }

        m_pSlots = static_cast< ServiceSlot* > ( addr );

        // Step 4: Initialize all slots to kIdle state
        for ( UInt32 i = 0; i < RegistryConfig::kMaxSlots; ++i )
        {
            new ( &m_pSlots[i] ) ServiceSlot();
        }

        // Step 5: Seal memfd for security (prevents resize/modification)
        if ( fcntl( m_memfd, F_ADD_SEALS, RegistryConfig::kSealingFlags ) != 0 )
        {
            LAP_COM_LOG_WARN << "fcntl(F_ADD_SEALS) failed: " << strerror( errno )
                             << " (non-critical, continuing)";
        }

        return Result< void > ();
    }

    Result< void > CRegistryServer::createSocket( Bool useSystemd ) noexcept
    {
        // TODO: Support systemd socket activation (SD_LISTEN_FDS_START)
        UNUSED( useSystemd );

        // Step 1: Create Unix Domain Socket
        m_socketFd = socket( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0 );
        if ( m_socketFd < 0 )
        {
            LAP_COM_LOG_ERROR << "socket(AF_UNIX) failed: " << strerror( errno );
            return Result< void >::FromError( MakeErrorCode( ComErrc::kSocketCreationFailed ) );
        }

        // Step 2: Remove old socket file if exists
        unlink( m_strSocketPath.c_str() );

        // Step 3: Bind to socket path
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy( addr.sun_path, m_strSocketPath.c_str(), sizeof( addr.sun_path ) - 1 );

        if ( bind( m_socketFd, reinterpret_cast< struct sockaddr* > ( &addr ), sizeof( addr ) ) != 0 )
        {
            LAP_COM_LOG_ERROR << "bind(\"" << m_strSocketPath << "\") failed: " << strerror( errno );
            close( m_socketFd );
            m_socketFd = -1;
            return Result< void >::FromError( MakeErrorCode( ComErrc::kSocketCreationFailed ) );
        }

        // Step 4: Set socket permissions based on registry type
        mode_t mode = ( m_registryType == RegistryType::kQM )
                      ? RegistryConfig::kQmPermissions   // 0666: all processes
                      : RegistryConfig::kAsilPermissions; // 0640: controlled access

        if ( chmod( m_strSocketPath.c_str(), mode ) != 0 )
        {
            LAP_COM_LOG_WARN << "chmod() failed: " << strerror( errno ) << " (non-critical)";
        }

        // Step 5: Listen for client connections (backlog=128)
        if ( listen( m_socketFd, 128 ) != 0 )
        {
            LAP_COM_LOG_ERROR << "listen() failed: " << strerror( errno );
            close( m_socketFd );
            m_socketFd = -1;
            return Result< void >::FromError( MakeErrorCode( ComErrc::kSocketCreationFailed ) );
        }

        LAP_COM_LOG_INFO << "Listening on: " << m_strSocketPath;

        return Result< void > ();
    }

    Result< void > CRegistryServer::Run( Bool useSystemdSocket ) noexcept
    {
        // Create and bind socket
        auto socketResult = createSocket( useSystemdSocket );
        if ( !socketResult.HasValue() )
        {
            return socketResult;
        }

        // Start accept loop
        m_bRunning.store( true, std::memory_order_release );
        LAP_COM_LOG_INFO << "Registry server started, waiting for client connections...";

        UInt64 clientCount = 0;

        while ( m_bRunning.load( std::memory_order_acquire ) )
        {
            struct sockaddr_un clientAddr{};
            socklen_t clientLen = sizeof( clientAddr );

            Int32 clientFd = accept( m_socketFd,
                                     reinterpret_cast< struct sockaddr* > ( &clientAddr ),
                                     &clientLen );

            if ( clientFd < 0 )
            {
                if ( errno == EINTR || errno == EBADF )
                {
                    // Interrupted or socket closed (shutdown triggered)
                    continue;
                }
                LAP_COM_LOG_ERROR << "accept() failed: " << strerror( errno );
                continue;
            }

            ++clientCount;
            LAP_COM_LOG_DEBUG << "Client #" << clientCount << " connected, fd=" << clientFd;

            // Handle client request (send memfd)
            handleClient( clientFd );

            close( clientFd );
        }

        LAP_COM_LOG_INFO << "Registry server stopped, served " << clientCount << " clients";
        return Result< void > ();
    }

    void CRegistryServer::Shutdown() noexcept
    {
        // Set shutdown flag (thread-safe)
        Bool wasRunning = m_bRunning.exchange( false, std::memory_order_acq_rel );

        if ( !wasRunning )
        {
            return;  // Already shut down
        }

        LAP_COM_LOG_INFO << "Shutting down registry server...";

        // Close socket to unblock accept()
        if ( m_socketFd >= 0 )
        {
            shutdown( m_socketFd, SHUT_RDWR );
        }
    }

    Result< void > CRegistryServer::handleClient( Int32 clientFd ) noexcept
    {
        auto result = sendMemfdToClient( clientFd );

        if ( result.HasValue() )
        {
            LAP_COM_LOG_DEBUG << "Successfully sent memfd to client, fd=" << clientFd;
        }
        else
        {
            LAP_COM_LOG_ERROR << "Failed to send memfd to client: " << result.Error().Message();
        }

        return result;
    }

    Result< void > CRegistryServer::sendMemfdToClient( Int32 clientFd ) noexcept
    {
        // Prepare message with file descriptor passing (SCM_RIGHTS)
        struct msghdr msg{};
        struct iovec iov{};
        char ctrlBuf[CMSG_SPACE( sizeof( Int32 ) )];
        char payload = 'R';  // Registry ready marker

        // Setup payload (1 byte marker)
        iov.iov_base = &payload;
        iov.iov_len = 1;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        // Setup control message for FD passing
        msg.msg_control = ctrlBuf;
        msg.msg_controllen = sizeof( ctrlBuf );

        struct cmsghdr* cmsg = CMSG_FIRSTHDR( &msg );
        if ( cmsg == nullptr )
        {
            LAP_COM_LOG_ERROR << "CMSG_FIRSTHDR returned nullptr";
            return Result< void >::FromError( MakeErrorCode( ComErrc::kFdPassingFailed ) );
        }

        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN( sizeof( Int32 ) );
        memcpy( CMSG_DATA( cmsg ), &m_memfd, sizeof( Int32 ) );

        // Send message with file descriptor
        ssize_t sent = sendmsg( clientFd, &msg, 0 );
        if ( sent <= 0 )
        {
            LAP_COM_LOG_ERROR << "sendmsg() failed: " << strerror( errno )
                              << " (sent=" << sent << ")";
            return Result< void >::FromError( MakeErrorCode( ComErrc::kFdPassingFailed ) );
        }

        return Result< void > ();
    }

} // namespace registry
} // namespace com
} // namespace lap
