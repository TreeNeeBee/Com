/**
 * @file        CommonAPIDBusAdapter.hpp
 * @brief       Adapter layer between CommonAPI-DBus generated code and LightAP Com infrastructure
 * @date        2025-10-30
 * @details     Provides base classes and utilities to integrate CommonAPI-DBus Proxy/Stub
 *              with LightAP's Result<T>, logging, and error handling conventions.
 * 
 * Usage:
 *   1. Generate code from Franca IDL using tools/commonapi/generate.sh (dbus transport)
 *   2. Inherit from DBusProxyAdapter or DBusStubAdapter
 *   3. Implement your service logic with LightAP conventions
 * 
 * @note This adapter is specifically for D-Bus transport. For SOME/IP, use CommonAPISomeIpAdapter.hpp
 * @note This is a compatibility layer - you can also use CommonAPI directly
 *       if you prefer CommonAPI's native API and don't need LightAP integration.
 */

#ifndef LAP_COM_COMMONAPI_DBUS_ADAPTER_HPP
#define LAP_COM_COMMONAPI_DBUS_ADAPTER_HPP

#include <core/CResult.hpp>
#include <core/CString.hpp>
#include <core/CTypedef.hpp>
#include <log/CLog.hpp>
#include "../../inc/ComTypes.hpp"

#include <CommonAPI/CommonAPI.hpp>
#include <memory>
#include <functional>

namespace lap
{
namespace com
{
namespace commonapi
{
    /**
     * @brief Base adapter for CommonAPI-DBus Proxy (client side)
     * @tparam ProxyType CommonAPI-DBus generated proxy class
     * 
     * Example:
     * @code
     * class MyServiceProxyWrapper : public DBusProxyAdapter<MyServiceProxy> {
     * public:
     *     MyServiceProxyWrapper() : DBusProxyAdapter("local", "MyService", "client") {}
     *     
     *     lap::core::Result<int> CallMethod(int param) {
     *         CommonAPI::CallStatus status;
     *         int result;
     *         GetProxy()->myMethod(param, status, result);
     *         return WrapCallStatus(status, result);
     *     }
     * };
     * @endcode
     */
    template<typename ProxyType>
    class DBusProxyAdapter
    {
    public:
        using ProxyPtr = std::shared_ptr<ProxyType>;
        
        DBusProxyAdapter(const lap::core::String& domain,
                        const lap::core::String& instance,
                        const lap::core::String& connection = "")
            : m_domain(domain)
            , m_instance(instance)
            , m_connectionId(connection)
        {
        }
        
        virtual ~DBusProxyAdapter() = default;
        
        /**
         * @brief Initialize proxy connection
         * @return Result<void> Success or error
         */
        lap::core::Result<void> Initialize()
        {
            try {
                if (m_connectionId.empty()) {
                    m_proxy = CommonAPI::Runtime::get()->buildProxy<ProxyType>(
                        m_domain.c_str(), m_instance.c_str());
                } else {
                    m_proxy = CommonAPI::Runtime::get()->buildProxy<ProxyType>(
                        m_domain.c_str(), m_instance.c_str(), m_connectionId.c_str());
                }
                
                if (!m_proxy) {
                    LAP_LOG_ERROR("COM.CommonAPI") << "Failed to create proxy: " 
                        << m_domain.c_str() << ":" << m_instance.c_str();
                    return lap::core::Result<void>::FromError(
                        MakeErrorCode(ComErrc::kCommunicationLinkError, 0));
                }
                
                // Wait for proxy to become available
                constexpr int MAX_WAIT_MS = 5000;
                if (!m_proxy->isAvailable()) {
                    LAP_LOG_INFO("COM.CommonAPI") << "Waiting for service availability...";
                    
                    std::mutex mtx;
                    std::condition_variable cv;
                    bool available = false;
                    
                    m_proxy->getProxyStatusEvent().subscribe([&](const CommonAPI::AvailabilityStatus& status) {
                        if (status == CommonAPI::AvailabilityStatus::AVAILABLE) {
                            std::lock_guard<std::mutex> lk(mtx);
                            available = true;
                            cv.notify_all();
                        }
                    });
                    
                    std::unique_lock<std::mutex> lk(mtx);
                    if (!cv.wait_for(lk, std::chrono::milliseconds(MAX_WAIT_MS), 
                                     [&available]{ return available; })) {
                        LAP_LOG_WARN("COM.CommonAPI") << "Service not available within timeout";
                        // Continue anyway - some methods might still work
                    }
                }
                
                LAP_LOG_INFO("COM.CommonAPI") << "Proxy initialized: " << m_domain.c_str() 
                                               << ":" << m_instance.c_str();
                return lap::core::Result<void>::FromValue();
                
            } catch (const std::exception& e) {
                LAP_LOG_ERROR("COM.CommonAPI") << "Proxy initialization exception: " << e.what();
                return lap::core::Result<void>::FromError(
                    MakeErrorCode(ComErrc::kCommunicationLinkError, 0));
            }
        }
        
        /**
         * @brief Check if proxy is available
         */
        bool IsAvailable() const noexcept
        {
            return m_proxy && m_proxy->isAvailable();
        }
        
        /**
         * @brief Get raw CommonAPI proxy (for advanced usage)
         */
        ProxyPtr GetProxy() const noexcept { return m_proxy; }
        
    protected:
        ProxyPtr m_proxy;
        lap::core::String m_domain;
        lap::core::String m_instance;
        lap::core::String m_connectionId;
        
        /**
         * @brief Helper: Convert CommonAPI CallStatus to LightAP Result
         */
        template<typename T>
        lap::core::Result<T> WrapCallStatus(const CommonAPI::CallStatus& status,
                                            const T& value,
                                            const char* methodName) const
        {
            if (status == CommonAPI::CallStatus::SUCCESS) {
                LAP_LOG_DEBUG("COM.CommonAPI") << methodName << " succeeded";
                return lap::core::Result<T>::FromValue(value);
            } else {
                LAP_LOG_ERROR("COM.CommonAPI") << methodName << " failed with status: " 
                                               << static_cast<int>(status);
                return lap::core::Result<T>::FromError(
                    MakeErrorCode(ComErrc::kCommunicationLinkError, 
                                 static_cast<int>(status)));
            }
        }
        
        lap::core::Result<void> WrapCallStatus(const CommonAPI::CallStatus& status,
                                               const char* methodName) const
        {
            if (status == CommonAPI::CallStatus::SUCCESS) {
                LAP_LOG_DEBUG("COM.CommonAPI") << methodName << " succeeded";
                return lap::core::Result<void>::FromValue();
            } else {
                LAP_LOG_ERROR("COM.CommonAPI") << methodName << " failed with status: " 
                                               << static_cast<int>(status);
                return lap::core::Result<void>::FromError(
                    MakeErrorCode(ComErrc::kCommunicationLinkError, 
                                 static_cast<int>(status)));
            }
        }
    };
    
    /**
     * @brief Base adapter for CommonAPI-DBus Stub (server side)
     * @tparam StubType CommonAPI-DBus generated stub implementation class
     * 
     * Example:
     * @code
     * class MyServiceImpl : public MyServiceStubDefault {
     * public:
     *     void myMethod(int param, methodReply_t reply) override {
     *         int result = param * 2;
     *         reply(result);
     *     }
     * };
     * 
     * class MyServiceStubWrapper : public DBusStubAdapter<MyServiceStub> {
     * public:
     *     MyServiceStubWrapper() : DBusStubAdapter("local", "MyService") {}
     * };
     * @endcode
     */
    template<typename StubType>
    class DBusStubAdapter
    {
    public:
        using StubPtr = std::shared_ptr<StubType>;
        
        DBusStubAdapter(const lap::core::String& domain,
                       const lap::core::String& instance,
                       const lap::core::String& connection = "")
            : m_domain(domain)
            , m_instance(instance)
            , m_connectionId(connection)
        {
        }
        
        virtual ~DBusStubAdapter() = default;
        
        /**
         * @brief Initialize and register stub
         * @param stub Shared pointer to your stub implementation
         * @return Result<void> Success or error
         */
        lap::core::Result<void> Initialize(StubPtr stub)
        {
            try {
                m_stub = stub;
                if (!m_stub) {
                    LAP_LOG_ERROR("COM.CommonAPI") << "Null stub provided";
                    return lap::core::Result<void>::FromError(
                        MakeErrorCode(ComErrc::kInvalidArgument, 0));
                }
                
                bool registered;
                if (m_connectionId.empty()) {
                    registered = CommonAPI::Runtime::get()->registerService(
                        m_domain.c_str(), m_instance.c_str(), m_stub);
                } else {
                    registered = CommonAPI::Runtime::get()->registerService(
                        m_domain.c_str(), m_instance.c_str(), m_stub, m_connectionId.c_str());
                }
                
                if (!registered) {
                    LAP_LOG_ERROR("COM.CommonAPI") << "Failed to register service: " 
                        << m_domain.c_str() << ":" << m_instance.c_str();
                    return lap::core::Result<void>::FromError(
                        MakeErrorCode(ComErrc::kCommunicationLinkError, 0));
                }
                
                LAP_LOG_INFO("COM.CommonAPI") << "Stub registered: " << m_domain.c_str() 
                                               << ":" << m_instance.c_str();
                return lap::core::Result<void>::FromValue();
                
            } catch (const std::exception& e) {
                LAP_LOG_ERROR("COM.CommonAPI") << "Stub registration exception: " << e.what();
                return lap::core::Result<void>::FromError(
                    MakeErrorCode(ComErrc::kCommunicationLinkError, 0));
            }
        }
        
        /**
         * @brief Unregister service
         */
        void Deinitialize() noexcept
        {
            if (m_stub) {
                try {
                    if (m_connectionId.empty()) {
                        CommonAPI::Runtime::get()->unregisterService(
                            m_domain.c_str(), m_stub->getStubAdapter()->getInterface(), 
                            m_instance.c_str());
                    } else {
                        CommonAPI::Runtime::get()->unregisterService(
                            m_domain.c_str(), m_stub->getStubAdapter()->getInterface(), 
                            m_instance.c_str(), m_connectionId.c_str());
                    }
                    LAP_LOG_INFO("COM.CommonAPI") << "Stub unregistered: " << m_domain.c_str();
                } catch (const std::exception& e) {
                    LAP_LOG_WARN("COM.CommonAPI") << "Unregister exception: " << e.what();
                }
                m_stub.reset();
            }
        }
        
        /**
         * @brief Get raw CommonAPI stub (for advanced usage)
         */
        StubPtr GetStub() const noexcept { return m_stub; }
        
    protected:
        StubPtr m_stub;
        lap::core::String m_domain;
        lap::core::String m_instance;
        lap::core::String m_connectionId;
    };
    
} // namespace commonapi
} // namespace com
} // namespace lap

#endif // LAP_COM_COMMONAPI_DBUS_ADAPTER_HPP
