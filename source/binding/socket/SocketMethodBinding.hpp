/**
 * @file        SocketMethodBinding.hpp
 * @author      LightAP Team
 * @brief       Socket Method Binding with Protobuf
 * @date        2025-10-30
 * @details     Method call/response over Unix Domain Socket using Protobuf serialization
 * @copyright   Copyright (c) 2025
 * @version     1.0
 */

#ifndef LAP_COM_BINDING_SOCKET_METHOD_BINDING_HPP
#define LAP_COM_BINDING_SOCKET_METHOD_BINDING_HPP

#include "SocketConnectionManager.hpp"
#include "ProtobufSerializer.hpp"
#include <functional>
#include <thread>
#include <atomic>
#include <future>
#include <cstring>

namespace lap {
namespace com {
namespace binding {
namespace socket {

/**
 * @brief Socket方法调用器 (客户端)
 * 
 * @tparam RequestType Protobuf请求消息类型
 * @tparam ResponseType Protobuf响应消息类型
 * 
 * @usage
 * SocketEndpoint endpoint{
 *     .socketPath = "/tmp/myservice.sock",
 *     .mode = SocketTransportMode::kStream,
 *     .maxMessageSize = 65536
 * };
 * 
 * SocketMethodCaller<MyRequest, MyResponse> caller(endpoint);
 * 
 * MyRequest request;
 * request.set_value(42);
 * 
 * auto result = caller.call(request, 5000);
 * if (result.HasValue()) {
 *     std::cout << "Response: " << result.Value().result() << std::endl;
 * }
 */
template<typename RequestType, typename ResponseType>
class SocketMethodCaller {
public:
    using CallbackType = std::function<void(Result<ResponseType>)>;

    /**
     * @brief 构造函数
     * @param endpoint 服务端端点配置
     */
    explicit SocketMethodCaller(const SocketEndpoint& endpoint)
        : m_endpoint(endpoint)
        , m_manager(SocketConnectionManager::GetInstance())
    {}

    /**
     * @brief 便捷构造函数，使用socket路径与默认配置
     */
    explicit SocketMethodCaller(const lap::core::String& socketPath)
        : m_manager(SocketConnectionManager::GetInstance()) {
        m_endpoint.socketPath = socketPath;
        m_endpoint.mode = SocketTransportMode::kStream;
        m_endpoint.maxMessageSize = 1u << 20; // 1MB 默认上限
        m_endpoint.sendBufferSize = 0;
        m_endpoint.recvBufferSize = 0;
        m_endpoint.reuseAddr = false;
        m_endpoint.listenBacklog = 0;
    }

    /**
     * @brief 同步方法调用
     * @param request 请求消息
     * @param timeoutMs 超时时间(毫秒)
     * @return Result<ResponseType> 响应消息或错误
     */
    Result<ResponseType> call(const RequestType& request, 
                             lap::core::UInt32 timeoutMs = 5000) noexcept {
        // 确保底层管理器已初始化
        auto init = m_manager.initialize();
        if (!init.HasValue()) {
            return Result<ResponseType>::FromError(init.Error());
        }

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

        // 发送请求（循环直到全部发送）
        auto sendData = serializer.GetData();
        size_t totalSent = 0;
        while (totalSent < sendData.size()) {
            auto sendResult = m_manager.send(
                clientFd,
                sendData.data() + totalSent,
                sendData.size() - totalSent,
                timeoutMs);
            if (!sendResult.HasValue() || sendResult.Value() == 0) {
                if (sendResult.HasValue()) {
                    std::fprintf(stderr, "[SocketMethodCaller] send returned 0 bytes (fd=%d)\n", clientFd);
                } else {
                    { auto sv = sendResult.Error().Message(); std::fprintf(stderr, "[SocketMethodCaller] send error: %.*s\n", (int)sv.size(), sv.data()); }
                }
                return Result<ResponseType>::FromError(
                    sendResult.HasValue() ? MakeErrorCode(ComErrc::kNetworkBindingFailure, 0)
                                          : sendResult.Error());
            }
            totalSent += static_cast<size_t>(sendResult.Value());
        }

        auto recvExact = [this, timeoutMs, clientFd](void* buf, size_t len) -> Result<void> {
            size_t off = 0;
            while (off < len) {
                auto r = m_manager.receive(clientFd, static_cast<char*>(buf) + off, len - off, timeoutMs);
                if (!r.HasValue()) {
                    { auto sv = r.Error().Message(); std::fprintf(stderr, "[SocketMethodCaller] recv error: %.*s\n", (int)sv.size(), sv.data()); }
                    return Result<void>::FromError(r.Error());
                }
                if (r.Value() == 0) {
                    std::fprintf(stderr, "[SocketMethodCaller] recv returned 0 bytes (fd=%d)\n", clientFd);
                    return Result<void>::FromError(MakeErrorCode(ComErrc::kNetworkBindingFailure, 0));
                }
                off += static_cast<size_t>(r.Value());
            }
            return Result<void>::FromValue();
        };

        // 接收响应 (Envelope: [4-byte len][4-byte status][payload...])
        lap::core::UInt32 envelopeLenNetwork = 0;
        {
            auto r = recvExact(&envelopeLenNetwork, 4);
            if (!r.HasValue()) return Result<ResponseType>::FromError(r.Error());
        }
        lap::core::UInt32 envelopeLen = ntohl(envelopeLenNetwork);
        if (envelopeLen < 4 || envelopeLen > m_endpoint.maxMessageSize + 4) {
            std::fprintf(stderr, "[SocketMethodCaller] invalid envelope length=%u (max=%u)\n", envelopeLen, m_endpoint.maxMessageSize + 4);
            return Result<ResponseType>::FromError(MakeErrorCode(ComErrc::kMessageTooLarge, 0));
        }

        lap::core::Vector<lap::core::UInt8> envelopeBuf(envelopeLen);
        {
            auto r = recvExact(envelopeBuf.data(), envelopeLen);
            if (!r.HasValue()) return Result<ResponseType>::FromError(r.Error());
        }

        // 解析状态码
        lap::core::UInt32 statusNetwork = 0;
        std::memcpy(&statusNetwork, envelopeBuf.data(), 4);
        lap::core::Int32 status = static_cast<lap::core::Int32>(ntohl(statusNetwork));
        if (status != 0) {
            std::fprintf(stderr, "[SocketMethodCaller] server returned error status=%d\n", status);
            // 将服务端错误码透传为 ComErrc 值
            return Result<ResponseType>::FromError(
                MakeErrorCode(static_cast<ComErrc>(status), 0));
        }

        // 成功：反序列化payload
        const size_t payloadLen = envelopeLen - 4;
        lap::core::Vector<lap::core::UInt8> frameBuf(4 + payloadLen);
        lap::core::UInt32 payloadLenNetwork = htonl(static_cast<lap::core::UInt32>(payloadLen));
        std::memcpy(frameBuf.data(), &payloadLenNetwork, 4);
        if (payloadLen > 0) {
            std::memcpy(frameBuf.data() + 4, envelopeBuf.data() + 4, payloadLen);
        }

        // 反序列化响应
        ProtobufDeserializer<ResponseType> deserializer(
            lap::core::MakeSpan(frameBuf.data(), frameBuf.size()));
        
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
                  lap::core::UInt32 timeoutMs = 5000) noexcept {
        // 在后台线程执行调用
        std::thread([this, request, callback, timeoutMs]() {
            auto result = call(request, timeoutMs);
            callback(std::move(result));
        }).detach();
    }

    /**
     * @brief 异步方法调用 (返回future)
     * @param request 请求消息
     * @param timeoutMs 超时时间(毫秒)
     * @return std::future<Result<ResponseType>> Future对象
     */
    std::future<Result<ResponseType>> callAsyncFuture(
        const RequestType& request, 
        lap::core::UInt32 timeoutMs = 5000) noexcept {
        
        return std::async(std::launch::async, [this, request, timeoutMs]() {
            return call(request, timeoutMs);
        });
    }

private:
    SocketEndpoint m_endpoint;
    SocketConnectionManager& m_manager;
};

/**
 * @brief Socket方法响应器 (服务端)
 * 
 * @tparam RequestType Protobuf请求消息类型
 * @tparam ResponseType Protobuf响应消息类型
 * 
 * @usage
 * SocketEndpoint endpoint{
 *     .socketPath = "/tmp/myservice.sock",
 *     .mode = SocketTransportMode::kStream,
 *     .maxMessageSize = 65536,
 *     .listenBacklog = 128
 * };
 * 
 * SocketMethodResponder<MyRequest, MyResponse> responder(
 *     endpoint,
 *     [](const MyRequest& req) -> MyResponse {
 *         MyResponse resp;
 *         resp.set_result(req.value() * 2);
 *         return resp;
 *     }
 * );
 * 
 * responder.start();
 * // ... 服务运行
 * responder.stop();
 */
template<typename RequestType, typename ResponseType>
class SocketMethodResponder {
public:
    using HandlerType = std::function<Result<ResponseType>(const RequestType&)>;

    /**
     * @brief 构造函数
     * @param endpoint 服务端端点配置
     * @param handler 方法处理器
     */
    SocketMethodResponder(const SocketEndpoint& endpoint, HandlerType handler)
        : m_endpoint(endpoint)
        , m_handler(std::move(handler))
        , m_manager(SocketConnectionManager::GetInstance())
        , m_running(false)
        , m_serverFd(-1)
    {}

    /**
     * @brief 便捷构造函数（socket路径 + 默认配置）
     */
    SocketMethodResponder(const lap::core::String& socketPath, HandlerType handler)
        : m_handler(std::move(handler))
        , m_manager(SocketConnectionManager::GetInstance())
        , m_running(false)
        , m_serverFd(-1) {
        m_endpoint.socketPath = socketPath;
        m_endpoint.mode = SocketTransportMode::kStream;
        m_endpoint.maxMessageSize = 1u << 20; // 1MB 默认上限
        m_endpoint.sendBufferSize = 0;
        m_endpoint.recvBufferSize = 0;
        m_endpoint.reuseAddr = true;
        m_endpoint.listenBacklog = 128;
    }

    /**
     * @brief 兼容仅返回响应值的处理器
     */
    SocketMethodResponder(const SocketEndpoint& endpoint,
                          std::function<ResponseType(const RequestType&)> valueHandler)
        : m_endpoint(endpoint)
        , m_manager(SocketConnectionManager::GetInstance())
        , m_running(false)
        , m_serverFd(-1) {
        m_handler = [valueHandler = std::move(valueHandler)](const RequestType& req) -> Result<ResponseType> {
            try {
                return Result<ResponseType>::FromValue(valueHandler(req));
            } catch (...) {
                return Result<ResponseType>::FromError(MakeErrorCode(ComErrc::kInternal, 0));
            }
        };
    }

    /**
     * @brief 兼容仅返回响应值的处理器（socket路径）
     */
    SocketMethodResponder(const lap::core::String& socketPath,
                          std::function<ResponseType(const RequestType&)> valueHandler)
        : SocketMethodResponder(SocketEndpoint{socketPath, SocketTransportMode::kStream,
                                               1u << 20, 0, 0, true, 128},
                                std::move(valueHandler)) {}

    /**
     * @brief 启动服务
     * @return Result<void> 成功或错误
     */
    Result<void> start() noexcept {
        if (m_running) {
            return Result<void>::FromError(
                MakeErrorCode(ComErrc::kInvalidState, 0));
        }

        // 确保底层管理器已初始化
        auto init = m_manager.initialize();
        if (!init.HasValue()) {
            return Result<void>::FromError(init.Error());
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
    std::fprintf(stderr, "[SocketMethodResponder] started at %s fd=%d\n", m_endpoint.socketPath.c_str(), m_serverFd);

        return Result<void>::FromValue();
    }

    /**
     * @brief 停止服务
     */
    void stop() noexcept {
        if (!m_running) {
            return;
        }

        m_running = false;
        
        // 关闭服务端socket (会导致accept返回错误)
        if (m_serverFd >= 0) {
            m_manager.closeSocket(m_serverFd);
            m_serverFd = -1;
        }
        
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    /**
     * @brief 检查服务是否运行
     * @return true if running
     */
    bool isRunning() const noexcept {
        return m_running;
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
                if (clientResult.Error().Value() != static_cast<int>(ComErrc::kTimeout)) {
                    std::fprintf(stderr, "[SocketMethodResponder] accept failed while running\n");
                    if (m_running) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }
                continue;
            }
            
            int clientFd = clientResult.Value();
            std::fprintf(stderr, "[SocketMethodResponder] accepted client fd=%d\n", clientFd);

            // 在新线程中处理请求 (支持并发)
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

        auto recvExact = [this, clientFd](void* buf, size_t len, lap::core::UInt32 timeoutMs) -> bool {
            size_t off = 0;
            while (off < len) {
                auto r = m_manager.receive(clientFd, static_cast<char*>(buf) + off, len - off, timeoutMs);
                if (!r.HasValue() || r.Value() == 0) return false;
                off += static_cast<size_t>(r.Value());
            }
            return true;
        };

        // 接收请求长度前缀
        lap::core::UInt32 networkSize = 0;
        if (!recvExact(&networkSize, 4, 5000)) {
            std::fprintf(stderr, "[SocketMethodResponder] failed to read request size\n");
            return;
        }

        lap::core::UInt32 requestSize = ntohl(networkSize);
        if (requestSize > m_endpoint.maxMessageSize) {
            std::fprintf(stderr, "[SocketMethodResponder] request too large: %u\n", requestSize);
            return;
        }

        // 接收请求数据
        lap::core::Vector<lap::core::UInt8> requestBuffer(4 + requestSize);
        std::memcpy(requestBuffer.data(), &networkSize, 4);
        if (!recvExact(requestBuffer.data() + 4, requestSize, 5000)) {
            std::fprintf(stderr, "[SocketMethodResponder] failed to read request payload\n");
            return;
        }

        // 反序列化请求
        ProtobufDeserializer<RequestType> deserializer(
            lap::core::MakeSpan(requestBuffer.data(), requestBuffer.size()));
        
        RequestType request;
        auto deserializeResult = deserializer.DeserializeMessage(request);
        if (!deserializeResult.HasValue()) {
            std::fprintf(stderr, "[SocketMethodResponder] deserialize request failed\n");
            return;
        }

        // 调用处理器
    auto handlerResult = m_handler(request);

        // 发送响应（Envelope: [4-byte len][4-byte status][payload...]）
        auto sendExact = [this, clientFd](const void* buf, size_t len, lap::core::UInt32 timeoutMs) -> bool {
            size_t off = 0;
            while (off < len) {
                auto s = m_manager.send(clientFd, static_cast<const char*>(buf) + off, len - off, timeoutMs);
                if (!s.HasValue() || s.Value() == 0) return false;
                off += static_cast<size_t>(s.Value());
            }
            return true;
        };

        if (!handlerResult.HasValue()) {
            // 错误：发送仅包含错误码的Envelope
            lap::core::UInt32 envelopeLen = htonl(4u);
            lap::core::UInt32 statusNetwork = htonl(static_cast<lap::core::UInt32>(handlerResult.Error().Value()));
            (void)sendExact(&envelopeLen, 4, 5000);
            (void)sendExact(&statusNetwork, 4, 5000);
            std::fprintf(stderr, "[SocketMethodResponder] handler error status=%d\n", handlerResult.Error().Value());
            return;
        }

        // 成功：序列化payload并发送
        ProtobufSerializer<ResponseType> serializer;
        auto serRes = serializer.SerializeMessage(handlerResult.Value());
        if (!serRes.HasValue()) {
            // 序列化失败，发送内部错误
            lap::core::UInt32 envelopeLen = htonl(4u);
            lap::core::UInt32 statusNetwork = htonl(static_cast<lap::core::UInt32>(static_cast<int>(ComErrc::kSerializationError)));
            (void)sendExact(&envelopeLen, 4, 5000);
            (void)sendExact(&statusNetwork, 4, 5000);
            std::fprintf(stderr, "[SocketMethodResponder] serialize response failed\n");
            return;
        }

        auto frame = serializer.GetData(); // [4-byte len][payload]
        // 计算总长度 = 4(status) + payload长度
        lap::core::UInt32 payloadLen = static_cast<lap::core::UInt32>(frame.size() - 4);
        lap::core::UInt32 envelopeLen = htonl(4u + payloadLen);
        lap::core::UInt32 statusNetwork = htonl(0u);
        (void)sendExact(&envelopeLen, 4, 5000);
        (void)sendExact(&statusNetwork, 4, 5000);
        (void)sendExact(frame.data() + 4, payloadLen, 5000);
        std::fprintf(stderr, "[SocketMethodResponder] response sent bytes=%u\n", payloadLen);
    }

    SocketEndpoint m_endpoint;
    HandlerType m_handler;
    SocketConnectionManager& m_manager;
    std::atomic<bool> m_running;
    int m_serverFd;
    std::thread m_thread;
};

} // namespace socket
} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_BINDING_SOCKET_METHOD_BINDING_HPP
