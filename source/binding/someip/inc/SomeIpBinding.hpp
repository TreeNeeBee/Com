/**
 * @file        SomeIpBinding.hpp
 * @author      LightAP Development Team
 * @brief       SOME/IP transport binding — Lightweight UDP-based implementation
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Lightweight SOME/IP-over-UDP transport binding.
 *              Implements the AUTOSAR SOME/IP wire format (PRS_SOMEIP_00041)
 *              using raw UDP sockets — no vsomeip dependency.
 *
 *              Supports:
 *              - Service offer/find via local registry
 *              - Event publish/subscribe via UDP unicast
 *              - Method request/response via UDP round-trip
 *              - Field get/set/notify mapped to method/event primitives
 *
 * @note        Priority: 60 (automotive standard — inter-ECU)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Stub facade
 * <tr><td>2026/02/28  <td>2.0      <td>Aii             <td>Lightweight SOME/IP-over-UDP implementation
 * </table>
 */

#ifndef LAP_COM_SOMEIP_BINDING_HPP
#define LAP_COM_SOMEIP_BINDING_HPP

// ==================== Project-Internal Headers ====================
#include "SomeIpTypes.hpp"
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
    // SomeIpBinding — Lightweight SOME/IP-over-UDP
    // ====================================================================

    /**
     * @brief   SOME/IP Transport Binding (Lightweight UDP)
     *
     * @details Implements SOME/IP wire format over UDP sockets.
     *          Service management uses a local in-memory registry.
     *          Events use UDP unicast with SOME/IP notification messages.
     *          Methods use UDP request/response with session tracking.
     *
     * @note    Priority: 60 (automotive standard)
     */
    class SomeIpBinding : public ITransportBinding
    {
    public:
        SomeIpBinding() noexcept;
        ~SomeIpBinding() noexcept override;

        // Rule of Five — non-copyable, non-movable
        SomeIpBinding( const SomeIpBinding& )             = delete;
        SomeIpBinding& operator=( const SomeIpBinding& )  = delete;
        SomeIpBinding( SomeIpBinding&& )                  = delete;
        SomeIpBinding& operator=( SomeIpBinding&& )       = delete;

    public:
        // ================================================================
        // Lifecycle
        // ================================================================

        Result< void > Initialize() noexcept override;
        Result< void > Shutdown() noexcept override;

    public:
        // ================================================================
        // Service Management (stub)
        // ================================================================

        Result< void > OfferService(
            UInt64 serviceId, UInt64 instanceId ) noexcept override;

        Result< void > StopOfferService(
            UInt64 serviceId, UInt64 instanceId ) noexcept override;

        Result< Vector< UInt64 > > FindService(
            UInt64 serviceId ) noexcept override;

        Result< UInt64 > StartFindService(
            UInt64 serviceId,
            ServiceDiscoveryCallback callback ) noexcept override;

        Result< void > StopFindService(
            UInt64 handle ) noexcept override;

    public:
        // ================================================================
        // Event Communication (stub)
        // ================================================================

        Result< void > UnsubscribeEvent(
            UInt64 serviceId, UInt64 instanceId,
            UInt32 eventId ) noexcept override;

    public:
        // ================================================================
        // Field Communication (stub)
        // ================================================================

        Result< void > UnsubscribeFieldNotification(
            UInt64 serviceId, UInt64 instanceId,
            UInt32 fieldId ) noexcept override;

    protected:
        // ================================================================
        // NVI Do* Overrides (type-erased virtual implementations)
        // ================================================================

        Result< void > DoSendEvent(
            UInt64 serviceId, UInt64 instanceId,
            UInt32 eventId, const void* pData,
            Size dataSize = 0 ) noexcept override;

        Result< void > DoSubscribeEvent(
            UInt64 serviceId, UInt64 instanceId,
            UInt32 eventId, EventCallback callback,
            Size dataSize = 0 ) noexcept override;

        Result< void > DoCallMethod(
            UInt64 serviceId, UInt64 instanceId,
            UInt32 methodId, const void* pRequest,
            void* pResponse,
            Size requestSize  = 0,
            Size responseSize = 0 ) noexcept override;

        Result< void > DoRegisterMethod(
            UInt64 serviceId, UInt64 instanceId,
            UInt32 methodId, MethodHandler handler,
            Size requestSize  = 0,
            Size responseSize = 0 ) noexcept override;

        Result< void > DoGetField(
            UInt64 serviceId, UInt64 instanceId,
            UInt32 fieldId, void* pOutValue,
            Size valueSize = 0 ) noexcept override;

        Result< void > DoSetField(
            UInt64 serviceId, UInt64 instanceId,
            UInt32 fieldId, const void* pValue,
            Size valueSize = 0 ) noexcept override;

        Result< void > DoSubscribeFieldNotification(
            UInt64 serviceId, UInt64 instanceId,
            UInt32 fieldId,
            FieldNotificationCallback callback,
            Size valueSize = 0 ) noexcept override;

    public:
        // ================================================================
        // Capability Queries
        // ================================================================

        const char* GetName() const noexcept override { return "SOME/IP"; }
        UInt32 GetVersion() const noexcept override { return 0x00010000U; }
        UInt32 GetPriority() const noexcept override { return 60U; }
        Bool SupportsZeroCopy() const noexcept override { return false; }
        Bool SupportsService( UInt64 serviceId ) const noexcept override;
        TransportMetrics GetMetrics() const noexcept override;

    private:
        // ================================================================
        // Internal Helpers
        // ================================================================

        void receiverThread() noexcept;
        Result< void > sendUdp( const ByteBuffer& packet ) noexcept;
        Result< ByteBuffer > sendAndWaitResponse(
            const ByteBuffer& packet, UInt32 timeoutMs ) noexcept;
        ByteBuffer buildSomeIpPacket(
            UInt16 serviceId, UInt16 methodId,
            UInt8 msgType, const void* pPayload,
            Size payloadSize ) noexcept;
        String makeServiceKey(
            UInt64 serviceId, UInt64 instanceId ) const noexcept;

    private:
        // ================================================================
        // Member Variables
        // ================================================================

        SomeIpConfig        m_config;           ///< Binding configuration
        mutable Mutex       m_mutex;            ///< Main serialisation lock
        Bool                m_bInitialized;     ///< Initialisation state
        mutable TransportMetrics m_metrics;     ///< Transport metrics

        // UDP socket
        Int32               m_iSockFd;          ///< UDP socket fd

        // Receiver thread
        ::std::thread       m_receiverThread;   ///< Background RX thread
        ::std::atomic< bool > m_bRunning;       ///< Receiver run flag

        // Session tracking
        UInt16              m_iNextSessionId;   ///< Next session ID

        // Service registry (local)
        Map< String, UInt64 >   m_offeredServices;  ///< serviceKey -> instanceId

        // Event subscriptions: "svc_inst_evt" -> callback
        Map< String, EventCallback >    m_eventSubscriptions;

        // Method handlers: "svc_inst_met" -> handler
        Map< String, MethodHandler >    m_methodHandlers;

        // Field notification callbacks: "svc_inst_fld" -> callback
        Map< String, FieldNotificationCallback > m_fieldNotifications;

        // Pending method responses: sessionId -> response buffer
        Map< UInt16, ByteBuffer >       m_pendingResponses;
        mutable Mutex                   m_responseMutex;
        mutable ConditionVariable       m_responseCv;
    };

} // namespace binding
} // namespace com
} // namespace lap

// ==================== C Export Functions ====================
extern "C" {
    lap::com::binding::ITransportBinding* CreateBindingInstance();
    void DestroyBindingInstance( lap::com::binding::ITransportBinding* instance );
}

#endif // LAP_COM_SOMEIP_BINDING_HPP
