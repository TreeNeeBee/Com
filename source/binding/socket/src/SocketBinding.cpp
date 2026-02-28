/**
 * @file        SocketBinding.cpp
 * @author      LightAP Development Team
 * @brief       Socket binding — Unix/TCP socket implementation
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Socket-based IPC transport using TLV framing.
 *              Supports AF_UNIX (default) and AF_INET (TCP) modes.
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Stub facade
 * <tr><td>2026/02/28  <td>2.0      <td>Aii             <td>Unix/TCP socket implementation
 * </table>
 */

#include "SocketBinding.hpp"
#include "ComTypes.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <sstream>
#include <chrono>
#include <algorithm>

namespace lap
{
namespace com
{
namespace binding
{

    // ====================================================================
    // Constructor / Destructor
    // ====================================================================

    SocketBinding::SocketBinding() noexcept
        : m_bInitialized( false )
        , m_iListenFd( -1 )
        , m_iConnFd( -1 )
        , m_bRunning( false )
        , m_iNextSessionId( 1 )
    {
        LAP_COM_LOG_INFO << "SocketBinding instance created";
    }

    SocketBinding::~SocketBinding() noexcept
    {
        static_cast< void >( Shutdown() );
        LAP_COM_LOG_INFO << "SocketBinding instance destroyed";
    }

    // ====================================================================
    // Lifecycle
    // ====================================================================

    Result< void > SocketBinding::Initialize() noexcept
    {
        LockGuard lock( m_mutex );
        if ( m_bInitialized )
        {
            return Result< void >::FromValue();
        }

        if ( m_config.m_bUseTcp )
        {
            // TCP mode
            m_iListenFd = static_cast< Int32 >(
                ::socket( AF_INET, SOCK_STREAM, 0 ) );
        }
        else
        {
            // Unix domain socket mode
            m_iListenFd = static_cast< Int32 >(
                ::socket( AF_UNIX, SOCK_STREAM, 0 ) );
        }

        if ( m_iListenFd < 0 )
        {
            LAP_COM_LOG_ERROR << "SocketBinding: failed to create socket";
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kCommunicationFailure ) );
        }

        int optval = 1;
        ::setsockopt( m_iListenFd, SOL_SOCKET, SO_REUSEADDR,
                       &optval, sizeof( optval ) );

        if ( m_config.m_bUseTcp )
        {
            struct sockaddr_in addr{};
            addr.sin_family      = AF_INET;
            addr.sin_port        = htons( m_config.m_iPort );
            addr.sin_addr.s_addr = ::inet_addr(
                m_config.m_strBindAddress.c_str() );

            if ( ::bind( m_iListenFd,
                         reinterpret_cast< struct sockaddr* >( &addr ),
                         sizeof( addr ) ) < 0 )
            {
                LAP_COM_LOG_ERROR << "SocketBinding: TCP bind failed";
                ::close( m_iListenFd );
                m_iListenFd = -1;
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }
        }
        else
        {
            // Remove existing socket file
            ::unlink( m_config.m_strSocketPath.c_str() );

            struct sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            ::strncpy( addr.sun_path,
                       m_config.m_strSocketPath.c_str(),
                       sizeof( addr.sun_path ) - 1 );

            if ( ::bind( m_iListenFd,
                         reinterpret_cast< struct sockaddr* >( &addr ),
                         sizeof( addr ) ) < 0 )
            {
                LAP_COM_LOG_ERROR << "SocketBinding: Unix bind failed";
                ::close( m_iListenFd );
                m_iListenFd = -1;
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }
        }

        if ( ::listen( m_iListenFd,
                       static_cast< int >( m_config.m_iMaxConnections ) ) < 0 )
        {
            LAP_COM_LOG_ERROR << "SocketBinding: listen failed";
            ::close( m_iListenFd );
            m_iListenFd = -1;
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kCommunicationFailure ) );
        }

        // Start acceptor thread
        m_bRunning.store( true );
        m_acceptorThread = ::std::thread( [this]() { acceptorThread(); } );

        m_metrics = {};
        m_bInitialized = true;
        LAP_COM_LOG_INFO << "SocketBinding initialized ("
                         << ( m_config.m_bUseTcp ? "TCP" : "Unix" ) << ")";
        return Result< void >::FromValue();
    }

    Result< void > SocketBinding::Shutdown() noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized )
        {
            return Result< void >::FromValue();
        }

        m_bRunning.store( false );

        // Close listen socket to unblock accept()
        if ( m_iListenFd >= 0 )
        {
            ::close( m_iListenFd );
            m_iListenFd = -1;
        }

        if ( m_acceptorThread.joinable() )
        {
            m_mutex.unlock();
            m_acceptorThread.join();
            m_mutex.lock();
        }

        // Close connected socket
        if ( m_iConnFd >= 0 )
        {
            ::close( m_iConnFd );
            m_iConnFd = -1;
        }

        // Close all client fds
        {
            LockGuard cLock( m_clientMutex );
            for ( auto fd : m_clientFds )
            {
                ::close( fd );
            }
            m_clientFds.clear();
        }

        // Remove socket file
        if ( !m_config.m_bUseTcp )
        {
            ::unlink( m_config.m_strSocketPath.c_str() );
        }

        m_offeredServices.clear();
        m_eventSubscriptions.clear();
        m_methodHandlers.clear();
        m_fieldNotifications.clear();
        m_pendingResponses.clear();

        m_bInitialized = false;
        LAP_COM_LOG_INFO << "SocketBinding shutdown";
        return Result< void >::FromValue();
    }

    // ====================================================================
    // Service Management
    // ====================================================================

    Result< void > SocketBinding::OfferService(
        UInt64 serviceId, UInt64 instanceId ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        auto key = makeServiceKey( serviceId, instanceId );
        m_offeredServices[ key ] = instanceId;

        LAP_COM_LOG_INFO << "SocketBinding: offering service "
                         << serviceId << ":" << instanceId;
        return Result< void >::FromValue();
    }

    Result< void > SocketBinding::StopOfferService(
        UInt64 serviceId, UInt64 instanceId ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        m_offeredServices.erase( makeServiceKey( serviceId, instanceId ) );
        return Result< void >::FromValue();
    }

    Result< Vector< UInt64 > > SocketBinding::FindService(
        UInt64 serviceId ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized )
        {
            return Result< Vector< UInt64 > >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        Vector< UInt64 > instances;
        String prefix = ::std::to_string( serviceId ) + "_";
        for ( const auto& entry : m_offeredServices )
        {
            if ( entry.first.find( prefix ) == 0 )
            {
                instances.push_back( entry.second );
            }
        }
        return Result< Vector< UInt64 > >::FromValue( ::std::move( instances ) );
    }

    Result< UInt64 > SocketBinding::StartFindService(
        UInt64 serviceId, ServiceDiscoveryCallback callback ) noexcept
    {
        auto findResult = FindService( serviceId );
        if ( findResult.HasValue() && callback )
        {
            callback( serviceId, findResult.Value() );
        }
        return Result< UInt64 >::FromValue( serviceId );
    }

    Result< void > SocketBinding::StopFindService( UInt64 handle ) noexcept
    {
        static_cast< void >( handle );
        return Result< void >::FromValue();
    }

    // ====================================================================
    // Event Communication
    // ====================================================================

    Result< void > SocketBinding::DoSendEvent(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 eventId, const void* pData,
        Size dataSize ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        SocketMsgHeader hdr{};
        hdr.m_iOpCode          = kSockOpEvent;
        hdr.m_iServiceId       = static_cast< UInt16 >( serviceId );
        hdr.m_iInstanceId      = static_cast< UInt16 >( instanceId );
        hdr.m_iMethodOrEventId = static_cast< UInt16 >( eventId );
        hdr.m_iSessionId       = 0;
        hdr.m_iPayloadLength   = static_cast< UInt32 >( dataSize );

        // Broadcast to all connected clients
        LockGuard cLock( m_clientMutex );
        for ( auto fd : m_clientFds )
        {
            sendMsg( fd, hdr, pData );
        }

        // Local subscribers
        ::std::ostringstream oss;
        oss << serviceId << "_" << instanceId << "_e" << eventId;
        auto it = m_eventSubscriptions.find( oss.str() );
        if ( it != m_eventSubscriptions.end() && it->second )
        {
            it->second( serviceId, instanceId, eventId, pData );
        }

        ++m_metrics.messagesSent;
        m_metrics.bytesSent += dataSize;
        return Result< void >::FromValue();
    }

    Result< void > SocketBinding::DoSubscribeEvent(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 eventId, EventCallback callback,
        Size dataSize ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        ::std::ostringstream oss;
        oss << serviceId << "_" << instanceId << "_e" << eventId;
        m_eventSubscriptions[ oss.str() ] = ::std::move( callback );
        static_cast< void >( dataSize );
        return Result< void >::FromValue();
    }

    Result< void > SocketBinding::UnsubscribeEvent(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 eventId ) noexcept
    {
        LockGuard lock( m_mutex );
        ::std::ostringstream oss;
        oss << serviceId << "_" << instanceId << "_e" << eventId;
        m_eventSubscriptions.erase( oss.str() );
        return Result< void >::FromValue();
    }

    // ====================================================================
    // Method Communication
    // ====================================================================

    Result< void > SocketBinding::DoCallMethod(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 methodId, const void* pRequest, void* pResponse,
        Size requestSize, Size responseSize ) noexcept
    {
        UInt16 sessionId;

        {
            LockGuard lock( m_mutex );
            if ( !m_bInitialized )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kNotInitialized ) );
            }

            // Try local handler first
            ::std::ostringstream oss;
            oss << serviceId << "_" << instanceId << "_m" << methodId;
            auto it = m_methodHandlers.find( oss.str() );
            if ( it != m_methodHandlers.end() && it->second )
            {
                it->second( serviceId, instanceId, methodId, pRequest, pResponse );
                ++m_metrics.messagesSent;
                ++m_metrics.messagesReceived;
                return Result< void >::FromValue();
            }

            sessionId = m_iNextSessionId++;
            if ( m_iNextSessionId == 0 ) { m_iNextSessionId = 1; }
        }

        // Build request header
        SocketMsgHeader hdr{};
        hdr.m_iOpCode          = kSockOpRequest;
        hdr.m_iServiceId       = static_cast< UInt16 >( serviceId );
        hdr.m_iInstanceId      = static_cast< UInt16 >( instanceId );
        hdr.m_iMethodOrEventId = static_cast< UInt16 >( methodId );
        hdr.m_iSessionId       = sessionId;
        hdr.m_iPayloadLength   = static_cast< UInt32 >( requestSize );

        auto respResult = sendAndWaitResponse( hdr, pRequest, m_config.m_iTimeoutMs );
        if ( !respResult.HasValue() )
        {
            return Result< void >::FromError( respResult.Error() );
        }

        const auto& respBuf = respResult.Value();
        if ( pResponse && responseSize > 0 && respBuf.size() > kSockHeaderSize )
        {
            Size payloadLen = respBuf.size() - kSockHeaderSize;
            Size copyLen = ( payloadLen < responseSize ) ? payloadLen : responseSize;
            ::std::memcpy( pResponse, respBuf.data() + kSockHeaderSize, copyLen );
        }

        {
            LockGuard lock( m_mutex );
            ++m_metrics.messagesSent;
            ++m_metrics.messagesReceived;
        }
        return Result< void >::FromValue();
    }

    Result< void > SocketBinding::DoRegisterMethod(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 methodId, MethodHandler handler,
        Size requestSize, Size responseSize ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        ::std::ostringstream oss;
        oss << serviceId << "_" << instanceId << "_m" << methodId;
        m_methodHandlers[ oss.str() ] = ::std::move( handler );
        static_cast< void >( requestSize );
        static_cast< void >( responseSize );
        return Result< void >::FromValue();
    }

    // ====================================================================
    // Field Communication
    // ====================================================================

    Result< void > SocketBinding::DoGetField(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 fieldId, void* pOutValue,
        Size valueSize ) noexcept
    {
        UInt32 getMethodId = fieldId | 0x8000U;
        return DoCallMethod( serviceId, instanceId,
                             getMethodId, nullptr, pOutValue,
                             0, valueSize );
    }

    Result< void > SocketBinding::DoSetField(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 fieldId, const void* pValue,
        Size valueSize ) noexcept
    {
        UInt32 setMethodId = fieldId | 0x8001U;
        Byte dummyResp{};
        return DoCallMethod( serviceId, instanceId,
                             setMethodId, pValue, &dummyResp,
                             valueSize, 0 );
    }

    Result< void > SocketBinding::DoSubscribeFieldNotification(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 fieldId, FieldNotificationCallback callback,
        Size valueSize ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        ::std::ostringstream oss;
        oss << serviceId << "_" << instanceId << "_f" << fieldId;
        m_fieldNotifications[ oss.str() ] = ::std::move( callback );
        static_cast< void >( valueSize );
        return Result< void >::FromValue();
    }

    Result< void > SocketBinding::UnsubscribeFieldNotification(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 fieldId ) noexcept
    {
        LockGuard lock( m_mutex );
        ::std::ostringstream oss;
        oss << serviceId << "_" << instanceId << "_f" << fieldId;
        m_fieldNotifications.erase( oss.str() );
        return Result< void >::FromValue();
    }

    // ====================================================================
    // Capability Queries
    // ====================================================================

    Bool SocketBinding::SupportsService( UInt64 serviceId ) const noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized ) { return false; }

        String prefix = ::std::to_string( serviceId ) + "_";
        for ( const auto& entry : m_offeredServices )
        {
            if ( entry.first.find( prefix ) == 0 ) { return true; }
        }
        return m_bInitialized;
    }

    TransportMetrics SocketBinding::GetMetrics() const noexcept
    {
        LockGuard lock( m_mutex );
        return m_metrics;
    }

    // ====================================================================
    // Internal Helpers
    // ====================================================================

    void SocketBinding::acceptorThread() noexcept
    {
        while ( m_bRunning.load() )
        {
            struct pollfd pfd{};
            pfd.fd     = m_iListenFd;
            pfd.events = POLLIN;

            int ret = ::poll( &pfd, 1, 100 );
            if ( ret <= 0 ) { continue; }

            Int32 clientFd = static_cast< Int32 >(
                ::accept( m_iListenFd, nullptr, nullptr ) );
            if ( clientFd < 0 ) { continue; }

            {
                LockGuard cLock( m_clientMutex );
                m_clientFds.push_back( clientFd );
                ++m_metrics.activeConnections;
            }

            // Handle client in a detached thread
            ::std::thread( [this, clientFd]()
            {
                clientHandler( clientFd );
            } ).detach();
        }
    }

    void SocketBinding::clientHandler( Int32 clientFd ) noexcept
    {
        Vector< Byte > buf( kSockHeaderSize + 65536 );

        while ( m_bRunning.load() )
        {
            struct pollfd pfd{};
            pfd.fd     = clientFd;
            pfd.events = POLLIN;

            int ret = ::poll( &pfd, 1, 100 );
            if ( ret <= 0 ) { continue; }

            // Read header
            ssize_t bytesRead = ::recv( clientFd, buf.data(),
                                         kSockHeaderSize, MSG_WAITALL );
            if ( bytesRead < static_cast< ssize_t >( kSockHeaderSize ) )
            {
                break;  // Connection closed or error
            }

            SocketMsgHeader hdr{};
            ::std::memcpy( &hdr, buf.data(), sizeof( SocketMsgHeader ) );

            // Read payload
            if ( hdr.m_iPayloadLength > 0 )
            {
                if ( hdr.m_iPayloadLength > buf.size() - kSockHeaderSize )
                {
                    buf.resize( kSockHeaderSize + hdr.m_iPayloadLength );
                }
                bytesRead = ::recv( clientFd,
                                     buf.data() + kSockHeaderSize,
                                     hdr.m_iPayloadLength, MSG_WAITALL );
                if ( bytesRead < static_cast< ssize_t >( hdr.m_iPayloadLength ) )
                {
                    break;
                }
            }

            const void* pPayload = buf.data() + kSockHeaderSize;

            if ( hdr.m_iOpCode == kSockOpResponse )
            {
                // Response to pending request
                LockGuard rLock( m_responseMutex );
                ByteBuffer respBuf( buf.data(),
                                     buf.data() + kSockHeaderSize + hdr.m_iPayloadLength );
                m_pendingResponses[ hdr.m_iSessionId ] = ::std::move( respBuf );
                m_responseCv.notify_all();
            }
            else if ( hdr.m_iOpCode == kSockOpEvent ||
                      hdr.m_iOpCode == kSockOpNotify )
            {
                LockGuard lock( m_mutex );
                ++m_metrics.messagesReceived;
                m_metrics.bytesReceived +=
                    kSockHeaderSize + hdr.m_iPayloadLength;

                // Dispatch events
                for ( const auto& sub : m_eventSubscriptions )
                {
                    sub.second( hdr.m_iServiceId, hdr.m_iInstanceId,
                                hdr.m_iMethodOrEventId, pPayload );
                }

                // Dispatch field notifications
                for ( const auto& fn : m_fieldNotifications )
                {
                    fn.second( hdr.m_iServiceId, hdr.m_iInstanceId,
                               hdr.m_iMethodOrEventId, pPayload );
                }
            }
            else if ( hdr.m_iOpCode == kSockOpRequest )
            {
                LockGuard lock( m_mutex );
                ++m_metrics.messagesReceived;

                // Find and invoke handler
                bool handled = false;
                for ( const auto& mh : m_methodHandlers )
                {
                    Vector< Byte > respPayload( 1024 );
                    mh.second( hdr.m_iServiceId, hdr.m_iInstanceId,
                               hdr.m_iMethodOrEventId,
                               pPayload, respPayload.data() );

                    SocketMsgHeader respHdr{};
                    respHdr.m_iOpCode          = kSockOpResponse;
                    respHdr.m_iServiceId       = hdr.m_iServiceId;
                    respHdr.m_iInstanceId      = hdr.m_iInstanceId;
                    respHdr.m_iMethodOrEventId = hdr.m_iMethodOrEventId;
                    respHdr.m_iSessionId       = hdr.m_iSessionId;
                    respHdr.m_iPayloadLength   = static_cast< UInt32 >(
                        respPayload.size() );

                    sendMsg( clientFd, respHdr, respPayload.data() );
                    ++m_metrics.messagesSent;
                    handled = true;
                    break;
                }

                if ( !handled )
                {
                    ++m_metrics.messagesDropped;
                }
            }
        }

        // Remove client fd
        {
            LockGuard cLock( m_clientMutex );
            m_clientFds.erase(
                ::std::remove( m_clientFds.begin(), m_clientFds.end(), clientFd ),
                m_clientFds.end() );
            if ( m_metrics.activeConnections > 0 )
            {
                --m_metrics.activeConnections;
            }
        }
        ::close( clientFd );
    }

    Result< void > SocketBinding::sendMsg(
        Int32 fd, const SocketMsgHeader& hdr,
        const void* pPayload ) noexcept
    {
        // Send header
        ssize_t sent = ::send( fd, &hdr, sizeof( SocketMsgHeader ), MSG_NOSIGNAL );
        if ( sent < static_cast< ssize_t >( sizeof( SocketMsgHeader ) ) )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kCommunicationFailure ) );
        }

        // Send payload
        if ( pPayload && hdr.m_iPayloadLength > 0 )
        {
            sent = ::send( fd, pPayload, hdr.m_iPayloadLength, MSG_NOSIGNAL );
            if ( sent < static_cast< ssize_t >( hdr.m_iPayloadLength ) )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }
        }

        return Result< void >::FromValue();
    }

    Result< ByteBuffer > SocketBinding::sendAndWaitResponse(
        const SocketMsgHeader& hdr,
        const void* pPayload, UInt32 timeoutMs ) noexcept
    {
        // Connect to server if not yet connected
        {
            LockGuard lock( m_mutex );
            if ( m_iConnFd < 0 )
            {
                if ( m_config.m_bUseTcp )
                {
                    m_iConnFd = static_cast< Int32 >(
                        ::socket( AF_INET, SOCK_STREAM, 0 ) );
                    struct sockaddr_in addr{};
                    addr.sin_family      = AF_INET;
                    addr.sin_port        = htons( m_config.m_iPort );
                    addr.sin_addr.s_addr = ::inet_addr(
                        m_config.m_strBindAddress.c_str() );
                    if ( ::connect( m_iConnFd,
                                     reinterpret_cast< struct sockaddr* >( &addr ),
                                     sizeof( addr ) ) < 0 )
                    {
                        ::close( m_iConnFd );
                        m_iConnFd = -1;
                    }
                }
                else
                {
                    m_iConnFd = static_cast< Int32 >(
                        ::socket( AF_UNIX, SOCK_STREAM, 0 ) );
                    struct sockaddr_un addr{};
                    addr.sun_family = AF_UNIX;
                    ::strncpy( addr.sun_path,
                               m_config.m_strSocketPath.c_str(),
                               sizeof( addr.sun_path ) - 1 );
                    if ( ::connect( m_iConnFd,
                                     reinterpret_cast< struct sockaddr* >( &addr ),
                                     sizeof( addr ) ) < 0 )
                    {
                        ::close( m_iConnFd );
                        m_iConnFd = -1;
                    }
                }

                if ( m_iConnFd < 0 )
                {
                    return Result< ByteBuffer >::FromError(
                        MakeErrorCode( ComErrc::kCommunicationFailure ) );
                }
            }
        }

        // Send message
        auto sendResult = sendMsg( m_iConnFd, hdr, pPayload );
        if ( !sendResult.HasValue() )
        {
            return Result< ByteBuffer >::FromError( sendResult.Error() );
        }

        // Wait for response
        UniqueLock lock( m_responseMutex );
        auto deadline = ::std::chrono::steady_clock::now()
                        + ::std::chrono::milliseconds( timeoutMs );

        while ( m_pendingResponses.find( hdr.m_iSessionId ) ==
                m_pendingResponses.end() )
        {
            if ( m_responseCv.wait_until( lock, deadline )
                 == ::std::cv_status::timeout )
            {
                return Result< ByteBuffer >::FromError(
                    MakeErrorCode( ComErrc::kTimeout ) );
            }
        }

        ByteBuffer response = ::std::move(
            m_pendingResponses[ hdr.m_iSessionId ] );
        m_pendingResponses.erase( hdr.m_iSessionId );
        return Result< ByteBuffer >::FromValue( ::std::move( response ) );
    }

    String SocketBinding::makeServiceKey(
        UInt64 serviceId, UInt64 instanceId ) const noexcept
    {
        ::std::ostringstream oss;
        oss << serviceId << "_" << instanceId;
        return oss.str();
    }

} // namespace binding
} // namespace com
} // namespace lap

// C Export Functions
extern "C" {

lap::com::binding::ITransportBinding* CreateBindingInstance()
{
    return new lap::com::binding::SocketBinding();
}

void DestroyBindingInstance( lap::com::binding::ITransportBinding* instance )
{
    delete instance;
}

} // extern "C"
