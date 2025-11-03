/**
 * @file        SocketFieldBinding.hpp
 * @brief       Field binding over Unix Domain Sockets using Protobuf envelopes
 *
 * Protocol:
 *  - Requests: FieldEnvelope { Op: GET|SET|SUBSCRIBE|UNSUBSCRIBE, payload: bytes }
 *  - Responses/Notifications: FieldValueEnvelope { payload: bytes, error: string }
 *  - Value message is user-provided protobuf MessageLite (ValueT)
 */

#pragma once

#include <binding/socket/SocketConnectionManager.hpp>
#include <binding/socket/ProtobufSerializer.hpp>
#include <cstring>

#include <atomic>
#include <thread>
#include <mutex>
#include <unordered_set>

namespace lap {
namespace com {
namespace binding {
namespace socket {

// Minimal field protocol over socket framing (Length-Delimited):
// Request frame payload: [1 byte op][optional serialized ValueT (for SET)]
//   op: 0=GET, 1=SET, 2=SUBSCRIBE, 3=UNSUBSCRIBE
// Response/Notification frame payload: serialized ValueT

template <typename ValueT>
class SocketFieldServer {
public:
    explicit SocketFieldServer(std::string socketPath, ValueT initialValue = {})
        : socketPath_(std::move(socketPath)), value_(std::move(initialValue)) {}

    ~SocketFieldServer() { stop(); }

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
        auto srv = mgr.createServerSocket(ep);
        if (!srv.HasValue()) return Result<void>(srv.Error());
        serverFd_ = srv.Value();
        running_.store(true);
        acceptThread_ = std::thread([this]() { this->acceptLoop(); });
        return Result<void>({});
    }

    void stop() {
        if (!running_.exchange(false)) return;
        auto& mgr = SocketConnectionManager::GetInstance();
        if (serverFd_ >= 0) {
            mgr.closeSocket(serverFd_);
            serverFd_ = -1;
        }
        if (acceptThread_.joinable()) acceptThread_.join();
        {
            std::lock_guard<std::mutex> lock(clientThreadsMutex_);
            for (auto& th : clientThreads_) {
                if (th.joinable()) th.join();
            }
            clientThreads_.clear();
        }
        std::lock_guard<std::mutex> lock(subMutex_);
        for (int fd : subscribers_) mgr.closeSocket(fd);
        subscribers_.clear();
    }

    // Local set that triggers notifications without remote request
    Result<void> setLocal(const ValueT& v) {
        {
            std::lock_guard<std::mutex> lock(valMutex_);
            value_ = v;
        }
        notifySubscribers();
        return Result<void>({});
    }

private:
    void acceptLoop() {
        auto& mgr = SocketConnectionManager::GetInstance();
        while (running_.load()) {
            auto cli = mgr.acceptConnection(serverFd_);
            if (!running_.load()) break;
            if (!cli.HasValue()) {
                std::fprintf(stderr, "[SocketFieldServer] acceptConnection failed: %s\n", "network error or no pending");
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            std::fprintf(stderr, "[SocketFieldServer] accepted client fd=%d\n", cli.Value());
            {
                std::lock_guard<std::mutex> lock(clientThreadsMutex_);
                clientThreads_.emplace_back(&SocketFieldServer::clientLoop, this, cli.Value());
            }
        }
    }

    void clientLoop(int fd) {
        auto& mgr = SocketConnectionManager::GetInstance();
        while (running_.load()) {
            // Read 4-byte length
            uint32_t netlen = 0;
            auto r1 = mgr.receive(fd, &netlen, sizeof(netlen), 2000);
            if (!r1.HasValue()) { 
                // Timeout or transient error - keep connection alive
                if (!running_.load()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue; 
            }
            if (r1.Value() == 0) { break; }
            if (r1.Value() != sizeof(netlen)) { continue; }
            uint32_t len = ntohl(netlen);
            if (len == 0 || len > (10u << 20)) { continue; }
            std::vector<lap::core::UInt8> buf(len + 4);
            std::memcpy(buf.data(), &netlen, 4);
            size_t off = 4;
            while (off < buf.size()) {
                auto rr = mgr.receive(fd, buf.data() + off, buf.size() - off, 2000);
                if (!rr.HasValue() || rr.Value() <= 0) { break; }
                off += static_cast<size_t>(rr.Value());
            }
            if (off != buf.size()) { break; }
            // Parse minimal envelope
            if (buf.size() < 5) { break; }
            const uint8_t op = static_cast<uint8_t>(buf[4]);
            std::fprintf(stderr, "[SocketFieldServer] recv op=%u from fd=%d payloadLen=%zu\n", (unsigned)op, fd, buf.size() - 5);
            switch (op) {
                case 0: { // GET
                    sendValue(fd);
                    break;
                }
                case 1: { // SET
                    ValueT v;
                    size_t payloadLen = buf.size() - 5; // 4 bytes len + 1 byte op
                    if (payloadLen > 0) {
                        if (!v.ParseFromArray(reinterpret_cast<const void*>(buf.data() + 5), static_cast<int>(payloadLen))) {
                            // ignore malformed set
                            std::fprintf(stderr, "[SocketFieldServer] malformed SET payload, ignoring\n");
                            break;
                        }
                    } else {
                        // empty set ignored
                        std::fprintf(stderr, "[SocketFieldServer] empty SET ignored\n");
                        break;
                    }
                    {
                        std::lock_guard<std::mutex> lock(valMutex_);
                        value_ = v;
                    }
                    sendValue(fd);
                    notifySubscribers();
                    break;
                }
                case 2: { // SUBSCRIBE
                    {
                        std::lock_guard<std::mutex> lock(subMutex_);
                        subscribers_.insert(fd);
                    }
                    std::fprintf(stderr, "[SocketFieldServer] SUBSCRIBE registered fd=%d (subscribers=%zu)\n", fd, subscribers_.size());
                    // Immediately push current value (outside lock to avoid deadlock)
                    sendValue(fd);
                    break;
                }
                case 3: { // UNSUBSCRIBE
                    std::lock_guard<std::mutex> lock(subMutex_);
                    subscribers_.erase(fd);
                    std::fprintf(stderr, "[SocketFieldServer] UNSUBSCRIBE fd=%d (subscribers=%zu)\n", fd, subscribers_.size());
                    break;
                }
                default:
                    std::fprintf(stderr, "[SocketFieldServer] unknown op=%u\n", (unsigned)op);
                    break;
            }
        }
        // cleanup
        std::lock_guard<std::mutex> lock(subMutex_);
        subscribers_.erase(fd);
        SocketConnectionManager::GetInstance().closeSocket(fd);
        std::fprintf(stderr, "[SocketFieldServer] closed fd=%d\n", fd);
    }

    void sendValue(int fd) {
        ValueT copy;
        {
            std::lock_guard<std::mutex> lock(valMutex_);
            copy = value_;
        }
        ProtobufSerializer<ValueT> ser;
        auto sres = ser.SerializeMessage(copy);
        if (!sres.HasValue()) return;
        auto data = ser.GetData();
        size_t total = 0;
        std::fprintf(stderr, "[SocketFieldServer] send value to fd=%d bytes=%zu\n", fd, data.size());
        while (total < data.size()) {
            auto sent = SocketConnectionManager::GetInstance().send(fd, data.data() + total, data.size() - total, 2000);
            if (!sent.HasValue() || sent.Value() <= 0) break;
            total += static_cast<size_t>(sent.Value());
        }
    }

    void notifySubscribers() {
        std::vector<int> toRemove;
        {
            std::lock_guard<std::mutex> lock(subMutex_);
            for (int fd : subscribers_) {
                ValueT copy;
                {
                    std::lock_guard<std::mutex> vlock(valMutex_);
                    copy = value_;
                }
                ProtobufSerializer<ValueT> ser;
                auto sres = ser.SerializeMessage(copy);
                if (!sres.HasValue()) { toRemove.push_back(fd); continue; }
                auto data = ser.GetData();
                size_t total = 0;
                bool ok = true;
                while (total < data.size()) {
                    auto sent = SocketConnectionManager::GetInstance().send(fd, data.data() + total, data.size() - total, 1000);
                    if (!sent.HasValue() || sent.Value() <= 0) { ok = false; break; }
                    total += static_cast<size_t>(sent.Value());
                }
                if (!ok) toRemove.push_back(fd);
            }
            std::fprintf(stderr, "[SocketFieldServer] notify subscribers=%zu, removed=%zu\n", subscribers_.size(), toRemove.size());
            for (int fd : toRemove) {
                SocketConnectionManager::GetInstance().closeSocket(fd);
                subscribers_.erase(fd);
            }
        }
    }

    std::string socketPath_;
    std::atomic<bool> running_{false};
    int serverFd_{-1};
    std::thread acceptThread_;
    std::vector<std::thread> clientThreads_;
    std::mutex clientThreadsMutex_;

    ValueT value_;
    std::mutex valMutex_;

    std::unordered_set<int> subscribers_;
    std::mutex subMutex_;
};

template <typename ValueT>
class SocketFieldClient {
public:
    using UpdateCallback = std::function<void(const ValueT&)>;

    explicit SocketFieldClient(std::string socketPath)
        : socketPath_(std::move(socketPath)) {}

    ~SocketFieldClient() { stop(); }

    Result<void> start() {
        if (connected_.load()) return Result<void>({});
        SocketEndpoint ep;
        ep.socketPath = socketPath_;
        ep.mode = SocketTransportMode::kStream;
    auto& mgr = SocketConnectionManager::GetInstance();
    auto init = mgr.initialize();
    if (!init.HasValue()) return Result<void>(init.Error());
    auto cli = mgr.createClientSocket(ep);
        if (!cli.HasValue()) return Result<void>(cli.Error());
        fd_ = cli.Value();
        connected_.store(true);
        return Result<void>({});
    }

    void stop() {
        if (!connected_.exchange(false)) return;
        if (recvThread_.joinable()) recvThread_.join();
        if (fd_ >= 0) {
            SocketConnectionManager::GetInstance().closeSocket(fd_);
            fd_ = -1;
        }
    }

    Result<ValueT> get(int timeoutMs = 2000) { return roundTrip(0, {}, timeoutMs); }

    Result<ValueT> set(const ValueT& v, int timeoutMs = 2000) {
        std::string bytes; v.SerializeToString(&bytes);
        return roundTrip(1, bytes, timeoutMs);
    }

    Result<void> subscribe(UpdateCallback cb) {
        callback_ = std::move(cb);
        // Send SUBSCRIBE framed request
        std::fprintf(stderr, "[SocketFieldClient] sending SUBSCRIBE...\n");
        auto frame = buildRequestFrame(2, {});
        if (!frame.HasValue()) return Result<void>(frame.Error());
        size_t total = 0;
        while (total < frame.Value().size()) {
            auto s = SocketConnectionManager::GetInstance().send(fd_, frame.Value().data() + total, frame.Value().size() - total, 2000);
            if (!s.HasValue() || s.Value() <= 0) return Result<void>(MakeErrorCode(ComErrc::kNetworkBindingFailure));
            total += static_cast<size_t>(s.Value());
        }
        // Deterministic readiness: the server immediately pushes the current value after SUBSCRIBE.
        // Use that initial value as an ACK before returning.
        auto initial = readValue(2000);
        if (!initial.HasValue()) return Result<void>(initial.Error());
        std::fprintf(stderr, "[SocketFieldClient] SUBSCRIBE ACK (initial value) received\n");
        if (callback_) { callback_(initial.Value()); }
        // Start async recv loop for subsequent notifications after ACK
        recvThread_ = std::thread([this]() { this->recvLoop(); });
        return Result<void>({});
    }

    Result<void> unsubscribe() {
        // Signal recv thread to stop
        connected_.store(false);
        // Send unsubscribe request
        auto frame = buildRequestFrame(3, {});
        if (frame.HasValue()) {
            SocketConnectionManager::GetInstance().send(fd_, frame.Value().data(), frame.Value().size(), 1000);
        }
        // Join recv thread
        if (recvThread_.joinable()) recvThread_.join();
        // Restore connected state for potential future use
        connected_.store(true);
        return Result<void>({});
    }

private:
    Result<std::vector<char>> buildRequestFrame(uint8_t op, const std::string& payload) {
        if (payload.size() > (1u << 30)) return Result<std::vector<char>>(MakeErrorCode(ComErrc::kInvalidArgument));
        std::vector<char> buf;
        buf.reserve(5 + payload.size());
        // We'll fill header later
        uint32_t nlen = htonl(static_cast<uint32_t>(1 + payload.size()));
        buf.resize(4);
        std::memcpy(buf.data(), &nlen, 4);
        buf.push_back(static_cast<char>(op));
        buf.insert(buf.end(), payload.begin(), payload.end());
        return Result<std::vector<char>>(buf);
    }

    Result<ValueT> roundTrip(uint8_t op, const std::string& bytes, int timeoutMs) {
        if (!connected_.load()) return Result<ValueT>(MakeErrorCode(ComErrc::kNotInitialized));
        auto frame = buildRequestFrame(op, bytes);
        if (!frame.HasValue()) return Result<ValueT>(frame.Error());
        auto s = SocketConnectionManager::GetInstance().send(fd_, frame.Value().data(), frame.Value().size(), timeoutMs);
        if (!s.HasValue()) return Result<ValueT>(s.Error());
        // Read response ValueT
        auto resp = readValue(timeoutMs);
        if (!resp.HasValue()) return Result<ValueT>(resp.Error());
        return Result<ValueT>(resp.Value());
    }

    Result<ValueT> readValue(int timeoutMs) {
        auto& mgr = SocketConnectionManager::GetInstance();
        uint32_t netlen = 0;
        auto r1 = mgr.receive(fd_, &netlen, sizeof(netlen), timeoutMs);
        if (!r1.HasValue() || r1.Value() != sizeof(netlen)) return Result<ValueT>(MakeErrorCode(ComErrc::kTimeout));
        uint32_t len = ntohl(netlen);
        if (len == 0 || len > (10u << 20)) return Result<ValueT>(MakeErrorCode(ComErrc::kInvalidArgument));
        std::vector<lap::core::UInt8> buf(len + 4);
        std::memcpy(buf.data(), &netlen, 4);
        size_t off = 4;
        while (off < buf.size()) {
            auto r = mgr.receive(fd_, buf.data() + off, buf.size() - off, timeoutMs);
            if (!r.HasValue() || r.Value() <= 0) return Result<ValueT>(MakeErrorCode(ComErrc::kTimeout));
            off += static_cast<size_t>(r.Value());
        }
        ProtobufDeserializer<ValueT> des(lap::core::MakeSpan(buf.data(), buf.size()));
        ValueT out;
        auto dres = des.DeserializeMessage(out);
        if (!dres.HasValue()) return Result<ValueT>(MakeErrorCode(ComErrc::kSerializationError));
        return Result<ValueT>(out);
    }

    void recvLoop() {
        while (connected_.load()) {
            auto env = readValue(5000);
            if (!env.HasValue()) continue;
            if (!callback_) continue;
            callback_(env.Value());
        }
    }

    std::string socketPath_;
    std::atomic<bool> connected_{false};
    int fd_{-1};
    std::thread recvThread_;
    UpdateCallback callback_;
};

} // namespace socket
} // namespace binding
} // namespace com
} // namespace lap
