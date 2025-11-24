/**
 * @file        Iceoryx2Binding.cpp
 * @author      LightAP Development Team
 * @brief       iceoryx2 zero-copy IPC binding implementation
 * @date        2025-11-22
 * @copyright   Copyright (c) 2025
 */

#include "Iceoryx2Binding.hpp"
#include "ComTypes.hpp"

#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstring>
#include <cstring>  // strlen
#include <unistd.h>

using lap::com::MakeErrorCode;

namespace lap
{
namespace com
{
namespace binding
{

using lap::core::Result;

// ========================================================================
// Constructor / Destructor
// ========================================================================

Iceoryx2Binding::Iceoryx2Binding() noexcept
    : initialized_(false)
{
    metrics_ = TransportMetrics();
}

Iceoryx2Binding::~Iceoryx2Binding() noexcept
{
    Shutdown();
}

// ========================================================================
// Lifecycle Management
// ========================================================================

Result<void> Iceoryx2Binding::Initialize() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_)
    {
        LAP_COM_LOG_WARN << "Iceoryx2Binding already initialized";
        return Result<void>::FromValue();
    }

    LAP_COM_LOG_INFO << "Initializing iceoryx2 binding";

    // Generate unique node name
    node_name_ = "lap_com_" + std::to_string(getpid());

    // Initialize iceoryx2 node using C API
    iox2_node_builder_h node_builder_handle = iox2_node_builder_new(NULL);
    if (node_builder_handle == NULL)
    {
        LAP_COM_LOG_ERROR << "Failed to create node builder";
        return Result<void>::FromError(
            MakeErrorCode(ComErrc::kNotInitialized, 0));
    }

    // Set node name
    iox2_node_name_h node_name_handle = NULL;
    if (iox2_node_name_new(NULL, node_name_.c_str(), node_name_.length(), &node_name_handle) != IOX2_OK)
    {
        LAP_COM_LOG_ERROR << "Failed to create node name: " << node_name_;
        return Result<void>::FromError(
            MakeErrorCode(ComErrc::kNotInitialized, 0));
    }

    iox2_node_name_ptr node_name_ptr = iox2_cast_node_name_ptr(node_name_handle);
    iox2_node_builder_set_name(&node_builder_handle, node_name_ptr);

    // Create node for IPC (this consumes node_builder_handle and node_name_handle)
    if (iox2_node_builder_create(node_builder_handle, NULL, iox2_service_type_e_IPC, &node_) != IOX2_OK)
    {
        LAP_COM_LOG_ERROR << "Failed to create iceoryx2 node: " << node_name_;
        return Result<void>::FromError(
            MakeErrorCode(ComErrc::kNotInitialized, 0));
    }

    initialized_ = true;

    LAP_COM_LOG_INFO << "iceoryx2 binding initialized with node: " << node_name_;
    return Result<void>::FromValue();
}

Result<void> Iceoryx2Binding::Shutdown() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_)
    {
        return Result<void>::FromValue();
    }

    LAP_COM_LOG_INFO << "Shutting down iceoryx2 binding";

    // Stop all subscribers
    for (auto& [key, sub] : subscribers_)
    {
        sub->running.store(false, std::memory_order_release);
        if (sub->listener_thread.joinable())
        {
            sub->listener_thread.join();
        }
        
        // Clean up iceoryx2 resources
        if (sub->subscriber != NULL)
        {
            iox2_subscriber_drop(sub->subscriber);
        }
        if (sub->service != NULL)
        {
            iox2_port_factory_pub_sub_drop(sub->service);
        }
    }
    subscribers_.clear();

    // Destroy all publishers
    for (auto& [key, pub] : publishers_)
    {
        if (pub->publisher != NULL)
        {
            iox2_publisher_drop(pub->publisher);
        }
        if (pub->service != NULL)
        {
            iox2_port_factory_pub_sub_drop(pub->service);
        }
    }
    publishers_.clear();
    
    // Destroy node
    if (node_ != NULL)
    {
        iox2_node_drop(node_);
        node_ = NULL;
    }

    initialized_ = false;

    LAP_COM_LOG_INFO << "iceoryx2 binding shutdown complete";
    return Result<void>::FromValue();
}

// ========================================================================
// Service Management
// ========================================================================

Result<void> Iceoryx2Binding::OfferService(uint64_t service_id, uint64_t instance_id) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_)
    {
        LAP_COM_LOG_ERROR << "iceoryx2 binding not initialized";
        return Result<void>::FromError(
            MakeErrorCode(ComErrc::kNotInitialized, 0));
    }

    uint64_t key = makeServiceKey(service_id, instance_id);
    std::string service_name = makeServiceName(service_id, instance_id);

    if (publishers_.find(key) != publishers_.end())
    {
        LAP_COM_LOG_WARN << "Service already offered: " << service_name;
        return Result<void>::FromValue();
    }

    LAP_COM_LOG_INFO << "Offering service: " << service_name;

    auto wrapper = std::make_unique<PublisherWrapper>();
    wrapper->service_id = service_id;
    wrapper->instance_id = instance_id;
    wrapper->service_name = service_name;

    // Create iceoryx2 service using C API
    iox2_service_name_h service_name_handle = NULL;
    if (iox2_service_name_new(NULL, service_name.c_str(), service_name.length(), &service_name_handle) != IOX2_OK)
    {
        LAP_COM_LOG_ERROR << "Invalid service name: " << service_name;
        return Result<void>::FromError(
            MakeErrorCode(ComErrc::kInvalidArgument, 0));
    }

    iox2_service_name_ptr service_name_ptr = iox2_cast_service_name_ptr(service_name_handle);
    iox2_service_builder_h service_builder = iox2_node_service_builder(&node_, NULL, service_name_ptr);
    iox2_service_builder_pub_sub_h service_builder_pub_sub = iox2_service_builder_pub_sub(service_builder);

    // Set payload type (dynamic u8 array)
    // For DYNAMIC type: size is element size (1 byte for u8), alignment is element alignment
    const char* type_name = "u8";
    iox2_service_builder_pub_sub_set_payload_type_details(&service_builder_pub_sub,
                                                           iox2_type_variant_e_DYNAMIC,
                                                           type_name, strlen(type_name),
                                                           1, 1); // element_size=1, element_alignment=1
    
    // Set maximum buffer size for subscribers (from config)
    iox2_service_builder_pub_sub_set_subscriber_max_buffer_size(&service_builder_pub_sub, 
                                                                 config_.subscriber_max_buffer_size);
    
    // Set max publishers and subscribers (from config)
    iox2_service_builder_pub_sub_set_max_publishers(&service_builder_pub_sub, config_.max_publishers);
    iox2_service_builder_pub_sub_set_max_subscribers(&service_builder_pub_sub, config_.max_subscribers);
    
    // Set history size if configured
    if (config_.history_size > 0)
    {
        iox2_service_builder_pub_sub_set_history_size(&service_builder_pub_sub, config_.history_size);
    }

    // Open or create service
    if (iox2_service_builder_pub_sub_open_or_create(service_builder_pub_sub, NULL, &wrapper->service) != IOX2_OK)
    {
        iox2_service_name_drop(service_name_handle);
        LAP_COM_LOG_ERROR << "Failed to create service: " << service_name;
        return Result<void>::FromError(
            MakeErrorCode(ComErrc::kServiceNotOffered, 0));
    }

    iox2_service_name_drop(service_name_handle);

    // Create publisher
    iox2_port_factory_publisher_builder_h publisher_builder = 
        iox2_port_factory_pub_sub_publisher_builder(&wrapper->service, NULL);
    
    // Set maximum slice length for dynamic payloads (from config)
    iox2_port_factory_publisher_builder_set_initial_max_slice_len(&publisher_builder, 
                                                                   config_.publisher_max_slice_len);
    
    if (iox2_port_factory_publisher_builder_create(publisher_builder, NULL, &wrapper->publisher) != IOX2_OK)
    {
        iox2_port_factory_pub_sub_drop(wrapper->service);
        LAP_COM_LOG_ERROR << "Failed to create publisher for: " << service_name;
        return Result<void>::FromError(
            MakeErrorCode(ComErrc::kServiceNotOffered, 0));
    }

    publishers_[key] = std::move(wrapper);

    LAP_COM_LOG_INFO << "Service offered successfully: " << service_name;
    return Result<void>::FromValue();
}

Result<void> Iceoryx2Binding::StopOfferService(uint64_t service_id, uint64_t instance_id) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t key = makeServiceKey(service_id, instance_id);
    std::string service_name = makeServiceName(service_id, instance_id);

    auto it = publishers_.find(key);
    if (it == publishers_.end())
    {
        LAP_COM_LOG_WARN << "Service not offered: " << service_name;
        return Result<void>::FromValue();
    }

    LAP_COM_LOG_INFO << "Stopping service offer: " << service_name;
    
    // Clean up iceoryx2 resources
    if (it->second->publisher != NULL)
    {
        iox2_publisher_drop(it->second->publisher);
    }
    if (it->second->service != NULL)
    {
        iox2_port_factory_pub_sub_drop(it->second->service);
    }
    
    publishers_.erase(it);

    LAP_COM_LOG_INFO << "Service offer stopped: " << service_name;
    return Result<void>::FromValue();
}

Result<std::vector<uint64_t>> Iceoryx2Binding::FindService(uint64_t service_id) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<uint64_t> instances;

    // Find all publishers matching this service_id
    for (const auto& [key, pub] : publishers_)
    {
        if (pub->service_id == service_id)
        {
            instances.push_back(pub->instance_id);
        }
    }

    std::ostringstream oss;
    oss << "FindService: service_id=0x" << std::hex << service_id
        << std::dec << ", found " << instances.size() << " instances";
    LAP_COM_LOG_DEBUG << oss.str();

    return Result<std::vector<uint64_t>>::FromValue(std::move(instances));
}

// ========================================================================
// Event Communication
// ========================================================================

Result<void> Iceoryx2Binding::SendEvent(uint64_t service_id, uint64_t instance_id,
                                         uint32_t event_id, const ByteBuffer& data) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_)
    {
        LAP_COM_LOG_ERROR << "iceoryx2 binding not initialized";
        return Result<void>::FromError(
            MakeErrorCode(ComErrc::kNotInitialized, 0));
    }

    uint64_t key = makeServiceKey(service_id, instance_id);
    std::string service_name = makeServiceName(service_id, instance_id);

    auto it = publishers_.find(key);
    if (it == publishers_.end())
    {
        LAP_COM_LOG_ERROR << "Publisher not found for service: " << service_name;
        return Result<void>::FromError(
            MakeErrorCode(ComErrc::kServiceNotOffered, 0));
    }

    auto start = std::chrono::steady_clock::now();

    // Zero-copy send via iceoryx2 C API
    iox2_sample_mut_h sample = NULL;
    int loan_result = iox2_publisher_loan_slice_uninit(&it->second->publisher, NULL, &sample, data.size());
    if (loan_result != IOX2_OK)
    {
        std::ostringstream oss;
        oss << "Failed to loan sample for service: " << service_name 
            << ", size=" << data.size() << ", error=" << loan_result;
        LAP_COM_LOG_ERROR << oss.str();
        return Result<void>::FromError(
            MakeErrorCode(ComErrc::kNetworkBindingFailure, 0));
    }

    // Get mutable payload and write data
    void* payload = NULL;
    iox2_sample_mut_payload_mut(&sample, &payload, NULL);
    
    std::memcpy(payload, data.data(), data.size());
    
    // Send the sample
    if (iox2_sample_mut_send(sample, NULL) != IOX2_OK)
    {
        LAP_COM_LOG_ERROR << "Failed to send sample for service: " << service_name;
        return Result<void>::FromError(
            MakeErrorCode(ComErrc::kNetworkBindingFailure, 0));
    }

    auto end = std::chrono::steady_clock::now();
    uint64_t latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    // Update metrics
    metrics_.messages_sent++;
    metrics_.bytes_sent += data.size();
    if (metrics_.messages_sent == 1) {
        metrics_.avg_latency_ns = latency_ns;
    } else {
        metrics_.avg_latency_ns = (metrics_.avg_latency_ns * (metrics_.messages_sent - 1) + latency_ns) / metrics_.messages_sent;
    }
    metrics_.max_latency_ns = std::max(metrics_.max_latency_ns, latency_ns);
    metrics_.min_latency_ns = std::min(metrics_.min_latency_ns, latency_ns);

    std::ostringstream oss2;
    oss2 << "Event sent: service=" << service_name
         << ", event_id=0x" << std::hex << event_id << std::dec
         << ", size=" << data.size() << " bytes"
         << ", latency=" << latency_ns << " ns";
    LAP_COM_LOG_DEBUG << oss2.str();

    return Result<void>::FromValue();
}

Result<void> Iceoryx2Binding::SubscribeEvent(uint64_t service_id, uint64_t instance_id,
                                               uint32_t event_id, EventCallback callback) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_)
    {
        LAP_COM_LOG_ERROR << "iceoryx2 binding not initialized";
        return Result<void>::FromError(
            MakeErrorCode(ComErrc::kNotInitialized, 0));
    }

    uint64_t key = makeServiceKey(service_id, instance_id);
    std::string service_name = makeServiceName(service_id, instance_id);

    if (subscribers_.find(key) != subscribers_.end())
    {
        LAP_COM_LOG_WARN << "Already subscribed to service: " << service_name;
        return Result<void>::FromValue();
    }

    LAP_COM_LOG_INFO << "Subscribing to service: " << service_name;

    auto wrapper = std::make_unique<SubscriberWrapper>();
    wrapper->service_id = service_id;
    wrapper->instance_id = instance_id;
    wrapper->event_id = event_id;
    wrapper->callback = callback;
    wrapper->service_name = service_name;

    // Create iceoryx2 subscriber using C API
    iox2_service_name_h service_name_handle = NULL;
    if (iox2_service_name_new(NULL, service_name.c_str(), service_name.length(), &service_name_handle) != IOX2_OK)
    {
        LAP_COM_LOG_ERROR << "Invalid service name: " << service_name;
        return Result<void>::FromError(
            MakeErrorCode(ComErrc::kInvalidArgument, 0));
    }

    iox2_service_name_ptr service_name_ptr = iox2_cast_service_name_ptr(service_name_handle);
    iox2_service_builder_h service_builder = iox2_node_service_builder(&node_, NULL, service_name_ptr);
    iox2_service_builder_pub_sub_h service_builder_pub_sub = iox2_service_builder_pub_sub(service_builder);

    // Set payload type (dynamic u8 array - must match publisher)
    const char* type_name = "u8";
    iox2_service_builder_pub_sub_set_payload_type_details(&service_builder_pub_sub,
                                                           iox2_type_variant_e_DYNAMIC,
                                                           type_name, strlen(type_name),
                                                           1, 1); // element_size=1, element_alignment=1

    // Open service (do not create)
    if (iox2_service_builder_pub_sub_open(service_builder_pub_sub, NULL, &wrapper->service) != IOX2_OK)
    {
        iox2_service_name_drop(service_name_handle);
        LAP_COM_LOG_ERROR << "Failed to open service: " << service_name;
        return Result<void>::FromError(
            MakeErrorCode(ComErrc::kServiceNotAvailable, 0));
    }

    iox2_service_name_drop(service_name_handle);

    // Create subscriber
    iox2_port_factory_subscriber_builder_h subscriber_builder = 
        iox2_port_factory_pub_sub_subscriber_builder(&wrapper->service, NULL);
    
    // Set buffer size to match service configuration (from config)
    iox2_port_factory_subscriber_builder_set_buffer_size(&subscriber_builder, 
                                                          config_.subscriber_max_buffer_size);
    
    int create_result = iox2_port_factory_subscriber_builder_create(subscriber_builder, NULL, &wrapper->subscriber);
    if (create_result != IOX2_OK)
    {
        iox2_port_factory_pub_sub_drop(wrapper->service);
        std::ostringstream oss;
        oss << "Failed to create subscriber for: " << service_name << ", error=" << create_result;
        LAP_COM_LOG_ERROR << oss.str();
        return Result<void>::FromError(
            MakeErrorCode(ComErrc::kServiceNotAvailable, 0));
    }

    // Start listener thread
    wrapper->running.store(true, std::memory_order_release);
    wrapper->listener_thread = std::thread(&Iceoryx2Binding::listenerThread, this, wrapper.get());

    subscribers_[key] = std::move(wrapper);

    LAP_COM_LOG_INFO << "Subscribed to service: " << service_name;
    return Result<void>::FromValue();
}

Result<void> Iceoryx2Binding::UnsubscribeEvent(uint64_t service_id, uint64_t instance_id,
                                                 uint32_t /*event_id*/) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t key = makeServiceKey(service_id, instance_id);
    std::string service_name = makeServiceName(service_id, instance_id);

    auto it = subscribers_.find(key);
    if (it == subscribers_.end())
    {
        LAP_COM_LOG_WARN << "Not subscribed to service: " << service_name;
        return Result<void>::FromValue();
    }

    LAP_COM_LOG_INFO << "Unsubscribing from service: " << service_name;

    // Stop listener thread
    it->second->running.store(false, std::memory_order_release);
    if (it->second->listener_thread.joinable())
    {
        it->second->listener_thread.join();
    }

    // Clean up iceoryx2 resources
    if (it->second->subscriber != NULL)
    {
        iox2_subscriber_drop(it->second->subscriber);
    }
    if (it->second->service != NULL)
    {
        iox2_port_factory_pub_sub_drop(it->second->service);
    }

    subscribers_.erase(it);

    LAP_COM_LOG_INFO << "Unsubscribed from service: " << service_name;
    return Result<void>::FromValue();
}

// ========================================================================
// Method Communication (Not Supported)
// ========================================================================

Result<ByteBuffer> Iceoryx2Binding::CallMethod(uint64_t /*service_id*/, uint64_t /*instance_id*/,
                                                 uint32_t /*method_id*/, const ByteBuffer& /*request*/) noexcept
{
    LAP_COM_LOG_ERROR << "CallMethod not supported by iceoryx2 (pub/sub only)";
    return Result<ByteBuffer>::FromError(
        MakeErrorCode(ComErrc::kNetworkBindingFailure, 0));
}

Result<void> Iceoryx2Binding::RegisterMethod(uint64_t /*service_id*/, uint64_t /*instance_id*/,
                                               uint32_t /*method_id*/, MethodCallback /*callback*/) noexcept
{
    LAP_COM_LOG_ERROR << "RegisterMethod not supported by iceoryx2 (pub/sub only)";
    return Result<void>::FromError(
        MakeErrorCode(ComErrc::kNetworkBindingFailure, 0));
}

// ========================================================================
// Field Communication (Not Supported)
// ========================================================================

Result<ByteBuffer> Iceoryx2Binding::GetField(uint64_t /*service_id*/, uint64_t /*instance_id*/,
                                               uint32_t /*field_id*/) noexcept
{
    LAP_COM_LOG_ERROR << "GetField not supported by iceoryx2 (pub/sub only)";
    return Result<ByteBuffer>::FromError(
        MakeErrorCode(ComErrc::kNetworkBindingFailure, 0));
}

Result<void> Iceoryx2Binding::SetField(uint64_t /*service_id*/, uint64_t /*instance_id*/,
                                         uint32_t /*field_id*/, const ByteBuffer& /*data*/) noexcept
{
    LAP_COM_LOG_ERROR << "SetField not supported by iceoryx2 (pub/sub only)";
    return Result<void>::FromError(
        MakeErrorCode(ComErrc::kNetworkBindingFailure, 0));
}

// ========================================================================
// Capability Queries
// ========================================================================

bool Iceoryx2Binding::SupportsService(uint64_t /*service_id*/) const noexcept
{
    // iceoryx2 supports all local IPC services
    return true;
}

TransportMetrics Iceoryx2Binding::GetMetrics() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return metrics_;
}

// ========================================================================
// Internal Methods
// ========================================================================

std::string Iceoryx2Binding::makeServiceName(uint64_t service_id, uint64_t instance_id) const noexcept
{
    std::ostringstream oss;
    oss << "lap_com_" 
        << std::hex << std::setw(4) << std::setfill('0') << (service_id & 0xFFFF)
        << "_"
        << std::hex << std::setw(4) << std::setfill('0') << (instance_id & 0xFFFF);
    return oss.str();
}

uint64_t Iceoryx2Binding::makeServiceKey(uint64_t service_id, uint64_t instance_id) const noexcept
{
    return (service_id << 32) | (instance_id & 0xFFFFFFFF);
}

void Iceoryx2Binding::listenerThread(SubscriberWrapper* wrapper) noexcept
{
    LAP_COM_LOG_INFO << "Listener thread started for service: " << wrapper->service_name;

    while (wrapper->running.load(std::memory_order_acquire))
    {
        // Receive samples from iceoryx2 C API
        iox2_sample_h sample_handle = NULL;
        int result = iox2_subscriber_receive(&wrapper->subscriber, NULL, &sample_handle);
        
        if (result != IOX2_OK || sample_handle == NULL)
        {
            // No data available yet
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // Extract payload
        const void* payload = NULL;
        size_t payload_len = 0;
        iox2_sample_payload(&sample_handle, &payload, &payload_len);
        
        ByteBuffer data(static_cast<const uint8_t*>(payload), 
                       static_cast<const uint8_t*>(payload) + payload_len);
        
        // Invoke user callback with all required parameters
        wrapper->callback(wrapper->service_id, wrapper->instance_id, wrapper->event_id, data);
        
        // Update metrics
        {
            std::lock_guard<std::mutex> lock(mutex_);
            metrics_.messages_received++;
            metrics_.bytes_received += data.size();
        }

        // Drop the sample
        iox2_sample_drop(sample_handle);

        // Small sleep to avoid busy-waiting (use configured poll interval)
        std::this_thread::sleep_for(std::chrono::microseconds(config_.listener_poll_interval_us));
    }

    LAP_COM_LOG_INFO << "Listener thread stopped for service: " << wrapper->service_name;
}

} // namespace binding
} // namespace com
} // namespace lap

// ========================================================================
// C Export Functions
// ========================================================================

extern "C" {

lap::com::binding::ITransportBinding* CreateBindingInstance()
{
    return new lap::com::binding::Iceoryx2Binding();
}

void DestroyBindingInstance(lap::com::binding::ITransportBinding* instance)
{
    delete instance;
}

} // extern "C"
