/**
 * @file        CRegistryDispatcher.hpp
 * @author      LightAP Development Team
 * @brief       Core IPC-based registry service (MPSC REQ + SPMC RESP)
 * @date        2026/02/06
 * @details     Central registry service managing service slot modifications.
 *              Architecture:
 *              - Shared memory: Read-only for clients (mmap with PROT_READ)
 *              - Modifications: Only through IPC request/response
 *              - MPSC channel: Multiple clients -> Registry service (requests)
 *              - SPMC channel: Registry service -> Multiple clients (responses)
 * @copyright   Copyright (c) 2026
 * @note        Design advantages:
 *              - Atomic updates (single writer thread)
 *              - Consistent state (no race conditions)
 *              - Auditable operations (all requests logged)
 *              - Permission enforcement (centralized access control)
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §2.4 (IPC-based Registry)
 *              Core IPC_DESIGN_ARCHITECTURE.md §4.3 (MPSC/SPMC Patterns)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/05  <td>1.0      <td>LightAP Team    <td>Initial implementation
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style optimization per code_rules.md
 * </table>
 */
#ifndef LAP_COM_CREGISTRY_DISPATCHER_HPP
#define LAP_COM_CREGISTRY_DISPATCHER_HPP

// ==================== Project-Internal Headers ====================
#include "CServiceRegistry.hpp"
#include "RegistryIpcMessage.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/ipc/Publisher.hpp>
#include <lap/core/ipc/Subscriber.hpp>
#include <lap/core/IPCFactory.hpp>
#include <lap/core/CResult.hpp>
#include <lap/core/CString.hpp>
#include <lap/core/CTypedef.hpp>

// ==================== Standard Library Headers ====================
#include <atomic>
#include <thread>
#include <unordered_map>
#include <mutex>
#include <optional>

namespace lap
{
namespace com
{
namespace registry
{
    using lap::core::Result;
    using lap::core::String;
    using lap::core::Bool;
    using lap::core::UInt32;
    using lap::core::UInt64;
    using lap::core::UniqueHandle;
    using lap::core::ipc::Publisher;
    using lap::core::ipc::Subscriber;
    using lap::core::ipc::PublisherConfig;
    using lap::core::ipc::SubscriberConfig;
    using lap::core::ipc::IPCFactory;
    using lap::core::ipc::SharedMemoryManager;
    using lap::core::ipc::SharedMemoryConfig;
    using lap::core::ipc::IPCType;

    /**
     * @brief Central registry service managing all slot modifications
     *
     * @details Architecture:
     *          1. Registry service owns both QM and ASIL registries
     *          2. Clients connect to IPC channels:
     *             - Publisher  -> "/lap_registry_req" (MPSC, send requests)
     *             - Subscriber -> "/lap_registry_resp" (SPMC, receive responses)
     *          3. Registry service processes requests sequentially (single thread)
     *          4. Registry service broadcasts responses to all subscribers
     *          5. Clients match responses by request_id (correlation)
     *
     * @note Thread model:
     *       - Main thread: Runs IPC event loop (Subscriber::Receive)
     *       - Single writer: All slot modifications serialized
     *       - No locks needed: Single-threaded processing ensures consistency
     * @note Thread-safe for Shutdown() only
     */
    class CRegistryDispatcher final
    {
    public:
        /**
         * @brief Constructor
         */
        CRegistryDispatcher() noexcept;

        /**
         * @brief Destructor
         */
        ~CRegistryDispatcher() noexcept;

        // Disable copy and move
        CRegistryDispatcher( const CRegistryDispatcher& ) = delete;
        CRegistryDispatcher& operator=( const CRegistryDispatcher& ) = delete;
        CRegistryDispatcher( CRegistryDispatcher&& ) = delete;
        CRegistryDispatcher& operator=( CRegistryDispatcher&& ) = delete;

    public:
        /**
         * @brief Initialize registry service
         * @return Result< void > Success or error code
         *
         * @details Initialization sequence:
         *          1. Initialize QM and ASIL registries (shared memory)
         *          2. Create Core IPC Subscriber on "/lap_registry_req" (MPSC)
         *          3. Create Core IPC Publisher on "/lap_registry_resp" (SPMC)
         *          4. Start background request processing thread
         * @note Not thread-safe
         */
        Result< void > Initialize() noexcept;

        /**
         * @brief Shutdown registry service
         * @note Thread-safe (stops background thread and cleans up IPC channels)
         */
        void Shutdown() noexcept;

        /**
         * @brief Run registry service event loop (blocking)
         * @return Result< void > Success or error code
         *
         * @details Main event loop:
         *          1. Subscriber::Receive() blocks waiting for requests
         *          2. Process each request (register/unregister/heartbeat)
         *          3. Modify registry slots (exclusive write access)
         *          4. Broadcast response via Publisher::Send()
         *          5. Repeat until Shutdown() is called
         * @note Not thread-safe (call from single thread only)
         */
        Result< void > Run() noexcept;

        /**
         * @brief Get QM registry (read-only access for clients)
         * @return Const reference to QM registry
         * @note Thread-safe
         */
        [[nodiscard]] const CServiceRegistry& GetQMRegistry() const noexcept
        {
            return m_qmRegistry;
        }

        /**
         * @brief Get ASIL registry (read-only access for clients)
         * @return Const reference to ASIL registry
         * @note Thread-safe
         */
        [[nodiscard]] const CServiceRegistry& GetASILRegistry() const noexcept
        {
            return m_asilRegistry;
        }

    private:
        /**
         * @brief Process a single registry request
         * @param request Registry request message
         * @return RegistryResponse Response message
         */
        RegistryResponse processRequest( const RegistryRequest& request ) noexcept;

        /**
         * @brief Handle kRegisterService request
         * @param request Request message
         * @return RegistryResponse Response message
         */
        RegistryResponse handleRegisterService( const RegistryRequest& request ) noexcept;

        /**
         * @brief Handle kUnregisterService request
         * @param request Request message
         * @return RegistryResponse Response message
         */
        RegistryResponse handleUnregisterService( const RegistryRequest& request ) noexcept;

        /**
         * @brief Handle kUpdateHeartbeat request
         * @param request Request message
         * @return RegistryResponse Response message
         */
        RegistryResponse handleUpdateHeartbeat( const RegistryRequest& request ) noexcept;

        /**
         * @brief Handle kQueryService request
         * @param request Request message
         * @return RegistryResponse Response message with service info
         */
        RegistryResponse handleQueryService( const RegistryRequest& request ) noexcept;

        /**
         * @brief Calculate slot index from service ID
         * @param serviceId Service ID
         * @return Slot index (1~1023, or 0 if invalid)
         */
        static UInt32 CalculateSlot( UInt64 serviceId ) noexcept
        {
            UInt32 slot = static_cast< UInt32 > ( serviceId & 1023 );
            return ( slot == 0 ) ? 0 : slot;  // Slot 0 is reserved
        }

        /**
         * @brief Select registry based on service ID
         * @param serviceId Service ID
         * @return RegistryType (kQM, kASIL, or kBoth)
         */
        static RegistryType SelectRegistry( UInt64 serviceId ) noexcept;

    private:
        // Registry storage (single-writer access)
        CServiceRegistry              m_qmRegistry;        ///< QM registry (QM + ASIL-A/B)
        CServiceRegistry              m_asilRegistry;      ///< ASIL registry (ASIL-C/D)

        // Core IPC shared memory segments (must outlive Publisher/Subscriber)
        UniqueHandle< SharedMemoryManager > m_pRequestShm;   ///< SHM for MPSC request channel
        UniqueHandle< SharedMemoryManager > m_pResponseShm;  ///< SHM for SPMC response channel

        // Core IPC channels
        std::optional< Subscriber > m_requestSubscriber;   ///< MPSC: Receive requests
        std::optional< Publisher >  m_responsePublisher;   ///< SPMC: Broadcast responses

        // Service control
        std::atomic< Bool >         m_bRunning;          ///< Running flag (true = active)
        std::thread                 m_eventLoopThread;   ///< Background event loop thread

        // Statistics (for monitoring)
        std::atomic< UInt64 >       m_iTotalRequests;    ///< Total requests processed
        std::atomic< UInt64 >       m_iSuccessfulOps;    ///< Successful operations
        std::atomic< UInt64 >       m_iFailedOps;        ///< Failed operations
    };

} // namespace registry
} // namespace com
} // namespace lap

#endif // LAP_COM_CREGISTRY_DISPATCHER_HPP
