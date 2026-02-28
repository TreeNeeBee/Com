/**
 * @file        SomeIpBinding.cpp
 * @author      LightAP Development Team
 * @brief       SOME/IP binding — Lightweight UDP-based implementation
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Lightweight SOME/IP-over-UDP transport binding.
 *              Implements AUTOSAR SOME/IP wire format (PRS_SOMEIP_00041)
 *              using raw UDP sockets.  No vsomeip dependency.
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Stub facade
 * <tr><td>2026/02/28  <td>2.0      <td>Aii             <td>Lightweight UDP implementation
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "SomeIpBinding.hpp"
#include "ComTypes.hpp"

// ==================== System Headers ====================
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <chrono>
#include <sstream>

namespace lap
{
namespace com
{
namespace binding
{

    // ====================================================================
    // Constructor / Destructor
    // ====================================================================

    SomeIpBinding::SomeIpBinding() noexcept
        : m_bInitialized( false )
        , m_iSockFd( -1 )
        , m_bRunning( false )
        , m_iNextSessionId( 1 )
    {
        LAP_COM_LOG_INFO << "SomeIpBinding instance created";
    }

    SomeIpBinding::~SomeIpBinding() noexcept
    {
        static_cast< void >( Shutdown() );
        LAP_COM_LOG_INFO << "SomeIpBinding instance destroyed";
    }

    // ====================================================================
    // Lifecycle Management
    // ====================================================================

    Result< void > SomeIpBinding::Initialize() noexcept
    {
        LockGuard lock( m_mutex );

        if ( m_bInitialized )
        {
            return Result< void >::FromValue();
        }

        // Create UDP socket
        m_iSockFd = static_cast< Int32 >( ::socket( AF_INET, SOCK_DGRAM, 0 ) );
        if ( m_iSockFd < 0 )
        {
            LAP_COM_LOG_ERROR << "SomeIpBinding: failed to create UDP socket";
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kCommunicationFailure ) );
        }

        // Allow address reuse
        int optval = 1;
        ::setsockopt( m_iSockFd, SOL_SOCKET, SO_REUSEADDR,
                       &optval, sizeof( optval ) );

        // Bind to configured port
        struct sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons( m_config.m_iPort );
        addr.sin_addr.s_addr = ::inet_addr( m_config.m_strUnicastAddress.c_str() );

        if ( ::bind( m_iSockFd, reinterpret_cast< struct sockaddr* >( &addr ),
                     sizeof( addr ) ) < 0 )
        {
            // Port may already be in use; try ephemeral
            addr.sin_port = 0;
            if ( ::bind( m_iSockFd, reinterpret_cast< struct sockaddr* >( &addr ),
                         sizeof( addr ) ) < 0 )
            {
                LAP_COM_LOG_ERROR << "SomeIpBinding: failed to bind UDP socket";
                ::close( m_iSockFd );
                m_iSockFd = -1;
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }
        }

        // Start receiver thread
        m_bRunning.store( true );
        m_receiverThread = ::std::thread( [this]() { receiverThread(); } );

        m_metrics = {};
        m_bInitialized = true;
        LAP_COM_LOG_INFO << "SomeIpBinding initialized (UDP on "
                         << m_config.m_strUnicastAddress << ":"
                         << m_config.m_iPort << ")";
        return Result< void >::FromValue();
    }

    Result< void > SomeIpBinding::Shutdown() noexcept
    {
        LockGuard lock( m_mutex );

        if ( !m_bInitialized )
        {
            return Result< void >::FromValue();
        }

        // Stop receiver thread
        m_bRunning.store( false );
        if ( m_receiverThread.joinable() )
        {
            // Unlock to let receiver exit
            m_mutex.unlock();
            m_receiverThread.join();
            m_mutex.lock();
        }

        // Close socket
        if ( m_iSockFd >= 0 )
        {
            ::close( m_iSockFd );
            m_iSockFd = -1;
        }

        // Clear registries
        m_offeredServices.clear();
        m_eventSubscriptions.clear();
        m_methodHandlers.clear();
        m_fieldNotifications.clear();
        m_pendingResponses.clear();

        m_bInitialized = false;
        LAP_COM_LOG_INFO << "SomeIpBinding shutdown";
        return Result< void >::FromValue();
    }

    // ====================================================================
    // Service Management
    // ====================================================================

    Result< void > SomeIpBinding::OfferService(
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

        LAP_COM_LOG_INFO << "SomeIpBinding: offering service "
                         << serviceId << ":" << instanceId;
        return Result< void >::FromValue();
    }

    Result< void > SomeIpBinding::StopOfferService(
        UInt64 serviceId, UInt64 instanceId ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        auto key = makeServiceKey( serviceId, instanceId );
        m_offeredServices.erase( key );

        LAP_COM_LOG_INFO << "SomeIpBinding: stopped offering service "
                         << serviceId << ":" << instanceId;
        return Result< void >::FromValue();
    }

    Result< Vector< UInt64 > > SomeIpBinding::FindService(
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

    Result< UInt64 > SomeIpBinding::StartFindService(
        UInt64 serviceId, ServiceDiscoveryCallback callback ) noexcept
    {
        // Trigger immediate callback with currently known instances
        auto findResult = FindService( serviceId );
        if ( findResult.HasValue() && callback )
        {
            callback( serviceId, findResult.Value() );
        }

        // Return a handle (use serviceId as handle)
        return Result< UInt64 >::FromValue( serviceId );
    }

    Result< void > SomeIpBinding::StopFindService(
        UInt64 handle ) noexcept
    {
        static_cast< void >( handle );
        return Result< void >::FromValue();
    }

    // ====================================================================
    // Event Communication
    // ====================================================================

    Result< void > SomeIpBinding::DoSendEvent(
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

        auto packet = buildSomeIpPacket(
            static_cast< UInt16 >( serviceId ),
            static_cast< UInt16 >( eventId ),
            kSomeIpMsgTypeNotify,
            pData, dataSize );

        auto result = sendUdp( packet );
        if ( result.HasValue() )
        {
            ++m_metrics.messagesSent;
            m_metrics.bytesSent += packet.size();
        }

        static_cast< void >( instanceId );
        return result;
    }

    Result< void > SomeIpBinding::DoSubscribeEvent(
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
        LAP_COM_LOG_DEBUG << "SomeIpBinding: subscribed to event " << eventId
                          << " on " << serviceId << ":" << instanceId;
        return Result< void >::FromValue();
    }

    Result< void > SomeIpBinding::UnsubscribeEvent(
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

    Result< void > SomeIpBinding::DoCallMethod(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 methodId, const void* pRequest, void* pResponse,
        Size requestSize, Size responseSize ) noexcept
    {
        UInt16 sessionId;
        ByteBuffer packet;

        {
            LockGuard lock( m_mutex );
            if ( !m_bInitialized )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kNotInitialized ) );
            }

            sessionId = m_iNextSessionId++;
            if ( m_iNextSessionId == 0 ) { m_iNextSessionId = 1; }

            packet = buildSomeIpPacket(
                static_cast< UInt16 >( serviceId ),
                static_cast< UInt16 >( methodId ),
                kSomeIpMsgTypeRequest,
                pRequest, requestSize );

            // Patch session ID into packet header  (bytes 10-11)
            if ( packet.size() >= kSomeIpHeaderSize )
            {
                packet[10] = static_cast< Byte >( ( sessionId >> 8 ) & 0xFF );
                packet[11] = static_cast< Byte >( sessionId & 0xFF );
            }
        }

        // Send and wait for response (outside main lock to avoid deadlock)
        auto respResult = sendAndWaitResponse( packet, m_config.m_iTimeoutMs );
        if ( !respResult.HasValue() )
        {
            return Result< void >::FromError( respResult.Error() );
        }

        // Copy response payload
        const auto& respBuf = respResult.Value();
        if ( pResponse && responseSize > 0 && respBuf.size() >= kSomeIpHeaderSize )
        {
            Size payloadLen = respBuf.size() - kSomeIpHeaderSize;
            Size copyLen = ( payloadLen < responseSize ) ? payloadLen : responseSize;
            ::std::memcpy( pResponse, respBuf.data() + kSomeIpHeaderSize, copyLen );
        }

        {
            LockGuard lock( m_mutex );
            ++m_metrics.messagesSent;
            ++m_metrics.messagesReceived;
            m_metrics.bytesSent += packet.size();
            m_metrics.bytesReceived += respBuf.size();
        }

        static_cast< void >( instanceId );
        return Result< void >::FromValue();
    }

    Result< void > SomeIpBinding::DoRegisterMethod(
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
        LAP_COM_LOG_DEBUG << "SomeIpBinding: registered method " << methodId
                          << " on " << serviceId << ":" << instanceId;
        return Result< void >::FromValue();
    }

    // ====================================================================
    // Field Communication (mapped to method/event primitives)
    // ====================================================================

    Result< void > SomeIpBinding::DoGetField(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 fieldId, void* pOutValue,
        Size valueSize ) noexcept
    {
        // Field-Get mapped to method call: methodId = fieldId | 0x8000
        UInt32 getMethodId = fieldId | 0x8000U;
        return DoCallMethod( serviceId, instanceId,
                             getMethodId, nullptr, pOutValue,
                             0, valueSize );
    }

    Result< void > SomeIpBinding::DoSetField(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 fieldId, const void* pValue,
        Size valueSize ) noexcept
    {
        // Field-Set mapped to method call: methodId = fieldId | 0x8001
        UInt32 setMethodId = fieldId | 0x8001U;
        Byte dummyResp{};
        return DoCallMethod( serviceId, instanceId,
                             setMethodId, pValue, &dummyResp,
                             valueSize, 0 );
    }

    Result< void > SomeIpBinding::DoSubscribeFieldNotification(
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

    Result< void > SomeIpBinding::UnsubscribeFieldNotification(
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

    Bool SomeIpBinding::SupportsService( UInt64 serviceId ) const noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized ) { return false; }

        String prefix = ::std::to_string( serviceId ) + "_";
        for ( const auto& entry : m_offeredServices )
        {
            if ( entry.first.find( prefix ) == 0 ) { return true; }
        }
        return m_bInitialized;  // If initialized, supports discovery
    }

    TransportMetrics SomeIpBinding::GetMetrics() const noexcept
    {
        LockGuard lock( m_mutex );
        return m_metrics;
    }

    // ====================================================================
    // Internal Helpers
    // ====================================================================

    void SomeIpBinding::receiverThread() noexcept
    {
        Vector< Byte > buf( m_config.m_iMaxPayloadSize + kSomeIpHeaderSize );

        while ( m_bRunning.load() )
        {
            struct pollfd pfd{};
            pfd.fd     = m_iSockFd;
            pfd.events = POLLIN;

            int ret = ::poll( &pfd, 1, 100 );  // 100ms timeout
            if ( ret <= 0 ) { continue; }

            struct sockaddr_in srcAddr{};
            socklen_t addrLen = sizeof( srcAddr );
            auto bytesRead = ::recvfrom(
                m_iSockFd, buf.data(), buf.size(), 0,
                reinterpret_cast< struct sockaddr* >( &srcAddr ), &addrLen );

            if ( bytesRead < static_cast< ssize_t >( kSomeIpHeaderSize ) )
            {
                continue;  // Too short to be a valid SOME/IP message
            }

            // Parse header
            SomeIpHeader hdr{};
            ::std::memcpy( &hdr, buf.data(), sizeof( SomeIpHeader ) );
            hdr.m_iServiceId = ntohs( hdr.m_iServiceId );
            hdr.m_iMethodId  = ntohs( hdr.m_iMethodId );
            hdr.m_iLength    = ntohl( hdr.m_iLength );
            hdr.m_iClientId  = ntohs( hdr.m_iClientId );
            hdr.m_iSessionId = ntohs( hdr.m_iSessionId );

            Size payloadLen = static_cast< Size >( bytesRead ) - kSomeIpHeaderSize;
            const void* pPayload = buf.data() + kSomeIpHeaderSize;
            static_cast< void >( payloadLen );  // Used indirectly via pPayload

            // Dispatch based on message type
            if ( hdr.m_iMessageType == kSomeIpMsgTypeResponse ||
                 hdr.m_iMessageType == kSomeIpMsgTypeError )
            {
                // Response to a pending method call
                LockGuard rLock( m_responseMutex );
                ByteBuffer respBuf( buf.data(),
                                     buf.data() + bytesRead );
                m_pendingResponses[ hdr.m_iSessionId ] = ::std::move( respBuf );
                m_responseCv.notify_all();
            }
            else if ( hdr.m_iMessageType == kSomeIpMsgTypeNotify )
            {
                // Notification → dispatch to event or field subscribers
                LockGuard lock( m_mutex );
                ++m_metrics.messagesReceived;
                m_metrics.bytesReceived +=
                    static_cast< UInt64 >( bytesRead );

                // Try event subscriptions
                for ( const auto& sub : m_eventSubscriptions )
                {
                    sub.second( hdr.m_iServiceId, 0,
                                hdr.m_iMethodId, pPayload );
                }

                // Try field notifications
                for ( const auto& fn : m_fieldNotifications )
                {
                    fn.second( hdr.m_iServiceId, 0,
                               hdr.m_iMethodId, pPayload );
                }
            }
            else if ( hdr.m_iMessageType == kSomeIpMsgTypeRequest )
            {
                // Incoming method request → dispatch to handler
                LockGuard lock( m_mutex );
                ++m_metrics.messagesReceived;
                m_metrics.bytesReceived +=
                    static_cast< UInt64 >( bytesRead );

                // Find handler
                bool handled = false;
                for ( const auto& mh : m_methodHandlers )
                {
                    // Allocate response buffer
                    Vector< Byte > respPayload( 1024 );
                    mh.second( hdr.m_iServiceId, 0,
                               hdr.m_iMethodId,
                               pPayload,
                               respPayload.data() );

                    // Build response packet
                    auto respPacket = buildSomeIpPacket(
                        hdr.m_iServiceId, hdr.m_iMethodId,
                        kSomeIpMsgTypeResponse,
                        respPayload.data(), respPayload.size() );

                    // Patch session and client IDs
                    if ( respPacket.size() >= kSomeIpHeaderSize )
                    {
                        respPacket[8]  = static_cast< Byte >(
                            ( hdr.m_iClientId >> 8 ) & 0xFF );
                        respPacket[9]  = static_cast< Byte >(
                            hdr.m_iClientId & 0xFF );
                        respPacket[10] = static_cast< Byte >(
                            ( hdr.m_iSessionId >> 8 ) & 0xFF );
                        respPacket[11] = static_cast< Byte >(
                            hdr.m_iSessionId & 0xFF );
                    }

                    // Send response back to caller
                    struct sockaddr_in dest{};
                    dest.sin_family = AF_INET;
                    dest.sin_port   = srcAddr.sin_port;
                    dest.sin_addr   = srcAddr.sin_addr;
                    ::sendto( m_iSockFd, respPacket.data(),
                              respPacket.size(), 0,
                              reinterpret_cast< struct sockaddr* >( &dest ),
                              sizeof( dest ) );

                    ++m_metrics.messagesSent;
                    m_metrics.bytesSent += respPacket.size();
                    handled = true;
                    break;  // First matching handler
                }

                if ( !handled )
                {
                    ++m_metrics.messagesDropped;
                }
            }
        }
    }

    Result< void > SomeIpBinding::sendUdp(
        const ByteBuffer& packet ) noexcept
    {
        if ( m_iSockFd < 0 )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        struct sockaddr_in dest{};
        dest.sin_family      = AF_INET;
        dest.sin_port        = htons( m_config.m_iPort );
        dest.sin_addr.s_addr = ::inet_addr(
            m_config.m_strUnicastAddress.c_str() );

        auto sent = ::sendto(
            m_iSockFd, packet.data(), packet.size(), 0,
            reinterpret_cast< struct sockaddr* >( &dest ),
            sizeof( dest ) );

        if ( sent < 0 )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kCommunicationFailure ) );
        }

        return Result< void >::FromValue();
    }

    Result< ByteBuffer > SomeIpBinding::sendAndWaitResponse(
        const ByteBuffer& packet, UInt32 timeoutMs ) noexcept
    {
        // Extract session ID from packet header
        if ( packet.size() < kSomeIpHeaderSize )
        {
            return Result< ByteBuffer >::FromError(
                MakeErrorCode( ComErrc::kInvalidArgument ) );
        }

        UInt16 sessionId = static_cast< UInt16 >(
            ( static_cast< UInt16 >( static_cast< UInt8 >( packet[10] ) ) << 8 ) |
              static_cast< UInt16 >( static_cast< UInt8 >( packet[11] ) ) );

        // Send the request
        auto sendResult = sendUdp( packet );
        if ( !sendResult.HasValue() )
        {
            return Result< ByteBuffer >::FromError( sendResult.Error() );
        }

        // Wait for matching response
        UniqueLock lock( m_responseMutex );
        auto deadline = ::std::chrono::steady_clock::now()
                        + ::std::chrono::milliseconds( timeoutMs );

        while ( m_pendingResponses.find( sessionId ) ==
                m_pendingResponses.end() )
        {
            if ( m_responseCv.wait_until( lock, deadline )
                 == ::std::cv_status::timeout )
            {
                return Result< ByteBuffer >::FromError(
                    MakeErrorCode( ComErrc::kTimeout ) );
            }
        }

        ByteBuffer response = ::std::move( m_pendingResponses[ sessionId ] );
        m_pendingResponses.erase( sessionId );
        return Result< ByteBuffer >::FromValue( ::std::move( response ) );
    }

    ByteBuffer SomeIpBinding::buildSomeIpPacket(
        UInt16 serviceId, UInt16 methodId,
        UInt8 msgType, const void* pPayload,
        Size payloadSize ) noexcept
    {
        Size totalSize = kSomeIpHeaderSize + payloadSize;
        ByteBuffer packet( totalSize );

        // Service ID  (network byte order)
        packet[0] = static_cast< Byte >( ( serviceId >> 8 ) & 0xFF );
        packet[1] = static_cast< Byte >( serviceId & 0xFF );

        // Method ID
        packet[2] = static_cast< Byte >( ( methodId >> 8 ) & 0xFF );
        packet[3] = static_cast< Byte >( methodId & 0xFF );

        // Length (payload + 8 remaining header bytes)
        UInt32 length = static_cast< UInt32 >( payloadSize + 8 );
        packet[4] = static_cast< Byte >( ( length >> 24 ) & 0xFF );
        packet[5] = static_cast< Byte >( ( length >> 16 ) & 0xFF );
        packet[6] = static_cast< Byte >( ( length >> 8 )  & 0xFF );
        packet[7] = static_cast< Byte >( length & 0xFF );

        // Client ID
        packet[8] = static_cast< Byte >(
            ( m_config.m_iClientId >> 8 ) & 0xFF );
        packet[9] = static_cast< Byte >(
            m_config.m_iClientId & 0xFF );

        // Session ID (0 by default — caller patches for requests)
        packet[10] = 0;
        packet[11] = 0;

        // Protocol version / Interface version / Message type / Return code
        packet[12] = static_cast< Byte >( kSomeIpProtocolVersion );
        packet[13] = static_cast< Byte >( kSomeIpInterfaceVersion );
        packet[14] = static_cast< Byte >( msgType );
        packet[15] = static_cast< Byte >( kSomeIpReturnCodeOk );

        // Payload
        if ( pPayload && payloadSize > 0 )
        {
            ::std::memcpy( packet.data() + kSomeIpHeaderSize,
                           pPayload, payloadSize );
        }

        return packet;
    }

    String SomeIpBinding::makeServiceKey(
        UInt64 serviceId, UInt64 instanceId ) const noexcept
    {
        ::std::ostringstream oss;
        oss << serviceId << "_" << instanceId;
        return oss.str();
    }

} // namespace binding
} // namespace com
} // namespace lap

// ====================================================================
// C Export Functions
// ====================================================================

extern "C" {

lap::com::binding::ITransportBinding* CreateBindingInstance()
{
    return new lap::com::binding::SomeIpBinding();
}

void DestroyBindingInstance( lap::com::binding::ITransportBinding* instance )
{
    delete instance;
}

} // extern "C"
