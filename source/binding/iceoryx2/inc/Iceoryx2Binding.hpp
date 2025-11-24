/**
 * @file        Iceoryx2Binding.hpp
 * @author      LightAP Development Team
 * @brief       iceoryx2 zero-copy IPC binding implementation
 * @date        2025-11-22
 * @copyright   Copyright (c) 2025
 * 
 * @details     iceoryx2-based transport binding for ultra-low-latency local IPC
 *              - Target latency: < 1µs (P99)
 *              - Zero-copy pub/sub via shared memory
 *              - Lock-free communication
 *              - Priority: 100 (highest for local IPC)
 * 
 * @note        Uses iceoryx2 v0.7.0 C++ API
 *              https://eclipse-iceoryx.github.io/iceoryx2/
 * 
 * @compliance  AUTOSAR SWS_CM_00400 - Transport Binding Interface
 *              AUTOSAR SWS_CM_00401 - Binding Management
 */

#ifndef LAP_COM_ICEORYX2_BINDING_HPP
#define LAP_COM_ICEORYX2_BINDING_HPP

#include "ITransportBinding.hpp"
#include "BindingTypes.hpp"
#include "ComTypes.hpp"

#include <core/CResult.hpp>
#include <core/COptional.hpp>

// iceoryx2 C API
#include <iox2/iceoryx2.h>

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
 * @brief Configuration for iceoryx2 binding
 */
struct Iceoryx2Config
{
    size_t max_payload_size = 1024;           // Maximum payload size in bytes
    size_t subscriber_max_buffer_size = 1024;  // Maximum buffer size for subscribers
    size_t publisher_max_slice_len = 1024;     // Maximum slice length for publishers
    size_t max_publishers = 8;                 // Maximum number of publishers per service
    size_t max_subscribers = 8;                // Maximum number of subscribers per service
    size_t history_size = 0;                   // History depth (0 = no history)
    uint32_t listener_poll_interval_us = 100;  // Listener thread poll interval in microseconds
};

/**
 * @brief iceoryx2 zero-copy IPC binding
 */
class Iceoryx2Binding : public ITransportBinding
{
public:
    Iceoryx2Binding() noexcept;
    ~Iceoryx2Binding() noexcept override;

    Iceoryx2Binding(const Iceoryx2Binding&) = delete;
    Iceoryx2Binding& operator=(const Iceoryx2Binding&) = delete;
    Iceoryx2Binding(Iceoryx2Binding&&) = delete;
    Iceoryx2Binding& operator=(Iceoryx2Binding&&) = delete;

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

    // Method Communication (Not Supported)
    Result<ByteBuffer> CallMethod(uint64_t service_id, uint64_t instance_id,
                                   uint32_t method_id, const ByteBuffer& request) noexcept override;
    Result<void> RegisterMethod(uint64_t service_id, uint64_t instance_id,
                                 uint32_t method_id, MethodCallback callback) noexcept override;

    // Field Communication (Not Supported)
    Result<ByteBuffer> GetField(uint64_t service_id, uint64_t instance_id,
                                 uint32_t field_id) noexcept override;
    Result<void> SetField(uint64_t service_id, uint64_t instance_id,
                           uint32_t field_id, const ByteBuffer& data) noexcept override;

    // Capability Queries
    const char* GetName() const noexcept override { return "iceoryx2"; }
    uint32_t GetPriority() const noexcept override { return 100; }
    uint32_t GetVersion() const noexcept override { return 0x000700; }
    bool SupportsZeroCopy() const noexcept override { return true; }
    bool SupportsService(uint64_t service_id) const noexcept override;
    TransportMetrics GetMetrics() const noexcept override;

private:
    struct PublisherWrapper
    {
        uint64_t service_id;
        uint64_t instance_id;
        std::string service_name;
        iox2_port_factory_pub_sub_h service;
        iox2_publisher_h publisher;
    };

    struct SubscriberWrapper
    {
        uint64_t service_id;
        uint64_t instance_id;
        uint32_t event_id;
        EventCallback callback;
        std::string service_name;
        std::atomic<bool> running{false};
        std::thread listener_thread;
        iox2_port_factory_pub_sub_h service;
        iox2_subscriber_h subscriber;
    };

    std::string makeServiceName(uint64_t service_id, uint64_t instance_id) const noexcept;
    uint64_t makeServiceKey(uint64_t service_id, uint64_t instance_id) const noexcept;
    void listenerThread(SubscriberWrapper* wrapper) noexcept;

    mutable std::mutex mutex_;
    bool initialized_;
    std::string node_name_;
    iox2_node_h node_;
    Iceoryx2Config config_;

    std::map<uint64_t, std::unique_ptr<PublisherWrapper>> publishers_;
    std::map<uint64_t, std::unique_ptr<SubscriberWrapper>> subscribers_;

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

#endif // LAP_COM_ICEORYX2_BINDING_HPP
