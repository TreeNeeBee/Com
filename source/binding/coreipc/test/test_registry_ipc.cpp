/**
 * @file        test_registry_ipc.cpp
 * @brief       Minimal test: CRegistryDispatcher + CRegistryProxy IPC only
 */
#include "CRegistryDispatcher.hpp"
#include "CRegistryProxy.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace lap::com::registry;

int main()
{
    std::cout << "=== Registry IPC minimal test ===" << std::endl;

    // 1. Create and initialize dispatcher
    CRegistryDispatcher dispatcher;
    auto initRes = dispatcher.Initialize();
    if ( !initRes.HasValue() ) {
        std::cerr << "Dispatcher init FAILED: " << initRes.Error().Message() << std::endl;
        return 1;
    }
    std::cout << "[OK] Dispatcher initialized" << std::endl;

    // 2. Run dispatcher in background
    std::thread dispThread( [&dispatcher]() {
        auto runRes = dispatcher.Run();
        if ( !runRes.HasValue() ) {
            std::cerr << "[WARN] Dispatcher Run exited: " << runRes.Error().Message() << std::endl;
        }
    } );

    // Wait for dispatcher to enter event loop
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
    std::cout << "[OK] Dispatcher running" << std::endl;

    // 3. Create and initialize proxy
    CRegistryProxy proxy;
    auto proxyInit = proxy.Initialize();
    if ( !proxyInit.HasValue() ) {
        std::cerr << "Proxy init FAILED: " << proxyInit.Error().Message() << std::endl;
        dispatcher.Shutdown();
        if ( dispThread.joinable() ) dispThread.join();
        return 1;
    }
    std::cout << "[OK] Proxy initialized" << std::endl;

    // Wait for scanner to discover channels
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

    // 4. Register a service
    std::cout << "\nRegistering service 0x5678..." << std::endl;
    auto regResult = proxy.RegisterService(
        0x5678,     // serviceId
        0x0001,     // instanceId
        1,          // majorVersion
        0,          // minorVersion
        "coreipc",  // bindingType
        "local",    // endpoint
        10000       // timeoutMs (10 seconds)
    );

    if ( regResult.HasValue() ) {
        std::cout << "[PASS] RegisterService returned slot=" << regResult.Value() << std::endl;
    } else {
        std::cerr << "[FAIL] RegisterService: " << regResult.Error().Message() << std::endl;
    }

    // 5. Cleanup
    dispatcher.Shutdown();
    if ( dispThread.joinable() ) dispThread.join();
    std::cout << "\n[OK] Cleanup complete" << std::endl;

    return regResult.HasValue() ? 0 : 1;
}
