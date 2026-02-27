/**
 * @file        CSomeIpDeserializer.hpp
 * @author      Aii
 * @brief       SOME/IP deserialization implementation (Strategy)
 * @date        2026/02/07
 * @details     SOME/IP wire format deserializer:
 *              - Big-endian by default (network byte order)
 *              - Strings: 32-bit length prefix + BOM(0xEFBBBF) + UTF-8 + NUL
 *              Concrete Strategy for IDeserializer interface.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 TPS_SOME/IP, SWS_CM_01103
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>New file — SOME/IP deserializer
 * </table>
 */
#ifndef LAP_COM_CSOMEIP_DESERIALIZER_HPP
#define LAP_COM_CSOMEIP_DESERIALIZER_HPP

// ==================== Project-Internal Headers ====================
#include "IDeserializer.hpp"

// ==================== Standard Library Headers ====================
#include <cstring>

namespace lap
{
namespace com
{
namespace serialization
{
    /**
     * @brief SOME/IP wire format deserializer
     *
     * @details Follows AUTOSAR TPS_SOME/IP deserialization rules.
     *          Mirror of CSomeIpSerializer.
     *
     * @note    SWS_CM_01103 — Concrete Strategy for SOME/IP deserialization
     */
    class CSomeIpDeserializer final : public IDeserializer
    {
    public:
        explicit CSomeIpDeserializer(
            lap::core::Span< const lap::core::UInt8 > data,
            ByteOrder byteOrder = ByteOrder::kBigEndian ) noexcept
            : m_data( data )
            , m_byteOrder( byteOrder )
            , m_position( 0 )
        {}

        ~CSomeIpDeserializer() noexcept override = default;

        // Non-copyable (AUTOSAR C++ A12-8-6)
        CSomeIpDeserializer( const CSomeIpDeserializer& )            = delete;
        CSomeIpDeserializer& operator=( const CSomeIpDeserializer& ) = delete;

        // ================================================================
        // IDeserializer Interface
        // ================================================================

        SerializationFormat GetFormat() const noexcept override
        {
            return SerializationFormat::kSomeIp;
        }

        ByteOrder GetByteOrder() const noexcept override
        {
            return m_byteOrder;
        }

        // ---- Primitive types ----

        Result< void > Deserialize( Bool& value ) noexcept override
        {
            if ( m_position >= m_data.size() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }
            value = ( m_data.data()[m_position++] != 0 );
            return Result< void >::FromValue();
        }

        Result< void > Deserialize( lap::core::Int8& value ) noexcept override
        {
            if ( m_position >= m_data.size() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }
            value = static_cast< lap::core::Int8 > ( m_data.data()[m_position++] );
            return Result< void >::FromValue();
        }

        Result< void > Deserialize( lap::core::Int16& value ) noexcept override
        {
            lap::core::UInt16 uval;
            auto r = ReadInteger( uval );
            value = static_cast< lap::core::Int16 > ( uval );
            return r;
        }

        Result< void > Deserialize( lap::core::Int32& value ) noexcept override
        {
            lap::core::UInt32 uval;
            auto r = ReadInteger( uval );
            value = static_cast< lap::core::Int32 > ( uval );
            return r;
        }

        Result< void > Deserialize( lap::core::Int64& value ) noexcept override
        {
            lap::core::UInt64 uval;
            auto r = ReadInteger( uval );
            value = static_cast< lap::core::Int64 > ( uval );
            return r;
        }

        Result< void > Deserialize( lap::core::UInt8& value ) noexcept override
        {
            if ( m_position >= m_data.size() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }
            value = m_data.data()[m_position++];
            return Result< void >::FromValue();
        }

        Result< void > Deserialize( lap::core::UInt16& value ) noexcept override
        {
            return ReadInteger( value );
        }

        Result< void > Deserialize( lap::core::UInt32& value ) noexcept override
        {
            return ReadInteger( value );
        }

        Result< void > Deserialize( lap::core::UInt64& value ) noexcept override
        {
            return ReadInteger( value );
        }

        Result< void > Deserialize( Float& value ) noexcept override
        {
            lap::core::UInt32 bits;
            auto r = ReadInteger( bits );
            if ( r.HasValue() )
            {
                ::std::memcpy( &value, &bits, sizeof( Float ) );
            }
            return r;
        }

        Result< void > Deserialize( Double& value ) noexcept override
        {
            lap::core::UInt64 bits;
            auto r = ReadInteger( bits );
            if ( r.HasValue() )
            {
                ::std::memcpy( &value, &bits, sizeof( Double ) );
            }
            return r;
        }

        // ---- Complex types ----

        /**
         * @brief Deserialize string from SOME/IP format
         * @details Format: [length:32] [BOM:3] [UTF-8 data] [NUL:1]
         */
        Result< void > Deserialize( lap::core::String& value ) noexcept override
        {
            // Read 32-bit length prefix
            lap::core::UInt32 totalLen = 0;
            auto lenResult = ReadInteger( totalLen );
            if ( !lenResult.HasValue() )
            {
                return lenResult;
            }

            if ( m_position + totalLen > m_data.size() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }

            // Minimum: BOM(3) + NUL(1) = 4
            static constexpr lap::core::UInt32 kMinLen = 4U;
            if ( totalLen < kMinLen )
            {
                // Empty string or malformed — accept gracefully
                value.clear();
                m_position += totalLen;
                return Result< void >::FromValue();
            }

            // Skip BOM (3 bytes: 0xEF 0xBB 0xBF)
            m_position += 3U;

            // Content length = totalLen - BOM(3) - NUL(1)
            const lap::core::UInt32 contentLen = totalLen - kMinLen;
            value.clear();
            value.reserve( contentLen );
            for ( lap::core::UInt32 i = 0; i < contentLen; ++i )
            {
                value.push_back(
                    static_cast< Char > ( m_data.data()[m_position++] ) );
            }

            // Skip NUL terminator
            m_position += 1U;

            return Result< void >::FromValue();
        }

        Result< void > DeserializeBytes(
            lap::core::Span< lap::core::UInt8 > data,
            lap::core::UInt32 length ) noexcept override
        {
            if ( ( m_position + length > m_data.size() ) ||
                 ( length > data.size() ) )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }
            for ( lap::core::UInt32 i = 0; i < length; ++i )
            {
                data.data()[i] = m_data.data()[m_position++];
            }
            return Result< void >::FromValue();
        }

        // ---- Stream state ----

        Bool HasMoreData() const noexcept override
        {
            return m_position < m_data.size();
        }

        void Reset() noexcept override
        {
            m_position = 0;
        }

    private:
        lap::core::Span< const lap::core::UInt8 > m_data;
        ByteOrder                               m_byteOrder;
        Size                                  m_position;

        template< typename T >
        Result< void > ReadInteger( T& value ) noexcept
        {
            constexpr Size kSize = sizeof( T );

            if ( m_position + kSize > m_data.size() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }

            value = 0;
            if ( m_byteOrder == ByteOrder::kBigEndian )
            {
                for ( Size i = 0; i < kSize; ++i )
                {
                    value |= static_cast< T > ( m_data.data()[m_position++] )
                             << ( 8U * ( kSize - 1U - i ) );
                }
            }
            else
            {
                for ( Size i = 0; i < kSize; ++i )
                {
                    value |= static_cast< T > ( m_data.data()[m_position++] )
                             << ( 8U * i );
                }
            }
            return Result< void >::FromValue();
        }
    };

} // namespace serialization
} // namespace com
} // namespace lap

#endif // LAP_COM_CSOMEIP_DESERIALIZER_HPP
