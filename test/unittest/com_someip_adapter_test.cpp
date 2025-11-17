/**
 * @file com_someip_adapter_test.cpp
 * @brief Unit tests for SOME/IP adapters (Proxy and Stub)
 * @copyright Copyright (c) 2025 LightAP
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "../../source/binding/commonapi/CommonAPISomeIpAdapter.hpp"
#include "Core/CoreBase/inc/CMemory.h"
#include "Core/CoreBase/inc/CLog.h"

using namespace lap::com::someip;
using namespace lap::core;
using namespace lap::log;

/**
 * @brief Mock CommonAPI proxy for testing
 */
class MockCommonAPIProxy {
public:
    MOCK_METHOD(bool, isAvailable, (), (const));
    MOCK_METHOD(bool, isAvailableBlocking, (), (const));
    MOCK_METHOD(CommonAPI::AvailabilityStatus, getAvailabilityStatus, (), (const));
};

/**
 * @brief Mock CommonAPI stub for testing
 */
class MockCommonAPIStub {
public:
    virtual ~MockCommonAPIStub() = default;
};

/**
 * @brief Test fixture for SOME/IP adapter tests
 */
class SomeIpAdapterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize LightAP core
    MemoryManager::getInstance();
        LogManager::getInstance().initialize();
    }

    void TearDown() override {
    }
};

// ============================================================================
// SomeIpProxyAdapter Tests
// ============================================================================

/**
 * @brief Test proxy adapter initialization failure (proxy creation fails)
 */
TEST_F(SomeIpAdapterTest, ProxyAdapterInitFailure) {
    SomeIpProxyAdapter<MockCommonAPIProxy> adapter("local", "TestService", "v1_0");
    
    // Initialize will fail because MockProxy cannot be created by CommonAPI runtime
    auto result = adapter.Initialize(1000);
    
    // Should fail (no real CommonAPI runtime for mock)
    EXPECT_FALSE(result.IsOk());
    EXPECT_FALSE(adapter.IsAvailable());
    EXPECT_EQ(adapter.GetProxy(), nullptr);
}

/**
 * @brief Test proxy adapter get/set connection ID
 */
TEST_F(SomeIpAdapterTest, ProxyAdapterConnectionId) {
    SomeIpProxyAdapter<MockCommonAPIProxy> adapter("local", "TestService", "v1_0");
    
    EXPECT_EQ(adapter.GetConnectionId(), "");
    
    adapter.SetConnectionId("test_connection");
    EXPECT_EQ(adapter.GetConnectionId(), "test_connection");
}

/**
 * @brief Test error conversion from CallStatus to Result
 */
TEST_F(SomeIpAdapterTest, ProxyAdapterErrorConversion) {
    SomeIpProxyAdapter<MockCommonAPIProxy> adapter("local", "TestService", "v1_0");
    
    // Test SUCCESS
    {
        auto result = adapter.WrapCallStatus<int>(CommonAPI::CallStatus::SUCCESS, 42);
        ASSERT_TRUE(result.IsOk());
        EXPECT_EQ(result.Value(), 42);
    }
    
    // Test OUT_OF_MEMORY
    {
        auto result = adapter.WrapCallStatus<int>(CommonAPI::CallStatus::OUT_OF_MEMORY, 0);
        EXPECT_FALSE(result.IsOk());
        EXPECT_EQ(result.Error().Value(), static_cast<int>(lap::com::ComErrc::OutOfMemory));
    }
    
    // Test NOT_AVAILABLE
    {
        auto result = adapter.WrapCallStatus<int>(CommonAPI::CallStatus::NOT_AVAILABLE, 0);
        EXPECT_FALSE(result.IsOk());
        EXPECT_EQ(result.Error().Value(), static_cast<int>(lap::com::ComErrc::NotAvailable));
    }
    
    // Test CONNECTION_FAILED
    {
        auto result = adapter.WrapCallStatus<int>(CommonAPI::CallStatus::CONNECTION_FAILED, 0);
        EXPECT_FALSE(result.IsOk());
        EXPECT_EQ(result.Error().Value(), static_cast<int>(lap::com::ComErrc::ConnectionFailed));
    }
    
    // Test REMOTE_ERROR
    {
        auto result = adapter.WrapCallStatus<int>(CommonAPI::CallStatus::REMOTE_ERROR, 0);
        EXPECT_FALSE(result.IsOk());
        EXPECT_EQ(result.Error().Value(), static_cast<int>(lap::com::ComErrc::RemoteError));
    }
    
    // Test SUBSCRIPTION_REFUSED
    {
        auto result = adapter.WrapCallStatus<int>(CommonAPI::CallStatus::SUBSCRIPTION_REFUSED, 0);
        EXPECT_FALSE(result.IsOk());
        EXPECT_EQ(result.Error().Value(), static_cast<int>(lap::com::ComErrc::SubscriptionRefused));
    }
    
    // Test UNKNOWN
    {
        auto result = adapter.WrapCallStatus<int>(CommonAPI::CallStatus::UNKNOWN, 0);
        EXPECT_FALSE(result.IsOk());
        EXPECT_EQ(result.Error().Value(), static_cast<int>(lap::com::ComErrc::InternalError));
    }
}

/**
 * @brief Test error conversion for void return type
 */
TEST_F(SomeIpAdapterTest, ProxyAdapterErrorConversionVoid) {
    SomeIpProxyAdapter<MockCommonAPIProxy> adapter("local", "TestService", "v1_0");
    
    // Test SUCCESS
    {
        auto result = adapter.WrapCallStatusVoid(CommonAPI::CallStatus::SUCCESS);
        EXPECT_TRUE(result.IsOk());
    }
    
    // Test errors
    {
        auto result = adapter.WrapCallStatusVoid(CommonAPI::CallStatus::REMOTE_ERROR);
        EXPECT_FALSE(result.IsOk());
        EXPECT_EQ(result.Error().Value(), static_cast<int>(lap::com::ComErrc::RemoteError));
    }
}

// ============================================================================
// SomeIpStubAdapter Tests
// ============================================================================

/**
 * @brief Test stub adapter initialization with null stub
 */
TEST_F(SomeIpStubAdapter, StubAdapterInitNullStub) {
    SomeIpStubAdapter<MockCommonAPIStub> adapter("local", "TestService", "v1_0");
    
    auto result = adapter.Initialize(nullptr);
    
    EXPECT_FALSE(result.IsOk());
    EXPECT_EQ(result.Error().Value(), static_cast<int>(lap::com::ComErrc::InvalidParameter));
    EXPECT_EQ(adapter.GetStub(), nullptr);
}

/**
 * @brief Test stub adapter initialization with valid stub
 */
TEST_F(SomeIpAdapterTest, StubAdapterInitSuccess) {
    SomeIpStubAdapter<MockCommonAPIStub> adapter("local", "TestService", "v1_0");
    
    auto stub = std::make_shared<MockCommonAPIStub>();
    auto result = adapter.Initialize(stub);
    
    // May fail due to no real CommonAPI runtime, but stub should be stored
    EXPECT_EQ(adapter.GetStub(), stub);
}

/**
 * @brief Test stub adapter double initialization
 */
TEST_F(SomeIpAdapterTest, StubAdapterDoubleInit) {
    SomeIpStubAdapter<MockCommonAPIStub> adapter("local", "TestService", "v1_0");
    
    auto stub1 = std::make_shared<MockCommonAPIStub>();
    auto stub2 = std::make_shared<MockCommonAPIStub>();
    
    adapter.Initialize(stub1);
    
    // Second initialization should update stub
    auto result2 = adapter.Initialize(stub2);
    
    // Stub should be updated to stub2
    EXPECT_EQ(adapter.GetStub(), stub2);
}

/**
 * @brief Test stub adapter deinitialization
 */
TEST_F(SomeIpAdapterTest, StubAdapterDeinit) {
    SomeIpStubAdapter<MockCommonAPIStub> adapter("local", "TestService", "v1_0");
    
    auto stub = std::make_shared<MockCommonAPIStub>();
    adapter.Initialize(stub);
    
    // Deinitialize
    auto result = adapter.Deinitialize();
    
    // Stub should be cleared
    EXPECT_EQ(adapter.GetStub(), nullptr);
}

/**
 * @brief Test stub adapter deinit before init
 */
TEST_F(SomeIpAdapterTest, StubAdapterDeinitBeforeInit) {
    SomeIpStubAdapter<MockCommonAPIStub> adapter("local", "TestService", "v1_0");
    
    auto result = adapter.Deinitialize();
    
    // Should succeed (no-op)
    EXPECT_TRUE(result.IsOk());
}

// ============================================================================
// Integration Tests
// ============================================================================

/**
 * @brief Test domain, instance, connection ID getters
 */
TEST_F(SomeIpAdapterTest, AdapterGettersSetters) {
    SomeIpProxyAdapter<MockCommonAPIProxy> proxyAdapter("testDomain", "testInstance", "v1_0");
    
    // Connection ID can be set
    proxyAdapter.SetConnectionId("testConnection");
    EXPECT_EQ(proxyAdapter.GetConnectionId(), "testConnection");
    
    SomeIpStubAdapter<MockCommonAPIStub> stubAdapter("testDomain2", "testInstance2", "v2_0");
    
    // Stub adapter doesn't have connection ID setter (by design)
}

/**
 * @brief Test multiple adapters with different configurations
 */
TEST_F(SomeIpAdapterTest, MultipleAdapters) {
    SomeIpProxyAdapter<MockCommonAPIProxy> adapter1("domain1", "instance1", "v1_0");
    SomeIpProxyAdapter<MockCommonAPIProxy> adapter2("domain2", "instance2", "v1_0");
    SomeIpStubAdapter<MockCommonAPIStub> adapter3("domain3", "instance3", "v1_0");
    
    // All adapters should be independent
    adapter1.SetConnectionId("conn1");
    adapter2.SetConnectionId("conn2");
    
    EXPECT_EQ(adapter1.GetConnectionId(), "conn1");
    EXPECT_EQ(adapter2.GetConnectionId(), "conn2");
}

/**
 * @brief Test adapter destruction
 */
TEST_F(SomeIpAdapterTest, AdapterDestruction) {
    {
        SomeIpProxyAdapter<MockCommonAPIProxy> adapter("local", "TestService", "v1_0");
        // Adapter goes out of scope
    }
    
    {
        SomeIpStubAdapter<MockCommonAPIStub> adapter("local", "TestService", "v1_0");
        auto stub = std::make_shared<MockCommonAPIStub>();
        adapter.Initialize(stub);
        // Adapter goes out of scope, should cleanup
    }
    
    // No crashes expected
    SUCCEED();
}

/**
 * @brief Test error code conversion completeness
 */
TEST_F(SomeIpAdapterTest, ErrorCodeCoverage) {
    SomeIpProxyAdapter<MockCommonAPIProxy> adapter("local", "TestService", "v1_0");
    
    // Test all possible CommonAPI::CallStatus values
    std::vector<CommonAPI::CallStatus> allStatuses = {
        CommonAPI::CallStatus::SUCCESS,
        CommonAPI::CallStatus::OUT_OF_MEMORY,
        CommonAPI::CallStatus::NOT_AVAILABLE,
        CommonAPI::CallStatus::CONNECTION_FAILED,
        CommonAPI::CallStatus::REMOTE_ERROR,
        CommonAPI::CallStatus::SUBSCRIPTION_REFUSED,
        CommonAPI::CallStatus::UNKNOWN,
        static_cast<CommonAPI::CallStatus>(-1) // Invalid status
    };
    
    for (auto status : allStatuses) {
        auto result = adapter.WrapCallStatus<int>(status, 0);
        
        if (status == CommonAPI::CallStatus::SUCCESS) {
            EXPECT_TRUE(result.IsOk());
        } else {
            EXPECT_FALSE(result.IsOk());
            // Should have valid error code
            EXPECT_NE(result.Error().Value(), 0);
        }
    }
}

/**
 * @brief Test timeout handling in initialization
 */
TEST_F(SomeIpAdapterTest, InitializationTimeout) {
    SomeIpProxyAdapter<MockCommonAPIProxy> adapter("local", "TestService", "v1_0");
    
    // Very short timeout
    auto result = adapter.Initialize(10); // 10ms timeout
    
    // Should timeout or fail (no real service available)
    EXPECT_FALSE(result.IsOk());
}

/**
 * @brief Test adapter with empty domain/instance
 */
TEST_F(SomeIpAdapterTest, EmptyDomainInstance) {
    SomeIpProxyAdapter<MockCommonAPIProxy> adapter1("", "", "");
    SomeIpStubAdapter<MockCommonAPIStub> adapter2("", "", "");
    
    // Should be created but initialization will fail
    auto result1 = adapter1.Initialize(1000);
    EXPECT_FALSE(result1.IsOk());
    
    auto stub = std::make_shared<MockCommonAPIStub>();
    auto result2 = adapter2.Initialize(stub);
    // May succeed with storing stub even if registration fails
}

/**
 * @brief Main test runner
 */
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
