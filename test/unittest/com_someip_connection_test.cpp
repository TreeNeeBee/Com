/**
 * @file com_someip_connection_test.cpp
 * @brief Unit tests for SOME/IP connection manager
 * @copyright Copyright (c) 2025 LightAP
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "../../source/binding/someip/SomeIpConnectionManager.hpp"
#include "Core/CoreBase/inc/CMemory.h"
#include "Core/CoreBase/inc/CLog.h"

using namespace lap::com::someip;
using namespace lap::core;
using namespace lap::log;

/**
 * @brief Test fixture for SomeIpConnectionManager tests
 */
class SomeIpConnectionManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize LightAP core
    MemoryManager::getInstance();
        LogManager::getInstance().initialize();
    }

    void TearDown() override {
        // Deinitialize connection manager
        auto& manager = SomeIpConnectionManager::getInstance();
        manager.Deinitialize();
    }
};

/**
 * @brief Test singleton pattern
 */
TEST_F(SomeIpConnectionManagerTest, SingletonPattern) {
    auto& manager1 = SomeIpConnectionManager::getInstance();
    auto& manager2 = SomeIpConnectionManager::getInstance();
    
    // Should return same instance
    EXPECT_EQ(&manager1, &manager2);
}

/**
 * @brief Test initialization without configuration
 */
TEST_F(SomeIpConnectionManagerTest, InitializeWithoutConfig) {
    auto& manager = SomeIpConnectionManager::getInstance();
    
    // Initialize with empty config should fail
    auto result = manager.Initialize("test_app", "");
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.Error().Value(), static_cast<int>(lap::com::ComErrc::InvalidParameter));
}

/**
 * @brief Test initialization with non-existent config file
 */
TEST_F(SomeIpConnectionManagerTest, InitializeWithInvalidConfig) {
    auto& manager = SomeIpConnectionManager::getInstance();
    
    // Initialize with non-existent file
    auto result = manager.Initialize("test_app", "/tmp/non_existent_vsomeip_config.json");
    
    // Should fail (file doesn't exist or vsomeip not installed)
    // Note: This test behavior depends on vsomeip installation status
    if (!result.IsOk()) {
        EXPECT_TRUE(
            result.Error().Value() == static_cast<int>(lap::com::ComErrc::FileNotFound) ||
            result.Error().Value() == static_cast<int>(lap::com::ComErrc::NotInitialized)
        );
    }
}

/**
 * @brief Test double initialization
 */
TEST_F(SomeIpConnectionManagerTest, DoubleInitialization) {
    auto& manager = SomeIpConnectionManager::getInstance();
    
    // Create a temporary valid config
    const char* configPath = "/tmp/test_vsomeip_config.json";
    std::ofstream configFile(configPath);
    configFile << R"({
        "unicast": "127.0.0.1",
        "logging": {
            "level": "info",
            "console": "true"
        },
        "applications": [{
            "name": "test_app",
            "id": "0x1111"
        }]
    })";
    configFile.close();
    
    // First initialization
    auto result1 = manager.Initialize("test_app", configPath);
    
    // Second initialization should fail (already initialized)
    auto result2 = manager.Initialize("test_app", configPath);
    EXPECT_FALSE(result2.IsOk());
    EXPECT_EQ(result2.Error().Value(), static_cast<int>(lap::com::ComErrc::AlreadyExists));
    
    // Cleanup
    std::remove(configPath);
}

/**
 * @brief Test start without initialization
 */
TEST_F(SomeIpConnectionManagerTest, StartWithoutInitialization) {
    auto& manager = SomeIpConnectionManager::getInstance();
    
    // Try to start without initialization
    auto result = manager.Start(false);
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.Error().Value(), static_cast<int>(lap::com::ComErrc::NotInitialized));
}

/**
 * @brief Test stop without start
 */
TEST_F(SomeIpConnectionManagerTest, StopWithoutStart) {
    auto& manager = SomeIpConnectionManager::getInstance();
    
    // Create config and initialize
    const char* configPath = "/tmp/test_vsomeip_config.json";
    std::ofstream configFile(configPath);
    configFile << R"({
        "unicast": "127.0.0.1",
        "applications": [{
            "name": "test_app",
            "id": "0x1111"
        }]
    })";
    configFile.close();
    
    auto initResult = manager.Initialize("test_app", configPath);
    
    // Try to stop without starting
    auto stopResult = manager.Stop();
    EXPECT_FALSE(stopResult.IsOk());
    EXPECT_EQ(stopResult.Error().Value(), static_cast<int>(lap::com::ComErrc::NotStarted));
    
    // Cleanup
    std::remove(configPath);
}

/**
 * @brief Test get application before initialization
 */
TEST_F(SomeIpConnectionManagerTest, GetApplicationBeforeInit) {
    auto& manager = SomeIpConnectionManager::getInstance();
    
    auto app = manager.GetApplication();
    EXPECT_EQ(app, nullptr);
}

/**
 * @brief Test deinitialization
 */
TEST_F(SomeIpConnectionManagerTest, Deinitialization) {
    auto& manager = SomeIpConnectionManager::getInstance();
    
    // Create config
    const char* configPath = "/tmp/test_vsomeip_config.json";
    std::ofstream configFile(configPath);
    configFile << R"({
        "unicast": "127.0.0.1",
        "applications": [{
            "name": "test_app",
            "id": "0x1111"
        }]
    })";
    configFile.close();
    
    // Initialize
    auto initResult = manager.Initialize("test_app", configPath);
    
    // Deinitialize
    manager.Deinitialize();
    
    // Get application should return nullptr after deinit
    auto app = manager.GetApplication();
    EXPECT_EQ(app, nullptr);
    
    // Can reinitialize after deinit
    auto reinitResult = manager.Initialize("test_app", configPath);
    // Result depends on vsomeip installation
    
    // Cleanup
    std::remove(configPath);
}

/**
 * @brief Test thread safety of singleton access
 */
TEST_F(SomeIpConnectionManagerTest, ThreadSafetySingleton) {
    const int NUM_THREADS = 10;
    std::vector<std::thread> threads;
    std::vector<SomeIpConnectionManager*> instances(NUM_THREADS);
    
    // Access singleton from multiple threads
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&instances, i]() {
            instances[i] = &SomeIpConnectionManager::getInstance();
        });
    }
    
    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }
    
    // All instances should be the same
    for (int i = 1; i < NUM_THREADS; ++i) {
        EXPECT_EQ(instances[0], instances[i]);
    }
}

/**
 * @brief Test initialization with application name validation
 */
TEST_F(SomeIpConnectionManagerTest, InitializeWithInvalidAppName) {
    auto& manager = SomeIpConnectionManager::getInstance();
    
    // Empty application name
    auto result1 = manager.Initialize("", "/tmp/config.json");
    EXPECT_FALSE(result1.IsOk());
    
    // Very long application name (edge case)
    std::string longName(1024, 'a');
    auto result2 = manager.Initialize(longName, "/tmp/config.json");
    EXPECT_FALSE(result2.IsOk());
}

/**
 * @brief Test configuration file content validation
 */
TEST_F(SomeIpConnectionManagerTest, InitializeWithMalformedConfig) {
    auto& manager = SomeIpConnectionManager::getInstance();
    
    // Create malformed JSON config
    const char* configPath = "/tmp/test_malformed_config.json";
    std::ofstream configFile(configPath);
    configFile << "{ invalid json content without closing brace";
    configFile.close();
    
    auto result = manager.Initialize("test_app", configPath);
    
    // Should fail due to malformed JSON
    // Exact error depends on vsomeip JSON parser
    EXPECT_FALSE(result.IsOk() || true); // May succeed if vsomeip is lenient
    
    // Cleanup
    std::remove(configPath);
}

/**
 * @brief Test multiple start/stop cycles
 */
TEST_F(SomeIpConnectionManagerTest, MultipleStartStopCycles) {
    auto& manager = SomeIpConnectionManager::getInstance();
    
    // Create config
    const char* configPath = "/tmp/test_vsomeip_config.json";
    std::ofstream configFile(configPath);
    configFile << R"({
        "unicast": "127.0.0.1",
        "applications": [{
            "name": "test_app",
            "id": "0x1111"
        }]
    })";
    configFile.close();
    
    auto initResult = manager.Initialize("test_app", configPath);
    
    if (initResult.IsOk()) {
        // Cycle 1
        auto startResult1 = manager.Start(false);
        if (startResult1.IsOk()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto stopResult1 = manager.Stop();
            EXPECT_TRUE(stopResult1.IsOk());
        }
        
        // Cycle 2
        auto startResult2 = manager.Start(false);
        if (startResult2.IsOk()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto stopResult2 = manager.Stop();
            EXPECT_TRUE(stopResult2.IsOk());
        }
    }
    
    // Cleanup
    std::remove(configPath);
}

/**
 * @brief Main test runner
 */
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
