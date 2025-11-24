/**
 * @file        ProtobufSerializer.hpp
 * @author      LightAP Team
 * @brief       Protobuf Serialization for Socket Transport
 * @date        2025-10-30
 * @details     Protobuf message serializer/deserializer with Length-Delimited framing
 * @copyright   Copyright (c) 2025
 * @version     1.0
 */

#ifndef LAP_COM_BINDING_SOCKET_PROTOBUF_SERIALIZER_HPP
#define LAP_COM_BINDING_SOCKET_PROTOBUF_SERIALIZER_HPP

#include <Serialization.hpp>
#include <vector>
#include <memory>
#include <cstring>
#include <arpa/inet.h>  // For htonl/ntohl

// Forward declaration - 应用代码会包含生成的protobuf头文件
namespace google {
namespace protobuf {
    class MessageLite;
}
}

namespace lap {
namespace com {
namespace binding {
namespace socket {

/**
 * @brief Protobuf消息序列化器
 * 
 * @details
 * 使用Length-Delimited格式:
 * - 4字节长度前缀 (网络字节序)
 * - Protobuf序列化后的消息内容
 * 
 * 特性:
 * - 类型安全 (模板化)
 * - 支持任意Protobuf消息类型
 * - 自动处理字节序
 * - 线程安全 (无共享状态)
 * 
 * @tparam MessageType Protobuf生成的消息类型 (必须继承自google::protobuf::MessageLite)
 * 
 * @usage
 * // 假设有MyRequest.proto定义
 * MyRequest request;
 * request.set_id(123);
 * request.set_name("test");
 * 
 * ProtobufSerializer<MyRequest> serializer;
 * auto result = serializer.SerializeMessage(request);
 * if (result.HasValue()) {
 *     auto data = serializer.GetData();
 *     // 发送 data
 * }
 */
template<typename MessageType>
class ProtobufSerializer : public serialization::Serializer {
public:
    ProtobufSerializer() = default;

    serialization::SerializationFormat GetFormat() const noexcept override {
        return serialization::SerializationFormat::kProtobuf;
    }

    serialization::ByteOrder GetByteOrder() const noexcept override {
        // Protobuf使用小端序(wire format), 但长度前缀使用网络字节序(大端)
        return serialization::ByteOrder::kBigEndian;
    }

    /**
     * @brief 序列化Protobuf消息
     * @param message Protobuf消息对象
     * @return Result<void> 成功或错误
     */
    Result<void> SerializeMessage(const MessageType& message) noexcept {
        try {
            // 计算消息大小
            size_t messageSize = message.ByteSizeLong();
            
            if (messageSize > UINT32_MAX) {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kMessageTooLarge, 0));
            }
            
            // 预留空间: 4字节长度 + 消息内容
            m_buffer.clear();
            m_buffer.resize(4 + messageSize);
            
            // 写入长度前缀 (网络字节序 - 大端)
            uint32_t networkSize = htonl(static_cast<uint32_t>(messageSize));
            std::memcpy(m_buffer.data(), &networkSize, 4);
            
            // 序列化消息到缓冲区
            if (!message.SerializeToArray(m_buffer.data() + 4, messageSize)) {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kSerializationError, 0));
            }
            
            return Result<void>::FromValue();
            
        } catch (const std::exception& e) {
            // 捕获任何Protobuf异常
            return Result<void>::FromError(
                MakeErrorCode(ComErrc::kSerializationError, 0));
        }
    }

    lap::core::Span<const lap::core::UInt8> GetData() const noexcept override {
        return lap::core::MakeSpan(m_buffer.data(), m_buffer.size());
    }

    void Reset() noexcept override {
        m_buffer.clear();
    }

    // 未使用的基类方法 (Protobuf不需要单独序列化基本类型)
    Result<void> Serialize(bool) noexcept override { return NotSupported(); }
    Result<void> Serialize(lap::core::Int8) noexcept override { return NotSupported(); }
    Result<void> Serialize(lap::core::Int16) noexcept override { return NotSupported(); }
    Result<void> Serialize(lap::core::Int32) noexcept override { return NotSupported(); }
    Result<void> Serialize(lap::core::Int64) noexcept override { return NotSupported(); }
    Result<void> Serialize(lap::core::UInt8) noexcept override { return NotSupported(); }
    Result<void> Serialize(lap::core::UInt16) noexcept override { return NotSupported(); }
    Result<void> Serialize(lap::core::UInt32) noexcept override { return NotSupported(); }
    Result<void> Serialize(lap::core::UInt64) noexcept override { return NotSupported(); }
    Result<void> Serialize(float) noexcept override { return NotSupported(); }
    Result<void> Serialize(double) noexcept override { return NotSupported(); }
    Result<void> Serialize(const lap::core::String&) noexcept override { return NotSupported(); }
    Result<void> SerializeBytes(lap::core::Span<const lap::core::UInt8>) noexcept override { 
        return NotSupported(); 
    }

private:
    Result<void> NotSupported() const noexcept {
        return Result<void>::FromError(
            MakeErrorCode(ComErrc::kNotSupported, 0));
    }

    lap::core::Vector<lap::core::UInt8> m_buffer;
};

/**
 * @brief Protobuf消息反序列化器
 * 
 * @details
 * 从Length-Delimited格式反序列化Protobuf消息
 * 
 * @tparam MessageType Protobuf生成的消息类型
 * 
 * @usage
 * lap::core::Vector<lap::core::UInt8> data = receiveData();
 * ProtobufDeserializer<MyRequest> deserializer(
 *     lap::core::MakeSpan(data.data(), data.size()));
 * 
 * MyRequest request;
 * auto result = deserializer.DeserializeMessage(request);
 * if (result.HasValue()) {
 *     // 使用 request
 * }
 */
template<typename MessageType>
class ProtobufDeserializer : public serialization::Deserializer {
public:
    explicit ProtobufDeserializer(lap::core::Span<const lap::core::UInt8> data)
        : m_data(data), m_position(0) {}
    
    // Support non-const Span as well
    explicit ProtobufDeserializer(lap::core::Span<lap::core::UInt8> data)
        : m_data(lap::core::Span<const lap::core::UInt8>(data.data(), data.size())), m_position(0) {}

    serialization::SerializationFormat GetFormat() const noexcept override {
        return serialization::SerializationFormat::kProtobuf;
    }

    serialization::ByteOrder GetByteOrder() const noexcept override {
        return serialization::ByteOrder::kBigEndian;
    }

    /**
     * @brief 反序列化Protobuf消息
     * @param message 输出消息对象
     * @return Result<void> 成功或错误
     */
    Result<void> DeserializeMessage(MessageType& message) noexcept {
        try {
            // 读取长度前缀 (至少需要4字节)
            if (m_data.size() - m_position < 4) {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            uint32_t networkSize;
            std::memcpy(&networkSize, m_data.data() + m_position, 4);
            uint32_t messageSize = ntohl(networkSize);
            
            // 检查数据完整性
            if (m_data.size() - m_position < 4 + messageSize) {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            // 反序列化消息
            if (!message.ParseFromArray(m_data.data() + m_position + 4, messageSize)) {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kDeserializationError, 0));
            }
            
            m_position += 4 + messageSize;
            return Result<void>::FromValue();
            
        } catch (const std::exception& e) {
            return Result<void>::FromError(
                MakeErrorCode(ComErrc::kDeserializationError, 0));
        }
    }

    bool HasMoreData() const noexcept override {
        return m_position < m_data.size();
    }

    void Reset() noexcept override {
        m_position = 0;
    }

    // 未使用的基类方法
    Result<void> Deserialize(bool&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(lap::core::Int8&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(lap::core::Int16&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(lap::core::Int32&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(lap::core::Int64&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(lap::core::UInt8&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(lap::core::UInt16&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(lap::core::UInt32&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(lap::core::UInt64&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(float&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(double&) noexcept override { return NotSupported(); }
    Result<void> Deserialize(lap::core::String&) noexcept override { return NotSupported(); }
    Result<void> DeserializeBytes(lap::core::Span<lap::core::UInt8>, lap::core::UInt32) 
        noexcept override { return NotSupported(); }

private:
    Result<void> NotSupported() const noexcept {
        return Result<void>::FromError(
            MakeErrorCode(ComErrc::kNotSupported, 0));
    }

    lap::core::Span<const lap::core::UInt8> m_data;
    size_t m_position;
};

} // namespace socket
} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_BINDING_SOCKET_PROTOBUF_SERIALIZER_HPP
