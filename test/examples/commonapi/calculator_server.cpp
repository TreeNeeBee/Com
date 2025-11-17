/**
 * @file calculator_server.cpp
 * @brief Calculator service implementation using CommonAPI generated code
 * 
 * This example demonstrates:
 * - Implementing a CommonAPI Stub (server-side)
 * - Using StubAdapter to integrate with LightAP
 * - Overriding generated virtual methods
 * - LAP_LOG integration
 */

#include <memory>
#include <thread>
#include <chrono>

// LightAP Core
#include <Core/CoreBase/inc/CMemory.h>
#include <Core/CoreBase/inc/CLog.h>

// CommonAPI generated headers (will be available after running generate.sh)
// #include <v1/com/lightap/example/CalculatorStubDefault.hpp>

// LightAP CommonAPI-DBus adapter
#include <binding/commonapi/CommonAPIDBusAdapter.hpp>

using namespace lap::core;
using namespace lap::log;

// TODO: Uncomment after code generation
#if 0
/**
 * @class CalculatorServiceImpl
 * @brief Implementation of Calculator interface
 * 
 * Generated interface (from Calculator.fidl):
 * - method calculate(Float a, Float b, String op) -> (Float result, Int32 errorCode)
 * - attribute currentValue (readonly)
 * - broadcast resultReady(Float result)
 */
class CalculatorServiceImpl : public v1::com::lightap::example::CalculatorStubDefault {
public:
    CalculatorServiceImpl() : m_currentValue(0.0f) {
        LAP_LOG_INFO("CalculatorServiceImpl: Created");
    }

    ~CalculatorServiceImpl() override {
        LAP_LOG_INFO("CalculatorServiceImpl: Destroyed");
    }

    /**
     * @brief Perform calculation
     * @param a First operand
     * @param b Second operand
     * @param op Operation ("+", "-", "*", "/")
     * @param result Calculation result (out parameter)
     * @param errorCode Error code: 0=success, 1=invalid_op, 2=divide_by_zero (out parameter)
     */
    void calculate(const float& a, const float& b, const std::string& op, 
                   float& result, int32_t& errorCode) override {
        LAP_LOG_INFO("calculate(%f, %s, %f)", a, op.c_str(), b);

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

        // Fire broadcast (optional)
        fireResultReadyEvent(result);

        LAP_LOG_INFO("Result: %f (error=%d)", result, errorCode);
    }

    /**
     * @brief Get current value (attribute getter)
     * Called when client reads 'currentValue' attribute
     */
    const float& getCurrentValueAttribute() override {
        LAP_LOG_DEBUG("getCurrentValueAttribute() -> %f", m_currentValue);
        return m_currentValue;
    }

    /**
     * @brief Fire broadcast to notify all subscribers
     */
    void notifyResult(float value) {
        m_currentValue = value;
        fireResultReadyEvent(value);
        LAP_LOG_INFO("Broadcast: resultReady(%f)", value);
    }

private:
    float m_currentValue; ///< Current calculation result
};
#endif

/**
 * @brief Main entry point
 */
int main(int argc, char** argv) {
    // Initialize LightAP core (required for logging)
    MemoryManager::getInstance();
    LogManager::getInstance().initialize();

    LAP_LOG_INFO("=== Calculator Service (CommonAPI) ===");

// TODO: Uncomment after code generation
#if 0
    try {
        // Create service implementation
        auto service = std::make_shared<CalculatorServiceImpl>();

        // Create adapter (domain="local", instance="Calculator")
        lap::com::commonapi::StubAdapter<CalculatorServiceImpl> adapter("local", "Calculator");

        // Initialize and register service with D-Bus
        auto result = adapter.Initialize(service);
        if (!result.HasValue()) {
            LAP_LOG_ERROR("Failed to initialize service: %s", result.Error().Message().c_str());
            return 1;
        }

        LAP_LOG_INFO("Service started at: com.lightap.example.Calculator");
        LAP_LOG_INFO("Press Ctrl+C to stop...");

        // Keep service running
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            // Optional: Periodically broadcast current value
            // service->notifyResult(service->getCurrentValueAttribute());
        }

        // Cleanup handled automatically by StubAdapter destructor
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
