/**
 * @file        CDdsServiceManager.hpp
 * @author      LightAP Development Team
 * @brief       DDS binding — Service lifecycle management
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Manages the service lifecycle for the DDS binding:
 *              - OfferService / StopOfferService / FindService
 *              - Creates "presence" DataWriters so discovery can detect services
 *
 *              NOT thread-safe — the facade (DdsBinding) serialises access.
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Extracted from monolithic DdsBinding
 * </table>
 */

#ifndef LAP_COM_DDS_CDDSSVCMGR_HPP
#define LAP_COM_DDS_CDDSSVCMGR_HPP

// ==================== Project-Internal Headers ====================
#include "DdsTypes.hpp"
#include "CCdrChannel.hpp"
#include "ITransportBinding.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/CResult.hpp>

// ==================== Third-Party Headers ====================
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>

// ==================== Forward Declarations ====================
namespace lap { namespace com { namespace binding { class DdsDiscoveryListener; } } }

namespace lap
{
namespace com
{
namespace binding
{

    using lap::core::Result;

    // ====================================================================
    // CDdsServiceManager
    // ====================================================================

    /**
     * @brief   Service lifecycle manager for the DDS binding
     *
     * @details Manages "presence" DataWriters (one per offered service).
     *          FindService combines DDS discovery listener cache with
     *          locally-offered service lookup.
     *
     * @note    NOT thread-safe — caller (facade) must serialise access
     */
    class CDdsServiceManager
    {
    public:
        /**
         * @brief   Construct with references to shared DDS resources
         */
        CDdsServiceManager(
            const DdsConfig& config,
            eprosima::fastdds::dds::DomainParticipant*& pParticipant,
            eprosima::fastdds::dds::Publisher*& pPublisher,
            eprosima::fastdds::dds::Subscriber*& pSubscriber,
            const eprosima::fastdds::dds::TypeSupport& typeSupport,
            DdsDiscoveryListener*& pDiscoveryListener,
            TransportMetrics& metrics ) noexcept;

        ~CDdsServiceManager() noexcept = default;

        // Rule of Five — non-copyable, non-movable
        CDdsServiceManager( const CDdsServiceManager& )             = delete;
        CDdsServiceManager& operator=( const CDdsServiceManager& )  = delete;
        CDdsServiceManager( CDdsServiceManager&& )                  = delete;
        CDdsServiceManager& operator=( CDdsServiceManager&& )       = delete;

    public:
        // ================================================================
        // Service Lifecycle
        // ================================================================

        Result< void > OfferService( UInt64 serviceId,
                                      UInt64 instanceId ) noexcept;

        Result< void > StopOfferService( UInt64 serviceId,
                                          UInt64 instanceId ) noexcept;

        Result< Vector< UInt64 > > FindService( UInt64 serviceId ) noexcept;

        /**
         * @brief   Close all service presence channels
         */
        void Shutdown() noexcept;

    private:
        // ================================================================
        // References to Shared Resources (facade-owned)
        // ================================================================

        const DdsConfig&                                    m_config;
        eprosima::fastdds::dds::DomainParticipant*&         m_pParticipant;
        eprosima::fastdds::dds::Publisher*&                 m_pPublisher;
        eprosima::fastdds::dds::Subscriber*&                m_pSubscriber;
        const eprosima::fastdds::dds::TypeSupport&          m_typeSupport;
        DdsDiscoveryListener*&                              m_pDiscoveryListener;
        TransportMetrics&                                   m_metrics;

        // ================================================================
        // Owned Channels: one CCdrChannel per presence (writer-only)
        // ================================================================

        Map< String, CCdrChannel >  m_mapChannels;
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_DDS_CDDSSVCMGR_HPP
