/**
 * @file        calculator_server.cpp
 * @brief       Socket Calculator Service Example (Server)
 * @author      LightAP Team
 * @date        2025-10-30
 * @details     Example server using Protobuf over Unix Domain Socket
 */

#include <binding/socket/SocketMethodBinding.hpp>
#include <log/CLog.hpp>
#include <log/CLogManager.hpp>
#include <core/CMemory.hpp>
#include <iostream>
#include <csignal>
#include <atomic>
#include <string>

// Include generated protobuf header
#include "../../tools/protobuf/generated/calculator.pb.h"

using namespace lap::com::binding::socket;
using namespace lap::com::example;

// Global running flag
std::atomic<bool> g_running(true);

// Signal handler for graceful shutdown
void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        // Use stderr here to avoid assertions if LogManager was already uninitialized
        std::fprintf(stderr, "[CalculatorServer] Received shutdown signal\n");
        g_running = false;
    }
}

/**
 * @brief Calculator service handler
 * @param request Calculate request
 * @return Calculate response
 */
CalculateResponse handleCalculate(const CalculateRequest& request) {
    CalculateResponse response;
    
    LAP_LOG_INFO("COM.SOCKET.Example") << "[CalculatorServer] Received request: operand1=" << request.operand1()
                                        << ", operation=" << request.operation().c_str()
                                        << ", operand2=" << request.operand2();
    
    // Perform calculation based on operation
    if (request.operation() == "add") {
        response.set_result(request.operand1() + request.operand2());
        response.set_error_code(0);
        
    } else if (request.operation() == "subtract") {
        response.set_result(request.operand1() - request.operand2());
        response.set_error_code(0);
        
    } else if (request.operation() == "multiply") {
        response.set_result(request.operand1() * request.operand2());
        response.set_error_code(0);
        
    } else if (request.operation() == "divide") {
        if (request.operand2() != 0) {
            response.set_result(request.operand1() / request.operand2());
            response.set_error_code(0);
        } else {
            response.set_result(0);
            response.set_error_message("Division by zero");
            response.set_error_code(-1);
        }
        
    } else {
        response.set_result(0);
        response.set_error_message("Unknown operation: " + request.operation());
        response.set_error_code(-2);
    }
    
    LAP_LOG_INFO("COM.SOCKET.Example") << "[CalculatorServer] Sending response: result=" << response.result()
                                        << ", error_code=" << response.error_code();
    
    return response;
}

int main(int argc, char* argv[]) {
    // Initialize memory manager first to avoid static destruction issues
    (void)::lap::core::MemoryManager::getInstance();
    // Initialize logging (safe even without config; defaults to console)
    ::lap::log::LogManager::getInstance().initialize();
    // Register signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    LAP_LOG_INFO("COM.SOCKET.Example") << "========================================";
    LAP_LOG_INFO("COM.SOCKET.Example") << "  Calculator Server (Socket + Protobuf)";
    LAP_LOG_INFO("COM.SOCKET.Example") << "========================================";
    
    // Initialize socket connection manager
    auto& manager = SocketConnectionManager::GetInstance();
    auto initResult = manager.initialize();
    if (!initResult.HasValue()) {
        LAP_LOG_ERROR("COM.SOCKET.Example") << "[CalculatorServer] Failed to initialize socket manager: "
                                             << initResult.Error().Message();
        return 1;
    }
    
    // Configure server endpoint
    const char* socketPath = (argc > 1) ? argv[1] : "/tmp/calculator.sock";
    
    SocketEndpoint endpoint{
        .socketPath = socketPath,
        .mode = SocketTransportMode::kStream,
        .maxMessageSize = 65536,
        .sendBufferSize = 8192,
        .recvBufferSize = 8192,
        .reuseAddr = true,
        .listenBacklog = 128
    };
    
    LAP_LOG_INFO("COM.SOCKET.Example") << "[CalculatorServer] Socket path: " << endpoint.socketPath.c_str();
    LAP_LOG_INFO("COM.SOCKET.Example") << "[CalculatorServer] Max message size: " << endpoint.maxMessageSize << " bytes";
    LAP_LOG_INFO("COM.SOCKET.Example") << "[CalculatorServer] Listen backlog: " << endpoint.listenBacklog;
    
    // Create method responder
    SocketMethodResponder<CalculateRequest, CalculateResponse> responder(
        endpoint, handleCalculate);
    
    // Start service
    auto startResult = responder.start();
    if (!startResult.HasValue()) {
        LAP_LOG_ERROR("COM.SOCKET.Example") << "[CalculatorServer] Failed to start service: "
                                             << startResult.Error().Message();
        manager.deinitialize();
        return 1;
    }
    
    LAP_LOG_INFO("COM.SOCKET.Example") << "[CalculatorServer] Service started successfully";
    LAP_LOG_INFO("COM.SOCKET.Example") << "[CalculatorServer] Waiting for client connections...";
    LAP_LOG_INFO("COM.SOCKET.Example") << "[CalculatorServer] Press Ctrl+C to stop";
    
    // Wait for shutdown signal
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Shutdown
    LAP_LOG_INFO("COM.SOCKET.Example") << "[CalculatorServer] Shutting down...";
    responder.stop();
    manager.deinitialize();
    // Log before uninitializing LogManager to avoid asserts in logging macros
    LAP_LOG_INFO("COM.SOCKET.Example") << "[CalculatorServer] Server stopped";
    ::lap::log::LogManager::getInstance().uninitialize();
    
    return 0;
}
