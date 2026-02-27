/**
 * @file        DdsBinding.hpp
 * @author      LightAP Development Team
 * @brief       DDS transport binding — Facade class
 * @date        2026/02/07
 * @details     Facade implementing ITransportBinding by aggregating:
 *              - CDdsServiceManager — service lifecycle
 *              - CDdsEventManager   — event pub/sub
 *              - CDdsMethodManager  — method/field RPC
 *
 *              Owns all shared DDS resources (participant, publisher,
 *              subscriber, discovery listener, maps) and delegates each
 *              ITransportBinding operation to the appropriate manager.
 *
 * @copyright   Copyright (c) 2026
 *
 * @note        AUTOSAR R25-11 Compliance:
 *              - TR_DDSS_00001-00007: DDS Security Integration
 *              - SWS_CM_00400: Transport Binding Interface
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/23  <td>1.0      <td>LightAP Team    <td>Initial DDS Binding implementation
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style refactor per code_rules.md
 * <tr><td>2026/02/07  <td>3.0      <td>Aii             <td>Composition refactor — facade + managers
 * </table>
 */

#ifndef LAP_COM_DDS_BINDING_HPP
#define LAP_COM_DDS_BINDING_HPP

// ==================== Project-Internal Headers ====================
#include "DdsTypes.hpp"
#include "DdsReaderListener.hpp"
#include "DdsDiscoveryListener.hpp"
#include "CDdsServiceManager.hpp"
#include "CDdsEventManager.hpp"
#include "CDdsMethodManager.hpp"
#include "ITransportBinding.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/CResult.hpp>

// ==================== Third-Party Headers ====================
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>

// Code-defined DDS wire type (replaces static LapComMessage.idl)
#include "CDdsPayload.hpp"

// ==================== Standard Library Headers ====================
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace lap
{
namespace com
{
namespace binding
{

    // ====================================================================
    // DdsBinding — Facade
    // ====================================================================

    /**
     * @brief   DDS Transport Binding (Facade)
     *
     * @details Implements ITransportBinding by aggregating three manager
     *          classes.  Owns the single mutex that serialises most
     *          operations, plus all shared DDS resources the managers
     *          reference.
     *
     *          Composition / aggregation pattern:
     *          - CDdsServiceManager  m_serviceManager
     *          - CDdsEventManager    m_eventManager
     *          - CDdsMethodManager   m_methodManager
     *
     * @note    Priority: 80 (lower than coreipc 100, higher than legacy 10)
     *          Thread-safe for all public methods
     *
     * @compliance  SWS_CM_00400 - Transport Binding Interface
     *              SWS_CM_00401 - Binding Lifecycle Management
     */
    class DdsBinding : public ITransportBinding
    {
    public:
        DdsBinding();
        ~DdsBinding() override;

        // Rule of Five — non-copyable, non-movable
        DdsBinding( const DdsBinding& )             = delete;
        DdsBinding& operator=( const DdsBinding& )  = delete;
        DdsBinding( DdsBinding&& )                  = delete;
        DdsBinding& operator=( DdsBinding&& )       = delete;

    public:
        // ================================================================
        // Lifecycle
        // ================================================================

        Result< void > Initialize() noexcept override;
        Result< void > Shutdown() noexcept override;
        void Configure( const Map< String, String >& params ) noexcept override;

        /// @brief DDS binding supports typed adapters (CDR via fastddsgen PubSubType)
        Bool SupportsTypedAdapters() const noexcept override { return true; }

        /// @brief Check CDdsTypeRegistry for a specific event adapter
        Bool HasEventAdapter(
            UInt64 serviceId, UInt32 eventId ) const noexcept override;

    public:
        // ================================================================
        // Service Management (delegates to CDdsServiceManager)
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
        // Event Communication (public virtual — UnsubscribeEvent only)
        // ================================================================

        Result< void > UnsubscribeEvent(
            UInt64 serviceId, UInt64 instanceId,
            UInt32 eventId ) noexcept override;

    public:
        // ================================================================
        // Field Communication (public virtual — unsubscribe only)
        // ================================================================

        Result< void > UnsubscribeFieldNotification(
            UInt64 serviceId, UInt64 instanceId,
            UInt32 fieldId ) noexcept override;

    public:
        // ================================================================
        // Capability Queries
        // ================================================================

        const char* GetName() const noexcept override { return "DDS"; }
        UInt32 GetVersion() const noexcept override { return 0x00010000U; }
        UInt32 GetPriority() const noexcept override { return 80U; }
        Bool SupportsZeroCopy() const noexcept override { return m_config.m_bAfXdpEnabled; }
        Bool SupportsService( UInt64 serviceId ) const noexcept override;
        TransportMetrics GetMetrics() const noexcept override;

    public:
        // ================================================================
        // Configuration
        // ================================================================

        /**
         * @brief   Set discovery server address before Initialize()
         * @param   address  Server address (e.g., "tcp://192.168.1.1:42100")
         */
        void SetDiscoveryServer( const String& address ) noexcept;

    protected:
        // ================================================================
        // NVI Do* Overrides (type-erased virtual implementations)
        // ================================================================

        Result< void > DoPrepareEventChannel(
            UInt64 serviceId, UInt64 instanceId,
            UInt32 eventId ) noexcept override;

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

    private:
        // ================================================================
        // Shared Resources (declared BEFORE managers for init order)
        // ================================================================

        DdsConfig           m_config;               ///< Binding configuration
        mutable Mutex       m_mutex;                ///< Main serialisation lock

        /// DDS core entities
        eprosima::fastdds::dds::DomainParticipant*  m_pParticipant  = nullptr;
        eprosima::fastdds::dds::Publisher*           m_pPublisher    = nullptr;
        eprosima::fastdds::dds::Subscriber*          m_pSubscriber   = nullptr;
        eprosima::fastdds::dds::TypeSupport          m_typeSupport;

        /// Discovery listener (raw pointer passed to managers, ownership below)
        DdsDiscoveryListener*   m_pDiscoveryListener = nullptr;
        UniqueHandle< DdsDiscoveryListener > m_pOwnedDiscoveryListener;

        /// Transport metrics
        mutable TransportMetrics    m_metrics;

        // ================================================================
        // Push Discovery (StartFindService / StopFindService)
        // ================================================================

        /**
         * @brief   Active find-service subscription
         * @details Tracks a push-based discovery request registered via
         *          StartFindService().  The callback is invoked whenever
         *          the DdsDiscoveryListener detects a change for the
         *          matching serviceId.
         */
        struct FindSubscription
        {
            UInt64                      serviceId;  ///< Monitored service
            ServiceDiscoveryCallback    callback;   ///< User callback
        };

        /// Monotonic handle generator for find subscriptions
        Atomic< UInt64 >     m_iNextFindHandle { 1 };

        /// Map: handle -> FindSubscription (protected by m_mutex)
        ::std::unordered_map< UInt64, FindSubscription >  m_mapFindSubscriptions;

        // ================================================================
        // Aggregated Manager Objects
        // ================================================================

        CDdsServiceManager  m_serviceManager;       ///< Service lifecycle
        CDdsEventManager    m_eventManager;         ///< Event communication
        CDdsMethodManager   m_methodManager;        ///< Method/Field RPC

        // ================================================================
        // Internal Helpers
        // ================================================================

        /**
         * @brief   Called from DdsDiscoveryListener when service availability changes
         * @param   serviceId   The service whose availability changed
         * @param   instances   Current set of available instance IDs
         */
        void OnDiscoveryChange(
            UInt64 serviceId, Vector< UInt64 > instances ) noexcept;
    };

} // namespace binding
} // namespace com
} // namespace lap

// ==================== C Export Functions ====================
extern "C" {
    lap::com::binding::ITransportBinding* CreateBindingInstance();
    void DestroyBindingInstance( lap::com::binding::ITransportBinding* instance );
}

#endif // LAP_COM_DDS_BINDING_HPP
