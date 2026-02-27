/**
 * @file        CDdsEventManager.hpp
 * @author      LightAP Development Team
 * @brief       DDS binding — Event communication management
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Manages event communication for the DDS binding:
 *              - SendEvent / SubscribeEvent / UnsubscribeEvent
 *              - Lazily creates DataWriters for send, DataReaders for subscribe
 *              - Manages DdsReaderListener lifetime
 *
 *              NOT thread-safe — the facade (DdsBinding) serialises access.
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Extracted from monolithic DdsBinding
 * </table>
 */

#ifndef LAP_COM_DDS_CDDSEVTMGR_HPP
#define LAP_COM_DDS_CDDSEVTMGR_HPP

// ==================== Project-Internal Headers ====================
#include "DdsTypes.hpp"
#include "CCdrChannel.hpp"
#include "ITransportBinding.hpp"
#include "IDdsTypeAdapter.hpp"

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
    using lap::core::Size;

    // ====================================================================
    // CDdsEventManager
    // ====================================================================

    /**
     * @brief   Event communication manager for the DDS binding
     *
     * @details Lazily creates DataWriters on SendEvent and DataReaders on
     *          SubscribeEvent.  Listener lifetime is managed via UniqueHandle.
     *
     * @note    NOT thread-safe — caller (facade) must serialise access
     */
    class CDdsEventManager
    {
    public:
        /**
         * @brief   Construct with references to shared DDS resources
         */
        CDdsEventManager(
            const DdsConfig& config,
            eprosima::fastdds::dds::DomainParticipant*& pParticipant,
            eprosima::fastdds::dds::Publisher*& pPublisher,
            eprosima::fastdds::dds::Subscriber*& pSubscriber,
            const eprosima::fastdds::dds::TypeSupport& typeSupport,
            TransportMetrics& metrics ) noexcept;

        ~CDdsEventManager() noexcept = default;

        // Rule of Five — non-copyable, non-movable
        CDdsEventManager( const CDdsEventManager& )             = delete;
        CDdsEventManager& operator=( const CDdsEventManager& )  = delete;
        CDdsEventManager( CDdsEventManager&& )                  = delete;
        CDdsEventManager& operator=( CDdsEventManager&& )       = delete;

    public:
        // ================================================================
        // Event Communication
        // ================================================================

        Result< void > SendEvent( UInt64 serviceId, UInt64 instanceId,
                                   UInt32 eventId,
                                   const void* pData,
                                   Size dataSize = 0 ) noexcept;

        Result< void > SubscribeEvent( UInt64 serviceId, UInt64 instanceId,
                                        UInt32 eventId,
                                        EventCallback callback,
                                        Size dataSize = 0 ) noexcept;

        Result< void > UnsubscribeEvent( UInt64 serviceId, UInt64 instanceId,
                                          UInt32 eventId ) noexcept;

        /**
         * @brief   Eagerly open event channel and create writer
         * @details Must be called from outside DDS callback context.
         *          Prevents deadlock when SendEvent is later called from
         *          within a DDS listener (e.g. method handler calling Update).
         */
        Result< void > PrepareChannel( UInt64 serviceId, UInt64 instanceId,
                                        UInt32 eventId ) noexcept;

        /**
         * @brief   Close all event channels and release resources
         */
        void Shutdown() noexcept;

    private:
        // ================================================================
        // References to Shared Resources (facade-owned)
        // ================================================================

        const DdsConfig&                                        m_config;
        eprosima::fastdds::dds::DomainParticipant*&             m_pParticipant;
        eprosima::fastdds::dds::Publisher*&                     m_pPublisher;
        eprosima::fastdds::dds::Subscriber*&                    m_pSubscriber;
        const eprosima::fastdds::dds::TypeSupport&              m_typeSupport;
        TransportMetrics&                                       m_metrics;

        // ================================================================
        // Owned Channels: one CCdrChannel per (serviceId, instanceId, eventId)
        // ================================================================

        Map< String, CCdrChannel >  m_mapChannels;
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_DDS_CDDSEVTMGR_HPP
