/**
 * @file        DdsBinding.cpp
 * @author      LightAP Development Team
 * @brief       DDS binding — Facade implementation
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Implements the DdsBinding facade:
 *              - Constructor / Destructor (manager initialisation via init list)
 *              - Initialize / Shutdown lifecycle (DDS participant, pub, sub)
 *              - ITransportBinding delegation to aggregated managers
 *              - Capability queries
 *              - C export factory functions
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/23  <td>1.0      <td>LightAP Team    <td>Initial DDS Binding implementation
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style refactor per code_rules.md
 * <tr><td>2026/02/07  <td>3.0      <td>Aii             <td>Composition refactor — facade + managers
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "DdsBinding.hpp"
#include "CDdsCodec.hpp"
#include "CDdsPayload.hpp"
#include "CDdsTypeRegistry.hpp"
#include "ComTypes.hpp"

// ==================== Third-Party Headers ====================
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/dds/publisher/qos/PublisherQos.hpp>
#include <fastdds/dds/subscriber/qos/SubscriberQos.hpp>
#include <fastdds/rtps/common/Locator.hpp>
#include <fastdds/utils/IPLocator.hpp>
#include <fastdds/rtps/transport/TCPv4TransportDescriptor.hpp>
#include <fastdds/rtps/transport/shared_mem/SharedMemTransportDescriptor.hpp>

// ==================== Standard Library Headers ====================
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>

namespace lap
{
namespace com
{
namespace binding
{

    using namespace eprosima::fastdds::dds;

    // ====================================================================
    // Constructor / Destructor
    // ====================================================================

    DdsBinding::DdsBinding()
        : m_serviceManager( m_config, m_pParticipant, m_pPublisher,
                            m_pSubscriber, m_typeSupport,
                            m_pDiscoveryListener, m_metrics )
        , m_eventManager( m_config, m_pParticipant, m_pPublisher,
                          m_pSubscriber, m_typeSupport, m_metrics )
        , m_methodManager( m_config, m_pParticipant, m_pPublisher,
                           m_pSubscriber, m_typeSupport, m_metrics )
    {
        LAP_COM_LOG_INFO << "DdsBinding instance created (FastDDS backend)";

        // Register code-defined DDS wire type (no .idl dependency)
        m_typeSupport.reset( new DdsPayloadPubSubType() );
    }

    DdsBinding::~DdsBinding()
    {
        static_cast< void > ( Shutdown() );
        LAP_COM_LOG_INFO << "DdsBinding instance destroyed";
    }

    // ====================================================================
    // Lifecycle Management
    // ====================================================================

    void DdsBinding::Configure(
        const Map< String, String >& params ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( m_pParticipant != nullptr ) {
            LAP_COM_LOG_WARN << "Configure ignored after Initialize";
            return;
        }

        auto tryGet = [&params]( const String& key ) -> const String* {
            auto it = params.find( key );
            return ( it != params.end() ) ? &it->second : nullptr;
        };

        if ( const auto* val = tryGet( "discovery_server" ) ) {
            m_config.m_strDiscoveryServer = *val;
        }
        if ( const auto* val = tryGet( "domain_id" ) ) {
            try {
                m_config.m_iDomainId = static_cast< UInt32 > (
                    ::std::stoul( *val ) );
            } catch ( const ::std::exception& ) {
                LAP_COM_LOG_WARN << "Invalid domain_id: " << *val;
            }
        }
        if ( const auto* val = tryGet( "shared_memory" ) ) {
            m_config.m_bUseSharedMemory = ( *val == "true" || *val == "1" );
        }
        if ( const auto* val = tryGet( "data_sharing" ) ) {
            m_config.m_bDataSharingEnabled = ( *val == "true" || *val == "1" );
        }
        if ( const auto* val = tryGet( "tcp_transport" ) ) {
            m_config.m_bUseTcpTransport = ( *val == "true" || *val == "1" );
        }
        if ( const auto* val = tryGet( "max_payload_size" ) ) {
            try {
                m_config.m_iMaxPayloadSize = static_cast< UInt32 > (
                    ::std::stoul( *val ) );
            } catch ( const ::std::exception& ) {
                LAP_COM_LOG_WARN << "Invalid max_payload_size: " << *val;
            }
        }
        if ( const auto* val = tryGet( "ds_health_check_interval_ms" ) ) {
            try {
                m_dsMonitorConfig.m_healthCheckInterval =
                    std::chrono::milliseconds( ::std::stoul( *val ) );
            } catch ( const ::std::exception& ) { /* ignore */ }
        }
        if ( const auto* val = tryGet( "ds_max_failures" ) ) {
            try {
                m_dsMonitorConfig.m_iMaxFailuresBeforeFallback =
                    static_cast< UInt32 >( ::std::stoul( *val ) );
            } catch ( const ::std::exception& ) { /* ignore */ }
        }
        if ( const auto* val = tryGet( "ds_reconnect_interval_ms" ) ) {
            try {
                m_dsMonitorConfig.m_reconnectInterval =
                    std::chrono::milliseconds( ::std::stoul( *val ) );
            } catch ( const ::std::exception& ) { /* ignore */ }
        }
        if ( const auto* val = tryGet( "ds_enable_fallback" ) ) {
            m_dsMonitorConfig.m_bEnableFallback = ( *val == "true" || *val == "1" );
        }
        if ( const auto* val = tryGet( "ds_enable_reconnect" ) ) {
            m_dsMonitorConfig.m_bEnableReconnect = ( *val == "true" || *val == "1" );
        }

        LAP_COM_LOG_INFO << "DdsBinding configured with "
                         << params.size() << " parameter(s)";
    }

    Result< void > DdsBinding::Initialize() noexcept
    {
        LockGuard lock( m_mutex );

        if ( m_pParticipant != nullptr ) {
            LAP_COM_LOG_WARN << "DdsBinding already initialized";
            return Result< void >::FromValue();
        }

        LAP_COM_LOG_INFO << "Initializing DDS Binding (FastDDS) on domain "
                         << m_config.m_iDomainId;

        // Create discovery listener BEFORE participant
        auto pDiscListener = MakeUnique< DdsDiscoveryListener > ( this );
        m_pDiscoveryListener = pDiscListener.get();

        // Configure participant QoS
        DomainParticipantQos pqos;
        pqos.name( "LightAP_DDS_Participant" );

        auto configureSimpleEdp = [&pqos]() {
            pqos.wire_protocol().builtin.discovery_config.discoveryProtocol =
                eprosima::fastdds::rtps::DiscoveryProtocol::SIMPLE;
            pqos.wire_protocol().builtin.discovery_config
                .use_SIMPLE_EndpointDiscoveryProtocol = true;
            pqos.wire_protocol().builtin.discovery_config
                .use_STATIC_EndpointDiscoveryProtocol = false;
            pqos.wire_protocol().builtin.discovery_config.m_DiscoveryServers.clear();
        };

        Bool bUseTcpTransport = false;

        auto configureDiscoveryServer = [&pqos, this, &bUseTcpTransport]() -> Bool {
            if ( m_config.m_strDiscoveryServer.empty() ) {
                return false;
            }

            String host = m_config.m_strDiscoveryServer;
            UInt32 port = pqos.wire_protocol().port.getUnicastPort(
                m_config.m_iDomainId, 0 );
            Int32 locatorKind = LOCATOR_KIND_UDPv4;

            const String tcpPrefix = "tcp://";
            const String udpPrefix = "udp://";

            if ( host.rfind( tcpPrefix, 0 ) == 0 ) {
                host.erase( 0, tcpPrefix.size() );
                locatorKind = LOCATOR_KIND_TCPv4;
                port = 42100;
                bUseTcpTransport = true;
            } else if ( host.rfind( udpPrefix, 0 ) == 0 ) {
                host.erase( 0, udpPrefix.size() );
                locatorKind = LOCATOR_KIND_UDPv4;
            }

            const auto pos = host.find( ':' );
            if ( pos != String::npos ) {
                const String portStr = host.substr( pos + 1 );
                host = host.substr( 0, pos );
                try {
                    port = static_cast< UInt32 > ( ::std::stoul( portStr ) );
                } catch ( const ::std::exception& ) {
                    LAP_COM_LOG_WARN << "Invalid discovery server port, fallback to Simple EDP";
                    return false;
                }
            }

            eprosima::fastdds::rtps::Locator_t serverLocator;
            serverLocator.kind = locatorKind;
            serverLocator.port = port;
            if ( !eprosima::fastdds::rtps::IPLocator::setIPv4( serverLocator, host ) ) {
                LAP_COM_LOG_WARN << "Invalid discovery server address, fallback to Simple EDP";
                return false;
            }

            // SUPER_CLIENT routes both PDP (participant) and EDP (endpoint)
            // discovery through the server.  Plain CLIENT only routes PDP,
            // which can leave endpoint (writer / reader) discovery broken
            // in environments without direct P2P metatraffic connectivity
            // (e.g. containers with restricted multicast).
            pqos.wire_protocol().builtin.discovery_config.discoveryProtocol =
                eprosima::fastdds::rtps::DiscoveryProtocol::SUPER_CLIENT;
            pqos.wire_protocol().builtin.discovery_config
                .use_SIMPLE_EndpointDiscoveryProtocol = true;
            pqos.wire_protocol().builtin.discovery_config
                .use_STATIC_EndpointDiscoveryProtocol = false;
            pqos.wire_protocol().builtin.discovery_config.m_DiscoveryServers.clear();
            pqos.wire_protocol().builtin.discovery_config.m_DiscoveryServers
                .push_back( serverLocator );
            return true;
        };

        const Bool bUseDs = configureDiscoveryServer();
        if ( !bUseDs ) {
            configureSimpleEdp();
        }

        // Configure transports
        if ( bUseTcpTransport || m_config.m_bUseTcpTransport ) {
            pqos.transport().use_builtin_transports = false;
            auto pTcpTransport = ::std::make_shared<
                eprosima::fastdds::rtps::TCPv4TransportDescriptor >();
            pTcpTransport->add_listener_port( 0 );
            pTcpTransport->sendBufferSize   = m_config.m_iUdpSendBufferSize;
            pTcpTransport->receiveBufferSize = m_config.m_iUdpRecvBufferSize;
            pqos.transport().user_transports.push_back( pTcpTransport );

            LAP_COM_LOG_DEBUG << "DDS: TCPv4 transport configured"
                              << " (send_buf=" << m_config.m_iUdpSendBufferSize
                              << ", recv_buf=" << m_config.m_iUdpRecvBufferSize << ")";
        }

        if ( m_config.m_bUseSharedMemory ) {
            // Enable builtin transports (includes UDPv4 + SHM)
            pqos.transport().use_builtin_transports = true;

            // Also add explicit SHM descriptor for tuning
            auto pShmTransport = ::std::make_shared<
                eprosima::fastdds::rtps::SharedMemTransportDescriptor >();
            pShmTransport->segment_size( m_config.m_iMaxPayloadSize * 2 );
            pqos.transport().user_transports.push_back( pShmTransport );

            LAP_COM_LOG_DEBUG << "DDS: Shared Memory transport configured"
                              << " (segment_size=" << m_config.m_iMaxPayloadSize * 2 << ")";
        }

        // Create participant with discovery listener.
        // Restrict to discovery-only events to prevent stealing on_data_available.
        const auto kDiscoveryMask = StatusMask::none();
        m_pParticipant = DomainParticipantFactory::get_instance()->create_participant(
            m_config.m_iDomainId, pqos, m_pDiscoveryListener, kDiscoveryMask );

        if ( m_pParticipant == nullptr && bUseDs ) {
            LAP_COM_LOG_WARN << "Discovery server connection failed, fallback to Simple EDP";
            configureSimpleEdp();
            m_pParticipant = DomainParticipantFactory::get_instance()->create_participant(
                m_config.m_iDomainId, pqos, m_pDiscoveryListener, kDiscoveryMask );
        }

        if ( m_pParticipant == nullptr ) {
            LAP_COM_LOG_ERROR << "Failed to create DDS participant";
            m_pDiscoveryListener = nullptr;
            return Result< void >::FromError( MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        // Keep ownership of the listener so it lives as long as participant
        m_pOwnedDiscoveryListener = ::std::move( pDiscListener );

        // Wire push-discovery callback so StartFindService subscriptions
        // get notified in real time
        m_pDiscoveryListener->SetDiscoveryChangeCallback(
            [this]( UInt64 svcId, Vector< UInt64 > instances ) {
                OnDiscoveryChange( svcId, ::std::move( instances ) );
            } );

        // Register type
        m_typeSupport.register_type( m_pParticipant );

        // Create default Publisher
        PublisherQos pubQos;
        m_pPublisher = m_pParticipant->create_publisher( pubQos );
        if ( m_pPublisher == nullptr ) {
            LAP_COM_LOG_ERROR << "Failed to create DDS publisher";
            DomainParticipantFactory::get_instance()->delete_participant( m_pParticipant );
            m_pParticipant = nullptr;
            m_pDiscoveryListener = nullptr;
            m_pOwnedDiscoveryListener.reset();
            return Result< void >::FromError( MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        // Create default Subscriber
        SubscriberQos subQos;
        m_pSubscriber = m_pParticipant->create_subscriber( subQos );
        if ( m_pSubscriber == nullptr ) {
            LAP_COM_LOG_ERROR << "Failed to create DDS subscriber";
            m_pParticipant->delete_publisher( m_pPublisher );
            DomainParticipantFactory::get_instance()->delete_participant( m_pParticipant );
            m_pParticipant = nullptr;
            m_pPublisher   = nullptr;
            m_pDiscoveryListener = nullptr;
            m_pOwnedDiscoveryListener.reset();
            return Result< void >::FromError( MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        // Initialize metrics
        m_metrics = {};

        LAP_COM_LOG_INFO << "DDS Binding initialized successfully";
        LAP_COM_LOG_INFO << "  Domain ID: " << m_config.m_iDomainId;
        LAP_COM_LOG_INFO << "  Type: " << m_typeSupport.get_type_name();
        LAP_COM_LOG_INFO << "  Shared Memory: "
                         << ( m_config.m_bUseSharedMemory ? "true" : "false" );
        LAP_COM_LOG_INFO << "  Data Sharing: "
                         << ( m_config.m_bDataSharingEnabled ? "true" : "false" );
        LAP_COM_LOG_INFO << "  Discovery Server: "
                         << ( m_config.m_strDiscoveryServer.empty()
                              ? "(none — standard PDP/EDP)"
                              : m_config.m_strDiscoveryServer );

        // ── Start Discovery Server Monitor ──
        if ( !m_config.m_strDiscoveryServer.empty() )
        {
            m_dsMonitorConfig.m_strServerAddress = m_config.m_strDiscoveryServer;

            m_pDiscoveryMonitor = MakeUnique< CDdsDiscoveryServerMonitor >(
                m_dsMonitorConfig );

            m_pDiscoveryMonitor->SetModeChangeCallback(
                [this]( DiscoveryMode oldMode, DiscoveryMode newMode ) {
                    OnDiscoveryModeChanged( oldMode, newMode );
                } );

            const auto initialMode = m_pDiscoveryMonitor->Start( m_pParticipant );

            LAP_COM_LOG_INFO << "  Discovery mode: "
                             << ( initialMode == DiscoveryMode::kDiscoveryServer
                                  ? "DISCOVERY_SERVER (SUPER_CLIENT)"
                                  : "SIMPLE_PDP (fallback)" );
        }

        return Result< void >::FromValue();
    }

    Result< void > DdsBinding::Shutdown() noexcept
    {
        LockGuard lock( m_mutex );

        if ( m_pParticipant == nullptr ) {
            return Result< void >::FromValue();
        }

        LAP_COM_LOG_INFO << "Shutting down DDS Binding";

        // Stop discovery monitor FIRST (it references the participant)
        if ( m_pDiscoveryMonitor )
        {
            m_pDiscoveryMonitor->Stop();
            m_pDiscoveryMonitor.reset();
        }

        // Clear push-discovery subscriptions
        m_mapFindSubscriptions.clear();

        // Detach discovery change callback before tearing down
        if ( m_pDiscoveryListener != nullptr ) {
            m_pDiscoveryListener->SetDiscoveryChangeCallback( nullptr );
        }

        // Shutdown managers in dependency order — each manager
        // closes its own CCdrChannel instances (readers, writers, topics)
        m_methodManager.Shutdown();
        m_eventManager.Shutdown();
        m_serviceManager.Shutdown();

        // Delete DDS entities
        if ( m_pSubscriber != nullptr ) {
            m_pParticipant->delete_subscriber( m_pSubscriber );
            m_pSubscriber = nullptr;
        }

        if ( m_pPublisher != nullptr ) {
            m_pParticipant->delete_publisher( m_pPublisher );
            m_pPublisher = nullptr;
        }

        if ( m_pParticipant != nullptr ) {
            DomainParticipantFactory::get_instance()->delete_participant( m_pParticipant );
            m_pParticipant = nullptr;
        }

        // Release discovery listener
        m_pDiscoveryListener = nullptr;
        m_pOwnedDiscoveryListener.reset();

        LAP_COM_LOG_INFO << "DDS Binding shutdown complete";
        return Result< void >::FromValue();
    }

    // ====================================================================
    // Service Management — delegates to CDdsServiceManager
    // ====================================================================

    Result< void > DdsBinding::OfferService(
        UInt64 serviceId, UInt64 instanceId ) noexcept
    {
        {
            LockGuard lock( m_mutex );
            if ( m_pParticipant == nullptr ) {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kNotInitialized ) );
            }
        }
        // m_mutex released: CreateWriter() may trigger on_data_writer_discovery
        // → OnDiscoveryChange() which needs m_mutex.  Holding it here deadlocks.
        return m_serviceManager.OfferService( serviceId, instanceId );
    }

    Result< void > DdsBinding::StopOfferService(
        UInt64 serviceId, UInt64 instanceId ) noexcept
    {
        {
            LockGuard lock( m_mutex );
            if ( m_pParticipant == nullptr ) {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kNotInitialized ) );
            }
        }
        // m_mutex released: same deadlock avoidance as OfferService.
        return m_serviceManager.StopOfferService( serviceId, instanceId );
    }

    Result< Vector< UInt64 > > DdsBinding::FindService(
        UInt64 serviceId ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( m_pParticipant == nullptr ) {
            return Result< Vector< UInt64 > >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }
        return m_serviceManager.FindService( serviceId );
    }

    Result< UInt64 > DdsBinding::StartFindService(
        UInt64 serviceId, ServiceDiscoveryCallback callback ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( m_pParticipant == nullptr ) {
            return Result< UInt64 >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }
        if ( !callback ) {
            return Result< UInt64 >::FromError(
                MakeErrorCode( ComErrc::kInvalidArgument ) );
        }

        // Generate a unique handle for this subscription
        const UInt64 handle = m_iNextFindHandle.fetch_add( 1U );

        // Store the subscription
        FindSubscription sub;
        sub.serviceId = serviceId;
        sub.callback  = callback;
        m_mapFindSubscriptions.emplace( handle, ::std::move( sub ) );

        // Fire immediately with current state (AUTOSAR SWS_CM_00001)
        Vector< UInt64 > currentInstances;
        if ( m_pDiscoveryListener != nullptr ) {
            currentInstances = m_pDiscoveryListener->GetDiscoveredInstances(
                serviceId, m_pParticipant );
        }

        LAP_COM_LOG_INFO << "StartFindService: handle=" << handle
                         << ", service=" << serviceId
                         << ", initial instances=" << currentInstances.size();

        // Invoke callback outside the discovery listener's lock
        // (m_mutex is held, which is fine — this is DdsBinding's own lock)
        callback( serviceId, ::std::move( currentInstances ) );

        return Result< UInt64 >::FromValue( handle );
    }

    Result< void > DdsBinding::StopFindService(
        UInt64 handle ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( m_pParticipant == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        auto it = m_mapFindSubscriptions.find( handle );
        if ( it == m_mapFindSubscriptions.end() ) {
            LAP_COM_LOG_WARN << "StopFindService: unknown handle=" << handle;
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kInvalidArgument ) );
        }

        LAP_COM_LOG_INFO << "StopFindService: handle=" << handle
                         << ", service=" << it->second.serviceId;
        m_mapFindSubscriptions.erase( it );
        return Result< void >::FromValue();
    }

    // ====================================================================
    // Event Communication — Do* NVI overrides (delegates to CDdsEventManager)
    // ====================================================================

    Result< void > DdsBinding::DoPrepareEventChannel(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 eventId ) noexcept
    {
        {
            LockGuard lock( m_mutex );
            if ( m_pParticipant == nullptr ) {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kNotInitialized ) );
            }
        }
        return m_eventManager.PrepareChannel( serviceId, instanceId, eventId );
    }

    Bool DdsBinding::HasEventAdapter(
        UInt64 serviceId, UInt32 eventId ) const noexcept
    {
        return CDdsTypeRegistry::Instance().FindAdapter(
            serviceId, eventId ) != nullptr;
    }

    Result< void > DdsBinding::DoSendEvent(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 eventId, const void* pData,
        Size dataSize ) noexcept
    {
        {
            LockGuard lock( m_mutex );
            if ( m_pParticipant == nullptr ) {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kNotInitialized ) );
            }
        }
        // m_mutex released: lazy writer creation may trigger discovery callbacks.
        return m_eventManager.SendEvent(
            serviceId, instanceId, eventId, pData, dataSize );
    }

    Result< void > DdsBinding::DoSubscribeEvent(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 eventId, EventCallback callback,
        Size dataSize ) noexcept
    {
        {
            LockGuard lock( m_mutex );
            if ( m_pParticipant == nullptr ) {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kNotInitialized ) );
            }
        }
        // m_mutex released: CreateReader may trigger discovery callbacks.
        return m_eventManager.SubscribeEvent(
            serviceId, instanceId, eventId, callback, dataSize );
    }

    Result< void > DdsBinding::UnsubscribeEvent(
        UInt64 serviceId, UInt64 instanceId, UInt32 eventId ) noexcept
    {
        {
            LockGuard lock( m_mutex );
            if ( m_pParticipant == nullptr ) {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kNotInitialized ) );
            }
        }
        // m_mutex released: entity destruction may trigger discovery callbacks.
        return m_eventManager.UnsubscribeEvent( serviceId, instanceId, eventId );
    }

    // ====================================================================
    // Method Communication — Do* NVI overrides (delegates to CDdsMethodManager)
    // (self-locking: do NOT hold facade mutex)
    // ====================================================================

    Result< void > DdsBinding::DoCallMethod(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 methodId, const void* pRequest,
        void* pResponse,
        Size requestSize, Size responseSize ) noexcept
    {
        if ( m_pParticipant == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }
        return m_methodManager.CallMethod(
            serviceId, instanceId, methodId, pRequest, pResponse,
            requestSize, responseSize );
    }

    Result< void > DdsBinding::DoRegisterMethod(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 methodId, MethodHandler handler,
        Size requestSize, Size responseSize ) noexcept
    {
        if ( m_pParticipant == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }
        return m_methodManager.RegisterMethod(
            serviceId, instanceId, methodId, ::std::move( handler ),
            requestSize, responseSize );
    }

    // ====================================================================
    // Field Communication — Do* NVI overrides (delegates to CDdsMethodManager)
    // ====================================================================

    Result< void > DdsBinding::DoGetField(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 fieldId, void* pOutValue,
        Size valueSize ) noexcept
    {
        static_cast< void >( valueSize );
        if ( m_pParticipant == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }
        return m_methodManager.GetField(
            serviceId, instanceId, fieldId, pOutValue );
    }

    Result< void > DdsBinding::DoSetField(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 fieldId, const void* pValue,
        Size valueSize ) noexcept
    {
        static_cast< void >( valueSize );
        if ( m_pParticipant == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }
        return m_methodManager.SetField(
            serviceId, instanceId, fieldId, pValue );
    }

    Result< void > DdsBinding::DoSubscribeFieldNotification(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 fieldId, FieldNotificationCallback callback,
        Size valueSize ) noexcept
    {
        if ( m_pParticipant == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }
        // Field notifications are published via DoSendEvent (SkeletonField → SkeletonEvent).
        // FieldNotificationCallback and EventCallback are the same underlying type:
        //   Function<void(UInt64, UInt64, UInt32, const void*)>.
        // Delegate to CDdsEventManager::SubscribeEvent — the same pub/sub infrastructure
        // used for broadcast events, distinguished only by elementId (0x200+).
        return m_eventManager.SubscribeEvent(
            serviceId, instanceId, fieldId,
            ::std::move( callback ),
            valueSize );
    }

    Result< void > DdsBinding::UnsubscribeFieldNotification(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 fieldId ) noexcept
    {
        if ( m_pParticipant == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }
        // Symmetric teardown: remove the event subscriber created in
        // DoSubscribeFieldNotification (delegates to the same event manager).
        return m_eventManager.UnsubscribeEvent( serviceId, instanceId, fieldId );
    }

    // ====================================================================
    // Capabilities and Configuration
    // ====================================================================

    Bool DdsBinding::SupportsService( UInt64 serviceId ) const noexcept
    {
        // DDS supports all services (cross-ECU capable)
        static_cast< void > ( serviceId );
        return true;
    }

    TransportMetrics DdsBinding::GetMetrics() const noexcept
    {
        LockGuard lock( m_mutex );
        return m_metrics;
    }

    void DdsBinding::SetDiscoveryServer( const String& address ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( m_pParticipant != nullptr ) {
            LAP_COM_LOG_WARN << "SetDiscoveryServer ignored after Initialize";
            return;
        }
        m_config.m_strDiscoveryServer = address;
    }

    DiscoveryMode DdsBinding::GetDiscoveryMode() const noexcept
    {
        if ( m_pDiscoveryMonitor ) {
            return m_pDiscoveryMonitor->GetCurrentMode();
        }
        // No monitor → we're using standard PDP/EDP (no DS configured)
        return ( m_pParticipant != nullptr )
            ? DiscoveryMode::kSimplePdp
            : DiscoveryMode::kDisconnected;
    }

    DiscoveryServerStats DdsBinding::GetDiscoveryStats() const noexcept
    {
        if ( m_pDiscoveryMonitor ) {
            return m_pDiscoveryMonitor->GetStats();
        }
        DiscoveryServerStats empty;
        empty.m_eCurrentMode = GetDiscoveryMode();
        return empty;
    }

    // ====================================================================
    // Discovery Server Fallback — mode change handler
    // ====================================================================

    void DdsBinding::OnDiscoveryModeChanged(
        DiscoveryMode oldMode, DiscoveryMode newMode ) noexcept
    {
        LAP_COM_LOG_INFO << "DdsBinding: Discovery mode changed "
                         << static_cast< int >( oldMode ) << " → "
                         << static_cast< int >( newMode );

        if ( newMode == DiscoveryMode::kSimplePdp &&
             oldMode == DiscoveryMode::kDiscoveryServer )
        {
            // DS went down → recreate participant in SIMPLE mode
            LAP_COM_LOG_WARN << "DdsBinding: Discovery Server lost, "
                             << "recreating participant with SIMPLE PDP/EDP";
            auto result = RecreateParticipant( DiscoveryMode::kSimplePdp );
            if ( !result.HasValue() ) {
                LAP_COM_LOG_ERROR << "DdsBinding: Failed to recreate participant ";
            }
        }
        else if ( newMode == DiscoveryMode::kDiscoveryServer &&
                  oldMode == DiscoveryMode::kSimplePdp )
        {
            // DS recovered → recreate participant in SUPER_CLIENT mode
            LAP_COM_LOG_INFO << "DdsBinding: Discovery Server recovered, "
                             << "recreating participant with SUPER_CLIENT";
            auto result = RecreateParticipant( DiscoveryMode::kDiscoveryServer );
            if ( !result.HasValue() ) {
                LAP_COM_LOG_ERROR << "DdsBinding: Failed to recreate participant "
                                  << "for DS mode, staying in PDP/EDP";
            }
        }
    }

    Result< void > DdsBinding::RecreateParticipant(
        DiscoveryMode targetMode ) noexcept
    {
        LockGuard lock( m_mutex );

        if ( m_pParticipant == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        LAP_COM_LOG_INFO << "DdsBinding::RecreateParticipant → mode "
                         << static_cast< int >( targetMode );

        // 1. Shutdown managers (they close their own DDS entities)
        m_methodManager.Shutdown();
        m_eventManager.Shutdown();
        m_serviceManager.Shutdown();

        // 2. Detach discovery listener callback
        if ( m_pDiscoveryListener != nullptr ) {
            m_pDiscoveryListener->SetDiscoveryChangeCallback( nullptr );
        }

        // 3. Delete old DDS entities
        if ( m_pSubscriber != nullptr ) {
            m_pParticipant->delete_subscriber( m_pSubscriber );
            m_pSubscriber = nullptr;
        }
        if ( m_pPublisher != nullptr ) {
            m_pParticipant->delete_publisher( m_pPublisher );
            m_pPublisher = nullptr;
        }
        DomainParticipantFactory::get_instance()->delete_participant( m_pParticipant );
        m_pParticipant = nullptr;
        m_pDiscoveryListener = nullptr;
        m_pOwnedDiscoveryListener.reset();

        // 4. Recreate discovery listener
        auto pDiscListener = MakeUnique< DdsDiscoveryListener >( this );
        m_pDiscoveryListener = pDiscListener.get();

        // 5. Configure participant QoS for target mode
        DomainParticipantQos pqos;
        pqos.name( "LightAP_DDS_Participant" );

        Bool bNeedTcp = false;

        if ( targetMode == DiscoveryMode::kDiscoveryServer &&
             !m_config.m_strDiscoveryServer.empty() )
        {
            // Parse DS address and configure SUPER_CLIENT
            String host;
            UInt32 port = 0;
            Int32 kind  = 0;
            if ( m_pDiscoveryMonitor &&
                 m_pDiscoveryMonitor->ParseServerAddress( host, port, kind ) )
            {
                eprosima::fastdds::rtps::Locator_t serverLocator;
                serverLocator.kind = kind;
                serverLocator.port = port;
                eprosima::fastdds::rtps::IPLocator::setIPv4( serverLocator, host );

                pqos.wire_protocol().builtin.discovery_config.discoveryProtocol =
                    eprosima::fastdds::rtps::DiscoveryProtocol::SUPER_CLIENT;
                pqos.wire_protocol().builtin.discovery_config
                    .use_SIMPLE_EndpointDiscoveryProtocol = true;
                pqos.wire_protocol().builtin.discovery_config
                    .use_STATIC_EndpointDiscoveryProtocol = false;
                pqos.wire_protocol().builtin.discovery_config.m_DiscoveryServers.clear();
                pqos.wire_protocol().builtin.discovery_config.m_DiscoveryServers
                    .push_back( serverLocator );

                bNeedTcp = ( kind == LOCATOR_KIND_TCPv4 );
            }
            else
            {
                // Fallback to SIMPLE if address parse fails
                pqos.wire_protocol().builtin.discovery_config.discoveryProtocol =
                    eprosima::fastdds::rtps::DiscoveryProtocol::SIMPLE;
                pqos.wire_protocol().builtin.discovery_config
                    .use_SIMPLE_EndpointDiscoveryProtocol = true;
                pqos.wire_protocol().builtin.discovery_config
                    .use_STATIC_EndpointDiscoveryProtocol = false;
            }
        }
        else
        {
            // SIMPLE PDP/EDP
            pqos.wire_protocol().builtin.discovery_config.discoveryProtocol =
                eprosima::fastdds::rtps::DiscoveryProtocol::SIMPLE;
            pqos.wire_protocol().builtin.discovery_config
                .use_SIMPLE_EndpointDiscoveryProtocol = true;
            pqos.wire_protocol().builtin.discovery_config
                .use_STATIC_EndpointDiscoveryProtocol = false;
            pqos.wire_protocol().builtin.discovery_config.m_DiscoveryServers.clear();
        }

        // 6. Configure transports
        if ( bNeedTcp || m_config.m_bUseTcpTransport ) {
            pqos.transport().use_builtin_transports = false;
            auto pTcp = ::std::make_shared<
                eprosima::fastdds::rtps::TCPv4TransportDescriptor >();
            pTcp->add_listener_port( 0 );
            pTcp->sendBufferSize   = m_config.m_iUdpSendBufferSize;
            pTcp->receiveBufferSize = m_config.m_iUdpRecvBufferSize;
            pqos.transport().user_transports.push_back( pTcp );
        }
        if ( m_config.m_bUseSharedMemory ) {
            pqos.transport().use_builtin_transports = true;
            auto pShm = ::std::make_shared<
                eprosima::fastdds::rtps::SharedMemTransportDescriptor >();
            pShm->segment_size( m_config.m_iMaxPayloadSize * 2 );
            pqos.transport().user_transports.push_back( pShm );
        }

        // 7. Create new participant
        const auto kDiscoveryMask = StatusMask::none();
        m_pParticipant = DomainParticipantFactory::get_instance()->create_participant(
            m_config.m_iDomainId, pqos, m_pDiscoveryListener, kDiscoveryMask );

        if ( m_pParticipant == nullptr ) {
            LAP_COM_LOG_ERROR << "RecreateParticipant: Failed to create participant";
            m_pDiscoveryListener = nullptr;
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        m_pOwnedDiscoveryListener = ::std::move( pDiscListener );
        m_pDiscoveryListener->SetDiscoveryChangeCallback(
            [this]( UInt64 svcId, Vector< UInt64 > instances ) {
                OnDiscoveryChange( svcId, ::std::move( instances ) );
            } );

        // 8. Register type and recreate publisher/subscriber
        m_typeSupport.register_type( m_pParticipant );

        PublisherQos pubQos;
        m_pPublisher = m_pParticipant->create_publisher( pubQos );
        if ( m_pPublisher == nullptr ) {
            LAP_COM_LOG_ERROR << "RecreateParticipant: Failed to create publisher";
            DomainParticipantFactory::get_instance()->delete_participant( m_pParticipant );
            m_pParticipant = nullptr;
            m_pDiscoveryListener = nullptr;
            m_pOwnedDiscoveryListener.reset();
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        SubscriberQos subQos;
        m_pSubscriber = m_pParticipant->create_subscriber( subQos );
        if ( m_pSubscriber == nullptr ) {
            LAP_COM_LOG_ERROR << "RecreateParticipant: Failed to create subscriber";
            m_pParticipant->delete_publisher( m_pPublisher );
            m_pPublisher = nullptr;
            DomainParticipantFactory::get_instance()->delete_participant( m_pParticipant );
            m_pParticipant = nullptr;
            m_pDiscoveryListener = nullptr;
            m_pOwnedDiscoveryListener.reset();
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        // 9. Update monitor with new participant
        if ( m_pDiscoveryMonitor ) {
            m_pDiscoveryMonitor->Stop();
            m_pDiscoveryMonitor->Start( m_pParticipant );
        }

        LAP_COM_LOG_INFO << "RecreateParticipant: Success ("
                         << ( targetMode == DiscoveryMode::kDiscoveryServer
                              ? "SUPER_CLIENT" : "SIMPLE" ) << ")";
        return Result< void >::FromValue();
    }

    // ====================================================================
    // Push-Discovery — internal notification handler
    // ====================================================================

    void DdsBinding::OnDiscoveryChange(
        UInt64 serviceId, Vector< UInt64 > instances ) noexcept
    {
        // Called from DdsDiscoveryListener's discovery thread.
        // We must NOT hold m_discoveryMutex here (the listener already does),
        // but we DO need to iterate over m_mapFindSubscriptions safely.
        //
        // Lock ordering:  m_discoveryMutex  →  m_mutex
        // (listener holds its lock, then we take m_mutex)
        LockGuard lock( m_mutex );

        for ( auto& [handle, sub] : m_mapFindSubscriptions ) {
            if ( sub.serviceId == serviceId ) {
                try {
                    sub.callback( serviceId, instances );
                } catch ( const ::std::exception& e ) {
                    LAP_COM_LOG_WARN << "FindService callback threw for handle="
                                     << handle << ": " << e.what();
                } catch ( ... ) {
                    LAP_COM_LOG_WARN << "FindService callback threw unknown "
                                        "exception for handle=" << handle;
                }
            }
        }
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
    return new lap::com::binding::DdsBinding();
}

void DestroyBindingInstance( lap::com::binding::ITransportBinding* instance )
{
    delete instance;
}

} // extern "C"
