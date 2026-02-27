/**
 * @file        CRegistryProxy.hpp
 * @author      LightAP Development Team
 * @brief       Registry client API (IPC-based v2.0)
 * @date        2026/02/06
 * @details     Lightweight client-side API for registry operations.
 *              Provides synchronous/asynchronous service registration and query.
 * @copyright   Copyright (c) 2026
 * @note        Design:
 *              - Shared memory: READ-ONLY (clients cannot write directly)
 *              - Modifications: Via IPC request/response (MPSC REQ + SPMC RESP)
 *              - FindService: Local read (< 500ns, zero IPC overhead)
 *              - OfferService: IPC request (< 50us, acceptable latency)
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §2.0 (IPC-based Registry v2.0)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/05  <td>1.0      <td>LightAP Team    <td>Initial implementation
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style optimization per code_rules.md
 * </table>
 */
#ifndef LAP_COM_CREGISTRY_PROXY_HPP
#define LAP_COM_CREGISTRY_PROXY_HPP

// ==================== Project-Internal Headers ====================
#include "CServiceRegistry.hpp"
#include "RegistryIpcMessage.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/ipc/Publisher.hpp>
#include <lap/core/ipc/Subscriber.hpp>
#include <lap/core/CResult.hpp>
#include <lap/core/COptional.hpp>
#include <lap/core/CString.hpp>
#include <lap/core/CTypedef.hpp>

// ==================== Standard Library Headers ====================
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <memory>
#include <optional>

namespace lap
{
namespace com
{
namespace registry
{
    using lap::core::Result;
    using lap::core::Optional;
    using lap::core::String;
    using lap::core::Bool;
    using lap::core::UInt32;
    using lap::core::UInt64;

    /**
     * @brief Registry client (IPC-based, v2.0)
     *
     * @details Client-side API for registry operations.
     *          Architecture changes (v2.0):
     *          - Shared memory: READ-ONLY (clients cannot write directly)
     *          - Modifications: Via IPC request/response (MPSC REQ + SPMC RESP)
     *          - CRegistryDispatcher: Central service managing all writes
     *          - Clients: Send requests via Core IPC Publisher
     *          - Responses: Received via Core IPC Subscriber (correlation by request_id)
     *
     * @note Benefits:
     *       - Atomic updates (single-writer model)
     *       - Consistent state (no race conditions)
     *       - Auditable operations (all modifications logged)
     *       - Permission enforcement (centralized access control)
     * @note Thread-safe for FindService(), IsInitialized(), GetStatistics()
     *
     * @reference SERVICE_DISCOVERY_ARCHITECTURE.md §2.4 (IPC-based Registry v2.0)
     */
    class CRegistryProxy final
    {
    public:
        /**
         * @brief Constructor
         */
        CRegistryProxy() noexcept;

        /**
         * @brief Destructor
         */
        ~CRegistryProxy() noexcept;

        // Disable copy and move
        CRegistryProxy( const CRegistryProxy& ) = delete;
        CRegistryProxy& operator=( const CRegistryProxy& ) = delete;
        CRegistryProxy( CRegistryProxy&& ) = delete;
        CRegistryProxy& operator=( CRegistryProxy&& ) = delete;

    public:
        /**
         * @brief Initialize registry client
         * @return Result< void > Success or error code
         *
         * @details Initialization sequence:
         *          1. Map QM and ASIL shared memory (READ-ONLY, PROT_READ)
         *          2. Create IPC Publisher on "/lap_registry_req" (MPSC, send requests)
         *          3. Create IPC Subscriber on "/lap_registry_resp" (SPMC, receive responses)
         *          4. Start response listener thread (match request_id)
         * @note Not thread-safe
         */
        Result< void > Initialize() noexcept;

        /**
         * @brief Register a service (async IPC request)
         * @param serviceId Service ID (determines registry selection)
         * @param instanceId Instance ID
         * @param majorVersion Major version
         * @param minorVersion Minor version
         * @param bindingType Binding type string
         * @param endpoint Endpoint address
         * @param timeoutMs Timeout in milliseconds (default 5000ms)
         * @return Result< UInt32 > Assigned slot index or error code
         *
         * @details Operation flow:
         *          1. Generate unique request_id
         *          2. Create RegistryRequest message
         *          3. Send request via IPC Publisher (MPSC channel)
         *          4. Wait for RegistryResponse (matched by request_id)
         *          5. Return slot index or error
         *
         * @note This is a blocking call with timeout
         * @note Routing logic:
         *       - 0x0001~0x0417 -> QM registry (QM/ASIL-A/B)
         *       - 0xF001~0xF3FE -> ASIL registry (ASIL-C/D)
         *       - 0xFFFF -> Both registries (broadcast)
         * @note Thread-safe (internally synchronized)
         */
        Result< UInt32 > RegisterService(
            UInt64 serviceId,
            UInt64 instanceId,
            UInt32 majorVersion,
            UInt32 minorVersion,
            const char* bindingType,
            const char* endpoint,
            UInt32 timeoutMs = 5000 ) noexcept;

        /**
         * @brief Unregister a service (async IPC request)
         * @param serviceId Service ID
         * @param timeoutMs Timeout in milliseconds (default 5000ms)
         * @return Result< void > Success or error code
         * @note Thread-safe (internally synchronized)
         */
        Result< void > UnregisterService( UInt64 serviceId, UInt32 timeoutMs = 5000 ) noexcept;

        /**
         * @brief Find a service by service ID (local read-only access)
         * @param serviceId Service ID to find
         * @return Optional< ServiceSlot > Service info if found
         *
         * @note Tries local shared memory first, falls back to IPC query
         * @note Thread-safe
         */
        Optional< ServiceSlot > FindService( UInt64 serviceId ) const noexcept;

        /**
         * @brief Query a service by ID via IPC (remote query to dispatcher)
         * @param serviceId Service ID to query
         * @param timeoutMs Timeout in milliseconds (default 5000ms)
         * @return Optional< ServiceSlot > Service info if found
         *
         * @note This goes through the IPC channel to the dispatcher
         * @note Thread-safe
         */
        Optional< ServiceSlot > QueryService( UInt64 serviceId, UInt32 timeoutMs = 5000 ) noexcept;

        /**
         * @brief Update heartbeat for a service (async IPC request, fire-and-forget)
         * @param serviceId Service ID
         * @param timestampNs Current timestamp (nanoseconds)
         * @return Result< void > Success or error code
         *
         * @note Fire-and-forget: Does not wait for response (best-effort delivery)
         * @note Thread-safe
         */
        Result< void > UpdateHeartbeat( UInt64 serviceId, UInt64 timestampNs ) noexcept;

        /**
         * @brief Check if client is initialized
         * @return true if initialized
         * @note Thread-safe
         */
        [[nodiscard]] Bool IsInitialized() const noexcept
        {
            return m_bRunning.load( std::memory_order_acquire );
        }

        /**
         * @brief Client statistics (for monitoring)
         */
        struct Statistics
        {
            UInt64 m_iTotalRequests;
            UInt64 m_iSuccessfulRequests;
            UInt64 m_iFailedRequests;
            UInt64 m_iTimeoutRequests;
        };

        /**
         * @brief Get client statistics
         * @return Statistics structure
         * @note Thread-safe
         */
        Statistics GetStatistics() const noexcept;

    private:
        /**
         * @brief Send IPC request and wait for response
         * @param request Registry request message
         * @param timeoutMs Timeout in milliseconds
         * @return Result< RegistryResponse > Response or error
         */
        Result< RegistryResponse > sendRequestAndWait(
            const RegistryRequest& request,
            UInt32 timeoutMs ) noexcept;

        /**
         * @brief Generate unique request ID
         * @return Unique request ID
         */
        UInt64 generateRequestId() noexcept
        {
            return m_iNextRequestId.fetch_add( 1, std::memory_order_relaxed );
        }

        /**
         * @brief Process incoming response messages (background thread)
         */
        void responseListenerLoop() noexcept;

    private:
        // Read-only registry access (shared memory mapped with PROT_READ)
        std::unique_ptr< CServiceRegistry >   m_pQmRegistry;    ///< QM registry (read-only)
        std::unique_ptr< CServiceRegistry >   m_pAsilRegistry;  ///< ASIL registry (read-only)

        // IPC channels for requests/responses
        std::optional< lap::core::ipc::Publisher >   m_requestPublisher;    ///< Send requests (MPSC)
        std::optional< lap::core::ipc::Subscriber >  m_responseSubscriber;  ///< Receive responses (SPMC)

        // Request ID generator
        std::atomic< UInt64 >               m_iNextRequestId;

        // Pending responses tracking
        std::mutex                          m_pendingResponsesMutex;
        std::unordered_map< UInt64, RegistryResponse > m_mapPendingResponses;
        std::condition_variable             m_responseCv;

        // Background response listener thread
        std::atomic< Bool >                 m_bRunning;
        std::thread                         m_responseListenerThread;

        // Statistics
        std::atomic< UInt64 >               m_iTotalRequests;
        std::atomic< UInt64 >               m_iSuccessfulRequests;
        std::atomic< UInt64 >               m_iFailedRequests;
        std::atomic< UInt64 >               m_iTimeoutRequests;
    };

} // namespace registry
} // namespace com
} // namespace lap

#endif // LAP_COM_CREGISTRY_PROXY_HPP
