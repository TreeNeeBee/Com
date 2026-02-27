/**
 * @file        CSomeIpSerializer.hpp
 * @author      Aii
 * @brief       SOME/IP serialization implementation (Strategy)
 * @date        2026/02/07
 * @details     SOME/IP wire format serializer:
 *              - Big-endian by default (network byte order)
 *              - Strings: BOM(0xEFBBBF) + UTF-8 + NUL + 32-bit length prefix
 *              - Length includes BOM + content + NUL
 *              Concrete Strategy for ISerializer interface.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 TPS_SOME/IP, SWS_CM_01102
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>New file — SOME/IP serializer
 * </table>
 */
#ifndef LAP_COM_CSOMEIP_SERIALIZER_HPP
#define LAP_COM_CSOMEIP_SERIALIZER_HPP

// ==================== Project-Internal Headers ====================
#include "ISerializer.hpp"

// ==================== Standard Library Headers ====================
#include <cstring>

namespace lap
{
namespace com
{
namespace serialization
{
    /**
     * @brief SOME/IP wire format serializer
     *
     * @details Follows AUTOSAR TPS_SOME/IP serialization rules:
     *          - Integer primitives: big-endian (configurable)
     *          - Float/double: IEEE 754, big-endian (configurable)
     *          - Strings: 32-bit length prefix (big-endian) + BOM + UTF-8 + NUL
     *          - Arrays/Structs: straightforward field concatenation
     *
     * @note    SWS_CM_01102 — Concrete Strategy for SOME/IP serialization
     */
    class CSomeIpSerializer final : public ISerializer
    {
    public:
        explicit CSomeIpSerializer(
            ByteOrder byteOrder = ByteOrder::kBigEndian ) noexcept
            : m_byteOrder( byteOrder )
        {
            m_buffer.reserve( 1024 );
        }

        ~CSomeIpSerializer() noexcept override = default;

        // Non-copyable (AUTOSAR C++ A12-8-6)
        CSomeIpSerializer( const CSomeIpSerializer& )            = delete;
        CSomeIpSerializer& operator=( const CSomeIpSerializer& ) = delete;

        // ================================================================
        // ISerializer Interface
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

        Result< void > Serialize( Bool value ) noexcept override
        {
            m_buffer.push_back( value ? 1U : 0U );
            return Result< void >::FromValue();
        }

        Result< void > Serialize( lap::core::Int8 value ) noexcept override
        {
            m_buffer.push_back( static_cast< lap::core::UInt8 > ( value ) );
            return Result< void >::FromValue();
        }

        Result< void > Serialize( lap::core::Int16 value ) noexcept override
        {
            return WriteInteger( static_cast< lap::core::UInt16 > ( value ) );
        }

        Result< void > Serialize( lap::core::Int32 value ) noexcept override
        {
            return WriteInteger( static_cast< lap::core::UInt32 > ( value ) );
        }

        Result< void > Serialize( lap::core::Int64 value ) noexcept override
        {
            return WriteInteger( static_cast< lap::core::UInt64 > ( value ) );
        }

        Result< void > Serialize( lap::core::UInt8 value ) noexcept override
        {
            m_buffer.push_back( value );
            return Result< void >::FromValue();
        }

        Result< void > Serialize( lap::core::UInt16 value ) noexcept override
        {
            return WriteInteger( value );
        }

        Result< void > Serialize( lap::core::UInt32 value ) noexcept override
        {
            return WriteInteger( value );
        }

        Result< void > Serialize( lap::core::UInt64 value ) noexcept override
        {
            return WriteInteger( value );
        }

        Result< void > Serialize( Float value ) noexcept override
        {
            lap::core::UInt32 bits;
            ::std::memcpy( &bits, &value, sizeof( Float ) );
            return WriteInteger( bits );
        }

        Result< void > Serialize( Double value ) noexcept override
        {
            lap::core::UInt64 bits;
            ::std::memcpy( &bits, &value, sizeof( Double ) );
            return WriteInteger( bits );
        }

        // ---- Complex types ----

        /**
         * @brief Serialize string in SOME/IP format
         * @details Format: [length:32] [BOM:3] [UTF-8 data] [NUL:1]
         *          length = BOM(3) + data_size + NUL(1)
         */
        Result< void > Serialize( const lap::core::String& value ) noexcept override
        {
            // SOME/IP string wire format:
            // 4-byte length prefix (includes BOM + content + NUL)
            // 3-byte UTF-8 BOM: 0xEF 0xBB 0xBF
            // UTF-8 string content
            // 1-byte NUL terminator
            static constexpr lap::core::UInt32 kBomSize = 3U;
            static constexpr lap::core::UInt32 kNulSize = 1U;

            const auto contentLen =
                static_cast< lap::core::UInt32 > ( value.size() );
            const lap::core::UInt32 totalLen = kBomSize + contentLen + kNulSize;

            // Length prefix
            auto result = WriteInteger( totalLen );
            if ( !result.HasValue() )
            {
                return result;
            }

            // UTF-8 BOM
            m_buffer.push_back( 0xEFU );
            m_buffer.push_back( 0xBBU );
            m_buffer.push_back( 0xBFU );

            // String content
            for ( Char c : value )
            {
                m_buffer.push_back( static_cast< lap::core::UInt8 > ( c ) );
            }

            // NUL terminator
            m_buffer.push_back( 0x00U );

            return Result< void >::FromValue();
        }

        Result< void > SerializeBytes(
            lap::core::Span< const lap::core::UInt8 > data ) noexcept override
        {
            for ( Size i = 0; i < data.size(); ++i )
            {
                m_buffer.push_back( data.data()[i] );
            }
            return Result< void >::FromValue();
        }

        // ---- Buffer management ----

        lap::core::Span< const lap::core::UInt8 > GetData() const noexcept override
        {
            return lap::core::MakeSpan( m_buffer.data(), m_buffer.size() );
        }

        void Reset() noexcept override
        {
            m_buffer.clear();
        }

    private:
        ByteOrder                              m_byteOrder;
        lap::core::Vector< lap::core::UInt8 >    m_buffer;

        /**
         * @brief Write integer in configured byte order
         * @tparam T  Unsigned integer type
         */
        template< typename T >
        Result< void > WriteInteger( T value ) noexcept
        {
            constexpr Size kSize = sizeof( T );

            if ( m_byteOrder == ByteOrder::kBigEndian )
            {
                for ( Size i = 0; i < kSize; ++i )
                {
                    m_buffer.push_back( static_cast< lap::core::UInt8 > (
                        ( value >> ( 8U * ( kSize - 1U - i ) ) ) & 0xFFU ) );
                }
            }
            else
            {
                for ( Size i = 0; i < kSize; ++i )
                {
                    m_buffer.push_back( static_cast< lap::core::UInt8 > (
                        ( value >> ( 8U * i ) ) & 0xFFU ) );
                }
            }

            return Result< void >::FromValue();
        }
    };

} // namespace serialization
} // namespace com
} // namespace lap

#endif // LAP_COM_CSOMEIP_SERIALIZER_HPP
