/**
 * @file        lap-registry-init.cpp
 * @author      LightAP Development Team
 * @brief       Registry initialization daemon - Phase 2 UDS FD passing server
 * @date        2025-11-20
 * @details     Systemd-activated service that creates registry memfd and distributes
 *              to client processes via Unix Domain Socket + SCM_RIGHTS
 * @copyright   Copyright (c) 2025
 * @usage       /usr/local/bin/lap-registry-init --type=qm --socket=/run/lap/registry_qm.sock
 */

#include "RegistryInitializer.hpp"
#include "ComTypes.hpp"

#include <iostream>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <atomic>

using namespace lap::com::registry;
using lap::core::String;

// Global shutdown flag
static std::atomic<bool> g_shutdown{false};
static RegistryInitializer* g_initializer = nullptr;

// Signal handler
void signal_handler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        LAP_COM_LOG_WARN << "Received signal " << signal << ", shutting down...";
        g_shutdown.store(true, std::memory_order_release);
        
        if (g_initializer != nullptr)
        {
            g_initializer->Shutdown();
        }
    }
}

// Parse command-line arguments
struct Config
{
    RegistryType type = RegistryType::QM;
    String socket_path = "/run/lap/registry_qm.sock";
};

bool parse_args(int argc, char** argv, Config& config)
{
    for (int i = 1; i < argc; ++i)
    {
        const char* arg = argv[i];
        
        if (strncmp(arg, "--type=", 7) == 0)
        {
            const char* type_str = arg + 7;
            if (strcmp(type_str, "qm") == 0)
            {
                config.type = RegistryType::QM;
            }
            else if (strcmp(type_str, "asil") == 0)
            {
                config.type = RegistryType::ASIL;
            }
            else
            {
                LAP_COM_LOG_ERROR << "Invalid registry type: " << type_str << " (must be 'qm' or 'asil')";
                return false;
            }
        }
        else if (strncmp(arg, "--socket=", 9) == 0)
        {
            config.socket_path = arg + 9;
        }
        else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0)
        {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --type=<qm|asil>        Registry type (default: qm)\n"
                      << "  --socket=<path>         Unix domain socket path\n"
                      << "                          (default: /run/lap/registry_qm.sock)\n"
                      << "  --help, -h              Show this help message\n"
                      << "\n"
                      << "Example:\n"
                      << "  " << argv[0] << " --type=qm --socket=/run/lap/registry_qm.sock\n"
                      << std::endl;
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

int main(int argc, char** argv)
{
    // Parse arguments
    Config config;
    if (!parse_args(argc, argv, config))
    {
        return EXIT_FAILURE;
    }
    
    LAP_COM_LOG_INFO << "Starting registry initializer: type=" 
              << (config.type == RegistryType::QM ? "QM" : "ASIL")
              << ", socket=" << config.socket_path;
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);  // Ignore broken pipe
    
    // Create initializer
    RegistryInitializer initializer(config.type, config.socket_path);
    g_initializer = &initializer;
    
    // Initialize registry (create memfd, initialize slots, seal memory)
    auto init_result = initializer.Initialize();
    if (!init_result.HasValue())
    {
        LAP_COM_LOG_ERROR << "Failed to initialize registry: " 
                  << init_result.Error().Message();
        return EXIT_FAILURE;
    }
    
    LAP_COM_LOG_INFO << "Registry initialized successfully, memfd=" 
              << initializer.GetMemfd();
    
    // Run server (blocks until shutdown)
    auto run_result = initializer.Run(false);  // TODO: Support systemd socket activation
    if (!run_result.HasValue())
    {
        LAP_COM_LOG_ERROR << "Server run failed: " << run_result.Error().Message();
        return EXIT_FAILURE;
    }
    
    LAP_COM_LOG_INFO << "Registry initializer stopped cleanly";
    
    g_initializer = nullptr;
    return EXIT_SUCCESS;
}
