/**
 * @file        CJsonSerializer.hpp
 * @author      Aii
 * @brief       JSON serialization implementation (Strategy)
 * @date        2026/02/07
 * @details     JSON serializer using nlohmann/json.
 *              Serializes values into a JSON array in order, then dumps
 *              the UTF-8 string to a byte buffer.  Call GetData() after
 *              all Serialize() calls to obtain the final wire representation.
 *              Concrete Strategy for ISerializer interface.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM_01102
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>New file — JSON serializer
 * </table>
 */
#ifndef LAP_COM_CJSON_SERIALIZER_HPP
#define LAP_COM_CJSON_SERIALIZER_HPP

// ==================== Project-Internal Headers ====================
#include "ISerializer.hpp"

// ==================== Third-Party Headers ====================
#include <nlohmann/json.hpp>

namespace lap
{
namespace com
{
namespace serialization
{
    /**
     * @brief JSON serializer (nlohmann/json backend)
     *
     * @details Values are appended to an ordered JSON array.
     *          GetData() dumps the array to a compact UTF-8 string.
     *          ByteOrder is accepted for interface conformance but has
     *          no semantic effect on JSON output.
     *
     * @note    SWS_CM_01102 — Concrete Strategy for JSON serialization
     */
    class CJsonSerializer final : public ISerializer
    {
    public:
        explicit CJsonSerializer(
            ByteOrder byteOrder = ByteOrder::kBigEndian ) noexcept
            : m_byteOrder( byteOrder )
            , m_json( nlohmann::json::array() )
        {}

        ~CJsonSerializer() noexcept override = default;

        // Non-copyable (AUTOSAR C++ A12-8-6)
        CJsonSerializer( const CJsonSerializer& )            = delete;
        CJsonSerializer& operator=( const CJsonSerializer& ) = delete;

        // ================================================================
        // ISerializer Interface
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

        Result< void > Serialize( Bool value ) noexcept override
        {
            return JsonAppend( value );
        }

        Result< void > Serialize( lap::core::Int8 value ) noexcept override
        {
            return JsonAppend( static_cast< int > ( value ) );
        }

        Result< void > Serialize( lap::core::Int16 value ) noexcept override
        {
            return JsonAppend( value );
        }

        Result< void > Serialize( lap::core::Int32 value ) noexcept override
        {
            return JsonAppend( value );
        }

        Result< void > Serialize( lap::core::Int64 value ) noexcept override
        {
            return JsonAppend( value );
        }

        Result< void > Serialize( lap::core::UInt8 value ) noexcept override
        {
            return JsonAppend( static_cast< unsigned > ( value ) );
        }

        Result< void > Serialize( lap::core::UInt16 value ) noexcept override
        {
            return JsonAppend( value );
        }

        Result< void > Serialize( lap::core::UInt32 value ) noexcept override
        {
            return JsonAppend( value );
        }

        Result< void > Serialize( lap::core::UInt64 value ) noexcept override
        {
            return JsonAppend( value );
        }

        Result< void > Serialize( Float value ) noexcept override
        {
            return JsonAppend( value );
        }

        Result< void > Serialize( Double value ) noexcept override
        {
            return JsonAppend( value );
        }

        // ---- Complex types ----

        Result< void > Serialize( const lap::core::String& value ) noexcept override
        {
            return JsonAppend( value );
        }

        Result< void > SerializeBytes(
            lap::core::Span< const lap::core::UInt8 > data ) noexcept override
        {
            try
            {
                // Encode raw bytes as a JSON array of unsigned integers
                auto arr = nlohmann::json::array();
                for ( Size i = 0; i < data.size(); ++i )
                {
                    arr.push_back( static_cast< unsigned > ( data.data()[i] ) );
                }
                m_json.push_back( ::std::move( arr ) );
                m_dirty = true;
                return Result< void >::FromValue();
            }
            catch ( ... )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kSerializationError, 0 ) );
            }
        }

        // ---- Buffer management ----

        lap::core::Span< const lap::core::UInt8 > GetData() const noexcept override
        {
            if ( m_dirty )
            {
                m_dumpCache = m_json.dump();
                m_dirty = false;
            }
            return lap::core::MakeSpan(
                reinterpret_cast< const lap::core::UInt8* > ( m_dumpCache.data() ),
                m_dumpCache.size() );
        }

        void Reset() noexcept override
        {
            m_json = nlohmann::json::array();
            m_dumpCache.clear();
            m_dirty = false;
        }

    private:
        ByteOrder        m_byteOrder;
        nlohmann::json   m_json;

        /// Cached dump() output — regenerated lazily on GetData()
        mutable String          m_dumpCache;
        mutable Bool           m_dirty = false;

        template< typename T >
        Result< void > JsonAppend( T&& value ) noexcept
        {
            try
            {
                m_json.push_back( ::std::forward< T > ( value ) );
                m_dirty = true;
                return Result< void >::FromValue();
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

#endif // LAP_COM_CJSON_SERIALIZER_HPP
