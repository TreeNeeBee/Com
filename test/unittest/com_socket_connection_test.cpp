/**
 * @file        com_socket_connection_test.cpp
 * @brief       Unit tests for SocketConnectionManager
 * @author      LightAP Team
 * @date        2025-10-30
 */

#include <binding/socket/SocketConnectionManager.hpp>
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace lap::com::binding::socket;

/**
 * @brief Test fixture for SocketConnectionManager
 */
class SocketConnectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_manager = &SocketConnectionManager::GetInstance();
        m_manager->initialize();
        
        // Use unique socket path for each test
        m_testSocketPath = "/tmp/test_socket_" + 
                          std::to_string(std::time(nullptr)) + ".sock";
    }

    void TearDown() override {
        m_manager->deinitialize();
        // Clean up socket file
        ::unlink(m_testSocketPath.c_str());
    }

    SocketConnectionManager* m_manager;
    std::string m_testSocketPath;
};

/**
 * @brief Test singleton pattern
 */
TEST_F(SocketConnectionTest, SingletonInstance) {
    auto& instance1 = SocketConnectionManager::GetInstance();
    auto& instance2 = SocketConnectionManager::GetInstance();
    
    EXPECT_EQ(&instance1, &instance2);
}

/**
 * @brief Test initialization
 */
TEST_F(SocketConnectionTest, Initialization) {
    auto result = m_manager->initialize();
    EXPECT_TRUE(result.HasValue());
    
    // Multiple initializations should be safe
    auto result2 = m_manager->initialize();
    EXPECT_TRUE(result2.HasValue());
}

/**
 * @brief Test server socket creation (SOCK_STREAM)
 */
TEST_F(SocketConnectionTest, CreateServerSocketStream) {
    SocketEndpoint endpoint{
        .socketPath = m_testSocketPath,
        .mode = SocketTransportMode::kStream,
        .maxMessageSize = 8192,
        .sendBufferSize = 4096,
        .recvBufferSize = 4096,
        .reuseAddr = true,
        .listenBacklog = 10
    };
    
    auto result = m_manager->createServerSocket(endpoint);
    
    ASSERT_TRUE(result.HasValue());
    int serverFd = result.Value();
    EXPECT_GE(serverFd, 0);
    
    // Verify socket is valid
    EXPECT_TRUE(m_manager->isSocketValid(serverFd));
    
    // Cleanup
    m_manager->closeSocket(serverFd);
    EXPECT_FALSE(m_manager->isSocketValid(serverFd));
}

/**
 * @brief Test client-server connection
 */
TEST_F(SocketConnectionTest, ClientServerConnection) {
    // Create server
    SocketEndpoint serverEndpoint{
        .socketPath = m_testSocketPath,
        .mode = SocketTransportMode::kStream,
        .maxMessageSize = 8192,
        .sendBufferSize = 4096,
        .recvBufferSize = 4096,
        .reuseAddr = true,
        .listenBacklog = 10
    };
    
    auto serverResult = m_manager->createServerSocket(serverEndpoint);
    ASSERT_TRUE(serverResult.HasValue());
    int serverFd = serverResult.Value();
    
    // Accept connection in background thread
    std::thread serverThread([this, serverFd]() {
        auto clientResult = m_manager->acceptConnection(serverFd);
        if (clientResult.HasValue()) {
            int clientFd = clientResult.Value();
            
            // Receive data
            char buffer[128];
            auto recvResult = m_manager->receive(clientFd, buffer, sizeof(buffer), 1000);
            
            if (recvResult.HasValue() && recvResult.Value() > 0) {
                // Echo back
                m_manager->send(clientFd, buffer, recvResult.Value(), 1000);
            }
            
            m_manager->closeSocket(clientFd);
        }
    });
    
    // Give server time to start accepting
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Create client and connect
    SocketEndpoint clientEndpoint{
        .socketPath = m_testSocketPath,
        .mode = SocketTransportMode::kStream,
        .maxMessageSize = 8192,
        .sendBufferSize = 4096,
        .recvBufferSize = 4096,
        .reuseAddr = false,
        .listenBacklog = 0
    };
    
    auto clientResult = m_manager->createClientSocket(clientEndpoint);
    ASSERT_TRUE(clientResult.HasValue());
    int clientFd = clientResult.Value();
    
    // Send data
    const char* message = "Hello Socket!";
    auto sendResult = m_manager->send(clientFd, message, std::strlen(message), 1000);
    ASSERT_TRUE(sendResult.HasValue());
    EXPECT_EQ(sendResult.Value(), std::strlen(message));
    
    // Receive echoed data
    char recvBuffer[128];
    auto recvResult = m_manager->receive(clientFd, recvBuffer, sizeof(recvBuffer), 1000);
    ASSERT_TRUE(recvResult.HasValue());
    EXPECT_EQ(recvResult.Value(), std::strlen(message));
    
    recvBuffer[recvResult.Value()] = '\0';
    EXPECT_STREQ(recvBuffer, message);
    
    // Cleanup
    m_manager->closeSocket(clientFd);
    serverThread.join();
    m_manager->closeSocket(serverFd);
}

/**
 * @brief Test send/receive with timeout
 */
TEST_F(SocketConnectionTest, SendReceiveTimeout) {
    // Create server
    SocketEndpoint serverEndpoint{
        .socketPath = m_testSocketPath,
        .mode = SocketTransportMode::kStream,
        .maxMessageSize = 8192,
        .sendBufferSize = 4096,
        .recvBufferSize = 4096,
        .reuseAddr = true,
        .listenBacklog = 10
    };
    
    auto serverResult = m_manager->createServerSocket(serverEndpoint);
    ASSERT_TRUE(serverResult.HasValue());
    int serverFd = serverResult.Value();
    
    // Accept connection in background thread (but don't read)
    std::thread serverThread([this, serverFd]() {
        auto clientResult = m_manager->acceptConnection(serverFd);
        if (clientResult.HasValue()) {
            int clientFd = clientResult.Value();
            // Sleep without reading to trigger client timeout
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            m_manager->closeSocket(clientFd);
        }
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Create client
    SocketEndpoint clientEndpoint{
        .socketPath = m_testSocketPath,
        .mode = SocketTransportMode::kStream,
        .maxMessageSize = 8192
    };
    
    auto clientResult = m_manager->createClientSocket(clientEndpoint);
    ASSERT_TRUE(clientResult.HasValue());
    int clientFd = clientResult.Value();
    
    // Send should succeed
    const char* message = "Test";
    auto sendResult = m_manager->send(clientFd, message, std::strlen(message), 1000);
    EXPECT_TRUE(sendResult.HasValue());
    
    // Receive should timeout
    char buffer[128];
    auto recvResult = m_manager->receive(clientFd, buffer, sizeof(buffer), 500);
    EXPECT_FALSE(recvResult.HasValue());
    EXPECT_EQ(recvResult.Error().Value(), static_cast<int>(lap::com::ComErrc::kTimeout));
    
    // Cleanup
    m_manager->closeSocket(clientFd);
    serverThread.join();
    m_manager->closeSocket(serverFd);
}

/**
 * @brief Test multiple connections
 */
TEST_F(SocketConnectionTest, MultipleConnections) {
    // Create server
    SocketEndpoint serverEndpoint{
        .socketPath = m_testSocketPath,
        .mode = SocketTransportMode::kStream,
        .maxMessageSize = 8192,
        .sendBufferSize = 4096,
        .recvBufferSize = 4096,
        .reuseAddr = true,
        .listenBacklog = 10
    };
    
    auto serverResult = m_manager->createServerSocket(serverEndpoint);
    ASSERT_TRUE(serverResult.HasValue());
    int serverFd = serverResult.Value();
    
    // Accept multiple connections
    std::atomic<int> connectionsHandled(0);
    std::thread serverThread([this, serverFd, &connectionsHandled]() {
        for (int i = 0; i < 3; ++i) {
            auto clientResult = m_manager->acceptConnection(serverFd);
            if (clientResult.HasValue()) {
                int clientFd = clientResult.Value();
                
                char buffer[128];
                auto recvResult = m_manager->receive(clientFd, buffer, sizeof(buffer), 1000);
                if (recvResult.HasValue()) {
                    m_manager->send(clientFd, buffer, recvResult.Value(), 1000);
                    ++connectionsHandled;
                }
                
                m_manager->closeSocket(clientFd);
            }
        }
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Create multiple clients
    for (int i = 0; i < 3; ++i) {
        SocketEndpoint clientEndpoint{
            .socketPath = m_testSocketPath,
            .mode = SocketTransportMode::kStream,
            .maxMessageSize = 8192
        };
        
        auto clientResult = m_manager->createClientSocket(clientEndpoint);
        ASSERT_TRUE(clientResult.HasValue());
        int clientFd = clientResult.Value();
        
        char message[32];
        std::snprintf(message, sizeof(message), "Client %d", i);
        
        m_manager->send(clientFd, message, std::strlen(message), 1000);
        
        char recvBuffer[128];
        auto recvResult = m_manager->receive(clientFd, recvBuffer, sizeof(recvBuffer), 1000);
        EXPECT_TRUE(recvResult.HasValue());
        
        m_manager->closeSocket(clientFd);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    serverThread.join();
    EXPECT_EQ(connectionsHandled, 3);
    
    m_manager->closeSocket(serverFd);
}

/**
 * @brief Test error handling - invalid socket
 */
TEST_F(SocketConnectionTest, InvalidSocket) {
    char buffer[128];
    
    // Operations on invalid socket should fail
    auto sendResult = m_manager->send(9999, buffer, sizeof(buffer), 1000);
    EXPECT_FALSE(sendResult.HasValue());
    
    auto recvResult = m_manager->receive(9999, buffer, sizeof(buffer), 1000);
    EXPECT_FALSE(recvResult.HasValue());
}

/**
 * @brief Test connection to non-existent server
 */
TEST_F(SocketConnectionTest, ConnectToNonExistentServer) {
    SocketEndpoint endpoint{
        .socketPath = "/tmp/nonexistent_socket.sock",
        .mode = SocketTransportMode::kStream,
        .maxMessageSize = 8192
    };
    
    auto result = m_manager->createClientSocket(endpoint);
    EXPECT_FALSE(result.HasValue());
}

/**
 * @brief Main function
 */
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
