/**
 * @file SomeIpEventBinding.hpp
 * @brief SOME/IP event subscription and broadcast binding layer
 * @details Provides event subscription management, event group handling,
 *          and callback registration for SOME/IP broadcasts.
 * 
 * @copyright Copyright (c) 2025 LightAP
 */

#ifndef LAP_COM_SOMEIP_EVENT_BINDING_HPP
#define LAP_COM_SOMEIP_EVENT_BINDING_HPP

#include <CommonAPI/CommonAPI.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "Core/CoreBase/inc/CTypedef.h"
#include "Core/CoreBase/inc/CResult.h"
#include "Core/CoreBase/inc/CLog.h"
#include "../ComTypes.hpp"

namespace lap {
namespace com {
namespace someip {

/**
 * @brief Client-side SOME/IP event subscriber
 * @tparam ProxyType The CommonAPI proxy type
 * 
 * Example:
 * @code
 * SomeIpEventSubscriber<CalculatorProxy> subscriber(proxy);
 * 
 * // Subscribe to event
 * auto result = subscriber.Subscribe<int32_t>(
 *     [](auto& p) { return p.getResultReadyEvent(); },
 *     [](int32_t value) {
 *         LAP_LOG_INFO("Result ready: {}", value);
 *     },
 *     "resultReady"
 * );
 * 
 * // Unsubscribe
 * subscriber.Unsubscribe("resultReady");
 * @endcode
 */
template<typename ProxyType>
class SomeIpEventSubscriber {
public:
    using ProxyPtr = std::shared_ptr<ProxyType>;

    explicit SomeIpEventSubscriber(ProxyPtr proxy)
        : proxy_(proxy) {
        if (!proxy_) {
            LAP_LOG_ERROR("[SomeIpEventSubscriber] Proxy is nullptr");
        }
    }

    ~SomeIpEventSubscriber() {
        UnsubscribeAll();
    }

    /**
     * @brief Subscribe to an event
     * @tparam EventType Event parameter type
     * @tparam EventGetter Functor to get event from proxy
     * @tparam CallbackFunc User callback type
     * @param eventGetter Lambda that returns event from proxy
     * @param callback User callback for event notifications
     * @param eventName Unique event identifier
     * @return Result indicating subscription success
     */
    template<typename EventType, typename EventGetter, typename CallbackFunc>
    lap::core::Result<void> Subscribe(
        EventGetter eventGetter,
        CallbackFunc callback,
        const std::string& eventName) {
        
        if (!proxy_) {
            LAP_LOG_ERROR("[SomeIpEventSubscriber] Subscribe failed: proxy is null");
            return MakeErrorCode(ComErrc::NotInitialized);
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Check if already subscribed
        if (subscriptions_.find(eventName) != subscriptions_.end()) {
            LAP_LOG_WARN("[SomeIpEventSubscriber] Already subscribed to event: {}", 
                        eventName);
            return MakeErrorCode(ComErrc::AlreadyExists);
        }

        try {
            // Get event from proxy
            auto& event = eventGetter(*proxy_);

            // Subscribe with CommonAPI
            auto subscription = event.subscribe([callback](const EventType& value) {
                try {
                    callback(value);
                } catch (const std::exception& e) {
                    LAP_LOG_ERROR("[SomeIpEventSubscriber] Event callback exception: {}", 
                                 e.what());
                }
            });

            // Store subscription token
            subscriptions_[eventName] = std::make_shared<SubscriptionToken>(
                [sub = subscription, &event]() mutable {
                    event.unsubscribe(sub);
                }
            );

            LAP_LOG_INFO("[SomeIpEventSubscriber] Subscribed to event: {}", eventName);
            return lap::core::Result<void>();

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("[SomeIpEventSubscriber] Subscribe exception: {}", e.what());
            return MakeErrorCode(ComErrc::InternalError);
        }
    }

    /**
     * @brief Unsubscribe from an event
     * @param eventName Event identifier used in Subscribe
     * @return Result indicating unsubscription success
     */
    lap::core::Result<void> Unsubscribe(const std::string& eventName) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = subscriptions_.find(eventName);
        if (it == subscriptions_.end()) {
            LAP_LOG_WARN("[SomeIpEventSubscriber] Event not subscribed: {}", eventName);
            return MakeErrorCode(ComErrc::NotFound);
        }

        try {
            // Unsubscribe through token
            it->second->Unsubscribe();
            subscriptions_.erase(it);

            LAP_LOG_INFO("[SomeIpEventSubscriber] Unsubscribed from event: {}", eventName);
            return lap::core::Result<void>();

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("[SomeIpEventSubscriber] Unsubscribe exception: {}", e.what());
            return MakeErrorCode(ComErrc::InternalError);
        }
    }

    /**
     * @brief Unsubscribe from all events
     */
    void UnsubscribeAll() {
        std::lock_guard<std::mutex> lock(mutex_);

        for (auto& [name, token] : subscriptions_) {
            try {
                token->Unsubscribe();
                LAP_LOG_INFO("[SomeIpEventSubscriber] Unsubscribed from event: {}", name);
            } catch (const std::exception& e) {
                LAP_LOG_ERROR("[SomeIpEventSubscriber] UnsubscribeAll exception for {}: {}", 
                             name, e.what());
            }
        }

        subscriptions_.clear();
    }

    /**
     * @brief Check if subscribed to an event
     */
    bool IsSubscribed(const std::string& eventName) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return subscriptions_.find(eventName) != subscriptions_.end();
    }

    /**
     * @brief Get number of active subscriptions
     */
    size_t GetSubscriptionCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return subscriptions_.size();
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

    ProxyPtr proxy_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<SubscriptionToken>> subscriptions_;
};

/**
 * @brief Server-side SOME/IP event broadcaster
 * @tparam StubType The CommonAPI stub type
 * 
 * Example:
 * @code
 * class MyServiceImpl : public MyServiceStubDefault {
 * private:
 *     SomeIpEventBroadcaster<MyServiceStub> broadcaster_;
 * 
 * public:
 *     MyServiceImpl() : broadcaster_(this) {}
 * 
 *     void notifyResult(int32_t result) {
 *         // Fire event to all subscribers
 *         broadcaster_.Fire<int32_t>(
 *             [](auto& stub) { return stub.fireResultReadyEvent; },
 *             result,
 *             "resultReady"
 *         );
 *     }
 * };
 * @endcode
 */
template<typename StubType>
class SomeIpEventBroadcaster {
public:
    using StubPtr = StubType*;

    explicit SomeIpEventBroadcaster(StubPtr stub)
        : stub_(stub) {
        if (!stub_) {
            LAP_LOG_ERROR("[SomeIpEventBroadcaster] Stub is nullptr");
        }
    }

    /**
     * @brief Fire an event to all subscribers
     * @tparam EventType Event parameter type
     * @tparam EventGetter Functor to get fire function from stub
     * @param eventGetter Lambda that returns fire function
     * @param value Event value to broadcast
     * @param eventName Event identifier for logging
     * @return Result indicating fire success
     */
    template<typename EventType, typename EventGetter>
    lap::core::Result<void> Fire(
        EventGetter eventGetter,
        const EventType& value,
        const std::string& eventName) {
        
        if (!stub_) {
            LAP_LOG_ERROR("[SomeIpEventBroadcaster] Fire failed: stub is null");
            return MakeErrorCode(ComErrc::NotInitialized);
        }

        try {
            // Get fire function from stub
            auto fireFunc = eventGetter(*stub_);
            
            // Fire event
            fireFunc(value);

            LAP_LOG_DEBUG("[SomeIpEventBroadcaster] Fired event: {}", eventName);
            return lap::core::Result<void>();

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("[SomeIpEventBroadcaster] Fire exception for {}: {}", 
                         eventName, e.what());
            return MakeErrorCode(ComErrc::InternalError);
        }
    }

    /**
     * @brief Fire event with multiple parameters
     * @tparam Args Event parameter types
     * @tparam EventGetter Functor to get fire function from stub
     * @param eventGetter Lambda that returns fire function
     * @param eventName Event identifier for logging
     * @param args Event values to broadcast
     * @return Result indicating fire success
     */
    template<typename EventGetter, typename... Args>
    lap::core::Result<void> FireMulti(
        EventGetter eventGetter,
        const std::string& eventName,
        Args&&... args) {
        
        if (!stub_) {
            LAP_LOG_ERROR("[SomeIpEventBroadcaster] FireMulti failed: stub is null");
            return MakeErrorCode(ComErrc::NotInitialized);
        }

        try {
            auto fireFunc = eventGetter(*stub_);
            fireFunc(std::forward<Args>(args)...);

            LAP_LOG_DEBUG("[SomeIpEventBroadcaster] Fired multi-param event: {}", 
                         eventName);
            return lap::core::Result<void>();

        } catch (const std::exception& e) {
            LAP_LOG_ERROR("[SomeIpEventBroadcaster] FireMulti exception for {}: {}", 
                         eventName, e.what());
            return MakeErrorCode(ComErrc::InternalError);
        }
    }

    /**
     * @brief Check if stub is valid
     */
    bool IsValid() const {
        return stub_ != nullptr;
    }

private:
    StubPtr stub_;
};

/**
 * @brief Selective event filter for conditional subscriptions
 * 
 * Example:
 * @code
 * SomeIpEventFilter<int> filter;
 * 
 * // Only notify if value > 100
 * filter.SetFilter([](int value) { return value > 100; });
 * 
 * subscriber.Subscribe<int>(
 *     eventGetter,
 *     [&filter](int value) {
 *         if (filter.ShouldNotify(value)) {
 *             LAP_LOG_INFO("High value: {}", value);
 *         }
 *     },
 *     "filteredEvent"
 * );
 * @endcode
 */
template<typename EventType>
class SomeIpEventFilter {
public:
    using FilterFunc = std::function<bool(const EventType&)>;

    SomeIpEventFilter() : filter_(nullptr) {}

    explicit SomeIpEventFilter(FilterFunc filter) 
        : filter_(filter) {}

    /**
     * @brief Set filter function
     */
    void SetFilter(FilterFunc filter) {
        std::lock_guard<std::mutex> lock(mutex_);
        filter_ = filter;
    }

    /**
     * @brief Clear filter (accept all)
     */
    void ClearFilter() {
        std::lock_guard<std::mutex> lock(mutex_);
        filter_ = nullptr;
    }

    /**
     * @brief Check if event should be notified
     */
    bool ShouldNotify(const EventType& value) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!filter_) {
            return true; // No filter, accept all
        }

        try {
            return filter_(value);
        } catch (const std::exception& e) {
            LAP_LOG_ERROR("[SomeIpEventFilter] Filter exception: {}", e.what());
            return false;
        }
    }

private:
    mutable std::mutex mutex_;
    FilterFunc filter_;
};

} // namespace someip
} // namespace com
} // namespace lap

#endif // LAP_COM_SOMEIP_EVENT_BINDING_HPP
