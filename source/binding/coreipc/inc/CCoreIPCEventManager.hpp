/**
 * @file        CCoreIPCEventManager.hpp
 * @author      LightAP Development Team
 * @brief       Core IPC binding — Event communication management
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Manages event communication for the CoreIPC binding:
 *              - SendEvent / SubscribeEvent / UnsubscribeEvent
 *              - Owns the subscriber map (one subscriber per event subscription)
 *              - Runs listener threads that poll for incoming event data
 *
 *              Uses publishers from CCoreIPCServiceManager for SendEvent.
 *
 *              NOT thread-safe — the facade (CoreIPCBinding) serialises access.
 *              Listener threads are the exception: they run independently and
 *              access only their own wrapper + read-only config + racy metrics.
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Split from monolithic CoreIPCBinding
 * </table>
 */

#ifndef LAP_COM_CORE_IPC_CCOREIPCEVENTMGR_HPP
#define LAP_COM_CORE_IPC_CCOREIPCEVENTMGR_HPP

// ==================== Project-Internal Headers ====================
#include "CoreIPCTypes.hpp"
#include "ITransportBinding.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/CResult.hpp>
#include <lap/core/ipc/Subscriber.hpp>

// ==================== Standard Library Headers ====================
#include <functional>
#include <thread>

// ==================== Forward Declarations ====================
namespace lap { namespace com { namespace registry { class CRegistryProxy; } } }

namespace lap
{
namespace com
{
namespace binding
{

    using lap::core::Result;

    // Forward declarations
    class CCoreIPCServiceManager;

    // ====================================================================
    // detail::SubscriberWrapper
    // ====================================================================

    namespace detail
    {

        /**
         * @brief   Subscriber wrapper — holds subscriber + listener thread
         */
        struct SubscriberWrapper
        {
            UInt64                          m_iServiceId;
            UInt64                          m_iInstanceId;
            UInt32                          m_iEventId;
            EventCallback                   m_callback;
            String                          m_strShmPath;
            Atomic< Bool >                  m_bRunning { false };
            ::std::thread                   m_listenerThread;
            lap::core::ipc::Subscriber      m_subscriber;

            SubscriberWrapper( UInt64 serviceId, UInt64 instanceId, UInt32 eventId,
                               EventCallback callback, const String& shmPath,
                               lap::core::ipc::Subscriber&& subscriber ) noexcept
                : m_iServiceId( serviceId )
                , m_iInstanceId( instanceId )
                , m_iEventId( eventId )
                , m_callback( ::std::move( callback ) )
                , m_strShmPath( shmPath )
                , m_subscriber( ::std::move( subscriber ) )
            {}
        };

    } // namespace detail

    // ====================================================================
    // CCoreIPCEventManager
    // ====================================================================

    /**
     * @brief   Event communication manager for the CoreIPC binding
     *
     * @details Owns the subscriber map. SubscribeEvent creates a Subscriber,
     *          connects it, and launches a listener thread. SendEvent uses a
     *          publisher from the service manager to send encoded events.
     *
     * @note    NOT thread-safe — caller (facade) must serialise access.
     *          Listener threads are internally spawned and run independently.
     */
    class CCoreIPCEventManager
    {
    public:
        /**
         * @brief   Construct with references to shared resources
         * @param   config              Binding configuration (read-only)
         * @param   pServiceRegistry    Registry proxy (facade-owned)
         * @param   metrics             Transport metrics (facade-owned)
         * @param   serviceManager      Service manager (for publisher access)
         */
        CCoreIPCEventManager(
            const CoreIPCConfig& config,
            SharedHandle< registry::CRegistryProxy >& pServiceRegistry,
            TransportMetrics& metrics,
            CCoreIPCServiceManager& serviceManager ) noexcept;

        ~CCoreIPCEventManager() noexcept = default;

        // Rule of Five — non-copyable, non-movable
        CCoreIPCEventManager( const CCoreIPCEventManager& )             = delete;
        CCoreIPCEventManager& operator=( const CCoreIPCEventManager& )  = delete;
        CCoreIPCEventManager( CCoreIPCEventManager&& )                  = delete;
        CCoreIPCEventManager& operator=( CCoreIPCEventManager&& )       = delete;

    public:
        // ================================================================
        // Event Communication
        // ================================================================

        Result< void > SendEvent( UInt64 serviceId, UInt64 instanceId,
                                   UInt32 eventId,
                                   const ByteBuffer& data ) noexcept;

        Result< void > SubscribeEvent( UInt64 serviceId, UInt64 instanceId,
                                        UInt32 eventId,
                                        EventCallback callback ) noexcept;

        Result< void > UnsubscribeEvent( UInt64 serviceId, UInt64 instanceId,
                                          UInt32 eventId ) noexcept;

    public:
        // ================================================================
        // Lifecycle
        // ================================================================

        /**
         * @brief   Stop all listener threads and clear subscribers
         * @note    Called by facade during Shutdown — caller holds the lock
         */
        void Shutdown() noexcept;

    private:
        // ================================================================
        // Private — Listener Thread
        // ================================================================

        void listenerThread( detail::SubscriberWrapper* pWrapper ) noexcept;

    private:
        // ================================================================
        // Shared Resource References (facade-owned)
        // ================================================================

        const CoreIPCConfig&        m_config;
        SharedHandle< registry::CRegistryProxy >& m_pServiceRegistry;
        TransportMetrics&           m_metrics;
        CCoreIPCServiceManager&     m_serviceManager;

        // ================================================================
        // Owned State
        // ================================================================

        /// Subscribers: eventKey -> SubscriberWrapper
        Map< UInt64, UniqueHandle< detail::SubscriberWrapper > > m_mapSubscribers;
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_CORE_IPC_CCOREIPCEVENTMGR_HPP
