/**
 * @file calculator_client_someip.cpp
 * @brief Calculator client using CommonAPI SOME/IP proxy
 * 
 * This example demonstrates:
 * - Using CommonAPI Proxy for SOME/IP (client-side)
 * - Using SomeIpProxyAdapter to integrate with LightAP
 * - Synchronous method calls over SOME/IP
 * - Event subscriptions
 * - Error handling with lap::core::Result<T>
 */

#include <memory>
#include <thread>
#include <chrono>
#include <csignal>

// LightAP Core
#include <Core/CoreBase/inc/CMemory.h>
#include <Core/CoreBase/inc/CLog.h>

// vsomeip and CommonAPI-SomeIP integration
#include <binding/someip/SomeIpConnectionManager.hpp>
#include <binding/commonapi/CommonAPISomeIpAdapter.hpp>

// CommonAPI generated headers (will be available after running generate_new.sh)
// #include <v1/com/lightap/example/CalculatorProxy.hpp>

using namespace lap::core;
using namespace lap::log;

// TODO: Uncomment after code generation
#if 0
/**
 * @brief Perform a calculation and print result
 */
void performCalculation(
    std::shared_ptr<v1::com::lightap::example::CalculatorProxy> proxy,
    lap::com::commonapi::SomeIpProxyAdapter<v1::com::lightap::example::CalculatorProxy>& adapter,
    float a, const std::string& op, float b)
{
    LAP_LOG_INFO("Calculate: %f %s %f", a, op.c_str(), b);

    // Prepare output parameters
    CommonAPI::CallStatus callStatus;
    float result = 0.0f;
    int32_t errorCode = 0;

    // Call remote method (synchronous over SOME/IP)
    proxy->calculate(a, b, op, callStatus, result, errorCode);

    // Wrap CommonAPI call status to LightAP Result<T>
    auto callResult = adapter.WrapCallStatus<float>(callStatus, result);

    if (!callResult.HasValue()) {
        LAP_LOG_ERROR("  RPC failed: %s", callResult.Error().Message().c_str());
        return;
    }

    // Check application-level error code
    if (errorCode != 0) {
        const char* errorMsg = (errorCode == 1) ? "Invalid operation" : "Division by zero";
        LAP_LOG_WARN("  Result: ERROR (%s)", errorMsg);
    } else {
        LAP_LOG_INFO("  Result: %f", result);
    }
}

/**
 * @brief Test fire-and-forget method
 */
void performAsyncCalculation(
    std::shared_ptr<v1::com::lightap::example::CalculatorProxy> proxy,
    float a, const std::string& op, float b)
{
    LAP_LOG_INFO("CalculateAsync (fire-and-forget): %f %s %f", a, op.c_str(), b);
    
    // Fire-and-forget - no response expected
    proxy->calculateAsync(a, b, op);
}

/**
 * @brief Subscribe to broadcasts
 */
void subscribeToEvents(std::shared_ptr<v1::com::lightap::example::CalculatorProxy> proxy) {
    // Subscribe to resultReady broadcast
    proxy->getResultReadyEvent().subscribe([](const float& value) {
        LAP_LOG_INFO("Event received: resultReady(%f)", value);
    });

    // Subscribe to errorOccurred broadcast
    proxy->getErrorOccurredEvent().subscribe([](const std::string& message) {
        LAP_LOG_ERROR("Event received: errorOccurred(%s)", message.c_str());
    });

    LAP_LOG_INFO("Subscribed to broadcasts");
}

/**
 * @brief Read and write attributes
 */
void testAttributes(
    std::shared_ptr<v1::com::lightap::example::CalculatorProxy> proxy,
    lap::com::commonapi::SomeIpProxyAdapter<v1::com::lightap::example::CalculatorProxy>& adapter)
{
    CommonAPI::CallStatus callStatus;

    // Read lastResult attribute
    float lastResult = 0.0f;
    proxy->getLastResultAttribute().getValue(callStatus, lastResult);
    auto readResult = adapter.WrapCallStatus<float>(callStatus, lastResult);
    if (readResult.HasValue()) {
        LAP_LOG_INFO("LastResult attribute: %f", lastResult);
    } else {
        LAP_LOG_ERROR("Failed to read lastResult: %s", readResult.Error().Message().c_str());
    }

    // Write lastResult attribute
    proxy->getLastResultAttribute().setValue(100.0f, callStatus, lastResult);
    auto writeResult = adapter.WrapCallStatus<float>(callStatus, lastResult);
    if (writeResult.HasValue()) {
        LAP_LOG_INFO("Set lastResult to 100.0, callback value: %f", lastResult);
    } else {
        LAP_LOG_ERROR("Failed to write lastResult: %s", writeResult.Error().Message().c_str());
    }

    // Read calculationCount (readonly attribute)
    uint32_t count = 0;
    proxy->getCalculationCountAttribute().getValue(callStatus, count);
    auto countResult = adapter.WrapCallStatus<uint32_t>(callStatus, count);
    if (countResult.HasValue()) {
        LAP_LOG_INFO("CalculationCount attribute: %u", count);
    } else {
        LAP_LOG_ERROR("Failed to read calculationCount: %s", countResult.Error().Message().c_str());
    }
}
#endif

// Global flag
static volatile bool g_running = true;

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        LAP_LOG_INFO("Received shutdown signal");
        g_running = false;
    }
}

/**
 * @brief Main entry point
 */
int main(int argc, char** argv) {
    // Initialize LightAP core
    MemoryManager::getInstance();
    LogManager::getInstance().initialize();

    LAP_LOG_INFO("=== Calculator Client (SOME/IP) ===");

    // Setup signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

// TODO: Uncomment after code generation
#if 0
    try {
        // Step 1: Initialize vsomeip connection manager
        auto& connMgr = lap::com::someip::SomeIpConnectionManager::getInstance();
        
        // Application name must match vsomeip config
        auto initResult = connMgr.Initialize("calculator_client");
        if (!initResult.HasValue()) {
            LAP_LOG_ERROR("Failed to initialize vsomeip: %s", 
                         initResult.Error().Message().c_str());
            return 1;
        }

        LAP_LOG_INFO("vsomeip initialized");

        // Step 2: Start vsomeip application in separate thread
        std::thread vsomeipThread([&connMgr]() {
            auto startResult = connMgr.Start(true); // Blocking call
            if (!startResult.HasValue()) {
                LAP_LOG_ERROR("vsomeip start failed: %s", 
                             startResult.Error().Message().c_str());
            }
        });

        // Give vsomeip time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Step 3: Create proxy adapter
        lap::com::commonapi::SomeIpProxyAdapter<v1::com::lightap::example::CalculatorProxy> 
            adapter("local", "Calculator");

        // Step 4: Initialize proxy and wait for service
        LAP_LOG_INFO("Connecting to Calculator service...");
        auto proxyResult = adapter.Initialize(10000); // 10 second timeout
        if (!proxyResult.HasValue()) {
            LAP_LOG_ERROR("Failed to connect: %s", proxyResult.Error().Message().c_str());
            connMgr.Stop();
            vsomeipThread.join();
            return 1;
        }

        LAP_LOG_INFO("Service available!");

        // Get proxy instance
        auto proxy = adapter.GetProxy();
        if (!proxy) {
            LAP_LOG_ERROR("Proxy is null");
            connMgr.Stop();
            vsomeipThread.join();
            return 1;
        }

        // Step 5: Subscribe to events
        subscribeToEvents(proxy);

        // Step 6: Perform calculations
        performCalculation(proxy, adapter, 10.0f, "+", 5.0f);    // 15
        performCalculation(proxy, adapter, 20.0f, "-", 8.0f);    // 12
        performCalculation(proxy, adapter, 6.0f, "*", 7.0f);     // 42
        performCalculation(proxy, adapter, 100.0f, "/", 4.0f);   // 25

        // Test error cases
        performCalculation(proxy, adapter, 10.0f, "/", 0.0f);    // Division by zero
        performCalculation(proxy, adapter, 5.0f, "%", 2.0f);     // Invalid operation

        // Test fire-and-forget
        performAsyncCalculation(proxy, 99.0f, "+", 1.0f);

        // Test attributes
        testAttributes(proxy, adapter);

        LAP_LOG_INFO("All operations completed");

        // Keep running to receive broadcasts
        LAP_LOG_INFO("Listening for broadcasts (press Ctrl+C to stop)...");
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // Step 7: Graceful shutdown
        LAP_LOG_INFO("Shutting down...");
        connMgr.Stop();
        vsomeipThread.join();
        connMgr.Deinitialize();

        LAP_LOG_INFO("Client stopped successfully");

    } catch (const std::exception& e) {
        LAP_LOG_ERROR("Exception: %s", e.what());
        return 1;
    }
#else
    LAP_LOG_WARN("========================================");
    LAP_LOG_WARN("Code generation required!");
    LAP_LOG_WARN("Run: cd ../../tools/commonapi");
    LAP_LOG_WARN("     ./generate_new.sh ../fidl/examples/Calculator.fidl someip");
    LAP_LOG_WARN("Then uncomment the #if 0 blocks in this file");
    LAP_LOG_WARN("========================================");
    LAP_LOG_INFO("");
    LAP_LOG_INFO("Before running client:");
    LAP_LOG_INFO("1. Ensure vsomeip is installed");
    LAP_LOG_INFO("2. Start calculator_server first");
    LAP_LOG_INFO("3. Set VSOMEIP_CONFIGURATION environment variable");
    LAP_LOG_INFO("   export VSOMEIP_CONFIGURATION=<path>/vsomeip_calculator.json");
#endif

    return 0;
}
