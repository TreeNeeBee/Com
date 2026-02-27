/**
 * @file        main.cpp
 * @author      LightAP Development Team
 * @brief       Registry server daemon entry point
 * @date        2026/02/06
 * @details     Systemd-activated daemon that creates registry shared memory (memfd)
 *              and distributes file descriptors to clients via Unix Domain Socket
 *              + SCM_RIGHTS. Supports both QM and ASIL registry types.
 * @copyright   Copyright (c) 2026
 * @note        Usage: lap-registry-init --type=qm --socket=/run/lap/registry_qm.sock
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §2.2.2 (UDS FD Passing)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/20  <td>1.0      <td>LightAP Team    <td>Initial implementation
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style + AUTOSAR naming refactor
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CRegistryServer.hpp"
#include "ComTypes.hpp"

// ==================== Standard Library Headers ====================
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>

namespace
{
    using lap::com::registry::CRegistryServer;
    using lap::com::registry::RegistryType;
    using lap::core::Bool;
    using lap::core::String;

    // ==================== Signal Handling ====================

    /**
     * @brief Global server pointer for signal handler access
     * @note  Async-signal-safe: Shutdown() only sets an atomic flag
     */
    static std::atomic< Bool >      g_bShutdown { false };
    static CRegistryServer*         g_pServer   { nullptr };

    /**
     * @brief POSIX signal handler (SIGINT / SIGTERM)
     * @param sigNum Signal number received
     * @note  Only calls async-signal-safe operations
     */
    void HandleSignal( int sigNum ) noexcept
    {
        if ( sigNum == SIGINT || sigNum == SIGTERM )
        {
            g_bShutdown.store( true, std::memory_order_release );

            if ( g_pServer != nullptr )
            {
                g_pServer->Shutdown();
            }
        }
    }

    /**
     * @brief Install signal handlers using sigaction (POSIX-compliant)
     * @note  Uses sigaction() instead of signal() for reliable behavior
     */
    void InstallSignalHandlers() noexcept
    {
        struct sigaction sa{};
        sa.sa_handler = HandleSignal;
        sigemptyset( &sa.sa_mask );
        sa.sa_flags = 0;   // No SA_RESTART: allow interrupted syscalls to return

        sigaction( SIGINT, &sa, nullptr );
        sigaction( SIGTERM, &sa, nullptr );

        // Ignore SIGPIPE (broken pipe from disconnected clients)
        struct sigaction saPipe{};
        saPipe.sa_handler = SIG_IGN;
        sigemptyset( &saPipe.sa_mask );
        saPipe.sa_flags = 0;
        sigaction( SIGPIPE, &saPipe, nullptr );
    }

    // ==================== Configuration ====================

    /**
     * @brief Daemon startup configuration
     */
    struct DaemonConfig
    {
        RegistryType    m_registryType  { RegistryType::kQM };
        String          m_strSocketPath { "/run/lap/registry_qm.sock" };
    };

    /**
     * @brief Print usage help to stdout
     * @param programName Executable path (argv[0])
     */
    void PrintUsage( const char* programName ) noexcept
    {
        std::cout << "Usage: " << programName << " [options]\n"
                  << "Options:\n"
                  << "  --type=<qm|asil>        Registry type (default: qm)\n"
                  << "  --socket=<path>         Unix domain socket path\n"
                  << "                          (default: /run/lap/registry_qm.sock)\n"
                  << "  --help, -h              Show this help message\n"
                  << "\n"
                  << "Example:\n"
                  << "  " << programName
                  << " --type=qm --socket=/run/lap/registry_qm.sock\n"
                  << std::endl;
    }

    /**
     * @brief Parse command-line arguments into DaemonConfig
     * @param argc Argument count
     * @param argv Argument vector
     * @param config [out] Parsed configuration
     * @return true if parsing succeeded, false on error or --help
     */
    Bool ParseArguments( int argc, char** argv, DaemonConfig& config ) noexcept
    {
        for ( int i = 1; i < argc; ++i )
        {
            const String arg( argv[i] );

            if ( arg.substr( 0, 7 ) == "--type=" )
            {
                const String typeStr = arg.substr( 7 );

                if ( typeStr == "qm" )
                {
                    config.m_registryType = RegistryType::kQM;
                }
                else if ( typeStr == "asil" )
                {
                    config.m_registryType = RegistryType::kASIL;
                }
                else
                {
                    LAP_COM_LOG_ERROR << "Invalid registry type: " << typeStr
                                     << " (must be 'qm' or 'asil')";
                    return false;
                }
            }
            else if ( arg.substr( 0, 9 ) == "--socket=" )
            {
                config.m_strSocketPath = arg.substr( 9 );
            }
            else if ( arg == "--help" || arg == "-h" )
            {
                PrintUsage( argv[0] );
                return false;
            }
            else
            {
                LAP_COM_LOG_ERROR << "Unknown argument: " << arg;
                return false;
            }
        }

        return true;
    }

}   // anonymous namespace

// ==================== Main Entry Point ====================

int main( int argc, char** argv )
{
    // Step 1: Parse command-line arguments
    DaemonConfig config;
    if ( !ParseArguments( argc, argv, config ) )
    {
        return EXIT_FAILURE;
    }

    const char* registryTypeStr =
        ( config.m_registryType == RegistryType::kQM ) ? "QM" : "ASIL";

    LAP_COM_LOG_INFO << "Starting registry server: type=" << registryTypeStr
                     << ", socket=" << config.m_strSocketPath;

    // Step 2: Install POSIX signal handlers
    InstallSignalHandlers();

    // Step 3: Create and initialize registry server
    CRegistryServer server( config.m_registryType, config.m_strSocketPath );
    g_pServer = &server;

    auto initResult = server.Initialize();
    if ( !initResult.HasValue() )
    {
        LAP_COM_LOG_ERROR << "Failed to initialize registry: "
                          << initResult.Error().Message();
        g_pServer = nullptr;
        return EXIT_FAILURE;
    }

    LAP_COM_LOG_INFO << "Registry initialized successfully, memfd="
                     << server.GetMemfd();

    // Step 4: Run server event loop (blocks until shutdown signal)
    auto runResult = server.Run( false );   // TODO: Support systemd socket activation
    if ( !runResult.HasValue() )
    {
        LAP_COM_LOG_ERROR << "Server run failed: " << runResult.Error().Message();
        g_pServer = nullptr;
        return EXIT_FAILURE;
    }

    LAP_COM_LOG_INFO << "Registry server stopped cleanly";

    g_pServer = nullptr;
    return EXIT_SUCCESS;
}
