/**
 * @file        CBinarySerializer.hpp
 * @author      Aii
 * @brief       Binary serialization implementation (Strategy: custom binary format)
 * @date        2026/02/07
 * @details     Simple binary serializer with configurable byte order.
 *              Concrete Strategy for ISerializer interface.
 *              Split from Serialization.hpp following SRP.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM_01104
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>Split from Serialization.hpp (SRP refactoring)
 * </table>
 */
#ifndef LAP_COM_CBINARY_SERIALIZER_HPP
#define LAP_COM_CBINARY_SERIALIZER_HPP

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
     * @brief Simple binary serializer implementation
     * @note SWS_CM_01104 - Concrete Strategy for binary serialization
     */
    class CBinarySerializer final : public ISerializer
    {
    public:
        explicit CBinarySerializer(
            ByteOrder byteOrder = ByteOrder::kBigEndian ) noexcept
            : m_byteOrder( byteOrder )
        {
            m_buffer.reserve( 1024 );
        }

        ~CBinarySerializer() noexcept override = default;

        // ================================================================
        // ISerializer Interface
        // ================================================================

        SerializationFormat GetFormat() const noexcept override
        {
            return SerializationFormat::kCustom;
        }

        ByteOrder GetByteOrder() const noexcept override
        {
            return m_byteOrder;
        }

        Result< void > Serialize( Bool value ) noexcept override
        {
            m_buffer.push_back( value ? 1 : 0 );
            return Result< void >::FromValue();
        }

        Result< void > Serialize( lap::core::Int8 value ) noexcept override
        {
            m_buffer.push_back( static_cast< lap::core::UInt8 > ( value ) );
            return Result< void >::FromValue();
        }

        Result< void > Serialize( lap::core::Int16 value ) noexcept override
        {
            return SerializeInteger( static_cast< lap::core::UInt16 > ( value ) );
        }

        Result< void > Serialize( lap::core::Int32 value ) noexcept override
        {
            return SerializeInteger( static_cast< lap::core::UInt32 > ( value ) );
        }

        Result< void > Serialize( lap::core::Int64 value ) noexcept override
        {
            return SerializeInteger( static_cast< lap::core::UInt64 > ( value ) );
        }

        Result< void > Serialize( lap::core::UInt8 value ) noexcept override
        {
            m_buffer.push_back( value );
            return Result< void >::FromValue();
        }

        Result< void > Serialize( lap::core::UInt16 value ) noexcept override
        {
            return SerializeInteger( value );
        }

        Result< void > Serialize( lap::core::UInt32 value ) noexcept override
        {
            return SerializeInteger( value );
        }

        Result< void > Serialize( lap::core::UInt64 value ) noexcept override
        {
            return SerializeInteger( value );
        }

        Result< void > Serialize( Float value ) noexcept override
        {
            lap::core::UInt32 intValue;
            std::memcpy( &intValue, &value, sizeof( Float ) );
            return SerializeInteger( intValue );
        }

        Result< void > Serialize( Double value ) noexcept override
        {
            lap::core::UInt64 intValue;
            std::memcpy( &intValue, &value, sizeof( Double ) );
            return SerializeInteger( intValue );
        }

        Result< void > Serialize( const lap::core::String& value ) noexcept override
        {
            // Length-prefixed string
            auto lengthResult = Serialize(
                static_cast< lap::core::UInt32 > ( value.size() ) );
            if ( !lengthResult.HasValue() )
            {
                return lengthResult;
            }

            for ( Char c : value )
            {
                m_buffer.push_back( static_cast< lap::core::UInt8 > ( c ) );
            }

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

        lap::core::Span< const lap::core::UInt8 > GetData() const noexcept override
        {
            return lap::core::MakeSpan( m_buffer.data(), m_buffer.size() );
        }

        void Reset() noexcept override
        {
            m_buffer.clear();
        }

        // Non-copyable (AUTOSAR C++ A12-8-6)
        CBinarySerializer( const CBinarySerializer& )            = delete;
        CBinarySerializer& operator=( const CBinarySerializer& ) = delete;

    private:
        ByteOrder                          m_byteOrder;
        lap::core::Vector< lap::core::UInt8 > m_buffer;

        /**
         * @brief Generic integer serialization with byte-order support
         * @tparam T Integer type
         * @param value Value to serialize
         * @return Result indicating success
         */
        template< typename T >
        Result< void > SerializeInteger( T value ) noexcept
        {
            constexpr Size size = sizeof( T );

            if ( m_byteOrder == ByteOrder::kBigEndian )
            {
                for ( Size i = 0; i < size; ++i )
                {
                    m_buffer.push_back( static_cast< lap::core::UInt8 > (
                        ( value >> ( 8 * ( size - 1 - i ) ) ) & 0xFF ) );
                }
            }
            else
            {
                for ( Size i = 0; i < size; ++i )
                {
                    m_buffer.push_back( static_cast< lap::core::UInt8 > (
                        ( value >> ( 8 * i ) ) & 0xFF ) );
                }
            }

            return Result< void >::FromValue();
        }
    };

} // namespace serialization
} // namespace com
} // namespace lap

#endif // LAP_COM_CBINARY_SERIALIZER_HPP
