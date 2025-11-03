/**
 * @file        calculator_client.cpp
 * @brief       Socket Calculator Service Example (Client)
 * @author      LightAP Team
 * @date        2025-10-30
 * @details     Example client using Protobuf over Unix Domain Socket
 */

#include <binding/socket/SocketMethodBinding.hpp>
#include <log/CLog.hpp>
#include <log/CLogManager.hpp>
#include <core/CMemory.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>

// Include generated protobuf header
#include "../../tools/protobuf/generated/calculator.pb.h"

using namespace lap::com::binding::socket;
using namespace lap::com::example;

/**
 * @brief Test case structure
 */
struct TestCase {
    double operand1;
    double operand2;
    std::string operation;
    double expectedResult;
};

/**
 * @brief Run a single test case
 */
bool runTestCase(SocketMethodCaller<CalculateRequest, CalculateResponse>& caller,
                const TestCase& testCase) {
    
    // Prepare request
    CalculateRequest request;
    request.set_operand1(testCase.operand1);
    request.set_operand2(testCase.operand2);
    request.set_operation(testCase.operation);
    
    LAP_LOG_INFO("COM.SOCKET.Example") << "[CalculatorClient] Calling: " << testCase.operand1 
                                        << " " << testCase.operation.c_str() << " " << testCase.operand2;
    
    // Call method (5 second timeout)
    auto result = caller.call(request, 5000);
    
    if (!result.HasValue()) {
        LAP_LOG_ERROR("COM.SOCKET.Example") << "[CalculatorClient] Call failed: " << result.Error().Message();
        return false;
    }
    
    const auto& response = result.Value();
    
    // Check for errors
    if (response.error_code() != 0) {
        LAP_LOG_ERROR("COM.SOCKET.Example") << "[CalculatorClient] Server returned error: " 
                                             << response.error_message().c_str() << " (code: " << response.error_code() << ")";
        return false;
    }
    
    // Verify result
    LAP_LOG_INFO("COM.SOCKET.Example") << "[CalculatorClient] Result: " << response.result();
    
    if (std::abs(response.result() - testCase.expectedResult) > 0.001) {
        LAP_LOG_ERROR("COM.SOCKET.Example") << "[CalculatorClient] Result mismatch! Expected: " 
                                             << testCase.expectedResult << ", Got: " << response.result();
        return false;
    }
    
    LAP_LOG_INFO("COM.SOCKET.Example") << "[CalculatorClient] ✓ Test passed";
    return true;
}

/**
 * @brief Test async call with future
 */
void testAsyncCall(SocketMethodCaller<CalculateRequest, CalculateResponse>& caller) {
    LAP_LOG_INFO("COM.SOCKET.Example") << "[CalculatorClient] Testing async call...";
    
    CalculateRequest request;
    request.set_operand1(100);
    request.set_operand2(25);
    request.set_operation("multiply");
    
    // Call asynchronously
    auto future = caller.callAsyncFuture(request, 5000);
    
    LAP_LOG_INFO("COM.SOCKET.Example") << "[CalculatorClient] Async call initiated, doing other work...";
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Wait for result
    auto result = future.get();
    
    if (result.HasValue()) {
        LAP_LOG_INFO("COM.SOCKET.Example") << "[CalculatorClient] Async result: " << result.Value().result();
    } else {
        LAP_LOG_ERROR("COM.SOCKET.Example") << "[CalculatorClient] Async call failed: " << result.Error().Message();
    }
}

int main(int argc, char* argv[]) {
    // Initialize memory manager first to avoid static destruction issues
    (void)::lap::core::MemManager::getInstance();
    // Initialize logging
    ::lap::log::LogManager::getInstance().initialize();
    LAP_LOG_INFO("COM.SOCKET.Example") << "========================================";
    LAP_LOG_INFO("COM.SOCKET.Example") << "  Calculator Client (Socket + Protobuf)";
    LAP_LOG_INFO("COM.SOCKET.Example") << "========================================";
    
    // Initialize socket connection manager
    auto& manager = SocketConnectionManager::GetInstance();
    auto initResult = manager.initialize();
    if (!initResult.HasValue()) {
        LAP_LOG_ERROR("COM.SOCKET.Example") << "[CalculatorClient] Failed to initialize socket manager: "
                                             << initResult.Error().Message();
        return 1;
    }
    
    // Configure client endpoint
    const char* socketPath = (argc > 1) ? argv[1] : "/tmp/calculator.sock";
    
    SocketEndpoint endpoint{
        .socketPath = socketPath,
        .mode = SocketTransportMode::kStream,
        .maxMessageSize = 65536,
        .sendBufferSize = 8192,
        .recvBufferSize = 8192,
        .reuseAddr = false,
        .listenBacklog = 0
    };
    
    LAP_LOG_INFO("COM.SOCKET.Example") << "[CalculatorClient] Connecting to: " << endpoint.socketPath.c_str();
    
    // Create method caller
    SocketMethodCaller<CalculateRequest, CalculateResponse> caller(endpoint);
    
    // Define test cases
    std::vector<TestCase> testCases = {
        {10.5, 3.2, "add", 13.7},
        {20.0, 5.0, "subtract", 15.0},
        {7.0, 8.0, "multiply", 56.0},
        {100.0, 4.0, "divide", 25.0},
        {50.0, 2.0, "add", 52.0},
        {1000.0, 999.0, "subtract", 1.0}
    };
    
    // Run test cases
    int passed = 0;
    int failed = 0;
    
    LAP_LOG_INFO("COM.SOCKET.Example") << "";
    LAP_LOG_INFO("COM.SOCKET.Example") << "Running test cases:";
    LAP_LOG_INFO("COM.SOCKET.Example") << "-------------------";
    
    for (size_t i = 0; i < testCases.size(); ++i) {
        LAP_LOG_INFO("COM.SOCKET.Example") << "";
        LAP_LOG_INFO("COM.SOCKET.Example") << "Test Case " << (i + 1) << "/" << testCases.size();
        
        if (runTestCase(caller, testCases[i])) {
            ++passed;
        } else {
            ++failed;
        }
        
        // Small delay between calls
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Test async call
    LAP_LOG_INFO("COM.SOCKET.Example") << "";
    LAP_LOG_INFO("COM.SOCKET.Example") << "-------------------";
    testAsyncCall(caller);
    
    // Test error handling
    LAP_LOG_INFO("COM.SOCKET.Example") << "";
    LAP_LOG_INFO("COM.SOCKET.Example") << "Testing error handling:";
    LAP_LOG_INFO("COM.SOCKET.Example") << "-------------------";
    
    CalculateRequest errorRequest;
    errorRequest.set_operand1(10);
    errorRequest.set_operand2(0);
    errorRequest.set_operation("divide");
    
    auto result = caller.call(errorRequest, 5000);
    if (result.HasValue()) {
        const auto& response = result.Value();
        if (response.error_code() != 0) {
            LAP_LOG_INFO("COM.SOCKET.Example") << "[CalculatorClient] ✓ Error handling works: "
                                                << response.error_message().c_str();
        }
    }
    
    // Print summary
    LAP_LOG_INFO("COM.SOCKET.Example") << "";
    LAP_LOG_INFO("COM.SOCKET.Example") << "========================================";
    LAP_LOG_INFO("COM.SOCKET.Example") << "Test Summary:";
    LAP_LOG_INFO("COM.SOCKET.Example") << "  Passed: " << passed;
    LAP_LOG_INFO("COM.SOCKET.Example") << "  Failed: " << failed;
    LAP_LOG_INFO("COM.SOCKET.Example") << "  Total:  " << (passed + failed);
    LAP_LOG_INFO("COM.SOCKET.Example") << "========================================";
    
    // Cleanup
    manager.deinitialize();
    ::lap::log::LogManager::getInstance().uninitialize();
    
    return (failed == 0) ? 0 : 1;
}
