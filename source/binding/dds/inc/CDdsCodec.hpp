/**
 * @file        CDdsCodec.hpp
 * @author      LightAP Development Team
 * @brief       DDS binding — Topic name / key generation and DDS entity factory
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Pure-static utility class providing:
 *              - Topic name generation (event, method-request, method-response)
 *              - Composite key generation for map lookups
 *              - DataWriter / DataReader QoS-aware factory helpers
 *              - FastDDS participant QoS configuration helpers
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Extracted from monolithic DdsBinding
 * </table>
 */

#ifndef LAP_COM_DDS_CDDCODEC_HPP
#define LAP_COM_DDS_CDDCODEC_HPP

// ==================== Project-Internal Headers ====================
#include "DdsTypes.hpp"
#include "ITransportBinding.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/CResult.hpp>

// ==================== Third-Party Headers ====================
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>

namespace lap
{
namespace com
{
namespace binding
{

    using lap::core::Result;

    // ====================================================================
    // CDdsCodec
    // ====================================================================

    /**
     * @brief   Pure-static utility class for DDS topic naming and key generation
     *
     * @details Provides all stateless helpers needed by the DDS managers:
     *          topic name generation, key computation, DDS entity factory.
     *          Not instantiable — all methods are static.
     *
     * @note    Thread-safe (all methods are stateless)
     */
    class CDdsCodec
    {
    public:
        CDdsCodec()                                 = delete;
        CDdsCodec( const CDdsCodec& )               = delete;
        CDdsCodec& operator=( const CDdsCodec& )    = delete;

    public:
        // ================================================================
        // Topic Name Generation
        // ================================================================

        /**
         * @brief   Generate event topic name
         * @details Format: "lap/com/{serviceId}/{instanceId}/{eventId}"
         */
        static String MakeEventTopicName( UInt64 serviceId,
                                           UInt64 instanceId,
                                           UInt32 eventId ) noexcept;

        /**
         * @brief   Generate method request/reply topic name
         * @details Format: "LapComMethod_{serviceId}_{instanceId}_{methodId}_{req|rep}"
         */
        static String MakeMethodTopicName( UInt64 serviceId,
                                            UInt64 instanceId,
                                            UInt32 methodId,
                                            Bool bIsRequest ) noexcept;

    public:
        // ================================================================
        // Key Generation
        // ================================================================

        /**
         * @brief   Generate composite key for event topic/writer/reader maps
         * @details Format: "{service_id_hex}_{instance_id_hex}_{event_id_hex}"
         */
        static String MakeEventKey( UInt64 serviceId,
                                     UInt64 instanceId,
                                     UInt32 eventId ) noexcept;

        /**
         * @brief   Generate composite key for method handler maps
         */
        static String MakeMethodKey( UInt64 serviceId,
                                      UInt64 instanceId,
                                      UInt32 methodId ) noexcept;

    public:
        // ================================================================
        // DDS Entity Factory
        // ================================================================

        /**
         * @brief   Create a DataWriter with configured QoS
         */
        static eprosima::fastdds::dds::DataWriter* CreateWriter(
            eprosima::fastdds::dds::Publisher* pPublisher,
            eprosima::fastdds::dds::Topic* pTopic,
            const DdsConfig& config ) noexcept;

        /**
         * @brief   Create a DataReader with configured QoS and optional listener
         */
        static eprosima::fastdds::dds::DataReader* CreateReader(
            eprosima::fastdds::dds::Subscriber* pSubscriber,
            eprosima::fastdds::dds::Topic* pTopic,
            const DdsConfig& config,
            eprosima::fastdds::dds::DataReaderListener* pListener = nullptr,
            eprosima::fastdds::dds::StatusMask mask =
                eprosima::fastdds::dds::StatusMask::all() ) noexcept;

    public:
        // ================================================================
        // FastDDS QoS Helpers
        // ================================================================

        /**
         * @brief   Apply DdsConfig QoS to a DataWriterQos
         * @details Sets deadline, lifespan, and resource limits from config
         */
        static void ApplyWriterQos(
            eprosima::fastdds::dds::DataWriterQos& wqos,
            const DdsConfig& config ) noexcept;

        /**
         * @brief   Apply DdsConfig QoS to a DataReaderQos
         * @details Sets deadline, lifespan, and resource limits from config
         */
        static void ApplyReaderQos(
            eprosima::fastdds::dds::DataReaderQos& rqos,
            const DdsConfig& config ) noexcept;
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_DDS_CDDCODEC_HPP
