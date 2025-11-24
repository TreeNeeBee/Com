/**
 * @file        SocketEventBinding.hpp
 * @brief       Event binding over Unix Domain Sockets using Protobuf framing
 *
 * This header provides:
 *  - SocketEventPublisher<EventT>: Accepts subscriber connections and publishes
 *    events to all connected subscribers.
 *  - SocketEventSubscriber<EventT>: Connects to a publisher and receives events
 *    asynchronously via a user-provided callback.
 *
 * Framing: Length-Delimited [4-byte big-endian length][protobuf payload]
 */

#pragma once

#include <binding/socket/SocketConnectionManager.hpp>
#include <binding/socket/ProtobufSerializer.hpp>

#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>

namespace lap {
namespace com {
namespace binding {
namespace socket {

template <typename EventT>
class SocketEventPublisher {
public:
    explicit SocketEventPublisher(std::string socketPath)
        : socketPath_(std::move(socketPath)) {}

    ~SocketEventPublisher() { stop(); }

    Result<void> start(int listenBacklog = 16) {
        if (running_.load()) return Result<void>({});

        SocketEndpoint ep;
        ep.socketPath = socketPath_;
        ep.mode = SocketTransportMode::kStream;
        ep.listenBacklog = listenBacklog;
        ep.reuseAddr = true;

        auto& mgr = SocketConnectionManager::GetInstance();
        auto init = mgr.initialize();
        if (!init.HasValue()) return Result<void>(init.Error());
        auto res = mgr.createServerSocket(ep);
        if (!res.HasValue()) return Result<void>(res.Error());
        serverFd_ = res.Value();

        running_.store(true);
        acceptThread_ = std::thread([this]() { this->acceptLoop(); });
        return Result<void>({});
    }

    void stop() {
        if (!running_.exchange(false)) return;

        auto& mgr = SocketConnectionManager::GetInstance();
        // Closing server first will break accept
        if (serverFd_ >= 0) {
            mgr.closeSocket(serverFd_);
            serverFd_ = -1;
        }
        if (acceptThread_.joinable()) acceptThread_.join();

        // Close all client sockets
        std::lock_guard<std::mutex> lock(subMutex_);
        for (int fd : subscribers_) {
            mgr.closeSocket(fd);
        }
        subscribers_.clear();
    }

    Result<void> publish(const EventT& evt, int timeoutMs = 2000) {
        if (!running_.load()) {
            return Result<void>(MakeErrorCode(ComErrc::kNotInitialized));
        }
        ProtobufSerializer<EventT> serializer;
            auto res = serializer.SerializeMessage(evt);
            if (!res.HasValue()) return Result<void>(res.Error());
        
            auto frameData = serializer.GetData();

        // 尝试快速吸收可能积压的连接，避免比赛条件导致首次消息丢失
        {
            auto& mgr = SocketConnectionManager::GetInstance();
            while (running_.load()) {
                auto cli = mgr.acceptConnection(serverFd_);
                if (!cli.HasValue()) break;
                std::lock_guard<std::mutex> lock(subMutex_);
                subscribers_.insert(cli.Value());
            }
        }

        std::vector<int> toRemove;
        {
            std::lock_guard<std::mutex> lock(subMutex_);
            for (int fd : subscribers_) {
                size_t totalSent = 0;
                while (totalSent < frameData.size()) {
                    auto sent = SocketConnectionManager::GetInstance().send(
                        fd, frameData.data() + totalSent, frameData.size() - totalSent, timeoutMs);
                    if (!sent.HasValue() || sent.Value() <= 0) {
                        toRemove.push_back(fd);
                        break;
                    }
                    totalSent += static_cast<size_t>(sent.Value());
                }
            }
            for (int fd : toRemove) {
                SocketConnectionManager::GetInstance().closeSocket(fd);
                subscribers_.erase(fd);
            }
        }
        return Result<void>({});
    }

private:
    void acceptLoop() {
        auto& mgr = SocketConnectionManager::GetInstance();
        while (running_.load()) {
                auto cli = mgr.acceptConnection(serverFd_);
            if (!running_.load()) break;
            if (!cli.HasValue()) {
                // timeout or error; continue
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            std::lock_guard<std::mutex> lock(subMutex_);
            subscribers_.insert(cli.Value());
        }
    }

    std::string socketPath_;
    std::atomic<bool> running_{false};
    int serverFd_{-1};
    std::thread acceptThread_;
    std::unordered_set<int> subscribers_;
    std::mutex subMutex_;
};

template <typename EventT>
class SocketEventSubscriber {
public:
    using Callback = std::function<void(const EventT&)>;

    SocketEventSubscriber(std::string socketPath, Callback cb)
        : socketPath_(std::move(socketPath)), callback_(std::move(cb)) {}

    ~SocketEventSubscriber() { stop(); }

    Result<void> start() {
        if (running_.load()) return Result<void>({});
        SocketEndpoint ep;
        ep.socketPath = socketPath_;
        ep.mode = SocketTransportMode::kStream;
        auto& mgr = SocketConnectionManager::GetInstance();
        auto init = mgr.initialize();
        if (!init.HasValue()) return Result<void>(init.Error());
        auto cli = mgr.createClientSocket(ep);
        if (!cli.HasValue()) return Result<void>(cli.Error());
        clientFd_ = cli.Value();
        running_.store(true);
        recvThread_ = std::thread([this]() { this->recvLoop(); });
        return Result<void>({});
    }

    void stop() {
        if (!running_.exchange(false)) return;
        auto& mgr = SocketConnectionManager::GetInstance();
        if (clientFd_ >= 0) {
            mgr.closeSocket(clientFd_);
            clientFd_ = -1;
        }
        if (recvThread_.joinable()) recvThread_.join();
    }

private:
    void recvLoop() {
        auto& mgr = SocketConnectionManager::GetInstance();
        while (running_.load()) {
            // Read 4-byte length
            uint32_t netlen = 0;
            auto r1 = mgr.receive(clientFd_, &netlen, sizeof(netlen), 2000);
            if (!running_.load()) break;
            if (!r1.HasValue()) continue;
            if (r1.Value() != sizeof(netlen)) continue;
            uint32_t len = ntohl(netlen);
            if (len == 0 || len > (10u << 20)) { // sanity: limit to 10MB
                continue;
            }
            // allocate buffer for [len-prefix + payload]
            std::vector<lap::core::UInt8> buf(len + 4);
            std::memcpy(buf.data(), &netlen, 4);
            size_t off = 4;
            while (off < buf.size()) {
                auto r = mgr.receive(clientFd_, buf.data() + off, buf.size() - off, 2000);
                if (!r.HasValue() || r.Value() <= 0) break;
                off += static_cast<size_t>(r.Value());
            }
            if (off != buf.size()) continue;
            // Deserialize one message from this frame
            ProtobufDeserializer<EventT> deserializer(lap::core::MakeSpan(buf.data(), buf.size()));
            EventT evt;
            auto dres = deserializer.DeserializeMessage(evt);
            if (dres.HasValue() && callback_) { callback_(evt); }
        }
    }

    std::string socketPath_;
    Callback callback_;
    std::atomic<bool> running_{false};
    int clientFd_{-1};
    std::thread recvThread_;
};

} // namespace socket
} // namespace binding
} // namespace com
} // namespace lap
