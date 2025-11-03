/**
 * @file        com_socket_serializer_test.cpp
 * @brief       Unit tests for ProtobufSerializer and ProtobufDeserializer
 * @author      LightAP Team
 * @date        2025-10-30
 */

#include <binding/socket/ProtobufSerializer.hpp>
#include <gtest/gtest.h>

// Test protobuf messages (need to generate from calculator.proto first)
// For this test we'll use a simple inline protobuf message
#include <google/protobuf/message_lite.h>

// Simple test message for demonstration
namespace lap {
namespace com {
namespace test {

// Mock protobuf message for testing
class TestMessage : public google::protobuf::MessageLite {
public:
    TestMessage() : m_value(0) {}
    
    void set_value(int32_t value) { m_value = value; }
    int32_t value() const { return m_value; }
    
    // MessageLite interface implementation
    std::string GetTypeName() const override { return "TestMessage"; }
    
    MessageLite* New(google::protobuf::Arena* arena) const override {
        return new TestMessage();
    }
    
    void Clear() override { m_value = 0; }
    
    bool IsInitialized() const override { return true; }
    
    size_t ByteSizeLong() const override { 
        return sizeof(m_value); 
    }
    
    bool SerializeToArray(void* data, int size) const override {
        if (size < static_cast<int>(sizeof(m_value))) return false;
        std::memcpy(data, &m_value, sizeof(m_value));
        return true;
    }
    
    bool ParseFromArray(const void* data, int size) override {
        if (size < static_cast<int>(sizeof(m_value))) return false;
        std::memcpy(&m_value, data, sizeof(m_value));
        return true;
    }
    
    int GetCachedSize() const override { 
        return static_cast<int>(ByteSizeLong()); 
    }
    
    uint8_t* _InternalSerialize(
        uint8_t* target,
        google::protobuf::io::EpsCopyOutputStream* stream) const override {
        std::memcpy(target, &m_value, sizeof(m_value));
        return target + sizeof(m_value);
    }

private:
    int32_t m_value;
};

} // namespace test
} // namespace com
} // namespace lap

using namespace lap::com::binding::socket;
using namespace lap::com::test;

/**
 * @brief Test fixture for ProtobufSerializer
 */
class ProtobufSerializerTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_message = std::make_shared<TestMessage>();
    }

    std::shared_ptr<TestMessage> m_message;
};

/**
 * @brief Test basic serialization
 */
TEST_F(ProtobufSerializerTest, BasicSerialization) {
    m_message->set_value(12345);
    
    ProtobufSerializer<TestMessage> serializer;
    auto result = serializer.SerializeMessage(*m_message);
    
    ASSERT_TRUE(result.HasValue());
    
    const auto& data = result.Value();
    
    // Check Length-Delimited format: 4-byte length + payload
    EXPECT_GE(data.size(), 4u);
    
    // Extract length (big-endian)
    uint32_t length = (static_cast<uint32_t>(static_cast<uint8_t>(data[0])) << 24) |
                     (static_cast<uint32_t>(static_cast<uint8_t>(data[1])) << 16) |
                     (static_cast<uint32_t>(static_cast<uint8_t>(data[2])) << 8) |
                     static_cast<uint32_t>(static_cast<uint8_t>(data[3]));
    
    EXPECT_EQ(length, m_message->ByteSizeLong());
    EXPECT_EQ(data.size(), 4u + length);
}

/**
 * @brief Test basic deserialization
 */
TEST_F(ProtobufSerializerTest, BasicDeserialization) {
    // Serialize first
    m_message->set_value(67890);
    
    ProtobufSerializer<TestMessage> serializer;
    auto serResult = serializer.SerializeMessage(*m_message);
    ASSERT_TRUE(serResult.HasValue());
    
    // Deserialize
    ProtobufDeserializer<TestMessage> deserializer;
    auto deserResult = deserializer.DeserializeMessage(serResult.Value());
    
    ASSERT_TRUE(deserResult.HasValue());
    EXPECT_EQ(deserResult.Value()->value(), 67890);
}

/**
 * @brief Test roundtrip serialization/deserialization
 */
TEST_F(ProtobufSerializerTest, RoundtripSerialization) {
    const int32_t testValues[] = {0, 1, -1, 42, -42, 2147483647, -2147483648};
    
    ProtobufSerializer<TestMessage> serializer;
    ProtobufDeserializer<TestMessage> deserializer;
    
    for (int32_t testValue : testValues) {
        m_message->set_value(testValue);
        
        // Serialize
        auto serResult = serializer.SerializeMessage(*m_message);
        ASSERT_TRUE(serResult.HasValue()) << "Failed to serialize value: " << testValue;
        
        // Deserialize
        auto deserResult = deserializer.DeserializeMessage(serResult.Value());
        ASSERT_TRUE(deserResult.HasValue()) << "Failed to deserialize value: " << testValue;
        
        // Verify
        EXPECT_EQ(deserResult.Value()->value(), testValue);
    }
}

/**
 * @brief Test deserialization with invalid data
 */
TEST_F(ProtobufSerializerTest, DeserializeInvalidData) {
    ProtobufDeserializer<TestMessage> deserializer;
    
    // Too short (less than 4 bytes)
    {
        std::vector<char> data = {0x00, 0x01, 0x02};
        auto result = deserializer.DeserializeMessage(data);
        EXPECT_FALSE(result.HasValue());
    }
    
    // Length mismatch (length says 100 bytes but only 4 bytes provided)
    {
        std::vector<char> data = {0x00, 0x00, 0x00, 0x64}; // length = 100
        auto result = deserializer.DeserializeMessage(data);
        EXPECT_FALSE(result.HasValue());
    }
    
    // Corrupted protobuf payload
    {
        std::vector<char> data = {
            0x00, 0x00, 0x00, 0x04,  // length = 4
            0xFF, 0xFF, 0xFF, 0xFF   // invalid protobuf data
        };
        auto result = deserializer.DeserializeMessage(data);
        EXPECT_FALSE(result.HasValue());
    }
}

/**
 * @brief Test empty message serialization
 */
TEST_F(ProtobufSerializerTest, EmptyMessage) {
    m_message->Clear();
    
    ProtobufSerializer<TestMessage> serializer;
    auto serResult = serializer.SerializeMessage(*m_message);
    ASSERT_TRUE(serResult.HasValue());
    
    ProtobufDeserializer<TestMessage> deserializer;
    auto deserResult = deserializer.DeserializeMessage(serResult.Value());
    ASSERT_TRUE(deserResult.HasValue());
    
    EXPECT_EQ(deserResult.Value()->value(), 0);
}

/**
 * @brief Test Length-Delimited framing format
 */
TEST_F(ProtobufSerializerTest, LengthDelimitedFormat) {
    m_message->set_value(999);
    
    ProtobufSerializer<TestMessage> serializer;
    auto result = serializer.SerializeMessage(*m_message);
    ASSERT_TRUE(result.HasValue());
    
    const auto& data = result.Value();
    
    // Verify format: [4-byte length][payload]
    ASSERT_GE(data.size(), 4u);
    
    // Extract length (network byte order - big-endian)
    uint32_t length = (static_cast<uint32_t>(static_cast<uint8_t>(data[0])) << 24) |
                     (static_cast<uint32_t>(static_cast<uint8_t>(data[1])) << 16) |
                     (static_cast<uint32_t>(static_cast<uint8_t>(data[2])) << 8) |
                     static_cast<uint32_t>(static_cast<uint8_t>(data[3]));
    
    // Verify length matches payload size
    EXPECT_EQ(data.size(), 4u + length);
    
    // Verify we can deserialize using this format
    ProtobufDeserializer<TestMessage> deserializer;
    auto deserResult = deserializer.DeserializeMessage(data);
    ASSERT_TRUE(deserResult.HasValue());
    EXPECT_EQ(deserResult.Value()->value(), 999);
}

/**
 * @brief Test network byte order (big-endian)
 */
TEST_F(ProtobufSerializerTest, NetworkByteOrder) {
    m_message->set_value(12345);
    
    ProtobufSerializer<TestMessage> serializer;
    auto result = serializer.SerializeMessage(*m_message);
    ASSERT_TRUE(result.HasValue());
    
    const auto& data = result.Value();
    ASSERT_GE(data.size(), 4u);
    
    // Manually extract length in big-endian format
    uint32_t length = (static_cast<uint32_t>(static_cast<uint8_t>(data[0])) << 24) |
                     (static_cast<uint32_t>(static_cast<uint8_t>(data[1])) << 16) |
                     (static_cast<uint32_t>(static_cast<uint8_t>(data[2])) << 8) |
                     static_cast<uint32_t>(static_cast<uint8_t>(data[3]));
    
    // Verify length matches message size
    EXPECT_EQ(length, m_message->ByteSizeLong());
    
    // Verify most significant byte is first (big-endian)
    if (length > 255) {
        EXPECT_NE(data[0], 0);
    }
}

/**
 * @brief Test multiple messages in sequence
 */
TEST_F(ProtobufSerializerTest, MultipleMessages) {
    ProtobufSerializer<TestMessage> serializer;
    ProtobufDeserializer<TestMessage> deserializer;
    
    std::vector<std::vector<char>> serializedMessages;
    
    // Serialize multiple messages
    for (int i = 0; i < 10; ++i) {
        m_message->set_value(i * 100);
        auto result = serializer.SerializeMessage(*m_message);
        ASSERT_TRUE(result.HasValue());
        serializedMessages.push_back(result.Value());
    }
    
    // Deserialize and verify
    for (size_t i = 0; i < serializedMessages.size(); ++i) {
        auto result = deserializer.DeserializeMessage(serializedMessages[i]);
        ASSERT_TRUE(result.HasValue());
        EXPECT_EQ(result.Value()->value(), static_cast<int32_t>(i * 100));
    }
}

/**
 * @brief Test error handling for oversized messages
 */
TEST_F(ProtobufSerializerTest, OversizedMessage) {
    // Create a length-delimited message with impossibly large length
    std::vector<char> data = {
        static_cast<char>(0xFF), static_cast<char>(0xFF), 
        static_cast<char>(0xFF), static_cast<char>(0xFF)  // length = 4294967295 (max uint32)
    };
    
    ProtobufDeserializer<TestMessage> deserializer;
    auto result = deserializer.DeserializeMessage(data);
    
    // Should fail because data size doesn't match claimed length
    EXPECT_FALSE(result.HasValue());
}

/**
 * @brief Main function
 */
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
