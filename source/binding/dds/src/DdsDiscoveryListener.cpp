/**
 * @file        DdsDiscoveryListener.cpp
 * @author      LightAP Development Team
 * @brief       DDS discovery listener — writer discovery implementation
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Implements on_data_writer_discovery and GetDiscoveredInstances.
 *              Parses topic names ("lap/com/{serviceId}/{instanceId}/{eventId}")
 *              to maintain a cache of discovered service instances.
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/23  <td>1.0      <td>LightAP Team    <td>Initial (in DdsBinding.cpp)
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style refactor per code_rules.md
 * <tr><td>2026/02/07  <td>3.0      <td>Aii             <td>Type compliance (CTypedef/CSync aliases)
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "DdsDiscoveryListener.hpp"
#include "ComTypes.hpp"

// ==================== Standard Library Headers ====================
#include <iomanip>
#include <sstream>

namespace lap
{
namespace com
{
namespace binding
{

    // ====================================================================
    // DdsDiscoveryListener Implementation
    // ====================================================================

    void DdsDiscoveryListener::on_data_writer_discovery(
        eprosima::fastdds::dds::DomainParticipant* participant [[maybe_unused]],
        eprosima::fastdds::rtps::WriterDiscoveryStatus reason,
        const eprosima::fastdds::rtps::PublicationBuiltinTopicData& info,
        bool& shouldBeIgnored )
    {
        shouldBeIgnored = false;

        // Parse topic name to extract serviceId and instanceId
        // Topic name format: "lap/com/{serviceId}/{instanceId}/{eventId}"
        const String topicName = info.topic_name.to_string();
        const String prefix = "lap/com/";

        if ( topicName.rfind( prefix, 0 ) != 0 ) {
            return;
        }

        Vector< String > parts;
        ::std::stringstream ss( topicName.substr( prefix.size() ) );
        String token;
        while ( ::std::getline( ss, token, '/' ) ) {
            parts.push_back( token );
        }

        if ( parts.size() < 3 ) {
            return;
        }

        try {
            const UInt64 serviceId  = ::std::stoull( parts[0], nullptr, 16 );
            const UInt64 instanceId = ::std::stoull( parts[1], nullptr, 16 );

            LockGuard lock( m_discoveryMutex );

            if ( reason == eprosima::fastdds::rtps::WriterDiscoveryStatus::DISCOVERED_WRITER ) {
                m_mapDiscoveredServices[serviceId].insert( instanceId );

                ::std::ostringstream oss;
                oss << "Discovered DDS publisher: service=0x"
                    << ::std::hex << ::std::setw( 4 ) << ::std::setfill( '0' )
                    << serviceId << ", instance=0x"
                    << ::std::setw( 4 ) << ::std::setfill( '0' ) << instanceId
                    << " (topic: " << topicName << ")";
                LAP_COM_LOG_INFO << oss.str();

            } else if ( reason == eprosima::fastdds::rtps::WriterDiscoveryStatus::REMOVED_WRITER ) {
                auto it = m_mapDiscoveredServices.find( serviceId );
                if ( it != m_mapDiscoveredServices.end() ) {
                    it->second.erase( instanceId );
                    if ( it->second.empty() ) {
                        m_mapDiscoveredServices.erase( it );
                    }
                }

                ::std::ostringstream oss;
                oss << "Removed DDS publisher: service=0x"
                    << ::std::hex << ::std::setw( 4 ) << ::std::setfill( '0' )
                    << serviceId << ", instance=0x"
                    << ::std::setw( 4 ) << ::std::setfill( '0' ) << instanceId;
                LAP_COM_LOG_INFO << oss.str();
            }

            // ---- Fire change callback if registered ----
            if ( m_changeCallback ) {
                auto it = m_mapDiscoveredServices.find( serviceId );
                Vector< UInt64 > instances;
                if ( it != m_mapDiscoveredServices.end() ) {
                    instances.assign( it->second.begin(), it->second.end() );
                }
                m_changeCallback( serviceId, ::std::move( instances ) );
            }

        } catch ( const ::std::exception& e ) {
            LAP_COM_LOG_WARN << "Failed to parse topic name '" << topicName
                             << "': " << e.what();
        }
    }

    void DdsDiscoveryListener::SetDiscoveryChangeCallback(
        DiscoveryChangeCallback callback ) noexcept
    {
        LockGuard lock( m_discoveryMutex );
        m_changeCallback = ::std::move( callback );
    }

    Vector< UInt64 > DdsDiscoveryListener::GetDiscoveredInstances(
        UInt64 serviceId,
        eprosima::fastdds::dds::DomainParticipant* pParticipant [[maybe_unused]] ) const
    {
        LockGuard lock( m_discoveryMutex );

        auto it = m_mapDiscoveredServices.find( serviceId );
        if ( it != m_mapDiscoveredServices.end() && !it->second.empty() ) {
            LAP_COM_LOG_DEBUG << "Found " << it->second.size()
                              << " cached instances for service 0x" << serviceId;
            return Vector< UInt64 > ( it->second.begin(), it->second.end() );
        }

        LAP_COM_LOG_DEBUG << "No cached discoveries for service 0x" << serviceId;
        return Vector< UInt64 > ();
    }

} // namespace binding
} // namespace com
} // namespace lap
