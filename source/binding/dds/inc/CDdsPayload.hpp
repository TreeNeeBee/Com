/**
 * @file        CDdsPayload.hpp
 * @author      Aii
 * @brief       Minimal DDS wire type and PubSubType — replaces LapComMessage.idl
 * @date        2026/02/24
 * @details     Defines a lean binary-payload DDS message type entirely in C++
 *              code, eliminating the need for a static .idl file.  Addressing
 *              information (service / instance / event IDs) is encoded in the
 *              DDS topic name; only a correlation ID and the raw application
 *              payload travel on the wire.
 *
 *              When per-service strongly-typed IDL is generated through the
 *              lap-sidl-gen → fastddsgen pipeline, services register their
 *              own TypeSupport via IDdsTypeAdapter / CDdsTypeRegistry.
 *              DdsPayload serves as the built-in fallback type.
 *
 * @copyright   Copyright (c) 2026
 * @reference   GENERATOR.md §11 — Dual-layer IDL Design
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/24  <td>1.0      <td>Aii     <td>init version — replaces LapComMessage.idl
 * </table>
 */

#ifndef LAP_COM_DDS_CDDSPAYLOAD_HPP
#define LAP_COM_DDS_CDDSPAYLOAD_HPP

// ==================== Cross-Module Headers ====================
#include <lap/core/CTypedef.hpp>

// ==================== Third-Party Headers ====================
#include <fastdds/dds/topic/TopicDataType.hpp>
#include <fastdds/rtps/common/SerializedPayload.hpp>
#include <fastdds/rtps/common/InstanceHandle.hpp>
#include <fastdds/utils/md5.hpp>
#include <fastcdr/Cdr.h>
#include <fastcdr/FastBuffer.h>
#include <fastcdr/CdrSizeCalculator.hpp>

// ==================== Standard Library Headers ====================
#include <cstdint>
#include <cstring>
#include <vector>

namespace lap
{
namespace com
{
namespace binding
{

    using lap::core::UInt8;
    using lap::core::UInt32;
    using lap::core::UInt64;

    // ====================================================================
    // DdsPayload — Minimal DDS Wire Type
    // ====================================================================

    /**
     * @brief   Minimal DDS message carrying a correlation ID and raw bytes
     *
     * @details Replaces the legacy LapComMessage generic envelope.
     *          Only two fields travel on the wire:
     *            - m_iRequestId : correlation token for request/response RPC
     *            - m_data       : CDR-serialised application payload
     *
     *          Addressing (service / instance / event) is encoded in the
     *          DDS topic name, so it is NOT duplicated in the message body.
     *
     * @note    Thread-safe (POD-like, no shared state)
     */
    struct DdsPayload
    {
        UInt64                      m_iRequestId { 0 };
        ::std::vector< UInt8 >     m_data;

        DdsPayload() = default;

        explicit DdsPayload( ::std::vector< UInt8 > data ) noexcept
            : m_data( ::std::move( data ) )
        {}

        DdsPayload( UInt64 requestId, ::std::vector< UInt8 > data ) noexcept
            : m_iRequestId( requestId )
            , m_data( ::std::move( data ) )
        {}

        // ---- Accessors (FastDDS-gen style) ----

        UInt64  request_id() const noexcept                         { return m_iRequestId; }
        void    request_id( UInt64 v ) noexcept                     { m_iRequestId = v; }

        const ::std::vector< UInt8 >& data() const noexcept        { return m_data; }
        ::std::vector< UInt8 >&       data() noexcept               { return m_data; }
        void  data( ::std::vector< UInt8 > v ) noexcept             { m_data = ::std::move( v ); }
    };

    // ====================================================================
    // CDR Helpers (in eprosima::fastcdr namespace, matching fastddsgen style)
    // ====================================================================

} // namespace binding
} // namespace com
} // namespace lap

// -------------------- CDR size / serialize / deserialize --------------------
// Must live in eprosima::fastcdr so that operator<< / operator>> overloads
// are found by ADL, exactly like fastddsgen-generated code.

namespace eprosima
{
namespace fastcdr
{

    // ---- Max CDR size constants ----
    // 8 (requestId) + 4 (seq length header) = 12 (plus variable data)
    constexpr uint32_t lap_com_binding_DdsPayload_max_cdr_typesize { 12U };
    constexpr uint32_t lap_com_binding_DdsPayload_max_key_cdr_typesize { 0U };

    // ---- calculate_serialized_size ----

    template<>
    inline size_t calculate_serialized_size(
        eprosima::fastcdr::CdrSizeCalculator& calculator,
        const lap::com::binding::DdsPayload& data,
        size_t& current_alignment )
    {
        eprosima::fastcdr::EncodingAlgorithmFlag prev = calculator.get_encoding();
        size_t calculated = calculator.begin_calculate_type_serialized_size(
            eprosima::fastcdr::CdrVersion::XCDRv2 == calculator.get_cdr_version()
                ? eprosima::fastcdr::EncodingAlgorithmFlag::DELIMIT_CDR2
                : eprosima::fastcdr::EncodingAlgorithmFlag::PLAIN_CDR,
            current_alignment );

        calculated += calculator.calculate_member_serialized_size(
            eprosima::fastcdr::MemberId( 0 ), data.request_id(), current_alignment );
        calculated += calculator.calculate_member_serialized_size(
            eprosima::fastcdr::MemberId( 1 ), data.data(), current_alignment );

        calculated += calculator.end_calculate_type_serialized_size(
            prev, current_alignment );
        return calculated;
    }

    // ---- serialize ----

    template<>
    inline void serialize(
        eprosima::fastcdr::Cdr& scdr,
        const lap::com::binding::DdsPayload& data )
    {
        eprosima::fastcdr::Cdr::state current_state( scdr );
        scdr.begin_serialize_type( current_state,
            eprosima::fastcdr::CdrVersion::XCDRv2 == scdr.get_cdr_version()
                ? eprosima::fastcdr::EncodingAlgorithmFlag::DELIMIT_CDR2
                : eprosima::fastcdr::EncodingAlgorithmFlag::PLAIN_CDR );

        scdr
            << eprosima::fastcdr::MemberId( 0 ) << data.request_id()
            << eprosima::fastcdr::MemberId( 1 ) << data.data();

        scdr.end_serialize_type( current_state );
    }

    // ---- deserialize ----

    template<>
    inline void deserialize(
        eprosima::fastcdr::Cdr& cdr,
        lap::com::binding::DdsPayload& data )
    {
        cdr.deserialize_type(
            eprosima::fastcdr::CdrVersion::XCDRv2 == cdr.get_cdr_version()
                ? eprosima::fastcdr::EncodingAlgorithmFlag::DELIMIT_CDR2
                : eprosima::fastcdr::EncodingAlgorithmFlag::PLAIN_CDR,
            [&data]( eprosima::fastcdr::Cdr& dcdr,
                     const eprosima::fastcdr::MemberId& mid ) -> bool
            {
                switch ( mid.id )
                {
                    case 0:
                        dcdr >> data.m_iRequestId;
                        return true;
                    case 1:
                        dcdr >> data.m_data;
                        return true;
                    default:
                        return false;
                }
            } );
    }

    // ---- serialize_key (no key fields) ----

    inline void serialize_key(
        eprosima::fastcdr::Cdr& scdr,
        const lap::com::binding::DdsPayload& data )
    {
        static_cast< void >( scdr );
        static_cast< void >( data );
    }

} // namespace fastcdr
} // namespace eprosima

// ====================================================================
// DdsPayloadPubSubType — TopicDataType for DdsPayload
// ====================================================================

namespace lap
{
namespace com
{
namespace binding
{

    /**
     * @brief   FastDDS TopicDataType for DdsPayload
     *
     * @details Hand-written PubSubType following the fastddsgen v3 API
     *          pattern.  No external .idl file or code generator required.
     *
     * @note    Thread-safe (stateless serialization, per-instance key buffer)
     */
    class DdsPayloadPubSubType : public eprosima::fastdds::dds::TopicDataType
    {
    public:
        using type = DdsPayload;

        DdsPayloadPubSubType() noexcept
        {
            set_name( "lap::com::binding::DdsPayload" );
            constexpr UInt32 kTypeSize =
                eprosima::fastcdr::lap_com_binding_DdsPayload_max_cdr_typesize;
            UInt32 aligned = kTypeSize +
                static_cast< UInt32 >( eprosima::fastcdr::Cdr::alignment( kTypeSize, 4 ) );
            max_serialized_type_size = aligned + 4U; /* encapsulation */
            is_compute_key_provided = false;
        }

        ~DdsPayloadPubSubType() noexcept override = default;

        // ---- serialize ----

        bool serialize(
            const void* const data,
            eprosima::fastdds::rtps::SerializedPayload_t& payload,
            eprosima::fastdds::dds::DataRepresentationId_t data_representation ) override
        {
            const auto* p = static_cast< const DdsPayload* >( data );

            eprosima::fastcdr::FastBuffer fb(
                reinterpret_cast< char* >( payload.data ), payload.max_size );
            eprosima::fastcdr::Cdr ser(
                fb, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN,
                data_representation ==
                    eprosima::fastdds::dds::DataRepresentationId_t::XCDR_DATA_REPRESENTATION
                    ? eprosima::fastcdr::CdrVersion::XCDRv1
                    : eprosima::fastcdr::CdrVersion::XCDRv2 );
            payload.encapsulation = ser.endianness() ==
                eprosima::fastcdr::Cdr::BIG_ENDIANNESS ? CDR_BE : CDR_LE;
            ser.set_encoding_flag(
                data_representation ==
                    eprosima::fastdds::dds::DataRepresentationId_t::XCDR_DATA_REPRESENTATION
                    ? eprosima::fastcdr::EncodingAlgorithmFlag::PLAIN_CDR
                    : eprosima::fastcdr::EncodingAlgorithmFlag::DELIMIT_CDR2 );

            try
            {
                ser.serialize_encapsulation();
                ser << *p;
                ser.set_dds_cdr_options( { 0, 0 } );
            }
            catch ( eprosima::fastcdr::exception::Exception& )
            {
                return false;
            }

            payload.length = static_cast< uint32_t >( ser.get_serialized_data_length() );
            return true;
        }

        // ---- deserialize ----

        bool deserialize(
            eprosima::fastdds::rtps::SerializedPayload_t& payload,
            void* data ) override
        {
            try
            {
                auto* p = static_cast< DdsPayload* >( data );
                eprosima::fastcdr::FastBuffer fb(
                    reinterpret_cast< char* >( payload.data ), payload.length );
                eprosima::fastcdr::Cdr deser(
                    fb, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN );
                deser.read_encapsulation();
                payload.encapsulation = deser.endianness() ==
                    eprosima::fastcdr::Cdr::BIG_ENDIANNESS ? CDR_BE : CDR_LE;
                deser >> *p;
            }
            catch ( eprosima::fastcdr::exception::Exception& )
            {
                return false;
            }
            return true;
        }

        // ---- calculate_serialized_size ----

        uint32_t calculate_serialized_size(
            const void* const data,
            eprosima::fastdds::dds::DataRepresentationId_t data_representation ) override
        {
            try
            {
                eprosima::fastcdr::CdrSizeCalculator calculator(
                    data_representation ==
                        eprosima::fastdds::dds::DataRepresentationId_t::XCDR_DATA_REPRESENTATION
                        ? eprosima::fastcdr::CdrVersion::XCDRv1
                        : eprosima::fastcdr::CdrVersion::XCDRv2 );
                size_t alignment { 0 };
                return static_cast< uint32_t >( calculator.calculate_serialized_size(
                    *static_cast< const DdsPayload* >( data ), alignment ) ) + 4U;
            }
            catch ( eprosima::fastcdr::exception::Exception& )
            {
                return 0;
            }
        }

        // ---- create / delete ----

        void* create_data() override
        {
            return reinterpret_cast< void* >( new DdsPayload() );
        }

        void delete_data( void* data ) override
        {
            delete reinterpret_cast< DdsPayload* >( data );
        }

        // ---- compute_key (no key) ----

        bool compute_key(
            eprosima::fastdds::rtps::SerializedPayload_t& /*payload*/,
            eprosima::fastdds::rtps::InstanceHandle_t& /*ihandle*/,
            bool /*force_md5*/ ) override
        {
            return false;
        }

        bool compute_key(
            const void* const /*data*/,
            eprosima::fastdds::rtps::InstanceHandle_t& /*ihandle*/,
            bool /*force_md5*/ ) override
        {
            return false;
        }

        // ---- register_type_object_representation (no-op for hand-written type) ----

        void register_type_object_representation() override
        {
            // No TypeObject registration — this is a built-in fallback type.
        }
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_DDS_CDDSPAYLOAD_HPP
