/**
 * @file        SocketConnectionManager.hpp
 * @author      LightAP Team
 * @brief       Unix Domain Socket Connection Manager
 * @date        2025-10-30
 * @details     Manages Unix Domain Socket connections for high-performance local IPC
 * @copyright   Copyright (c) 2025
 * @version     1.0
 */

#ifndef LAP_COM_BINDING_SOCKET_CONNECTION_MANAGER_HPP
#define LAP_COM_BINDING_SOCKET_CONNECTION_MANAGER_HPP

#include <ComTypes.hpp>
#include <core/CResult.hpp>
#include <core/CString.hpp>
#include <core/CTypedef.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace lap {
namespace com {
namespace binding {
namespace socket {

/**
 * @brief Unix Socket传输模式
 */
enum class SocketTransportMode : lap::core::UInt8 {
    kStream = SOCK_STREAM,        // 可靠、有序、面向连接
    kDatagram = SOCK_DGRAM,       // 无连接、消息边界
    kSeqPacket = SOCK_SEQPACKET   // 可靠、有序、有边界
};

/**
 * @brief Socket连接配置
 */
struct SocketEndpoint {
    lap::core::String socketPath;           // Socket文件路径
    SocketTransportMode mode;               // 传输模式
    lap::core::UInt32 maxMessageSize;       // 最大消息大小 (字节)
    lap::core::UInt32 sendBufferSize;       // 发送缓冲区大小
    lap::core::UInt32 recvBufferSize;       // 接收缓冲区大小
    bool reuseAddr;                         // 地址重用
    lap::core::UInt32 listenBacklog;        // 监听队列大小 (服务端)
};

/**
 * @brief Unix Socket连接管理器 (单例)
 * 
 * @details
 * 管理Unix Domain Socket连接生命周期，支持:
 * - SOCK_STREAM: 面向连接的可靠传输
 * - SOCK_DGRAM: 无连接的数据报传输
 * - SOCK_SEQPACKET: 可靠的有序数据包传输
 * 
 * 特性:
 * - 线程安全的连接管理
 * - 超时控制
 * - 自动资源清理
 * - 错误恢复
 * 
 * @usage
 * auto& manager = SocketConnectionManager::GetInstance();
 * manager.initialize();
 * 
 * SocketEndpoint endpoint{
 *     .socketPath = "/tmp/myservice.sock",
 *     .mode = SocketTransportMode::kStream,
 *     .maxMessageSize = 65536,
 *     .sendBufferSize = 8192,
 *     .recvBufferSize = 8192,
 *     .reuseAddr = true,
 *     .listenBacklog = 128
 * };
 * 
 * auto serverResult = manager.createServerSocket(endpoint);
 * if (serverResult.HasValue()) {
 *     int serverFd = serverResult.Value();
 *     // 使用socket...
 * }
 */
class SocketConnectionManager {
public:
    /**
     * @brief 获取单例实例
     */
    static SocketConnectionManager& GetInstance() noexcept {
        static SocketConnectionManager instance;
        return instance;
    }

    /**
     * @brief 初始化连接管理器
     * @return Result<void> 成功或错误
     */
    Result<void> initialize() noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (m_initialized) {
            return Result<void>::FromValue();
        }
        
        m_initialized = true;
        return Result<void>::FromValue();
    }

    /**
     * @brief 反初始化，关闭所有连接
     */
    void deinitialize() noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (!m_initialized) {
            return;
        }
        
        // 关闭所有注册的socket
        for (const auto& pair : m_sockets) {
            ::close(pair.first);
            // 删除socket文件 (如果是服务端)
            if (!pair.second.socketPath.empty()) {
                ::unlink(pair.second.socketPath.c_str());
            }
        }
        
        m_sockets.clear();
        m_initialized = false;
    }

    /**
     * @brief 创建服务端Socket
     * @param endpoint Socket配置
     * @return Result<int> Socket文件描述符或错误
     */
    Result<int> createServerSocket(const SocketEndpoint& endpoint) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (!m_initialized) {
            return Result<int>::FromError(
                MakeErrorCode(ComErrc::kNotInitialized, 0));
        }
        
        // 创建socket
        int sockfd = ::socket(AF_UNIX, static_cast<int>(endpoint.mode), 0);
        if (sockfd < 0) {
            return Result<int>::FromError(
                MakeErrorCode(ComErrc::kNetworkBindingFailure, errno));
        }
        
        // 设置socket选项
        auto configResult = configureSocket(sockfd, endpoint);
        if (!configResult.HasValue()) {
            ::close(sockfd);
            return Result<int>::FromError(configResult.Error());
        }
        
        // 删除旧的socket文件 (如果存在)
        ::unlink(endpoint.socketPath.c_str());
        
        // 绑定地址
        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, endpoint.socketPath.c_str(), 
                    sizeof(addr.sun_path) - 1);
        
        if (::bind(sockfd, reinterpret_cast<struct sockaddr*>(&addr), 
                  sizeof(addr)) < 0) {
            ::close(sockfd);
            return Result<int>::FromError(
                MakeErrorCode(ComErrc::kNetworkBindingFailure, errno));
        }
        
        // 监听 (仅SOCK_STREAM和SOCK_SEQPACKET)
        if (endpoint.mode == SocketTransportMode::kStream ||
            endpoint.mode == SocketTransportMode::kSeqPacket) {
            if (::listen(sockfd, endpoint.listenBacklog) < 0) {
                ::close(sockfd);
                ::unlink(endpoint.socketPath.c_str());
                return Result<int>::FromError(
                    MakeErrorCode(ComErrc::kNetworkBindingFailure, errno));
            }
            // 将服务端socket设置为非阻塞，便于在无连接时快速返回
            int flags = ::fcntl(sockfd, F_GETFL, 0);
            if (flags >= 0) {
                (void)::fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
            }
        }
        
        // 注册socket
        m_sockets[sockfd] = endpoint;
        
        return Result<int>::FromValue(sockfd);
    }

    /**
     * @brief 创建客户端Socket并连接
     * @param endpoint 服务端端点配置
     * @return Result<int> Socket文件描述符或错误
     */
    Result<int> createClientSocket(const SocketEndpoint& endpoint) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (!m_initialized) {
            return Result<int>::FromError(
                MakeErrorCode(ComErrc::kNotInitialized, 0));
        }
        
        // 创建socket
        int sockfd = ::socket(AF_UNIX, static_cast<int>(endpoint.mode), 0);
        if (sockfd < 0) {
            return Result<int>::FromError(
                MakeErrorCode(ComErrc::kNetworkBindingFailure, errno));
        }
        
        // 设置socket选项
        auto configResult = configureSocket(sockfd, endpoint);
        if (!configResult.HasValue()) {
            ::close(sockfd);
            return Result<int>::FromError(configResult.Error());
        }
        
        // 连接到服务端
        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, endpoint.socketPath.c_str(), 
                    sizeof(addr.sun_path) - 1);
        
        if (::connect(sockfd, reinterpret_cast<struct sockaddr*>(&addr), 
                     sizeof(addr)) < 0) {
            ::close(sockfd);
            return Result<int>::FromError(
                MakeErrorCode(ComErrc::kNetworkBindingFailure, errno));
        }
        
        // 注册socket (客户端不保存socketPath)
        SocketEndpoint clientEndpoint = endpoint;
        clientEndpoint.socketPath.clear();
        m_sockets[sockfd] = clientEndpoint;
        
        return Result<int>::FromValue(sockfd);
    }

    /**
     * @brief 接受客户端连接 (仅SOCK_STREAM/SOCK_SEQPACKET)
     * @param serverFd 服务端socket
     * @return Result<int> 客户端socket或错误
     */
    Result<int> acceptConnection(int serverFd) noexcept {
        struct sockaddr_un addr;
        socklen_t addrLen = sizeof(addr);
        
        int clientFd = ::accept(serverFd, reinterpret_cast<struct sockaddr*>(&addr), 
                               &addrLen);
        if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // TODO: if socket set non-block, not treat as error, just return no client connected
                // it should be add epoll or select to handle
                return Result<int>::FromError(
                    MakeErrorCode(ComErrc::kTimeout, errno));
            }
            return Result<int>::FromError(
                MakeErrorCode(ComErrc::kNetworkBindingFailure, errno));
        }
        
        // 继承服务端配置
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_sockets.find(serverFd);
        if (it != m_sockets.end()) {
            SocketEndpoint clientEndpoint = it->second;
            clientEndpoint.socketPath.clear();  // 客户端socket没有路径
            m_sockets[clientFd] = clientEndpoint;
        }
        
        return Result<int>::FromValue(clientFd);
    }

    /**
     * @brief 发送数据
     * @param fd Socket文件描述符
     * @param data 数据缓冲区
     * @param length 数据长度
     * @param timeoutMs 超时时间(毫秒), 0表示阻塞
     * @return Result<size_t> 实际发送字节数或错误
     */
    Result<size_t> send(int fd, const void* data, size_t length, 
                       lap::core::UInt32 timeoutMs = 0) noexcept {
        if (timeoutMs > 0) {
            auto waitResult = waitForSocket(fd, true, timeoutMs);
            if (!waitResult.HasValue()) {
                return Result<size_t>::FromError(waitResult.Error());
            }
        }
        
        ssize_t sent = ::send(fd, data, length, MSG_NOSIGNAL);
        if (sent < 0) {
            std::fprintf(stderr, "[SocketConnectionManager] send fd=%d len=%zu errno=%d(%s)\n", fd, length, errno, std::strerror(errno));
            return Result<size_t>::FromError(
                MakeErrorCode(ComErrc::kNetworkBindingFailure, errno));
        }
        
        return Result<size_t>::FromValue(static_cast<size_t>(sent));
    }

    /**
     * @brief 接收数据
     * @param fd Socket文件描述符
     * @param buffer 接收缓冲区
     * @param maxLength 缓冲区大小
     * @param timeoutMs 超时时间(毫秒), 0表示阻塞
     * @return Result<size_t> 实际接收字节数或错误
     */
    Result<size_t> receive(int fd, void* buffer, size_t maxLength,
                          lap::core::UInt32 timeoutMs = 0) noexcept {
        if (timeoutMs > 0) {
            auto waitResult = waitForSocket(fd, false, timeoutMs);
            if (!waitResult.HasValue()) {
                return Result<size_t>::FromError(waitResult.Error());
            }
        }
        
        ssize_t received = ::recv(fd, buffer, maxLength, 0);
        if (received < 0) {
            std::fprintf(stderr, "[SocketConnectionManager] recv fd=%d max=%zu errno=%d(%s)\n", fd, maxLength, errno, std::strerror(errno));
            return Result<size_t>::FromError(
                MakeErrorCode(ComErrc::kNetworkBindingFailure, errno));
        }
        
        return Result<size_t>::FromValue(static_cast<size_t>(received));
    }

    /**
     * @brief 关闭Socket连接
     * @param fd Socket文件描述符
     */
    void closeSocket(int fd) noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto it = m_sockets.find(fd);
        if (it != m_sockets.end()) {
            // 删除socket文件 (如果是服务端)
            if (!it->second.socketPath.empty()) {
                ::unlink(it->second.socketPath.c_str());
            }
            m_sockets.erase(it);
        }
        
        ::close(fd);
    }

    /**
     * @brief 检查Socket是否有效
     * @param fd Socket文件描述符
     * @return true if valid, false otherwise
     */
    bool isSocketValid(int fd) const noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_sockets.find(fd) != m_sockets.end();
    }

    /**
     * @brief 获取Socket错误信息
     * @param fd Socket文件描述符
     * @return 错误字符串
     */
    lap::core::String getSocketError(int fd) const noexcept {
        int error = 0;
        socklen_t len = sizeof(error);
        
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
            return "Failed to get socket error";
        }
        
        return std::strerror(error);
    }

private:
    SocketConnectionManager() = default;
    ~SocketConnectionManager() { deinitialize(); }

    // 禁止拷贝和移动
    SocketConnectionManager(const SocketConnectionManager&) = delete;
    SocketConnectionManager& operator=(const SocketConnectionManager&) = delete;
    SocketConnectionManager(SocketConnectionManager&&) = delete;
    SocketConnectionManager& operator=(SocketConnectionManager&&) = delete;

    /**
     * @brief 设置Socket选项
     */
    Result<void> configureSocket(int fd, const SocketEndpoint& endpoint) noexcept {
        // 设置发送缓冲区大小
        if (endpoint.sendBufferSize > 0) {
            int size = static_cast<int>(endpoint.sendBufferSize);
            if (::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) < 0) {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kNetworkBindingFailure, errno));
            }
        }
        
        // 设置接收缓冲区大小
        if (endpoint.recvBufferSize > 0) {
            int size = static_cast<int>(endpoint.recvBufferSize);
            if (::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) < 0) {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kNetworkBindingFailure, errno));
            }
        }
        
        // 设置地址重用（AF_UNIX 上可能不支持，忽略 ENOPROTOOPT/EINVAL 错误）
        if (endpoint.reuseAddr) {
            int reuse = 1;
            if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
                // 对于 Unix Domain Socket，SO_REUSEADDR 不是必须且可能不支持
                if (errno != ENOPROTOOPT && errno != EINVAL) {
                    return Result<void>::FromError(
                        MakeErrorCode(ComErrc::kNetworkBindingFailure, errno));
                }
            }
        }
        
        return Result<void>::FromValue();
    }

    /**
     * @brief 等待Socket可读/可写
     */
    Result<void> waitForSocket(int fd, bool waitWrite, lap::core::UInt32 timeoutMs) noexcept {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = waitWrite ? POLLOUT : POLLIN;
        pfd.revents = 0;
        
        int ret = ::poll(&pfd, 1, static_cast<int>(timeoutMs));
        
        if (ret < 0) {
            return Result<void>::FromError(
                MakeErrorCode(ComErrc::kNetworkBindingFailure, errno));
        } else if (ret == 0) {
            return Result<void>::FromError(
                MakeErrorCode(ComErrc::kTimeout, 0));
        }
        
        // For reads: POLLIN is good; POLLHUP may be present alongside POLLIN when peer closed after sending data.
        // In that case we should still read available bytes and only treat EOF when recv() returns 0.
        // For writes: POLLHUP means the peer is gone; treat as failure.
        if (waitWrite) {
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kNetworkBindingFailure, 0));
            }
            if (!(pfd.revents & POLLOUT)) {
                // Unexpected state: woke up without POLLOUT and no fatal flags; treat as failure
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kNetworkBindingFailure, 0));
            }
        } else {
            if (pfd.revents & (POLLERR | POLLNVAL)) {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kNetworkBindingFailure, 0));
            }
            if (!(pfd.revents & (POLLIN | POLLHUP))) {
                // Not readable yet (e.g., only POLLPRI or other); treat as failure
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kNetworkBindingFailure, 0));
            }
        }
        
        return Result<void>::FromValue();
    }

    mutable std::mutex m_mutex;                          // 保护并发访问
    bool m_initialized{false};                           // 初始化状态
    std::unordered_map<int, SocketEndpoint> m_sockets;   // Socket注册表
};

} // namespace socket
} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_BINDING_SOCKET_CONNECTION_MANAGER_HPP
