/**
 * @file        SomeIpBinding.hpp
 * @author      LightAP Development Team
 * @brief       SOME/IP transport binding — Stub facade (not yet implemented)
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Stub implementation of ITransportBinding for SOME/IP.
 *              All communication methods return ComErrc::kCommunicationFailure.
 *              The composition pattern (managers + codec) will be added
 *              when the SOME/IP binding is actually implemented.
 *
 *              Future architecture:
 *              - CSomeIpServiceManager — service offer/find via SOME/IP-SD
 *              - CSomeIpEventManager   — event pub/sub via event groups
 *              - CSomeIpMethodManager  — method/field RPC via request/response
 *
 * @note        Priority: 60 (automotive standard — inter-ECU)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Stub facade — all ops return kCommunicationFailure
 * </table>
 */

#ifndef LAP_COM_SOMEIP_BINDING_HPP
#define LAP_COM_SOMEIP_BINDING_HPP

// ==================== Project-Internal Headers ====================
#include "SomeIpTypes.hpp"
#include "ITransportBinding.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/CResult.hpp>

namespace lap
{
namespace com
{
namespace binding
{

    // ====================================================================
    // SomeIpBinding — Stub Facade
    // ====================================================================

    /**
     * @brief   SOME/IP Transport Binding (Stub)
     *
     * @details All ITransportBinding operations return kCommunicationFailure.
     *          This class reserves the plugin slot so that the binding
     *          manager can enumerate it.  Actual SOME/IP communication
     *          (via vsomeip) will be implemented in a future release.
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
        // Member Variables
        // ================================================================

        SomeIpConfig        m_config;           ///< Binding configuration
        mutable Mutex       m_mutex;            ///< Main serialisation lock
        Bool                m_bInitialized;     ///< Initialisation state
        mutable TransportMetrics m_metrics;     ///< Transport metrics
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
