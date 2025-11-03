/**
 * @file        test_runtime_basic.cpp
 * @brief       Basic test for Com Runtime API
 * @date        2025-10-30
 */

#include "../../source/inc/ara_com.hpp"
#include <iostream>

int main()
{
    std::cout << "=== AUTOSAR Communication Management Runtime Test ===" << std::endl;
    std::cout << "Version: " << lap::com::Version::Major << "." 
              << lap::com::Version::Minor << "." 
              << lap::com::Version::Patch << std::endl;
    std::cout << "Specification: " << lap::com::Version::Specification << std::endl;
    
    // Test 1: Initialize Runtime
    std::cout << "\n[Test 1] Initializing Runtime..." << std::endl;
    auto initResult = lap::com::Runtime::Initialize();
    if (initResult.HasValue())
    {
        std::cout << "✓ Runtime initialized successfully" << std::endl;
    }
    else
    {
        std::cerr << "✗ Failed to initialize Runtime" << std::endl;
        return 1;
    }
    
    // Test 2: Runtime is already initialized (should fail gracefully)
    std::cout << "\n[Test 2] Attempting to initialize again..." << std::endl;
    auto reinitResult = lap::com::Runtime::Initialize();
    if (!reinitResult.HasValue())
    {
        std::cout << "✓ Correctly rejected duplicate initialization" << std::endl;
    }
    else
    {
        std::cout << "⚠ Allowed duplicate initialization (unexpected)" << std::endl;
    }
    
    // Test 3: Create InstanceSpecifier
    std::cout << "\n[Test 3] Creating InstanceSpecifier..." << std::endl;
    auto instanceSpec = lap::core::InstanceSpecifier::Create("/test/service/instance");
    if (instanceSpec.HasValue())
    {
        std::cout << "✓ InstanceSpecifier created: " << instanceSpec.Value().ToString() << std::endl;
    }
    else
    {
        std::cerr << "✗ Failed to create InstanceSpecifier" << std::endl;
    }
    
    // Test 4: Deinitialize Runtime
    std::cout << "\n[Test 4] Deinitializing Runtime..." << std::endl;
    auto deinitResult = lap::com::Runtime::Deinitialize();
    if (deinitResult.HasValue())
    {
        std::cout << "✓ Runtime deinitialized successfully" << std::endl;
    }
    else
    {
        std::cerr << "✗ Failed to deinitialize Runtime" << std::endl;
        return 1;
    }
    
    std::cout << "\n=== All tests passed! ===" << std::endl;
    return 0;
}
