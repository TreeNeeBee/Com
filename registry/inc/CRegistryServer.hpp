/**
 * @file        CRegistryServer.hpp
 * @author      LightAP Development Team
 * @brief       Registry initialization server - creates shared memfd and distributes via UDS
 * @date        2026/02/06
 * @details     Implements Phase 2 of SERVICE_DISCOVERY_ARCHITECTURE.md §2.2.2:
 *              - Creates single memfd for registry (kQM or kASIL)
 *              - Listens on Unix Domain Socket
 *              - Passes memfd FD to clients via SCM_RIGHTS
 *              - Intended for systemd socket activation
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 Compliance:
 *              - SWS_CM_00001: Service discovery infrastructure
 *              - SWS_CM_00110: Registry lifecycle management
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §2.2.2 (UDS FD Passing)
 *              unix(7), cmsg(3), systemd.socket(5)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/20  <td>1.0      <td>LightAP Team    <td>Initial implementation
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style optimization per code_rules.md
 * </table>
 */
#ifndef LAP_COM_CREGISTRY_SERVER_HPP
#define LAP_COM_CREGISTRY_SERVER_HPP

// ==================== Project-Internal Headers ====================
#include "ServiceSlot.hpp"
#include "CServiceRegistry.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/CResult.hpp>
#include <lap/core/CString.hpp>
#include <lap/core/CTypedef.hpp>

// ==================== Standard Library Headers ====================
#include <atomic>
#include <thread>
#include <sys/socket.h>
#include <sys/un.h>

namespace lap
{
namespace com
{
namespace registry
{
    using lap::core::Result;
    using lap::core::String;
    using lap::core::Bool;
    using lap::core::Int32;

    /**
     * @brief Registry initialization server
     *
     * @details Lifecycle:
     *          1. Create memfd (kQM or kASIL)
     *          2. Initialize registry slots
     *          3. Listen on Unix Domain Socket
     *          4. Accept client connections
     *          5. Send memfd FD via SCM_RIGHTS
     *          6. Keep running until shutdown
     *
     * @note Designed for systemd socket activation:
     *       - Socket passed via SD_LISTEN_FDS_START
     *       - Or manually bind to socket path
     * @note Thread-safe for Shutdown() (can be called from signal handler)
     */
    class CRegistryServer
    {
    public:
        /**
         * @brief Constructor
         * @param registryType Type of registry (kQM or kASIL)
         * @param socketPath Unix domain socket path (e.g., /run/lap/registry_qm.sock)
         */
        explicit CRegistryServer( RegistryType registryType,
                                      const String& socketPath );

        /**
         * @brief Destructor - cleanup resources
         */
        ~CRegistryServer();

        // Non-copyable, non-movable
        CRegistryServer( const CRegistryServer& ) = delete;
        CRegistryServer& operator=( const CRegistryServer& ) = delete;
        CRegistryServer( CRegistryServer&& ) = delete;
        CRegistryServer& operator=( CRegistryServer&& ) = delete;

    public:
        /**
         * @brief Initialize the registry server
         * @return Result< void > indicating success or error
         *
         * @details Steps:
         *          1. Create memfd
         *          2. Resize to 256KB
         *          3. mmap to process space
         *          4. Initialize slots to kIdle
         *          5. Seal memfd (F_SEAL_SHRINK|GROW|SEAL)
         * @note Not thread-safe
         */
        Result< void > Initialize() noexcept;

        /**
         * @brief Start listening for client connections
         * @param useSystemdSocket If true, use systemd-provided socket (SD_LISTEN_FDS_START)
         * @return Result< void > indicating success or error
         *
         * @details Blocks until shutdown
         * @note Not thread-safe (call from single thread only)
         */
        Result< void > Run( Bool useSystemdSocket = false ) noexcept;

        /**
         * @brief Shutdown the server (can be called from signal handler)
         * @note Thread-safe
         */
        void Shutdown() noexcept;

        /**
         * @brief Get memfd file descriptor (for testing)
         * @return memfd FD or -1 if not initialized
         * @note Thread-safe
         */
        [[nodiscard]] Int32 GetMemfd() const noexcept { return m_memfd; }

        /**
         * @brief Get mapped registry slots (for testing)
         * @return Pointer to slot array or nullptr if not initialized
         * @note Thread-safe
         */
        [[nodiscard]] ServiceSlot* GetSlots() const noexcept { return m_pSlots; }

    private:
        /**
         * @brief Create and initialize memfd
         * @return Result< void > indicating success or error
         */
        Result< void > createMemfd() noexcept;

        /**
         * @brief Create Unix domain socket
         * @param useSystemd If true, use SD_LISTEN_FDS_START
         * @return Result< void > indicating success or error
         */
        Result< void > createSocket( Bool useSystemd ) noexcept;

        /**
         * @brief Accept client connection and send memfd FD
         * @param clientFd Client socket file descriptor
         * @return Result< void > indicating success or error
         */
        Result< void > handleClient( Int32 clientFd ) noexcept;

        /**
         * @brief Send memfd FD to client via SCM_RIGHTS
         * @param clientFd Client socket file descriptor
         * @return Result< void > indicating success or error
         */
        Result< void > sendMemfdToClient( Int32 clientFd ) noexcept;

    private:
        // Configuration
        RegistryType        m_registryType;
        String              m_strSocketPath;

        // Resources
        Int32               m_memfd      { -1 };        ///< Anonymous memfd file descriptor
        Int32               m_socketFd   { -1 };        ///< Unix domain socket file descriptor
        ServiceSlot*        m_pSlots     { nullptr };   ///< Mapped registry slots

        // Runtime state
        std::atomic< Bool > m_bRunning   { false };     ///< Server running flag
        std::thread         m_acceptThread;              ///< Client acceptance thread
    };

} // namespace registry
} // namespace com
} // namespace lap

#endif // LAP_COM_CREGISTRY_SERVER_HPP
