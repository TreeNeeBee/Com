/**
 * @file        CCdrChannel.cpp
 * @author      LightAP Development Team
 * @brief       DDS CDR Channel implementation
 * @date        2026/02/24
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/24  <td>1.0      <td>LightAP Team    <td>Initial implementation
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CCdrChannel.hpp"
#include "CDdsCodec.hpp"
#include "ComTypes.hpp"

namespace lap
{
namespace com
{
namespace binding
{

    using namespace eprosima::fastdds::dds;

    // ====================================================================
    // Destructor / Move
    // ====================================================================

    CCdrChannel::~CCdrChannel() noexcept
    {
        Close();
    }

    CCdrChannel::CCdrChannel( CCdrChannel&& rhs ) noexcept
        : m_pParticipant( rhs.m_pParticipant )
        , m_pPublisher( rhs.m_pPublisher )
        , m_pSubscriber( rhs.m_pSubscriber )
        , m_pConfig( rhs.m_pConfig )
        , m_pTopic( rhs.m_pTopic )
        , m_pWriter( rhs.m_pWriter )
        , m_strTopicName( ::std::move( rhs.m_strTopicName ) )
        , m_vecReaders( ::std::move( rhs.m_vecReaders ) )
    {
        rhs.m_pParticipant = nullptr;
        rhs.m_pPublisher   = nullptr;
        rhs.m_pSubscriber  = nullptr;
        rhs.m_pConfig      = nullptr;
        rhs.m_pTopic       = nullptr;
        rhs.m_pWriter      = nullptr;
    }

    CCdrChannel& CCdrChannel::operator=( CCdrChannel&& rhs ) noexcept
    {
        if ( this != &rhs )
        {
            Close();
            m_pParticipant = rhs.m_pParticipant;
            m_pPublisher   = rhs.m_pPublisher;
            m_pSubscriber  = rhs.m_pSubscriber;
            m_pConfig      = rhs.m_pConfig;
            m_pTopic       = rhs.m_pTopic;
            m_pWriter      = rhs.m_pWriter;
            m_strTopicName = ::std::move( rhs.m_strTopicName );
            m_vecReaders   = ::std::move( rhs.m_vecReaders );

            rhs.m_pParticipant = nullptr;
            rhs.m_pPublisher   = nullptr;
            rhs.m_pSubscriber  = nullptr;
            rhs.m_pConfig      = nullptr;
            rhs.m_pTopic       = nullptr;
            rhs.m_pWriter      = nullptr;
        }
        return *this;
    }

    // ====================================================================
    // Lifecycle
    // ====================================================================

    bool CCdrChannel::Open(
        DomainParticipant* pParticipant,
        Publisher*          pPublisher,
        Subscriber*         pSubscriber,
        const TypeSupport&  typeSupport,
        const String&       topicName,
        const DdsConfig&    config ) noexcept
    {
        if ( m_pTopic != nullptr )
        {
            return true;   // already open
        }

        Topic* pTopic = pParticipant->create_topic(
            topicName, typeSupport.get_type_name(), TOPIC_QOS_DEFAULT );
        if ( pTopic == nullptr )
        {
            LAP_COM_LOG_ERROR << "CCdrChannel: failed to create topic '"
                              << topicName << "'";
            return false;
        }

        m_pParticipant = pParticipant;
        m_pPublisher   = pPublisher;
        m_pSubscriber  = pSubscriber;
        m_pConfig      = &config;
        m_pTopic       = pTopic;
        m_strTopicName = topicName;

        LAP_COM_LOG_DEBUG << "CCdrChannel opened: " << topicName
                          << " (type=" << typeSupport.get_type_name() << ")";
        return true;
    }

    void CCdrChannel::Close() noexcept
    {
        if ( m_pTopic == nullptr )
        {
            return;
        }

        // 1. Delete readers (with listeners)
        for ( auto& entry : m_vecReaders )
        {
            if ( entry.pReader != nullptr && m_pSubscriber != nullptr )
            {
                m_pSubscriber->delete_datareader( entry.pReader );
            }
            // UniqueHandle<DataReaderListener> destructs automatically
        }
        m_vecReaders.clear();

        // 2. Delete writer
        if ( m_pWriter != nullptr && m_pPublisher != nullptr )
        {
            m_pPublisher->delete_datawriter( m_pWriter );
            m_pWriter = nullptr;
        }

        // 3. Delete topic
        if ( m_pTopic != nullptr && m_pParticipant != nullptr )
        {
            m_pParticipant->delete_topic( m_pTopic );
        }

        LAP_COM_LOG_DEBUG << "CCdrChannel closed: " << m_strTopicName;

        m_pTopic       = nullptr;
        m_pParticipant = nullptr;
        m_pPublisher   = nullptr;
        m_pSubscriber  = nullptr;
        m_pConfig      = nullptr;
        m_strTopicName.clear();
    }

    // ====================================================================
    // Writer
    // ====================================================================

    DataWriter* CCdrChannel::GetOrCreateWriter() noexcept
    {
        if ( m_pWriter != nullptr )
        {
            return m_pWriter;
        }

        if ( m_pTopic == nullptr || m_pPublisher == nullptr ||
             m_pConfig == nullptr )
        {
            return nullptr;
        }

        m_pWriter = CDdsCodec::CreateWriter( m_pPublisher, m_pTopic, *m_pConfig );
        if ( m_pWriter != nullptr )
        {
            LAP_COM_LOG_DEBUG << "CCdrChannel: writer created for '"
                              << m_strTopicName << "'";
        }
        return m_pWriter;
    }

    // ====================================================================
    // Readers
    // ====================================================================

    DataReader* CCdrChannel::AddReader(
        UniqueHandle< DataReaderListener > pListener,
        StatusMask mask ) noexcept
    {
        if ( m_pTopic == nullptr || m_pSubscriber == nullptr ||
             m_pConfig == nullptr )
        {
            return nullptr;
        }

        DataReaderListener* pRawListener = pListener.get();
        DataReader* pReader = CDdsCodec::CreateReader(
            m_pSubscriber, m_pTopic, *m_pConfig, pRawListener, mask );

        if ( pReader == nullptr )
        {
            return nullptr;
        }

        m_vecReaders.push_back( { pReader, ::std::move( pListener ) } );

        LAP_COM_LOG_DEBUG << "CCdrChannel: reader #" << m_vecReaders.size()
                          << " added for '" << m_strTopicName << "'";
        return pReader;
    }

    void CCdrChannel::RemoveReader(
        DataReader* pReader ) noexcept
    {
        for ( auto it = m_vecReaders.begin(); it != m_vecReaders.end(); ++it )
        {
            if ( it->pReader == pReader )
            {
                if ( m_pSubscriber != nullptr )
                {
                    m_pSubscriber->delete_datareader( pReader );
                }
                m_vecReaders.erase( it );
                LAP_COM_LOG_DEBUG << "CCdrChannel: reader removed from '"
                                  << m_strTopicName << "'";
                return;
            }
        }
    }

    DataReader* CCdrChannel::GetFirstReader() const noexcept
    {
        return m_vecReaders.empty() ? nullptr : m_vecReaders.front().pReader;
    }

} // namespace binding
} // namespace com
} // namespace lap
