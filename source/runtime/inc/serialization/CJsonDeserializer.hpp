/**
 * @file        CJsonDeserializer.hpp
 * @author      Aii
 * @brief       JSON deserialization implementation (Strategy)
 * @date        2026/02/07
 * @details     JSON deserializer using nlohmann/json.
 *              Parses the input buffer as a JSON array and reads values
 *              sequentially.  Mirror of CJsonSerializer.
 *              Concrete Strategy for IDeserializer interface.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM_01103
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>New file — JSON deserializer
 * </table>
 */
#ifndef LAP_COM_CJSON_DESERIALIZER_HPP
#define LAP_COM_CJSON_DESERIALIZER_HPP

// ==================== Project-Internal Headers ====================
#include "IDeserializer.hpp"

// ==================== Third-Party Headers ====================
#include <nlohmann/json.hpp>

namespace lap
{
namespace com
{
namespace serialization
{
    /**
     * @brief JSON deserializer (nlohmann/json backend)
     *
     * @details Parses the input byte buffer as a JSON array (produced by
     *          CJsonSerializer) and reads elements sequentially.
     *          ByteOrder is accepted for interface conformance but has
     *          no semantic effect on JSON parsing.
     *
     * @note    SWS_CM_01103 — Concrete Strategy for JSON deserialization
     */
    class CJsonDeserializer final : public IDeserializer
    {
    public:
        explicit CJsonDeserializer(
            lap::core::Span< const lap::core::UInt8 > data,
            ByteOrder byteOrder = ByteOrder::kBigEndian ) noexcept
            : m_data( data )
            , m_byteOrder( byteOrder )
            , m_position( 0 )
            , m_valid( false )
        {
            try
            {
                const auto* begin =
                    reinterpret_cast< const char* > ( data.data() );
                m_json = nlohmann::json::parse( begin, begin + data.size() );
                m_valid = m_json.is_array();
            }
            catch ( ... )
            {
                m_json = nlohmann::json::array();
                m_valid = false;
            }
        }

        ~CJsonDeserializer() noexcept override = default;

        // Non-copyable (AUTOSAR C++ A12-8-6)
        CJsonDeserializer( const CJsonDeserializer& )            = delete;
        CJsonDeserializer& operator=( const CJsonDeserializer& ) = delete;

        // ================================================================
        // IDeserializer Interface
        // ================================================================

        SerializationFormat GetFormat() const noexcept override
        {
            return SerializationFormat::kJSON;
        }

        ByteOrder GetByteOrder() const noexcept override
        {
            return m_byteOrder;
        }

        // ---- Primitive types ----

        Result< void > Deserialize( Bool& value ) noexcept override
        {
            return JsonRead< Bool > ( value );
        }

        Result< void > Deserialize( lap::core::Int8& value ) noexcept override
        {
            int tmp = 0;
            auto r = JsonRead< int > ( tmp );
            value = static_cast< lap::core::Int8 > ( tmp );
            return r;
        }

        Result< void > Deserialize( lap::core::Int16& value ) noexcept override
        {
            return JsonRead< lap::core::Int16 > ( value );
        }

        Result< void > Deserialize( lap::core::Int32& value ) noexcept override
        {
            return JsonRead< lap::core::Int32 > ( value );
        }

        Result< void > Deserialize( lap::core::Int64& value ) noexcept override
        {
            return JsonRead< lap::core::Int64 > ( value );
        }

        Result< void > Deserialize( lap::core::UInt8& value ) noexcept override
        {
            unsigned tmp = 0;
            auto r = JsonRead< unsigned > ( tmp );
            value = static_cast< lap::core::UInt8 > ( tmp );
            return r;
        }

        Result< void > Deserialize( lap::core::UInt16& value ) noexcept override
        {
            return JsonRead< lap::core::UInt16 > ( value );
        }

        Result< void > Deserialize( lap::core::UInt32& value ) noexcept override
        {
            return JsonRead< lap::core::UInt32 > ( value );
        }

        Result< void > Deserialize( lap::core::UInt64& value ) noexcept override
        {
            return JsonRead< lap::core::UInt64 > ( value );
        }

        Result< void > Deserialize( Float& value ) noexcept override
        {
            return JsonRead< Float > ( value );
        }

        Result< void > Deserialize( Double& value ) noexcept override
        {
            return JsonRead< Double > ( value );
        }

        // ---- Complex types ----

        Result< void > Deserialize( lap::core::String& value ) noexcept override
        {
            return JsonRead< lap::core::String > ( value );
        }

        Result< void > DeserializeBytes(
            lap::core::Span< lap::core::UInt8 > data,
            lap::core::UInt32 length ) noexcept override
        {
            if ( !m_valid || m_position >= m_json.size() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }

            try
            {
                const auto& arr = m_json[m_position++];
                if ( !arr.is_array() || arr.size() < length ||
                     length > data.size() )
                {
                    return Result< void >::FromError(
                        MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
                }

                for ( lap::core::UInt32 i = 0; i < length; ++i )
                {
                    data.data()[i] =
                        static_cast< lap::core::UInt8 > ( arr[i].get< unsigned > () );
                }
                return Result< void >::FromValue();
            }
            catch ( ... )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }
        }

        // ---- Stream state ----

        Bool HasMoreData() const noexcept override
        {
            return m_valid && m_position < m_json.size();
        }

        void Reset() noexcept override
        {
            m_position = 0;
        }

    private:
        lap::core::Span< const lap::core::UInt8 > m_data;
        ByteOrder                               m_byteOrder;
        Size                                  m_position;
        Bool                                    m_valid;
        nlohmann::json                          m_json;

        /**
         * @brief Read next element from JSON array
         */
        template< typename T >
        Result< void > JsonRead( T& value ) noexcept
        {
            if ( !m_valid || m_position >= m_json.size() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kDeserializationError, 0 ) );
            }

            try
            {
                value = m_json[m_position++].get< T > ();
                return Result< void >::FromValue();
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

#endif // LAP_COM_CJSON_DESERIALIZER_HPP
