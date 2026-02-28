/**
 * @file        SocketBinding.hpp
 * @author      LightAP Development Team
 * @brief       Socket transport binding — Unix/TCP socket implementation
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Socket-based IPC transport binding.
 *              Supports both Unix domain sockets (AF_UNIX) and TCP sockets.
 *              Uses a simple TLV framing protocol (SocketMsgHeader + payload).
 *
 *              Implements:
 *              - Service offer/find via local registry
 *              - Event publish/subscribe via socket broadcast
 *              - Method request/response via socket round-trip
 *              - Field get/set/notify mapped to method/event primitives
 *
 * @note        Priority: 40 (fallback for testing / socket-based IPC)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Stub facade
 * <tr><td>2026/02/28  <td>2.0      <td>Aii             <td>Unix/TCP socket implementation
 * </table>
 */

#ifndef LAP_COM_SOCKET_BINDING_HPP
#define LAP_COM_SOCKET_BINDING_HPP

// ==================== Project-Internal Headers ====================
#include "SocketTypes.hpp"
#include "ITransportBinding.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/CResult.hpp>

// ==================== Standard Library Headers ====================
#include <thread>
#include <atomic>

namespace lap
{
namespace com
{
namespace binding
{

    // ====================================================================
    // SocketBinding — Unix/TCP Socket
    // ====================================================================

    /**
     * @brief   Socket Transport Binding (Unix domain / TCP)
     *
     * @details Socket-based IPC using TLV framing protocol.
     *          Default: Unix domain socket at /tmp/lap_com.sock.
     *          Optional: TCP socket on configurable address/port.
     *
     * @note    Priority: 40 (fallback for testing / socket-based IPC)
     */
    class SocketBinding : public ITransportBinding
    {
    public:
        SocketBinding() noexcept;
        ~SocketBinding() noexcept override;

        SocketBinding( const SocketBinding& )             = delete;
        SocketBinding& operator=( const SocketBinding& )  = delete;
        SocketBinding( SocketBinding&& )                  = delete;
        SocketBinding& operator=( SocketBinding&& )       = delete;

    public:
        // Lifecycle
        Result< void > Initialize() noexcept override;
        Result< void > Shutdown() noexcept override;

    public:
        // Service Management
        Result< void > OfferService( UInt64 serviceId, UInt64 instanceId ) noexcept override;
        Result< void > StopOfferService( UInt64 serviceId, UInt64 instanceId ) noexcept override;
        Result< Vector< UInt64 > > FindService( UInt64 serviceId ) noexcept override;

        Result< UInt64 > StartFindService(
            UInt64 serviceId,
            ServiceDiscoveryCallback callback ) noexcept override;

        Result< void > StopFindService(
            UInt64 handle ) noexcept override;

    public:
        // Event Communication
        Result< void > UnsubscribeEvent( UInt64 serviceId, UInt64 instanceId, UInt32 eventId ) noexcept override;

    public:
        // Field Communication
        Result< void > UnsubscribeFieldNotification(
            UInt64 serviceId, UInt64 instanceId,
            UInt32 fieldId ) noexcept override;

    protected:
        // NVI Do* Overrides
        Result< void > DoSendEvent( UInt64 serviceId, UInt64 instanceId, UInt32 eventId, const void* pData, Size dataSize = 0 ) noexcept override;
        Result< void > DoSubscribeEvent( UInt64 serviceId, UInt64 instanceId, UInt32 eventId, EventCallback callback, Size dataSize = 0 ) noexcept override;
        Result< void > DoCallMethod( UInt64 serviceId, UInt64 instanceId, UInt32 methodId, const void* pRequest, void* pResponse, Size requestSize = 0, Size responseSize = 0 ) noexcept override;
        Result< void > DoRegisterMethod( UInt64 serviceId, UInt64 instanceId, UInt32 methodId, MethodHandler handler, Size requestSize = 0, Size responseSize = 0 ) noexcept override;
        Result< void > DoGetField( UInt64 serviceId, UInt64 instanceId, UInt32 fieldId, void* pOutValue, Size valueSize = 0 ) noexcept override;
        Result< void > DoSetField( UInt64 serviceId, UInt64 instanceId, UInt32 fieldId, const void* pValue, Size valueSize = 0 ) noexcept override;
        Result< void > DoSubscribeFieldNotification( UInt64 serviceId, UInt64 instanceId, UInt32 fieldId, FieldNotificationCallback callback, Size valueSize = 0 ) noexcept override;

    public:
        // Capability Queries
        const char* GetName() const noexcept override { return "Socket"; }
        UInt32 GetVersion() const noexcept override { return 0x00010000U; }
        UInt32 GetPriority() const noexcept override { return 40U; }
        Bool SupportsZeroCopy() const noexcept override { return false; }
        Bool SupportsService( UInt64 serviceId ) const noexcept override;
        TransportMetrics GetMetrics() const noexcept override;

    private:
        // ================================================================
        // Internal Helpers
        // ================================================================

        void acceptorThread() noexcept;
        void clientHandler( Int32 clientFd ) noexcept;
        Result< void > sendMsg( Int32 fd, const SocketMsgHeader& hdr,
                                const void* pPayload ) noexcept;
        Result< ByteBuffer > sendAndWaitResponse(
            const SocketMsgHeader& hdr,
            const void* pPayload, UInt32 timeoutMs ) noexcept;
        String makeServiceKey( UInt64 serviceId, UInt64 instanceId ) const noexcept;

    private:
        // ================================================================
        // Member Variables
        // ================================================================

        SocketConfig         m_config;
        mutable Mutex        m_mutex;
        Bool                 m_bInitialized;
        mutable TransportMetrics m_metrics;

        // Server socket
        Int32               m_iListenFd;        ///< Listening socket fd
        Int32               m_iConnFd;           ///< Connected socket fd (client side)

        // Acceptor thread
        ::std::thread       m_acceptorThread;    ///< Background accept thread
        ::std::atomic< bool > m_bRunning;        ///< Thread run flag

        // Session tracking
        UInt16              m_iNextSessionId;

        // Service registry
        Map< String, UInt64 > m_offeredServices;

        // Event subscriptions
        Map< String, EventCallback > m_eventSubscriptions;

        // Method handlers
        Map< String, MethodHandler > m_methodHandlers;

        // Field notifications
        Map< String, FieldNotificationCallback > m_fieldNotifications;

        // Connected clients
        Vector< Int32 >    m_clientFds;
        mutable Mutex      m_clientMutex;

        // Pending responses
        Map< UInt16, ByteBuffer > m_pendingResponses;
        mutable Mutex             m_responseMutex;
        mutable ConditionVariable m_responseCv;
    };

} // namespace binding
} // namespace com
} // namespace lap

// ==================== C Export Functions ====================
extern "C" {
    lap::com::binding::ITransportBinding* CreateBindingInstance();
    void DestroyBindingInstance( lap::com::binding::ITransportBinding* instance );
}

#endif // LAP_COM_SOCKET_BINDING_HPP
