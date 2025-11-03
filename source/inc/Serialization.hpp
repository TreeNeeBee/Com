/**
 * @file        Serialization.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR Adaptive Platform Serialization Framework
 * @date        2025-10-30
 * @details     Data serialization/deserialization for communication (SWS_CM Section 10.3)
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 SWS_CM compliant
 * @version     1.0
 */
#ifndef LAP_COM_SERIALIZATION_HPP
#define LAP_COM_SERIALIZATION_HPP

#include "ComTypes.hpp"
#include <core/CResult.hpp>
#include <core/CTypedef.hpp>
#include <core/CString.hpp>
#include <core/CSpan.hpp>

#include <type_traits>
#include <cstring>

namespace lap
{
namespace com
{
namespace serialization
{
    /**
     * @brief Serialization format enumeration
     * @note SWS_CM_01100
     */
    enum class SerializationFormat : lap::core::UInt8
    {
        kSomeIp        = 0,    ///< SOME/IP serialization
        kDDS           = 1,    ///< DDS CDR serialization
        kJSON          = 2,    ///< JSON serialization
        kProtobuf      = 3,    ///< Protocol Buffers serialization
        kCustom        = 255   ///< Custom serialization
    };
    
    /**
     * @brief Byte order enumeration
     * @note SWS_CM_01101
     */
    enum class ByteOrder : lap::core::UInt8
    {
        kBigEndian     = 0,    ///< Big-endian (network byte order)
        kLittleEndian  = 1     ///< Little-endian
    };
    
    /**
     * @brief Serializer interface
     * @note SWS_CM_01102 - Abstract interface for data serialization
     */
    class Serializer
    {
    public:
        virtual ~Serializer() = default;
        
        /**
         * @brief Get serialization format
         * @return Serialization format
         */
        virtual SerializationFormat GetFormat() const noexcept = 0;
        
        /**
         * @brief Get byte order
         * @return Byte order
         */
        virtual ByteOrder GetByteOrder() const noexcept = 0;
        
        /**
         * @brief Serialize boolean value
         * @param value Value to serialize
         * @return Result indicating success or error
         */
        virtual Result<void> Serialize(bool value) noexcept = 0;
        
        /**
         * @brief Serialize 8-bit integer
         * @param value Value to serialize
         * @return Result indicating success or error
         */
        virtual Result<void> Serialize(lap::core::Int8 value) noexcept = 0;
        
        /**
         * @brief Serialize 16-bit integer
         * @param value Value to serialize
         * @return Result indicating success or error
         */
        virtual Result<void> Serialize(lap::core::Int16 value) noexcept = 0;
        
        /**
         * @brief Serialize 32-bit integer
         * @param value Value to serialize
         * @return Result indicating success or error
         */
        virtual Result<void> Serialize(lap::core::Int32 value) noexcept = 0;
        
        /**
         * @brief Serialize 64-bit integer
         * @param value Value to serialize
         * @return Result indicating success or error
         */
        virtual Result<void> Serialize(lap::core::Int64 value) noexcept = 0;
        
        /**
         * @brief Serialize unsigned 8-bit integer
         * @param value Value to serialize
         * @return Result indicating success or error
         */
        virtual Result<void> Serialize(lap::core::UInt8 value) noexcept = 0;
        
        /**
         * @brief Serialize unsigned 16-bit integer
         * @param value Value to serialize
         * @return Result indicating success or error
         */
        virtual Result<void> Serialize(lap::core::UInt16 value) noexcept = 0;
        
        /**
         * @brief Serialize unsigned 32-bit integer
         * @param value Value to serialize
         * @return Result indicating success or error
         */
        virtual Result<void> Serialize(lap::core::UInt32 value) noexcept = 0;
        
        /**
         * @brief Serialize unsigned 64-bit integer
         * @param value Value to serialize
         * @return Result indicating success or error
         */
        virtual Result<void> Serialize(lap::core::UInt64 value) noexcept = 0;
        
        /**
         * @brief Serialize 32-bit float
         * @param value Value to serialize
         * @return Result indicating success or error
         */
        virtual Result<void> Serialize(float value) noexcept = 0;
        
        /**
         * @brief Serialize 64-bit double
         * @param value Value to serialize
         * @return Result indicating success or error
         */
        virtual Result<void> Serialize(double value) noexcept = 0;
        
        /**
         * @brief Serialize string
         * @param value Value to serialize
         * @return Result indicating success or error
         */
        virtual Result<void> Serialize(const lap::core::String& value) noexcept = 0;
        
        /**
         * @brief Serialize byte array
         * @param data Data to serialize
         * @return Result indicating success or error
         */
        virtual Result<void> SerializeBytes(lap::core::Span<const lap::core::UInt8> data) noexcept = 0;
        
        /**
         * @brief Get serialized data
         * @return Span of serialized bytes
         */
        virtual lap::core::Span<const lap::core::UInt8> GetData() const noexcept = 0;
        
        /**
         * @brief Reset serializer state
         */
        virtual void Reset() noexcept = 0;
    };
    
    /**
     * @brief Deserializer interface
     * @note SWS_CM_01103 - Abstract interface for data deserialization
     */
    class Deserializer
    {
    public:
        virtual ~Deserializer() = default;
        
        /**
         * @brief Get serialization format
         * @return Serialization format
         */
        virtual SerializationFormat GetFormat() const noexcept = 0;
        
        /**
         * @brief Get byte order
         * @return Byte order
         */
        virtual ByteOrder GetByteOrder() const noexcept = 0;
        
        /**
         * @brief Deserialize boolean value
         * @param value Output value
         * @return Result indicating success or error
         */
        virtual Result<void> Deserialize(bool& value) noexcept = 0;
        
        /**
         * @brief Deserialize 8-bit integer
         * @param value Output value
         * @return Result indicating success or error
         */
        virtual Result<void> Deserialize(lap::core::Int8& value) noexcept = 0;
        
        /**
         * @brief Deserialize 16-bit integer
         * @param value Output value
         * @return Result indicating success or error
         */
        virtual Result<void> Deserialize(lap::core::Int16& value) noexcept = 0;
        
        /**
         * @brief Deserialize 32-bit integer
         * @param value Output value
         * @return Result indicating success or error
         */
        virtual Result<void> Deserialize(lap::core::Int32& value) noexcept = 0;
        
        /**
         * @brief Deserialize 64-bit integer
         * @param value Output value
         * @return Result indicating success or error
         */
        virtual Result<void> Deserialize(lap::core::Int64& value) noexcept = 0;
        
        /**
         * @brief Deserialize unsigned 8-bit integer
         * @param value Output value
         * @return Result indicating success or error
         */
        virtual Result<void> Deserialize(lap::core::UInt8& value) noexcept = 0;
        
        /**
         * @brief Deserialize unsigned 16-bit integer
         * @param value Output value
         * @return Result indicating success or error
         */
        virtual Result<void> Deserialize(lap::core::UInt16& value) noexcept = 0;
        
        /**
         * @brief Deserialize unsigned 32-bit integer
         * @param value Output value
         * @return Result indicating success or error
         */
        virtual Result<void> Deserialize(lap::core::UInt32& value) noexcept = 0;
        
        /**
         * @brief Deserialize unsigned 64-bit integer
         * @param value Output value
         * @return Result indicating success or error
         */
        virtual Result<void> Deserialize(lap::core::UInt64& value) noexcept = 0;
        
        /**
         * @brief Deserialize 32-bit float
         * @param value Output value
         * @return Result indicating success or error
         */
        virtual Result<void> Deserialize(float& value) noexcept = 0;
        
        /**
         * @brief Deserialize 64-bit double
         * @param value Output value
         * @return Result indicating success or error
         */
        virtual Result<void> Deserialize(double& value) noexcept = 0;
        
        /**
         * @brief Deserialize string
         * @param value Output value
         * @return Result indicating success or error
         */
        virtual Result<void> Deserialize(lap::core::String& value) noexcept = 0;
        
        /**
         * @brief Deserialize byte array
         * @param data Output buffer
         * @param length Number of bytes to read
         * @return Result indicating success or error
         */
        virtual Result<void> DeserializeBytes(lap::core::Span<lap::core::UInt8> data, 
                                             lap::core::UInt32 length) noexcept = 0;
        
        /**
         * @brief Check if more data is available
         * @return true if data remains, false otherwise
         */
        virtual bool HasMoreData() const noexcept = 0;
        
        /**
         * @brief Reset deserializer to beginning
         */
        virtual void Reset() noexcept = 0;
    };
    
    /**
     * @brief Simple binary serializer implementation
     * @note SWS_CM_01104 - Basic binary serialization
     */
    class BinarySerializer : public Serializer
    {
    public:
        explicit BinarySerializer(ByteOrder byteOrder = ByteOrder::kBigEndian) noexcept
            : m_byteOrder(byteOrder)
        {
            m_buffer.reserve(1024);
        }
        
        SerializationFormat GetFormat() const noexcept override
        {
            return SerializationFormat::kCustom;
        }
        
        ByteOrder GetByteOrder() const noexcept override
        {
            return m_byteOrder;
        }
        
        Result<void> Serialize(bool value) noexcept override
        {
            m_buffer.push_back(value ? 1 : 0);
            return Result<void>::FromValue();
        }
        
        Result<void> Serialize(lap::core::Int8 value) noexcept override
        {
            m_buffer.push_back(static_cast<lap::core::UInt8>(value));
            return Result<void>::FromValue();
        }
        
        Result<void> Serialize(lap::core::Int16 value) noexcept override
        {
            return SerializeInteger(static_cast<lap::core::UInt16>(value));
        }
        
        Result<void> Serialize(lap::core::Int32 value) noexcept override
        {
            return SerializeInteger(static_cast<lap::core::UInt32>(value));
        }
        
        Result<void> Serialize(lap::core::Int64 value) noexcept override
        {
            return SerializeInteger(static_cast<lap::core::UInt64>(value));
        }
        
        Result<void> Serialize(lap::core::UInt8 value) noexcept override
        {
            m_buffer.push_back(value);
            return Result<void>::FromValue();
        }
        
        Result<void> Serialize(lap::core::UInt16 value) noexcept override
        {
            return SerializeInteger(value);
        }
        
        Result<void> Serialize(lap::core::UInt32 value) noexcept override
        {
            return SerializeInteger(value);
        }
        
        Result<void> Serialize(lap::core::UInt64 value) noexcept override
        {
            return SerializeInteger(value);
        }
        
        Result<void> Serialize(float value) noexcept override
        {
            lap::core::UInt32 intValue;
            std::memcpy(&intValue, &value, sizeof(float));
            return SerializeInteger(intValue);
        }
        
        Result<void> Serialize(double value) noexcept override
        {
            lap::core::UInt64 intValue;
            std::memcpy(&intValue, &value, sizeof(double));
            return SerializeInteger(intValue);
        }
        
        Result<void> Serialize(const lap::core::String& value) noexcept override
        {
            // Serialize length first
            auto lengthResult = Serialize(static_cast<lap::core::UInt32>(value.size()));
            if (!lengthResult.HasValue())
            {
                return lengthResult;
            }
            
            // Then serialize string data
            for (char c : value)
            {
                m_buffer.push_back(static_cast<lap::core::UInt8>(c));
            }
            
            return Result<void>::FromValue();
        }
        
        Result<void> SerializeBytes(lap::core::Span<const lap::core::UInt8> data) noexcept override
        {
            for (size_t i = 0; i < data.size(); ++i)
            {
                m_buffer.push_back(data.data()[i]);
            }
            return Result<void>::FromValue();
        }
        
        lap::core::Span<const lap::core::UInt8> GetData() const noexcept override
        {
            return lap::core::MakeSpan(m_buffer.data(), m_buffer.size());
        }
        
        void Reset() noexcept override
        {
            m_buffer.clear();
        }
        
    private:
        ByteOrder m_byteOrder;
        lap::core::Vector<lap::core::UInt8> m_buffer;
        
        template<typename T>
        Result<void> SerializeInteger(T value) noexcept
        {
            constexpr size_t size = sizeof(T);
            
            if (m_byteOrder == ByteOrder::kBigEndian)
            {
                // Big-endian: MSB first
                for (size_t i = 0; i < size; ++i)
                {
                    m_buffer.push_back(static_cast<lap::core::UInt8>(
                        (value >> (8 * (size - 1 - i))) & 0xFF));
                }
            }
            else
            {
                // Little-endian: LSB first
                for (size_t i = 0; i < size; ++i)
                {
                    m_buffer.push_back(static_cast<lap::core::UInt8>(
                        (value >> (8 * i)) & 0xFF));
                }
            }
            
            return Result<void>::FromValue();
        }
    };
    
    /**
     * @brief Simple binary deserializer implementation
     * @note SWS_CM_01105 - Basic binary deserialization
     */
    class BinaryDeserializer : public Deserializer
    {
    public:
        explicit BinaryDeserializer(lap::core::Span<const lap::core::UInt8> data,
                                   ByteOrder byteOrder = ByteOrder::kBigEndian) noexcept
            : m_data(data)
            , m_byteOrder(byteOrder)
            , m_position(0)
        {}
        
        SerializationFormat GetFormat() const noexcept override
        {
            return SerializationFormat::kCustom;
        }
        
        ByteOrder GetByteOrder() const noexcept override
        {
            return m_byteOrder;
        }
        
        Result<void> Deserialize(bool& value) noexcept override
        {
            if (m_position >= m_data.size())
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            value = (m_data.data()[m_position++] != 0);
            return Result<void>::FromValue();
        }
        
        Result<void> Deserialize(lap::core::Int8& value) noexcept override
        {
            if (m_position >= m_data.size())
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            value = static_cast<lap::core::Int8>(m_data.data()[m_position++]);
            return Result<void>::FromValue();
        }
        
        Result<void> Deserialize(lap::core::Int16& value) noexcept override
        {
            lap::core::UInt16 uintValue;
            auto result = DeserializeInteger(uintValue);
            value = static_cast<lap::core::Int16>(uintValue);
            return result;
        }
        
        Result<void> Deserialize(lap::core::Int32& value) noexcept override
        {
            lap::core::UInt32 uintValue;
            auto result = DeserializeInteger(uintValue);
            value = static_cast<lap::core::Int32>(uintValue);
            return result;
        }
        
        Result<void> Deserialize(lap::core::Int64& value) noexcept override
        {
            lap::core::UInt64 uintValue;
            auto result = DeserializeInteger(uintValue);
            value = static_cast<lap::core::Int64>(uintValue);
            return result;
        }
        
        Result<void> Deserialize(lap::core::UInt8& value) noexcept override
        {
            if (m_position >= m_data.size())
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            value = m_data.data()[m_position++];
            return Result<void>::FromValue();
        }
        
        Result<void> Deserialize(lap::core::UInt16& value) noexcept override
        {
            return DeserializeInteger(value);
        }
        
        Result<void> Deserialize(lap::core::UInt32& value) noexcept override
        {
            return DeserializeInteger(value);
        }
        
        Result<void> Deserialize(lap::core::UInt64& value) noexcept override
        {
            return DeserializeInteger(value);
        }
        
        Result<void> Deserialize(float& value) noexcept override
        {
            lap::core::UInt32 intValue;
            auto result = DeserializeInteger(intValue);
            if (result.HasValue())
            {
                std::memcpy(&value, &intValue, sizeof(float));
            }
            return result;
        }
        
        Result<void> Deserialize(double& value) noexcept override
        {
            lap::core::UInt64 intValue;
            auto result = DeserializeInteger(intValue);
            if (result.HasValue())
            {
                std::memcpy(&value, &intValue, sizeof(double));
            }
            return result;
        }
        
        Result<void> Deserialize(lap::core::String& value) noexcept override
        {
            // Deserialize length first
            lap::core::UInt32 length;
            auto lengthResult = Deserialize(length);
            if (!lengthResult.HasValue())
            {
                return lengthResult;
            }
            
            if (m_position + length > m_data.size())
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            // Deserialize string data
            value.clear();
            value.reserve(length);
            for (lap::core::UInt32 i = 0; i < length; ++i)
            {
                value.push_back(static_cast<char>(m_data.data()[m_position++]));
            }
            
            return Result<void>::FromValue();
        }
        
        Result<void> DeserializeBytes(lap::core::Span<lap::core::UInt8> data, 
                                     lap::core::UInt32 length) noexcept override
        {
            if (m_position + length > m_data.size() || length > data.size())
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            for (lap::core::UInt32 i = 0; i < length; ++i)
            {
                data.data()[i] = m_data.data()[m_position++];
            }
            
            return Result<void>::FromValue();
        }
        
        bool HasMoreData() const noexcept override
        {
            return m_position < m_data.size();
        }
        
        void Reset() noexcept override
        {
            m_position = 0;
        }
        
    private:
        lap::core::Span<const lap::core::UInt8> m_data;
        ByteOrder m_byteOrder;
        size_t m_position;
        
        template<typename T>
        Result<void> DeserializeInteger(T& value) noexcept
        {
            constexpr size_t size = sizeof(T);
            
            if (m_position + size > m_data.size())
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            value = 0;
            
            if (m_byteOrder == ByteOrder::kBigEndian)
            {
                // Big-endian: MSB first
                for (size_t i = 0; i < size; ++i)
                {
                    value |= static_cast<T>(m_data.data()[m_position++]) << (8 * (size - 1 - i));
                }
            }
            else
            {
                // Little-endian: LSB first
                for (size_t i = 0; i < size; ++i)
                {
                    value |= static_cast<T>(m_data.data()[m_position++]) << (8 * i);
                }
            }
            
            return Result<void>::FromValue();
        }
    };
    
} // namespace serialization
} // namespace com
} // namespace lap

#endif // LAP_COM_SERIALIZATION_HPP
