/**
 * @file binding_headers_test.cpp
 * @brief Test that all binding headers can be included without errors
 * 
 * This file validates the header dependencies and ensures all binding
 * interfaces are correctly exposed.
 */

// Test D-Bus manual binding
#include "../../source/binding/dbus/DBusEventBinding.hpp"
#include "../../source/binding/dbus/DBusMethodBinding.hpp"
#include "../../source/binding/dbus/DBusFieldBinding.hpp"
#include "../../source/binding/dbus/DBusConnectionManager.hpp"

// Test SOME/IP binding
#include "../../source/binding/someip/SomeIpConnectionManager.hpp"

// Test CommonAPI-DBus adapter
#include "../../source/binding/commonapi/CommonAPIDBusAdapter.hpp"

// Test CommonAPI-SOME/IP adapter
#include "../../source/binding/commonapi/CommonAPISomeIpAdapter.hpp"

// LightAP Core
#include <Core/CoreBase/inc/CMemory.h>
#include <Core/CoreBase/inc/CLog.h>

#include <gtest/gtest.h>

using namespace lap::com;
using namespace lap::core;
using namespace lap::log;

/**
 * @brief Test that D-Bus connection manager can be instantiated
 */
TEST(BindingHeaders, DBusConnectionManager) {
    auto& dbusConn = dbus::DBusConnectionManager::getInstance();
    EXPECT_TRUE(true); // Just test compilation
}

/**
 * @brief Test that SOME/IP connection manager can be instantiated
 */
TEST(BindingHeaders, SomeIpConnectionManager) {
    auto& someipConn = someip::SomeIpConnectionManager::getInstance();
    EXPECT_TRUE(true); // Just test compilation
}

/**
 * @brief Test that error codes are available
 */
TEST(BindingHeaders, ErrorCodes) {
    auto err = MakeErrorCode(ComErrc::NotInitialized);
    EXPECT_EQ(err.Value(), static_cast<int>(ComErrc::NotInitialized));
}

int main(int argc, char** argv) {
    // Initialize LightAP core (required for singletons)
    MemManager::getInstance();
    LogManager::getInstance().initialize();

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
