/**
 * @file calculator_server_someip.cpp
 * @brief Calculator service implementation using CommonAPI SOME/IP
 * 
 * This example demonstrates:
 * - Implementing a CommonAPI Stub for SOME/IP (server-side)
 * - Using SomeIpStubAdapter to integrate with LightAP
 * - vsomeip lifecycle management
 * - LAP_LOG integration
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
// #include <v1/com/lightap/example/CalculatorStubDefault.hpp>

using namespace lap::core;
using namespace lap::log;

// TODO: Uncomment after code generation
#if 0
/**
 * @class CalculatorServiceImpl
 * @brief Implementation of Calculator interface for SOME/IP
 */
class CalculatorServiceImpl : public v1::com::lightap::example::CalculatorStubDefault {
public:
    CalculatorServiceImpl() : m_currentValue(0.0f) {
        LAP_LOG_INFO("CalculatorServiceImpl (SOME/IP) created");
    }

    ~CalculatorServiceImpl() override {
        LAP_LOG_INFO("CalculatorServiceImpl destroyed");
    }

    /**
     * @brief Synchronous calculation
     */
    void calculate(const float& a, const float& b, const std::string& op, 
                   float& result, int32_t& errorCode) override {
        LAP_LOG_INFO("calculate(%f %s %f)", a, op.c_str(), b);

        errorCode = 0; // Success
        result = 0.0f;

        if (op == "+") {
            result = a + b;
        } else if (op == "-") {
            result = a - b;
        } else if (op == "*") {
            result = a * b;
        } else if (op == "/") {
            if (b == 0.0f) {
                LAP_LOG_ERROR("Division by zero");
                errorCode = 2; // DIVIDE_BY_ZERO
                return;
            }
            result = a / b;
        } else {
            LAP_LOG_WARN("Invalid operation: %s", op.c_str());
            errorCode = 1; // INVALID_OPERATION
            return;
        }

        // Update internal state
        m_currentValue = result;

        // Fire broadcast to all subscribers
        fireResultReadyEvent(result);

        LAP_LOG_INFO("Result: %f (error=%d)", result, errorCode);
    }

    /**
     * @brief Fire-and-forget calculation (no response)
     */
    void calculateAsync(const float& a, const float& b, const std::string& op) override {
        LAP_LOG_INFO("calculateAsync(%f %s %f) - fire-and-forget", a, op.c_str(), b);
        
        float result = 0.0f;
        int32_t errorCode = 0;
        calculate(a, b, op, result, errorCode);
        
        // No response sent in fire-and-forget
    }

    /**
     * @brief Get last calculation result (attribute getter)
     */
    const float& getLastResultAttribute() override {
        LAP_LOG_DEBUG("getLastResultAttribute() -> %f", m_currentValue);
        return m_currentValue;
    }

    /**
     * @brief Set last result (attribute setter)
     */
    void setLastResultAttribute(const float& value) override {
        LAP_LOG_DEBUG("setLastResultAttribute(%f)", value);
        m_currentValue = value;
        fireLastResultAttributeChanged(value);
    }

    /**
     * @brief Get calculation count (readonly attribute)
     */
    const uint32_t& getCalculationCountAttribute() override {
        // In real implementation, track actual count
        static uint32_t count = 42;
        LAP_LOG_DEBUG("getCalculationCountAttribute() -> %u", count);
        return count;
    }

private:
    float m_currentValue; ///< Current calculation result
};
#endif

// Global flag for graceful shutdown
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

    LAP_LOG_INFO("=== Calculator Service (SOME/IP) ===");

    // Setup signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

// TODO: Uncomment after code generation
#if 0
    try {
        // Step 1: Initialize vsomeip connection manager
        auto& connMgr = lap::com::someip::SomeIpConnectionManager::getInstance();
        
        // Application name must match vsomeip config
        auto initResult = connMgr.Initialize("calculator_service");
        if (!initResult.HasValue()) {
            LAP_LOG_ERROR("Failed to initialize vsomeip: %s", 
                         initResult.Error().Message().c_str());
            return 1;
        }

        LAP_LOG_INFO("vsomeip initialized");

        // Step 2: Create service implementation
        auto service = std::make_shared<CalculatorServiceImpl>();

        // Step 3: Create SOME/IP adapter
        lap::com::commonapi::SomeIpStubAdapter<CalculatorServiceImpl> 
            adapter("local", "Calculator");

        // Step 4: Register service
        auto regResult = adapter.Initialize(service);
        if (!regResult.HasValue()) {
            LAP_LOG_ERROR("Failed to register service: %s", 
                         regResult.Error().Message().c_str());
            return 1;
        }

        LAP_LOG_INFO("Service registered: com.lightap.example.Calculator");
        LAP_LOG_INFO("Service ID: 0x1234, Instance: 0x5678");

        // Step 5: Start vsomeip application in separate thread
        std::thread vsomeipThread([&connMgr]() {
            auto startResult = connMgr.Start(true); // Blocking call
            if (!startResult.HasValue()) {
                LAP_LOG_ERROR("vsomeip start failed: %s", 
                             startResult.Error().Message().c_str());
            }
        });

        LAP_LOG_INFO("Service started. Press Ctrl+C to stop...");
        LAP_LOG_INFO("Configuration: VSOMEIP_CONFIGURATION environment variable");
        LAP_LOG_INFO("               or tools/someip/vsomeip_calculator.json");

        // Main loop - keep service running
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // Step 6: Graceful shutdown
        LAP_LOG_INFO("Shutting down...");
        
        connMgr.Stop();
        vsomeipThread.join();
        
        adapter.Deinitialize();
        connMgr.Deinitialize();

        LAP_LOG_INFO("Service stopped successfully");

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
    LAP_LOG_INFO("vsomeip Setup:");
    LAP_LOG_INFO("1. Install vsomeip library:");
    LAP_LOG_INFO("   git clone https://github.com/COVESA/vsomeip.git");
    LAP_LOG_INFO("   cd vsomeip && mkdir build && cd build");
    LAP_LOG_INFO("   cmake .. && make && sudo make install");
    LAP_LOG_INFO("");
    LAP_LOG_INFO("2. Set environment variable:");
    LAP_LOG_INFO("   export VSOMEIP_CONFIGURATION=<path>/vsomeip_calculator.json");
    LAP_LOG_INFO("");
    LAP_LOG_INFO("3. Install CommonAPI SOME/IP runtime:");
    LAP_LOG_INFO("   See: https://github.com/COVESA/capicxx-someip-runtime");
#endif

    return 0;
}
