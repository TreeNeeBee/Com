/**
 * @file        Runtime.cpp
 * @author      Aii
 * @brief       AUTOSAR Adaptive Platform Communication Management Runtime (PIMPL)
 * @date        2026/02/07
 * @details     Runtime implementation using PIMPL idiom.
 *              All registry operations are encapsulated as Runtime member methods.
 *              BindingManager integration for transport-level service management.
 *              HeartbeatWorker maintains service liveness in registry.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §2.4 (Runtime lifecycle)
 *              ARCHITECTURE_SUMMARY.md §7.2 (BindingManager integration)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/10/30  <td>1.0      <td>ddkv587         <td>Initial implementation
 * <tr><td>2025/11/20  <td>2.0      <td>LightAP Team    <td>Registry integration
 * <tr><td>2026/02/07  <td>3.0      <td>Aii             <td>PIMPL, member methods, no free functions
 * <tr><td>2026/02/07  <td>4.0      <td>Aii             <td>BindingManager + HeartbeatWorker integration
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "Runtime.hpp"
#include "CRegistryProxy.hpp"
#include "BindingManager.hpp"
#include "ServiceDiscovery.hpp"

// ==================== Standard Library Headers ====================
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

namespace lap
{
namespace com
{
    // ====================================================================
    // Static member definitions
    // ====================================================================
    Mutex               Runtime::s_mutex;
    Atomic< Bool >      Runtime::s_initialized{ false };

    // ====================================================================
    // File-local helper utilities
    // ====================================================================
    namespace
    {
        /**
         * @brief Default binding configuration path
         */
        static constexpr const char* kDefaultBindingConfigPath =
            "/etc/lap/com/bindings.yaml";

        /**
         * @brief Heartbeat interval (100ms per ARCHITECTURE_SUMMARY.md)
         */
        static constexpr auto kHeartbeatInterval =
            std::chrono::milliseconds( 100 );

        /**
         * @brief Map numeric binding type to string identifier
         * @param bindingValue Binding type (0–4)
         * @return Binding name string for CRegistryProxy
         */
        const char* BindingToString( lap::core::UInt8 bindingValue ) noexcept
        {
            switch ( bindingValue )
            {
                case 0:  return "coreipc";
                case 1:  return "dds";
                case 2:  return "socket";
                case 3:  return "dbus";
                case 4:  return "someip";
                default: return "unknown";
            }
        }

        /**
         * @brief Validate service ID range
         * @param serviceId Service identifier
         * @return true if within valid range (0x0001–0xFFFE)
         * @note Accepts full 16-bit range for DDS and all binding types.
         *       Range 0x0001–0x3FFF: QM services
         *       Range 0x4000–0xEFFF: DDS / application-defined services
         *       Range 0xF000–0xFFFE: ASIL services
         */
        Bool IsValidServiceId( lap::core::UInt16 serviceId ) noexcept
        {
            return ( serviceId >= 0x0001 && serviceId <= 0xFFFE );
        }

        /**
         * @brief Validate service ID (UInt64 overload for binding layer)
         */
        Bool IsValidServiceId64( lap::core::UInt64 serviceId ) noexcept
        {
            if ( serviceId > 0xFFFF ) { return false; }
            return IsValidServiceId( static_cast< lap::core::UInt16 > ( serviceId ) );
        }

        /**
         * @brief Get current timestamp in nanoseconds (for heartbeat)
         */
        lap::core::UInt64 GetTimestampNs() noexcept
        {
            auto now = std::chrono::steady_clock::now();
            auto ns  = std::chrono::duration_cast< std::chrono::nanoseconds > (
                now.time_since_epoch() );
            return static_cast< lap::core::UInt64 > ( ns.count() );
        }
    } // anonymous namespace

    // ====================================================================
    // Internal types for find-service callback management
    // ====================================================================
    namespace
    {
        /**
         * @brief Active find-service subscription record
         */
        using FindCallback = Function< void(
            lap::core::UInt64,
            lap::core::Vector< lap::core::UInt64 >,
            FindServiceHandle ) >;

        struct FindServiceRecord
        {
            lap::core::UInt64                handleId;       ///< Local handle ID
            lap::core::UInt16                serviceId;      ///< AUTOSAR service ID
            FindCallback                     callback;       ///< Type-erased callback
            lap::core::UInt64                bindingHandle;  ///< Handle from binding layer
            lap::core::String                bindingName;    ///< Which binding manages this
        };
    } // anonymous namespace

    // ====================================================================
    // Runtime::Impl — PIMPL implementation details
    // ====================================================================

    /**
     * @brief Hidden implementation of Runtime
     *
     * @details Contains all state:
     *          - CRegistryProxy (dual-registry client, IPC-based v2.0)
     *          - BindingManager reference (singleton, transport layer)
     *          - Heartbeat thread (periodic keep-alive for registered services)
     *          - Offered services set (for heartbeat tracking)
     *          - Active find subscriptions (for StartFindService/StopFindService)
     */
    struct Runtime::Impl final
    {
        // ============================================================
        // Construction / Destruction
        // ============================================================

        Impl() noexcept
            : m_pRegistry( nullptr )
            , m_bHeartbeatRunning( false )
            , m_iNextFindHandle( 1 )
        {
        }

        ~Impl() noexcept
        {
            Shutdown();
        }

        // Non-copyable, non-movable
        Impl( const Impl& )            = delete;
        Impl& operator=( const Impl& ) = delete;
        Impl( Impl&& )                 = delete;
        Impl& operator=( Impl&& )      = delete;

        // ============================================================
        // Initialization
        // ============================================================

        /**
         * @brief Initialize all subsystems
         * @param configPath Path to binding YAML config
         * @return Result<void>
         *
         * @details Initialization order (critical for correctness):
         *          1. CRegistryProxy (shared memory + IPC channels)
         *          2. BindingManager (load YAML → dlopen bindings)
         *          3. HeartbeatWorker (starts periodic keep-alive)
         */
        Result< void > Initialize( const lap::core::String& configPath ) noexcept
        {
            // Step 1: Registry (non-fatal for DDS-only deployments)
            m_pRegistry = MakeUnique< registry::CRegistryProxy > ();
            auto regResult = m_pRegistry->Initialize();
            if ( !regResult.HasValue() )
            {
                // Registry unavailable — proceed without it.
                // DDS binding has its own discovery; registry is only
                // needed for CoreIPC shared-memory service lookup.
                m_pRegistry.reset();
            }

            // Step 2: BindingManager
            auto& bindingMgr = binding::BindingManager::GetInstance();
            auto bindResult = bindingMgr.LoadConfiguration( configPath );
            // Non-fatal: binding layer may not be configured in minimal deployments
            static_cast< void > ( bindResult );

            // Step 3: ServiceDiscoveryManager (three-tier)
            {
                discovery::ServiceDiscoveryManager::Config discoveryConfig;
                // Use default static config path; discovery server config is optional
                auto discResult = discovery::ServiceDiscoveryManager::Create(
                    discoveryConfig );
                if ( discResult.HasValue() )
                {
                    m_pDiscovery = std::move( discResult ).Value();
                }
                // Non-fatal: discovery is supplementary to binding-level discovery
            }

            // Step 4: Heartbeat
            StartHeartbeat();
            return Result< void >::FromValue();
        }

        // ============================================================
        // Shutdown
        // ============================================================

        /**
         * @brief Shutdown all subsystems (reverse of Initialize)
         * @note Order: Heartbeat → FindService subscriptions → BindingManager → Registry
         */
        void Shutdown() noexcept
        {
            StopHeartbeat();

            // Cancel all active find subscriptions
            {
                ScopedLock< Mutex > lock( m_findMutex );
                for ( auto& pair : m_mapFindRecords )
                {
                    auto& record = pair.second;
                    auto& bindingMgr = binding::BindingManager::GetInstance();
                    auto bindingOpt = bindingMgr.GetBinding( record.bindingName );
                    if ( bindingOpt.has_value() )
                    {
                        bindingOpt.value()->StopFindService( record.bindingHandle );
                    }
                }
                m_mapFindRecords.clear();
            }

            // Unregister all offered services
            {
                ScopedLock< Mutex > lock( m_offerMutex );
                for ( auto serviceId : m_vecOfferedServices )
                {
                    auto& bindingMgr = binding::BindingManager::GetInstance();
                    auto* binding = bindingMgr.SelectBinding( serviceId );
                    if ( binding != nullptr )
                    {
                        binding->StopOfferService( serviceId, 0 );
                    }
                    if ( m_pRegistry != nullptr )
                    {
                        m_pRegistry->UnregisterService( serviceId, 1000 );
                    }
                }
                m_vecOfferedServices.clear();
            }

            // Shutdown three-tier ServiceDiscoveryManager
            m_pDiscovery.reset();

            // Shutdown binding manager (deferred — singleton lifecycle)
            // binding::BindingManager::GetInstance().Shutdown();

            m_pRegistry.reset();
        }

        // ============================================================
        // Heartbeat Management
        // ============================================================

        void StartHeartbeat() noexcept
        {
            m_bHeartbeatRunning.store( true, std::memory_order_release );
            m_heartbeatThread = std::thread( &Impl::HeartbeatWorker, this );
        }

        void StopHeartbeat() noexcept
        {
            m_bHeartbeatRunning.store( false, std::memory_order_release );
            if ( m_heartbeatThread.joinable() )
            {
                m_heartbeatThread.join();
            }
        }

        /**
         * @brief Heartbeat worker thread
         *
         * @details Periodically iterates all offered services and sends
         *          UpdateHeartbeat to CRegistryProxy. This prevents
         *          the dispatcher from garbage-collecting stale registrations.
         *
         * @note Interval: 100ms (configurable via kHeartbeatInterval)
         * @note Performance: O(N) where N = number of offered services
         */
        void HeartbeatWorker() noexcept
        {
            while ( m_bHeartbeatRunning.load( std::memory_order_acquire ) )
            {
                // Snapshot offered services under lock (minimize hold time)
                lap::core::Vector< lap::core::UInt64 > snapshot;
                {
                    ScopedLock< Mutex > lock( m_offerMutex );
                    snapshot = m_vecOfferedServices;
                }

                // Send heartbeats outside lock
                auto timestampNs = GetTimestampNs();
                for ( auto serviceId : snapshot )
                {
                    if ( m_pRegistry != nullptr )
                    {
                        m_pRegistry->UpdateHeartbeat( serviceId, timestampNs );
                    }
                }

                std::this_thread::sleep_for( kHeartbeatInterval );
            }
        }

        // ============================================================
        // Service Discovery (binding-level)
        // ============================================================

        /**
         * @brief Query all active bindings for service instances
         * @param serviceId AUTOSAR service ID
         * @return Vector of discovered instance IDs
         */
        lap::core::Vector< lap::core::UInt64 > FindServiceViaBindings(
            lap::core::UInt64 serviceId ) noexcept
        {
            lap::core::Vector< lap::core::UInt64 > results;

            auto& bindingMgr = binding::BindingManager::GetInstance();
            auto bindingNames = bindingMgr.GetLoadedBindings();

            for ( const auto& name : bindingNames )
            {
                auto bindingOpt = bindingMgr.GetBinding( name );
                if ( !bindingOpt.has_value() )
                {
                    continue;
                }

                auto* binding = bindingOpt.value();
                auto findResult = binding->FindService( serviceId );
                if ( findResult.HasValue() )
                {
                    for ( auto instanceId : findResult.Value() )
                    {
                        results.push_back( instanceId );
                    }
                }
            }

            return results;
        }

        /**
         * @brief Start push-based find via binding layer
         * @param serviceId AUTOSAR service ID
         * @param instanceSpec Instance specifier (for routing)
         * @param callback Type-erased callback
         * @return Result containing FindServiceHandle
         */
        Result< FindServiceHandle > StartFindServiceImpl(
            lap::core::UInt16 serviceId,
            lap::core::InstanceSpecifier instanceSpec,
            Runtime::InternalFindCallback callback ) noexcept
        {
            // Parse optional instance filter from InstanceSpecifier
            // (same logic as Runtime::FindService template)
            lap::core::Optional< InstanceIdentifierType > instanceFilter;
            {
                auto specStr = instanceSpec.ToString();
                auto pos = specStr.rfind( '/' );
                auto lastSeg = ( pos != lap::core::StringView::npos )
                                   ? specStr.substr( pos + 1 )
                                   : specStr;
                if ( !lastSeg.empty() )
                {
                    Bool allDigits = true;
                    for ( auto ch : lastSeg )
                    {
                        if ( ch < '0' || ch > '9' ) { allDigits = false; break; }
                    }
                    if ( allDigits )
                    {
                        String numStr( lastSeg.data(), lastSeg.size() );
                        instanceFilter = static_cast< InstanceIdentifierType > (
                            std::strtoul( numStr.c_str(), nullptr, 10 ) );
                    }
                }
            }

            auto& bindingMgr = binding::BindingManager::GetInstance();
            auto* binding = bindingMgr.SelectBinding(
                static_cast< lap::core::UInt64 > ( serviceId ) );

            if ( binding == nullptr )
            {
                return Result< FindServiceHandle >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure, 0 ) );
            }

            // Allocate local handle
            auto localHandleId = m_iNextFindHandle.fetch_add(
                1, std::memory_order_relaxed );
            FindServiceHandle fsh( localHandleId );

            // Create binding-level callback adapter (applies instance filter)
            auto adaptedCallback = [this, localHandleId, callback, serviceId, instanceFilter](
                lap::core::UInt64 svcId,
                lap::core::Vector< lap::core::UInt64 > instances )
            {
                // Apply instance filter if present
                if ( instanceFilter.has_value() )
                {
                    lap::core::Vector< lap::core::UInt64 > filtered;
                    for ( auto inst : instances )
                    {
                        if ( static_cast< InstanceIdentifierType > ( inst )
                             == instanceFilter.value() )
                        {
                            filtered.push_back( inst );
                        }
                    }
                    instances = std::move( filtered );
                }

                FindServiceHandle fsh( localHandleId );
                callback( svcId, std::move( instances ), fsh );
            };

            // Register with binding
            auto bindResult = binding->StartFindService(
                static_cast< lap::core::UInt64 > ( serviceId ),
                std::move( adaptedCallback ) );

            if ( !bindResult.HasValue() )
            {
                return Result< FindServiceHandle >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure, 0 ) );
            }

            // Track the subscription
            {
                FindServiceRecord record;
                record.handleId      = localHandleId;
                record.serviceId     = serviceId;
                record.callback      = callback;
                record.bindingHandle = bindResult.Value();
                record.bindingName   = binding->GetName();

                ScopedLock< Mutex > lock( m_findMutex );
                m_mapFindRecords.emplace( localHandleId, std::move( record ) );
            }

            return Result< FindServiceHandle >::FromValue( fsh );
        }

        /**
         * @brief Stop a push-based find subscription
         * @param handle FindServiceHandle from StartFindService
         */
        void StopFindServiceImpl( FindServiceHandle handle ) noexcept
        {
            FindServiceRecord record;
            {
                ScopedLock< Mutex > lock( m_findMutex );
                auto it = m_mapFindRecords.find( handle.GetInternalId() );
                if ( it == m_mapFindRecords.end() )
                {
                    return;
                }
                record = std::move( it->second );
                m_mapFindRecords.erase( it );
            }

            // Cancel at binding level
            auto& bindingMgr = binding::BindingManager::GetInstance();
            auto bindingOpt = bindingMgr.GetBinding( record.bindingName );
            if ( bindingOpt.has_value() )
            {
                bindingOpt.value()->StopFindService( record.bindingHandle );
            }
        }

        // ============================================================
        // Service Offering (binding-level)
        // ============================================================

        /**
         * @brief Offer service: Registry + Binding
         * @param serviceId AUTOSAR service ID
         * @param instanceSpec Instance specifier
         * @return Result<void>
         *
         * @details Flow:
         *          1. Select best binding via BindingManager::SelectBinding
         *          2. Register in shared-memory registry (for fast local lookup)
         *          3. Offer via binding (network-level advertisement)
         *          4. Track in offered services list (for heartbeat)
         */
        Result< void > OfferServiceImpl(
            lap::core::UInt64 serviceId,
            lap::core::InstanceSpecifier instanceSpec ) noexcept
        {
            static_cast< void > ( instanceSpec );

            if ( !IsValidServiceId64( serviceId ) )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kInvalidArgument, 0 ) );
            }

            auto& bindingMgr = binding::BindingManager::GetInstance();
            auto* binding = bindingMgr.SelectBinding( serviceId );

            // Determine binding name for registry
            const char* bindingName = "coreipc";
            if ( binding != nullptr )
            {
                bindingName = binding->GetName();
            }

            // Step 1: Register in shared-memory registry (non-fatal)
            if ( m_pRegistry != nullptr )
            {
                auto instanceId = serviceId & 0xFFFF; // Default: service_id == instance_id
                auto regResult = m_pRegistry->RegisterService(
                    serviceId,
                    instanceId,
                    1, 0,           // version 1.0
                    bindingName,
                    "",             // endpoint auto-assigned
                    5000 );

                // Non-fatal: DDS binding has its own service discovery;
                // registry is supplementary for CoreIPC lookups.
                static_cast< void > ( regResult );
            }

            // Step 2: Offer via binding (network advertisement)
            if ( binding != nullptr )
            {
                auto instanceId = serviceId & 0xFFFF;
                auto offerResult = binding->OfferService( serviceId, instanceId );
                if ( !offerResult.HasValue() )
                {
                    // Rollback registry registration
                    if ( m_pRegistry != nullptr )
                    {
                        m_pRegistry->UnregisterService( serviceId, 1000 );
                    }
                    return Result< void >::FromError(
                        MakeErrorCode( ComErrc::kServiceNotOffered, 0 ) );
                }
            }

            // Step 3: Register in ServiceDiscoveryManager (three-tier)
            if ( m_pDiscovery != nullptr )
            {
                discovery::ServiceInstanceInfo info;
                info.m_iInstanceId = static_cast< InstanceIdentifierType > (
                    serviceId & 0xFFFF );
                info.m_strServiceInterfaceName = std::to_string( serviceId );
                info.m_strBindingType = ( binding != nullptr )
                    ? binding->GetName() : "coreipc";
                info.m_iTtlSeconds = 60;

                // Non-fatal: supplementary to binding-level offering
                static_cast< void > ( m_pDiscovery->RegisterService( info ) );
            }

            // Step 4: Track for heartbeat
            {
                ScopedLock< Mutex > lock( m_offerMutex );
                m_vecOfferedServices.push_back( serviceId );
            }

            return Result< void >::FromValue();
        }

        /**
         * @brief Stop offering service: Binding + Registry
         * @param serviceId AUTOSAR service ID
         * @param instanceSpec Instance specifier
         *
         * @details Flow (reverse of OfferService):
         *          1. Remove from heartbeat tracking
         *          2. StopOffer via binding (network-level)
         *          3. Unregister from shared-memory registry
         */
        void StopOfferServiceImpl(
            lap::core::UInt64 serviceId,
            lap::core::InstanceSpecifier instanceSpec ) noexcept
        {
            static_cast< void > ( instanceSpec );

            // Step 1: Remove from heartbeat tracking
            {
                ScopedLock< Mutex > lock( m_offerMutex );
                auto it = std::find(
                    m_vecOfferedServices.begin(),
                    m_vecOfferedServices.end(),
                    serviceId );
                if ( it != m_vecOfferedServices.end() )
                {
                    m_vecOfferedServices.erase( it );
                }
            }

            // Step 2: StopOffer via binding
            auto& bindingMgr = binding::BindingManager::GetInstance();
            auto* binding = bindingMgr.SelectBinding( serviceId );
            if ( binding != nullptr )
            {
                auto instanceId = serviceId & 0xFFFF;
                binding->StopOfferService( serviceId, instanceId );
            }

            // Step 3: Unregister from ServiceDiscoveryManager
            if ( m_pDiscovery != nullptr )
            {
                auto instanceId = static_cast< InstanceIdentifierType > (
                    serviceId & 0xFFFF );
                static_cast< void > ( m_pDiscovery->UnregisterService( instanceId ) );
            }

            // Step 4: Unregister from registry
            if ( m_pRegistry != nullptr )
            {
                m_pRegistry->UnregisterService( serviceId, 5000 );
            }
        }

        // ============================================================
        // Member Variables
        // ============================================================

        /// Registry client (shared memory + IPC)
        UniqueHandle< registry::CRegistryProxy >     m_pRegistry;

        /// Heartbeat thread
        std::thread                                  m_heartbeatThread;
        Atomic< Bool >                               m_bHeartbeatRunning;

        /// Offered services (protected by m_offerMutex)
        Mutex                                        m_offerMutex;
        lap::core::Vector< lap::core::UInt64 >       m_vecOfferedServices;

        /// Active find-service subscriptions (protected by m_findMutex)
        using FindRecordMap = std::unordered_map< lap::core::UInt64, FindServiceRecord >;
        Mutex                                        m_findMutex;
        FindRecordMap                                m_mapFindRecords;
        Atomic< lap::core::UInt64 >                  m_iNextFindHandle;

        /// Three-tier service discovery manager (Static Config → DDS Server → Dynamic)
        lap::core::UniqueHandle< discovery::ServiceDiscoveryManager > m_pDiscovery;
    };

    // ====================================================================
    // Runtime — Constructor / Destructor
    // ====================================================================

    Runtime::Runtime() noexcept
        : m_pImpl( nullptr )
    {
    }

    Runtime::~Runtime() noexcept
    {
        m_pImpl.reset();
    }

    // ====================================================================
    // Runtime — Lifecycle (SWS_CM_00122)
    // ====================================================================

    Result< void > Runtime::Initialize() noexcept
    {
        return Initialize( kDefaultBindingConfigPath );
    }

    Result< void > Runtime::Initialize( const lap::core::String& configPath ) noexcept
    {
        ScopedLock< Mutex > lock( s_mutex );

        if ( s_initialized.load( std::memory_order_acquire ) )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kInvalidState, 0 ) );
        }

        auto& instance = GetInstance();
        instance.m_pImpl = MakeUnique< Impl > ();

        auto result = instance.m_pImpl->Initialize( configPath );
        if ( !result.HasValue() )
        {
            instance.m_pImpl.reset();
            return result;
        }

        s_initialized.store( true, std::memory_order_release );
        return Result< void >::FromValue();
    }

    Result< void > Runtime::Deinitialize() noexcept
    {
        ScopedLock< Mutex > lock( s_mutex );

        if ( !s_initialized.load( std::memory_order_acquire ) )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kInvalidState, 0 ) );
        }

        auto& instance = GetInstance();
        instance.m_pImpl.reset();

        s_initialized.store( false, std::memory_order_release );
        return Result< void >::FromValue();
    }

    Bool Runtime::IsInitialized() noexcept
    {
        return s_initialized.load( std::memory_order_acquire );
    }

    Runtime& Runtime::GetInstance() noexcept
    {
        static Runtime instance;
        return instance;
    }

    // ====================================================================
    // Runtime — Service Discovery (non-template implementations)
    // ====================================================================

    void Runtime::StopFindService( FindServiceHandle handle ) noexcept
    {
        if ( !IsInitialized() )
        {
            return;
        }

        auto& instance = GetInstance();
        if ( instance.m_pImpl )
        {
            instance.m_pImpl->StopFindServiceImpl( handle );
        }
    }

    lap::core::Vector< lap::core::UInt64 > Runtime::findServiceViaBindings(
        lap::core::UInt64 serviceId ) noexcept
    {
        if ( m_pImpl )
        {
            return m_pImpl->FindServiceViaBindings( serviceId );
        }
        return {};
    }

    Result< FindServiceHandle > Runtime::startFindServiceImpl(
        lap::core::UInt16 serviceId,
        lap::core::InstanceSpecifier instanceSpec,
        InternalFindCallback callback ) noexcept
    {
        if ( m_pImpl )
        {
            return m_pImpl->StartFindServiceImpl(
                serviceId, std::move( instanceSpec ), std::move( callback ) );
        }
        return Result< FindServiceHandle >::FromError(
            MakeErrorCode( ComErrc::kNotInitialized, 0 ) );
    }

    Result< void > Runtime::offerServiceImpl(
        lap::core::UInt64 serviceId,
        lap::core::InstanceSpecifier instanceSpec ) noexcept
    {
        if ( m_pImpl )
        {
            return m_pImpl->OfferServiceImpl(
                serviceId, std::move( instanceSpec ) );
        }
        return Result< void >::FromError(
            MakeErrorCode( ComErrc::kNotInitialized, 0 ) );
    }

    void Runtime::stopOfferServiceImpl(
        lap::core::UInt64 serviceId,
        lap::core::InstanceSpecifier instanceSpec ) noexcept
    {
        if ( m_pImpl )
        {
            m_pImpl->StopOfferServiceImpl(
                serviceId, std::move( instanceSpec ) );
        }
    }

    // ====================================================================
    // Runtime — BindingManager access
    // ====================================================================

    binding::BindingManager& Runtime::GetBindingManager() noexcept
    {
        return binding::BindingManager::GetInstance();
    }

    // ====================================================================
    // Runtime — Registry Integration (member methods)
    // ====================================================================

    registry::CRegistryProxy* Runtime::GetRegistry() noexcept
    {
        if ( m_pImpl )
        {
            return m_pImpl->m_pRegistry.get();
        }
        return nullptr;
    }

    Result< void > Runtime::RegisterService(
        lap::core::UInt16 serviceId,
        lap::core::UInt16 instanceId,
        lap::core::UInt8 networkBinding ) noexcept
    {
        if ( !IsInitialized() )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized, 0 ) );
        }

        if ( !IsValidServiceId( serviceId ) )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kInvalidArgument, 0 ) );
        }

        auto* registry = GetInstance().GetRegistry();
        if ( registry == nullptr )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kCommunicationFailure, 0 ) );
        }

        const char* bindingStr = BindingToString( networkBinding );

        auto regResult = registry->RegisterService(
            static_cast< lap::core::UInt64 > ( serviceId ),
            static_cast< lap::core::UInt64 > ( instanceId ),
            1,              // majorVersion (default)
            0,              // minorVersion (default)
            bindingStr,     // binding type string
            "",             // endpoint (auto-assigned by dispatcher)
            5000            // timeout 5s
        );

        if ( !regResult.HasValue() )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kServiceNotOffered, 0 ) );
        }

        return Result< void >::FromValue();
    }

    lap::core::Optional< registry::ServiceSlot > Runtime::FindServiceById(
        lap::core::UInt16 serviceId ) noexcept
    {
        if ( !IsInitialized() )
        {
            return lap::core::Optional< registry::ServiceSlot > ();
        }

        if ( !IsValidServiceId( serviceId ) )
        {
            return lap::core::Optional< registry::ServiceSlot > ();
        }

        auto* registry = GetInstance().GetRegistry();
        if ( registry == nullptr )
        {
            return lap::core::Optional< registry::ServiceSlot > ();
        }

        return registry->FindService(
            static_cast< lap::core::UInt64 > ( serviceId ) );
    }

    Result< void > Runtime::UnregisterService( lap::core::UInt16 serviceId ) noexcept
    {
        if ( !IsInitialized() )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized, 0 ) );
        }

        if ( !IsValidServiceId( serviceId ) )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kInvalidArgument, 0 ) );
        }

        auto* registry = GetInstance().GetRegistry();
        if ( registry == nullptr )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kCommunicationFailure, 0 ) );
        }

        return registry->UnregisterService(
            static_cast< lap::core::UInt64 > ( serviceId ), 5000 );
    }

    // ====================================================================
    // Runtime — ResolveInstanceIDs [SWS_CM_00118]
    // ====================================================================

    Result< InstanceIdentifierContainer > Runtime::ResolveInstanceIDs(
        lap::core::InstanceSpecifier metaModelIdentifier ) noexcept
    {
        if ( !IsInitialized() )
        {
            return Result< InstanceIdentifierContainer >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized, 0 ) );
        }

        // Resolve InstanceSpecifier against service instance manifest.
        // Current strategy: return the specifier as a single InstanceIdentifier.
        // Future: Parse service instance manifest ARXML for multi-instance mapping.
        InstanceIdentifierContainer result;
        result.emplace_back( std::move( metaModelIdentifier ) );
        return Result< InstanceIdentifierContainer >::FromValue( std::move( result ) );
    }

} // namespace com
} // namespace lap
