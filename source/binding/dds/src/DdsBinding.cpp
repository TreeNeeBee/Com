/**
 * @file        DdsBinding.cpp
 * @author      LightAP Development Team
 * @brief       DDS transport binding implementation with FastDDS
 * @date        2025-11-23
 */

#include "DdsBinding.hpp"
#include "LapComMessage.h"
#include "LapComMessagePubSubTypes.h"
#include "ComTypes.hpp"

#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/dds/publisher/qos/PublisherQos.hpp>
#include <fastdds/dds/publisher/qos/DataWriterQos.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/core/status/PublicationMatchedStatus.hpp>
#include <fastdds/dds/subscriber/qos/SubscriberQos.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/rtps/common/InstanceHandle.h>

#include <chrono>
#include <sstream>
#include <iomanip>

namespace lap
{
namespace com
{
namespace binding
{
    using namespace lap::core;
    using namespace eprosima::fastdds::dds;

    // ========================================================================
    // DdsReaderListener Implementation
    // ========================================================================

    void DdsReaderListener::on_data_available(DataReader* reader)
    {
        std::cout << "[DDS DATA] on_data_available FIRED!" << std::endl << std::flush;
        
        lap::com::binding::LapComMessage msg;
        SampleInfo info;

        if (reader->take_next_sample(&msg, &info) == ReturnCode_t::RETCODE_OK)
        {
            if (info.valid_data)
            {
                // Convert payload to ByteBuffer
                ByteBuffer data(msg.payload().begin(), msg.payload().end());
                
                // Invoke user callback
                callback_(msg.service_id(), msg.instance_id(), msg.event_id(), data);
                
                // Update metrics
                metrics_.messages_received++;
                metrics_.bytes_received += data.size();
            }
        }
    }

    void DdsReaderListener::on_subscription_matched(
        DataReader* reader [[maybe_unused]],
        const SubscriptionMatchedStatus& info)
    {
        std::cout << "[DDS MATCH] Subscription matched! current_count_change=" << info.current_count_change 
                  << ", total=" << info.current_count << std::endl << std::flush;
        
        if (info.current_count_change == 1)
        {
            LAP_COM_LOG_INFO << "DDS subscriber matched with publisher (total=" << info.current_count << ")";
        }
        else if (info.current_count_change == -1)
        {
            LAP_COM_LOG_INFO << "DDS subscriber unmatched from publisher (remaining=" << info.current_count << ")";
        }
    }

    // ========================================================================
    // DdsDiscoveryListener Implementation
    // ========================================================================

    void DdsDiscoveryListener::on_publisher_discovery(
        DomainParticipant* participant [[maybe_unused]],
        eprosima::fastrtps::rtps::WriterDiscoveryInfo&& info)
    {
        using eprosima::fastrtps::rtps::WriterDiscoveryInfo;
        
        // Parse topic name to extract service_id and instance_id
        // Topic name format: "LapComTopic_<service_id>_<instance_id>_<event_id>"
        std::string topic_name = info.info.topicName().to_string();
        
        if (topic_name.find("LapComTopic_") == 0) {
            // Parse service_id from topic name
            size_t first_underscore = topic_name.find('_');
            size_t second_underscore = topic_name.find('_', first_underscore + 1);
            
            if (first_underscore != std::string::npos && second_underscore != std::string::npos) {
                try {
                    std::string service_id_str = topic_name.substr(
                        first_underscore + 1, 
                        second_underscore - first_underscore - 1
                    );
                    uint64_t service_id = std::stoull(service_id_str, nullptr, 16);
                    
                    // Find instance_id
                    size_t third_underscore = topic_name.find('_', second_underscore + 1);
                    if (third_underscore != std::string::npos) {
                        std::string instance_id_str = topic_name.substr(
                            second_underscore + 1,
                            third_underscore - second_underscore - 1
                        );
                        uint64_t instance_id = std::stoull(instance_id_str, nullptr, 16);
                        
                        std::lock_guard<std::mutex> lock(discovery_mutex_);
                        
                        if (info.status == WriterDiscoveryInfo::DISCOVERED_WRITER) {
                            // Add discovered instance
                            discovered_services_[service_id].insert(instance_id);
                            
                            std::ostringstream oss;
                            oss << "Discovered DDS publisher: service=0x" 
                                << std::hex << std::setw(4) << std::setfill('0') << service_id 
                                << ", instance=0x" << std::setw(4) << std::setfill('0') << instance_id
                                << " (topic: " << topic_name << ")";
                            LAP_COM_LOG_INFO << oss.str();
                        }
                        else if (info.status == WriterDiscoveryInfo::REMOVED_WRITER) {
                            // Remove instance
                            auto it = discovered_services_.find(service_id);
                            if (it != discovered_services_.end()) {
                                it->second.erase(instance_id);
                                if (it->second.empty()) {
                                    discovered_services_.erase(it);
                                }
                            }
                            
                            std::ostringstream oss;
                            oss << "Removed DDS publisher: service=0x" 
                                << std::hex << std::setw(4) << std::setfill('0') << service_id 
                                << ", instance=0x" << std::setw(4) << std::setfill('0') << instance_id;
                            LAP_COM_LOG_INFO << oss.str();
                        }
                    }
                }
                catch (const std::exception& e) {
                    LAP_COM_LOG_WARN << "Failed to parse topic name '" << topic_name 
                                     << "': " << e.what();
                }
            }
        }
    }

    std::vector<uint64_t> DdsDiscoveryListener::GetDiscoveredInstances(
        uint64_t service_id,
        eprosima::fastdds::dds::DomainParticipant* participant [[maybe_unused]]
    ) const
    {
        std::lock_guard<std::mutex> lock(discovery_mutex_);
        
        // Check cached discoveries from callbacks
        auto it = discovered_services_.find(service_id);
        if (it != discovered_services_.end() && !it->second.empty()) {
            LAP_COM_LOG_DEBUG << "Found " << it->second.size() 
                              << " cached instances for service 0x" << service_id;
            return std::vector<uint64_t>(it->second.begin(), it->second.end());
        }
        
        // Fallback: For same-process discovery, query the binding's internal state
        // This is handled by the DdsBinding class directly since discovery callbacks
        // may not fire for local entities
        
        LAP_COM_LOG_DEBUG << "No cached discoveries for service 0x" << service_id;
        return std::vector<uint64_t>();
    }

    // ========================================================================
    // Constructor / Destructor
    // ========================================================================

    DdsBinding::DdsBinding()
    {
        LAP_COM_LOG_INFO << "DdsBinding instance created (FastDDS backend)";
        
        // Register IDL type
        type_support_.reset(new lap::com::binding::LapComMessagePubSubType());
    }

    DdsBinding::~DdsBinding()
    {
        Shutdown();
        LAP_COM_LOG_INFO << "DdsBinding instance destroyed";
    }

    // ========================================================================
    // Lifecycle Management
    // ========================================================================

    Result<void> DdsBinding::Initialize() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (participant_ != nullptr) {
            LAP_COM_LOG_WARN << "DdsBinding already initialized";
            return Result<void>::FromValue();
        }

        LAP_COM_LOG_INFO << "Initializing DDS Binding (FastDDS) on domain " << config_.domain_id;

        // Create discovery listener BEFORE participant
        discovery_listener_ = std::make_unique<DdsDiscoveryListener>(this);

        // Create DomainParticipant with discovery listener
        DomainParticipantQos pqos;
        pqos.name("LightAP_DDS_Participant");
        
        participant_ = DomainParticipantFactory::get_instance()->create_participant(
            config_.domain_id,
            pqos,
            discovery_listener_.get()  // Register listener for discovery callbacks
        );

        if (participant_ == nullptr) {
            LAP_COM_LOG_ERROR << "Failed to create DDS participant";
            discovery_listener_.reset();
            return Result<void>::FromError(MakeErrorCode(ComErrc::kNotInitialized));
        }

        // Register type
        type_support_.register_type(participant_);

        // Create default Publisher
        PublisherQos pub_qos;
        publisher_ = participant_->create_publisher(pub_qos);
        if (publisher_ == nullptr) {
            LAP_COM_LOG_ERROR << "Failed to create DDS publisher";
            DomainParticipantFactory::get_instance()->delete_participant(participant_);
            participant_ = nullptr;
            discovery_listener_.reset();
            return Result<void>::FromError(MakeErrorCode(ComErrc::kNotInitialized));
        }

        // Create default Subscriber
        SubscriberQos sub_qos;
        subscriber_ = participant_->create_subscriber(sub_qos);
        if (subscriber_ == nullptr) {
            LAP_COM_LOG_ERROR << "Failed to create DDS subscriber";
            participant_->delete_publisher(publisher_);
            DomainParticipantFactory::get_instance()->delete_participant(participant_);
            participant_ = nullptr;
            publisher_ = nullptr;
            discovery_listener_.reset();
            return Result<void>::FromError(MakeErrorCode(ComErrc::kNotInitialized));
        }

        LAP_COM_LOG_INFO << "DDS Binding initialized successfully";
        LAP_COM_LOG_INFO << "  Domain ID: " << config_.domain_id;
        LAP_COM_LOG_INFO << "  Type: " << type_support_.get_type_name();
        LAP_COM_LOG_INFO << "  Shared Memory: " << (config_.use_shared_memory ? "true" : "false");
        LAP_COM_LOG_INFO << "  AF_XDP Enabled: " << (config_.af_xdp_enabled ? "true" : "false");

        return Result<void>::FromValue();
    }

    Result<void> DdsBinding::Shutdown() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (participant_ == nullptr) {
            return Result<void>::FromValue();
        }

        LAP_COM_LOG_INFO << "Shutting down DDS Binding";

        // Delete all readers
        for (auto& [key, reader] : readers_) {
            if (reader != nullptr) {
                subscriber_->delete_datareader(reader);
            }
        }
        readers_.clear();
        listeners_.clear();

        // Delete all writers
        for (auto& [key, writer] : writers_) {
            if (writer != nullptr) {
                publisher_->delete_datawriter(writer);
            }
        }
        writers_.clear();

        // Delete all topics
        for (auto& [key, topic] : topics_) {
            if (topic != nullptr) {
                participant_->delete_topic(topic);
            }
        }
        topics_.clear();

        // Delete DDS entities
        if (subscriber_ != nullptr) {
            participant_->delete_subscriber(subscriber_);
            subscriber_ = nullptr;
        }

        if (publisher_ != nullptr) {
            participant_->delete_publisher(publisher_);
            publisher_ = nullptr;
        }

        if (participant_ != nullptr) {
            DomainParticipantFactory::get_instance()->delete_participant(participant_);
            participant_ = nullptr;
        }

        // Release discovery listener
        discovery_listener_.reset();

        LAP_COM_LOG_INFO << "DDS Binding shutdown complete";
        return Result<void>::FromValue();
    }

    // ========================================================================
    // Service Management
    // ========================================================================

    Result<void> DdsBinding::OfferService(
        uint64_t service_id,
        uint64_t instance_id
    ) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // For DDS Binding, OfferService just marks the service as available
        // Actual DataWriter creation happens lazily in SendEvent() when first event is sent
        // This avoids creating unnecessary topics/writers for services that never send events
        
        LAP_COM_LOG_INFO << "Service offered (DDS): service_id=0x" << service_id 
                         << ", instance_id=0x" << instance_id
                         << " (writers created on-demand in SendEvent)";

        return Result<void>::FromValue();
    }

    Result<void> DdsBinding::StopOfferService(
        uint64_t service_id,
        uint64_t instance_id
    ) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // DDS Binding uses lazy writer creation, so StopOfferService is a no-op
        // Actual cleanup happens when writers are deleted in Shutdown()
        
        LAP_COM_LOG_INFO << "Service stopped (DDS): service_id=0x" << service_id 
                         << ", instance_id=0x" << instance_id;

        return Result<void>::FromValue();
    }

    Result<std::vector<uint64_t>> DdsBinding::FindService(
        uint64_t service_id
    ) noexcept
    {
        if (!discovery_listener_) {
            LAP_COM_LOG_ERROR << "FindService called before Initialize";
            return Result<std::vector<uint64_t>>::FromError(MakeErrorCode(ComErrc::kNotInitialized));
        }

        // First try discovery listener (for remote entities)
        auto instances = discovery_listener_->GetDiscoveredInstances(service_id, participant_);
        
        // Also check local writers (for same-process or manually tracked entities)
        std::lock_guard<std::mutex> lock(mutex_);
        std::unordered_set<uint64_t> all_instances(instances.begin(), instances.end());
        
        // Parse topic names from local writers to find matching services
        for (const auto& [key, writer] : writers_) {
            // Key format: "service_id_instance_id_event_id"
            size_t first_underscore = key.find('_');
            if (first_underscore != std::string::npos) {
                try {
                    uint64_t sid = std::stoull(key.substr(0, first_underscore), nullptr, 16);
                    if (sid == service_id) {
                        size_t second_underscore = key.find('_', first_underscore + 1);
                        if (second_underscore != std::string::npos) {
                            std::string instance_str = key.substr(
                                first_underscore + 1,
                                second_underscore - first_underscore - 1
                            );
                            uint64_t instance_id = std::stoull(instance_str, nullptr, 16);
                            all_instances.insert(instance_id);
                        }
                    }
                } catch (const std::exception& e) {
                    // Skip malformed keys
                }
            }
        }
        
        std::vector<uint64_t> result(all_instances.begin(), all_instances.end());
        
        LAP_COM_LOG_DEBUG << "FindService(0x" << service_id << ") found " 
                          << result.size() << " instances (" 
                          << instances.size() << " remote, " 
                          << (result.size() - instances.size()) << " local)";
        
        return Result<std::vector<uint64_t>>::FromValue(std::move(result));
    }

    // ========================================================================
    // Event Communication
    // ========================================================================

    Result<void> DdsBinding::SendEvent(
        uint64_t service_id,
        uint64_t instance_id,
        uint32_t event_id,
        const ByteBuffer& data
    ) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto key = MakeKey(service_id, instance_id, event_id);
        auto writer_it = writers_.find(key);

        if (writer_it == writers_.end()) {
            // Get or create topic
            auto* topic = GetOrCreateTopic(service_id, instance_id, event_id);
            if (topic == nullptr) {
                return Result<void>::FromError(MakeErrorCode(ComErrc::kBindingConnectionError));
            }

            auto* writer = CreateWriter(topic);
            if (writer == nullptr) {
                return Result<void>::FromError(MakeErrorCode(ComErrc::kBindingConnectionError));
            }

            LAP_COM_LOG_DEBUG << "Created DataWriter for service=0x" << service_id 
                              << ", instance=0x" << instance_id 
                              << ", event=" << event_id;

            writers_[key] = writer;
            writer_it = writers_.find(key);
        }

        LAP_COM_LOG_DEBUG << "Sending event: service=0x" << service_id 
                          << ", instance=0x" << instance_id 
                          << ", event=" << event_id 
                          << ", size=" << data.size() << " bytes";

        // Check if writer has matched readers (required for RELIABLE QoS)
        if (config_.reliable) {
            PublicationMatchedStatus pub_status;
            writer_it->second->get_publication_matched_status(pub_status);
            
            if (pub_status.current_count == 0) {
                LAP_COM_LOG_ERROR << "No matched readers for RELIABLE writer (service=0x" << service_id 
                                  << ", instance=0x" << instance_id 
                                  << ", event=" << event_id << "). Write will likely fail!";
            }
        }

        // Create DDS sample
        lap::com::binding::LapComMessage msg;
        msg.service_id(service_id);
        msg.instance_id(instance_id);
        msg.event_id(event_id);
        msg.timestamp_ns(std::chrono::steady_clock::now().time_since_epoch().count());
        msg.payload(std::vector<uint8_t>(data.begin(), data.end()));

        // Write to DDS (需要传递 InstanceHandle，使用 c_InstanceHandle_Unknown 让 DDS 自动推断)
        char service_buf[32], instance_buf[32];
        snprintf(service_buf, sizeof(service_buf), "0x%llx", 
                 static_cast<unsigned long long>(service_id));
        snprintf(instance_buf, sizeof(instance_buf), "0x%llx", 
                 static_cast<unsigned long long>(instance_id));
        
        LAP_COM_LOG_INFO << "[DDS SEND] About to write event: service=" << service_buf
                          << ", instance=" << instance_buf
                          << ", event=" << event_id 
                          << ", payload_size=" << data.size();
        
        auto start = std::chrono::steady_clock::now();
        ReturnCode_t ret = writer_it->second->write(
            static_cast<void*>(&msg), 
            eprosima::fastrtps::rtps::c_InstanceHandle_Unknown
        );
        auto end = std::chrono::steady_clock::now();

        if (ret != ReturnCode_t::RETCODE_OK) {
            LAP_COM_LOG_ERROR << "DDS write failed with code " << ret();
            metrics_.messages_dropped++;
            return Result<void>::FromError(MakeErrorCode(ComErrc::kCommunicationLinkError));
        }

        // Update metrics
        metrics_.messages_sent++;
        metrics_.bytes_sent += data.size();
        
        auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        if (metrics_.messages_sent == 1) {
            metrics_.min_latency_ns = latency_ns;
            metrics_.max_latency_ns = latency_ns;
            metrics_.avg_latency_ns = static_cast<double>(latency_ns);
        } else {
            metrics_.min_latency_ns = std::min(metrics_.min_latency_ns, static_cast<uint64_t>(latency_ns));
            metrics_.max_latency_ns = std::max(metrics_.max_latency_ns, static_cast<uint64_t>(latency_ns));
            metrics_.avg_latency_ns = (metrics_.avg_latency_ns * (metrics_.messages_sent - 1) + latency_ns) 
                                     / metrics_.messages_sent;
        }

        LAP_COM_LOG_DEBUG << "Event sent via DDS: service=0x" << service_id 
                          << ", instance=0x" << instance_id 
                          << ", event=" << event_id 
                          << ", size=" << data.size() << " bytes";

        return Result<void>::FromValue();
    }

    Result<void> DdsBinding::SubscribeEvent(
        uint64_t service_id,
        uint64_t instance_id,
        uint32_t event_id,
        EventCallback callback
    ) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto key = MakeKey(service_id, instance_id, event_id);

        if (readers_.find(key) != readers_.end()) {
            LAP_COM_LOG_WARN << "Already subscribed: service=0x" << service_id 
                             << ", instance=0x" << instance_id 
                             << ", event=" << event_id;
            return Result<void>::FromValue();
        }

        // Get or create topic
        auto* topic = GetOrCreateTopic(service_id, instance_id, event_id);
        if (topic == nullptr) {
            return Result<void>::FromError(MakeErrorCode(ComErrc::kBindingConnectionError));
        }

        // Create reader with listener (pass correct key for listener storage)
        auto* reader = CreateReader(topic, key, callback);
        if (reader == nullptr) {
            return Result<void>::FromError(MakeErrorCode(ComErrc::kBindingConnectionError));
        }

        readers_[key] = reader;

        LAP_COM_LOG_INFO << "Subscribed to event: service=0x" << service_id 
                         << ", instance=0x" << instance_id 
                         << ", event=" << event_id;

        return Result<void>::FromValue();
    }

    Result<void> DdsBinding::UnsubscribeEvent(
        uint64_t service_id,
        uint64_t instance_id,
        uint32_t event_id
    ) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto key = MakeKey(service_id, instance_id, event_id);

        auto reader_it = readers_.find(key);
        if (reader_it == readers_.end()) {
            LAP_COM_LOG_WARN << "Not subscribed: service=0x" << service_id 
                             << ", instance=0x" << instance_id 
                             << ", event=" << event_id;
            return Result<void>::FromValue();
        }

        subscriber_->delete_datareader(reader_it->second);
        readers_.erase(reader_it);
        listeners_.erase(key);

        auto topic_it = topics_.find(key);
        if (topic_it != topics_.end()) {
            participant_->delete_topic(topic_it->second);
            topics_.erase(topic_it);
        }

        LAP_COM_LOG_INFO << "Unsubscribed from event: service=0x" << service_id
                         << ", instance=0x" << instance_id 
                         << ", event=" << event_id;

        return Result<void>::FromValue();
    }

    // ========================================================================
    // Method/Field Communication (Not Yet Implemented)
    // ========================================================================

    Result<ByteBuffer> DdsBinding::CallMethod(
        uint64_t service_id [[maybe_unused]],
        uint64_t instance_id [[maybe_unused]],
        uint32_t method_id [[maybe_unused]],
        const ByteBuffer& request [[maybe_unused]]
    ) noexcept
    {
        LAP_COM_LOG_ERROR << "CallMethod not yet implemented";
        return Result<ByteBuffer>::FromError(MakeErrorCode(ComErrc::kNotImplemented));
    }

    Result<void> DdsBinding::RegisterMethod(
        uint64_t service_id [[maybe_unused]],
        uint64_t instance_id [[maybe_unused]],
        uint32_t method_id [[maybe_unused]],
        MethodCallback handler [[maybe_unused]]
    ) noexcept
    {
        LAP_COM_LOG_ERROR << "RegisterMethod not yet implemented";
        return Result<void>::FromError(MakeErrorCode(ComErrc::kNotImplemented));
    }

    Result<ByteBuffer> DdsBinding::GetField(
        uint64_t service_id [[maybe_unused]],
        uint64_t instance_id [[maybe_unused]],
        uint32_t field_id [[maybe_unused]]
    ) noexcept
    {
        LAP_COM_LOG_ERROR << "GetField not yet implemented";
        return Result<ByteBuffer>::FromError(MakeErrorCode(ComErrc::kNotImplemented));
    }

    Result<void> DdsBinding::SetField(
        uint64_t service_id [[maybe_unused]],
        uint64_t instance_id [[maybe_unused]],
        uint32_t field_id [[maybe_unused]],
        const ByteBuffer& value [[maybe_unused]]
    ) noexcept
    {
        LAP_COM_LOG_ERROR << "SetField not yet implemented";
        return Result<void>::FromError(MakeErrorCode(ComErrc::kNotImplemented));
    }

    // ========================================================================
    // Capabilities and Diagnostics
    // ========================================================================

    bool DdsBinding::SupportsService(uint64_t service_id) const noexcept
    {
        // DDS supports all services (cross-ECU capable)
        (void)service_id;
        return true;
    }

    TransportMetrics DdsBinding::GetMetrics() const noexcept
    {
        return metrics_;
    }

    // ========================================================================
    // Internal Helper Methods
    // ========================================================================

    Topic* DdsBinding::GetOrCreateTopic(
        uint64_t service_id,
        uint64_t instance_id,
        uint32_t event_id
    ) noexcept
    {
        auto key = MakeKey(service_id, instance_id, event_id);
        
        // Check if topic already exists
        auto topic_it = topics_.find(key);
        if (topic_it != topics_.end()) {
            return topic_it->second;
        }

        // Create new topic
        auto* topic = CreateTopic(service_id, instance_id, event_id);
        if (topic != nullptr) {
            topics_[key] = topic;
        }
        
        return topic;
    }

    Topic* DdsBinding::CreateTopic(
        uint64_t service_id,
        uint64_t instance_id,
        uint32_t event_id
    ) noexcept
    {
        // Topic name: "lap/com/{service_id}/{instance_id}/{event_id}"
        std::ostringstream oss;
        oss << "lap/com/" << std::hex << service_id << "/" << instance_id << "/" << event_id;
        std::string topic_name = oss.str();

        Topic* topic = participant_->create_topic(
            topic_name,
            type_support_.get_type_name(),
            TOPIC_QOS_DEFAULT
        );
        
        if (topic == nullptr) {
            LAP_COM_LOG_ERROR << "Failed to create topic '" << topic_name << "'";
        } else {
            LAP_COM_LOG_DEBUG << "Created topic '" << topic_name << "' (type=" << type_support_.get_type_name() << ")";
        }

        return topic;
    }

    DataWriter* DdsBinding::CreateWriter(Topic* topic) noexcept
    {
        // Create QoS
        DataWriterQos wqos;
        
        if (config_.reliable) {
            wqos.reliability().kind = eprosima::fastdds::dds::RELIABLE_RELIABILITY_QOS;
        } else {
            wqos.reliability().kind = eprosima::fastdds::dds::BEST_EFFORT_RELIABILITY_QOS;
        }

        if (config_.transient_local) {
            wqos.durability().kind = eprosima::fastdds::dds::TRANSIENT_LOCAL_DURABILITY_QOS;
        } else {
            wqos.durability().kind = eprosima::fastdds::dds::VOLATILE_DURABILITY_QOS;
        }

        wqos.history().kind = eprosima::fastdds::dds::KEEP_LAST_HISTORY_QOS;
        wqos.history().depth = config_.history_depth;

        // Add resource limits for RELIABLE QoS
        wqos.resource_limits().max_samples = config_.history_depth;
        wqos.resource_limits().max_instances = 100;
        wqos.resource_limits().max_samples_per_instance = config_.history_depth;

        DataWriter* writer = publisher_->create_datawriter(topic, wqos);

        if (writer == nullptr) {
            LAP_COM_LOG_ERROR << "Failed to create DataWriter";
        }

        return writer;
    }

    DataReader* DdsBinding::CreateReader(
        Topic* topic,
        const std::string& key,
        EventCallback callback
    ) noexcept
    {
        // Create QoS
        DataReaderQos rqos;
        
        if (config_.reliable) {
            rqos.reliability().kind = eprosima::fastdds::dds::RELIABLE_RELIABILITY_QOS;
        } else {
            rqos.reliability().kind = eprosima::fastdds::dds::BEST_EFFORT_RELIABILITY_QOS;
        }

        if (config_.transient_local) {
            rqos.durability().kind = eprosima::fastdds::dds::TRANSIENT_LOCAL_DURABILITY_QOS;
        } else {
            rqos.durability().kind = eprosima::fastdds::dds::VOLATILE_DURABILITY_QOS;
        }

        rqos.history().kind = eprosima::fastdds::dds::KEEP_LAST_HISTORY_QOS;
        rqos.history().depth = config_.history_depth;

        // Add resource limits for RELIABLE QoS
        rqos.resource_limits().max_samples = config_.history_depth;
        rqos.resource_limits().max_instances = 100;
        rqos.resource_limits().max_samples_per_instance = config_.history_depth;

        // Create listener
        auto listener = std::make_unique<DdsReaderListener>(callback, metrics_);
        auto* listener_ptr = listener.get();

        LAP_COM_LOG_INFO << "Creating DataReader with key=" << key 
                          << ", QoS: reliable=" << config_.reliable 
                          << ", transient_local=" << config_.transient_local 
                          << ", history_depth=" << config_.history_depth;

        // Create reader with STATUS_MASK_ALL to enable all listener callbacks
        DataReader* reader = subscriber_->create_datareader(
            topic, 
            rqos, 
            listener_ptr,
            eprosima::fastdds::dds::StatusMask::all()
        );

        if (reader == nullptr) {
            LAP_COM_LOG_ERROR << "Failed to create DataReader for key=" << key;
        } else {
            // Store listener to keep it alive with correct key
            listeners_[key] = std::move(listener);
            LAP_COM_LOG_INFO << "DataReader created successfully, listener stored with key=" << key;
        }

        return reader;
    }

    Result<void> DdsBinding::InitializeAfXdp() noexcept
    {
        LAP_COM_LOG_INFO <<("AF_XDP initialization not yet implemented");
        return Result<void>::FromError(MakeErrorCode(ComErrc::kInvalidState));
    }

    Result<void> DdsBinding::SendViaAfXdp(const ByteBuffer& data) noexcept
    {
        LAP_COM_LOG_ERROR << "AF_XDP send not yet implemented (size=" << data.size() << " bytes)";
        return Result<void>::FromError(MakeErrorCode(ComErrc::kNotImplemented));
    }

    std::string DdsBinding::MakeKey(
        uint64_t service_id,
        uint64_t instance_id,
        uint32_t event_id
    ) const noexcept
    {
        std::ostringstream oss;
        oss << std::hex << service_id << "_" << instance_id << "_" << event_id;
        return oss.str();
    }

} // namespace binding
} // namespace com
} // namespace lap
