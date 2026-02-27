/**
 * @file        DdsDiscoveryListener.hpp
 * @author      LightAP Development Team
 * @brief       DDS discovery listener for tracking remote service instances
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Listens to on_data_writer_discovery to maintain a list
 *              of available service instances discovered on the network.
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/23  <td>1.0      <td>LightAP Team    <td>Initial (in DdsBinding.hpp)
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style refactor per code_rules.md
 * <tr><td>2026/02/07  <td>3.0      <td>Aii             <td>Type compliance (CTypedef/CSync aliases)
 * </table>
 */

#ifndef LAP_COM_DDS_DISCOVERY_LISTENER_HPP
#define LAP_COM_DDS_DISCOVERY_LISTENER_HPP

// ==================== Project-Internal Headers ====================
#include "DdsTypes.hpp"

// ==================== Third-Party Headers ====================
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantListener.hpp>
#include <fastdds/rtps/builtin/data/PublicationBuiltinTopicData.hpp>
#include <fastdds/rtps/writer/WriterDiscoveryStatus.hpp>

// ==================== Cross-Module Headers ====================
#include <core/CFunction.hpp>

// ==================== Standard Library Headers ====================
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace lap
{
namespace com
{
namespace binding
{

    // Forward declaration
    class DdsBinding;

    // ====================================================================
    // DDS Discovery Listener
    // ====================================================================

    /**
     * @brief   Discovery Listener for tracking remote service instances
     * @details Listens to on_data_writer_discovery to maintain a list of available
     *          service instances discovered on the network.
     *          Also uses direct querying of builtin topics for robustness.
     */
    class DdsDiscoveryListener : public eprosima::fastdds::dds::DomainParticipantListener
    {
    public:
        /**
         * @brief   Callback type for service availability change notifications
         * @param   serviceId   The service whose availability changed
         * @param   instances   Current set of available instance IDs
         */
        using DiscoveryChangeCallback = Function< void(
            UInt64 serviceId, Vector< UInt64 > instances ) >;

    public:
        explicit DdsDiscoveryListener( DdsBinding* pBinding ) noexcept
            : m_pBinding( pBinding )
        {}

        void on_data_writer_discovery(
            eprosima::fastdds::dds::DomainParticipant* participant,
            eprosima::fastdds::rtps::WriterDiscoveryStatus reason,
            const eprosima::fastdds::rtps::PublicationBuiltinTopicData& info,
            bool& shouldBeIgnored ) override;

        /**
         * @brief   Get discovered instance IDs for a service
         * @param   serviceId       Service identifier
         * @param   pParticipant    DomainParticipant to query builtin topics
         * @return  Vector of instance IDs
         */
        Vector< UInt64 > GetDiscoveredInstances(
            UInt64 serviceId,
            eprosima::fastdds::dds::DomainParticipant* pParticipant ) const;

        /**
         * @brief   Register a callback to be notified on service changes
         * @param   callback    Function to call when a service's instance set changes
         *
         * @details The callback is invoked from within the DDS discovery
         *          thread holding m_discoveryMutex.  Implementations must
         *          not call back into DdsDiscoveryListener from within the
         *          callback to avoid deadlocks.
         */
        void SetDiscoveryChangeCallback( DiscoveryChangeCallback callback ) noexcept;

    private:
        DdsBinding*     m_pBinding;
        mutable Mutex   m_discoveryMutex;

        /// Map: serviceId -> set of instance_ids
        ::std::unordered_map< UInt64, ::std::unordered_set< UInt64 > >
                        m_mapDiscoveredServices;

        /// Optional callback fired when service availability changes
        DiscoveryChangeCallback m_changeCallback;
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_DDS_DISCOVERY_LISTENER_HPP
