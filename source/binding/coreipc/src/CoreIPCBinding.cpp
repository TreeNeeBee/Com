/**
 * @file        CoreIPCBinding.cpp
 * @author      LightAP Development Team
 * @brief       Core IPC binding — Facade implementation
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Implements the CoreIPCBinding facade:
 *              - Constructor / Destructor (manager initialisation via init list)
 *              - Initialize / Shutdown lifecycle
 *              - ITransportBinding delegation to aggregated managers
 *              - Push-based discovery (StartFindService/StopFindService)
 *              - Field notification via event subsystem
 *              - Capability queries
 *              - C export factory functions
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/01/19  <td>1.0      <td>LightAP Team    <td>Initial implementation
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style refactor per code_rules.md
 * <tr><td>2026/02/07  <td>3.0      <td>Aii             <td>Composition refactor — facade + managers
 * <tr><td>2026/02/07  <td>4.0      <td>Aii             <td>StartFindService + FieldNotification impl
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CoreIPCBinding.hpp"
#include "CCoreIPCCodec.hpp"
#include "CRegistryProxy.hpp"
#include "ComTypes.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/log/CLog.hpp>

#include <chrono>
#include <cstring>
#include <thread>

namespace lap
{
namespace com
{
namespace binding
{

    using namespace lap::core;
    using namespace lap::core::ipc;
    using namespace lap::com::registry;

    // ====================================================================
    // File-Local Helpers — Shared Registry Singleton
    // ====================================================================

    namespace
    {
        Mutex                           g_registryMutex;
        SharedHandle< CRegistryProxy > g_pSharedRegistry;

        /**
         * @brief Discovery poll interval for push-based find
         */
        static constexpr auto kDiscoveryPollInterval =
            std::chrono::milliseconds( 200 );

        /**
         * @brief Special event ID prefix for field notifications
         * @note  Field ID + this offset = event channel for field change
         */
        static constexpr UInt32 kFieldNotificationEventOffset = 0xF000;

    } // anonymous namespace

    // ====================================================================
    // Constructor / Destructor
    // ====================================================================

    CoreIPCBinding::CoreIPCBinding() noexcept
        : m_bInitialized( false )
        , m_serviceManager( m_config, m_mapShmSegments,
                            m_pServiceRegistry, m_metrics )
        , m_eventManager( m_config, m_pServiceRegistry,
                          m_metrics, m_serviceManager )
        , m_methodManager( m_config, m_mapShmSegments,
                           m_pServiceRegistry, m_metrics, m_mutex )
        , m_iNextDiscoveryHandle( 1 )
        , m_bDiscoveryRunning( false )
    {
    }

    CoreIPCBinding::~CoreIPCBinding() noexcept
    {
        if ( m_bInitialized ) {
            static_cast< void > ( Shutdown() );
        }
    }

    // ====================================================================
    // Lifecycle Management
    // ====================================================================

    Result< void > CoreIPCBinding::Initialize() noexcept
    {
        LockGuard lock( m_mutex );

        if ( m_bInitialized ) {
            return Result< void >::FromValue();
        }

        LAP_LOG_INFO() << "[CoreIPCBinding] Initializing (composition model)";

        // Initialize CRegistryProxy (shared within process)
        {
            LockGuard regLock( g_registryMutex );
            if ( !g_pSharedRegistry ) {
                g_pSharedRegistry = MakeShared< CRegistryProxy > ();
                auto regResult = g_pSharedRegistry->Initialize();
                if ( !regResult ) {
                    LAP_LOG_ERROR() << "[CoreIPCBinding] Failed to initialize registry";
                    g_pSharedRegistry.reset();
                    return Result< void >::FromError( regResult.Error() );
                }
            }
            m_pServiceRegistry = g_pSharedRegistry;
        }

        // Initialize metrics
        m_metrics = {};
        m_metrics.bytesSent        = 0;
        m_metrics.bytesReceived    = 0;
        m_metrics.messagesSent     = 0;
        m_metrics.messagesReceived = 0;

        m_bInitialized = true;
        LAP_LOG_INFO() << "[CoreIPCBinding] Initialization complete";

        return Result< void >::FromValue();
    }

    Result< void > CoreIPCBinding::Shutdown() noexcept
    {
        LockGuard lock( m_mutex );

        if ( !m_bInitialized ) {
            return Result< void >::FromValue();
        }

        LAP_LOG_INFO() << "[CoreIPCBinding] Shutting down";

        // Stop all active discovery subscriptions
        stopAllDiscovery();

        // Shutdown managers in dependency order
        m_eventManager.Shutdown();
        m_methodManager.Shutdown();
        m_serviceManager.Clear();

        // Clear shared memory segments
        m_mapShmSegments.clear();

        // Release shared CRegistryProxy reference
        {
            LockGuard regLock( g_registryMutex );
            m_pServiceRegistry.reset();
            if ( g_pSharedRegistry && g_pSharedRegistry.use_count() == 1 ) {
                g_pSharedRegistry.reset();
            }
        }

        m_bInitialized = false;
        LAP_LOG_INFO() << "[CoreIPCBinding] Shutdown complete";

        return Result< void >::FromValue();
    }

    // ====================================================================
    // Service Management — delegates to CCoreIPCServiceManager
    // ====================================================================

    Result< void > CoreIPCBinding::OfferService(
        UInt64 serviceId, UInt64 instanceId ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized ) {
            return Result< void >::FromError(
                MakeErrorCode( CoreErrc::kIPCInvalidState ) );
        }
        return m_serviceManager.OfferService( serviceId, instanceId );
    }

    Result< void > CoreIPCBinding::StopOfferService(
        UInt64 serviceId, UInt64 instanceId ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized ) {
            return Result< void >::FromError(
                MakeErrorCode( CoreErrc::kIPCInvalidState ) );
        }
        // Stop method server first (cross-manager coordination)
        auto key = CCoreIPCCodec::MakeServiceKey( serviceId, instanceId );
        m_methodManager.StopServer( key );
        return m_serviceManager.StopOfferService( serviceId, instanceId );
    }

    Result< Vector< UInt64 > > CoreIPCBinding::FindService(
        UInt64 serviceId ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized ) {
            return Result< Vector< UInt64 > >::FromError(
                MakeErrorCode( CoreErrc::kIPCInvalidState ) );
        }
        return m_serviceManager.FindService( serviceId );
    }

    // ====================================================================
    // Push-Based Service Discovery
    // ====================================================================

    Result< UInt64 > CoreIPCBinding::StartFindService(
        UInt64 serviceId, ServiceDiscoveryCallback callback ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized ) {
            return Result< UInt64 >::FromError(
                MakeErrorCode( CoreErrc::kIPCInvalidState ) );
        }

        auto handle = m_iNextDiscoveryHandle.fetch_add(
            1, std::memory_order_relaxed );

        // Store the subscription
        DiscoverySubscription sub;
        sub.m_iServiceId = serviceId;
        sub.m_callback  = std::move( callback );
        sub.m_bActive    = true;
        m_mapDiscoverySubs.emplace( handle, std::move( sub ) );

        // Start the discovery polling thread if not running
        if ( !m_bDiscoveryRunning )
        {
            m_bDiscoveryRunning = true;
            m_discoveryThread = std::thread(
                &CoreIPCBinding::discoveryPollingWorker, this );
        }

        // Immediately fire initial callback with current state (outside lock)
        // Schedule via async to avoid callback under lock
        auto initialInstances = m_serviceManager.FindService( serviceId );
        if ( initialInstances.HasValue() )
        {
            auto& instances = initialInstances.Value();
            if ( !instances.empty() )
            {
                // Fire callback for initial state
                auto it = m_mapDiscoverySubs.find( handle );
                if ( it != m_mapDiscoverySubs.end() && it->second.m_bActive )
                {
                    it->second.m_callback( serviceId, instances );
                    it->second.m_vecLastKnownInstances = instances;
                }
            }
        }

        LAP_LOG_INFO() << "[CoreIPCBinding] StartFindService: handle="
                        << handle << " serviceId=0x" << serviceId;

        return Result< UInt64 >::FromValue( handle );
    }

    Result< void > CoreIPCBinding::StopFindService(
        UInt64 handle ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized ) {
            return Result< void >::FromError(
                MakeErrorCode( CoreErrc::kIPCInvalidState ) );
        }

        auto it = m_mapDiscoverySubs.find( handle );
        if ( it == m_mapDiscoverySubs.end() )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kInvalidArgument ) );
        }

        it->second.m_bActive = false;
        m_mapDiscoverySubs.erase( it );

        // Stop polling thread if no more subscriptions
        if ( m_mapDiscoverySubs.empty() )
        {
            stopAllDiscovery();
        }

        LAP_LOG_INFO() << "[CoreIPCBinding] StopFindService: handle=" << handle;

        return Result< void >::FromValue();
    }

    void CoreIPCBinding::discoveryPollingWorker() noexcept
    {
        while ( m_bDiscoveryRunning )
        {
            std::this_thread::sleep_for( kDiscoveryPollInterval );

            if ( !m_bDiscoveryRunning )
            {
                break;
            }

            // Take snapshot of subscriptions
            Map< UInt64, DiscoverySubscription > snapshot;
            {
                LockGuard lock( m_mutex );
                snapshot = m_mapDiscoverySubs;
            }

            // Poll each subscription
            for ( auto& pair : snapshot )
            {
                if ( !pair.second.m_bActive )
                {
                    continue;
                }

                Vector< UInt64 > currentInstances;
                {
                    LockGuard lock( m_mutex );
                    auto findResult = m_serviceManager.FindService(
                        pair.second.m_iServiceId );
                    if ( findResult.HasValue() )
                    {
                        currentInstances = findResult.Value();
                    }
                }

                // Compare with last known state — fire callback on change
                if ( currentInstances != pair.second.m_vecLastKnownInstances )
                {
                    pair.second.m_callback(
                        pair.second.m_iServiceId, currentInstances );

                    // Update the tracked state
                    LockGuard lock( m_mutex );
                    auto it = m_mapDiscoverySubs.find( pair.first );
                    if ( it != m_mapDiscoverySubs.end() )
                    {
                        it->second.m_vecLastKnownInstances = currentInstances;
                    }
                }
            }
        }
    }

    void CoreIPCBinding::stopAllDiscovery() noexcept
    {
        m_bDiscoveryRunning = false;
        if ( m_discoveryThread.joinable() )
        {
            m_discoveryThread.join();
        }
        m_mapDiscoverySubs.clear();
    }

    // ====================================================================
    // Event Communication — Do* NVI overrides (delegates to CCoreIPCEventManager)
    // ====================================================================

    Result< void > CoreIPCBinding::DoSendEvent(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 eventId, const void* pData,
        Size dataSize ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized ) {
            return Result< void >::FromError(
                MakeErrorCode( CoreErrc::kIPCInvalidState ) );
        }
        if ( dataSize > 0 ) {
            // memcpy fallback: raw bytes into ByteBuffer for wire transport
            ByteBuffer data(
                reinterpret_cast< const uint8_t* >( pData ),
                reinterpret_cast< const uint8_t* >( pData ) + dataSize );
            return m_eventManager.SendEvent(
                serviceId, instanceId, eventId, data );
        }
        // Phase 1 compat: pData points to a ByteBuffer
        const auto& data = *static_cast< const ByteBuffer* >( pData );
        return m_eventManager.SendEvent( serviceId, instanceId, eventId, data );
    }

    Result< void > CoreIPCBinding::DoSubscribeEvent(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 eventId, EventCallback callback,
        Size dataSize ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized ) {
            return Result< void >::FromError(
                MakeErrorCode( CoreErrc::kIPCInvalidState ) );
        }
        if ( dataSize > 0 ) {
            // Wrap callback: internal manager dispatches ByteBuffer*,
            // outer callback expects pointer to raw T data
            auto typedCallback = [callback, dataSize](
                UInt64 svcId, UInt64 instId, UInt32 evtId,
                const void* pData )
            {
                const auto& buf =
                    *static_cast< const ByteBuffer* >( pData );
                if ( buf.size() >= dataSize ) {
                    callback( svcId, instId, evtId, buf.data() );
                }
            };
            return m_eventManager.SubscribeEvent(
                serviceId, instanceId, eventId, typedCallback );
        }
        // Phase 1 compat: pass through directly
        return m_eventManager.SubscribeEvent(
            serviceId, instanceId, eventId, callback );
    }

    Result< void > CoreIPCBinding::UnsubscribeEvent(
        UInt64 serviceId, UInt64 instanceId, UInt32 eventId ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized ) {
            return Result< void >::FromError(
                MakeErrorCode( CoreErrc::kIPCInvalidState ) );
        }
        return m_eventManager.UnsubscribeEvent( serviceId, instanceId, eventId );
    }

    // ====================================================================
    // Method Communication — Do* NVI overrides (delegates to CCoreIPCMethodManager)
    // (self-locking: do NOT hold facade mutex)
    // ====================================================================

    Result< void > CoreIPCBinding::DoCallMethod(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 methodId, const void* pRequest, void* pResponse,
        Size requestSize, Size responseSize ) noexcept
    {
        if ( !m_bInitialized ) {
            return Result< void >::FromError(
                MakeErrorCode( CoreErrc::kIPCInvalidState ) );
        }

        // ── Request serialization: memcpy path vs Phase 1 ByteBuffer ──
        Result< ByteBuffer > result = [&]() -> Result< ByteBuffer > {
            if ( requestSize > 0 ) {
                ByteBuffer request(
                    reinterpret_cast< const uint8_t* >( pRequest ),
                    reinterpret_cast< const uint8_t* >( pRequest ) + requestSize );
                return m_methodManager.CallMethod(
                    serviceId, instanceId, methodId, request );
            }
            const auto& request = *static_cast< const ByteBuffer* >( pRequest );
            return m_methodManager.CallMethod(
                serviceId, instanceId, methodId, request );
        }();

        if ( !result.HasValue() ) {
            return Result< void >::FromError( result.Error() );
        }

        // ── Response deserialization: memcpy path vs Phase 1 ByteBuffer ──
        auto& response = result.Value();
        if ( responseSize > 0 && response.size() >= responseSize ) {
            ::std::memcpy( pResponse, response.data(), responseSize );
        } else {
            *static_cast< ByteBuffer* >( pResponse ) = ::std::move( response );
        }
        return Result< void >::FromValue();
    }

    Result< void > CoreIPCBinding::DoRegisterMethod(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 methodId, MethodHandler handler,
        Size requestSize, Size responseSize ) noexcept
    {
        if ( !m_bInitialized ) {
            return Result< void >::FromError(
                MakeErrorCode( CoreErrc::kIPCInvalidState ) );
        }
        if ( requestSize > 0 || responseSize > 0 ) {
            // Wrap handler: internal manager passes ByteBuffer* for pReq/pResp;
            // outer handler expects typed data pointers
            auto wrappedHandler = [handler, requestSize, responseSize](
                UInt64 svcId, UInt64 instId, UInt32 mthId,
                const void* pReq, void* pResp )
            {
                const auto& reqBuf =
                    *static_cast< const ByteBuffer* >( pReq );
                auto& respBuf =
                    *static_cast< ByteBuffer* >( pResp );

                // Typed request: point into ByteBuffer raw data
                const void* pTypedReq =
                    ( requestSize > 0 ) ? reqBuf.data() : pReq;

                // Typed response: allocate temporary buffer
                ByteBuffer tempResp;
                void* pTypedResp = nullptr;
                if ( responseSize > 0 ) {
                    tempResp.resize( responseSize, 0 );
                    pTypedResp = tempResp.data();
                } else {
                    pTypedResp = pResp;
                }

                handler( svcId, instId, mthId, pTypedReq, pTypedResp );

                // Copy typed response back to ByteBuffer
                if ( responseSize > 0 ) {
                    respBuf.assign( tempResp.begin(), tempResp.end() );
                }
            };
            return m_methodManager.RegisterMethod(
                serviceId, instanceId, methodId,
                ::std::move( wrappedHandler ) );
        }
        return m_methodManager.RegisterMethod(
            serviceId, instanceId, methodId, ::std::move( handler ) );
    }

    // ====================================================================
    // Field Communication — Do* NVI overrides
    //                        and CCoreIPCEventManager (notifications)
    // ====================================================================

    Result< void > CoreIPCBinding::DoGetField(
        UInt64 serviceId, UInt64 instanceId, UInt32 fieldId,
        void* pOutValue, Size valueSize ) noexcept
    {
        if ( !m_bInitialized ) {
            return Result< void >::FromError(
                MakeErrorCode( CoreErrc::kIPCInvalidState ) );
        }
        auto result = m_methodManager.GetField( serviceId, instanceId, fieldId );
        if ( !result.HasValue() ) {
            return Result< void >::FromError( result.Error() );
        }
        auto& response = result.Value();
        if ( valueSize > 0 && response.size() >= valueSize ) {
            ::std::memcpy( pOutValue, response.data(), valueSize );
        } else {
            *static_cast< ByteBuffer* >( pOutValue ) = ::std::move( response );
        }
        return Result< void >::FromValue();
    }

    Result< void > CoreIPCBinding::DoSetField(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 fieldId, const void* pValue,
        Size valueSize ) noexcept
    {
        if ( !m_bInitialized ) {
            return Result< void >::FromError(
                MakeErrorCode( CoreErrc::kIPCInvalidState ) );
        }
        if ( valueSize > 0 ) {
            ByteBuffer data(
                reinterpret_cast< const uint8_t* >( pValue ),
                reinterpret_cast< const uint8_t* >( pValue ) + valueSize );
            return m_methodManager.SetField(
                serviceId, instanceId, fieldId, data );
        }
        // Phase 1 compat: pValue points to a ByteBuffer
        const auto& data = *static_cast< const ByteBuffer* >( pValue );
        return m_methodManager.SetField( serviceId, instanceId, fieldId, data );
    }

    /**
     * @brief Subscribe to field change notifications
     *
     * @details Field notifications are implemented by subscribing to a
     *          synthetic event channel. The event ID is computed as:
     *              fieldId + kFieldNotificationEventOffset
     *          This allows field notifications to piggyback on the existing
     *          event infrastructure without a separate notification mechanism.
     *
     * @note Provider side: When a field is updated via SetField(), the provider
     *       skeleton should also call SendEvent() with the synthetic event ID
     *       to notify subscribers. This is handled at the skeleton level.
     */
    Result< void > CoreIPCBinding::DoSubscribeFieldNotification(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 fieldId, FieldNotificationCallback callback,
        Size valueSize ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized ) {
            return Result< void >::FromError(
                MakeErrorCode( CoreErrc::kIPCInvalidState ) );
        }

        // Map field notification to event subscription on synthetic event ID
        UInt32 syntheticEventId = fieldId + kFieldNotificationEventOffset;

        // Adapt FieldNotificationCallback → EventCallback
        // Both use const void* in NVI pattern, just map eventId → fieldId
        auto eventAdapter = [callback, fieldId, valueSize](
            UInt64 svcId, UInt64 instId, UInt32 evtId,
            const void* pData )
        {
            static_cast< void > ( evtId );
            if ( valueSize > 0 ) {
                // Typed path: extract raw bytes from ByteBuffer
                const auto& buf =
                    *static_cast< const ByteBuffer* >( pData );
                if ( buf.size() >= valueSize ) {
                    callback( svcId, instId, fieldId, buf.data() );
                    return;
                }
            }
            callback( svcId, instId, fieldId, pData );
        };

        return m_eventManager.SubscribeEvent(
            serviceId, instanceId, syntheticEventId, std::move( eventAdapter ) );
    }

    Result< void > CoreIPCBinding::UnsubscribeFieldNotification(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 fieldId ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized ) {
            return Result< void >::FromError(
                MakeErrorCode( CoreErrc::kIPCInvalidState ) );
        }

        UInt32 syntheticEventId = fieldId + kFieldNotificationEventOffset;
        return m_eventManager.UnsubscribeEvent(
            serviceId, instanceId, syntheticEventId );
    }

    // ====================================================================
    // Capability Queries
    // ====================================================================

    Bool CoreIPCBinding::SupportsService( UInt64 serviceId ) const noexcept
    {
        static_cast< void > ( serviceId );
        return true;
    }

    TransportMetrics CoreIPCBinding::GetMetrics() const noexcept
    {
        LockGuard lock( m_mutex );
        return m_metrics;
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
    return new lap::com::binding::CoreIPCBinding();
}

void DestroyBindingInstance( lap::com::binding::ITransportBinding* instance )
{
    delete instance;
}

} // extern "C"
