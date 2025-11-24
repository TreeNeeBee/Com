/**
 * @file SomeIpMethodBinding.hpp
 * @brief SOME/IP method call binding layer
 * @details Provides synchronous and asynchronous method invocation over SOME/IP,
 *          with timeout control and error handling.
 * 
 * @copyright Copyright (c) 2025 LightAP
 */

#ifndef LAP_COM_SOMEIP_METHOD_BINDING_HPP
#define LAP_COM_SOMEIP_METHOD_BINDING_HPP

#include <CommonAPI/CommonAPI.hpp>
#include <functional>
#include <future>
#include <chrono>

#include "Core/CoreBase/inc/CTypedef.h"
#include "Core/CoreBase/inc/CResult.h"
#include "Core/CoreBase/inc/CLog.h"
#include "../ComTypes.hpp"

namespace lap {
namespace com {
namespace someip {

/**
 * @brief Client-side SOME/IP method caller
 * @tparam ProxyType The CommonAPI proxy type
 * 
 * Example:
 * @code
 * SomeIpMethodCaller<CalculatorProxy> caller(proxy);
 * 
 * // Synchronous call with timeout
 * auto result = caller.CallSync<int>(
 *     [](auto& p) { return p.calculate(10, 5, '+'); },
 *     std::chrono::milliseconds(1000)
 * );
 * 
 * // Asynchronous call
 * caller.CallAsync<int>(
 *     [](auto& p, auto callback) { p.calculateAsync(10, 5, '+', callback); },
 *     [](lap::core::Result<int> result) {
 *         if (result) {
 *             LAP_LOG_INFO("Result: {}", result.Value());
 *         }
 *     }
 * );
 * @endcode
 */
template<typename ProxyType>
class SomeIpMethodCaller {
public:
    using ProxyPtr = std::shared_ptr<ProxyType>;

    /**
     * @brief Construct method caller with proxy
     * @param proxy CommonAPI proxy instance
     */
    explicit SomeIpMethodCaller(ProxyPtr proxy) 
        : proxy_(proxy) {
        if (!proxy_) {
            LAP_LOG_ERROR("[SomeIpMethodCaller] Proxy is nullptr");
        }
    }

    /**
     * @brief Synchronous method call with timeout
     * @tparam ReturnType Expected return type
     * @tparam MethodFunc Method invocation functor type
     * @param method Lambda that calls the proxy method
     * @param timeout Maximum wait time for response
     * @return Result containing return value or error
     * 
     * The method functor receives proxy reference and returns CommonAPI::CallInfo.
     */
    template<typename ReturnType, typename MethodFunc>
    lap::core::Result<ReturnType> CallSync(
        MethodFunc method,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        
        if (!proxy_) {
            LAP_LOG_ERROR("[SomeIpMethodCaller] CallSync failed: proxy is null");
            return MakeErrorCode(ComErrc::NotInitialized);
        }

        try {
            // Call the method through proxy
            CommonAPI::CallStatus callStatus;
            ReturnType returnValue;
            
            // Invoke method with timeout
            auto future = std::async(std::launch::async, [&]() {
                method(*proxy_, callStatus, returnValue);
            });

            // Wait with timeout
            auto status = future.wait_for(timeout);
            if (status == std::future_status::timeout) {
                LAP_LOG_ERROR("[SomeIpMethodCaller] CallSync timeout after {}ms", 
                             timeout.count());
                return MakeErrorCode(ComErrc::Timeout);
            }

            // Check call status
            return ConvertCallStatus(callStatus, returnValue);

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("[SomeIpMethodCaller] CallSync exception: {}", e.what());
            return MakeErrorCode(ComErrc::InternalError);
        }
    }

    /**
     * @brief Synchronous method call (void return)
     * @tparam MethodFunc Method invocation functor type
     * @param method Lambda that calls the proxy method
     * @param timeout Maximum wait time for response
     * @return Result indicating success or error
     */
    template<typename MethodFunc>
    lap::core::Result<void> CallSyncVoid(
        MethodFunc method,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        
        if (!proxy_) {
            LAP_LOG_ERROR("[SomeIpMethodCaller] CallSyncVoid failed: proxy is null");
            return MakeErrorCode(ComErrc::NotInitialized);
        }

        try {
            CommonAPI::CallStatus callStatus;
            
            auto future = std::async(std::launch::async, [&]() {
                method(*proxy_, callStatus);
            });

            auto status = future.wait_for(timeout);
            if (status == std::future_status::timeout) {
                LAP_LOG_ERROR("[SomeIpMethodCaller] CallSyncVoid timeout after {}ms", 
                             timeout.count());
                return MakeErrorCode(ComErrc::Timeout);
            }

            return ConvertCallStatusVoid(callStatus);

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("[SomeIpMethodCaller] CallSyncVoid exception: {}", e.what());
            return MakeErrorCode(ComErrc::InternalError);
        }
    }

    /**
     * @brief Asynchronous method call with callback
     * @tparam ReturnType Expected return type
     * @tparam MethodFunc Async method invocation functor type
     * @tparam CallbackFunc User callback functor type
     * @param method Lambda that calls async proxy method
     * @param callback User callback for result
     * @return Result indicating request submission success
     * 
     * The method functor receives proxy reference and a CommonAPI callback.
     * User callback receives Result<ReturnType> when method completes.
     */
    template<typename ReturnType, typename MethodFunc, typename CallbackFunc>
    lap::core::Result<void> CallAsync(
        MethodFunc method,
        CallbackFunc callback) {
        
        if (!proxy_) {
            LAP_LOG_ERROR("[SomeIpMethodCaller] CallAsync failed: proxy is null");
            return MakeErrorCode(ComErrc::NotInitialized);
        }

        try {
            // Create CommonAPI callback wrapper
            auto commonApiCallback = [callback](
                const CommonAPI::CallStatus& callStatus,
                const ReturnType& returnValue) {
                
                auto result = ConvertCallStatus(callStatus, returnValue);
                callback(result);
            };

            // Invoke async method
            method(*proxy_, commonApiCallback);

            return lap::core::Result<void>();

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("[SomeIpMethodCaller] CallAsync exception: {}", e.what());
            return MakeErrorCode(ComErrc::InternalError);
        }
    }

    /**
     * @brief Asynchronous method call (void return)
     * @tparam MethodFunc Async method invocation functor type
     * @tparam CallbackFunc User callback functor type
     * @param method Lambda that calls async proxy method
     * @param callback User callback for completion
     * @return Result indicating request submission success
     */
    template<typename MethodFunc, typename CallbackFunc>
    lap::core::Result<void> CallAsyncVoid(
        MethodFunc method,
        CallbackFunc callback) {
        
        if (!proxy_) {
            LAP_LOG_ERROR("[SomeIpMethodCaller] CallAsyncVoid failed: proxy is null");
            return MakeErrorCode(ComErrc::NotInitialized);
        }

        try {
            auto commonApiCallback = [callback](const CommonAPI::CallStatus& callStatus) {
                auto result = ConvertCallStatusVoid(callStatus);
                callback(result);
            };

            method(*proxy_, commonApiCallback);

            return lap::core::Result<void>();

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("[SomeIpMethodCaller] CallAsyncVoid exception: {}", e.what());
            return MakeErrorCode(ComErrc::InternalError);
        }
    }

    /**
     * @brief Check if proxy is valid
     */
    bool IsValid() const {
        return proxy_ != nullptr;
    }

    /**
     * @brief Get underlying proxy
     */
    ProxyPtr GetProxy() const {
        return proxy_;
    }

private:
    /**
     * @brief Convert CommonAPI::CallStatus to Result<T>
     */
    template<typename T>
    static lap::core::Result<T> ConvertCallStatus(
        const CommonAPI::CallStatus& callStatus,
        const T& value) {
        
        switch (callStatus) {
            case CommonAPI::CallStatus::SUCCESS:
                return value;
            
            case CommonAPI::CallStatus::OUT_OF_MEMORY:
                LAP_LOG_ERROR("[SomeIpMethodCaller] Call failed: OUT_OF_MEMORY");
                return MakeErrorCode(ComErrc::OutOfMemory);
            
            case CommonAPI::CallStatus::NOT_AVAILABLE:
                LAP_LOG_ERROR("[SomeIpMethodCaller] Call failed: NOT_AVAILABLE");
                return MakeErrorCode(ComErrc::NotAvailable);
            
            case CommonAPI::CallStatus::CONNECTION_FAILED:
                LAP_LOG_ERROR("[SomeIpMethodCaller] Call failed: CONNECTION_FAILED");
                return MakeErrorCode(ComErrc::ConnectionFailed);
            
            case CommonAPI::CallStatus::REMOTE_ERROR:
                LAP_LOG_ERROR("[SomeIpMethodCaller] Call failed: REMOTE_ERROR");
                return MakeErrorCode(ComErrc::RemoteError);
            
            case CommonAPI::CallStatus::UNKNOWN:
            default:
                LAP_LOG_ERROR("[SomeIpMethodCaller] Call failed: UNKNOWN");
                return MakeErrorCode(ComErrc::InternalError);
        }
    }

    /**
     * @brief Convert CommonAPI::CallStatus to Result<void>
     */
    static lap::core::Result<void> ConvertCallStatusVoid(
        const CommonAPI::CallStatus& callStatus) {
        
        switch (callStatus) {
            case CommonAPI::CallStatus::SUCCESS:
                return lap::core::Result<void>();
            
            case CommonAPI::CallStatus::OUT_OF_MEMORY:
                LAP_LOG_ERROR("[SomeIpMethodCaller] Call failed: OUT_OF_MEMORY");
                return MakeErrorCode(ComErrc::OutOfMemory);
            
            case CommonAPI::CallStatus::NOT_AVAILABLE:
                LAP_LOG_ERROR("[SomeIpMethodCaller] Call failed: NOT_AVAILABLE");
                return MakeErrorCode(ComErrc::NotAvailable);
            
            case CommonAPI::CallStatus::CONNECTION_FAILED:
                LAP_LOG_ERROR("[SomeIpMethodCaller] Call failed: CONNECTION_FAILED");
                return MakeErrorCode(ComErrc::ConnectionFailed);
            
            case CommonAPI::CallStatus::REMOTE_ERROR:
                LAP_LOG_ERROR("[SomeIpMethodCaller] Call failed: REMOTE_ERROR");
                return MakeErrorCode(ComErrc::RemoteError);
            
            case CommonAPI::CallStatus::UNKNOWN:
            default:
                LAP_LOG_ERROR("[SomeIpMethodCaller] Call failed: UNKNOWN");
                return MakeErrorCode(ComErrc::InternalError);
        }
    }

    ProxyPtr proxy_;
};

/**
 * @brief Server-side SOME/IP method handler
 * @tparam StubType The CommonAPI stub type
 * 
 * Example:
 * @code
 * class MyServiceImpl : public MyServiceStubDefault {
 * public:
 *     void calculate(const std::shared_ptr<CommonAPI::ClientId> clientId,
 *                    int32_t a, int32_t b, std::string op,
 *                    calculateReply_t reply) override {
 *         
 *         SomeIpMethodResponder responder(reply);
 *         
 *         // Perform calculation
 *         int32_t result;
 *         if (op == "+") result = a + b;
 *         else if (op == "-") result = a - b;
 *         // ...
 *         
 *         // Send response
 *         responder.Reply(result);
 *     }
 * };
 * @endcode
 */
template<typename ReplyFunc>
class SomeIpMethodResponder {
public:
    explicit SomeIpMethodResponder(ReplyFunc reply)
        : reply_(reply), replied_(false) {}

    ~SomeIpMethodResponder() {
        if (!replied_) {
            LAP_LOG_WARN("[SomeIpMethodResponder] Destructor: reply not sent");
        }
    }

    /**
     * @brief Send successful response
     * @tparam Args Reply argument types
     * @param args Reply values
     */
    template<typename... Args>
    void Reply(Args&&... args) {
        if (replied_) {
            LAP_LOG_WARN("[SomeIpMethodResponder] Reply already sent");
            return;
        }
        
        try {
            reply_(std::forward<Args>(args)...);
            replied_ = true;
        } catch (const std::exception& e) {
            LAP_LOG_ERROR("[SomeIpMethodResponder] Reply exception: {}", e.what());
        }
    }

    /**
     * @brief Check if reply has been sent
     */
    bool HasReplied() const {
        return replied_;
    }

private:
    ReplyFunc reply_;
    bool replied_;
};

} // namespace someip
} // namespace com
} // namespace lap

#endif // LAP_COM_SOMEIP_METHOD_BINDING_HPP
