/**
 * @file        CBinaryDeserializer.hpp
 * @author      Aii
 * @brief       Binary deserialization implementation (Strategy: custom binary format)
 * @date        2026/02/07
 * @details     Simple binary deserializer with configurable byte order.
 *              Concrete Strategy for IDeserializer interface.
 *              Split from Serialization.hpp following SRP.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM_01105
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>Split from Serialization.hpp (SRP refactoring)
 * </table>
 */
#ifndef LAP_COM_CBINARY_DESERIALIZER_HPP
#define LAP_COM_CBINARY_DESERIALIZER_HPP

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
     * @brief Simple binary deserializer implementation
     * @note SWS_CM_01105 - Concrete Strategy for binary deserialization
     */
    class CBinaryDeserializer final : public IDeserializer
    {
    public:
        explicit CBinaryDeserializer(
            lap::core::Span< const lap::core::UInt8 > data,
            ByteOrder byteOrder = ByteOrder::kBigEndian ) noexcept
            : m_data( data )
            , m_byteOrder( byteOrder )
            , m_position( 0 )
        {}

        ~CBinaryDeserializer() noexcept override = default;

        // ================================================================
        // IDeserializer Interface
        // ================================================================

        SerializationFormat GetFormat() const noexcept override
        {
            return SerializationFormat::kCustom;
        }

        ByteOrder GetByteOrder() const noexcept override
        {
            return m_byteOrder;
        }

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
            lap::core::UInt16 uintValue;
            auto result = DeserializeInteger( uintValue );
            value = static_cast< lap::core::Int16 > ( uintValue );
            return result;
        }

        Result< void > Deserialize( lap::core::Int32& value ) noexcept override
        {
            lap::core::UInt32 uintValue;
            auto result = DeserializeInteger( uintValue );
            value = static_cast< lap::core::Int32 > ( uintValue );
            return result;
        }

        Result< void > Deserialize( lap::core::Int64& value ) noexcept override
        {
            lap::core::UInt64 uintValue;
            auto result = DeserializeInteger( uintValue );
            value = static_cast< lap::core::Int64 > ( uintValue );
            return result;
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
            return DeserializeInteger( value );
        }

        Result< void > Deserialize( lap::core::UInt32& value ) noexcept override
        {
            return DeserializeInteger( value );
        }

        Result< void > Deserialize( lap::core::UInt64& value ) noexcept override
        {
            return DeserializeInteger( value );
        }

        Result< void > Deserialize( Float& value ) noexcept override
        {
            lap::core::UInt32 intValue;
            auto result = DeserializeInteger( intValue );
            if ( result.HasValue() )
            {
                std::memcpy( &value, &intValue, sizeof( Float ) );
            }
            return result;
        }

        Result< void > Deserialize( Double& value ) noexcept override
        {
            lap::core::UInt64 intValue;
            auto result = DeserializeInteger( intValue );
            if ( result.HasValue() )
            {
                std::memcpy( &value, &intValue, sizeof( Double ) );
            }
            return result;
        }

        Result< void > Deserialize( lap::core::String& value ) noexcept override
        {
            lap::core::UInt32 length;
            auto lengthResult = Deserialize( length );
            if ( !lengthResult.HasValue() )
            {
                return lengthResult;
            }

            if ( m_position + length > m_data.size() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }

            value.clear();
            value.reserve( length );
            for ( lap::core::UInt32 i = 0; i < length; ++i )
            {
                value.push_back(
                    static_cast< Char > ( m_data.data()[m_position++] ) );
            }

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

        Bool HasMoreData() const noexcept override
        {
            return m_position < m_data.size();
        }

        void Reset() noexcept override
        {
            m_position = 0;
        }

        // Non-copyable (AUTOSAR C++ A12-8-6)
        CBinaryDeserializer( const CBinaryDeserializer& )            = delete;
        CBinaryDeserializer& operator=( const CBinaryDeserializer& ) = delete;

    private:
        lap::core::Span< const lap::core::UInt8 > m_data;
        ByteOrder                               m_byteOrder;
        Size                                  m_position;

        /**
         * @brief Generic integer deserialization with byte-order support
         * @tparam T Integer type
         * @param value Output value
         * @return Result indicating success
         */
        template< typename T >
        Result< void > DeserializeInteger( T& value ) noexcept
        {
            constexpr Size size = sizeof( T );

            if ( m_position + size > m_data.size() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }

            value = 0;

            if ( m_byteOrder == ByteOrder::kBigEndian )
            {
                for ( Size i = 0; i < size; ++i )
                {
                    value |= static_cast< T > ( m_data.data()[m_position++] )
                             << ( 8 * ( size - 1 - i ) );
                }
            }
            else
            {
                for ( Size i = 0; i < size; ++i )
                {
                    value |= static_cast< T > ( m_data.data()[m_position++] )
                             << ( 8 * i );
                }
            }

            return Result< void >::FromValue();
        }
    };

} // namespace serialization
} // namespace com
} // namespace lap

#endif // LAP_COM_CBINARY_DESERIALIZER_HPP
