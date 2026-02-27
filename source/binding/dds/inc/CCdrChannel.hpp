/**
 * @file        CCdrChannel.hpp
 * @author      LightAP Development Team
 * @brief       DDS CDR Channel — Topic + unique Writer + N Readers
 * @date        2026/02/24
 * @copyright   Copyright (c) 2026
 *
 * @details     Encapsulates a single DDS Topic that shares one TypeSupport (CDR).
 *              Lazily creates at most one DataWriter and supports multiple
 *              DataReaders, each with its own optional DataReaderListener.
 *
 *              CDR (TypeSupport) is registered once per DomainParticipant and
 *              shared across all channels.  Each channel creates exactly one
 *              Topic; writer and readers are added lazily.
 *
 *              Ownership:
 *              - Topic, Writer, Readers: owned (deleted on Close/destructor)
 *              - Listeners: co-owned per reader (UniqueHandle lifetime)
 *              - Participant, Publisher, Subscriber: NOT owned (must outlive)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/24  <td>1.0      <td>LightAP Team    <td>Initial CdrChannel design
 * </table>
 */

#ifndef LAP_COM_DDS_CCDRCHANNEL_HPP
#define LAP_COM_DDS_CCDRCHANNEL_HPP

// ==================== Project-Internal Headers ====================
#include "DdsTypes.hpp"

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

    // ====================================================================
    // CCdrChannel
    // ====================================================================

    /**
     * @brief   DDS CDR Channel — one Topic, one Writer, N Readers
     *
     * @details Represents a single addressable DDS endpoint.  The CDR
     *          (TypeSupport) is registered once per DomainParticipant;
     *          each channel owns its Topic, at most one lazily-created
     *          Writer, and zero or more Readers (each with an optional
     *          listener).
     *
     *          Typical usage:
     *          - Event publish:   channel.GetOrCreateWriter()->write(&msg)
     *          - Event subscribe: channel.AddReader(listener)
     *          - Method RPC:      req channel (writer) + rep channel (reader)
     *
     * @note    NOT thread-safe — caller must serialise access.
     */
    class CCdrChannel
    {
    public:
        CCdrChannel() noexcept = default;
        ~CCdrChannel() noexcept;

        // Non-copyable
        CCdrChannel( const CCdrChannel& )            = delete;
        CCdrChannel& operator=( const CCdrChannel& ) = delete;

        // Movable
        CCdrChannel( CCdrChannel&& rhs ) noexcept;
        CCdrChannel& operator=( CCdrChannel&& rhs ) noexcept;

    public:
        // ================================================================
        // Lifecycle
        // ================================================================

        /**
         * @brief   Open the channel — creates the DDS Topic
         * @param   pParticipant  DDS DomainParticipant (must be initialized)
         * @param   pPublisher    DDS Publisher for writer creation
         * @param   pSubscriber   DDS Subscriber for reader creation
         * @param   typeSupport   Shared CDR TypeSupport (already registered)
         * @param   topicName     Unique DDS topic name
         * @param   config        QoS configuration
         * @return  true on success, false if topic creation fails
         */
        bool Open(
            eprosima::fastdds::dds::DomainParticipant* pParticipant,
            eprosima::fastdds::dds::Publisher*          pPublisher,
            eprosima::fastdds::dds::Subscriber*         pSubscriber,
            const eprosima::fastdds::dds::TypeSupport&  typeSupport,
            const String&                               topicName,
            const DdsConfig&                            config ) noexcept;

        /**
         * @brief   Close the channel — destroy all entities
         * @details Deletes readers (with listeners), writer, and topic
         *          in correct DDS dependency order.
         */
        void Close() noexcept;

        /**
         * @brief   Check if channel is open (Topic created)
         */
        bool IsOpen() const noexcept { return m_pTopic != nullptr; }

    public:
        // ================================================================
        // Writer (unique — at most one per channel)
        // ================================================================

        /**
         * @brief   Get or lazily create the unique DataWriter
         * @return  DataWriter* or nullptr on failure
         */
        eprosima::fastdds::dds::DataWriter* GetOrCreateWriter() noexcept;

        /**
         * @brief   Get writer (nullptr if not yet created)
         */
        eprosima::fastdds::dds::DataWriter* GetWriter() const noexcept
        {
            return m_pWriter;
        }

        /**
         * @brief   Check if writer exists
         */
        bool HasWriter() const noexcept { return m_pWriter != nullptr; }

    public:
        // ================================================================
        // Readers (0..N per channel)
        // ================================================================

        /**
         * @brief   Add a DataReader with optional listener
         * @param   pListener  Listener (ownership transferred; nullptr for polling)
         * @param   mask       Status mask for listener callback filtering
         * @return  DataReader* or nullptr on failure
         */
        eprosima::fastdds::dds::DataReader* AddReader(
            UniqueHandle< eprosima::fastdds::dds::DataReaderListener >
                pListener = nullptr,
            eprosima::fastdds::dds::StatusMask mask
                = eprosima::fastdds::dds::StatusMask::all() ) noexcept;

        /**
         * @brief   Remove and destroy a specific reader
         * @param   pReader  The reader to remove (must belong to this channel)
         */
        void RemoveReader(
            eprosima::fastdds::dds::DataReader* pReader ) noexcept;

        /**
         * @brief   Get the first (or only) reader
         * @return  DataReader* or nullptr if no readers
         */
        eprosima::fastdds::dds::DataReader* GetFirstReader() const noexcept;

        /**
         * @brief   Number of active readers
         */
        Size ReaderCount() const noexcept
        {
            return static_cast< Size >( m_vecReaders.size() );
        }

        /**
         * @brief   Check if channel has no writer and no readers
         */
        bool IsEmpty() const noexcept
        {
            return m_pWriter == nullptr && m_vecReaders.empty();
        }

    public:
        // ================================================================
        // Accessors
        // ================================================================

        /**
         * @brief   Get underlying Topic
         */
        eprosima::fastdds::dds::Topic* GetTopic() const noexcept
        {
            return m_pTopic;
        }

        /**
         * @brief   Get topic name (empty if not open)
         */
        const String& GetTopicName() const noexcept { return m_strTopicName; }

    private:
        // ================================================================
        // External references (NOT owned — must outlive this channel)
        // ================================================================

        eprosima::fastdds::dds::DomainParticipant*  m_pParticipant = nullptr;
        eprosima::fastdds::dds::Publisher*           m_pPublisher   = nullptr;
        eprosima::fastdds::dds::Subscriber*          m_pSubscriber  = nullptr;
        const DdsConfig*                             m_pConfig      = nullptr;

        // ================================================================
        // Owned DDS entities
        // ================================================================

        eprosima::fastdds::dds::Topic*       m_pTopic  = nullptr;  ///< Shared by W+R
        eprosima::fastdds::dds::DataWriter*  m_pWriter = nullptr;  ///< Unique (0..1)
        String                               m_strTopicName;

        /**
         * @brief   Reader slot — owns the reader and its listener
         */
        struct ReaderEntry
        {
            eprosima::fastdds::dds::DataReader* pReader = nullptr;
            UniqueHandle< eprosima::fastdds::dds::DataReaderListener >
                                                pListener;
        };
        Vector< ReaderEntry > m_vecReaders;                        ///< 0..N readers
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_DDS_CCDRCHANNEL_HPP
