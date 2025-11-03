/**
 * @file SomeIpFieldBinding.hpp
 * @brief SOME/IP attribute (field) binding layer
 * @details Provides getter, setter, and notifier operations for SOME/IP attributes,
 *          with change notification subscription support.
 * 
 * @copyright Copyright (c) 2025 LightAP
 */

#ifndef LAP_COM_SOMEIP_FIELD_BINDING_HPP
#define LAP_COM_SOMEIP_FIELD_BINDING_HPP

#include <CommonAPI/CommonAPI.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <chrono>

#include "Core/CoreBase/inc/CTypedef.h"
#include "Core/CoreBase/inc/CResult.h"
#include "Core/CoreBase/inc/CLog.h"
#include "../ComTypes.hpp"

namespace lap {
namespace com {
namespace someip {

/**
 * @brief Client-side SOME/IP attribute accessor
 * @tparam ProxyType The CommonAPI proxy type
 * 
 * Example:
 * @code
 * SomeIpFieldAccessor<CalculatorProxy> accessor(proxy);
 * 
 * // Read attribute value
 * auto result = accessor.Get<int32_t>(
 *     [](auto& p) { return p.getLastResultAttribute(); },
 *     "lastResult"
 * );
 * 
 * // Write attribute value
 * accessor.Set<int32_t>(
 *     [](auto& p) { return p.getLastResultAttribute(); },
 *     42,
 *     "lastResult"
 * );
 * 
 * // Subscribe to changes
 * accessor.SubscribeChanges<int32_t>(
 *     [](auto& p) { return p.getLastResultAttribute(); },
 *     [](int32_t newValue) {
 *         LAP_LOG_INFO("Value changed to: {}", newValue);
 *     },
 *     "lastResult"
 * );
 * @endcode
 */
template<typename ProxyType>
class SomeIpFieldAccessor {
public:
    using ProxyPtr = std::shared_ptr<ProxyType>;

    explicit SomeIpFieldAccessor(ProxyPtr proxy)
        : proxy_(proxy) {
        if (!proxy_) {
            LAP_LOG_ERROR("[SomeIpFieldAccessor] Proxy is nullptr");
        }
    }

    ~SomeIpFieldAccessor() {
        UnsubscribeAll();
    }

    /**
     * @brief Get attribute value (synchronous)
     * @tparam ValueType Attribute value type
     * @tparam AttributeGetter Functor to get attribute from proxy
     * @param attributeGetter Lambda that returns attribute from proxy
     * @param attributeName Attribute identifier for logging
     * @param timeout Maximum wait time
     * @return Result containing attribute value or error
     */
    template<typename ValueType, typename AttributeGetter>
    lap::core::Result<ValueType> Get(
        AttributeGetter attributeGetter,
        const std::string& attributeName,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        
        if (!proxy_) {
            LAP_LOG_ERROR("[SomeIpFieldAccessor] Get failed: proxy is null");
            return MakeErrorCode(ComErrc::NotInitialized);
        }

        try {
            // Get attribute from proxy
            auto& attribute = attributeGetter(*proxy_);

            // Get value synchronously
            CommonAPI::CallStatus callStatus;
            ValueType value;
            attribute.getValue(callStatus, value);

            // Convert status
            if (callStatus != CommonAPI::CallStatus::SUCCESS) {
                LAP_LOG_ERROR("[SomeIpFieldAccessor] Get {} failed with status: {}", 
                             attributeName, static_cast<int>(callStatus));
                return ConvertCallStatusForGet<ValueType>(callStatus);
            }

            LAP_LOG_DEBUG("[SomeIpFieldAccessor] Get {} succeeded", attributeName);
            return value;

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("[SomeIpFieldAccessor] Get {} exception: {}", 
                         attributeName, e.what());
            return MakeErrorCode(ComErrc::InternalError);
        }
    }

    /**
     * @brief Set attribute value (synchronous)
     * @tparam ValueType Attribute value type
     * @tparam AttributeGetter Functor to get attribute from proxy
     * @param attributeGetter Lambda that returns attribute from proxy
     * @param value New attribute value
     * @param attributeName Attribute identifier for logging
     * @param timeout Maximum wait time
     * @return Result indicating success or error
     */
    template<typename ValueType, typename AttributeGetter>
    lap::core::Result<void> Set(
        AttributeGetter attributeGetter,
        const ValueType& value,
        const std::string& attributeName,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        
        if (!proxy_) {
            LAP_LOG_ERROR("[SomeIpFieldAccessor] Set failed: proxy is null");
            return MakeErrorCode(ComErrc::NotInitialized);
        }

        try {
            auto& attribute = attributeGetter(*proxy_);

            // Set value synchronously
            CommonAPI::CallStatus callStatus;
            attribute.setValue(value, callStatus);

            if (callStatus != CommonAPI::CallStatus::SUCCESS) {
                LAP_LOG_ERROR("[SomeIpFieldAccessor] Set {} failed with status: {}", 
                             attributeName, static_cast<int>(callStatus));
                return ConvertCallStatusForSet(callStatus);
            }

            LAP_LOG_DEBUG("[SomeIpFieldAccessor] Set {} succeeded", attributeName);
            return lap::core::Result<void>();

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("[SomeIpFieldAccessor] Set {} exception: {}", 
                         attributeName, e.what());
            return MakeErrorCode(ComErrc::InternalError);
        }
    }

    /**
     * @brief Get attribute value asynchronously
     * @tparam ValueType Attribute value type
     * @tparam AttributeGetter Functor to get attribute from proxy
     * @tparam CallbackFunc User callback type
     * @param attributeGetter Lambda that returns attribute from proxy
     * @param callback User callback for result
     * @param attributeName Attribute identifier for logging
     * @return Result indicating request submission success
     */
    template<typename ValueType, typename AttributeGetter, typename CallbackFunc>
    lap::core::Result<void> GetAsync(
        AttributeGetter attributeGetter,
        CallbackFunc callback,
        const std::string& attributeName) {
        
        if (!proxy_) {
            LAP_LOG_ERROR("[SomeIpFieldAccessor] GetAsync failed: proxy is null");
            return MakeErrorCode(ComErrc::NotInitialized);
        }

        try {
            auto& attribute = attributeGetter(*proxy_);

            // Get value asynchronously
            attribute.getValueAsync([callback, attributeName](
                const CommonAPI::CallStatus& callStatus,
                const ValueType& value) {
                
                lap::core::Result<ValueType> result;
                if (callStatus == CommonAPI::CallStatus::SUCCESS) {
                    result = value;
                    LAP_LOG_DEBUG("[SomeIpFieldAccessor] GetAsync {} succeeded", 
                                 attributeName);
                } else {
                    LAP_LOG_ERROR("[SomeIpFieldAccessor] GetAsync {} failed with status: {}", 
                                 attributeName, static_cast<int>(callStatus));
                    result = ConvertCallStatusForGet<ValueType>(callStatus);
                }

                callback(result);
            });

            return lap::core::Result<void>();

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("[SomeIpFieldAccessor] GetAsync {} exception: {}", 
                         attributeName, e.what());
            return MakeErrorCode(ComErrc::InternalError);
        }
    }

    /**
     * @brief Set attribute value asynchronously
     * @tparam ValueType Attribute value type
     * @tparam AttributeGetter Functor to get attribute from proxy
     * @tparam CallbackFunc User callback type
     * @param attributeGetter Lambda that returns attribute from proxy
     * @param value New attribute value
     * @param callback User callback for completion
     * @param attributeName Attribute identifier for logging
     * @return Result indicating request submission success
     */
    template<typename ValueType, typename AttributeGetter, typename CallbackFunc>
    lap::core::Result<void> SetAsync(
        AttributeGetter attributeGetter,
        const ValueType& value,
        CallbackFunc callback,
        const std::string& attributeName) {
        
        if (!proxy_) {
            LAP_LOG_ERROR("[SomeIpFieldAccessor] SetAsync failed: proxy is null");
            return MakeErrorCode(ComErrc::NotInitialized);
        }

        try {
            auto& attribute = attributeGetter(*proxy_);

            // Set value asynchronously
            attribute.setValueAsync(value, [callback, attributeName](
                const CommonAPI::CallStatus& callStatus) {
                
                lap::core::Result<void> result;
                if (callStatus == CommonAPI::CallStatus::SUCCESS) {
                    result = lap::core::Result<void>();
                    LAP_LOG_DEBUG("[SomeIpFieldAccessor] SetAsync {} succeeded", 
                                 attributeName);
                } else {
                    LAP_LOG_ERROR("[SomeIpFieldAccessor] SetAsync {} failed with status: {}", 
                                 attributeName, static_cast<int>(callStatus));
                    result = ConvertCallStatusForSet(callStatus);
                }

                callback(result);
            });

            return lap::core::Result<void>();

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("[SomeIpFieldAccessor] SetAsync {} exception: {}", 
                         attributeName, e.what());
            return MakeErrorCode(ComErrc::InternalError);
        }
    }

    /**
     * @brief Subscribe to attribute change notifications
     * @tparam ValueType Attribute value type
     * @tparam AttributeGetter Functor to get attribute from proxy
     * @tparam CallbackFunc User callback type
     * @param attributeGetter Lambda that returns attribute from proxy
     * @param callback User callback for change notifications
     * @param attributeName Unique attribute identifier
     * @return Result indicating subscription success
     */
    template<typename ValueType, typename AttributeGetter, typename CallbackFunc>
    lap::core::Result<void> SubscribeChanges(
        AttributeGetter attributeGetter,
        CallbackFunc callback,
        const std::string& attributeName) {
        
        if (!proxy_) {
            LAP_LOG_ERROR("[SomeIpFieldAccessor] SubscribeChanges failed: proxy is null");
            return MakeErrorCode(ComErrc::NotInitialized);
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Check if already subscribed
        if (subscriptions_.find(attributeName) != subscriptions_.end()) {
            LAP_LOG_WARN("[SomeIpFieldAccessor] Already subscribed to attribute: {}", 
                        attributeName);
            return MakeErrorCode(ComErrc::AlreadyExists);
        }

        try {
            auto& attribute = attributeGetter(*proxy_);

            // Subscribe to change notifications
            auto subscription = attribute.getChangedEvent().subscribe(
                [callback, attributeName](const ValueType& value) {
                    try {
                        LAP_LOG_DEBUG("[SomeIpFieldAccessor] Attribute {} changed", 
                                     attributeName);
                        callback(value);
                    } catch (const std::exception& e) {
                        LAP_LOG_ERROR("[SomeIpFieldAccessor] Change callback exception for {}: {}", 
                                     attributeName, e.what());
                    }
                }
            );

            // Store subscription token
            subscriptions_[attributeName] = std::make_shared<SubscriptionToken>(
                [sub = subscription, &attribute]() mutable {
                    attribute.getChangedEvent().unsubscribe(sub);
                }
            );

            LAP_LOG_INFO("[SomeIpFieldAccessor] Subscribed to attribute changes: {}", 
                        attributeName);
            return lap::core::Result<void>();

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("[SomeIpFieldAccessor] SubscribeChanges exception for {}: {}", 
                         attributeName, e.what());
            return MakeErrorCode(ComErrc::InternalError);
        }
    }

    /**
     * @brief Unsubscribe from attribute changes
     * @param attributeName Attribute identifier used in SubscribeChanges
     * @return Result indicating unsubscription success
     */
    lap::core::Result<void> UnsubscribeChanges(const std::string& attributeName) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = subscriptions_.find(attributeName);
        if (it == subscriptions_.end()) {
            LAP_LOG_WARN("[SomeIpFieldAccessor] Attribute not subscribed: {}", 
                        attributeName);
            return MakeErrorCode(ComErrc::NotFound);
        }

        try {
            it->second->Unsubscribe();
            subscriptions_.erase(it);

            LAP_LOG_INFO("[SomeIpFieldAccessor] Unsubscribed from attribute: {}", 
                        attributeName);
            return lap::core::Result<void>();

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("[SomeIpFieldAccessor] UnsubscribeChanges exception for {}: {}", 
                         attributeName, e.what());
            return MakeErrorCode(ComErrc::InternalError);
        }
    }

    /**
     * @brief Unsubscribe from all attribute changes
     */
    void UnsubscribeAll() {
        std::lock_guard<std::mutex> lock(mutex_);

        for (auto& [name, token] : subscriptions_) {
            try {
                token->Unsubscribe();
                LAP_LOG_INFO("[SomeIpFieldAccessor] Unsubscribed from attribute: {}", name);
            } catch (const std::exception& e) {
                LAP_LOG_ERROR("[SomeIpFieldAccessor] UnsubscribeAll exception for {}: {}", 
                             name, e.what());
            }
        }

        subscriptions_.clear();
    }

    /**
     * @brief Check if subscribed to attribute changes
     */
    bool IsSubscribed(const std::string& attributeName) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return subscriptions_.find(attributeName) != subscriptions_.end();
    }

private:
    /**
     * @brief Subscription token for cleanup
     */
    struct SubscriptionToken {
        using UnsubscribeFunc = std::function<void()>;

        explicit SubscriptionToken(UnsubscribeFunc func)
            : unsubscribe_(func), unsubscribed_(false) {}

        ~SubscriptionToken() {
            if (!unsubscribed_) {
                Unsubscribe();
            }
        }

        void Unsubscribe() {
            if (!unsubscribed_ && unsubscribe_) {
                unsubscribe_();
                unsubscribed_ = true;
            }
        }

        UnsubscribeFunc unsubscribe_;
        bool unsubscribed_;
    };

    /**
     * @brief Convert CallStatus for Get operations
     */
    template<typename T>
    static lap::core::Result<T> ConvertCallStatusForGet(
        const CommonAPI::CallStatus& callStatus) {
        
        switch (callStatus) {
            case CommonAPI::CallStatus::OUT_OF_MEMORY:
                return MakeErrorCode(ComErrc::OutOfMemory);
            case CommonAPI::CallStatus::NOT_AVAILABLE:
                return MakeErrorCode(ComErrc::NotAvailable);
            case CommonAPI::CallStatus::CONNECTION_FAILED:
                return MakeErrorCode(ComErrc::ConnectionFailed);
            case CommonAPI::CallStatus::REMOTE_ERROR:
                return MakeErrorCode(ComErrc::RemoteError);
            default:
                return MakeErrorCode(ComErrc::InternalError);
        }
    }

    /**
     * @brief Convert CallStatus for Set operations
     */
    static lap::core::Result<void> ConvertCallStatusForSet(
        const CommonAPI::CallStatus& callStatus) {
        
        switch (callStatus) {
            case CommonAPI::CallStatus::OUT_OF_MEMORY:
                return MakeErrorCode(ComErrc::OutOfMemory);
            case CommonAPI::CallStatus::NOT_AVAILABLE:
                return MakeErrorCode(ComErrc::NotAvailable);
            case CommonAPI::CallStatus::CONNECTION_FAILED:
                return MakeErrorCode(ComErrc::ConnectionFailed);
            case CommonAPI::CallStatus::REMOTE_ERROR:
                return MakeErrorCode(ComErrc::RemoteError);
            default:
                return MakeErrorCode(ComErrc::InternalError);
        }
    }

    ProxyPtr proxy_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<SubscriptionToken>> subscriptions_;
};

/**
 * @brief Server-side SOME/IP attribute manager
 * @tparam StubType The CommonAPI stub type
 * 
 * Stub implementations typically handle attributes automatically.
 * This class provides utilities for attribute change notifications.
 * 
 * Example:
 * @code
 * class MyServiceImpl : public MyServiceStubDefault {
 * private:
 *     SomeIpFieldNotifier<MyServiceStub> notifier_;
 * 
 * public:
 *     MyServiceImpl() : notifier_(this) {}
 * 
 *     void updateLastResult(int32_t newValue) {
 *         // Update internal value
 *         lastResult_ = newValue;
 *         
 *         // Notify subscribers
 *         notifier_.NotifyChange(
 *             [](auto& stub, int32_t val) { 
 *                 stub.setLastResultAttribute(val); 
 *             },
 *             newValue,
 *             "lastResult"
 *         );
 *     }
 * };
 * @endcode
 */
template<typename StubType>
class SomeIpFieldNotifier {
public:
    using StubPtr = StubType*;

    explicit SomeIpFieldNotifier(StubPtr stub)
        : stub_(stub) {
        if (!stub_) {
            LAP_LOG_ERROR("[SomeIpFieldNotifier] Stub is nullptr");
        }
    }

    /**
     * @brief Notify attribute change to subscribers
     * @tparam ValueType Attribute value type
     * @tparam SetterFunc Functor to set attribute on stub
     * @param setter Lambda that sets attribute on stub
     * @param value New attribute value
     * @param attributeName Attribute identifier for logging
     * @return Result indicating notification success
     */
    template<typename ValueType, typename SetterFunc>
    lap::core::Result<void> NotifyChange(
        SetterFunc setter,
        const ValueType& value,
        const std::string& attributeName) {
        
        if (!stub_) {
            LAP_LOG_ERROR("[SomeIpFieldNotifier] NotifyChange failed: stub is null");
            return MakeErrorCode(ComErrc::NotInitialized);
        }

        try {
            // Update attribute value (triggers notification)
            setter(*stub_, value);

            LAP_LOG_DEBUG("[SomeIpFieldNotifier] Notified change for attribute: {}", 
                         attributeName);
            return lap::core::Result<void>();

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("[SomeIpFieldNotifier] NotifyChange exception for {}: {}", 
                         attributeName, e.what());
            return MakeErrorCode(ComErrc::InternalError);
        }
    }

private:
    StubPtr stub_;
};

} // namespace someip
} // namespace com
} // namespace lap

#endif // LAP_COM_SOMEIP_FIELD_BINDING_HPP
