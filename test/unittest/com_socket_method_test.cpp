/**
 * @file        com_socket_method_test.cpp
 * @brief       Unit tests for SocketMethodBinding (Caller and Responder)
 * @author      LightAP Team
 * @date        2025-10-30
 */

#include <binding/socket/SocketMethodBinding.hpp>
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <future>
#include <atomic>

using namespace lap::com::binding::socket;

// Simple test messages (reusing from serializer test)
namespace lap {
namespace com {
namespace test {

class RequestMessage : public google::protobuf::MessageLite {
public:
    RequestMessage() : m_value(0) {}
    
    void set_value(int32_t value) { m_value = value; }
    int32_t value() const { return m_value; }
    
    // MessageLite interface
    std::string GetTypeName() const override { return "RequestMessage"; }
    MessageLite* New(google::protobuf::Arena* arena) const override { 
        return new RequestMessage(); 
    }
    void Clear() override { m_value = 0; }
    bool IsInitialized() const override { return true; }
    size_t ByteSizeLong() const override { return sizeof(m_value); }
    
    bool SerializeToArray(void* data, int size) const override {
        if (size < static_cast<int>(sizeof(m_value))) return false;
        std::memcpy(data, &m_value, sizeof(m_value));
        return true;
    }
    
    bool ParseFromArray(const void* data, int size) override {
        if (size < static_cast<int>(sizeof(m_value))) return false;
        std::memcpy(&m_value, data, sizeof(m_value));
        return true;
    }
    
    int GetCachedSize() const override { return static_cast<int>(ByteSizeLong()); }
    
    uint8_t* _InternalSerialize(
        uint8_t* target,
        google::protobuf::io::EpsCopyOutputStream* stream) const override {
        std::memcpy(target, &m_value, sizeof(m_value));
        return target + sizeof(m_value);
    }

private:
    int32_t m_value;
};

class ResponseMessage : public google::protobuf::MessageLite {
public:
    ResponseMessage() : m_result(0) {}
    
    void set_result(int32_t result) { m_result = result; }
    int32_t result() const { return m_result; }
    
    // MessageLite interface
    std::string GetTypeName() const override { return "ResponseMessage"; }
    MessageLite* New(google::protobuf::Arena* arena) const override { 
        return new ResponseMessage(); 
    }
    void Clear() override { m_result = 0; }
    bool IsInitialized() const override { return true; }
    size_t ByteSizeLong() const override { return sizeof(m_result); }
    
    bool SerializeToArray(void* data, int size) const override {
        if (size < static_cast<int>(sizeof(m_result))) return false;
        std::memcpy(data, &m_result, sizeof(m_result));
        return true;
    }
    
    bool ParseFromArray(const void* data, int size) override {
        if (size < static_cast<int>(sizeof(m_result))) return false;
        std::memcpy(&m_result, data, sizeof(m_result));
        return true;
    }
    
    int GetCachedSize() const override { return static_cast<int>(ByteSizeLong()); }
    
    uint8_t* _InternalSerialize(
        uint8_t* target,
        google::protobuf::io::EpsCopyOutputStream* stream) const override {
        std::memcpy(target, &m_result, sizeof(m_result));
        return target + sizeof(m_result);
    }

private:
    int32_t m_result;
};

} // namespace test
} // namespace com
} // namespace lap

using namespace lap::com::test;

/**
 * @brief Test fixture for SocketMethodBinding
 */
class SocketMethodBindingTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_socketPath = "/tmp/test_method_socket_" + 
                      std::to_string(std::time(nullptr)) + ".sock";
    }

    void TearDown() override {
        ::unlink(m_socketPath.c_str());
    }

    std::string m_socketPath;
};

/**
 * @brief Test basic synchronous method call
 */
TEST_F(SocketMethodBindingTest, BasicSynchronousCall) {
    // Handler that doubles the input value
    auto handler = [](const RequestMessage& req) -> Result<ResponseMessage> {
        ResponseMessage resp;
        resp.set_result(req.value() * 2);
        return Result<ResponseMessage>(resp);
    };
    
    // Start responder (server)
    SocketMethodResponder<RequestMessage, ResponseMessage> responder(m_socketPath, handler);
    auto startResult = responder.start();
    ASSERT_TRUE(startResult.HasValue());
    
    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Create caller (client)
    SocketMethodCaller<RequestMessage, ResponseMessage> caller(m_socketPath);
    
    // Make request
    RequestMessage req;
    req.set_value(42);
    
    auto result = caller.call(req, 5000);
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value().result(), 84);
    
    // Cleanup
    responder.stop();
}

/**
 * @brief Test multiple sequential calls
 */
TEST_F(SocketMethodBindingTest, MultipleSequentialCalls) {
    auto handler = [](const RequestMessage& req) -> Result<ResponseMessage> {
        ResponseMessage resp;
        resp.set_result(req.value() + 100);
        return Result<ResponseMessage>(resp);
    };
    
    SocketMethodResponder<RequestMessage, ResponseMessage> responder(m_socketPath, handler);
    auto startResult = responder.start();
    ASSERT_TRUE(startResult.HasValue());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    SocketMethodCaller<RequestMessage, ResponseMessage> caller(m_socketPath);
    
    // Make multiple calls
    for (int i = 0; i < 10; ++i) {
        RequestMessage req;
        req.set_value(i);
        
        auto result = caller.call(req, 5000);
        ASSERT_TRUE(result.HasValue());
        EXPECT_EQ(result.Value().result(), i + 100);
    }
    
    responder.stop();
}

/**
 * @brief Test asynchronous call with callback
 */
TEST_F(SocketMethodBindingTest, AsynchronousCallWithCallback) {
    auto handler = [](const RequestMessage& req) -> Result<ResponseMessage> {
        // Simulate some processing
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ResponseMessage resp;
        resp.set_result(req.value() * 3);
        return Result<ResponseMessage>(resp);
    };
    
    SocketMethodResponder<RequestMessage, ResponseMessage> responder(m_socketPath, handler);
    auto startResult = responder.start();
    ASSERT_TRUE(startResult.HasValue());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    SocketMethodCaller<RequestMessage, ResponseMessage> caller(m_socketPath);
    
    // Async call with callback
    std::promise<int32_t> resultPromise;
    auto resultFuture = resultPromise.get_future();
    
    auto callback = [&resultPromise](Result<ResponseMessage> result) {
        if (result.HasValue()) {
            resultPromise.set_value(result.Value().result());
        } else {
            resultPromise.set_value(-1);
        }
    };
    
    RequestMessage req;
    req.set_value(99);
    
    caller.callAsync(req, callback, 5000);
    
    // Wait for result
    auto status = resultFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);
    EXPECT_EQ(resultFuture.get(), 297);
    
    responder.stop();
}

/**
 * @brief Test asynchronous call with future
 */
TEST_F(SocketMethodBindingTest, AsynchronousCallWithFuture) {
    auto handler = [](const RequestMessage& req) -> Result<ResponseMessage> {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ResponseMessage resp;
        resp.set_result(req.value() - 10);
        return Result<ResponseMessage>(resp);
    };
    
    SocketMethodResponder<RequestMessage, ResponseMessage> responder(m_socketPath, handler);
    auto startResult = responder.start();
    ASSERT_TRUE(startResult.HasValue());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    SocketMethodCaller<RequestMessage, ResponseMessage> caller(m_socketPath);
    
    RequestMessage req;
    req.set_value(200);
    
    auto future = caller.callAsyncFuture(req, 5000);
    
    auto status = future.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);
    
    auto result = future.get();
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value().result(), 190);
    
    responder.stop();
}

/**
 * @brief Test concurrent calls from multiple clients
 */
TEST_F(SocketMethodBindingTest, ConcurrentClients) {
    std::atomic<int> callsHandled(0);
    
    auto handler = [&callsHandled](const RequestMessage& req) -> Result<ResponseMessage> {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ResponseMessage resp;
        resp.set_result(req.value() * 2);
        ++callsHandled;
        return Result<ResponseMessage>(resp);
    };
    
    SocketMethodResponder<RequestMessage, ResponseMessage> responder(m_socketPath, handler);
    auto startResult = responder.start();
    ASSERT_TRUE(startResult.HasValue());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Create multiple client threads
    const int numClients = 5;
    std::vector<std::thread> clientThreads;
    std::vector<bool> results(numClients, false);
    
    for (int i = 0; i < numClients; ++i) {
        clientThreads.emplace_back([this, i, &results]() {
            SocketMethodCaller<RequestMessage, ResponseMessage> caller(m_socketPath);
            
            RequestMessage req;
            req.set_value(i * 10);
            
            auto result = caller.call(req, 5000);
            if (result.HasValue() && result.Value().result() == i * 20) {
                results[i] = true;
            }
        });
    }
    
    // Wait for all clients
    for (auto& thread : clientThreads) {
        thread.join();
    }
    
    // Verify all calls succeeded
    for (bool result : results) {
        EXPECT_TRUE(result);
    }
    
    EXPECT_EQ(callsHandled, numClients);
    
    responder.stop();
}

/**
 * @brief Test timeout handling
 */
TEST_F(SocketMethodBindingTest, TimeoutHandling) {
    auto handler = [](const RequestMessage& req) -> Result<ResponseMessage> {
        // Sleep longer than client timeout
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        ResponseMessage resp;
        resp.set_result(req.value());
        return Result<ResponseMessage>(resp);
    };
    
    SocketMethodResponder<RequestMessage, ResponseMessage> responder(m_socketPath, handler);
    auto startResult = responder.start();
    ASSERT_TRUE(startResult.HasValue());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    SocketMethodCaller<RequestMessage, ResponseMessage> caller(m_socketPath);
    
    RequestMessage req;
    req.set_value(123);
    
    // Call with short timeout (should timeout)
    auto result = caller.call(req, 500);
    EXPECT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().Value(), static_cast<int>(lap::com::ComErrc::kTimeout));
    
    responder.stop();
}

/**
 * @brief Test error handling in handler
 */
TEST_F(SocketMethodBindingTest, HandlerErrorHandling) {
    auto handler = [](const RequestMessage& req) -> Result<ResponseMessage> {
        if (req.value() == 0) {
            return Result<ResponseMessage>(
                lap::com::MakeErrorCode(lap::com::ComErrc::kInvalidArgument));
        }
        ResponseMessage resp;
        resp.set_result(100 / req.value());
        return Result<ResponseMessage>(resp);
    };
    
    SocketMethodResponder<RequestMessage, ResponseMessage> responder(m_socketPath, handler);
    auto startResult = responder.start();
    ASSERT_TRUE(startResult.HasValue());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    SocketMethodCaller<RequestMessage, ResponseMessage> caller(m_socketPath);
    
    // Valid request
    {
        RequestMessage req;
        req.set_value(10);
        auto result = caller.call(req, 5000);
        ASSERT_TRUE(result.HasValue());
        EXPECT_EQ(result.Value().result(), 10);
    }
    
    // Invalid request (causes error in handler)
    {
        RequestMessage req;
        req.set_value(0);
        auto result = caller.call(req, 5000);
        EXPECT_FALSE(result.HasValue());
        EXPECT_EQ(result.Error().Value(), 
                 static_cast<int>(lap::com::ComErrc::kInvalidArgument));
    }
    
    responder.stop();
}

/**
 * @brief Test responder start/stop
 */
TEST_F(SocketMethodBindingTest, ResponderStartStop) {
    auto handler = [](const RequestMessage& req) -> Result<ResponseMessage> {
        ResponseMessage resp;
        resp.set_result(req.value());
        return Result<ResponseMessage>(resp);
    };
    
    SocketMethodResponder<RequestMessage, ResponseMessage> responder(m_socketPath, handler);
    
    // Start
    auto startResult = responder.start();
    EXPECT_TRUE(startResult.HasValue());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Make a call while running
    SocketMethodCaller<RequestMessage, ResponseMessage> caller(m_socketPath);
    RequestMessage req;
    req.set_value(55);
    auto result = caller.call(req, 5000);
    EXPECT_TRUE(result.HasValue());
    
    // Stop
    responder.stop();
    
    // Call should fail after stop
    auto result2 = caller.call(req, 1000);
    EXPECT_FALSE(result2.HasValue());
    
    // Restart should work
    auto restartResult = responder.start();
    EXPECT_TRUE(restartResult.HasValue());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // New connection should work
    SocketMethodCaller<RequestMessage, ResponseMessage> caller2(m_socketPath);
    auto result3 = caller2.call(req, 5000);
    EXPECT_TRUE(result3.HasValue());
    
    responder.stop();
}

/**
 * @brief Test call to non-existent server
 */
TEST_F(SocketMethodBindingTest, CallToNonExistentServer) {
    SocketMethodCaller<RequestMessage, ResponseMessage> caller("/tmp/nonexistent_socket.sock");
    
    RequestMessage req;
    req.set_value(42);
    
    auto result = caller.call(req, 1000);
    EXPECT_FALSE(result.HasValue());
}

/**
 * @brief Main function
 */
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
