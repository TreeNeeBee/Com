/**
 * @file        CSerializerFactory.hpp
 * @author      Aii
 * @brief       Factory for creating serializer/deserializer instances
 * @date        2026/02/07
 * @details     Factory Method pattern: Creates concrete serializer/deserializer
 *              instances based on the requested SerializationFormat.
 *              Decouples client code from concrete serializer classes.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>New file — Factory Method pattern
 * </table>
 */
#ifndef LAP_COM_CSERIALIZER_FACTORY_HPP
#define LAP_COM_CSERIALIZER_FACTORY_HPP

// ==================== Project-Internal Headers ====================
#include "ISerializer.hpp"
#include "IDeserializer.hpp"
#include "CBinarySerializer.hpp"
#include "CBinaryDeserializer.hpp"
#include "CSomeIpSerializer.hpp"
#include "CSomeIpDeserializer.hpp"
#include "CCdrSerializer.hpp"
#include "CCdrDeserializer.hpp"
#include "CJsonSerializer.hpp"
#include "CJsonDeserializer.hpp"

// ==================== Standard Library Headers ====================
#include <memory>

namespace lap
{
namespace com
{
namespace serialization
{
    /**
     * @brief Factory for creating serializer/deserializer instances
     *
     * @details Implements the Factory Method pattern.
     *          Usage:
     *          @code
     *          auto serializer = CSerializerFactory::CreateSerializer(
     *              SerializationFormat::kCustom, ByteOrder::kBigEndian);
     *          serializer->Serialize(42);
     *          auto data = serializer->GetData();
     *
     *          auto deserializer = CSerializerFactory::CreateDeserializer(
     *              SerializationFormat::kCustom, data, ByteOrder::kBigEndian);
     *          int32_t value;
     *          deserializer->Deserialize(value);
     *          @endcode
     */
    class CSerializerFactory final
    {
    public:
        // Static-only class — no instantiation
        CSerializerFactory()                                   = delete;
        ~CSerializerFactory()                                  = delete;
        CSerializerFactory( const CSerializerFactory& )        = delete;
        CSerializerFactory& operator=( const CSerializerFactory& ) = delete;

        /**
         * @brief Create a serializer instance for the given format
         * @param format Serialization format to use
         * @param byteOrder Byte order for serialization
         * @return Unique pointer to serializer, or nullptr if format unsupported
         */
        static UniqueHandle< ISerializer > CreateSerializer(
            SerializationFormat format,
            ByteOrder byteOrder = ByteOrder::kBigEndian ) noexcept
        {
            switch ( format )
            {
                case SerializationFormat::kCustom:
                    return MakeUnique< CBinarySerializer > ( byteOrder );

                case SerializationFormat::kSomeIp:
                    return MakeUnique< CSomeIpSerializer > ( byteOrder );

                case SerializationFormat::kDDS:
                    return MakeUnique< CCdrSerializer > ( byteOrder );

                case SerializationFormat::kJSON:
                    return MakeUnique< CJsonSerializer > ( byteOrder );

                case SerializationFormat::kProtobuf:
                    // Protobuf requires libprotobuf (not available)
                    return nullptr;

                default:
                    return nullptr;
            }
        }

        /**
         * @brief Create a deserializer instance for the given format
         * @param format Serialization format to use
         * @param data   Input data buffer
         * @param byteOrder Byte order for deserialization
         * @return Unique pointer to deserializer, or nullptr if format unsupported
         */
        static UniqueHandle< IDeserializer > CreateDeserializer(
            SerializationFormat format,
            lap::core::Span< const lap::core::UInt8 > data,
            ByteOrder byteOrder = ByteOrder::kBigEndian ) noexcept
        {
            switch ( format )
            {
                case SerializationFormat::kCustom:
                    return MakeUnique< CBinaryDeserializer > ( data, byteOrder );

                case SerializationFormat::kSomeIp:
                    return MakeUnique< CSomeIpDeserializer > ( data, byteOrder );

                case SerializationFormat::kDDS:
                    return MakeUnique< CCdrDeserializer > ( data, byteOrder );

                case SerializationFormat::kJSON:
                    return MakeUnique< CJsonDeserializer > ( data, byteOrder );

                case SerializationFormat::kProtobuf:
                    // Protobuf requires libprotobuf (not available)
                    return nullptr;

                default:
                    return nullptr;
            }
        }
    };

} // namespace serialization
} // namespace com
} // namespace lap

#endif // LAP_COM_CSERIALIZER_FACTORY_HPP
