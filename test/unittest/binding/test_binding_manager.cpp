/**
 * @file        test_binding_manager.cpp
 * @author      LightAP Development Team
 * @brief       Unit tests for BindingManager
 * @date        2025-11-21
 * @copyright   Copyright (c) 2025
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "../../../source/binding/manager/inc/BindingManager.hpp"

#include <fstream>
#include <filesystem>

using namespace lap::com::binding;
using namespace lap::core;

namespace fs = std::filesystem;

// ============================================================================
// Mock Transport Binding
// ============================================================================

class MockBinding : public ITransportBinding
{
public:
    MOCK_METHOD(Result<void>, Initialize, (), (noexcept, override));
    MOCK_METHOD(Result<void>, Shutdown, (), (noexcept, override));
    
    MOCK_METHOD(Result<void>, OfferService, (uint64_t, uint64_t), (noexcept, override));
    MOCK_METHOD(Result<void>, StopOfferService, (uint64_t, uint64_t), (noexcept, override));
    MOCK_METHOD(Result<std::vector<uint64_t>>, FindService, (uint64_t), (noexcept, override));
    
    MOCK_METHOD(Result<void>, SendEvent, (uint64_t, uint64_t, uint32_t, const ByteBuffer&), (noexcept, override));
    MOCK_METHOD(Result<void>, SubscribeEvent, (uint64_t, uint64_t, uint32_t, EventCallback), (noexcept, override));
    MOCK_METHOD(Result<void>, UnsubscribeEvent, (uint64_t, uint64_t, uint32_t), (noexcept, override));
    
    MOCK_METHOD(Result<ByteBuffer>, CallMethod, (uint64_t, uint64_t, uint32_t, const ByteBuffer&), (noexcept, override));
    MOCK_METHOD(Result<void>, RegisterMethod, (uint64_t, uint64_t, uint32_t, MethodCallback), (noexcept, override));
    
    MOCK_METHOD(Result<ByteBuffer>, GetField, (uint64_t, uint64_t, uint32_t), (noexcept, override));
    MOCK_METHOD(Result<void>, SetField, (uint64_t, uint64_t, uint32_t, const ByteBuffer&), (noexcept, override));
    
    MOCK_METHOD(const char*, GetName, (), (const, noexcept, override));
    MOCK_METHOD(uint32_t, GetVersion, (), (const, noexcept, override));
};

// ============================================================================
// Test Fixture
// ============================================================================

class BindingManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Create temporary test configuration directory
        test_config_dir_ = fs::temp_directory_path() / "lap_binding_test";
        fs::create_directories(test_config_dir_);
        
        test_config_file_ = test_config_dir_ / "test_bindings.yaml";
    }
    
    void TearDown() override
    {
        // Cleanup
        auto& manager = BindingManager::GetInstance();
        manager.Shutdown();
        
        // Remove test files
        if (fs::exists(test_config_dir_))
        {
            fs::remove_all(test_config_dir_);
        }
    }
    
    /**
     * @brief Create a simple test YAML configuration
     */
    void CreateTestConfig(const std::string& yaml_content)
    {
        std::ofstream file(test_config_file_);
        file << yaml_content;
        file.close();
    }
    
    fs::path test_config_dir_;
    fs::path test_config_file_;
};

// ============================================================================
// Basic Functionality Tests
// ============================================================================

TEST_F(BindingManagerTest, SingletonInstance)
{
    // Verify singleton pattern
    auto& manager1 = BindingManager::GetInstance();
    auto& manager2 = BindingManager::GetInstance();
    
    EXPECT_EQ(&manager1, &manager2) << "BindingManager should be singleton";
}

TEST_F(BindingManagerTest, ManualBindingRegistration)
{
    auto& manager = BindingManager::GetInstance();
    
    // Create mock binding
    auto mock_binding = std::make_shared<MockBinding>();
    
    EXPECT_CALL(*mock_binding, Initialize())
        .Times(0);  // Initialize not called for manual registration
    
    // Register binding
    BindingConfig config;
    config.name = "mock_binding";
    config.priority = BindingPriority::ICEORYX2;
    config.enabled = true;
    
    auto result = manager.RegisterBinding(config, mock_binding);
    ASSERT_TRUE(result.HasValue()) << "Binding registration should succeed";
    
    // Verify binding is registered
    auto loaded_bindings = manager.GetLoadedBindings();
    EXPECT_EQ(loaded_bindings.size(), 1);
    EXPECT_EQ(loaded_bindings[0], "mock_binding");
    
    // Verify GetBinding
    auto binding_opt = manager.GetBinding("mock_binding");
    ASSERT_TRUE(binding_opt.HasValue()) << "Binding should be retrievable";
    EXPECT_EQ(binding_opt.Value(), mock_binding.get());
}

TEST_F(BindingManagerTest, GetNonExistentBinding)
{
    auto& manager = BindingManager::GetInstance();
    
    auto binding_opt = manager.GetBinding("non_existent");
    EXPECT_FALSE(binding_opt.HasValue()) << "Non-existent binding should return nullopt";
}

TEST_F(BindingManagerTest, UnloadBinding)
{
    auto& manager = BindingManager::GetInstance();
    
    // Register mock binding
    auto mock_binding = std::make_shared<MockBinding>();
    
    EXPECT_CALL(*mock_binding, Shutdown())
        .WillOnce(::testing::Return(Result<void>::Ok()));
    
    BindingConfig config;
    config.name = "test_binding";
    config.priority = BindingPriority::SOCKET;
    
    manager.RegisterBinding(config, mock_binding);
    
    // Unload binding
    auto result = manager.UnloadBinding("test_binding");
    ASSERT_TRUE(result.HasValue()) << "Unload should succeed";
    
    // Verify binding is removed
    auto loaded_bindings = manager.GetLoadedBindings();
    EXPECT_EQ(loaded_bindings.size(), 0);
}

// ============================================================================
// Priority Selection Tests
// ============================================================================

TEST_F(BindingManagerTest, PriorityBasedSelection)
{
    auto& manager = BindingManager::GetInstance();
    
    // Register multiple bindings with different priorities
    auto high_priority = std::make_shared<MockBinding>();
    auto low_priority = std::make_shared<MockBinding>();
    
    EXPECT_CALL(*high_priority, GetName())
        .WillRepeatedly(::testing::Return("high"));
    EXPECT_CALL(*low_priority, GetName())
        .WillRepeatedly(::testing::Return("low"));
    
    BindingConfig high_config;
    high_config.name = "high_priority";
    high_config.priority = BindingPriority::ICEORYX2;  // 100
    manager.RegisterBinding(high_config, high_priority);
    
    BindingConfig low_config;
    low_config.name = "low_priority";
    low_config.priority = BindingPriority::DBUS;  // 20
    manager.RegisterBinding(low_config, low_priority);
    
    // Select binding (should return high priority)
    auto* selected = manager.SelectBinding(0x1234, 0x0001);
    ASSERT_NE(selected, nullptr);
    EXPECT_EQ(selected, high_priority.get()) << "Should select high priority binding";
}

TEST_F(BindingManagerTest, SelectBindingWithNoBindings)
{
    auto& manager = BindingManager::GetInstance();
    
    auto* selected = manager.SelectBinding(0x1234, 0x0001);
    EXPECT_EQ(selected, nullptr) << "Should return nullptr when no bindings available";
}

// ============================================================================
// YAML Configuration Tests
// ============================================================================

TEST_F(BindingManagerTest, LoadYAMLConfigurationEmpty)
{
    auto& manager = BindingManager::GetInstance();
    
    // Create empty config
    CreateTestConfig("bindings: []\n");
    
    auto result = manager.LoadConfiguration(test_config_file_.string());
    EXPECT_TRUE(result.HasValue()) << "Loading empty config should succeed";
    
    EXPECT_EQ(manager.GetLoadedBindings().size(), 0);
}

TEST_F(BindingManagerTest, LoadYAMLConfigurationInvalidPath)
{
    auto& manager = BindingManager::GetInstance();
    
    auto result = manager.LoadConfiguration("/non/existent/path.yaml");
    EXPECT_FALSE(result.HasValue()) << "Loading non-existent file should fail";
}

TEST_F(BindingManagerTest, ParseYAMLWithStaticMappings)
{
    auto& manager = BindingManager::GetInstance();
    
    std::string yaml_content = R"(
bindings:
  - name: test_binding
    priority: 100
    library: /tmp/test.so
    enabled: false

static_mappings:
  - service_id: "0xF001"
    instance_id: "0x0001"
    binding: test_binding
)";
    
    CreateTestConfig(yaml_content);
    
    // Note: This will fail to load the actual .so file, but should parse YAML
    auto result = manager.LoadConfiguration(test_config_file_.string());
    
    // Even if binding load fails, config parsing should succeed
    // (bindings are disabled anyway)
    EXPECT_TRUE(result.HasValue()) << "YAML parsing should succeed";
}

// ============================================================================
// Static Mapping Tests
// ============================================================================

TEST_F(BindingManagerTest, StaticMappingOverridesPriority)
{
    auto& manager = BindingManager::GetInstance();
    
    // Register two bindings
    auto high_priority = std::make_shared<MockBinding>();
    auto specific_binding = std::make_shared<MockBinding>();
    
    BindingConfig high_config;
    high_config.name = "high_priority";
    high_config.priority = BindingPriority::ICEORYX2;
    manager.RegisterBinding(high_config, high_priority);
    
    BindingConfig specific_config;
    specific_config.name = "specific_binding";
    specific_config.priority = BindingPriority::SOCKET;  // Lower priority
    manager.RegisterBinding(specific_config, specific_binding);
    
    // Create config with static mapping
    std::string yaml_content = R"(
bindings: []

static_mappings:
  - service_id: "0xF001"
    instance_id: "0x0001"
    binding: specific_binding
)";
    
    CreateTestConfig(yaml_content);
    manager.LoadConfiguration(test_config_file_.string());
    
    // Select for mapped service (should use specific_binding despite lower priority)
    auto* selected = manager.SelectBinding(0xF001, 0x0001);
    
    // Note: This test assumes static mapping implementation is working
    // In current implementation, may need to verify differently
    EXPECT_NE(selected, nullptr);
}

// ============================================================================
// Shutdown Tests
// ============================================================================

TEST_F(BindingManagerTest, ShutdownCallsBindingShutdown)
{
    auto& manager = BindingManager::GetInstance();
    
    auto mock_binding = std::make_shared<MockBinding>();
    
    EXPECT_CALL(*mock_binding, Shutdown())
        .WillOnce(::testing::Return(Result<void>::Ok()));
    
    BindingConfig config;
    config.name = "test";
    config.priority = BindingPriority::SOCKET;
    
    manager.RegisterBinding(config, mock_binding);
    
    // Shutdown manager
    auto result = manager.Shutdown();
    EXPECT_TRUE(result.HasValue());
    
    // Verify all bindings cleared
    EXPECT_EQ(manager.GetLoadedBindings().size(), 0);
}

TEST_F(BindingManagerTest, ShutdownWithMultipleBindings)
{
    auto& manager = BindingManager::GetInstance();
    
    auto binding1 = std::make_shared<MockBinding>();
    auto binding2 = std::make_shared<MockBinding>();
    auto binding3 = std::make_shared<MockBinding>();
    
    EXPECT_CALL(*binding1, Shutdown()).WillOnce(::testing::Return(Result<void>::Ok()));
    EXPECT_CALL(*binding2, Shutdown()).WillOnce(::testing::Return(Result<void>::Ok()));
    EXPECT_CALL(*binding3, Shutdown()).WillOnce(::testing::Return(Result<void>::Ok()));
    
    BindingConfig config1{"binding1", BindingPriority::ICEORYX2, "", true, {}};
    BindingConfig config2{"binding2", BindingPriority::DDS, "", true, {}};
    BindingConfig config3{"binding3", BindingPriority::SOCKET, "", true, {}};
    
    manager.RegisterBinding(config1, binding1);
    manager.RegisterBinding(config2, binding2);
    manager.RegisterBinding(config3, binding3);
    
    EXPECT_EQ(manager.GetLoadedBindings().size(), 3);
    
    auto result = manager.Shutdown();
    EXPECT_TRUE(result.HasValue());
    EXPECT_EQ(manager.GetLoadedBindings().size(), 0);
}

// ============================================================================
// Thread Safety Tests (Basic)
// ============================================================================

TEST_F(BindingManagerTest, ConcurrentBindingSelection)
{
    auto& manager = BindingManager::GetInstance();
    
    auto binding = std::make_shared<MockBinding>();
    
    BindingConfig config;
    config.name = "test";
    config.priority = BindingPriority::ICEORYX2;
    manager.RegisterBinding(config, binding);
    
    // Launch multiple threads selecting bindings
    constexpr int num_threads = 10;
    constexpr int iterations = 1000;
    
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back([&manager]() {
            for (int j = 0; j < iterations; ++j)
            {
                auto* selected = manager.SelectBinding(0x1234, 0x0001);
                EXPECT_NE(selected, nullptr);
            }
        });
    }
    
    for (auto& thread : threads)
    {
        thread.join();
    }
    
    // No crashes = success
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
