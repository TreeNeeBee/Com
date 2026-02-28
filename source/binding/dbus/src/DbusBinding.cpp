/**
 * @file        DbusBinding.cpp
 * @author      LightAP Development Team
 * @brief       D-Bus binding — sd-bus based implementation
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     D-Bus transport binding using systemd sd-bus API.
 *              Events are mapped to D-Bus signals, methods to D-Bus calls.
 *              Service offer = request well-known name on bus.
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Stub facade
 * <tr><td>2026/02/28  <td>2.0      <td>Aii             <td>sd-bus implementation
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "DbusBinding.hpp"
#include "ComTypes.hpp"

// ==================== System Headers ====================
#include <systemd/sd-bus.h>
#include <cstring>
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

    DbusBinding::DbusBinding() noexcept
        : m_bInitialized( false )
        , m_pBus( nullptr )
        , m_bRunning( false )
    {
        LAP_COM_LOG_INFO << "DbusBinding instance created";
    }

    DbusBinding::~DbusBinding() noexcept
    {
        static_cast< void >( Shutdown() );
        LAP_COM_LOG_INFO << "DbusBinding instance destroyed";
    }

    // ====================================================================
    // Lifecycle Management
    // ====================================================================

    Result< void > DbusBinding::Initialize() noexcept
    {
        LockGuard lock( m_mutex );

        if ( m_bInitialized )
        {
            return Result< void >::FromValue();
        }

        // Connect to the bus
        int ret = 0;
        if ( m_config.m_bUseSystemBus )
        {
            ret = sd_bus_open_system( &m_pBus );
        }
        else
        {
            ret = sd_bus_open_user( &m_pBus );
        }

        if ( ret < 0 )
        {
            LAP_COM_LOG_WARN << "DbusBinding: sd-bus open failed (ret="
                             << ret << "), falling back to direct bus";
            // Try with direct connection if address provided
            if ( !m_config.m_strBusAddress.empty() )
            {
                ret = sd_bus_open_system_with_description(
                    &m_pBus, m_config.m_strBusAddress.c_str() );
            }

            if ( ret < 0 )
            {
                LAP_COM_LOG_WARN << "DbusBinding: all bus open attempts failed, "
                                 << "running in offline mode";
                m_pBus = nullptr;
                // Still initialize — allow local service registry
            }
        }

        // Start bus event-processing thread
        if ( m_pBus )
        {
            m_bRunning.store( true );
            m_busThread = ::std::thread( [this]() { busProcessThread(); } );
        }

        m_metrics = {};
        m_bInitialized = true;
        LAP_COM_LOG_INFO << "DbusBinding initialized (sd-bus, bus="
                         << ( m_pBus ? "connected" : "offline" ) << ")";
        return Result< void >::FromValue();
    }

    Result< void > DbusBinding::Shutdown() noexcept
    {
        LockGuard lock( m_mutex );

        if ( !m_bInitialized )
        {
            return Result< void >::FromValue();
        }

        // Stop bus thread
        m_bRunning.store( false );
        if ( m_busThread.joinable() )
        {
            m_mutex.unlock();
            m_busThread.join();
            m_mutex.lock();
        }

        // Release offered names
        if ( m_pBus )
        {
            for ( const auto& svc : m_offeredServices )
            {
                sd_bus_release_name( m_pBus, svc.first.c_str() );
            }
        }

        // Close bus
        if ( m_pBus )
        {
            sd_bus_unref( m_pBus );
            m_pBus = nullptr;
        }

        // Clear registries
        m_offeredServices.clear();
        m_eventSubscriptions.clear();
        m_methodHandlers.clear();
        m_fieldNotifications.clear();

        m_bInitialized = false;
        LAP_COM_LOG_INFO << "DbusBinding shutdown";
        return Result< void >::FromValue();
    }

    // ====================================================================
    // Service Management
    // ====================================================================

    Result< void > DbusBinding::OfferService(
        UInt64 serviceId, UInt64 instanceId ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        auto dbusName = makeDbusName( serviceId, instanceId );
        m_offeredServices[ dbusName ] = instanceId;

        // Request well-known name on bus
        if ( m_pBus )
        {
            int ret = sd_bus_request_name( m_pBus, dbusName.c_str(), 0 );
            if ( ret < 0 )
            {
                LAP_COM_LOG_WARN << "DbusBinding: sd_bus_request_name failed for "
                                 << dbusName << " (ret=" << ret << ")";
                // Continue anyway — local registry still works
            }
        }

        LAP_COM_LOG_INFO << "DbusBinding: offering service " << dbusName;
        return Result< void >::FromValue();
    }

    Result< void > DbusBinding::StopOfferService(
        UInt64 serviceId, UInt64 instanceId ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        auto dbusName = makeDbusName( serviceId, instanceId );

        if ( m_pBus )
        {
            sd_bus_release_name( m_pBus, dbusName.c_str() );
        }

        m_offeredServices.erase( dbusName );
        LAP_COM_LOG_INFO << "DbusBinding: stopped offering " << dbusName;
        return Result< void >::FromValue();
    }

    Result< Vector< UInt64 > > DbusBinding::FindService(
        UInt64 serviceId ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized )
        {
            return Result< Vector< UInt64 > >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        Vector< UInt64 > instances;
        String prefix = m_config.m_strServicePrefix + ".S"
                        + ::std::to_string( serviceId );

        for ( const auto& entry : m_offeredServices )
        {
            if ( entry.first.find( prefix ) == 0 )
            {
                instances.push_back( entry.second );
            }
        }

        return Result< Vector< UInt64 > >::FromValue( ::std::move( instances ) );
    }

    Result< UInt64 > DbusBinding::StartFindService(
        UInt64 serviceId, ServiceDiscoveryCallback callback ) noexcept
    {
        auto findResult = FindService( serviceId );
        if ( findResult.HasValue() && callback )
        {
            callback( serviceId, findResult.Value() );
        }
        return Result< UInt64 >::FromValue( serviceId );
    }

    Result< void > DbusBinding::StopFindService(
        UInt64 handle ) noexcept
    {
        static_cast< void >( handle );
        return Result< void >::FromValue();
    }

    // ====================================================================
    // Event Communication
    // ====================================================================

    Result< void > DbusBinding::DoSendEvent(
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

        if ( m_pBus )
        {
            auto objPath = makeDbusPath( serviceId, instanceId );
            String sigName = "Event" + ::std::to_string( eventId );

            // Emit signal with raw byte array payload
            int ret = sd_bus_emit_signal(
                m_pBus,
                objPath.c_str(),
                m_config.m_strInterfaceName.c_str(),
                sigName.c_str(),
                "ay",
                static_cast< int >( dataSize ),
                pData );

            if ( ret < 0 )
            {
                LAP_COM_LOG_WARN << "DbusBinding: emit_signal failed (ret=" << ret << ")";
            }
        }

        // Also deliver to local subscribers
        ::std::ostringstream oss;
        oss << serviceId << "_" << instanceId << "_e" << eventId;
        auto it = m_eventSubscriptions.find( oss.str() );
        if ( it != m_eventSubscriptions.end() && it->second )
        {
            it->second( serviceId, instanceId, eventId, pData );
        }

        ++m_metrics.messagesSent;
        m_metrics.bytesSent += dataSize;
        return Result< void >::FromValue();
    }

    Result< void > DbusBinding::DoSubscribeEvent(
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
        LAP_COM_LOG_DEBUG << "DbusBinding: subscribed to event " << eventId;
        return Result< void >::FromValue();
    }

    Result< void > DbusBinding::UnsubscribeEvent(
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

    Result< void > DbusBinding::DoCallMethod(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 methodId, const void* pRequest, void* pResponse,
        Size requestSize, Size responseSize ) noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized )
        {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        // Try local handler first
        ::std::ostringstream oss;
        oss << serviceId << "_" << instanceId << "_m" << methodId;
        auto it = m_methodHandlers.find( oss.str() );
        if ( it != m_methodHandlers.end() && it->second )
        {
            it->second( serviceId, instanceId, methodId, pRequest, pResponse );
            ++m_metrics.messagesSent;
            ++m_metrics.messagesReceived;
            return Result< void >::FromValue();
        }

        // Fall back to remote D-Bus call
        if ( m_pBus )
        {
            auto dbusName = makeDbusName( serviceId, instanceId );
            auto objPath  = makeDbusPath( serviceId, instanceId );
            String methName = "Method" + ::std::to_string( methodId );

            sd_bus_error error = SD_BUS_ERROR_NULL;
            sd_bus_message* reply = nullptr;

            int ret = sd_bus_call_method(
                m_pBus,
                dbusName.c_str(),
                objPath.c_str(),
                m_config.m_strInterfaceName.c_str(),
                methName.c_str(),
                &error,
                &reply,
                "ay",
                static_cast< int >( requestSize ),
                pRequest );

            if ( ret < 0 )
            {
                sd_bus_error_free( &error );
                if ( reply ) { sd_bus_message_unref( reply ); }
                ++m_metrics.failedConnections;
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }

            // Read response
            if ( reply && pResponse && responseSize > 0 )
            {
                const void* respData = nullptr;
                size_t respLen = 0;
                ret = sd_bus_message_read_array( reply, 'y', &respData, &respLen );
                if ( ret >= 0 && respData )
                {
                    Size copyLen = ( respLen < responseSize ) ? respLen : responseSize;
                    ::std::memcpy( pResponse, respData, copyLen );
                }
            }

            if ( reply ) { sd_bus_message_unref( reply ); }
            sd_bus_error_free( &error );

            ++m_metrics.messagesSent;
            ++m_metrics.messagesReceived;
            return Result< void >::FromValue();
        }

        return Result< void >::FromError(
            MakeErrorCode( ComErrc::kCommunicationFailure ) );
    }

    Result< void > DbusBinding::DoRegisterMethod(
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
        LAP_COM_LOG_DEBUG << "DbusBinding: registered method " << methodId;
        return Result< void >::FromValue();
    }

    // ====================================================================
    // Field Communication
    // ====================================================================

    Result< void > DbusBinding::DoGetField(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 fieldId, void* pOutValue,
        Size valueSize ) noexcept
    {
        // Map to method call: methodId = fieldId | 0x8000
        UInt32 getMethodId = fieldId | 0x8000U;
        return DoCallMethod( serviceId, instanceId,
                             getMethodId, nullptr, pOutValue,
                             0, valueSize );
    }

    Result< void > DbusBinding::DoSetField(
        UInt64 serviceId, UInt64 instanceId,
        UInt32 fieldId, const void* pValue,
        Size valueSize ) noexcept
    {
        UInt32 setMethodId = fieldId | 0x8001U;
        Byte dummyResp{};
        return DoCallMethod( serviceId, instanceId,
                             setMethodId, pValue, &dummyResp,
                             valueSize, 0 );
    }

    Result< void > DbusBinding::DoSubscribeFieldNotification(
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

    Result< void > DbusBinding::UnsubscribeFieldNotification(
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

    Bool DbusBinding::SupportsService( UInt64 serviceId ) const noexcept
    {
        LockGuard lock( m_mutex );
        if ( !m_bInitialized ) { return false; }

        String prefix = m_config.m_strServicePrefix + ".S"
                        + ::std::to_string( serviceId );
        for ( const auto& entry : m_offeredServices )
        {
            if ( entry.first.find( prefix ) == 0 ) { return true; }
        }
        return m_bInitialized;  // Default: accept if initialized
    }

    TransportMetrics DbusBinding::GetMetrics() const noexcept
    {
        LockGuard lock( m_mutex );
        return m_metrics;
    }

    // ====================================================================
    // Internal Helpers
    // ====================================================================

    String DbusBinding::makeDbusName(
        UInt64 serviceId, UInt64 instanceId ) const noexcept
    {
        ::std::ostringstream oss;
        oss << m_config.m_strServicePrefix
            << ".S" << serviceId << ".I" << instanceId;
        return oss.str();
    }

    String DbusBinding::makeDbusPath(
        UInt64 serviceId, UInt64 instanceId ) const noexcept
    {
        ::std::ostringstream oss;
        oss << m_config.m_strObjectPath
            << "/S" << serviceId << "/I" << instanceId;
        return oss.str();
    }

    void DbusBinding::busProcessThread() noexcept
    {
        while ( m_bRunning.load() )
        {
            if ( m_pBus )
            {
                int ret = sd_bus_process( m_pBus, nullptr );
                if ( ret > 0 ) { continue; }  // More to process

                // Wait for bus activity (up to 100ms)
                ret = sd_bus_wait( m_pBus, 100000 );  // 100ms in µs
                static_cast< void >( ret );
            }
            else
            {
                // No bus — sleep to avoid busy-spin
                ::std::this_thread::sleep_for(
                    ::std::chrono::milliseconds( 100 ) );
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
    return new lap::com::binding::DbusBinding();
}

void DestroyBindingInstance( lap::com::binding::ITransportBinding* instance )
{
    delete instance;
}

} // extern "C"
