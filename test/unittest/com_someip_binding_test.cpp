/**
 * @file com_someip_binding_test.cpp
 * @brief Unit tests for SOME/IP method/event/field bindings
 * @copyright Copyright (c) 2025 LightAP
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "../../source/binding/someip/SomeIpMethodBinding.hpp"
#include "../../source/binding/someip/SomeIpEventBinding.hpp"
#include "../../source/binding/someip/SomeIpFieldBinding.hpp"
#include "Core/CoreBase/inc/CMemory.h"
#include "Core/CoreBase/inc/CLog.h"

using namespace lap::com::someip;
using namespace lap::core;
using namespace lap::log;

/**
 * @brief Mock proxy for testing
 */
class MockProxy {
public:
    MOCK_METHOD(void, synchronousMethod, (CommonAPI::CallStatus&, int&), ());
    MOCK_METHOD(void, asynchronousMethod, (std::function<void(CommonAPI::CallStatus, int)>), ());
};

/**
 * @brief Test fixture for SOME/IP binding tests
 */
class SomeIpBindingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize LightAP core
        MemManager::getInstance();
        LogManager::getInstance().initialize();
        
        mockProxy_ = std::make_shared<MockProxy>();
    }

    void TearDown() override {
        mockProxy_.reset();
    }

    std::shared_ptr<MockProxy> mockProxy_;
};

// ============================================================================
// SomeIpMethodCaller Tests
// ============================================================================

/**
 * @brief Test method caller with null proxy
 */
TEST_F(SomeIpBindingTest, MethodCallerNullProxy) {
    std::shared_ptr<MockProxy> nullProxy = nullptr;
    SomeIpMethodCaller<MockProxy> caller(nullProxy);
    
    EXPECT_FALSE(caller.IsValid());
    EXPECT_EQ(caller.GetProxy(), nullptr);
}

/**
 * @brief Test method caller with valid proxy
 */
TEST_F(SomeIpBindingTest, MethodCallerValidProxy) {
    SomeIpMethodCaller<MockProxy> caller(mockProxy_);
    
    EXPECT_TRUE(caller.IsValid());
    EXPECT_EQ(caller.GetProxy(), mockProxy_);
}

/**
 * @brief Test synchronous call success
 */
TEST_F(SomeIpBindingTest, MethodCallerSyncSuccess) {
    SomeIpMethodCaller<MockProxy> caller(mockProxy_);
    
    // Mock successful call
    EXPECT_CALL(*mockProxy_, synchronousMethod(::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgReferee<0>(CommonAPI::CallStatus::SUCCESS),
            ::testing::SetArgReferee<1>(42)
        ));
    
    auto result = caller.CallSync<int>(
        [](auto& proxy, CommonAPI::CallStatus& status, int& value) {
            proxy.synchronousMethod(status, value);
        },
        std::chrono::milliseconds(1000)
    );
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_EQ(result.Value(), 42);
}

/**
 * @brief Test synchronous call with timeout
 */
TEST_F(SomeIpBindingTest, MethodCallerSyncTimeout) {
    SomeIpMethodCaller<MockProxy> caller(mockProxy_);
    
    // Mock slow call
    EXPECT_CALL(*mockProxy_, synchronousMethod(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeWithoutArgs([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }));
    
    auto result = caller.CallSync<int>(
        [](auto& proxy, CommonAPI::CallStatus& status, int& value) {
            proxy.synchronousMethod(status, value);
        },
        std::chrono::milliseconds(50) // Short timeout
    );
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.Error().Value(), static_cast<int>(lap::com::ComErrc::Timeout));
}

/**
 * @brief Test synchronous call with various error statuses
 */
TEST_F(SomeIpBindingTest, MethodCallerSyncErrors) {
    SomeIpMethodCaller<MockProxy> caller(mockProxy_);
    
    // Test NOT_AVAILABLE
    {
        EXPECT_CALL(*mockProxy_, synchronousMethod(::testing::_, ::testing::_))
            .WillOnce(::testing::SetArgReferee<0>(CommonAPI::CallStatus::NOT_AVAILABLE));
        
        auto result = caller.CallSync<int>(
            [](auto& proxy, CommonAPI::CallStatus& status, int& value) {
                proxy.synchronousMethod(status, value);
            },
            std::chrono::milliseconds(1000)
        );
        
        EXPECT_FALSE(result.IsOk());
        EXPECT_EQ(result.Error().Value(), static_cast<int>(lap::com::ComErrc::NotAvailable));
    }
    
    // Test OUT_OF_MEMORY
    {
        EXPECT_CALL(*mockProxy_, synchronousMethod(::testing::_, ::testing::_))
            .WillOnce(::testing::SetArgReferee<0>(CommonAPI::CallStatus::OUT_OF_MEMORY));
        
        auto result = caller.CallSync<int>(
            [](auto& proxy, CommonAPI::CallStatus& status, int& value) {
                proxy.synchronousMethod(status, value);
            },
            std::chrono::milliseconds(1000)
        );
        
        EXPECT_FALSE(result.IsOk());
        EXPECT_EQ(result.Error().Value(), static_cast<int>(lap::com::ComErrc::OutOfMemory));
    }
    
    // Test REMOTE_ERROR
    {
        EXPECT_CALL(*mockProxy_, synchronousMethod(::testing::_, ::testing::_))
            .WillOnce(::testing::SetArgReferee<0>(CommonAPI::CallStatus::REMOTE_ERROR));
        
        auto result = caller.CallSync<int>(
            [](auto& proxy, CommonAPI::CallStatus& status, int& value) {
                proxy.synchronousMethod(status, value);
            },
            std::chrono::milliseconds(1000)
        );
        
        EXPECT_FALSE(result.IsOk());
        EXPECT_EQ(result.Error().Value(), static_cast<int>(lap::com::ComErrc::RemoteError));
    }
}

/**
 * @brief Test asynchronous call
 */
TEST_F(SomeIpBindingTest, MethodCallerAsyncSuccess) {
    SomeIpMethodCaller<MockProxy> caller(mockProxy_);
    
    // Mock async call
    EXPECT_CALL(*mockProxy_, asynchronousMethod(::testing::_))
        .WillOnce(::testing::Invoke([](std::function<void(CommonAPI::CallStatus, int)> callback) {
            // Simulate async completion
            std::thread([callback]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                callback(CommonAPI::CallStatus::SUCCESS, 99);
            }).detach();
        }));
    
    bool callbackInvoked = false;
    int receivedValue = 0;
    
    auto result = caller.CallAsync<int>(
        [](auto& proxy, auto callback) {
            proxy.asynchronousMethod(callback);
        },
        [&callbackInvoked, &receivedValue](lap::core::Result<int> result) {
            callbackInvoked = true;
            if (result.IsOk()) {
                receivedValue = result.Value();
            }
        }
    );
    
    // Submission should succeed
    EXPECT_TRUE(result.IsOk());
    
    // Wait for callback
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_TRUE(callbackInvoked);
    EXPECT_EQ(receivedValue, 99);
}

// ============================================================================
// SomeIpMethodResponder Tests
// ============================================================================

/**
 * @brief Test method responder reply
 */
TEST_F(SomeIpBindingTest, MethodResponderReply) {
    bool replyCalled = false;
    int receivedValue = 0;
    
    auto replyFunc = [&replyCalled, &receivedValue](int value) {
        replyCalled = true;
        receivedValue = value;
    };
    
    SomeIpMethodResponder<decltype(replyFunc)> responder(replyFunc);
    
    EXPECT_FALSE(responder.HasReplied());
    
    responder.Reply(42);
    
    EXPECT_TRUE(responder.HasReplied());
    EXPECT_TRUE(replyCalled);
    EXPECT_EQ(receivedValue, 42);
}

/**
 * @brief Test method responder double reply
 */
TEST_F(SomeIpBindingTest, MethodResponderDoubleReply) {
    int replyCount = 0;
    
    auto replyFunc = [&replyCount](int value) {
        replyCount++;
    };
    
    SomeIpMethodResponder<decltype(replyFunc)> responder(replyFunc);
    
    responder.Reply(10);
    responder.Reply(20); // Should be ignored
    
    EXPECT_EQ(replyCount, 1); // Only first reply should be processed
}

// ============================================================================
// SomeIpEventSubscriber Tests
// ============================================================================

/**
 * @brief Mock event for testing
 */
class MockEvent {
public:
    using SubscriptionToken = int;
    using Callback = std::function<void(int)>;
    
    SubscriptionToken subscribe(Callback callback) {
        callbacks_.push_back(callback);
        return callbacks_.size() - 1;
    }
    
    void unsubscribe(SubscriptionToken token) {
        if (token >= 0 && token < static_cast<int>(callbacks_.size())) {
            callbacks_[token] = nullptr;
        }
    }
    
    void fire(int value) {
        for (auto& cb : callbacks_) {
            if (cb) cb(value);
        }
    }
    
private:
    std::vector<Callback> callbacks_;
};

/**
 * @brief Mock proxy with event for testing
 */
class MockProxyWithEvent {
public:
    MockEvent& getTestEvent() { return event_; }
    
private:
    MockEvent event_;
};

/**
 * @brief Test event subscription
 */
TEST_F(SomeIpBindingTest, EventSubscriberSubscribe) {
    auto proxy = std::make_shared<MockProxyWithEvent>();
    SomeIpEventSubscriber<MockProxyWithEvent> subscriber(proxy);
    
    int receivedValue = 0;
    int callCount = 0;
    
    auto result = subscriber.Subscribe<int>(
        [](auto& p) -> MockEvent& { return p.getTestEvent(); },
        [&receivedValue, &callCount](int value) {
            receivedValue = value;
            callCount++;
        },
        "testEvent"
    );
    
    ASSERT_TRUE(result.IsOk());
    EXPECT_TRUE(subscriber.IsSubscribed("testEvent"));
    EXPECT_EQ(subscriber.GetSubscriptionCount(), 1);
    
    // Fire event
    proxy->getTestEvent().fire(123);
    
    EXPECT_EQ(receivedValue, 123);
    EXPECT_EQ(callCount, 1);
}

/**
 * @brief Test event unsubscription
 */
TEST_F(SomeIpBindingTest, EventSubscriberUnsubscribe) {
    auto proxy = std::make_shared<MockProxyWithEvent>();
    SomeIpEventSubscriber<MockProxyWithEvent> subscriber(proxy);
    
    int callCount = 0;
    
    subscriber.Subscribe<int>(
        [](auto& p) -> MockEvent& { return p.getTestEvent(); },
        [&callCount](int value) { callCount++; },
        "testEvent"
    );
    
    // Unsubscribe
    auto result = subscriber.Unsubscribe("testEvent");
    ASSERT_TRUE(result.IsOk());
    EXPECT_FALSE(subscriber.IsSubscribed("testEvent"));
    
    // Fire event after unsubscribe
    proxy->getTestEvent().fire(456);
    
    // Callback should not be invoked
    EXPECT_EQ(callCount, 0);
}

/**
 * @brief Test double subscription
 */
TEST_F(SomeIpBindingTest, EventSubscriberDoubleSubscribe) {
    auto proxy = std::make_shared<MockProxyWithEvent>();
    SomeIpEventSubscriber<MockProxyWithEvent> subscriber(proxy);
    
    auto result1 = subscriber.Subscribe<int>(
        [](auto& p) -> MockEvent& { return p.getTestEvent(); },
        [](int value) {},
        "testEvent"
    );
    EXPECT_TRUE(result1.IsOk());
    
    // Second subscription with same name should fail
    auto result2 = subscriber.Subscribe<int>(
        [](auto& p) -> MockEvent& { return p.getTestEvent(); },
        [](int value) {},
        "testEvent"
    );
    EXPECT_FALSE(result2.IsOk());
    EXPECT_EQ(result2.Error().Value(), static_cast<int>(lap::com::ComErrc::AlreadyExists));
}

/**
 * @brief Test unsubscribe non-existent event
 */
TEST_F(SomeIpBindingTest, EventSubscriberUnsubscribeNonExistent) {
    auto proxy = std::make_shared<MockProxyWithEvent>();
    SomeIpEventSubscriber<MockProxyWithEvent> subscriber(proxy);
    
    auto result = subscriber.Unsubscribe("nonExistentEvent");
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.Error().Value(), static_cast<int>(lap::com::ComErrc::NotFound));
}

/**
 * @brief Test unsubscribe all
 */
TEST_F(SomeIpBindingTest, EventSubscriberUnsubscribeAll) {
    auto proxy = std::make_shared<MockProxyWithEvent>();
    SomeIpEventSubscriber<MockProxyWithEvent> subscriber(proxy);
    
    // Subscribe to multiple events (simulated)
    subscriber.Subscribe<int>(
        [](auto& p) -> MockEvent& { return p.getTestEvent(); },
        [](int value) {},
        "event1"
    );
    
    EXPECT_EQ(subscriber.GetSubscriptionCount(), 1);
    
    subscriber.UnsubscribeAll();
    
    EXPECT_EQ(subscriber.GetSubscriptionCount(), 0);
    EXPECT_FALSE(subscriber.IsSubscribed("event1"));
}

// ============================================================================
// SomeIpEventFilter Tests
// ============================================================================

/**
 * @brief Test event filter
 */
TEST_F(SomeIpBindingTest, EventFilterBasic) {
    SomeIpEventFilter<int> filter;
    
    // Without filter, all values should pass
    EXPECT_TRUE(filter.ShouldNotify(10));
    EXPECT_TRUE(filter.ShouldNotify(100));
    
    // Set filter: only values > 50
    filter.SetFilter([](int value) { return value > 50; });
    
    EXPECT_FALSE(filter.ShouldNotify(10));
    EXPECT_FALSE(filter.ShouldNotify(50));
    EXPECT_TRUE(filter.ShouldNotify(51));
    EXPECT_TRUE(filter.ShouldNotify(100));
    
    // Clear filter
    filter.ClearFilter();
    
    EXPECT_TRUE(filter.ShouldNotify(10));
    EXPECT_TRUE(filter.ShouldNotify(100));
}

/**
 * @brief Main test runner
 */
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
