/**
 * @file        DdsBinding.cpp
 * @author      LightAP Development Team
 * @brief       DDS transport binding implementation with FastDDS
 * @date        2025-11-23
 */

#include "DdsBinding.hpp"
#include "LapComMessage.hpp"
#include "LapComMessagePubSubTypes.hpp"
#include "ComTypes.hpp"

#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/dds/publisher/qos/PublisherQos.hpp>
#include <fastdds/dds/publisher/qos/DataWriterQos.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/core/status/PublicationMatchedStatus.hpp>
#include <fastdds/dds/core/status/SubscriptionMatchedStatus.hpp>
#include <fastdds/dds/core/detail/DDSReturnCode.hpp>
#include <fastdds/dds/subscriber/qos/SubscriberQos.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/rtps/common/InstanceHandle.hpp>
#include <fastdds/rtps/common/Locator.hpp>
#include <fastdds/utils/IPLocator.hpp>
#include <fastdds/rtps/transport/TCPv4TransportDescriptor.hpp>
#include <fastdds/rtps/transport/shared_mem/SharedMemTransportDescriptor.hpp>

#include <chrono>
#include <thread>
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

        if (reader->take_next_sample(&msg, &info) == RETCODE_OK)
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

    void DdsDiscoveryListener::on_data_writer_discovery(
        DomainParticipant* participant [[maybe_unused]],
        eprosima::fastdds::rtps::WriterDiscoveryStatus reason,
        const eprosima::fastdds::rtps::PublicationBuiltinTopicData& info,
        bool& should_be_ignored)
    {
        should_be_ignored = false;

        // Parse topic name to extract service_id and instance_id
        // Topic name format: "lap/com/{service_id}/{instance_id}/{event_id}"
        const std::string topic_name = info.topic_name.to_string();
        const std::string prefix = "lap/com/";

        if (topic_name.rfind(prefix, 0) != 0) {
            return;
        }

        std::vector<std::string> parts;
        std::stringstream ss(topic_name.substr(prefix.size()));
        std::string token;
        while (std::getline(ss, token, '/')) {
            parts.push_back(token);
        }

        if (parts.size() < 3) {
            return;
        }

        try {
            const uint64_t service_id = std::stoull(parts[0], nullptr, 16);
            const uint64_t instance_id = std::stoull(parts[1], nullptr, 16);

            std::lock_guard<std::mutex> lock(discovery_mutex_);

            if (reason == eprosima::fastdds::rtps::WriterDiscoveryStatus::DISCOVERED_WRITER) {
                discovered_services_[service_id].insert(instance_id);

                std::ostringstream oss;
                oss << "Discovered DDS publisher: service=0x"
                    << std::hex << std::setw(4) << std::setfill('0') << service_id
                    << ", instance=0x" << std::setw(4) << std::setfill('0') << instance_id
                    << " (topic: " << topic_name << ")";
                LAP_COM_LOG_INFO << oss.str();
            } else if (reason == eprosima::fastdds::rtps::WriterDiscoveryStatus::REMOVED_WRITER) {
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
        catch (const std::exception& e) {
            LAP_COM_LOG_WARN << "Failed to parse topic name '" << topic_name
                             << "': " << e.what();
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

    namespace
    {
        class DdsMethodReaderListener : public DataReaderListener
        {
        public:
            DdsMethodReaderListener(
                MethodCallback handler,
                DataWriter* response_writer,
                TransportMetrics& metrics
            )
                : handler_(std::move(handler))
                , response_writer_(response_writer)
                , metrics_(metrics)
            {
            }

            void on_data_available(DataReader* reader) override
            {
                lap::com::binding::LapComMessage msg;
                SampleInfo info;

                while (reader->take_next_sample(&msg, &info) == RETCODE_OK) {
                    if (!info.valid_data) {
                        continue;
                    }

                    ByteBuffer request(msg.payload().begin(), msg.payload().end());
                    ByteBuffer response;
                    int32_t status = 0;

                    if (handler_) {
                        try {
                            response = handler_(msg.service_id(), msg.instance_id(),
                                                 msg.event_id(), request);
                        } catch (const std::exception& e) {
                            status = static_cast<int32_t>(ComErrc::kInternal);
                            LAP_COM_LOG_WARN << "DDS Method handler exception: " << e.what();
                        }
                    } else {
                        status = static_cast<int32_t>(ComErrc::kServiceNotAvailable);
                    }

                    lap::com::binding::LapComMessage resp_msg;
                    resp_msg.service_id(msg.service_id());
                    resp_msg.instance_id(msg.instance_id());
                    resp_msg.event_id(msg.event_id());
                    resp_msg.timestamp_ns(msg.timestamp_ns());
                    // Encode status at front of payload (4 bytes)
                    ByteBuffer payload;
                    payload.reserve(4 + response.size());
                    payload.push_back(static_cast<uint8_t>(status & 0xFF));
                    payload.push_back(static_cast<uint8_t>((status >> 8) & 0xFF));
                    payload.push_back(static_cast<uint8_t>((status >> 16) & 0xFF));
                    payload.push_back(static_cast<uint8_t>((status >> 24) & 0xFF));
                    payload.insert(payload.end(), response.begin(), response.end());
                    resp_msg.payload(std::move(payload));

                    if (response_writer_ != nullptr) {
                        std::ostringstream log_oss;
                        log_oss << "DDS method request received: service=0x" << std::hex
                            << msg.service_id() << ", instance=0x" << msg.instance_id()
                            << ", method=" << std::dec << msg.event_id()
                            << ", request_size=" << request.size();
                        LAP_COM_LOG_DEBUG << log_oss.str();
                        ReturnCode_t write_ret = response_writer_->write(&resp_msg);
                        if (write_ret != RETCODE_OK) {
                            LAP_COM_LOG_WARN << "DDS method response write failed with code " << write_ret;
                        } else {
                            metrics_.messages_sent++;
                            metrics_.bytes_sent += resp_msg.payload().size();
                        }
                    }
                }
            }

        private:
            MethodCallback handler_;
            DataWriter* response_writer_;
            TransportMetrics& metrics_;
        };
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

        auto configure_simple_edp = [&pqos]() {
            pqos.wire_protocol().builtin.discovery_config.discoveryProtocol =
                eprosima::fastdds::rtps::DiscoveryProtocol::SIMPLE;
            pqos.wire_protocol().builtin.discovery_config.use_SIMPLE_EndpointDiscoveryProtocol = true;
            pqos.wire_protocol().builtin.discovery_config.use_STATIC_EndpointDiscoveryProtocol = false;
            pqos.wire_protocol().builtin.discovery_config.m_DiscoveryServers.clear();
        };

        bool use_tcp_transport = false;
        auto configure_discovery_server = [&pqos, this, &use_tcp_transport]() -> bool {
            if (config_.discovery_server.empty()) {
                return false;
            }

            std::string host = config_.discovery_server;
            uint32_t port = pqos.wire_protocol().port.getUnicastPort(config_.domain_id, 0);
            int32_t locator_kind = LOCATOR_KIND_UDPv4;

            const std::string tcp_prefix = "tcp://";
            const std::string udp_prefix = "udp://";
            if (host.rfind(tcp_prefix, 0) == 0) {
                host.erase(0, tcp_prefix.size());
                locator_kind = LOCATOR_KIND_TCPv4;
                port = 42100;
                use_tcp_transport = true;
            } else if (host.rfind(udp_prefix, 0) == 0) {
                host.erase(0, udp_prefix.size());
                locator_kind = LOCATOR_KIND_UDPv4;
            }

            const auto pos = host.find(':');
            if (pos != std::string::npos) {
                const std::string port_str = host.substr(pos + 1);
                host = host.substr(0, pos);
                try {
                    port = static_cast<uint32_t>(std::stoul(port_str));
                } catch (const std::exception&) {
                    LAP_COM_LOG_WARN << "Invalid discovery server port, fallback to Simple EDP";
                    return false;
                }
            }

            eprosima::fastdds::rtps::Locator_t server_locator;
            server_locator.kind = locator_kind;
            server_locator.port = port;
            if (!eprosima::fastdds::rtps::IPLocator::setIPv4(server_locator, host)) {
                LAP_COM_LOG_WARN << "Invalid discovery server address, fallback to Simple EDP";
                return false;
            }

            pqos.wire_protocol().builtin.discovery_config.discoveryProtocol =
                eprosima::fastdds::rtps::DiscoveryProtocol::CLIENT;
            pqos.wire_protocol().builtin.discovery_config.use_SIMPLE_EndpointDiscoveryProtocol = true;
            pqos.wire_protocol().builtin.discovery_config.use_STATIC_EndpointDiscoveryProtocol = false;
            pqos.wire_protocol().builtin.discovery_config.m_DiscoveryServers.clear();
            pqos.wire_protocol().builtin.discovery_config.m_DiscoveryServers.push_back(server_locator);
            return true;
        };

        const bool use_ds = configure_discovery_server();
        if (!use_ds) {
            configure_simple_edp();
        }

        if (use_tcp_transport) {
            pqos.transport().use_builtin_transports = false;
            auto tcp_transport = std::make_shared<eprosima::fastdds::rtps::TCPv4TransportDescriptor>();
            tcp_transport->add_listener_port(0);
            tcp_transport->interfaceWhiteList.emplace_back("127.0.0.1");
            pqos.transport().user_transports.push_back(tcp_transport);
        }

        if (config_.use_shared_memory) {
            if (!use_tcp_transport) {
                pqos.transport().use_builtin_transports = true;
            }
            auto shm_transport = std::make_shared<eprosima::fastdds::rtps::SharedMemTransportDescriptor>();
            pqos.transport().user_transports.push_back(shm_transport);
        }

        participant_ = DomainParticipantFactory::get_instance()->create_participant(
            config_.domain_id,
            pqos,
            discovery_listener_.get()  // Register listener for discovery callbacks
        );

        if (participant_ == nullptr && use_ds) {
            LAP_COM_LOG_WARN << "Discovery server connection failed, fallback to Simple EDP";
            configure_simple_edp();
            participant_ = DomainParticipantFactory::get_instance()->create_participant(
                config_.domain_id,
                pqos,
                discovery_listener_.get()
            );
        }

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

        // Delete all method readers
        for (auto& [topic, reader] : method_readers_) {
            if (reader != nullptr) {
                subscriber_->delete_datareader(reader);
            }
        }
        method_readers_.clear();
        method_listeners_.clear();

        // Delete all method writers
        for (auto& [topic, writer] : method_writers_) {
            if (writer != nullptr) {
                publisher_->delete_datawriter(writer);
            }
        }
        method_writers_.clear();

        // Delete all method topics
        for (auto& [topic_name, topic] : method_topics_) {
            if (topic != nullptr) {
                participant_->delete_topic(topic);
            }
        }
        method_topics_.clear();
        method_handlers_.clear();
        method_mutexes_.clear();

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
            eprosima::fastdds::rtps::c_InstanceHandle_Unknown
        );
        auto end = std::chrono::steady_clock::now();

        if (ret != RETCODE_OK) {
            LAP_COM_LOG_ERROR << "DDS write failed with code " << ret;
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
        std::lock_guard<std::mutex> lock(mutex_);

        if (participant_ == nullptr || publisher_ == nullptr || subscriber_ == nullptr) {
            return Result<ByteBuffer>::FromError(MakeErrorCode(ComErrc::kNotInitialized));
        }

        const auto key = MakeMethodKey(service_id, instance_id, method_id);
        auto& call_mutex_ptr = method_mutexes_[key];
        if (!call_mutex_ptr) {
            call_mutex_ptr = std::make_unique<std::mutex>();
        }

        std::unique_lock<std::mutex> call_lock(*call_mutex_ptr);

        const std::string req_topic = MakeMethodTopicName(service_id, instance_id, method_id, true);
        const std::string rep_topic = MakeMethodTopicName(service_id, instance_id, method_id, false);

        DataWriter* req_writer = nullptr;
        DataReader* rep_reader = nullptr;

        auto writer_it = method_writers_.find(req_topic);
        if (writer_it == method_writers_.end()) {
            auto* topic = participant_->create_topic(req_topic, type_support_.get_type_name(), TOPIC_QOS_DEFAULT);
            if (topic == nullptr) {
                return Result<ByteBuffer>::FromError(MakeErrorCode(ComErrc::kBindingConnectionError));
            }
            req_writer = CreateWriter(topic);
            if (req_writer == nullptr) {
                return Result<ByteBuffer>::FromError(MakeErrorCode(ComErrc::kBindingConnectionError));
            }
            method_topics_[req_topic] = topic;
            method_writers_[req_topic] = req_writer;
        } else {
            req_writer = writer_it->second;
        }

        auto reader_it = method_readers_.find(rep_topic);
        if (reader_it == method_readers_.end()) {
            auto* topic = participant_->create_topic(rep_topic, type_support_.get_type_name(), TOPIC_QOS_DEFAULT);
            if (topic == nullptr) {
                return Result<ByteBuffer>::FromError(MakeErrorCode(ComErrc::kBindingConnectionError));
            }

            DataReaderQos rqos;
            if (config_.reliable) {
                rqos.reliability().kind = RELIABLE_RELIABILITY_QOS;
            } else {
                rqos.reliability().kind = BEST_EFFORT_RELIABILITY_QOS;
            }
            if (config_.transient_local) {
                rqos.durability().kind = TRANSIENT_LOCAL_DURABILITY_QOS;
            } else {
                rqos.durability().kind = VOLATILE_DURABILITY_QOS;
            }
            rqos.history().kind = KEEP_LAST_HISTORY_QOS;
            rqos.history().depth = config_.history_depth;

            rep_reader = subscriber_->create_datareader(topic, rqos);
            if (rep_reader == nullptr) {
                return Result<ByteBuffer>::FromError(MakeErrorCode(ComErrc::kBindingConnectionError));
            }
            method_topics_[rep_topic] = topic;
            method_readers_[rep_topic] = rep_reader;
        } else {
            rep_reader = reader_it->second;
        }

        if (req_writer == nullptr || rep_reader == nullptr) {
            return Result<ByteBuffer>::FromError(MakeErrorCode(ComErrc::kBindingConnectionError));
        }

        // Wait briefly for request/response matching
        {
            const auto match_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            PublicationMatchedStatus pub_status;
            SubscriptionMatchedStatus sub_status;
            while (std::chrono::steady_clock::now() < match_deadline) {
                req_writer->get_publication_matched_status(pub_status);
                rep_reader->get_subscription_matched_status(sub_status);
                if (pub_status.current_count > 0 && sub_status.current_count > 0) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        LapComMessage request_msg;
        request_msg.service_id(service_id);
        request_msg.instance_id(instance_id);
        request_msg.event_id(method_id);
        const auto token = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        request_msg.timestamp_ns(token);
        request_msg.payload(std::vector<uint8_t>(request.begin(), request.end()));

        if (req_writer->write(&request_msg) != RETCODE_OK) {
            return Result<ByteBuffer>::FromError(MakeErrorCode(ComErrc::kCommunicationLinkError));
        }

        metrics_.messages_sent++;
        metrics_.bytes_sent += request.size();

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
        LapComMessage response_msg;
        SampleInfo info;

        while (std::chrono::steady_clock::now() < deadline) {
            auto ret = rep_reader->take_next_sample(&response_msg, &info);
            if (ret != RETCODE_OK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            if (!info.valid_data) {
                continue;
            }

            if (response_msg.service_id() != service_id ||
                response_msg.instance_id() != instance_id ||
                response_msg.event_id() != method_id ||
                response_msg.timestamp_ns() != token) {
                continue;
            }

            ByteBuffer payload(response_msg.payload().begin(), response_msg.payload().end());
            if (payload.size() < 4) {
                return Result<ByteBuffer>::FromError(MakeErrorCode(ComErrc::kCommunicationLinkError));
            }

            int32_t status = static_cast<int32_t>(payload[0])
                           | (static_cast<int32_t>(payload[1]) << 8)
                           | (static_cast<int32_t>(payload[2]) << 16)
                           | (static_cast<int32_t>(payload[3]) << 24);
            if (status != 0) {
                return Result<ByteBuffer>::FromError(MakeErrorCode(static_cast<ComErrc>(status)));
            }
            payload.erase(payload.begin(), payload.begin() + 4);

            metrics_.messages_received++;
            metrics_.bytes_received += payload.size();
            return Result<ByteBuffer>::FromValue(std::move(payload));
        }

        return Result<ByteBuffer>::FromError(MakeErrorCode(ComErrc::kTimeout));
    }

    Result<void> DdsBinding::RegisterMethod(
        uint64_t service_id [[maybe_unused]],
        uint64_t instance_id [[maybe_unused]],
        uint32_t method_id [[maybe_unused]],
        MethodCallback handler [[maybe_unused]]
    ) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (participant_ == nullptr || publisher_ == nullptr || subscriber_ == nullptr) {
            return Result<void>::FromError(MakeErrorCode(ComErrc::kNotInitialized));
        }

        const auto key = MakeMethodKey(service_id, instance_id, method_id);
        method_handlers_[key] = handler;

        const std::string req_topic = MakeMethodTopicName(service_id, instance_id, method_id, true);
        const std::string rep_topic = MakeMethodTopicName(service_id, instance_id, method_id, false);

        DataWriter* resp_writer = nullptr;
        auto writer_it = method_writers_.find(rep_topic);
        if (writer_it == method_writers_.end()) {
            auto* topic = participant_->create_topic(rep_topic, type_support_.get_type_name(), TOPIC_QOS_DEFAULT);
            if (topic == nullptr) {
                return Result<void>::FromError(MakeErrorCode(ComErrc::kBindingConnectionError));
            }
            resp_writer = CreateWriter(topic);
            if (resp_writer == nullptr) {
                return Result<void>::FromError(MakeErrorCode(ComErrc::kBindingConnectionError));
            }
            method_topics_[rep_topic] = topic;
            method_writers_[rep_topic] = resp_writer;
        } else {
            resp_writer = writer_it->second;
        }

        DataReader* req_reader = nullptr;
        auto reader_it = method_readers_.find(req_topic);
        if (reader_it == method_readers_.end()) {
            auto* topic = participant_->create_topic(req_topic, type_support_.get_type_name(), TOPIC_QOS_DEFAULT);
            if (topic == nullptr) {
                return Result<void>::FromError(MakeErrorCode(ComErrc::kBindingConnectionError));
            }

            DataReaderQos rqos;
            if (config_.reliable) {
                rqos.reliability().kind = RELIABLE_RELIABILITY_QOS;
            } else {
                rqos.reliability().kind = BEST_EFFORT_RELIABILITY_QOS;
            }
            if (config_.transient_local) {
                rqos.durability().kind = TRANSIENT_LOCAL_DURABILITY_QOS;
            } else {
                rqos.durability().kind = VOLATILE_DURABILITY_QOS;
            }
            rqos.history().kind = KEEP_LAST_HISTORY_QOS;
            rqos.history().depth = config_.history_depth;

            auto listener = std::make_unique<DdsMethodReaderListener>(handler, resp_writer, metrics_);
            req_reader = subscriber_->create_datareader(
                topic,
                rqos,
                listener.get(),
                eprosima::fastdds::dds::StatusMask::data_available()
            );
            if (req_reader == nullptr) {
                return Result<void>::FromError(MakeErrorCode(ComErrc::kBindingConnectionError));
            }

            method_topics_[req_topic] = topic;
            method_readers_[req_topic] = req_reader;
            method_listeners_[req_topic] = std::move(listener);
        } else {
            req_reader = reader_it->second;
        }

        (void)req_reader;
        return Result<void>::FromValue();
    }

    Result<ByteBuffer> DdsBinding::GetField(
        uint64_t service_id [[maybe_unused]],
        uint64_t instance_id [[maybe_unused]],
        uint32_t field_id [[maybe_unused]]
    ) noexcept
    {
        const uint32_t getter_method_id = field_id | 0x10000U;
        return CallMethod(service_id, instance_id, getter_method_id, ByteBuffer{});
    }

    Result<void> DdsBinding::SetField(
        uint64_t service_id [[maybe_unused]],
        uint64_t instance_id [[maybe_unused]],
        uint32_t field_id [[maybe_unused]],
        const ByteBuffer& value [[maybe_unused]]
    ) noexcept
    {
        const uint32_t setter_method_id = field_id | 0x20000U;
        auto result = CallMethod(service_id, instance_id, setter_method_id, value);
        if (!result) {
            return Result<void>::FromError(result.Error());
        }
        return Result<void>::FromValue();
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

    void DdsBinding::SetDiscoveryServer(const std::string& address) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (participant_ != nullptr) {
            LAP_COM_LOG_WARN << "SetDiscoveryServer ignored after Initialize";
            return;
        }
        config_.discovery_server = address;
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

    std::string DdsBinding::MakeMethodKey(
        uint64_t service_id,
        uint64_t instance_id,
        uint32_t method_id
    ) const noexcept
    {
        std::ostringstream oss;
        oss << std::hex << service_id << "_" << instance_id << "_" << method_id;
        return oss.str();
    }

    std::string DdsBinding::MakeMethodTopicName(
        uint64_t service_id,
        uint64_t instance_id,
        uint32_t method_id,
        bool is_request
    ) const noexcept
    {
        std::ostringstream oss;
        oss << "LapComMethod_" << std::hex << service_id << "_" << instance_id
            << "_" << method_id << (is_request ? "_req" : "_rep");
        return oss.str();
    }

} // namespace binding
} // namespace com
} // namespace lap
