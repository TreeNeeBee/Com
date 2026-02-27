/**
 * @file        HelloWorldTypes.hpp
 * @author      Aii
 * @brief       Auto-generated types for HelloWorld service
 * @date        2026/02/09
 * @details     Auto-generated from /workspace/LightAP/modules/Com/examples/helloworld/HelloWorld.fidl by lap-sidl-gen v1.0
 * @copyright   Copyright (c) 2026
 * @note        DO NOT EDIT — This file is auto-generated
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>Auto-generated
 * </table>
 */

#ifndef LAP_COM_EXAMPLES_HELLOWORLDTYPES_HPP
#define LAP_COM_EXAMPLES_HELLOWORLDTYPES_HPP

// ==================== Cross-Module Headers ====================
#include <com/ComTypes.hpp>

// ==================== Serialization Headers ====================
#include "serialization/CSerializationTraits.hpp"

// ==================== Standard Library Headers ====================
#include <vector>

namespace lap
{
namespace com
{
namespace examples
{

    // ==================== Type Collection: HelloWorldTypes ====================

    namespace HelloWorldTypes
    {

    /**
     * @brief ErrorCode enumeration
     */
    enum class ErrorCode : Int32 {
        kOk                     = 0,
        kUnknown                = 1,
        kInvalidArg             = 2,
        kOverflow               = 3
    };

    /**
     * @brief ServerStatus enumeration
     */
    enum class ServerStatus : Int32 {
        kStarting               = 0,
        kRunning                = 1,
        kBusy                   = 2,
        kStopping               = 3
    };

    /**
     * @brief GreetingMessage data structure
     */
    struct GreetingMessage {
        String                      text;
        UInt64                      timestamp;
    };

    /**
     * @brief DataChunk data structure
     */
    struct DataChunk {
        UInt32                      sequenceNo;
        UInt32                      totalSize;
        ::std::vector< UInt8 >      payload;
    };

    // --- ADL serialization (in-namespace for ADL) ---

    inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const GreetingMessage& v ) noexcept {
        auto r_text = ::lap::com::serialization::SerializeValue( s, v.text );
        if ( !r_text.HasValue() ) { return r_text; }
        auto r_timestamp = ::lap::com::serialization::SerializeValue( s, v.timestamp );
        if ( !r_timestamp.HasValue() ) { return r_timestamp; }
        return Result< void >::FromValue();
    }

    inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, GreetingMessage& v ) noexcept {
        auto r_text = ::lap::com::serialization::DeserializeValue( d, v.text );
        if ( !r_text.HasValue() ) { return r_text; }
        auto r_timestamp = ::lap::com::serialization::DeserializeValue( d, v.timestamp );
        if ( !r_timestamp.HasValue() ) { return r_timestamp; }
        return Result< void >::FromValue();
    }

    inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const DataChunk& v ) noexcept {
        auto r_sequenceNo = ::lap::com::serialization::SerializeValue( s, v.sequenceNo );
        if ( !r_sequenceNo.HasValue() ) { return r_sequenceNo; }
        auto r_totalSize = ::lap::com::serialization::SerializeValue( s, v.totalSize );
        if ( !r_totalSize.HasValue() ) { return r_totalSize; }
        auto r_payload = ::lap::com::serialization::SerializeValue( s, v.payload );
        if ( !r_payload.HasValue() ) { return r_payload; }
        return Result< void >::FromValue();
    }

    inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, DataChunk& v ) noexcept {
        auto r_sequenceNo = ::lap::com::serialization::DeserializeValue( d, v.sequenceNo );
        if ( !r_sequenceNo.HasValue() ) { return r_sequenceNo; }
        auto r_totalSize = ::lap::com::serialization::DeserializeValue( d, v.totalSize );
        if ( !r_totalSize.HasValue() ) { return r_totalSize; }
        auto r_payload = ::lap::com::serialization::DeserializeValue( d, v.payload );
        if ( !r_payload.HasValue() ) { return r_payload; }
        return Result< void >::FromValue();
    }

    inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const ErrorCode& v ) noexcept {
        return s.Serialize( static_cast< Int32 >( v ) );
    }

    inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, ErrorCode& v ) noexcept {
        Int32 tmp = 0;
        auto r = d.Deserialize( tmp );
        if ( r.HasValue() ) { v = static_cast< ErrorCode >( tmp ); }
        return r;
    }

    inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const ServerStatus& v ) noexcept {
        return s.Serialize( static_cast< Int32 >( v ) );
    }

    inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, ServerStatus& v ) noexcept {
        Int32 tmp = 0;
        auto r = d.Deserialize( tmp );
        if ( r.HasValue() ) { v = static_cast< ServerStatus >( tmp ); }
        return r;
    }

    } // namespace HelloWorldTypes

    /**
     * @brief Event data for broadcast Greeting
     * @note [SWS_CM_00700] — Event communication
     */
    struct GreetingEvent {
        HelloWorldTypes::GreetingMessage message;
    };

    /**
     * @brief Event data for broadcast StatusChanged
     * @note [SWS_CM_00700] — Event communication
     */
    struct StatusChangedEvent {
        HelloWorldTypes::ServerStatus status;
    };

    /**
     * @brief Event data for broadcast DataStream
     * @note [SWS_CM_00700] — Event communication
     */
    struct DataStreamEvent {
        HelloWorldTypes::DataChunk  chunk;
    };


    // ==================== ADL Serialization Traits ====================
    // Required by CSerializationTraits.hpp for non-primitive types

    inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const GreetingEvent& v ) noexcept {
        auto r_message = ::lap::com::serialization::SerializeValue( s, v.message );
        if ( !r_message.HasValue() ) { return r_message; }
        return Result< void >::FromValue();
    }

    inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, GreetingEvent& v ) noexcept {
        auto r_message = ::lap::com::serialization::DeserializeValue( d, v.message );
        if ( !r_message.HasValue() ) { return r_message; }
        return Result< void >::FromValue();
    }

    inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const StatusChangedEvent& v ) noexcept {
        auto r_status = ::lap::com::serialization::SerializeValue( s, v.status );
        if ( !r_status.HasValue() ) { return r_status; }
        return Result< void >::FromValue();
    }

    inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, StatusChangedEvent& v ) noexcept {
        auto r_status = ::lap::com::serialization::DeserializeValue( d, v.status );
        if ( !r_status.HasValue() ) { return r_status; }
        return Result< void >::FromValue();
    }

    inline Result< void > Serialize( ::lap::com::serialization::ISerializer& s, const DataStreamEvent& v ) noexcept {
        auto r_chunk = ::lap::com::serialization::SerializeValue( s, v.chunk );
        if ( !r_chunk.HasValue() ) { return r_chunk; }
        return Result< void >::FromValue();
    }

    inline Result< void > Deserialize( ::lap::com::serialization::IDeserializer& d, DataStreamEvent& v ) noexcept {
        auto r_chunk = ::lap::com::serialization::DeserializeValue( d, v.chunk );
        if ( !r_chunk.HasValue() ) { return r_chunk; }
        return Result< void >::FromValue();
    }


// [SWS_CM_11501] — common inner namespace
namespace common
{

    /**
     * @brief Common service identification for HelloWorld [SWS_CM_01010]
     * @version 2.0.0
     */
    class HelloWorld {
    public:
        static constexpr UInt16 kServiceId = 0x02e0;  ///< [SWS_CM_11506]
        static constexpr const Char* kServiceName = "HelloWorld";
        static constexpr const Char* kSchemaHash  = "5a1b660e3ef06eb8";
        static constexpr UInt32 kVersionMajor = 2;  ///< [SWS_CM_11507]
        static constexpr UInt32 kVersionMinor = 0;
    };

} // namespace common

} // namespace examples
} // namespace com
} // namespace lap

#endif // LAP_COM_EXAMPLES_HELLOWORLDTYPES_HPP
