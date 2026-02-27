/**
 * @file        CCdrDeserializer.hpp
 * @author      Aii
 * @brief       DDS CDR deserialization implementation (Strategy)
 * @date        2026/02/07
 * @details     OMG CDR (Common Data Representation) deserializer using
 *              eProsima FastCDR library.  Follows XCDR2 encoding by default.
 *              Concrete Strategy for IDeserializer interface.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM_01103, OMG CDR
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>New file — CDR deserializer via FastCDR
 * </table>
 */
#ifndef LAP_COM_CCDR_DESERIALIZER_HPP
#define LAP_COM_CCDR_DESERIALIZER_HPP

// ==================== Project-Internal Headers ====================
#include "IDeserializer.hpp"

// ==================== Third-Party Headers ====================
#include <fastcdr/Cdr.h>
#include <fastcdr/FastBuffer.h>

// ==================== Standard Library Headers ====================
#include <cstring>

namespace lap
{
namespace com
{
namespace serialization
{
    /**
     * @brief DDS CDR (Common Data Representation) deserializer
     *
     * @details Delegates to eProsima FastCDR for standards-compliant
     *          CDR decoding.  The input Span is wrapped in a FastBuffer
     *          and read sequentially.
     *
     * @note    SWS_CM_01103 — Concrete Strategy for DDS CDR deserialization
     */
    class CCdrDeserializer final : public IDeserializer
    {
    public:
        explicit CCdrDeserializer(
            lap::core::Span< const lap::core::UInt8 > data,
            ByteOrder byteOrder = ByteOrder::kBigEndian ) noexcept
            : m_data( data )
            , m_byteOrder( byteOrder )
            , m_fastBuffer(
                  reinterpret_cast< char* > (
                      const_cast< lap::core::UInt8* > ( data.data() ) ),
                  data.size() )
            , m_cdr( m_fastBuffer,
                     ( byteOrder == ByteOrder::kBigEndian )
                         ? eprosima::fastcdr::Cdr::Endianness::BIG_ENDIANNESS
                         : eprosima::fastcdr::Cdr::Endianness::LITTLE_ENDIANNESS )
        {}

        ~CCdrDeserializer() noexcept override = default;

        // Non-copyable (AUTOSAR C++ A12-8-6)
        CCdrDeserializer( const CCdrDeserializer& )            = delete;
        CCdrDeserializer& operator=( const CCdrDeserializer& ) = delete;

        // ================================================================
        // IDeserializer Interface
        // ================================================================

        SerializationFormat GetFormat() const noexcept override
        {
            return SerializationFormat::kDDS;
        }

        ByteOrder GetByteOrder() const noexcept override
        {
            return m_byteOrder;
        }

        // ---- Primitive types ----

        Result< void > Deserialize( Bool& value ) noexcept override
        {
            return CdrRead( [&]{ m_cdr >> value; } );
        }

        Result< void > Deserialize( lap::core::Int8& value ) noexcept override
        {
            return CdrRead( [&]{ m_cdr >> value; } );
        }

        Result< void > Deserialize( lap::core::Int16& value ) noexcept override
        {
            return CdrRead( [&]{ m_cdr >> value; } );
        }

        Result< void > Deserialize( lap::core::Int32& value ) noexcept override
        {
            return CdrRead( [&]{ m_cdr >> value; } );
        }

        Result< void > Deserialize( lap::core::Int64& value ) noexcept override
        {
            return CdrRead( [&]{ m_cdr >> value; } );
        }

        Result< void > Deserialize( lap::core::UInt8& value ) noexcept override
        {
            return CdrRead( [&]{ m_cdr >> value; } );
        }

        Result< void > Deserialize( lap::core::UInt16& value ) noexcept override
        {
            return CdrRead( [&]{ m_cdr >> value; } );
        }

        Result< void > Deserialize( lap::core::UInt32& value ) noexcept override
        {
            return CdrRead( [&]{ m_cdr >> value; } );
        }

        Result< void > Deserialize( lap::core::UInt64& value ) noexcept override
        {
            return CdrRead( [&]{ m_cdr >> value; } );
        }

        Result< void > Deserialize( Float& value ) noexcept override
        {
            return CdrRead( [&]{ m_cdr >> value; } );
        }

        Result< void > Deserialize( Double& value ) noexcept override
        {
            return CdrRead( [&]{ m_cdr >> value; } );
        }

        // ---- Complex types ----

        Result< void > Deserialize( lap::core::String& value ) noexcept override
        {
            return CdrRead( [&]{ m_cdr >> value; } );
        }

        Result< void > DeserializeBytes(
            lap::core::Span< lap::core::UInt8 > data,
            lap::core::UInt32 length ) noexcept override
        {
            if ( length > data.size() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }
            return CdrRead( [&]{
                m_cdr.deserialize_array(
                    data.data(),
                    static_cast< Size > ( length ) );
            } );
        }

        // ---- Stream state ----

        Bool HasMoreData() const noexcept override
        {
            return m_cdr.get_serialized_data_length() < m_data.size();
        }

        void Reset() noexcept override
        {
            m_cdr.reset();
        }

    private:
        lap::core::Span< const lap::core::UInt8 > m_data;
        ByteOrder                               m_byteOrder;
        eprosima::fastcdr::FastBuffer           m_fastBuffer;
        eprosima::fastcdr::Cdr                  m_cdr;

        /**
         * @brief Wrap a CDR read operation with exception safety
         */
        template< typename Func >
        Result< void > CdrRead( Func&& fn ) noexcept
        {
            try
            {
                fn();
                return Result< void >::FromValue();
            }
            catch ( const eprosima::fastcdr::exception::Exception& )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }
            catch ( ... )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }
        }
    };

} // namespace serialization
} // namespace com
} // namespace lap

#endif // LAP_COM_CCDR_DESERIALIZER_HPP
