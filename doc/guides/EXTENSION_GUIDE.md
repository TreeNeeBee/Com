# Com模块扩展指南

## 目标

本指南说明如何在现有Com模块架构上扩展新的传输层：
1. **Protobuf over Unix Domain Socket** - 高性能本地进程间通信
2. **自定义私有协议** - 针对特定场景优化的轻量级协议

## 1. Protobuf over Unix Socket 扩展设计

### 1.1 设计目标

- **高性能**: 利用Unix Socket零拷贝特性
- **跨语言**: Protobuf支持多种编程语言
- **类型安全**: 基于IDL定义
- **易于使用**: 遵循Com模块统一API

### 1.2 目录结构

```
modules/Com/source/binding/socket/
├── SocketConnectionManager.hpp      # Unix Socket连接管理
├── SocketMethodBinding.hpp          # 方法绑定
├── SocketEventBinding.hpp           # 事件绑定
├── SocketFieldBinding.hpp           # 字段绑定
└── ProtobufSerializer.hpp           # Protobuf序列化器
```

### 1.3 核心组件设计

#### 1.3.1 SocketConnectionManager

```cpp
/**
 * @file SocketConnectionManager.hpp
 * @brief Unix Domain Socket连接管理器
 */

#ifndef LAP_COM_BINDING_SOCKET_CONNECTION_MANAGER_HPP
#define LAP_COM_BINDING_SOCKET_CONNECTION_MANAGER_HPP

#include <ComTypes.hpp>
#include <core/CResult.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <mutex>
#include <unordered_map>

namespace lap {
namespace com {
namespace binding {
namespace socket {

/**
 * @brief Unix Socket传输模式
 */
enum class SocketTransportMode {
    kStream,      // SOCK_STREAM (可靠、有序)
    kDatagram,    // SOCK_DGRAM (无连接)
    kSeqPacket    // SOCK_SEQPACKET (可靠、有边界)
};

/**
 * @brief Socket连接信息
 */
struct SocketEndpoint {
    lap::core::String socketPath;           // Socket文件路径
    SocketTransportMode mode;               // 传输模式
    lap::core::UInt32 maxMessageSize;       // 最大消息大小
    lap::core::UInt32 sendBufferSize;       // 发送缓冲区
    lap::core::UInt32 recvBufferSize;       // 接收缓冲区
    bool reuseAddr;                         // 地址重用
};

/**
 * @brief Unix Socket连接管理器 (单例)
 * 
 * @details
 * - 管理Unix Domain Socket连接生命周期
 * - 支持SOCK_STREAM和SOCK_DGRAM模式
 * - 提供连接池和重连机制
 * - 线程安全
 * 
 * @usage
 * auto& manager = SocketConnectionManager::GetInstance();
 * manager.initialize();
 * 
 * SocketEndpoint endpoint{
 *     .socketPath = "/tmp/myservice.sock",
 *     .mode = SocketTransportMode::kStream,
 *     .maxMessageSize = 65536
 * };
 * 
 * auto result = manager.createServerSocket(endpoint);
 * if (result.HasValue()) {
 *     int serverFd = result.Value();
 *     // 使用socket
 * }
 */
class SocketConnectionManager {
public:
    /**
     * @brief 获取单例实例
     */
    static SocketConnectionManager& GetInstance() {
        static SocketConnectionManager instance;
        return instance;
    }

    /**
     * @brief 初始化连接管理器
     * @return Result<void> 成功或错误
     */
    Result<void> initialize() noexcept;

    /**
     * @brief 反初始化，关闭所有连接
     */
    void deinitialize() noexcept;

    /**
     * @brief 创建服务端Socket
     * @param endpoint Socket端点配置
     * @return Result<int> Socket文件描述符或错误
     */
    Result<int> createServerSocket(const SocketEndpoint& endpoint) noexcept;

    /**
     * @brief 创建客户端Socket并连接
     * @param endpoint 服务端端点
     * @return Result<int> Socket文件描述符或错误
     */
    Result<int> createClientSocket(const SocketEndpoint& endpoint) noexcept;

    /**
     * @brief 接受客户端连接 (仅SOCK_STREAM)
     * @param serverFd 服务端socket
     * @return Result<int> 客户端socket或错误
     */
    Result<int> acceptConnection(int serverFd) noexcept;

    /**
     * @brief 发送数据
     * @param fd Socket文件描述符
     * @param data 数据缓冲区
     * @param length 数据长度
     * @param timeoutMs 超时时间(毫秒), 0表示阻塞
     * @return Result<size_t> 实际发送字节数或错误
     */
    Result<size_t> send(int fd, const void* data, size_t length, 
                       uint32_t timeoutMs = 0) noexcept;

    /**
     * @brief 接收数据
     * @param fd Socket文件描述符
     * @param buffer 接收缓冲区
     * @param maxLength 缓冲区大小
     * @param timeoutMs 超时时间(毫秒), 0表示阻塞
     * @return Result<size_t> 实际接收字节数或错误
     */
    Result<size_t> receive(int fd, void* buffer, size_t maxLength,
                          uint32_t timeoutMs = 0) noexcept;

    /**
     * @brief 关闭Socket连接
     * @param fd Socket文件描述符
     */
    void closeSocket(int fd) noexcept;

    /**
     * @brief 检查Socket是否有效
     * @param fd Socket文件描述符
     * @return true if valid, false otherwise
     */
    bool isSocketValid(int fd) const noexcept;

    /**
     * @brief 获取Socket错误信息
     * @param fd Socket文件描述符
     * @return 错误字符串
     */
    lap::core::String getSocketError(int fd) const noexcept;

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
    Result<void> configureSocket(int fd, const SocketEndpoint& endpoint) noexcept;

    /**
     * @brief 设置非阻塞模式
     */
    Result<void> setNonBlocking(int fd, bool enable) noexcept;

    /**
     * @brief 等待Socket可读/可写
     */
    Result<void> waitForSocket(int fd, bool waitWrite, uint32_t timeoutMs) noexcept;

    mutable std::mutex m_mutex;                          // 保护并发访问
    bool m_initialized{false};                           // 初始化状态
    std::unordered_map<int, SocketEndpoint> m_sockets;   // Socket注册表
};

} // namespace socket
} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_BINDING_SOCKET_CONNECTION_MANAGER_HPP
```

#### 1.3.2 ProtobufSerializer

```cpp
/**
 * @file ProtobufSerializer.hpp
 * @brief Protobuf序列化器实现
 */

#ifndef LAP_COM_BINDING_SOCKET_PROTOBUF_SERIALIZER_HPP
#define LAP_COM_BINDING_SOCKET_PROTOBUF_SERIALIZER_HPP

#include <Serialization.hpp>
#include <google/protobuf/message.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

namespace lap {
namespace com {
namespace binding {
namespace socket {

/**
 * @brief Protobuf消息序列化器
 * 
 * @details
 * - 支持任意Protobuf Message类型
 * - 使用Length-Delimited格式 (4字节长度 + payload)
 * - 支持零拷贝优化
 * 
 * @tparam MessageType Protobuf生成的消息类型
 * 
 * @usage
 * // 假设有MyRequest.proto定义
 * MyRequest request;
 * request.set_id(123);
 * request.set_name("test");
 * 
 * ProtobufSerializer<MyRequest> serializer;
 * auto result = serializer.Serialize(request);
 * if (result.HasValue()) {
 *     auto data = serializer.GetData();
 *     // 发送 data
 * }
 */
template<typename MessageType>
class ProtobufSerializer : public serialization::Serializer {
public:
    static_assert(std::is_base_of_v<google::protobuf::Message, MessageType>,
                  "MessageType must be a Protobuf Message");

    ProtobufSerializer() = default;

    SerializationFormat GetFormat() const noexcept override {
        return SerializationFormat::kProtobuf;
    }

    ByteOrder GetByteOrder() const noexcept override {
        // Protobuf使用小端序(wire format)
        return ByteOrder::kLittleEndian;
    }

    /**
     * @brief 序列化Protobuf消息
     * @param message Protobuf消息对象
     * @return Result<void> 成功或错误
     */
    Result<void> SerializeMessage(const MessageType& message) noexcept {
        try {
            // 计算消息大小
            size_t messageSize = message.ByteSizeLong();
            
            // 预留空间: 4字节长度 + 消息内容
            m_buffer.clear();
            m_buffer.resize(4 + messageSize);
            
            // 写入长度前缀 (网络字节序)
            uint32_t networkSize = htonl(static_cast<uint32_t>(messageSize));
            std::memcpy(m_buffer.data(), &networkSize, 4);
            
            // 序列化消息到缓冲区
            if (!message.SerializeToArray(m_buffer.data() + 4, messageSize)) {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kSerializationError, 0));
            }
            
            return Result<void>::FromValue();
            
        } catch (const std::exception& e) {
            LAP_LOG_ERROR("[ProtobufSerializer] Serialization failed: {}", e.what());
            return Result<void>::FromError(
                MakeErrorCode(ComErrc::kSerializationError, 0));
        }
    }

    lap::core::Span<const lap::core::UInt8> GetData() const noexcept override {
        return lap::core::MakeSpan(m_buffer.data(), m_buffer.size());
    }

    void Reset() noexcept override {
        m_buffer.clear();
    }

    // 未使用的基类方法 (Protobuf不需要单独序列化基本类型)
    Result<void> Serialize(bool) noexcept override { return NotSupported(); }
    Result<void> Serialize(lap::core::Int8) noexcept override { return NotSupported(); }
    Result<void> Serialize(lap::core::Int16) noexcept override { return NotSupported(); }
    Result<void> Serialize(lap::core::Int32) noexcept override { return NotSupported(); }
    Result<void> Serialize(lap::core::Int64) noexcept override { return NotSupported(); }
    Result<void> Serialize(lap::core::UInt8) noexcept override { return NotSupported(); }
    Result<void> Serialize(lap::core::UInt16) noexcept override { return NotSupported(); }
    Result<void> Serialize(lap::core::UInt32) noexcept override { return NotSupported(); }
    Result<void> Serialize(lap::core::UInt64) noexcept override { return NotSupported(); }
    Result<void> Serialize(float) noexcept override { return NotSupported(); }
    Result<void> Serialize(double) noexcept override { return NotSupported(); }
    Result<void> Serialize(const lap::core::String&) noexcept override { return NotSupported(); }
    Result<void> SerializeBytes(lap::core::Span<const lap::core::UInt8>) noexcept override { 
        return NotSupported(); 
    }

private:
    Result<void> NotSupported() const noexcept {
        return Result<void>::FromError(
            MakeErrorCode(ComErrc::kNotSupported, 0));
    }

    lap::core::Vector<lap::core::UInt8> m_buffer;
};

/**
 * @brief Protobuf消息反序列化器
 */
template<typename MessageType>
class ProtobufDeserializer : public serialization::Deserializer {
public:
    static_assert(std::is_base_of_v<google::protobuf::Message, MessageType>,
                  "MessageType must be a Protobuf Message");

    explicit ProtobufDeserializer(lap::core::Span<const lap::core::UInt8> data)
        : m_data(data), m_position(0) {}

    SerializationFormat GetFormat() const noexcept override {
        return SerializationFormat::kProtobuf;
    }

    ByteOrder GetByteOrder() const noexcept override {
        return ByteOrder::kLittleEndian;
    }

    /**
     * @brief 反序列化Protobuf消息
     * @param message 输出消息对象
     * @return Result<void> 成功或错误
     */
    Result<void> DeserializeMessage(MessageType& message) noexcept {
        try {
            // 读取长度前缀
            if (m_data.size() < 4) {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            uint32_t networkSize;
            std::memcpy(&networkSize, m_data.data(), 4);
            uint32_t messageSize = ntohl(networkSize);
            
            // 检查数据完整性
            if (m_data.size() < 4 + messageSize) {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            // 反序列化消息
            if (!message.ParseFromArray(m_data.data() + 4, messageSize)) {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kDeserializationError, 0));
            }
            
            m_position = 4 + messageSize;
            return Result<void>::FromValue();
            
        } catch (const std::exception& e) {
            LAP_LOG_ERROR("[ProtobufDeserializer] Deserialization failed: {}", e.what());
            return Result<void>::FromError(
                MakeErrorCode(ComErrc::kDeserializationError, 0));
        }
    }

    bool HasMoreData() const noexcept override {
        return m_position < m_data.size();
    }

    void Reset() noexcept override {
        m_position = 0;
    }

    // 未使用的基类方法
    Result<void> Deserialize(bool&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(lap::core::Int8&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(lap::core::Int16&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(lap::core::Int32&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(lap::core::Int64&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(lap::core::UInt8&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(lap::core::UInt16&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(lap::core::UInt32&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(lap::core::UInt64&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(float&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(double&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(lap::core::String&) noexcept override { return NotSupported(); }
    Result<void> DeserializeBytes(lap::core::Span<lap::core::UInt8>, lap::core::UInt32) 
        noexcept override { return NotSupported(); }

private:
    Result<void> NotSupported() const noexcept {
        return Result<void>::FromError(
            MakeErrorCode(ComErrc::kNotSupported, 0));
    }

    lap::core::Span<const lap::core::UInt8> m_data;
    size_t m_position;
};

} // namespace socket
} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_BINDING_SOCKET_PROTOBUF_SERIALIZER_HPP
```

#### 1.3.3 SocketMethodBinding

```cpp
/**
 * @file SocketMethodBinding.hpp
 * @brief Unix Socket方法绑定
 */

#ifndef LAP_COM_BINDING_SOCKET_METHOD_BINDING_HPP
#define LAP_COM_BINDING_SOCKET_METHOD_BINDING_HPP

#include "SocketConnectionManager.hpp"
#include "ProtobufSerializer.hpp"
#include <functional>
#include <future>

namespace lap {
namespace com {
namespace binding {
namespace socket {

/**
 * @brief Socket方法调用器 (客户端)
 * 
 * @tparam RequestType Protobuf请求消息类型
 * @tparam ResponseType Protobuf响应消息类型
 */
template<typename RequestType, typename ResponseType>
class SocketMethodCaller {
public:
    using CallbackType = std::function<void(Result<ResponseType>)>;

    /**
     * @brief 构造函数
     * @param endpoint 服务端端点
     */
    explicit SocketMethodCaller(const SocketEndpoint& endpoint)
        : m_endpoint(endpoint)
        , m_manager(SocketConnectionManager::GetInstance())
    {}

    /**
     * @brief 同步方法调用
     * @param request 请求消息
     * @param timeoutMs 超时时间(毫秒)
     * @return Result<ResponseType> 响应消息或错误
     */
    Result<ResponseType> call(const RequestType& request, uint32_t timeoutMs = 5000) noexcept {
        // 连接到服务端
        auto connectResult = m_manager.createClientSocket(m_endpoint);
        if (!connectResult.HasValue()) {
            return Result<ResponseType>::FromError(connectResult.Error());
        }
        int clientFd = connectResult.Value();

        // RAII: 确保socket关闭
        struct SocketGuard {
            SocketConnectionManager& mgr;
            int fd;
            ~SocketGuard() { mgr.closeSocket(fd); }
        } guard{m_manager, clientFd};

        // 序列化请求
        ProtobufSerializer<RequestType> serializer;
        auto serializeResult = serializer.SerializeMessage(request);
        if (!serializeResult.HasValue()) {
            return Result<ResponseType>::FromError(serializeResult.Error());
        }

        // 发送请求
        auto sendData = serializer.GetData();
        auto sendResult = m_manager.send(clientFd, sendData.data(), sendData.size(), timeoutMs);
        if (!sendResult.HasValue()) {
            return Result<ResponseType>::FromError(sendResult.Error());
        }

        // 接收响应 (先读取长度前缀)
        uint32_t networkSize;
        auto recvLenResult = m_manager.receive(clientFd, &networkSize, 4, timeoutMs);
        if (!recvLenResult.HasValue() || recvLenResult.Value() != 4) {
            return Result<ResponseType>::FromError(
                MakeErrorCode(ComErrc::kNetworkBindingFailure, 0));
        }

        uint32_t responseSize = ntohl(networkSize);
        if (responseSize > m_endpoint.maxMessageSize) {
            return Result<ResponseType>::FromError(
                MakeErrorCode(ComErrc::kMessageTooLarge, 0));
        }

        // 接收响应数据
        lap::core::Vector<lap::core::UInt8> responseBuffer(4 + responseSize);
        std::memcpy(responseBuffer.data(), &networkSize, 4);
        auto recvDataResult = m_manager.receive(clientFd, responseBuffer.data() + 4, 
                                               responseSize, timeoutMs);
        if (!recvDataResult.HasValue() || recvDataResult.Value() != responseSize) {
            return Result<ResponseType>::FromError(
                MakeErrorCode(ComErrc::kNetworkBindingFailure, 0));
        }

        // 反序列化响应
        ProtobufDeserializer<ResponseType> deserializer(
            lap::core::MakeSpan(responseBuffer.data(), responseBuffer.size()));
        
        ResponseType response;
        auto deserializeResult = deserializer.DeserializeMessage(response);
        if (!deserializeResult.HasValue()) {
            return Result<ResponseType>::FromError(deserializeResult.Error());
        }

        return Result<ResponseType>::FromValue(std::move(response));
    }

    /**
     * @brief 异步方法调用
     * @param request 请求消息
     * @param callback 回调函数
     * @param timeoutMs 超时时间(毫秒)
     */
    void callAsync(const RequestType& request, CallbackType callback, 
                  uint32_t timeoutMs = 5000) noexcept {
        // 在后台线程执行调用
        std::thread([this, request, callback, timeoutMs]() {
            auto result = call(request, timeoutMs);
            callback(std::move(result));
        }).detach();
    }

private:
    SocketEndpoint m_endpoint;
    SocketConnectionManager& m_manager;
};

/**
 * @brief Socket方法响应器 (服务端)
 */
template<typename RequestType, typename ResponseType>
class SocketMethodResponder {
public:
    using HandlerType = std::function<ResponseType(const RequestType&)>;

    /**
     * @brief 构造函数
     * @param endpoint 服务端端点
     * @param handler 方法处理器
     */
    SocketMethodResponder(const SocketEndpoint& endpoint, HandlerType handler)
        : m_endpoint(endpoint)
        , m_handler(std::move(handler))
        , m_manager(SocketConnectionManager::GetInstance())
        , m_running(false)
    {}

    /**
     * @brief 启动服务
     * @return Result<void> 成功或错误
     */
    Result<void> start() noexcept {
        if (m_running) {
            return Result<void>::FromError(
                MakeErrorCode(ComErrc::kAlreadyRunning, 0));
        }

        // 创建服务端socket
        auto serverResult = m_manager.createServerSocket(m_endpoint);
        if (!serverResult.HasValue()) {
            return Result<void>::FromError(serverResult.Error());
        }
        m_serverFd = serverResult.Value();

        // 启动处理线程
        m_running = true;
        m_thread = std::thread(&SocketMethodResponder::processLoop, this);

        return Result<void>::FromValue();
    }

    /**
     * @brief 停止服务
     */
    void stop() noexcept {
        if (!m_running) return;

        m_running = false;
        if (m_thread.joinable()) {
            m_thread.join();
        }
        m_manager.closeSocket(m_serverFd);
    }

    ~SocketMethodResponder() {
        stop();
    }

private:
    void processLoop() noexcept {
        while (m_running) {
            // 接受客户端连接
            auto clientResult = m_manager.acceptConnection(m_serverFd);
            if (!clientResult.HasValue()) {
                continue;
            }
            int clientFd = clientResult.Value();

            // 在新线程中处理请求
            std::thread([this, clientFd]() {
                handleClient(clientFd);
            }).detach();
        }
    }

    void handleClient(int clientFd) noexcept {
        // RAII: 确保socket关闭
        struct SocketGuard {
            SocketConnectionManager& mgr;
            int fd;
            ~SocketGuard() { mgr.closeSocket(fd); }
        } guard{m_manager, clientFd};

        // 接收请求
        uint32_t networkSize;
        auto recvLenResult = m_manager.receive(clientFd, &networkSize, 4, 5000);
        if (!recvLenResult.HasValue() || recvLenResult.Value() != 4) {
            return;
        }

        uint32_t requestSize = ntohl(networkSize);
        if (requestSize > m_endpoint.maxMessageSize) {
            return;
        }

        lap::core::Vector<lap::core::UInt8> requestBuffer(4 + requestSize);
        std::memcpy(requestBuffer.data(), &networkSize, 4);
        auto recvDataResult = m_manager.receive(clientFd, requestBuffer.data() + 4, 
                                               requestSize, 5000);
        if (!recvDataResult.HasValue() || recvDataResult.Value() != requestSize) {
            return;
        }

        // 反序列化请求
        ProtobufDeserializer<RequestType> deserializer(
            lap::core::MakeSpan(requestBuffer.data(), requestBuffer.size()));
        
        RequestType request;
        auto deserializeResult = deserializer.DeserializeMessage(request);
        if (!deserializeResult.HasValue()) {
            return;
        }

        // 调用处理器
        ResponseType response = m_handler(request);

        // 序列化响应
        ProtobufSerializer<ResponseType> serializer;
        auto serializeResult = serializer.SerializeMessage(response);
        if (!serializeResult.HasValue()) {
            return;
        }

        // 发送响应
        auto sendData = serializer.GetData();
        m_manager.send(clientFd, sendData.data(), sendData.size(), 5000);
    }

    SocketEndpoint m_endpoint;
    HandlerType m_handler;
    SocketConnectionManager& m_manager;
    int m_serverFd{-1};
    std::thread m_thread;
    std::atomic<bool> m_running;
};

} // namespace socket
} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_BINDING_SOCKET_METHOD_BINDING_HPP
```

### 1.4 使用示例

#### 1.4.1 定义Protobuf消息

```protobuf
// calculator.proto
syntax = "proto3";

package example;

message CalculateRequest {
    double operand1 = 1;
    double operand2 = 2;
    string operation = 3;  // "add", "subtract", "multiply", "divide"
}

message CalculateResponse {
    double result = 1;
    string error_message = 2;
}
```

生成C++代码:
```bash
protoc --cpp_out=. calculator.proto
```

#### 1.4.2 服务端实现

```cpp
#include <binding/socket/SocketMethodBinding.hpp>
#include "calculator.pb.h"

using namespace lap::com::binding::socket;

// 定义方法处理器
example::CalculateResponse calculateHandler(const example::CalculateRequest& request) {
    example::CalculateResponse response;
    
    if (request.operation() == "add") {
        response.set_result(request.operand1() + request.operand2());
    } else if (request.operation() == "subtract") {
        response.set_result(request.operand1() - request.operand2());
    } else if (request.operation() == "multiply") {
        response.set_result(request.operand1() * request.operand2());
    } else if (request.operation() == "divide") {
        if (request.operand2() != 0) {
            response.set_result(request.operand1() / request.operand2());
        } else {
            response.set_error_message("Division by zero");
        }
    } else {
        response.set_error_message("Unknown operation");
    }
    
    return response;
}

int main() {
    // 初始化连接管理器
    auto& manager = SocketConnectionManager::GetInstance();
    manager.initialize();
    
    // 配置端点
    SocketEndpoint endpoint{
        .socketPath = "/tmp/calculator.sock",
        .mode = SocketTransportMode::kStream,
        .maxMessageSize = 65536,
        .sendBufferSize = 8192,
        .recvBufferSize = 8192,
        .reuseAddr = true
    };
    
    // 创建方法响应器
    SocketMethodResponder<example::CalculateRequest, example::CalculateResponse> 
        responder(endpoint, calculateHandler);
    
    // 启动服务
    auto result = responder.start();
    if (result.HasValue()) {
        LAP_LOG_INFO("Calculator service started on {}", endpoint.socketPath);
        
        // 等待退出信号
        std::cin.get();
        
        responder.stop();
    } else {
        LAP_LOG_ERROR("Failed to start service: {}", result.Error().Message());
    }
    
    manager.deinitialize();
    return 0;
}
```

#### 1.4.3 客户端实现

```cpp
#include <binding/socket/SocketMethodBinding.hpp>
#include "calculator.pb.h"

using namespace lap::com::binding::socket;

int main() {
    // 初始化连接管理器
    auto& manager = SocketConnectionManager::GetInstance();
    manager.initialize();
    
    // 配置端点
    SocketEndpoint endpoint{
        .socketPath = "/tmp/calculator.sock",
        .mode = SocketTransportMode::kStream,
        .maxMessageSize = 65536
    };
    
    // 创建方法调用器
    SocketMethodCaller<example::CalculateRequest, example::CalculateResponse> caller(endpoint);
    
    // 准备请求
    example::CalculateRequest request;
    request.set_operand1(10.5);
    request.set_operand2(3.2);
    request.set_operation("add");
    
    // 同步调用
    auto result = caller.call(request, 5000);
    if (result.HasValue()) {
        const auto& response = result.Value();
        if (response.error_message().empty()) {
            LAP_LOG_INFO("Result: {}", response.result());
        } else {
            LAP_LOG_ERROR("Error: {}", response.error_message());
        }
    } else {
        LAP_LOG_ERROR("Call failed: {}", result.Error().Message());
    }
    
    // 异步调用
    caller.callAsync(request, [](Result<example::CalculateResponse> asyncResult) {
        if (asyncResult.HasValue()) {
            LAP_LOG_INFO("Async result: {}", asyncResult.Value().result());
        }
    }, 5000);
    
    // 等待异步完成
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    manager.deinitialize();
    return 0;
}
```

### 1.5 集成步骤

1. **添加Protobuf依赖** (CMakeLists.txt):
```cmake
find_package(Protobuf REQUIRED)

add_library(lap_com_socket
    source/binding/socket/SocketConnectionManager.cpp
    # ... 其他实现文件
)

target_link_libraries(lap_com_socket
    PUBLIC lap_core
    PRIVATE protobuf::libprotobuf
)
```

2. **生成Protobuf代码**:
```bash
cd modules/Com/tools/protobuf
protoc --cpp_out=../../source/binding/socket/generated *.proto
```

3. **编写单元测试**:
```cpp
// test/unittest/com_socket_method_test.cpp
TEST(SocketMethodTest, BasicCall) {
    // 测试方法调用
}
```

4. **添加文档**:
- 更新 `doc/COM_ARCHITECTURE.md`
- 创建 `tools/protobuf/README.md`

## 2. 自定义私有协议扩展设计

### 2.1 设计目标

- **极致性能**: 最小化序列化开销
- **灵活性**: 可配置帧格式、压缩、加密
- **可靠性**: 内置CRC校验、重传机制
- **多传输**: 支持TCP、UDP、共享内存

### 2.2 协议格式定义

```
┌────────────────────────────────────────────────────────────┐
│                   LightAP Custom Protocol Frame             │
├─────────┬─────────┬─────────┬──────────┬──────────┬────────┤
│ Magic   │ Version │ Flags   │ Length   │ Payload  │  CRC   │
│ (4B)    │ (1B)    │ (1B)    │ (4B)     │ (N bytes)│ (4B)   │
└─────────┴─────────┴─────────┴──────────┴──────────┴────────┘

Magic:   0x4C415000 ('LAP\0')
Version: Protocol version (0x01)
Flags:   bit0: Compressed
         bit1: Encrypted
         bit2-3: Priority (0-3)
         bit4-5: Message Type (0=Request, 1=Response, 2=Event, 3=Notification)
         bit6-7: Reserved
Length:  Payload length (big-endian)
Payload: Serialized data
CRC:     CRC32 checksum
```

### 2.3 目录结构

```
modules/Com/source/binding/custom/
├── CustomProtocol.hpp           # 协议定义
├── CustomCodec.hpp              # 编解码器
├── CustomTransport.hpp          # 传输层抽象
├── TcpTransport.hpp             # TCP传输实现
├── UdpTransport.hpp             # UDP传输实现
├── ShmTransport.hpp             # 共享内存传输
├── CustomMethodBinding.hpp      # 方法绑定
├── CustomEventBinding.hpp       # 事件绑定
└── CustomFieldBinding.hpp       # 字段绑定
```

### 2.4 核心组件设计概要

#### 2.4.1 CustomProtocol.hpp

定义协议常量、帧结构、消息类型：

```cpp
struct ProtocolHeader {
    uint32_t magic;       // 魔数 0x4C415000
    uint8_t version;      // 版本号
    uint8_t flags;        // 标志位
    uint32_t length;      // 负载长度
};

enum class MessageType : uint8_t {
    kRequest = 0,
    kResponse = 1,
    kEvent = 2,
    kNotification = 3
};

struct ProtocolFrame {
    ProtocolHeader header;
    lap::core::Vector<lap::core::UInt8> payload;
    uint32_t crc;
};
```

#### 2.4.2 CustomCodec.hpp

实现帧的编码/解码、CRC计算、压缩/解压缩：

```cpp
class CustomCodec {
public:
    // 编码帧
    Result<lap::core::Vector<lap::core::UInt8>> encode(const ProtocolFrame& frame);
    
    // 解码帧
    Result<ProtocolFrame> decode(lap::core::Span<const lap::core::UInt8> data);
    
    // 计算CRC
    uint32_t calculateCRC(lap::core::Span<const lap::core::UInt8> data);
    
    // 压缩/解压
    lap::core::Vector<lap::core::UInt8> compress(lap::core::Span<const lap::core::UInt8> data);
    lap::core::Vector<lap::core::UInt8> decompress(lap::core::Span<const lap::core::UInt8> data);
};
```

#### 2.4.3 CustomTransport.hpp

抽象传输层接口，支持多种底层传输：

```cpp
class ITransport {
public:
    virtual ~ITransport() = default;
    
    virtual Result<void> connect(const lap::core::String& address) = 0;
    virtual Result<size_t> send(lap::core::Span<const lap::core::UInt8> data) = 0;
    virtual Result<lap::core::Vector<lap::core::UInt8>> receive(uint32_t timeoutMs) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
};

// TCP, UDP, SharedMemory实现
class TcpTransport : public ITransport { /* ... */ };
class UdpTransport : public ITransport { /* ... */ };
class ShmTransport : public ITransport { /* ... */ };
```

### 2.5 使用示例

```cpp
// 服务端
CustomMethodResponder<MyRequest, MyResponse> responder(
    std::make_unique<TcpTransport>(),  // 选择传输层
    "0.0.0.0:8080",                    // 监听地址
    [](const MyRequest& req) -> MyResponse {
        // 处理请求
        MyResponse resp;
        // ...
        return resp;
    }
);
responder.start();

// 客户端
CustomMethodCaller<MyRequest, MyResponse> caller(
    std::make_unique<TcpTransport>(),
    "127.0.0.1:8080"
);
auto result = caller.call(request);
```

### 2.6 优化特性

1. **零拷贝**: 使用内存映射和引用计数
2. **批量处理**: 支持消息批量发送
3. **优先级队列**: 高优先级消息优先发送
4. **流量控制**: 滑动窗口、拥塞控制
5. **安全性**: 可选加密、认证

## 3. 实施计划

### 3.1 Phase 1: Protobuf over Socket (优先级高)

**预计工作量**: 2-3周

- Week 1: 核心组件实现
  - SocketConnectionManager (3天)
  - ProtobufSerializer (2天)
  
- Week 2: 绑定层实现
  - SocketMethodBinding (3天)
  - SocketEventBinding (2天)
  - SocketFieldBinding (2天)
  
- Week 3: 测试和文档
  - 单元测试 (3天)
  - 集成测试 (2天)
  - 文档和示例 (2天)

### 3.2 Phase 2: 自定义协议 (优先级中)

**预计工作量**: 3-4周

- Week 1: 协议设计和编解码器
  - 协议规范定义 (2天)
  - CustomCodec实现 (3天)
  - CRC和压缩 (2天)
  
- Week 2-3: 传输层实现
  - TCP/UDP Transport (4天)
  - SharedMemory Transport (3天)
  - 传输层抽象和工厂 (3天)
  
- Week 4: 绑定层和优化
  - Method/Event/Field Binding (4天)
  - 性能优化 (2天)
  - 测试和文档 (1天)

### 3.3 集成检查清单

- [ ] 代码实现完成
- [ ] 单元测试通过 (覆盖率 > 80%)
- [ ] 集成测试通过
- [ ] 性能基准测试
- [ ] 内存泄漏检查 (Valgrind)
- [ ] 线程安全验证 (TSan)
- [ ] 文档完善
- [ ] 示例代码
- [ ] Code Review
- [ ] 集成到主分支

## 4. 性能目标

### 4.1 Protobuf over Socket

| 指标 | 目标 | 说明 |
|------|------|------|
| 延迟 | < 100μs | 本地进程间通信 |
| 吞吐量 | > 100k msg/s | 小消息 (< 1KB) |
| 吞吐量 | > 1GB/s | 大消息 (> 1MB) |
| 内存开销 | < 100KB | 每连接固定开销 |
| CPU占用 | < 5% | 单核空闲时 |

### 4.2 自定义协议

| 指标 | 目标 | 说明 |
|------|------|------|
| 延迟 | < 10μs | 共享内存传输 |
| 吞吐量 | > 500k msg/s | 小消息 (< 256B) |
| 压缩率 | > 50% | 文本数据 |
| 序列化速度 | > 10GB/s | 原始数据 |

## 5. 风险和挑战

### 5.1 技术风险

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|----------|
| Protobuf版本兼容性 | 中 | 中 | 使用proto3，明确依赖版本 |
| Unix Socket权限问题 | 低 | 中 | 完善错误处理，文档说明 |
| 共享内存同步复杂 | 高 | 高 | 充分测试，考虑使用Boost.Interprocess |
| 自定义协议调试困难 | 中 | 中 | 提供协议分析工具 |

### 5.2 性能挑战

- **Protobuf序列化开销**: 使用Arena分配器优化
- **Socket系统调用开销**: 批量发送、sendmmsg/recvmmsg
- **内存拷贝**: 零拷贝技术、引用计数

## 6. 测试策略

### 6.1 单元测试

- 连接管理器: 连接、断开、重连、并发
- 序列化器: 各种数据类型、边界条件
- 绑定层: 方法调用、事件、字段、超时

### 6.2 集成测试

- 端到端通信: 客户端-服务端正常流程
- 压力测试: 高并发、大消息、长时间运行
- 异常测试: 网络故障、超时、错误数据

### 6.3 性能测试

- 延迟测试: ping-pong benchmark
- 吞吐量测试: 持续发送大量消息
- 资源监控: CPU、内存、文件描述符

## 7. 文档要求

### 7.1 必须文档

1. **架构设计文档** (本文档)
2. **API参考文档** (Doxygen生成)
3. **用户指南** (如何使用新传输层)
4. **协议规范** (自定义协议详细定义)
5. **性能调优指南**
6. **故障排查手册**

### 7.2 示例代码

- 基础用法: 最简单的客户端/服务端
- 高级用法: 异步、批量、流式传输
- 性能优化: 零拷贝、批量处理
- 错误处理: 重试、超时、降级

## 8. 总结

本扩展指南为Com模块添加Protobuf over Socket和自定义私有协议提供了完整的设计方案：

### 8.1 Protobuf over Socket

✅ **优势**:
- 成熟的序列化框架
- 跨语言支持
- 高性能本地通信
- 易于集成

📋 **适用场景**:
- 本地高性能IPC
- 需要跨语言互操作
- 复杂数据结构传输

### 8.2 自定义协议

✅ **优势**:
- 极致性能优化
- 完全可控
- 灵活定制
- 多传输支持

📋 **适用场景**:
- 性能关键路径
- 特定领域优化
- 嵌入式资源受限环境

### 8.3 与现有传输层对比

| 特性 | D-Bus | SOME/IP | Protobuf+Socket | 自定义协议 |
|------|-------|---------|-----------------|-----------|
| 延迟 | 中 (ms) | 低 (μs) | 极低 (μs) | 极低 (μs) |
| 吞吐量 | 中 | 高 | 极高 | 极高 |
| 易用性 | 高 | 中 | 高 | 低 |
| 灵活性 | 低 | 中 | 高 | 极高 |
| 跨平台 | Linux | 汽车 | 通用 | 通用 |
| 成熟度 | 高 | 高 | 高 | 待开发 |

---

**下一步行动**:
1. Review本设计文档，确认技术方案
2. 创建feature分支
3. 实施Phase 1: Protobuf over Socket
4. 完成测试和文档
5. 合并主分支
6. 启动Phase 2: 自定义协议

**文档版本**: 1.0  
**最后更新**: 2025-10-30  
**作者**: LightAP Team
