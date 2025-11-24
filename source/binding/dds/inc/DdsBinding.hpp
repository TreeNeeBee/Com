/**
 * @file        DdsBinding.hpp
 * @author      LightAP Development Team
 * @brief       DDS transport binding with AF_XDP acceleration
 * @date        2025-11-23
 * @details     Implements ITransportBinding using CycloneDDS for cross-ECU communication.
 *              Supports AF_XDP zero-copy for large payloads (>64KB).
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R24-11 Compliance:
 *              - TR_DDSS_00001-00007: DDS Security Integration
 *              - SWS_CM_00400: Transport Binding Interface
 * @reference   IMPLEMENTATION_PLAN_UPDATED.md Phase 4
 *              ARCHITECTURE_SUMMARY.md §8 DDS Transport Binding
 *              AUTOSAR_AP_TR_DDSSecurity.pdf
 * sdk:         CycloneDDS 0.10+
 * platform:    Linux 5.10+ (AF_XDP requires kernel 5.10+)
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/23  <td>1.0      <td>LightAP Team    <td>Initial DDS Binding implementation
 * </table>
 */
#ifndef LAP_COM_BINDING_DDS_BINDING_HPP
#define LAP_COM_BINDING_DDS_BINDING_HPP

#include "ITransportBinding.hpp"
#include "BindingTypes.hpp"

#include <lap/core/CResult.hpp>
#include <lap/log/CLogger.hpp>

// FastDDS C++ API
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipantListener.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/rtps/writer/WriterDiscoveryInfo.h>

// Generated IDL type
#include "LapComMessage.h"
#include "LapComMessagePubSubTypes.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

namespace lap
{
namespace com
{
namespace binding
{
    // Forward declaration
    class DdsBinding;

    /**
     * @brief DDS Binding configuration
     */
    struct DdsConfig
    {
        uint32_t domain_id = 0;                         ///< DDS domain ID (default: 0)
        std::string discovery_server;                   ///< Discovery server address (optional)
        bool use_shared_memory = true;                  ///< Enable DDS shared memory transport
        bool af_xdp_enabled = false;                    ///< Enable AF_XDP zero-copy for network
        std::string af_xdp_interface = "eth0";          ///< Network interface for AF_XDP
        std::vector<uint32_t> af_xdp_queues = {0, 1};   ///< AF_XDP queue IDs
        uint32_t large_payload_threshold = 65536;      ///< >64KB uses AF_XDP (bytes)
        uint32_t max_payload_size = 10485760;          ///< Max payload: 10MB

        // QoS defaults
        bool reliable = true;                           ///< RELIABLE vs BEST_EFFORT
        bool transient_local = false;                   ///< TRANSIENT_LOCAL durability
        uint32_t history_depth = 10;                    ///< KEEP_LAST depth
    };

    /**
     * @brief DataReader Listener for async event reception
     */
    class DdsReaderListener : public eprosima::fastdds::dds::DataReaderListener
    {
    public:
        DdsReaderListener(
            EventCallback callback,
            TransportMetrics& metrics
        ) : callback_(callback), metrics_(metrics) {}

        void on_data_available(eprosima::fastdds::dds::DataReader* reader) override;
        void on_subscription_matched(
            eprosima::fastdds::dds::DataReader* reader,
            const eprosima::fastdds::dds::SubscriptionMatchedStatus& info
        ) override;

    private:
        EventCallback callback_;
        TransportMetrics& metrics_;
    };

    /**
     * @brief Discovery Listener for tracking remote service instances
     * @details Listens to on_publisher_discovery to maintain a list of available
     *          service instances discovered on the network.
     *          Note: Also uses direct querying of builtin topics for robustness.
     */
    class DdsDiscoveryListener : public eprosima::fastdds::dds::DomainParticipantListener
    {
    public:
        DdsDiscoveryListener(DdsBinding* binding) : binding_(binding) {}

        void on_publisher_discovery(
            eprosima::fastdds::dds::DomainParticipant* participant,
            eprosima::fastrtps::rtps::WriterDiscoveryInfo&& info
        ) override;

        /**
         * @brief Get discovered instance IDs for a service
         * @param service_id Service identifier
         * @param participant DomainParticipant to query builtin topics
         * @return Vector of instance IDs
         */
        std::vector<uint64_t> GetDiscoveredInstances(
            uint64_t service_id,
            eprosima::fastdds::dds::DomainParticipant* participant
        ) const;

    private:
        DdsBinding* binding_;
        mutable std::mutex discovery_mutex_;
        // Map: service_id -> set of instance_ids
        std::unordered_map<uint64_t, std::unordered_set<uint64_t>> discovered_services_;
    };

    /**
     * @brief DDS Transport Binding
     * 
     * @details Implements cross-ECU communication using FastDDS.
     *          - Small payloads (<64KB): DDS shared memory
     *          - Large payloads (>64KB): AF_XDP zero-copy (optional)
     *          - QoS policies: Reliable, Transient Local
     * 
     * @note Priority: 80 (lower than iceoryx2 100, higher than legacy 10)
     */
    class DdsBinding : public ITransportBinding
    {
    public:
        DdsBinding();
        ~DdsBinding() override;

        // Disable copy/move
        DdsBinding(const DdsBinding&) = delete;
        DdsBinding& operator=(const DdsBinding&) = delete;
        DdsBinding(DdsBinding&&) = delete;
        DdsBinding& operator=(DdsBinding&&) = delete;

        // ====================================================================
        // ITransportBinding Interface Implementation
        // ====================================================================

        Result<void> Initialize() noexcept override;
        Result<void> Shutdown() noexcept override;

        Result<void> OfferService(uint64_t service_id, uint64_t instance_id) noexcept override;
        Result<void> StopOfferService(uint64_t service_id, uint64_t instance_id) noexcept override;
        Result<std::vector<uint64_t>> FindService(uint64_t service_id) noexcept override;

        Result<void> SendEvent(
            uint64_t service_id,
            uint64_t instance_id,
            uint32_t event_id,
            const ByteBuffer& data
        ) noexcept override;

        Result<void> SubscribeEvent(
            uint64_t service_id,
            uint64_t instance_id,
            uint32_t event_id,
            EventCallback callback
        ) noexcept override;

        Result<void> UnsubscribeEvent(
            uint64_t service_id,
            uint64_t instance_id,
            uint32_t event_id
        ) noexcept override;

        Result<ByteBuffer> CallMethod(
            uint64_t service_id,
            uint64_t instance_id,
            uint32_t method_id,
            const ByteBuffer& request
        ) noexcept override;

        Result<void> RegisterMethod(
            uint64_t service_id,
            uint64_t instance_id,
            uint32_t method_id,
            MethodCallback handler
        ) noexcept override;

        Result<ByteBuffer> GetField(
            uint64_t service_id,
            uint64_t instance_id,
            uint32_t field_id
        ) noexcept override;

        Result<void> SetField(
            uint64_t service_id,
            uint64_t instance_id,
            uint32_t field_id,
            const ByteBuffer& value
        ) noexcept override;

        // Diagnostics and Capabilities
        const char* GetName() const noexcept override { return "DDS"; }
        uint32_t GetVersion() const noexcept override { return 0x00010000; }
        uint32_t GetPriority() const noexcept override { return 80; }
        bool SupportsZeroCopy() const noexcept override { return config_.af_xdp_enabled; }
        bool SupportsService(uint64_t service_id) const noexcept override;
        TransportMetrics GetMetrics() const noexcept override;

    private:
        // Helper methods
        eprosima::fastdds::dds::Topic* GetOrCreateTopic(
            uint64_t service_id,
            uint64_t instance_id,
            uint32_t event_id
        ) noexcept;
        
        eprosima::fastdds::dds::Topic* CreateTopic(
            uint64_t service_id,
            uint64_t instance_id,
            uint32_t event_id
        ) noexcept;
        
        eprosima::fastdds::dds::DataWriter* CreateWriter(
            eprosima::fastdds::dds::Topic* topic
        ) noexcept;
        
        eprosima::fastdds::dds::DataReader* CreateReader(
            eprosima::fastdds::dds::Topic* topic,
            const std::string& key,
            EventCallback callback
        ) noexcept;
        
        Result<void> InitializeAfXdp() noexcept;
        Result<void> SendViaAfXdp(const ByteBuffer& data) noexcept;
        std::string MakeKey(uint64_t service_id, uint64_t instance_id, uint32_t event_id) const noexcept;

        // Member variables
        DdsConfig config_;

        eprosima::fastdds::dds::DomainParticipant* participant_ = nullptr;
        eprosima::fastdds::dds::Publisher* publisher_ = nullptr;
        eprosima::fastdds::dds::Subscriber* subscriber_ = nullptr;
        eprosima::fastdds::dds::TypeSupport type_support_;

        std::mutex mutex_;
        std::unordered_map<std::string, eprosima::fastdds::dds::Topic*> topics_;
        std::unordered_map<std::string, eprosima::fastdds::dds::DataWriter*> writers_;
        std::unordered_map<std::string, eprosima::fastdds::dds::DataReader*> readers_;
        std::unordered_map<std::string, std::unique_ptr<DdsReaderListener>> listeners_;
        std::unique_ptr<DdsDiscoveryListener> discovery_listener_;

        mutable TransportMetrics metrics_;
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_BINDING_DDS_BINDING_HPP
