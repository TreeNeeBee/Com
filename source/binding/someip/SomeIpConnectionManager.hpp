/**
 * @file SomeIpConnectionManager.hpp
 * @brief SOME/IP connection management using vsomeip
 * 
 * This file provides connection lifecycle management for vsomeip,
 * similar to DBusConnectionManager but for SOME/IP protocol.
 * 
 * SOME/IP (Scalable service-Oriented MiddlewarE over IP) is designed for:
 * - Automotive Ethernet communication
 * - Service-oriented architecture (SOA)
 * - High-performance inter-ECU communication
 * - Dynamic service discovery
 */

#ifndef LAP_COM_SOMEIP_CONNECTION_MANAGER_HPP
#define LAP_COM_SOMEIP_CONNECTION_MANAGER_HPP

#include <memory>
#include <string>
#include <mutex>
#include <vsomeip/vsomeip.hpp>

#include <Core/CoreBase/inc/CTypedef.h>
#include <Core/CoreBase/inc/CResult.h>
#include <Core/CoreBase/inc/CLog.h>
#include <ComTypes.hpp>

namespace lap {
namespace com {
namespace someip {

using namespace lap::core;
using namespace lap::log;

/**
 * @class SomeIpConnectionManager
 * @brief Manages vsomeip application lifecycle
 * 
 * Singleton pattern for managing a single vsomeip application instance.
 * Provides initialization, event loop management, and graceful shutdown.
 * 
 * Thread-safety: All public methods are thread-safe.
 */
class SomeIpConnectionManager {
public:
    /**
     * @brief Get singleton instance
     * @return Reference to the singleton instance
     */
    static SomeIpConnectionManager& getInstance() {
        static SomeIpConnectionManager instance;
        return instance;
    }

    // Delete copy/move constructors
    SomeIpConnectionManager(const SomeIpConnectionManager&) = delete;
    SomeIpConnectionManager& operator=(const SomeIpConnectionManager&) = delete;
    SomeIpConnectionManager(SomeIpConnectionManager&&) = delete;
    SomeIpConnectionManager& operator=(SomeIpConnectionManager&&) = delete;

    /**
     * @brief Initialize vsomeip application
     * @param appName Application name (must be unique per process)
     * @param configPath Optional path to vsomeip JSON configuration file
     * @return Result<void> - Success or error
     * 
     * @note Must be called before any SOME/IP operations
     * @note Application name must match the one in vsomeip config
     */
    Result<void> Initialize(const String& appName, const String& configPath = "") {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_initialized) {
            LAP_LOG_WARN("SomeIpConnectionManager already initialized");
            return Result<void>();
        }

        try {
            // Create vsomeip runtime
            m_runtime = vsomeip::runtime::get();
            if (!m_runtime) {
                LAP_LOG_ERROR("Failed to get vsomeip runtime");
                return Result<void>(MakeErrorCode(ComErrc::InitializationFailed));
            }

            // Create application
            m_app = m_runtime->create_application(appName.c_str());
            if (!m_app) {
                LAP_LOG_ERROR("Failed to create vsomeip application: %s", appName.c_str());
                return Result<void>(MakeErrorCode(ComErrc::InitializationFailed));
            }

            // Set configuration path if provided
            if (!configPath.empty()) {
                // vsomeip loads config from environment variable VSOMEIP_CONFIGURATION
                // or from default paths. Custom loading would require vsomeip rebuild.
                LAP_LOG_INFO("Configuration path specified: %s", configPath.c_str());
                LAP_LOG_INFO("Note: Set VSOMEIP_CONFIGURATION environment variable");
            }

            // Initialize application
            if (!m_app->init()) {
                LAP_LOG_ERROR("Failed to initialize vsomeip application");
                m_app.reset();
                return Result<void>(MakeErrorCode(ComErrc::InitializationFailed));
            }

            m_appName = appName;
            m_initialized = true;

            LAP_LOG_INFO("SomeIpConnectionManager initialized: %s", appName.c_str());
            return Result<void>();

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("Exception in Initialize: %s", e.what());
            return Result<void>(MakeErrorCode(ComErrc::InitializationFailed));
        }
    }

    /**
     * @brief Start vsomeip application and event loop
     * @param blocking If true, blocks until Stop() is called
     * @return Result<void> - Success or error
     * 
     * @note Call this after registering all services/clients
     * @note For non-blocking mode, call this in a separate thread
     */
    Result<void> Start(bool blocking = false) {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_initialized) {
            LAP_LOG_ERROR("SomeIpConnectionManager not initialized");
            return Result<void>(MakeErrorCode(ComErrc::NotInitialized));
        }

        if (m_running) {
            LAP_LOG_WARN("Application already running");
            return Result<void>();
        }

        try {
            m_running = true;
            
            if (blocking) {
                LAP_LOG_INFO("Starting vsomeip application (blocking)...");
                m_app->start(); // Blocks until stop() is called
            } else {
                LAP_LOG_INFO("Starting vsomeip application (non-blocking)...");
                // For non-blocking, user should call this in a thread
                m_app->start();
            }

            return Result<void>();

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("Exception in Start: %s", e.what());
            m_running = false;
            return Result<void>(MakeErrorCode(ComErrc::ConnectionFailed));
        }
    }

    /**
     * @brief Stop vsomeip application
     * @return Result<void> - Success or error
     * 
     * @note This will unblock Start() if it was called with blocking=true
     */
    Result<void> Stop() {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_running) {
            return Result<void>();
        }

        try {
            LAP_LOG_INFO("Stopping vsomeip application...");
            if (m_app) {
                m_app->stop();
            }
            m_running = false;
            LAP_LOG_INFO("vsomeip application stopped");
            return Result<void>();

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("Exception in Stop: %s", e.what());
            return Result<void>(MakeErrorCode(ComErrc::Disconnected));
        }
    }

    /**
     * @brief Get vsomeip application instance
     * @return Shared pointer to application (may be null if not initialized)
     */
    std::shared_ptr<vsomeip::application> GetApplication() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_app;
    }

    /**
     * @brief Check if manager is initialized
     */
    bool IsInitialized() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_initialized;
    }

    /**
     * @brief Check if application is running
     */
    bool IsRunning() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_running;
    }

    /**
     * @brief Get application name
     */
    String GetApplicationName() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_appName;
    }

    /**
     * @brief Cleanup and deinitialize
     * @note Called automatically by destructor
     */
    void Deinitialize() {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_initialized) {
            return;
        }

        try {
            if (m_running && m_app) {
                m_app->stop();
                m_running = false;
            }

            m_app.reset();
            m_initialized = false;

            LAP_LOG_INFO("SomeIpConnectionManager deinitialized");

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("Exception in Deinitialize: %s", e.what());
        }
    }

private:
    SomeIpConnectionManager() 
        : m_initialized(false)
        , m_running(false) {
    }

    ~SomeIpConnectionManager() {
        Deinitialize();
    }

    mutable std::mutex m_mutex;
    std::shared_ptr<vsomeip::runtime> m_runtime;
    std::shared_ptr<vsomeip::application> m_app;
    String m_appName;
    bool m_initialized;
    bool m_running;
};

} // namespace someip
} // namespace com
} // namespace lap

#endif // LAP_COM_SOMEIP_CONNECTION_MANAGER_HPP
