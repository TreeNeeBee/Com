/**
 * @file        CoreIPCBinding.hpp
 * @author      LightAP Development Team
 * @brief       Core IPC zero-copy transport binding — Facade class
 * @date        2026/02/07
 * @details     Facade implementing ITransportBinding by aggregating:
 *              - CCoreIPCServiceManager — service lifecycle
 *              - CCoreIPCEventManager   — event pub/sub
 *              - CCoreIPCMethodManager  — method/field RPC
 *              Plus push-based discovery polling and field notification
 *              via synthetic event channels.
 *
 *              Owns all shared resources (mutex, config, SHM map, registry,
 *              metrics) and delegates each ITransportBinding operation to the
 *              appropriate manager class.
 *
 * @copyright   Copyright (c) 2026
 *
 * @compliance  AUTOSAR SWS_CM_00400 - Transport Binding Interface
 *              AUTOSAR SWS_CM_00401 - Binding Management
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/01/19  <td>1.0      <td>LightAP Team    <td>Initial implementation
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style refactor per code_rules.md
 * <tr><td>2026/02/07  <td>3.0      <td>Aii             <td>Composition refactor — facade + managers
 * <tr><td>2026/02/07  <td>4.0      <td>Aii             <td>Push discovery + field notification impl
 * </table>
 */

#ifndef LAP_COM_CORE_IPC_BINDING_HPP
#define LAP_COM_CORE_IPC_BINDING_HPP

// ==================== Project-Internal Headers ====================
#include "CoreIPCTypes.hpp"
#include "CCoreIPCServiceManager.hpp"
#include "CCoreIPCEventManager.hpp"
#include "CCoreIPCMethodManager.hpp"
#include "ITransportBinding.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/CResult.hpp>

// ==================== Standard Library Headers ====================
#include <thread>

namespace lap
{
namespace com
{
namespace binding
{

    // ====================================================================
    // CoreIPCBinding — Facade
    // ====================================================================

    /**
     * @brief   Core IPC zero-copy transport binding (Facade)
     *
     * @details Implements ITransportBinding by aggregating three manager
     *          classes.  Owns the single mutex that serialises most
     *          operations, plus all shared resources the managers reference.
     *
     *          Composition / aggregation pattern:
     *          - CCoreIPCServiceManager  m_serviceManager
     *          - CCoreIPCEventManager    m_eventManager
     *          - CCoreIPCMethodManager   m_methodManager
     *
     * @note    Thread-safe for all public methods
     *
     * @compliance  SWS_CM_00400 - Transport Binding Interface
     *              SWS_CM_00401 - Binding Lifecycle Management
     */
    class CoreIPCBinding : public ITransportBinding
    {
    public:
        CoreIPCBinding() noexcept;
        ~CoreIPCBinding() noexcept override;

        // Rule of Five — non-copyable, non-movable
        CoreIPCBinding( const CoreIPCBinding& )             = delete;
        CoreIPCBinding& operator=( const CoreIPCBinding& )  = delete;
        CoreIPCBinding( CoreIPCBinding&& )                  = delete;
        CoreIPCBinding& operator=( CoreIPCBinding&& )       = delete;

    public:
        // ================================================================
        // Lifecycle
        // ================================================================

        Result< void > Initialize() noexcept override;
        Result< void > Shutdown() noexcept override;

    public:
        // ================================================================
        // Service Management (delegates to CCoreIPCServiceManager)
        // ================================================================

        Result< void > OfferService( UInt64 serviceId,
                                      UInt64 instanceId ) noexcept override;

        Result< void > StopOfferService( UInt64 serviceId,
                                          UInt64 instanceId ) noexcept override;

        Result< Vector< UInt64 > > FindService(
            UInt64 serviceId ) noexcept override;

        Result< UInt64 > StartFindService(
            UInt64 serviceId,
            ServiceDiscoveryCallback callback ) noexcept override;

        Result< void > StopFindService(
            UInt64 handle ) noexcept override;

    public:
        // ================================================================
        // Event Communication (delegates to CCoreIPCEventManager)
        // ================================================================

        Result< void > UnsubscribeEvent( UInt64 serviceId, UInt64 instanceId,
                                          UInt32 eventId ) noexcept override;

    public:
        // ================================================================
        // Field Communication (delegates to CCoreIPCMethodManager / EventManager)
        // ================================================================

        Result< void > UnsubscribeFieldNotification(
            UInt64 serviceId, UInt64 instanceId,
            UInt32 fieldId ) noexcept override;

    protected:
        // ================================================================
        // NVI Do* Overrides (type-erased virtual implementations)
        // ================================================================

        Result< void > DoSendEvent( UInt64 serviceId, UInt64 instanceId,
                                     UInt32 eventId,
                                     const void* pData,
                                     Size dataSize = 0 ) noexcept override;

        Result< void > DoSubscribeEvent( UInt64 serviceId, UInt64 instanceId,
                                          UInt32 eventId,
                                          EventCallback callback,
                                          Size dataSize = 0 ) noexcept override;

        Result< void > DoCallMethod( UInt64 serviceId, UInt64 instanceId,
                                      UInt32 methodId,
                                      const void* pRequest,
                                      void* pResponse,
                                      Size requestSize  = 0,
                                      Size responseSize = 0 ) noexcept override;

        Result< void > DoRegisterMethod( UInt64 serviceId, UInt64 instanceId,
                                          UInt32 methodId,
                                          MethodHandler handler,
                                          Size requestSize  = 0,
                                          Size responseSize = 0 ) noexcept override;

        Result< void > DoGetField( UInt64 serviceId, UInt64 instanceId,
                                    UInt32 fieldId,
                                    void* pOutValue,
                                    Size valueSize = 0 ) noexcept override;

        Result< void > DoSetField( UInt64 serviceId, UInt64 instanceId,
                                    UInt32 fieldId,
                                    const void* pValue,
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

        const char* GetName() const noexcept override { return "coreipc"; }
        UInt32 GetPriority() const noexcept override { return 100U; }
        UInt32 GetVersion() const noexcept override { return 0x010000U; }
        Bool SupportsZeroCopy() const noexcept override { return true; }
        Bool SupportsService( UInt64 serviceId ) const noexcept override;
        TransportMetrics GetMetrics() const noexcept override;

    private:
        // ================================================================
        // Shared Resources (declared BEFORE managers for init order)
        // ================================================================

        mutable Mutex           m_mutex;            ///< Main serialisation lock
        Bool                    m_bInitialized;     ///< Initialisation state
        CoreIPCConfig           m_config;           ///< Binding configuration
        ShmSegmentMap           m_mapShmSegments;   ///< Shared SHM segments
        SharedHandle< registry::CRegistryProxy > m_pServiceRegistry; ///< Registry proxy
        mutable TransportMetrics m_metrics;         ///< Transport metrics

        // ================================================================
        // Aggregated Manager Objects
        // ================================================================

        CCoreIPCServiceManager  m_serviceManager;   ///< Service lifecycle
        CCoreIPCEventManager    m_eventManager;     ///< Event communication
        CCoreIPCMethodManager   m_methodManager;    ///< Method/Field RPC

        // ================================================================
        // Push-Based Discovery State
        // ================================================================

        /**
         * @brief Active discovery subscription record
         */
        struct DiscoverySubscription
        {
            UInt64                      m_iServiceId;              ///< Tracked service ID
            ServiceDiscoveryCallback    m_callback;                ///< User callback
            Bool                        m_bActive;                 ///< Active flag
            Vector< UInt64 >            m_vecLastKnownInstances;   ///< For change detection
        };

        Atomic< UInt64 >            m_iNextDiscoveryHandle;  ///< Handle allocator
        Bool                        m_bDiscoveryRunning;     ///< Polling thread active
        ::std::thread               m_discoveryThread;       ///< Polling thread

        /// Active subscriptions: handle → subscription
        Map< UInt64, DiscoverySubscription > m_mapDiscoverySubs;

        /**
         * @brief Discovery polling worker (runs in m_discoveryThread)
         */
        void discoveryPollingWorker() noexcept;

        /**
         * @brief Stop all active discovery subscriptions and join thread
         */
        void stopAllDiscovery() noexcept;
    };

} // namespace binding
} // namespace com
} // namespace lap

// ==================== C Export Functions ====================
extern "C" {
    lap::com::binding::ITransportBinding* CreateBindingInstance();
    void DestroyBindingInstance( lap::com::binding::ITransportBinding* instance );
}

#endif // LAP_COM_CORE_IPC_BINDING_HPP
