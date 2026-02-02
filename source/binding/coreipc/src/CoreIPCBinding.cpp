/**
 * @file        CoreIPCBinding.cpp
 * @author      LightAP Development Team
 * @brief       Core IPC zero-copy binding implementation with ServiceRegistry integration
 * @date        2026-01-19
 * @copyright   Copyright (c) 2026
 */

#include "CoreIPCBinding.hpp"
#include "ServiceRegistry.hpp"
#include <lap/core/IPCFactory.hpp>
#include <lap/log/CLog.hpp>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <chrono>

namespace lap
{
namespace com
{
namespace binding
{

using namespace lap::core;
using namespace lap::core::ipc;
using namespace lap::com::registry;

namespace
{
constexpr size_t kEventHeaderSize = 8U;  // event_id(4) + payload_size(4)
constexpr size_t kMethodHeaderSize = 20U; // method_id(4) + client_token(8) + status(4) + payload_size(4)
std::mutex g_registry_mutex;
std::shared_ptr<ServiceRegistry> g_shared_registry;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

CoreIPCBinding::CoreIPCBinding() noexcept
    : initialized_(false)
{
}

CoreIPCBinding::~CoreIPCBinding() noexcept
{
    if (initialized_) {
        (void)Shutdown();
    }
}

// ============================================================================
// Lifecycle Management
// ============================================================================

Result<void> CoreIPCBinding::Initialize() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return Result<void>::FromValue();
    }

    LAP_LOG_INFO() << "[CoreIPCBinding] Initializing Core IPC binding with ServiceRegistry";

    // Initialize ServiceRegistry (shared within process)
    {
        std::lock_guard<std::mutex> reg_lock(g_registry_mutex);
        if (!g_shared_registry) {
            g_shared_registry = std::make_shared<ServiceRegistry>();
            auto reg_result = g_shared_registry->Initialize();
            if (!reg_result) {
                LAP_LOG_ERROR() << "[CoreIPCBinding] Failed to initialize ServiceRegistry";
                g_shared_registry.reset();
                return Result<void>::FromError(reg_result.Error());
            }
        }
        service_registry_ = g_shared_registry;
    }

    // Initialize metrics
    metrics_ = {};
    metrics_.bytes_sent = 0;
    metrics_.bytes_received = 0;
    metrics_.messages_sent = 0;
    metrics_.messages_received = 0;

    initialized_ = true;
    LAP_LOG_INFO() << "[CoreIPCBinding] Initialization complete";
    
    return Result<void>::FromValue();
}

Result<void> CoreIPCBinding::Shutdown() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return Result<void>::FromValue();
    }

    LAP_LOG_INFO() << "[CoreIPCBinding] Shutting down Core IPC binding";

    // Stop all subscribers
    for (auto& [key, wrapper] : subscribers_) {
        wrapper->running = false;
        if (wrapper->listener_thread.joinable()) {
            wrapper->listener_thread.join();
        }
    }
    subscribers_.clear();

    // Stop all method servers
    for (auto& [key, wrapper] : method_servers_) {
        wrapper->running = false;
        if (wrapper->worker_thread.joinable()) {
            wrapper->worker_thread.join();
        }
    }
    method_servers_.clear();

    // Clear method clients
    method_clients_.clear();

    // Clear shared memory segments created by this binding
    shm_segments_.clear();

    // Clear publishers
    publishers_.clear();

    // Release shared ServiceRegistry reference
    {
        std::lock_guard<std::mutex> reg_lock(g_registry_mutex);
        service_registry_.reset();
        if (g_shared_registry && g_shared_registry.use_count() == 1) {
            g_shared_registry.reset();
        }
    }

    initialized_ = false;
    LAP_LOG_INFO() << "[CoreIPCBinding] Shutdown complete";
    
    return Result<void>::FromValue();
}

// ============================================================================
// Service Management
// ============================================================================

Result<void> CoreIPCBinding::OfferService(uint64_t service_id, uint64_t instance_id) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kIPCInvalidState));
    }

    LAP_LOG_INFO() << "[CoreIPCBinding] OfferService: service_id=0x" << std::hex << service_id
               << ", instance_id=0x" << instance_id;

    // Create publisher for this service
    auto key = makeServiceKey(service_id, instance_id);
    
    // Check if already offered
    if (publishers_.find(key) != publishers_.end()) {
        LAP_LOG_WARN() << "[CoreIPCBinding] Service already offered";
        return Result<void>::FromValue();
    }

    // Generate SHM path
    auto shm_path = makeServicePath(service_id, instance_id);

    // Ensure SHM exists for event channel
    SharedMemoryConfig shm_config{};
    shm_config.max_chunks = static_cast<UInt16>(config_.max_chunks);
    shm_config.chunk_size = static_cast<UInt32>(config_.max_payload_size + kEventHeaderSize);
    shm_config.ipc_type = IPCType::kSPMC;
    shm_config.channel_capacity = kMaxChannelCapacity;

    auto shm_result = ensureSharedMemoryLocked(shm_path, shm_config);
    if (!shm_result) {
        LAP_LOG_ERROR() << "[CoreIPCBinding] Failed to create shared memory: "
                    << shm_result.Error().Message();
        return Result<void>::FromError(shm_result.Error());
    }
    
    // Create publisher
    PublisherConfig pub_config;
    pub_config.max_chunks = config_.max_chunks;
    pub_config.chunk_size = config_.max_payload_size + kEventHeaderSize; // +4 for event_id header
    pub_config.publish_timeout = 100000000; // 100ms
    pub_config.policy = PublishPolicy::kOverwrite;

    auto pub_result = Publisher::Create(shm_path, pub_config);
    if (!pub_result) {
        LAP_LOG_ERROR() << "[CoreIPCBinding] Failed to create publisher: " 
                    << pub_result.Error().Message();
        return Result<void>::FromError(pub_result.Error());
    }

    // Store publisher
    auto wrapper = std::make_unique<PublisherWrapper>(
        service_id, instance_id, shm_path, std::move(pub_result).Value());
    publishers_[key] = std::move(wrapper);

    // Register in ServiceRegistry with shm_path as endpoint
    // ServiceRegistry automatically routes to correct registry based on service_id
    auto reg_result = service_registry_->RegisterService(
        service_id,
        instance_id,
        1, // major_version
        0, // minor_version
        "coreipc",
        shm_path.c_str());
    
    if (!reg_result) {
        LAP_LOG_ERROR() << "[CoreIPCBinding] Failed to register service in registry: "
                    << reg_result.Error().Message();
        publishers_.erase(key);
        return Result<void>::FromError(reg_result.Error());
    }

    LAP_LOG_INFO() << "[CoreIPCBinding] Service offered successfully: " << shm_path;
    return Result<void>::FromValue();
}

Result<void> CoreIPCBinding::StopOfferService(uint64_t service_id, uint64_t instance_id) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kIPCInvalidState));
    }

    LAP_LOG_INFO() << "[CoreIPCBinding] StopOfferService: service_id=0x" << std::hex << service_id
               << ", instance_id=0x" << instance_id;

    auto key = makeServiceKey(service_id, instance_id);
    
    // Remove publisher
    auto it = publishers_.find(key);
    if (it != publishers_.end()) {
        publishers_.erase(it);
    }

    // Stop method server for this service instance
    auto server_it = method_servers_.find(key);
    if (server_it != method_servers_.end()) {
        server_it->second->running = false;
        if (server_it->second->worker_thread.joinable()) {
            server_it->second->worker_thread.join();
        }
        method_servers_.erase(server_it);
    }

    // Release shared memory segments owned by this service
    shm_segments_.erase(makeServicePath(service_id, instance_id));
    shm_segments_.erase(makeMethodRequestPath(service_id, instance_id));
    shm_segments_.erase(makeMethodResponsePath(service_id, instance_id));

    // Unregister from ServiceRegistry
    auto reg_result = service_registry_->UnregisterService(service_id);
    if (!reg_result) {
        LAP_LOG_WARN() << "[CoreIPCBinding] Failed to unregister service from registry: "
                      << reg_result.Error().Message();
    }

    LAP_LOG_INFO() << "[CoreIPCBinding] Service stopped successfully";
    return Result<void>::FromValue();
}

Result<std::vector<uint64_t>> CoreIPCBinding::FindService(uint64_t service_id) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return Result<std::vector<uint64_t>>::FromError(
            MakeErrorCode(CoreErrc::kIPCInvalidState));
    }

    LAP_LOG_INFO() << "[CoreIPCBinding] FindService: service_id=0x" << std::hex << service_id;

    // Query ServiceRegistry (automatically queries correct registry)
    auto slot_result = service_registry_->FindService(service_id);
    
    std::vector<uint64_t> instances;
    
    if (slot_result && slot_result->IsActive()) {
        instances.push_back(slot_result->instance_id);
        LAP_LOG_INFO() << "[CoreIPCBinding] Found service instance: 0x" 
                      << std::hex << slot_result->instance_id 
                      << ", endpoint: " << slot_result->endpoint;
    }

    return Result<std::vector<uint64_t>>::FromValue(instances);
}

// ============================================================================
// Event Communication
// ============================================================================

Result<void> CoreIPCBinding::SendEvent(
    uint64_t service_id, uint64_t instance_id,
    uint32_t event_id, const ByteBuffer& data) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kIPCInvalidState));
    }

    auto key = makeServiceKey(service_id, instance_id);
    auto it = publishers_.find(key);
    
    if (it == publishers_.end()) {
        LAP_LOG_ERROR() << "[CoreIPCBinding] Publisher not found for service";
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }

    // Encode message with event_id header
    auto encoded = encodeEventMessage(event_id, data);

    // Send using Core IPC lambda-based API (channel_id ignored)
    auto result = it->second->publisher.Send([&encoded](UInt8, Byte* buf, Size size) -> Size {
        if (size < encoded.size()) {
            return 0; // Buffer too small
        }
        std::memcpy(buf, encoded.data(), encoded.size());
        return encoded.size();
    }, PublishPolicy::kOverwrite);
    if (!result) {
        LAP_LOG_ERROR() << "[CoreIPCBinding] Failed to send event: " << result.Error().Message();
        return Result<void>::FromError(result.Error());
    }

    metrics_.messages_sent++;
    metrics_.bytes_sent += encoded.size();

    return Result<void>::FromValue();
}

Result<void> CoreIPCBinding::SubscribeEvent(
    uint64_t service_id, uint64_t instance_id,
    uint32_t event_id, EventCallback callback) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kIPCInvalidState));
    }

    LAP_LOG_INFO() << "[CoreIPCBinding] SubscribeEvent: service=0x" << std::hex << service_id
               << ", instance=0x" << instance_id << ", event=" << event_id;

    auto event_key = makeEventKey(service_id, instance_id, event_id);
    
    // Check if already subscribed
    if (subscribers_.find(event_key) != subscribers_.end()) {
        LAP_LOG_WARN() << "[CoreIPCBinding] Already subscribed to this event";
        return Result<void>::FromValue();
    }

    // Query ServiceRegistry to get shm_path from endpoint
    auto slot_result = service_registry_->FindService(service_id);
    
    if (!slot_result || !slot_result->IsActive()) {
        LAP_LOG_ERROR() << "[CoreIPCBinding] Service not found in registry";
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    if (slot_result->instance_id != instance_id) {
        LAP_LOG_ERROR() << "[CoreIPCBinding] Instance ID mismatch";
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInvalidArgument));
    }
    
    std::string shm_path(slot_result->endpoint);
    LAP_LOG_INFO() << "[CoreIPCBinding] Found service endpoint: " << shm_path;
    
    // Create subscriber
    SubscriberConfig sub_config;
    sub_config.channel_capacity = config_.subscriber_queue_capacity;
    sub_config.chunk_size = config_.max_payload_size + kEventHeaderSize;
    sub_config.timeout = 100000000; // 100ms
    sub_config.empty_policy = SubscribePolicy::kBlock;

    auto sub_result = Subscriber::Create(shm_path, sub_config);
    if (!sub_result) {
        LAP_LOG_ERROR() << "[CoreIPCBinding] Failed to create subscriber: " 
                    << sub_result.Error().Message();
        return Result<void>::FromError(sub_result.Error());
    }

    // Create wrapper
    auto wrapper = std::make_unique<SubscriberWrapper>(
        service_id, instance_id, event_id, callback, shm_path,
        std::move(sub_result).Value());

    auto connect_result = wrapper->subscriber.Connect();
    if (!connect_result) {
        LAP_LOG_ERROR() << "[CoreIPCBinding] Failed to connect subscriber: "
                    << connect_result.Error().Message();
        return Result<void>::FromError(connect_result.Error());
    }

    // Drain any stale entries in the subscriber queue
    for (size_t i = 0; i < config_.max_chunks; ++i) {
        auto drain_result = wrapper->subscriber.Receive(SubscribePolicy::kSkip);
        if (!drain_result) {
            auto err = drain_result.Error();
            if (err == CoreErrc::kChannelEmpty || err == CoreErrc::kChannelTimeout) {
                break;
            }
            if (err == CoreErrc::kIPCInvalidState) {
                continue;
            }
            break;
        }
        if (drain_result.Value().empty()) {
            break;
        }
    }

    // Start listener thread
    wrapper->running = true;
    wrapper->listener_thread = std::thread(&CoreIPCBinding::listenerThread, this, wrapper.get());

    subscribers_[event_key] = std::move(wrapper);

    LAP_LOG_INFO() << "[CoreIPCBinding] Event subscription successful: " << shm_path;
    return Result<void>::FromValue();
}

Result<void> CoreIPCBinding::UnsubscribeEvent(
    uint64_t service_id, uint64_t instance_id, uint32_t event_id) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kIPCInvalidState));
    }

    LAP_LOG_INFO() << "[CoreIPCBinding] UnsubscribeEvent: service=0x" << std::hex << service_id
               << ", instance=0x" << instance_id << ", event=" << event_id;

    auto event_key = makeEventKey(service_id, instance_id, event_id);
    auto it = subscribers_.find(event_key);
    
    if (it == subscribers_.end()) {
        LAP_LOG_WARN() << "[CoreIPCBinding] Subscription not found";
        return Result<void>::FromValue();
    }

    // Stop listener thread
    it->second->running = false;
    if (it->second->listener_thread.joinable()) {
        it->second->listener_thread.join();
    }

    // Remove subscriber
    subscribers_.erase(it);

    LAP_LOG_INFO() << "[CoreIPCBinding] Event unsubscription successful";
    return Result<void>::FromValue();
}

// ============================================================================
// Method Communication (Not Supported Yet)
// ============================================================================

Result<ByteBuffer> CoreIPCBinding::CallMethod(
    uint64_t service_id, uint64_t instance_id,
    uint32_t method_id, const ByteBuffer& request) noexcept
{
    if (!initialized_) {
        return Result<ByteBuffer>::FromError(MakeErrorCode(CoreErrc::kIPCInvalidState));
    }

    // Validate service availability
    auto slot_result = service_registry_->FindService(service_id);
    if (!slot_result || !slot_result->IsActive() || slot_result->instance_id != instance_id) {
        return Result<ByteBuffer>::FromError(lap::com::MakeErrorCode(ComErrc::kServiceNotAvailable));
    }

    const auto key = makeServiceKey(service_id, instance_id);
    MethodClientWrapper* client = nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = method_clients_.find(key);
        if (it == method_clients_.end()) {
            auto req_path = makeMethodRequestPath(service_id, instance_id);
            auto resp_path = makeMethodResponsePath(service_id, instance_id);

            PublisherConfig pub_config;
            pub_config.max_chunks = config_.max_chunks;
            pub_config.chunk_size = config_.max_payload_size + kMethodHeaderSize;
            pub_config.publish_timeout = 100000000; // 100ms
            pub_config.policy = PublishPolicy::kOverwrite;
            pub_config.ipc_type = IPCType::kMPSC;

            auto pub_result = Publisher::Create(req_path, pub_config);
            if (!pub_result) {
                return Result<ByteBuffer>::FromError(pub_result.Error());
            }

            SubscriberConfig sub_config;
            sub_config.channel_capacity = config_.subscriber_queue_capacity;
            sub_config.chunk_size = config_.max_payload_size + kMethodHeaderSize;
            sub_config.timeout = 1000000; // 1ms
            sub_config.empty_policy = SubscribePolicy::kSkip;
            sub_config.ipc_type = IPCType::kSPMC;

            auto sub_result = Subscriber::Create(resp_path, sub_config);
            if (!sub_result) {
                return Result<ByteBuffer>::FromError(sub_result.Error());
            }

            auto wrapper = std::make_unique<MethodClientWrapper>(
                service_id, instance_id, req_path, resp_path,
                std::move(pub_result).Value(), std::move(sub_result).Value());

            auto connect_result = wrapper->response_subscriber.Connect();
            if (!connect_result) {
                return Result<ByteBuffer>::FromError(connect_result.Error());
            }

            client = wrapper.get();
            method_clients_[key] = std::move(wrapper);
        } else {
            client = it->second.get();
        }
    }

    if (client == nullptr) {
        return Result<ByteBuffer>::FromError(MakeErrorCode(CoreErrc::kInternalError));
    }

    std::unique_lock<std::mutex> call_lock(client->call_mutex);

    const uint64_t token = method_request_seq_.fetch_add(1, std::memory_order_relaxed);
    auto encoded = encodeMethodMessage(method_id, token, 0, request);

    auto send_result = client->request_publisher.Send([&encoded](UInt8, Byte* buf, Size size) -> Size {
        if (size < encoded.size()) {
            return 0;
        }
        std::memcpy(buf, encoded.data(), encoded.size());
        return encoded.size();
    }, PublishPolicy::kOverwrite);

    if (!send_result) {
        return Result<ByteBuffer>::FromError(send_result.Error());
    }

    metrics_.messages_sent++;
    metrics_.bytes_sent += encoded.size();

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(config_.method_call_timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        auto recv_result = client->response_subscriber.Receive(SubscribePolicy::kSkip);
        if (!recv_result) {
            auto err = recv_result.Error();
            if (err == CoreErrc::kChannelEmpty || err == CoreErrc::kChannelTimeout) {
                std::this_thread::sleep_for(
                    std::chrono::microseconds(config_.method_poll_interval_us));
                continue;
            }
            return Result<ByteBuffer>::FromError(err);
        }

        auto samples = std::move(recv_result).Value();
        if (samples.empty()) {
            std::this_thread::sleep_for(
                std::chrono::microseconds(config_.method_poll_interval_us));
            continue;
        }

        for (auto& sample : samples) {
            uint32_t resp_method_id = 0;
            uint64_t resp_token = 0;
            int32_t status = 0;
            uint32_t payload_size = 0;
            size_t offset = 0;

            if (!decodeMethodMessage(sample.RawData(), sample.RawDataSize(),
                                     resp_method_id, resp_token, status,
                                     payload_size, offset)) {
                metrics_.messages_dropped++;
                continue;
            }

            if (resp_token != token || resp_method_id != method_id) {
                continue;
            }

            metrics_.messages_received++;
            metrics_.bytes_received += sample.RawDataSize();

            if (status != 0) {
                return Result<ByteBuffer>::FromError(
                    lap::com::MakeErrorCode(static_cast<ComErrc>(status)));
            }

            ByteBuffer payload(
                sample.RawData() + offset,
                sample.RawData() + offset + payload_size);

            return Result<ByteBuffer>::FromValue(std::move(payload));
        }

        std::this_thread::sleep_for(
            std::chrono::microseconds(config_.method_poll_interval_us));
    }

    metrics_.timeout_errors++;
    return Result<ByteBuffer>::FromError(lap::com::MakeErrorCode(ComErrc::kTimeout));
}

Result<void> CoreIPCBinding::RegisterMethod(
    uint64_t service_id, uint64_t instance_id,
    uint32_t method_id, MethodCallback callback) noexcept
{
    if (!initialized_) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kIPCInvalidState));
    }

    const auto key = makeServiceKey(service_id, instance_id);
    MethodServerWrapper* server = nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = method_servers_.find(key);
        if (it == method_servers_.end()) {
            auto req_path = makeMethodRequestPath(service_id, instance_id);
            auto resp_path = makeMethodResponsePath(service_id, instance_id);

            SharedMemoryConfig req_shm_config{};
            req_shm_config.max_chunks = static_cast<UInt16>(config_.max_chunks);
            req_shm_config.chunk_size = static_cast<UInt32>(config_.max_payload_size + kMethodHeaderSize);
            req_shm_config.ipc_type = IPCType::kMPSC;
            req_shm_config.channel_capacity = kMaxChannelCapacity;

            auto req_shm_result = ensureSharedMemoryLocked(req_path, req_shm_config);
            if (!req_shm_result) {
                LAP_LOG_ERROR() << "[CoreIPCBinding] Failed to create request SHM: "
                            << req_shm_result.Error().Message();
                return Result<void>::FromError(req_shm_result.Error());
            }

            SharedMemoryConfig resp_shm_config{};
            resp_shm_config.max_chunks = static_cast<UInt16>(config_.max_chunks);
            resp_shm_config.chunk_size = static_cast<UInt32>(config_.max_payload_size + kMethodHeaderSize);
            resp_shm_config.ipc_type = IPCType::kSPMC;
            resp_shm_config.channel_capacity = kMaxChannelCapacity;

            auto resp_shm_result = ensureSharedMemoryLocked(resp_path, resp_shm_config);
            if (!resp_shm_result) {
                LAP_LOG_ERROR() << "[CoreIPCBinding] Failed to create response SHM: "
                            << resp_shm_result.Error().Message();
                return Result<void>::FromError(resp_shm_result.Error());
            }

            SubscriberConfig sub_config;
            sub_config.channel_capacity = config_.subscriber_queue_capacity;
            sub_config.chunk_size = config_.max_payload_size + kMethodHeaderSize;
            sub_config.timeout = 1000000; // 1ms
            sub_config.empty_policy = SubscribePolicy::kSkip;
            sub_config.ipc_type = IPCType::kMPSC;

            auto sub_result = Subscriber::Create(req_path, sub_config);
            if (!sub_result) {
                return Result<void>::FromError(sub_result.Error());
            }

            PublisherConfig pub_config;
            pub_config.max_chunks = config_.max_chunks;
            pub_config.chunk_size = config_.max_payload_size + kMethodHeaderSize;
            pub_config.publish_timeout = 100000000; // 100ms
            pub_config.policy = PublishPolicy::kOverwrite;
            pub_config.ipc_type = IPCType::kSPMC;

            auto pub_result = Publisher::Create(resp_path, pub_config);
            if (!pub_result) {
                return Result<void>::FromError(pub_result.Error());
            }

            auto wrapper = std::make_unique<MethodServerWrapper>(
                service_id, instance_id, req_path, resp_path,
                std::move(sub_result).Value(), std::move(pub_result).Value());

            auto connect_result = wrapper->request_subscriber.Connect();
            if (!connect_result) {
                return Result<void>::FromError(connect_result.Error());
            }

            wrapper->running = true;
            wrapper->worker_thread = std::thread(&CoreIPCBinding::methodServerThread, this, wrapper.get());

            server = wrapper.get();
            method_servers_[key] = std::move(wrapper);
        } else {
            server = it->second.get();
        }
    }

    if (server == nullptr) {
        return Result<void>::FromError(MakeErrorCode(CoreErrc::kInternalError));
    }

    {
        std::lock_guard<std::mutex> lock(server->handler_mutex);
        server->handlers[method_id] = std::move(callback);
    }

    return Result<void>::FromValue();
}

// ============================================================================
// Field Communication (Not Supported Yet)
// ============================================================================

Result<ByteBuffer> CoreIPCBinding::GetField(
    uint64_t service_id, uint64_t instance_id, uint32_t field_id) noexcept
{
    const uint32_t getter_method_id = field_id | 0x10000U;
    return CallMethod(service_id, instance_id, getter_method_id, ByteBuffer{});
}

Result<void> CoreIPCBinding::SetField(
    uint64_t service_id, uint64_t instance_id,
    uint32_t field_id, const ByteBuffer& data) noexcept
{
    const uint32_t setter_method_id = field_id | 0x20000U;
    auto result = CallMethod(service_id, instance_id, setter_method_id, data);
    if (!result) {
        return Result<void>::FromError(result.Error());
    }
    return Result<void>::FromValue();
}

// ============================================================================
// Capability Queries
// ============================================================================

bool CoreIPCBinding::SupportsService(uint64_t service_id) const noexcept
{
    // Core IPC binding supports all local services
    return true;
}

TransportMetrics CoreIPCBinding::GetMetrics() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return metrics_;
}

// ============================================================================
// Private Helpers
// ============================================================================

std::string CoreIPCBinding::makeServicePath(uint64_t service_id, uint64_t instance_id) const noexcept
{
    std::ostringstream oss;
    oss << "/lap_ipc_" << std::hex << std::setfill('0')
        << std::setw(4) << service_id << "_"
        << std::setw(4) << instance_id;
    return oss.str();
}

std::string CoreIPCBinding::makeMethodRequestPath(uint64_t service_id, uint64_t instance_id) const noexcept
{
    std::ostringstream oss;
    oss << "/lap_ipc_method_req_" << std::hex << std::setfill('0')
        << std::setw(4) << service_id << "_"
        << std::setw(4) << instance_id;
    return oss.str();
}

std::string CoreIPCBinding::makeMethodResponsePath(uint64_t service_id, uint64_t instance_id) const noexcept
{
    std::ostringstream oss;
    oss << "/lap_ipc_method_resp_" << std::hex << std::setfill('0')
        << std::setw(4) << service_id << "_"
        << std::setw(4) << instance_id;
    return oss.str();
}

uint64_t CoreIPCBinding::makeServiceKey(uint64_t service_id, uint64_t instance_id) const noexcept
{
    return (service_id << 32) | instance_id;
}

uint64_t CoreIPCBinding::makeEventKey(
    uint64_t service_id, uint64_t instance_id, uint32_t event_id) const noexcept
{
    return (service_id << 32) | (instance_id << 16) | event_id;
}

ByteBuffer CoreIPCBinding::encodeEventMessage(uint32_t event_id, const ByteBuffer& payload) const noexcept
{
    ByteBuffer result;
    result.reserve(kEventHeaderSize + payload.size());
    
    // Encode event_id (4 bytes, little-endian)
    result.push_back(static_cast<uint8_t>(event_id & 0xFF));
    result.push_back(static_cast<uint8_t>((event_id >> 8) & 0xFF));
    result.push_back(static_cast<uint8_t>((event_id >> 16) & 0xFF));
    result.push_back(static_cast<uint8_t>((event_id >> 24) & 0xFF));
    
    const uint32_t payload_size = static_cast<uint32_t>(payload.size());
    result.push_back(static_cast<uint8_t>(payload_size & 0xFF));
    result.push_back(static_cast<uint8_t>((payload_size >> 8) & 0xFF));
    result.push_back(static_cast<uint8_t>((payload_size >> 16) & 0xFF));
    result.push_back(static_cast<uint8_t>((payload_size >> 24) & 0xFF));

    // Append payload
    result.insert(result.end(), payload.begin(), payload.end());
    
    return result;
}

bool CoreIPCBinding::decodeEventMessage(const uint8_t* data, size_t size,
                                        uint32_t& event_id, uint32_t& payload_size,
                                        size_t& payload_offset) const noexcept
{
    if (size < kEventHeaderSize) {
        return false;
    }

    event_id = static_cast<uint32_t>(data[0])
             | (static_cast<uint32_t>(data[1]) << 8)
             | (static_cast<uint32_t>(data[2]) << 16)
             | (static_cast<uint32_t>(data[3]) << 24);

    payload_size = static_cast<uint32_t>(data[4])
                 | (static_cast<uint32_t>(data[5]) << 8)
                 | (static_cast<uint32_t>(data[6]) << 16)
                 | (static_cast<uint32_t>(data[7]) << 24);

    payload_offset = kEventHeaderSize;
    return payload_offset + payload_size <= size;
}

ByteBuffer CoreIPCBinding::encodeMethodMessage(uint32_t method_id, uint64_t client_token,
                                               int32_t status, const ByteBuffer& payload) const noexcept
{
    ByteBuffer result;
    result.reserve(kMethodHeaderSize + payload.size());

    result.push_back(static_cast<uint8_t>(method_id & 0xFF));
    result.push_back(static_cast<uint8_t>((method_id >> 8) & 0xFF));
    result.push_back(static_cast<uint8_t>((method_id >> 16) & 0xFF));
    result.push_back(static_cast<uint8_t>((method_id >> 24) & 0xFF));

    for (uint32_t i = 0; i < 8; ++i) {
        result.push_back(static_cast<uint8_t>((client_token >> (i * 8)) & 0xFF));
    }

    uint32_t status_u = static_cast<uint32_t>(status);
    result.push_back(static_cast<uint8_t>(status_u & 0xFF));
    result.push_back(static_cast<uint8_t>((status_u >> 8) & 0xFF));
    result.push_back(static_cast<uint8_t>((status_u >> 16) & 0xFF));
    result.push_back(static_cast<uint8_t>((status_u >> 24) & 0xFF));

    const uint32_t payload_size = static_cast<uint32_t>(payload.size());
    result.push_back(static_cast<uint8_t>(payload_size & 0xFF));
    result.push_back(static_cast<uint8_t>((payload_size >> 8) & 0xFF));
    result.push_back(static_cast<uint8_t>((payload_size >> 16) & 0xFF));
    result.push_back(static_cast<uint8_t>((payload_size >> 24) & 0xFF));

    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

bool CoreIPCBinding::decodeMethodMessage(const uint8_t* data, size_t size,
                                         uint32_t& method_id, uint64_t& client_token,
                                         int32_t& status, uint32_t& payload_size,
                                         size_t& payload_offset) const noexcept
{
    if (size < kMethodHeaderSize) {
        return false;
    }

    method_id = static_cast<uint32_t>(data[0])
              | (static_cast<uint32_t>(data[1]) << 8)
              | (static_cast<uint32_t>(data[2]) << 16)
              | (static_cast<uint32_t>(data[3]) << 24);

    client_token = 0U;
    for (uint32_t i = 0; i < 8; ++i) {
        client_token |= (static_cast<uint64_t>(data[4 + i]) << (i * 8));
    }

    uint32_t status_u = static_cast<uint32_t>(data[12])
                      | (static_cast<uint32_t>(data[13]) << 8)
                      | (static_cast<uint32_t>(data[14]) << 16)
                      | (static_cast<uint32_t>(data[15]) << 24);
    status = static_cast<int32_t>(status_u);

    payload_size = static_cast<uint32_t>(data[16])
                 | (static_cast<uint32_t>(data[17]) << 8)
                 | (static_cast<uint32_t>(data[18]) << 16)
                 | (static_cast<uint32_t>(data[19]) << 24);
    payload_offset = kMethodHeaderSize;
    return payload_offset + payload_size <= size;
}

Result<void> CoreIPCBinding::ensureSharedMemoryLocked(
    const std::string& shm_path,
    const SharedMemoryConfig& config) noexcept
{
    auto it = shm_segments_.find(shm_path);
    if (it != shm_segments_.end()) {
        return Result<void>::FromValue();
    }

    auto shm_result = IPCFactory::CreateSHM(shm_path.c_str(), config);
    if (!shm_result) {
        if (shm_result.Error() == CoreErrc::kIPCShmAlreadyExists) {
            auto shm = lap::core::MakeUnique<SharedMemoryManager>();
            auto open_result = shm->Open(shm_path.c_str(), config);
            if (!open_result) {
                return Result<void>::FromError(open_result.Error());
            }
            shm_segments_[shm_path] = std::move(shm);
            return Result<void>::FromValue();
        }
        return Result<void>::FromError(shm_result.Error());
    }

    shm_segments_[shm_path] = std::move(shm_result).Value();
    return Result<void>::FromValue();
}

void CoreIPCBinding::listenerThread(SubscriberWrapper* wrapper) noexcept
{
    LAP_LOG_INFO() << "[CoreIPCBinding] Listener thread started for service 0x" << std::hex 
               << wrapper->service_id << "_" << wrapper->instance_id;

    while (wrapper->running) {
        // Receive message
        auto result = wrapper->subscriber.Receive(SubscribePolicy::kSkip);

        if (!result) {
            auto err = result.Error();
            if (err == CoreErrc::kChannelEmpty || err == CoreErrc::kChannelTimeout ||
                err == CoreErrc::kIPCInvalidState) {
                LAP_LOG_DEBUG() << "[CoreIPCBinding] Receive transient: " << err.Message();
                std::this_thread::sleep_for(
                    std::chrono::microseconds(config_.listener_poll_interval_us));
                continue;
            }
            LAP_LOG_WARN() << "[CoreIPCBinding] Receive failed: " << err.Message();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        auto samples = std::move(result).Value();
        if (samples.empty()) {
            std::this_thread::sleep_for(std::chrono::microseconds(config_.listener_poll_interval_us));
            continue;
        }

        for (auto& sample : samples) {
            uint32_t event_id = 0;
            uint32_t payload_size = 0;
            size_t offset = 0;

            if (!decodeEventMessage(sample.RawData(), sample.RawDataSize(),
                                    event_id, payload_size, offset)) {
                metrics_.messages_dropped++;
                continue;
            }

            // Check if this is the event we subscribed to
            if (event_id != wrapper->event_id) {
                continue;
            }

            // Extract payload
            ByteBuffer payload(
                sample.RawData() + offset,
                sample.RawData() + offset + payload_size);

            metrics_.messages_received++;
            metrics_.bytes_received += sample.RawDataSize();

            // Invoke callback
            try {
                wrapper->callback(wrapper->service_id, wrapper->instance_id, event_id, payload);
            } catch (const std::exception& e) {
                LAP_LOG_ERROR() << "[CoreIPCBinding] Event callback exception: " << e.what();
            }
        }
    }

    LAP_LOG_INFO() << "[CoreIPCBinding] Listener thread stopped";
}

void CoreIPCBinding::methodServerThread(MethodServerWrapper* wrapper) noexcept
{
    LAP_LOG_INFO() << "[CoreIPCBinding] Method server thread started for service 0x" << std::hex
               << wrapper->service_id << "_" << wrapper->instance_id;

    while (wrapper->running) {
        auto result = wrapper->request_subscriber.Receive(SubscribePolicy::kSkip);

        if (!result) {
            auto err = result.Error();
            if (err == CoreErrc::kChannelEmpty || err == CoreErrc::kChannelTimeout ||
                err == CoreErrc::kIPCInvalidState) {
                LAP_LOG_DEBUG() << "[CoreIPCBinding] Method receive transient: " << err.Message();
                std::this_thread::sleep_for(
                    std::chrono::microseconds(config_.method_poll_interval_us));
                continue;
            }

            LAP_LOG_WARN() << "[CoreIPCBinding] Method receive failed: " << err.Message();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        auto samples = std::move(result).Value();
        if (samples.empty()) {
            std::this_thread::sleep_for(
                std::chrono::microseconds(config_.method_poll_interval_us));
            continue;
        }

        for (auto& sample : samples) {
            uint32_t method_id = 0;
            uint64_t token = 0;
            int32_t status = 0;
            uint32_t payload_size = 0;
            size_t offset = 0;

            if (!decodeMethodMessage(sample.RawData(), sample.RawDataSize(),
                                     method_id, token, status,
                                     payload_size, offset)) {
                metrics_.messages_dropped++;
                continue;
            }

            ByteBuffer request_payload(
                sample.RawData() + offset,
                sample.RawData() + offset + payload_size);

            metrics_.messages_received++;
            metrics_.bytes_received += sample.RawDataSize();

            MethodCallback handler;
            {
                std::lock_guard<std::mutex> lock(wrapper->handler_mutex);
                auto it = wrapper->handlers.find(method_id);
                if (it != wrapper->handlers.end()) {
                    handler = it->second;
                }
            }

            ByteBuffer response_payload;
            int32_t resp_status = 0;

            if (handler) {
                try {
                    response_payload = handler(wrapper->service_id, wrapper->instance_id,
                                               method_id, request_payload);
                } catch (const std::exception& e) {
                    LAP_LOG_ERROR() << "[CoreIPCBinding] Method handler exception: " << e.what();
                    resp_status = static_cast<int32_t>(ComErrc::kInternal);
                }
            } else {
                resp_status = static_cast<int32_t>(ComErrc::kServiceNotAvailable);
            }

            auto encoded = encodeMethodMessage(method_id, token, resp_status, response_payload);

            auto send_result = wrapper->response_publisher.Send([&encoded](UInt8, Byte* buf, Size size) -> Size {
                if (size < encoded.size()) {
                    return 0;
                }
                std::memcpy(buf, encoded.data(), encoded.size());
                return encoded.size();
            }, PublishPolicy::kOverwrite);

            if (!send_result) {
                LAP_LOG_ERROR() << "[CoreIPCBinding] Method response send failed: "
                            << send_result.Error().Message();
                continue;
            }

            metrics_.messages_sent++;
            metrics_.bytes_sent += encoded.size();
        }
    }

    LAP_LOG_INFO() << "[CoreIPCBinding] Method server thread stopped";
}

} // namespace binding
} // namespace com
} // namespace lap

// ============================================================================
// C Export Functions
// ============================================================================

extern "C" {

lap::com::binding::ITransportBinding* CreateBindingInstance()
{
    return new lap::com::binding::CoreIPCBinding();
}

void DestroyBindingInstance(lap::com::binding::ITransportBinding* instance)
{
    delete instance;
}

} // extern "C"
