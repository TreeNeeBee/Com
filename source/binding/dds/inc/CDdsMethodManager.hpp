/**
 * @file        CDdsMethodManager.hpp
 * @author      LightAP Development Team
 * @brief       DDS binding — Method and Field communication management
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Manages Method/Field RPC for the DDS binding:
 *              - CallMethod  — client-side request-reply via DDS topics
 *              - RegisterMethod — server-side handler registration
 *              - GetField / SetField — thin wrappers mapped to CallMethod
 *
 *              NOT thread-safe — the facade (DdsBinding) serialises access
 *              for RegisterMethod, but CallMethod has its own per-key mutex.
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Extracted from monolithic DdsBinding
 * </table>
 */

#ifndef LAP_COM_DDS_CDDSMETHODMGR_HPP
#define LAP_COM_DDS_CDDSMETHODMGR_HPP

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
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
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
    // CDdsMethodManager
    // ====================================================================

    /**
     * @brief   Method/Field RPC manager for the DDS binding
     *
     * @details Owns method topic/writer/reader/listener/handler maps.
     *          CallMethod creates request/response topics lazily and polls
     *          for replies.  RegisterMethod creates a request reader with
     *          a DdsMethodReaderListener that dispatches to the handler.
     *
     * @note    CallMethod has its own per-key mutex.
     *          RegisterMethod assumes facade lock is held.
     */
    class CDdsMethodManager
    {
    public:
        /**
         * @brief   Construct with references to shared DDS resources
         */
        CDdsMethodManager(
            const DdsConfig& config,
            eprosima::fastdds::dds::DomainParticipant*& pParticipant,
            eprosima::fastdds::dds::Publisher*& pPublisher,
            eprosima::fastdds::dds::Subscriber*& pSubscriber,
            const eprosima::fastdds::dds::TypeSupport& typeSupport,
            TransportMetrics& metrics ) noexcept;

        ~CDdsMethodManager() noexcept = default;

        // Rule of Five — non-copyable, non-movable
        CDdsMethodManager( const CDdsMethodManager& )             = delete;
        CDdsMethodManager& operator=( const CDdsMethodManager& )  = delete;
        CDdsMethodManager( CDdsMethodManager&& )                  = delete;
        CDdsMethodManager& operator=( CDdsMethodManager&& )       = delete;

    public:
        // ================================================================
        // Method Communication (type-erased NVI signatures)
        // ================================================================

        Result< void > CallMethod( UInt64 serviceId, UInt64 instanceId,
                                    UInt32 methodId,
                                    const void* pRequest,
                                    void* pResponse,
                                    Size requestSize = 0,
                                    Size responseSize = 0 ) noexcept;

        Result< void > RegisterMethod( UInt64 serviceId, UInt64 instanceId,
                                        UInt32 methodId,
                                        MethodHandler handler,
                                        Size requestSize = 0,
                                        Size responseSize = 0 ) noexcept;

    public:
        // ================================================================
        // Field Communication (delegates to CallMethod)
        // ================================================================

        Result< void > GetField( UInt64 serviceId, UInt64 instanceId,
                                  UInt32 fieldId,
                                  void* pOutValue ) noexcept;

        Result< void > SetField( UInt64 serviceId, UInt64 instanceId,
                                  UInt32 fieldId,
                                  const void* pValue ) noexcept;

    public:
        // ================================================================
        // Lifecycle (cleanup all DDS method entities)
        // ================================================================

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
        TransportMetrics&                                   m_metrics;

        // ================================================================
        // Owned Method Channels
        // ================================================================

        Map< String, CCdrChannel >  m_mapChannels;  ///< req/rep channels by topic name
        Map< String, MethodHandler >                         m_mapMethodHandlers;
        Map< String, UniqueHandle< Mutex > >                 m_mapMethodMutexes;
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_DDS_CDDSMETHODMGR_HPP
