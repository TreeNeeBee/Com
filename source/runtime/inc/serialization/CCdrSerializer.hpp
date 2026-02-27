/**
 * @file        CCdrSerializer.hpp
 * @author      Aii
 * @brief       DDS CDR serialization implementation (Strategy)
 * @date        2026/02/07
 * @details     OMG CDR (Common Data Representation) serializer using
 *              eProsima FastCDR library.  Follows XCDR2 encoding by default.
 *              Concrete Strategy for ISerializer interface.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM_01102, OMG CDR
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>New file — CDR serializer via FastCDR
 * </table>
 */
#ifndef LAP_COM_CCDR_SERIALIZER_HPP
#define LAP_COM_CCDR_SERIALIZER_HPP

// ==================== Project-Internal Headers ====================
#include "ISerializer.hpp"

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
     * @brief DDS CDR (Common Data Representation) serializer
     *
     * @details Delegates to eProsima FastCDR for standards-compliant
     *          CDR encoding.  Uses a resizable internal buffer.
     *          The byteOrder parameter maps to CDR endianness:
     *          - BigEndian    → BIG_ENDIANNESS
     *          - LittleEndian → LITTLE_ENDIANNESS
     *
     * @note    SWS_CM_01102 — Concrete Strategy for DDS CDR serialization
     */
    class CCdrSerializer final : public ISerializer
    {
    public:
        explicit CCdrSerializer(
            ByteOrder byteOrder = ByteOrder::kBigEndian ) noexcept
            : m_byteOrder( byteOrder )
            , m_rawBuffer( kInitialCapacity )
            , m_fastBuffer( m_rawBuffer.data(), m_rawBuffer.size() )
            , m_cdr( m_fastBuffer,
                     ( byteOrder == ByteOrder::kBigEndian )
                         ? eprosima::fastcdr::Cdr::Endianness::BIG_ENDIANNESS
                         : eprosima::fastcdr::Cdr::Endianness::LITTLE_ENDIANNESS )
        {}

        ~CCdrSerializer() noexcept override = default;

        // Non-copyable (AUTOSAR C++ A12-8-6)
        CCdrSerializer( const CCdrSerializer& )            = delete;
        CCdrSerializer& operator=( const CCdrSerializer& ) = delete;

        // ================================================================
        // ISerializer Interface
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

        Result< void > Serialize( Bool value ) noexcept override
        {
            return CdrWrite( [&]{ m_cdr << value; } );
        }

        Result< void > Serialize( lap::core::Int8 value ) noexcept override
        {
            return CdrWrite( [&]{ m_cdr << value; } );
        }

        Result< void > Serialize( lap::core::Int16 value ) noexcept override
        {
            return CdrWrite( [&]{ m_cdr << value; } );
        }

        Result< void > Serialize( lap::core::Int32 value ) noexcept override
        {
            return CdrWrite( [&]{ m_cdr << value; } );
        }

        Result< void > Serialize( lap::core::Int64 value ) noexcept override
        {
            return CdrWrite( [&]{ m_cdr << value; } );
        }

        Result< void > Serialize( lap::core::UInt8 value ) noexcept override
        {
            return CdrWrite( [&]{ m_cdr << value; } );
        }

        Result< void > Serialize( lap::core::UInt16 value ) noexcept override
        {
            return CdrWrite( [&]{ m_cdr << value; } );
        }

        Result< void > Serialize( lap::core::UInt32 value ) noexcept override
        {
            return CdrWrite( [&]{ m_cdr << value; } );
        }

        Result< void > Serialize( lap::core::UInt64 value ) noexcept override
        {
            return CdrWrite( [&]{ m_cdr << value; } );
        }

        Result< void > Serialize( Float value ) noexcept override
        {
            return CdrWrite( [&]{ m_cdr << value; } );
        }

        Result< void > Serialize( Double value ) noexcept override
        {
            return CdrWrite( [&]{ m_cdr << value; } );
        }

        // ---- Complex types ----

        Result< void > Serialize( const lap::core::String& value ) noexcept override
        {
            return CdrWrite( [&]{ m_cdr << value; } );
        }

        Result< void > SerializeBytes(
            lap::core::Span< const lap::core::UInt8 > data ) noexcept override
        {
            return CdrWrite( [&]{
                m_cdr.serialize_array(
                    data.data(),
                    static_cast< Size > ( data.size() ) );
            } );
        }

        // ---- Buffer management ----

        lap::core::Span< const lap::core::UInt8 > GetData() const noexcept override
        {
            const auto* begin =
                reinterpret_cast< const lap::core::UInt8* > (
                    m_fastBuffer.getBuffer() );
            const auto length = m_cdr.get_serialized_data_length();
            return lap::core::MakeSpan( begin, length );
        }

        void Reset() noexcept override
        {
            m_cdr.reset();
        }

    private:
        static constexpr Size kInitialCapacity = 4096;

        ByteOrder                              m_byteOrder;
        lap::core::Vector< char >                m_rawBuffer;
        eprosima::fastcdr::FastBuffer          m_fastBuffer;
        eprosima::fastcdr::Cdr                 m_cdr;

        /**
         * @brief Wrap a CDR write operation with exception safety
         */
        template< typename Func >
        Result< void > CdrWrite( Func&& fn ) noexcept
        {
            try
            {
                fn();
                return Result< void >::FromValue();
            }
            catch ( const eprosima::fastcdr::exception::Exception& )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kSerializationError, 0 ) );
            }
            catch ( ... )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kSerializationError, 0 ) );
            }
        }
    };

} // namespace serialization
} // namespace com
} // namespace lap

#endif // LAP_COM_CCDR_SERIALIZER_HPP
