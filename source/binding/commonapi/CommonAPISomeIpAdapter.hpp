/**
 * @file CommonAPISomeIpAdapter.hpp
 * @brief Adapter layer for CommonAPI SOME/IP generated code
 * 
 * This adapter bridges CommonAPI-SomeIP generated Proxy/Stub with LightAP conventions:
 * - Uses lap::core::Result<T> for error handling
 * - Integrates LAP_LOG_* logging
 * - Manages vsomeip lifecycle through SomeIpConnectionManager
 * 
 * Usage pattern:
 * - Client: ProxyAdapter<GeneratedProxy> for consuming services
 * - Server: StubAdapter<GeneratedStub> for providing services
 */

#ifndef LAP_COM_COMMONAPI_SOMEIP_ADAPTER_HPP
#define LAP_COM_COMMONAPI_SOMEIP_ADAPTER_HPP

#include <memory>
#include <chrono>
#include <CommonAPI/CommonAPI.hpp>

#include <Core/CoreBase/inc/CTypedef.h>
#include <Core/CoreBase/inc/CResult.h>
#include <Core/CoreBase/inc/CLog.h>
#include <ComTypes.hpp>
#include <binding/someip/SomeIpConnectionManager.hpp>

namespace lap {
namespace com {
namespace commonapi {

using namespace lap::core;
using namespace lap::log;

/**
 * @class SomeIpProxyAdapter
 * @brief Client-side adapter for CommonAPI-SomeIP Proxy
 * 
 * @tparam ProxyType CommonAPI generated Proxy class (e.g., CalculatorProxy)
 * 
 * Example:
 * @code
 * SomeIpProxyAdapter<v1::com::example::CalculatorProxy> adapter("local", "Calculator");
 * auto result = adapter.Initialize();
 * if (result.HasValue()) {
 *     auto proxy = adapter.GetProxy();
 *     // Use proxy methods...
 * }
 * @endcode
 */
template<typename ProxyType>
class SomeIpProxyAdapter {
public:
    /**
     * @brief Constructor
     * @param domain Service domain (e.g., "local")
     * @param instance Service instance name
     * @param connectionId Connection identifier (default: "client")
     */
    SomeIpProxyAdapter(const String& domain, const String& instance, 
                       const String& connectionId = "client")
        : m_domain(domain)
        , m_instance(instance)
        , m_connectionId(connectionId)
        , m_initialized(false) {
        LAP_LOG_DEBUG("SomeIpProxyAdapter created: %s:%s", domain.c_str(), instance.c_str());
    }

    ~SomeIpProxyAdapter() {
        if (m_initialized) {
            LAP_LOG_DEBUG("SomeIpProxyAdapter destroyed: %s:%s", 
                         m_domain.c_str(), m_instance.c_str());
        }
    }

    /**
     * @brief Initialize proxy and wait for service availability
     * @param timeoutMs Timeout in milliseconds (0 = no wait, -1 = infinite)
     * @return Result<void> - Success or error
     * 
     * @note Ensures vsomeip connection manager is initialized
     */
    Result<void> Initialize(int32_t timeoutMs = 5000) {
        if (m_initialized) {
            LAP_LOG_WARN("SomeIpProxyAdapter already initialized");
            return Result<void>();
        }

        // Ensure vsomeip is initialized
        auto& connMgr = someip::SomeIpConnectionManager::getInstance();
        if (!connMgr.IsInitialized()) {
            LAP_LOG_ERROR("SomeIpConnectionManager not initialized. Call Initialize() first.");
            return Result<void>(MakeErrorCode(ComErrc::NotInitialized));
        }

        try {
            // Build service address
            String serviceAddress = m_domain + ":" + ProxyType::getInterface() + ":" + m_instance;
            
            LAP_LOG_INFO("Creating SOME/IP proxy: %s", serviceAddress.c_str());

            // Get CommonAPI runtime for SOME/IP
            auto runtime = CommonAPI::Runtime::get();
            if (!runtime) {
                LAP_LOG_ERROR("Failed to get CommonAPI runtime");
                return Result<void>(MakeErrorCode(ComErrc::InitializationFailed));
            }

            // Create proxy
            m_proxy = runtime->buildProxy<ProxyType>(m_domain, m_instance, m_connectionId);
            if (!m_proxy) {
                LAP_LOG_ERROR("Failed to create proxy: %s", serviceAddress.c_str());
                return Result<void>(MakeErrorCode(ComErrc::ConnectionFailed));
            }

            // Wait for service availability
            if (timeoutMs != 0) {
                LAP_LOG_DEBUG("Waiting for service availability (timeout=%dms)...", timeoutMs);
                
                bool available = false;
                if (timeoutMs < 0) {
                    // Infinite wait
                    m_proxy->isAvailableBlocking();
                    available = true;
                } else {
                    // Timed wait
                    available = m_proxy->isAvailableBlocking(std::chrono::milliseconds(timeoutMs));
                }

                if (!available) {
                    LAP_LOG_ERROR("Service not available: %s", serviceAddress.c_str());
                    return Result<void>(MakeErrorCode(ComErrc::Timeout));
                }
            }

            m_initialized = true;
            LAP_LOG_INFO("SOME/IP proxy initialized successfully: %s", serviceAddress.c_str());
            return Result<void>();

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("Exception in Initialize: %s", e.what());
            return Result<void>(MakeErrorCode(ComErrc::InitializationFailed));
        }
    }

    /**
     * @brief Get the proxy instance
     * @return Shared pointer to proxy (may be null if not initialized)
     */
    std::shared_ptr<ProxyType> GetProxy() const {
        return m_proxy;
    }

    /**
     * @brief Check if proxy is available
     * @return true if service is available
     */
    bool IsAvailable() const {
        return m_proxy && m_proxy->isAvailable();
    }

    /**
     * @brief Check if adapter is initialized
     */
    bool IsInitialized() const {
        return m_initialized;
    }

    /**
     * @brief Convert CommonAPI CallStatus to Result<T>
     * @tparam T Value type
     * @param callStatus CommonAPI call status
     * @param value The actual value (if call succeeded)
     * @return Result<T> with value or error
     * 
     * @note Helper for wrapping synchronous method calls
     */
    template<typename T>
    Result<T> WrapCallStatus(const CommonAPI::CallStatus& callStatus, const T& value) const {
        if (callStatus == CommonAPI::CallStatus::SUCCESS) {
            return Result<T>(value);
        }

        // Map CommonAPI errors to LightAP error codes
        const char* errorMsg = "";
        ComErrc errorCode = ComErrc::Unknown;

        switch (callStatus) {
            case CommonAPI::CallStatus::OUT_OF_MEMORY:
                errorMsg = "Out of memory";
                errorCode = ComErrc::OutOfMemory;
                break;
            case CommonAPI::CallStatus::NOT_AVAILABLE:
                errorMsg = "Service not available";
                errorCode = ComErrc::NotAvailable;
                break;
            case CommonAPI::CallStatus::CONNECTION_FAILED:
                errorMsg = "Connection failed";
                errorCode = ComErrc::ConnectionFailed;
                break;
            case CommonAPI::CallStatus::REMOTE_ERROR:
                errorMsg = "Remote error";
                errorCode = ComErrc::RemoteError;
                break;
            case CommonAPI::CallStatus::SUBSCRIPTION_REFUSED:
                errorMsg = "Subscription refused";
                errorCode = ComErrc::SubscriptionFailed;
                break;
            default:
                errorMsg = "Unknown CommonAPI error";
                errorCode = ComErrc::Unknown;
                break;
        }

        LAP_LOG_ERROR("SOME/IP call failed: %s", errorMsg);
        return Result<T>(MakeErrorCode(errorCode));
    }

    /**
     * @brief Overload for void return type
     */
    Result<void> WrapCallStatus(const CommonAPI::CallStatus& callStatus) const {
        if (callStatus == CommonAPI::CallStatus::SUCCESS) {
            return Result<void>();
        }

        // Same error mapping as above
        const char* errorMsg = "";
        ComErrc errorCode = ComErrc::Unknown;

        switch (callStatus) {
            case CommonAPI::CallStatus::OUT_OF_MEMORY:
                errorMsg = "Out of memory";
                errorCode = ComErrc::OutOfMemory;
                break;
            case CommonAPI::CallStatus::NOT_AVAILABLE:
                errorMsg = "Service not available";
                errorCode = ComErrc::NotAvailable;
                break;
            case CommonAPI::CallStatus::CONNECTION_FAILED:
                errorMsg = "Connection failed";
                errorCode = ComErrc::ConnectionFailed;
                break;
            case CommonAPI::CallStatus::REMOTE_ERROR:
                errorMsg = "Remote error";
                errorCode = ComErrc::RemoteError;
                break;
            case CommonAPI::CallStatus::SUBSCRIPTION_REFUSED:
                errorMsg = "Subscription refused";
                errorCode = ComErrc::SubscriptionFailed;
                break;
            default:
                errorMsg = "Unknown CommonAPI error";
                errorCode = ComErrc::Unknown;
                break;
        }

        LAP_LOG_ERROR("SOME/IP call failed: %s", errorMsg);
        return Result<void>(MakeErrorCode(errorCode));
    }

private:
    String m_domain;
    String m_instance;
    String m_connectionId;
    std::shared_ptr<ProxyType> m_proxy;
    bool m_initialized;
};

/**
 * @class SomeIpStubAdapter
 * @brief Server-side adapter for CommonAPI-SomeIP Stub
 * 
 * @tparam StubType CommonAPI generated Stub implementation class
 * 
 * Example:
 * @code
 * class MyService : public v1::com::example::CalculatorStubDefault { ... };
 * auto service = std::make_shared<MyService>();
 * 
 * SomeIpStubAdapter<MyService> adapter("local", "Calculator");
 * auto result = adapter.Initialize(service);
 * // Service is now registered and running
 * @endcode
 */
template<typename StubType>
class SomeIpStubAdapter {
public:
    /**
     * @brief Constructor
     * @param domain Service domain (e.g., "local")
     * @param instance Service instance name
     * @param connectionId Connection identifier (default: "service")
     */
    SomeIpStubAdapter(const String& domain, const String& instance,
                      const String& connectionId = "service")
        : m_domain(domain)
        , m_instance(instance)
        , m_connectionId(connectionId)
        , m_initialized(false) {
        LAP_LOG_DEBUG("SomeIpStubAdapter created: %s:%s", domain.c_str(), instance.c_str());
    }

    ~SomeIpStubAdapter() {
        Deinitialize();
    }

    /**
     * @brief Initialize and register service
     * @param stub Shared pointer to stub implementation
     * @return Result<void> - Success or error
     * 
     * @note Ensures vsomeip connection manager is initialized
     * @note Service becomes available after this call
     */
    Result<void> Initialize(std::shared_ptr<StubType> stub) {
        if (m_initialized) {
            LAP_LOG_WARN("SomeIpStubAdapter already initialized");
            return Result<void>();
        }

        if (!stub) {
            LAP_LOG_ERROR("Stub pointer is null");
            return Result<void>(MakeErrorCode(ComErrc::InvalidParameter));
        }

        // Ensure vsomeip is initialized
        auto& connMgr = someip::SomeIpConnectionManager::getInstance();
        if (!connMgr.IsInitialized()) {
            LAP_LOG_ERROR("SomeIpConnectionManager not initialized. Call Initialize() first.");
            return Result<void>(MakeErrorCode(ComErrc::NotInitialized));
        }

        try {
            // Build service address
            String serviceAddress = m_domain + ":" + StubType::StubInterface::getInterface() + ":" + m_instance;
            
            LAP_LOG_INFO("Registering SOME/IP service: %s", serviceAddress.c_str());

            // Get CommonAPI runtime for SOME/IP
            auto runtime = CommonAPI::Runtime::get();
            if (!runtime) {
                LAP_LOG_ERROR("Failed to get CommonAPI runtime");
                return Result<void>(MakeErrorCode(ComErrc::InitializationFailed));
            }

            // Register service
            bool registered = runtime->registerService(m_domain, m_instance, stub, m_connectionId);
            if (!registered) {
                LAP_LOG_ERROR("Failed to register service: %s", serviceAddress.c_str());
                return Result<void>(MakeErrorCode(ComErrc::RegistrationFailed));
            }

            m_stub = stub;
            m_initialized = true;

            LAP_LOG_INFO("SOME/IP service registered successfully: %s", serviceAddress.c_str());
            return Result<void>();

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("Exception in Initialize: %s", e.what());
            return Result<void>(MakeErrorCode(ComErrc::InitializationFailed));
        }
    }

    /**
     * @brief Unregister service
     * @return Result<void> - Success or error
     */
    Result<void> Deinitialize() {
        if (!m_initialized) {
            return Result<void>();
        }

        try {
            String serviceAddress = m_domain + ":" + StubType::StubInterface::getInterface() + ":" + m_instance;
            LAP_LOG_INFO("Unregistering SOME/IP service: %s", serviceAddress.c_str());

            auto runtime = CommonAPI::Runtime::get();
            if (runtime && m_stub) {
                bool unregistered = runtime->unregisterService(m_domain, 
                    StubType::StubInterface::getInterface(), m_instance);
                if (!unregistered) {
                    LAP_LOG_WARN("Failed to unregister service: %s", serviceAddress.c_str());
                }
            }

            m_stub.reset();
            m_initialized = false;

            LAP_LOG_INFO("SOME/IP service unregistered: %s", serviceAddress.c_str());
            return Result<void>();

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("Exception in Deinitialize: %s", e.what());
            return Result<void>(MakeErrorCode(ComErrc::Disconnected));
        }
    }

    /**
     * @brief Get stub implementation
     */
    std::shared_ptr<StubType> GetStub() const {
        return m_stub;
    }

    /**
     * @brief Check if adapter is initialized
     */
    bool IsInitialized() const {
        return m_initialized;
    }

private:
    String m_domain;
    String m_instance;
    String m_connectionId;
    std::shared_ptr<StubType> m_stub;
    bool m_initialized;
};

} // namespace commonapi
} // namespace com
} // namespace lap

#endif // LAP_COM_COMMONAPI_SOMEIP_ADAPTER_HPP
