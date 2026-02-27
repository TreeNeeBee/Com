/**
 * @file        com_runtime_serialization_test.cpp
 * @brief       Unit tests for runtime serialization strategies
 * @author      LightAP Team
 * @date        2026-02-08
 */

#include "ComTypes.hpp"
#include "serialization/CSerializationTraits.hpp"
#include "serialization/CSomeIpSerializer.hpp"
#include "serialization/CSomeIpDeserializer.hpp"
#include "serialization/CCdrSerializer.hpp"
#include "serialization/CCdrDeserializer.hpp"
#include "serialization/CJsonSerializer.hpp"
#include "serialization/CJsonDeserializer.hpp"
#include "serialization/CSerializerFactory.hpp"

#include <core/CSpan.hpp>
#include <gtest/gtest.h>

namespace lap
{
namespace com
{
namespace test
{
    using lap::core::Result;

    struct CustomType
    {
        lap::core::UInt32 id{ 0 };
        lap::core::String name;
    };

    inline Result< void > Serialize(
        serialization::ISerializer& serializer,
        const CustomType& value ) noexcept
    {
        auto r1 = serialization::SerializeValue( serializer, value.id );
        if ( !r1.HasValue() )
        {
            return r1;
        }
        return serialization::SerializeValue( serializer, value.name );
    }

    inline Result< void > Deserialize(
        serialization::IDeserializer& deserializer,
        CustomType& value ) noexcept
    {
        auto r1 = serialization::DeserializeValue( deserializer, value.id );
        if ( !r1.HasValue() )
        {
            return r1;
        }
        return serialization::DeserializeValue( deserializer, value.name );
    }

    template< typename Serializer, typename Deserializer >
    void RoundTripPrimitiveAndCustom()
    {
        Serializer serializer;

        auto r1 = serialization::SerializeValue(
            serializer, static_cast< lap::core::UInt32 > ( 42U ) );
        ASSERT_TRUE( r1.HasValue() );

        lap::core::String hello = "hello";
        auto r2 = serialization::SerializeValue( serializer, hello );
        ASSERT_TRUE( r2.HasValue() );

        CustomType input;
        input.id = 7U;
        input.name = "world";
        auto r3 = serialization::SerializeValue( serializer, input );
        ASSERT_TRUE( r3.HasValue() );

        auto data = serializer.GetData();
        auto span = lap::core::MakeSpan( data.data(), data.size() );
        Deserializer deserializer( span );

        lap::core::UInt32 outId = 0U;
        lap::core::String outText;
        CustomType outCustom;

        auto d1 = serialization::DeserializeValue( deserializer, outId );
        ASSERT_TRUE( d1.HasValue() );

        auto d2 = serialization::DeserializeValue( deserializer, outText );
        ASSERT_TRUE( d2.HasValue() );

        auto d3 = serialization::DeserializeValue( deserializer, outCustom );
        ASSERT_TRUE( d3.HasValue() );

        EXPECT_EQ( outId, 42U );
        EXPECT_EQ( outText, hello );
        EXPECT_EQ( outCustom.id, input.id );
        EXPECT_EQ( outCustom.name, input.name );
    }

} // namespace test
} // namespace com
} // namespace lap

TEST( SerializationRoundTrip, SomeIp_PrimitiveAndCustom )
{
    lap::com::test::RoundTripPrimitiveAndCustom<
        lap::com::serialization::CSomeIpSerializer,
        lap::com::serialization::CSomeIpDeserializer > ();
}

TEST( SerializationRoundTrip, DdsCdr_PrimitiveAndCustom )
{
    lap::com::test::RoundTripPrimitiveAndCustom<
        lap::com::serialization::CCdrSerializer,
        lap::com::serialization::CCdrDeserializer > ();
}

TEST( SerializationRoundTrip, Json_PrimitiveAndCustom )
{
    lap::com::test::RoundTripPrimitiveAndCustom<
        lap::com::serialization::CJsonSerializer,
        lap::com::serialization::CJsonDeserializer > ();
}

TEST( SerializationFactory, CreatesSupportedFormats )
{
    using lap::com::serialization::CSerializerFactory;
    using lap::com::serialization::SerializationFormat;

    auto serializer = CSerializerFactory::CreateSerializer(
        SerializationFormat::kSomeIp );
    ASSERT_NE( serializer, nullptr );

    auto sr = serializer->Serialize( static_cast< lap::core::UInt32 > ( 123U ) );
    ASSERT_TRUE( sr.HasValue() );

    auto data = serializer->GetData();
    auto deserializer = CSerializerFactory::CreateDeserializer(
        SerializationFormat::kSomeIp, data );
    ASSERT_NE( deserializer, nullptr );

    lap::core::UInt32 value = 0U;
    auto dr = deserializer->Deserialize( value );
    ASSERT_TRUE( dr.HasValue() );
    EXPECT_EQ( value, 123U );

    auto protoSerializer = CSerializerFactory::CreateSerializer(
        SerializationFormat::kProtobuf );
    EXPECT_EQ( protoSerializer, nullptr );
}
