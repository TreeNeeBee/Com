/**
 * @file        CDdsServiceManager.cpp
 * @author      LightAP Development Team
 * @brief       DDS binding — Service lifecycle management implementation
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Extracted from DdsService.cpp
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CDdsServiceManager.hpp"
#include "CDdsCodec.hpp"
#include "DdsDiscoveryListener.hpp"
#include "ComTypes.hpp"

// ==================== Standard Library Headers ====================
#include <unordered_set>

namespace lap
{
namespace com
{
namespace binding
{

    using namespace eprosima::fastdds::dds;

    // ====================================================================
    // Constructor
    // ====================================================================

    CDdsServiceManager::CDdsServiceManager(
        const DdsConfig& config,
        DomainParticipant*& pParticipant,
        Publisher*& pPublisher,
        Subscriber*& pSubscriber,
        const TypeSupport& typeSupport,
        DdsDiscoveryListener*& pDiscoveryListener,
        TransportMetrics& metrics ) noexcept
        : m_config( config )
        , m_pParticipant( pParticipant )
        , m_pPublisher( pPublisher )
        , m_pSubscriber( pSubscriber )
        , m_typeSupport( typeSupport )
        , m_pDiscoveryListener( pDiscoveryListener )
        , m_metrics( metrics )
    {}

    // ====================================================================
    // Service Lifecycle
    // ====================================================================

    Result< void > CDdsServiceManager::OfferService(
        UInt64 serviceId, UInt64 instanceId ) noexcept
    {
        if ( m_pParticipant == nullptr || m_pPublisher == nullptr ) {
            return Result< void >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        // Create a "presence" DataWriter on eventId=0 so other
        // participants' discovery listeners can detect this service.
        constexpr UInt32 kPresenceEventId = 0;
        auto key = CDdsCodec::MakeEventKey( serviceId, instanceId, kPresenceEventId );

        auto& channel = m_mapChannels[key];
        if ( !channel.IsOpen() ) {
            auto topicName = CDdsCodec::MakeEventTopicName(
                serviceId, instanceId, kPresenceEventId );
            if ( !channel.Open( m_pParticipant, m_pPublisher, m_pSubscriber,
                                m_typeSupport, topicName, m_config ) ) {
                m_mapChannels.erase( key );
                LAP_COM_LOG_ERROR << "Failed to create presence topic for service 0x"
                                  << serviceId;
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }
        }

        if ( !channel.HasWriter() ) {
            auto* pWriter = channel.GetOrCreateWriter();
            if ( pWriter == nullptr ) {
                LAP_COM_LOG_ERROR << "Failed to create presence writer for service 0x"
                                  << serviceId;
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kCommunicationFailure ) );
            }
        }

        LAP_COM_LOG_INFO << "Service offered (DDS): serviceId=0x" << serviceId
                         << ", instanceId=0x" << instanceId;

        return Result< void >::FromValue();
    }

    Result< void > CDdsServiceManager::StopOfferService(
        UInt64 serviceId, UInt64 instanceId ) noexcept
    {
        constexpr UInt32 kPresenceEventId = 0;
        auto key = CDdsCodec::MakeEventKey( serviceId, instanceId, kPresenceEventId );

        auto it = m_mapChannels.find( key );
        if ( it != m_mapChannels.end() ) {
            it->second.Close();
            m_mapChannels.erase( it );
        }

        LAP_COM_LOG_INFO << "Service stopped (DDS): serviceId=0x" << serviceId
                         << ", instanceId=0x" << instanceId;

        return Result< void >::FromValue();
    }

    Result< Vector< UInt64 > > CDdsServiceManager::FindService(
        UInt64 serviceId ) noexcept
    {
        if ( m_pDiscoveryListener == nullptr ) {
            LAP_COM_LOG_ERROR << "FindService called before Initialize";
            return Result< Vector< UInt64 > >::FromError(
                MakeErrorCode( ComErrc::kNotInitialized ) );
        }

        // First try discovery listener (for remote entities)
        auto instances = m_pDiscoveryListener->GetDiscoveredInstances(
            serviceId, m_pParticipant );

        // Also check local presence channels
        ::std::unordered_set< UInt64 > allInstances(
            instances.begin(), instances.end() );

        for ( const auto& [key, channel] : m_mapChannels ) {
            // Key format: "service_id_instance_id_event_id"
            auto firstUnderscore = key.find( '_' );
            if ( firstUnderscore != String::npos ) {
                try {
                    UInt64 sid = ::std::stoull(
                        key.substr( 0, firstUnderscore ), nullptr, 16 );
                    if ( sid == serviceId ) {
                        auto secondUnderscore = key.find( '_', firstUnderscore + 1 );
                        if ( secondUnderscore != String::npos ) {
                            String instanceStr = key.substr(
                                firstUnderscore + 1,
                                secondUnderscore - firstUnderscore - 1 );
                            UInt64 instanceId = ::std::stoull( instanceStr, nullptr, 16 );
                            allInstances.insert( instanceId );
                        }
                    }
                } catch ( const ::std::exception& ) {
                    // Skip malformed keys
                }
            }
        }

        Vector< UInt64 > result( allInstances.begin(), allInstances.end() );

        LAP_COM_LOG_DEBUG << "FindService(0x" << serviceId << ") found "
                          << result.size() << " instances ("
                          << instances.size() << " remote, "
                          << ( result.size() - instances.size() ) << " local)";

        return Result< Vector< UInt64 > >::FromValue( ::std::move( result ) );
    }

    // ====================================================================
    // Lifecycle
    // ====================================================================

    void CDdsServiceManager::Shutdown() noexcept
    {
        for ( auto& [key, channel] : m_mapChannels )
        {
            channel.Close();
        }
        m_mapChannels.clear();
    }

} // namespace binding
} // namespace com
} // namespace lap
