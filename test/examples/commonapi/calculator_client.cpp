/**
 * @file calculator_client.cpp
 * @brief Calculator client using CommonAPI generated proxy
 * 
 * This example demonstrates:
 * - Using CommonAPI Proxy (client-side)
 * - Using ProxyAdapter to integrate with LightAP
 * - Synchronous method calls
 * - Error handling with lap::core::Result<T>
 * - LAP_LOG integration
 */

#include <memory>
#include <thread>
#include <chrono>

// LightAP Core
#include <Core/CoreBase/inc/CMemory.h>
#include <Core/CoreBase/inc/CLog.h>

// CommonAPI generated headers (will be available after running generate.sh)
// #include <v1/com/lightap/example/CalculatorProxy.hpp>

// LightAP CommonAPI-DBus adapter
#include <binding/commonapi/CommonAPIDBusAdapter.hpp>

using namespace lap::core;
using namespace lap::log;

// TODO: Uncomment after code generation
#if 0
/**
 * @brief Perform a calculation and print result
 * @param proxy Calculator proxy
 * @param a First operand
 * @param op Operation
 * @param b Second operand
 */
void performCalculation(
    std::shared_ptr<v1::com::lightap::example::CalculatorProxy> proxy,
    lap::com::commonapi::ProxyAdapter<v1::com::lightap::example::CalculatorProxy>& adapter,
    float a, const std::string& op, float b)
{
    LAP_LOG_INFO("Calculate: %f %s %f", a, op.c_str(), b);

    // Prepare output parameters
    CommonAPI::CallStatus callStatus;
    float result = 0.0f;
    int32_t errorCode = 0;

    // Call remote method (synchronous)
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
 * @brief Subscribe to result broadcast
 * @param proxy Calculator proxy
 */
void subscribeToEvents(std::shared_ptr<v1::com::lightap::example::CalculatorProxy> proxy) {
    // Subscribe to resultReady broadcast
    proxy->getResultReadyEvent().subscribe([](const float& value) {
        LAP_LOG_INFO("Event received: resultReady(%f)", value);
    });

    LAP_LOG_INFO("Subscribed to resultReady events");
}

/**
 * @brief Read attribute value
 * @param proxy Calculator proxy
 * @param adapter Adapter for error handling
 */
void readAttribute(
    std::shared_ptr<v1::com::lightap::example::CalculatorProxy> proxy,
    lap::com::commonapi::ProxyAdapter<v1::com::lightap::example::CalculatorProxy>& adapter)
{
    CommonAPI::CallStatus callStatus;
    float currentValue = 0.0f;

    proxy->getCurrentValueAttribute().getValue(callStatus, currentValue);

    auto result = adapter.WrapCallStatus<float>(callStatus, currentValue);
    if (result.HasValue()) {
        LAP_LOG_INFO("CurrentValue attribute: %f", currentValue);
    } else {
        LAP_LOG_ERROR("Failed to read attribute: %s", result.Error().Message().c_str());
    }
}
#endif

/**
 * @brief Main entry point
 */
int main(int argc, char** argv) {
    // Initialize LightAP core
    MemoryManager::getInstance();
    LogManager::getInstance().initialize();

    LAP_LOG_INFO("=== Calculator Client (CommonAPI) ===");

// TODO: Uncomment after code generation
#if 0
    try {
        // Create proxy adapter (domain="local", instance="Calculator")
        lap::com::commonapi::ProxyAdapter<v1::com::lightap::example::CalculatorProxy> 
            adapter("local", "Calculator");

        // Initialize proxy and wait for service availability
        LAP_LOG_INFO("Connecting to Calculator service...");
        auto result = adapter.Initialize();
        if (!result.HasValue()) {
            LAP_LOG_ERROR("Failed to connect: %s", result.Error().Message().c_str());
            return 1;
        }

        LAP_LOG_INFO("Service available!");

        // Get proxy instance
        auto proxy = adapter.GetProxy();
        if (!proxy) {
            LAP_LOG_ERROR("Proxy is null");
            return 1;
        }

        // Subscribe to broadcasts (optional)
        subscribeToEvents(proxy);

        // Perform some calculations
        performCalculation(proxy, adapter, 10.0f, "+", 5.0f);    // 15
        performCalculation(proxy, adapter, 20.0f, "-", 8.0f);    // 12
        performCalculation(proxy, adapter, 6.0f, "*", 7.0f);     // 42
        performCalculation(proxy, adapter, 100.0f, "/", 4.0f);   // 25

        // Test error cases
        performCalculation(proxy, adapter, 10.0f, "/", 0.0f);    // Division by zero
        performCalculation(proxy, adapter, 5.0f, "%", 2.0f);     // Invalid operation

        // Read attribute
        readAttribute(proxy, adapter);

        LAP_LOG_INFO("All operations completed");

        // Keep running to receive broadcasts (if any)
        LAP_LOG_INFO("Waiting for broadcasts (5 seconds)...");
        std::this_thread::sleep_for(std::chrono::seconds(5));

        // Cleanup handled automatically by ProxyAdapter destructor
    } catch (const std::exception& e) {
        LAP_LOG_ERROR("Exception: %s", e.what());
        return 1;
    }
#else
    LAP_LOG_WARN("========================================");
    LAP_LOG_WARN("Code generation required!");
    LAP_LOG_WARN("Run: cd ../../tools/commonapi && ./generate.sh ../fidl/examples/Calculator.fidl");
    LAP_LOG_WARN("Then uncomment the #if 0 blocks in this file");
    LAP_LOG_WARN("========================================");
#endif

    return 0;
}
