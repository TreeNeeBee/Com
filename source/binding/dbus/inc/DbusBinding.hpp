/**
 * @file        DbusBinding.hpp
 * @author      LightAP Development Team
 * @brief       D-Bus transport binding — sd-bus based implementation
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     D-Bus transport binding using sd-bus (libsystemd).
 *              Uses Unix domain socket IPC via the D-Bus session/system bus.
 *
 *              Implements:
 *              - Service offer/find via D-Bus name ownership
 *              - Event publish/subscribe via D-Bus signals
 *              - Method request/response via D-Bus method calls
 *              - Field get/set/notify mapped to D-Bus property interface
 *
 * @note        Priority: 20 (desktop / debug / inter-process)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Stub facade
 * <tr><td>2026/02/28  <td>2.0      <td>Aii             <td>sd-bus implementation
 * </table>
 */

#ifndef LAP_COM_DBUS_BINDING_HPP
#define LAP_COM_DBUS_BINDING_HPP

// ==================== Project-Internal Headers ====================
#include "DbusTypes.hpp"
#include "ITransportBinding.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/CResult.hpp>

// ==================== Standard Library Headers ====================
#include <thread>
#include <atomic>

struct sd_bus;  // Forward declaration (from systemd/sd-bus.h)

namespace lap
{
namespace com
{
namespace binding
{

    // ====================================================================
    // DbusBinding — sd-bus Implementation
    // ====================================================================

    /**
     * @brief   D-Bus Transport Binding (sd-bus)
     *
     * @details Uses the systemd sd-bus API for IPC over the D-Bus protocol.
     *          Services are mapped to D-Bus well-known names.
     *          Events use D-Bus signals, methods use D-Bus method calls.
     *
     * @note    Priority: 20 (desktop / debugging)
     */
    class DbusBinding : public ITransportBinding
    {
    public:
        DbusBinding() noexcept;
        ~DbusBinding() noexcept override;

        // Rule of Five — non-copyable, non-movable
        DbusBinding( const DbusBinding& )             = delete;
        DbusBinding& operator=( const DbusBinding& )  = delete;
        DbusBinding( DbusBinding&& )                  = delete;
        DbusBinding& operator=( DbusBinding&& )       = delete;

    public:
        // ================================================================
        // Lifecycle
        // ================================================================

        Result< void > Initialize() noexcept override;
        Result< void > Shutdown() noexcept override;

    public:
        // ================================================================
        // Service Management
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
        // Event Communication
        // ================================================================

        Result< void > UnsubscribeEvent(
            UInt64 serviceId, UInt64 instanceId,
            UInt32 eventId ) noexcept override;

    public:
        // ================================================================
        // Field Communication
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

        const char* GetName() const noexcept override { return "DBus"; }
        UInt32 GetVersion() const noexcept override { return 0x00010000U; }
        UInt32 GetPriority() const noexcept override { return 20U; }
        Bool SupportsZeroCopy() const noexcept override { return false; }
        Bool SupportsService( UInt64 serviceId ) const noexcept override;
        TransportMetrics GetMetrics() const noexcept override;

    private:
        // ================================================================
        // Internal Helpers
        // ================================================================

        String makeDbusName( UInt64 serviceId, UInt64 instanceId ) const noexcept;
        String makeDbusPath( UInt64 serviceId, UInt64 instanceId ) const noexcept;
        void   busProcessThread() noexcept;

    private:
        // ================================================================
        // Member Variables
        // ================================================================

        DbusConfig          m_config;           ///< Binding configuration
        mutable Mutex       m_mutex;            ///< Main serialisation lock
        Bool                m_bInitialized;     ///< Initialisation state
        mutable TransportMetrics m_metrics;     ///< Transport metrics

        // sd-bus connection
        struct sd_bus*      m_pBus;             ///< sd-bus connection handle

        // Bus event-processing thread
        ::std::thread       m_busThread;        ///< Background bus-process thread
        ::std::atomic< bool > m_bRunning;       ///< Bus thread run flag

        // Service registry (local)
        Map< String, UInt64 >   m_offeredServices;  ///< name -> instanceId

        // Event subscriptions
        Map< String, EventCallback >    m_eventSubscriptions;

        // Method handlers
        Map< String, MethodHandler >    m_methodHandlers;

        // Field notification callbacks
        Map< String, FieldNotificationCallback > m_fieldNotifications;
    };

} // namespace binding
} // namespace com
} // namespace lap

// ==================== C Export Functions ====================
extern "C" {
    lap::com::binding::ITransportBinding* CreateBindingInstance();
    void DestroyBindingInstance( lap::com::binding::ITransportBinding* instance );
}

#endif // LAP_COM_DBUS_BINDING_HPP
