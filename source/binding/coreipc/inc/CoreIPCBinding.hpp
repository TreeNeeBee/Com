/**
 * @file        CoreIPCBinding.hpp
 * @author      LightAP Development Team
 * @brief       Core IPC zero-copy binding implementation
 * @date        2026-01-19
 * @copyright   Copyright (c) 2026
 * 
 * @details     Core IPC-based transport binding for ultra-low-latency local IPC
 *              - Target latency: < 5µs (P99)
 *              - Zero-copy pub/sub via shared memory
 *              - Lock-free communication
 *              - Priority: 100 (highest for local IPC)
 * 
 * @note        Uses LightAP Core IPC API
 *              Replaces iceoryx2 binding to reduce external dependencies
 * 
 * @compliance  AUTOSAR SWS_CM_00400 - Transport Binding Interface
 *              AUTOSAR SWS_CM_00401 - Binding Management
 */

#ifndef LAP_COM_CORE_IPC_BINDING_HPP
#define LAP_COM_CORE_IPC_BINDING_HPP

#include "ITransportBinding.hpp"
#include "BindingTypes.hpp"
#include "ComTypes.hpp"
#include "ServiceRegistry.hpp"

#include <lap/core/CResult.hpp>
#include <lap/core/COptional.hpp>
#include <lap/core/ipc/Publisher.hpp>
#include <lap/core/ipc/Subscriber.hpp>
#include <lap/core/ipc/SharedMemoryManager.hpp>

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <functional>
#include <atomic>
#include <thread>

namespace lap
{
namespace com
{
namespace binding
{

/**
 * @brief Configuration for Core IPC binding
 */
struct CoreIPCConfig
{
    size_t max_payload_size = 1024;           // Maximum payload size in bytes
    size_t subscriber_queue_capacity = 32;    // Subscriber queue capacity
    size_t max_chunks = 64;                   // Maximum chunks per publisher
    uint32_t listener_poll_interval_us = 100; // Listener thread poll interval in microseconds
    uint32_t method_call_timeout_ms = 1000;   // Method call timeout in milliseconds
    uint32_t method_poll_interval_us = 100;   // Method poll interval in microseconds
};

/**
 * @brief Core IPC zero-copy binding
 */
class CoreIPCBinding : public ITransportBinding
{
public:
    CoreIPCBinding() noexcept;
    ~CoreIPCBinding() noexcept override;

    CoreIPCBinding(const CoreIPCBinding&) = delete;
    CoreIPCBinding& operator=(const CoreIPCBinding&) = delete;
    CoreIPCBinding(CoreIPCBinding&&) = delete;
    CoreIPCBinding& operator=(CoreIPCBinding&&) = delete;

    // Lifecycle
    Result<void> Initialize() noexcept override;
    Result<void> Shutdown() noexcept override;

    // Service Management
    Result<void> OfferService(uint64_t service_id, uint64_t instance_id) noexcept override;
    Result<void> StopOfferService(uint64_t service_id, uint64_t instance_id) noexcept override;
    Result<std::vector<uint64_t>> FindService(uint64_t service_id) noexcept override;

    // Event Communication
    Result<void> SendEvent(uint64_t service_id, uint64_t instance_id,
                           uint32_t event_id, const ByteBuffer& data) noexcept override;
    Result<void> SubscribeEvent(uint64_t service_id, uint64_t instance_id,
                                uint32_t event_id, EventCallback callback) noexcept override;
    Result<void> UnsubscribeEvent(uint64_t service_id, uint64_t instance_id,
                                   uint32_t event_id) noexcept override;

    // Method Communication (Not Supported Yet - Requires MethodChannel)
    Result<ByteBuffer> CallMethod(uint64_t service_id, uint64_t instance_id,
                                   uint32_t method_id, const ByteBuffer& request) noexcept override;
    Result<void> RegisterMethod(uint64_t service_id, uint64_t instance_id,
                                 uint32_t method_id, MethodCallback callback) noexcept override;

    // Field Communication (Not Supported Yet)
    Result<ByteBuffer> GetField(uint64_t service_id, uint64_t instance_id,
                                 uint32_t field_id) noexcept override;
    Result<void> SetField(uint64_t service_id, uint64_t instance_id,
                           uint32_t field_id, const ByteBuffer& data) noexcept override;

    // Capability Queries
    const char* GetName() const noexcept override { return "coreipc"; }
    uint32_t GetPriority() const noexcept override { return 100; }
    uint32_t GetVersion() const noexcept override { return 0x010000; } // v1.0.0
    bool SupportsZeroCopy() const noexcept override { return true; }
    bool SupportsService(uint64_t service_id) const noexcept override;
    TransportMetrics GetMetrics() const noexcept override;

private:
    /**
     * @brief Publisher wrapper for Core IPC
     */
    struct PublisherWrapper
    {
        uint64_t service_id;
        uint64_t instance_id;
        std::string shm_path;
        lap::core::ipc::Publisher publisher;
        
        PublisherWrapper(uint64_t sid, uint64_t iid, const std::string& path,
                        lap::core::ipc::Publisher&& pub)
            : service_id(sid)
            , instance_id(iid)
            , shm_path(path)
            , publisher(std::move(pub))
        {}
    };

    /**
     * @brief Subscriber wrapper for Core IPC
     */
    struct SubscriberWrapper
    {
        uint64_t service_id;
        uint64_t instance_id;
        uint32_t event_id;
        EventCallback callback;
        std::string shm_path;
        std::atomic<bool> running{false};
        std::thread listener_thread;
        lap::core::ipc::Subscriber subscriber;
        
        SubscriberWrapper(uint64_t sid, uint64_t iid, uint32_t eid,
                         EventCallback cb, const std::string& path,
                         lap::core::ipc::Subscriber&& sub)
            : service_id(sid)
            , instance_id(iid)
            , event_id(eid)
            , callback(std::move(cb))
            , shm_path(path)
            , subscriber(std::move(sub))
        {}
    };

    /**
     * @brief Method client wrapper for Core IPC
     */
    struct MethodClientWrapper
    {
        uint64_t service_id;
        uint64_t instance_id;
        std::string request_path;
        std::string response_path;
        lap::core::ipc::Publisher request_publisher;
        lap::core::ipc::Subscriber response_subscriber;
        std::mutex call_mutex;

        MethodClientWrapper(uint64_t sid, uint64_t iid,
                            const std::string& req_path,
                            const std::string& resp_path,
                            lap::core::ipc::Publisher&& req_pub,
                            lap::core::ipc::Subscriber&& resp_sub)
            : service_id(sid)
            , instance_id(iid)
            , request_path(req_path)
            , response_path(resp_path)
            , request_publisher(std::move(req_pub))
            , response_subscriber(std::move(resp_sub))
        {}
    };

    /**
     * @brief Method server wrapper for Core IPC
     */
    struct MethodServerWrapper
    {
        uint64_t service_id;
        uint64_t instance_id;
        std::string request_path;
        std::string response_path;
        lap::core::ipc::Subscriber request_subscriber;
        lap::core::ipc::Publisher response_publisher;
        std::atomic<bool> running{false};
        std::thread worker_thread;
        std::mutex handler_mutex;
        std::map<uint32_t, MethodCallback> handlers;

        MethodServerWrapper(uint64_t sid, uint64_t iid,
                            const std::string& req_path,
                            const std::string& resp_path,
                            lap::core::ipc::Subscriber&& req_sub,
                            lap::core::ipc::Publisher&& resp_pub)
            : service_id(sid)
            , instance_id(iid)
            , request_path(req_path)
            , response_path(resp_path)
            , request_subscriber(std::move(req_sub))
            , response_publisher(std::move(resp_pub))
        {}
    };

    /**
     * @brief Generate SHM path for service
     * @param service_id Service ID
     * @param instance_id Instance ID
     * @return SHM path string
     */
    std::string makeServicePath(uint64_t service_id, uint64_t instance_id) const noexcept;

    /**
     * @brief Generate method request SHM path for service
     */
    std::string makeMethodRequestPath(uint64_t service_id, uint64_t instance_id) const noexcept;

    /**
     * @brief Generate method response SHM path for service
     */
    std::string makeMethodResponsePath(uint64_t service_id, uint64_t instance_id) const noexcept;

    /**
     * @brief Generate unique key for service instance
     */
    uint64_t makeServiceKey(uint64_t service_id, uint64_t instance_id) const noexcept;

    /**
     * @brief Generate unique key for event subscription
     */
    uint64_t makeEventKey(uint64_t service_id, uint64_t instance_id, uint32_t event_id) const noexcept;

    /**
     * @brief Event listener thread function
     */
    void listenerThread(SubscriberWrapper* wrapper) noexcept;

    /**
     * @brief Method server thread function
     */
    void methodServerThread(MethodServerWrapper* wrapper) noexcept;

    /**
     * @brief Encode event message with header
     * @details Message format: [event_id(4)] [payload]
     */
    ByteBuffer encodeEventMessage(uint32_t event_id, const ByteBuffer& payload) const noexcept;

    /**
     * @brief Decode event message header
     * @return true if header is valid
     */
    bool decodeEventMessage(const uint8_t* data, size_t size,
                            uint32_t& event_id, uint32_t& payload_size,
                            size_t& payload_offset) const noexcept;

    /**
     * @brief Ensure shared memory segment exists (caller must hold mutex_)
     */
    Result<void> ensureSharedMemoryLocked(
        const std::string& shm_path,
        const lap::core::ipc::SharedMemoryConfig& config) noexcept;

    /**
     * @brief Encode method message with header
     * @details Message format: [method_id(4)] [client_token(8)] [status(4)] [payload_size(4)] [payload]
     */
    ByteBuffer encodeMethodMessage(uint32_t method_id, uint64_t client_token,
                                   int32_t status, const ByteBuffer& payload) const noexcept;

    /**
     * @brief Decode method message header
     * @return true if header is valid
     */
    bool decodeMethodMessage(const uint8_t* data, size_t size,
                             uint32_t& method_id, uint64_t& client_token,
                             int32_t& status, uint32_t& payload_size,
                             size_t& payload_offset) const noexcept;

    mutable std::mutex mutex_;
    bool initialized_;
    CoreIPCConfig config_;

    // Publishers: service_key -> PublisherWrapper
    std::map<uint64_t, std::unique_ptr<PublisherWrapper>> publishers_;
    
    // Subscribers: event_key -> SubscriberWrapper
    std::map<uint64_t, std::unique_ptr<SubscriberWrapper>> subscribers_;

    // Method clients: service_key -> MethodClientWrapper
    std::map<uint64_t, std::unique_ptr<MethodClientWrapper>> method_clients_;

    // Method servers: service_key -> MethodServerWrapper
    std::map<uint64_t, std::unique_ptr<MethodServerWrapper>> method_servers_;

    // Shared memory segments created by this binding
    std::map<std::string, lap::core::UniqueHandle<lap::core::ipc::SharedMemoryManager>> shm_segments_;

    std::atomic<uint64_t> method_request_seq_{1};

    // Service registry integration (Com's fixed-slot registry)
    std::shared_ptr<registry::ServiceRegistry> service_registry_;

    mutable TransportMetrics metrics_;
};

} // namespace binding
} // namespace com
} // namespace lap

// C Export Functions
extern "C" {
    lap::com::binding::ITransportBinding* CreateBindingInstance();
    void DestroyBindingInstance(lap::com::binding::ITransportBinding* instance);
}

#endif // LAP_COM_CORE_IPC_BINDING_HPP
