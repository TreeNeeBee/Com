/**
 * @file        CCoreIPCMethodManager.hpp
 * @author      LightAP Development Team
 * @brief       Core IPC binding — Method and Field communication management
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Manages Method/Field RPC for the CoreIPC binding:
 *              - CallMethod  — client-side synchronous RPC with retry-send
 *              - RegisterMethod — server-side handler registration + worker thread
 *              - GetField / SetField — thin wrappers mapped to CallMethod
 *
 *              Owns:
 *              - Method-client map (one per service, created on first CallMethod)
 *              - Method-server map (one per service, created on first RegisterMethod)
 *              - Monotonic request-sequence counter for correlation
 *
 *              CallMethod and RegisterMethod have their own locking pattern:
 *              they briefly acquire the facade's mutex for map access, then
 *              release it for the (potentially blocking) I/O phase.  All other
 *              methods assume the facade holds the lock.
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Split from monolithic CoreIPCBinding
 * </table>
 */

#ifndef LAP_COM_CORE_IPC_CCOREIPCMETHODMGR_HPP
#define LAP_COM_CORE_IPC_CCOREIPCMETHODMGR_HPP

// ==================== Project-Internal Headers ====================
#include "CoreIPCTypes.hpp"
#include "ITransportBinding.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/CResult.hpp>
#include <lap/core/ipc/Publisher.hpp>
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

    // ====================================================================
    // detail::MethodClientWrapper / MethodServerWrapper
    // ====================================================================

    namespace detail
    {

        /**
         * @brief   Method client wrapper — dual pub/sub for request/response
         */
        struct MethodClientWrapper
        {
            UInt64                          m_iServiceId;
            UInt64                          m_iInstanceId;
            String                          m_strRequestPath;
            String                          m_strResponsePath;
            lap::core::ipc::Publisher       m_requestPublisher;
            lap::core::ipc::Subscriber      m_responseSubscriber;
            Mutex                           m_callMutex;

            MethodClientWrapper( UInt64 serviceId, UInt64 instanceId,
                                 const String& requestPath,
                                 const String& responsePath,
                                 lap::core::ipc::Publisher&& requestPub,
                                 lap::core::ipc::Subscriber&& responseSub ) noexcept
                : m_iServiceId( serviceId )
                , m_iInstanceId( instanceId )
                , m_strRequestPath( requestPath )
                , m_strResponsePath( responsePath )
                , m_requestPublisher( ::std::move( requestPub ) )
                , m_responseSubscriber( ::std::move( responseSub ) )
            {}
        };

        /**
         * @brief   Method server wrapper — receives requests, dispatches handlers
         */
        struct MethodServerWrapper
        {
            UInt64                          m_iServiceId;
            UInt64                          m_iInstanceId;
            String                          m_strRequestPath;
            String                          m_strResponsePath;
            lap::core::ipc::Subscriber      m_requestSubscriber;
            lap::core::ipc::Publisher       m_responsePublisher;
            Atomic< Bool >                  m_bRunning { false };
            ::std::thread                   m_workerThread;
            Mutex                           m_handlerMutex;
            Map< UInt32, MethodHandler >    m_mapHandlers;

            MethodServerWrapper( UInt64 serviceId, UInt64 instanceId,
                                 const String& requestPath,
                                 const String& responsePath,
                                 lap::core::ipc::Subscriber&& requestSub,
                                 lap::core::ipc::Publisher&& responsePub ) noexcept
                : m_iServiceId( serviceId )
                , m_iInstanceId( instanceId )
                , m_strRequestPath( requestPath )
                , m_strResponsePath( responsePath )
                , m_requestSubscriber( ::std::move( requestSub ) )
                , m_responsePublisher( ::std::move( responsePub ) )
            {}
        };

    } // namespace detail

    // ====================================================================
    // CCoreIPCMethodManager
    // ====================================================================

    /**
     * @brief   Method/Field RPC manager for the CoreIPC binding
     *
     * @details Owns method-client and method-server maps.
     *          - CallMethod/RegisterMethod briefly lock the facade's mutex for
     *            map access, then release it for the blocking I/O phase.
     *          - StopServer / Shutdown assume the facade lock is already held.
     *
     * @note    Special locking semantics — see per-method documentation
     */
    class CCoreIPCMethodManager
    {
    public:
        /**
         * @brief   Construct with references to shared resources
         * @param   config              Binding configuration (read-only)
         * @param   mapShmSegments      Shared SHM segment map (facade-owned)
         * @param   pServiceRegistry    Registry proxy (facade-owned)
         * @param   metrics             Transport metrics (facade-owned)
         * @param   facadeMutex         Facade's main mutex (for brief-lock pattern)
         */
        CCoreIPCMethodManager(
            const CoreIPCConfig& config,
            ShmSegmentMap& mapShmSegments,
            SharedHandle< registry::CRegistryProxy >& pServiceRegistry,
            TransportMetrics& metrics,
            Mutex& facadeMutex ) noexcept;

        ~CCoreIPCMethodManager() noexcept = default;

        // Rule of Five — non-copyable, non-movable
        CCoreIPCMethodManager( const CCoreIPCMethodManager& )             = delete;
        CCoreIPCMethodManager& operator=( const CCoreIPCMethodManager& )  = delete;
        CCoreIPCMethodManager( CCoreIPCMethodManager&& )                  = delete;
        CCoreIPCMethodManager& operator=( CCoreIPCMethodManager&& )       = delete;

    public:
        // ================================================================
        // Method Communication (self-locking — do NOT hold facade lock)
        // ================================================================

        /**
         * @brief   Synchronous method call with retry-send
         * @note    Self-locking — caller must NOT hold the facade mutex
         */
        Result< ByteBuffer > CallMethod( UInt64 serviceId, UInt64 instanceId,
                                          UInt32 methodId,
                                          const ByteBuffer& request ) noexcept;

        /**
         * @brief   Register a server-side method handler
         * @note    Self-locking — caller must NOT hold the facade mutex
         */
        Result< void > RegisterMethod( UInt64 serviceId, UInt64 instanceId,
                                        UInt32 methodId,
                                        MethodHandler handler ) noexcept;

    public:
        // ================================================================
        // Field Communication (delegates to CallMethod)
        // ================================================================

        Result< ByteBuffer > GetField( UInt64 serviceId, UInt64 instanceId,
                                        UInt32 fieldId ) noexcept;

        Result< void > SetField( UInt64 serviceId, UInt64 instanceId,
                                  UInt32 fieldId,
                                  const ByteBuffer& data ) noexcept;

    public:
        // ================================================================
        // Lifecycle (caller holds facade lock)
        // ================================================================

        /**
         * @brief   Stop method server for a specific service key
         * @note    Caller must hold the facade mutex
         */
        void StopServer( UInt64 serviceKey ) noexcept;

        /**
         * @brief   Stop all method servers and clear all clients
         * @note    Caller must hold the facade mutex
         */
        void Shutdown() noexcept;

    private:
        // ================================================================
        // Private — Method Server Thread
        // ================================================================

        void methodServerThread( detail::MethodServerWrapper* pWrapper ) noexcept;

    private:
        // ================================================================
        // Shared Resource References (facade-owned)
        // ================================================================

        const CoreIPCConfig&        m_config;
        ShmSegmentMap&              m_mapShmSegments;
        SharedHandle< registry::CRegistryProxy >& m_pServiceRegistry;
        TransportMetrics&           m_metrics;
        Mutex&                      m_facadeMutex;

        // ================================================================
        // Owned State
        // ================================================================

        /// Method clients: serviceKey -> MethodClientWrapper
        Map< UInt64, UniqueHandle< detail::MethodClientWrapper > > m_mapMethodClients;

        /// Method servers: serviceKey -> MethodServerWrapper
        Map< UInt64, UniqueHandle< detail::MethodServerWrapper > > m_mapMethodServers;

        /// Monotonic request sequence for method correlation
        Atomic< UInt64 >             m_iMethodRequestSeq { 1 };
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_CORE_IPC_CCOREIPCMETHODMGR_HPP
