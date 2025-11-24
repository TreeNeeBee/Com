/**
 * @file        RegistryInitializer.hpp
 * @author      LightAP Development Team
 * @brief       Registry initialization server - creates shared memfd and distributes via UDS
 * @date        2025-11-20
 * @details     Implements Phase 2 of SERVICE_DISCOVERY_ARCHITECTURE.md §2.2.2:
 *              - Creates single memfd for registry (QM or ASIL)
 *              - Listens on Unix Domain Socket
 *              - Passes memfd FD to clients via SCM_RIGHTS
 *              - Intended for systemd socket activation
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R24-11 Compliance:
 *              - SWS_CM_00001: Service discovery infrastructure
 *              - SWS_CM_00110: Registry lifecycle management
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §2.2.2 (UDS FD Passing)
 *              unix(7), cmsg(3), systemd.socket(5)
 * @version     1.0
 */
#ifndef LAP_COM_REGISTRY_REGISTRY_INITIALIZER_HPP
#define LAP_COM_REGISTRY_REGISTRY_INITIALIZER_HPP

#include "ServiceSlot.hpp"
#include "SharedMemoryRegistry.hpp"
#include <lap/core/CResult.hpp>
#include <lap/core/CString.hpp>

#include <cstdint>
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

    /**
     * @brief Registry initialization server
     * 
     * @details Lifecycle:
     *          1. Create memfd (QM or ASIL)
     *          2. Initialize registry slots
     *          3. Listen on Unix Domain Socket
     *          4. Accept client connections
     *          5. Send memfd FD via SCM_RIGHTS
     *          6. Keep running until shutdown
     * 
     * @note Designed for systemd socket activation:
     *       - Socket passed via SD_LISTEN_FDS_START
     *       - Or manually bind to socket path
     */
    class RegistryInitializer
    {
    public:
        /**
         * @brief Constructor
         * @param registry_type Type of registry (QM or ASIL)
         * @param socket_path Unix domain socket path (e.g., /run/lap/registry_qm.sock)
         */
        explicit RegistryInitializer(RegistryType registry_type, 
                                     const String& socket_path);
        
        /**
         * @brief Destructor - cleanup resources
         */
        ~RegistryInitializer();

        // Non-copyable, non-movable
        RegistryInitializer(const RegistryInitializer&) = delete;
        RegistryInitializer& operator=(const RegistryInitializer&) = delete;
        RegistryInitializer(RegistryInitializer&&) = delete;
        RegistryInitializer& operator=(RegistryInitializer&&) = delete;
        
        /**
         * @brief Initialize the registry server
         * @return Result indicating success or error
         * 
         * @details Steps:
         *          1. Create memfd
         *          2. Resize to 256KB
         *          3. mmap to process space
         *          4. Initialize slots to IDLE
         *          5. Seal memfd (F_SEAL_SHRINK|GROW|SEAL)
         */
        Result<void> Initialize() noexcept;
        
        /**
         * @brief Start listening for client connections
         * @param use_systemd_socket If true, use systemd-provided socket (SD_LISTEN_FDS_START)
         * @return Result indicating success or error
         * 
         * @details Blocks until shutdown
         */
        Result<void> Run(bool use_systemd_socket = false) noexcept;
        
        /**
         * @brief Shutdown the server (can be called from signal handler)
         */
        void Shutdown() noexcept;
        
        /**
         * @brief Get memfd file descriptor (for testing)
         * @return memfd FD or -1 if not initialized
         */
        int GetMemfd() const noexcept { return memfd_; }
        
        /**
         * @brief Get mapped registry slots (for testing)
         * @return Pointer to slot array or nullptr if not initialized
         */
        ServiceSlot* GetSlots() const noexcept { return slots_; }
        
    private:
        /**
         * @brief Create and initialize memfd
         * @return Result indicating success or error
         */
        Result<void> createMemfd() noexcept;
        
        /**
         * @brief Create Unix domain socket
         * @param use_systemd If true, use SD_LISTEN_FDS_START
         * @return Result indicating success or error
         */
        Result<void> createSocket(bool use_systemd) noexcept;
        
        /**
         * @brief Accept client connection and send memfd FD
         * @param client_fd Client socket file descriptor
         * @return Result indicating success or error
         */
        Result<void> handleClient(int client_fd) noexcept;
        
        /**
         * @brief Send memfd FD to client via SCM_RIGHTS
         * @param client_fd Client socket file descriptor
         * @return Result indicating success or error
         */
        Result<void> sendMemfdToClient(int client_fd) noexcept;
        
        // Configuration
        RegistryType registry_type_;
        String socket_path_;
        
        // Resources
        int memfd_ = -1;                    ///< Anonymous memfd file descriptor
        int socket_fd_ = -1;                ///< Unix domain socket file descriptor
        ServiceSlot* slots_ = nullptr;      ///< Mapped registry slots
        
        // Runtime state
        std::atomic<bool> running_{false};  ///< Server running flag
        std::thread accept_thread_;         ///< Client acceptance thread
    };

} // namespace registry
} // namespace com
} // namespace lap

#endif // LAP_COM_REGISTRY_REGISTRY_INITIALIZER_HPP
