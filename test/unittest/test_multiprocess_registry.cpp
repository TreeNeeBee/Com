/**
 * @file        test_multiprocess_registry.cpp
 * @author      LightAP Development Team
 * @brief       Multi-process integration test for UDS FD passing
 * @date        2025-11-20
 * @details     Tests Phase 2 implementation:
 *              - Server creates memfd and listens on UDS socket
 *              - Multiple client processes connect and receive FD
 *              - All processes share same physical memory
 *              - Verify cross-process service registration/discovery
 * @copyright   Copyright (c) 2025
 */

#include <gtest/gtest.h>
#include "RegistryInitializer.hpp"
#include "SharedMemoryRegistry.hpp"

#include <sys/wait.h>
#include <unistd.h>
#include <thread>
#include <chrono>

using namespace lap::com::registry;
using namespace std::chrono_literals;

class MultiProcessRegistryTest : public ::testing::Test
{
protected:
    const String kSocketPath = "/tmp/test_registry.sock";
    
    void SetUp() override
    {
        // Clean up any leftover socket file
        unlink(kSocketPath.c_str());
    }
    
    void TearDown() override
    {
        unlink(kSocketPath.c_str());
    }
};

// Test 1: Server creates memfd and serves clients
TEST_F(MultiProcessRegistryTest, ServerClientFdPassing)
{
    pid_t server_pid = fork();
    
    if (server_pid == 0)
    {
        // Child process: Server
        RegistryInitializer server(RegistryType::QM, kSocketPath);
        
        auto init_result = server.Initialize();
        ASSERT_TRUE(init_result.HasValue()) << init_result.Error().Message();
        
        // Run server for 5 seconds
        std::thread shutdown_thread([&server]() {
            std::this_thread::sleep_for(5s);
            server.Shutdown();
        });
        
        auto run_result = server.Run(false);
        shutdown_thread.join();
        
        EXPECT_TRUE(run_result.HasValue());
        exit(0);
    }
    else if (server_pid > 0)
    {
        // Parent process: Wait for server to start
        std::this_thread::sleep_for(500ms);
        
        // Client: Connect and receive memfd
        SingleRegistry client_registry(RegistryType::QM);
        auto client_result = client_registry.InitializeFromSocket(kSocketPath);
        
        ASSERT_TRUE(client_result.HasValue()) << client_result.Error().Message();
        EXPECT_TRUE(client_registry.IsInitialized());
        EXPECT_GE(client_registry.GetMemfd(), 0);
        
        // Wait for server to finish
        int status;
        waitpid(server_pid, &status, 0);
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
    else
    {
        FAIL() << "fork() failed";
    }
}

// Test 2: Multiple clients share same memory
TEST_F(MultiProcessRegistryTest, MultipleClientsShareMemory)
{
    pid_t server_pid = fork();
    
    if (server_pid == 0)
    {
        // Server process
        RegistryInitializer server(RegistryType::QM, kSocketPath);
        server.Initialize();
        
        std::thread shutdown_thread([&server]() {
            std::this_thread::sleep_for(10s);
            server.Shutdown();
        });
        
        server.Run(false);
        shutdown_thread.join();
        exit(0);
    }
    else if (server_pid > 0)
    {
        std::this_thread::sleep_for(500ms);  // Wait for server
        
        // Client 1: Register a service
        pid_t client1_pid = fork();
        if (client1_pid == 0)
        {
            SingleRegistry registry(RegistryType::QM);
            auto result = registry.InitializeFromSocket(kSocketPath);
            ASSERT_TRUE(result.HasValue());
            
            // Register service at slot 100
            auto reg_result = registry.RegisterService(
                100,  // slot_index
                0x1234,  // service_id
                0x0001,  // instance_id
                1, 0,    // version
                "test",  // binding_type
                "localhost:5000"  // endpoint
            );
            
            EXPECT_TRUE(reg_result.HasValue());
            
            // Keep process alive for a bit
            std::this_thread::sleep_for(3s);
            exit(0);
        }
        
        // Client 2: Read service registered by Client 1
        std::this_thread::sleep_for(1s);  // Wait for Client 1 to register
        
        pid_t client2_pid = fork();
        if (client2_pid == 0)
        {
            SingleRegistry registry(RegistryType::QM);
            auto result = registry.InitializeFromSocket(kSocketPath);
            ASSERT_TRUE(result.HasValue());
            
            // Read slot 100 (should see Client 1's service)
            auto opt_slot = registry.ReadSlot(100);
            ASSERT_TRUE(opt_slot.has_value());
            
            const auto& slot = opt_slot.value();
            EXPECT_EQ(slot.service_id, 0x1234u);
            EXPECT_EQ(slot.instance_id, 0x0001u);
            EXPECT_STREQ(slot.binding_type, "test");
            EXPECT_TRUE(slot.IsActive());
            
            exit(0);
        }
        
        // Wait for all children
        int status;
        waitpid(client1_pid, &status, 0);
        EXPECT_EQ(WEXITSTATUS(status), 0);
        
        waitpid(client2_pid, &status, 0);
        EXPECT_EQ(WEXITSTATUS(status), 0);
        
        // Cleanup server
        kill(server_pid, SIGTERM);
        waitpid(server_pid, &status, 0);
    }
    else
    {
        FAIL() << "fork() failed";
    }
}

// Test 3: Cross-process service discovery
TEST_F(MultiProcessRegistryTest, CrossProcessServiceDiscovery)
{
    // Start server
    pid_t server_pid = fork();
    if (server_pid == 0)
    {
        RegistryInitializer server(RegistryType::QM, kSocketPath);
        server.Initialize();
        
        std::thread shutdown_thread([&server]() {
            std::this_thread::sleep_for(8s);
            server.Shutdown();
        });
        
        server.Run(false);
        shutdown_thread.join();
        exit(0);
    }
    
    std::this_thread::sleep_for(500ms);
    
    // Writer process: Register 10 services
    pid_t writer_pid = fork();
    if (writer_pid == 0)
    {
        SingleRegistry registry(RegistryType::QM);
        registry.InitializeFromSocket(kSocketPath);
        
        for (uint32_t i = 1; i <= 10; ++i)
        {
            registry.RegisterService(
                i,  // slot_index
                0x1000 + i,  // service_id
                1, 1, 0,
                "dds",
                "topic_name"
            );
        }
        
        std::this_thread::sleep_for(3s);
        exit(0);
    }
    
    std::this_thread::sleep_for(1s);
    
    // Reader process: Find all 10 services
    pid_t reader_pid = fork();
    if (reader_pid == 0)
    {
        SingleRegistry registry(RegistryType::QM);
        registry.InitializeFromSocket(kSocketPath);
        
        uint32_t found_count = 0;
        for (uint32_t i = 1; i <= 10; ++i)
        {
            auto opt_slot = registry.ReadSlot(i);
            if (opt_slot.has_value() && opt_slot.value().IsActive())
            {
                ++found_count;
            }
        }
        
        EXPECT_EQ(found_count, 10u);
        exit(found_count == 10 ? 0 : 1);
    }
    
    // Wait and verify
    int status;
    waitpid(writer_pid, &status, 0);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    
    waitpid(reader_pid, &status, 0);
    EXPECT_EQ(WEXITSTATUS(status), 0);
    
    kill(server_pid, SIGTERM);
    waitpid(server_pid, &status, 0);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
