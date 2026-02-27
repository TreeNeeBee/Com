/**
 * @file        HelloWorld2ServiceDdsAdapter.hpp
 * @author      Aii
 * @brief       Auto-generated DDS type adapter registration for HelloWorld2Service
 * @date        2026/02/09
 * @details     Auto-generated from examples/helloworld2/HelloWorld2.fidl by lap-sidl-gen v1.0
 * @copyright   Copyright (c) 2026
 * @note        DO NOT EDIT — This file is auto-generated
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/09  <td>1.0      <td>Aii     <td>Auto-generated
 * </table>
 */

#ifndef HELLOWORLD2_HELLOWORLD2SERVICEDDSADAPTER_HPP
#define HELLOWORLD2_HELLOWORLD2SERVICEDDSADAPTER_HPP

// ==================== DDS Binding Headers ====================
#include "CDdsPayload.hpp"
#include "IDdsTypeAdapter.hpp"
#include "CDdsTypeRegistry.hpp"

// ==================== fastddsgen Type Headers ====================
#include "HelloWorld2Service.hpp"
#include "HelloWorld2ServicePubSubTypes.hpp"

// ==================== Application Type Headers ====================
#include "HelloWorld2ServiceTypes.hpp"

// ==================== Standard Library Headers ====================
#include <cstring>
#include <cstddef>

namespace helloworld2
{

namespace dds_adapter
{

// DDS namespace alias for typed event adapters
namespace DdsTypes = ::lap::examples::helloworld2;

// ================================================================
// Event Adapters (typed CDR via fastddsgen PubSubType)
// ================================================================

/// @brief DDS type adapter for event 'Greeting' (elementId=0x0001)
class HelloWorld2Service_Greeting_EventAdapter final
    : public ::lap::com::binding::IDdsTypeAdapter
{
public:
    eprosima::fastdds::dds::TypeSupport GetTypeSupport() const noexcept override {
        return eprosima::fastdds::dds::TypeSupport(
            new DdsTypes::GreetingEventPubSubType() );
    }

    void* CreateSample( const void* pData,
                        ::lap::core::Size dataSize ) const override {
        auto* pDds = new DdsTypes::GreetingEvent();
        if ( pData != nullptr && dataSize > 0u ) {
            const auto& app = *static_cast< const ::helloworld2::GreetingEvent* >( pData );
            pDds->text( app.text );
        }
        return pDds;
    }

    const void* ExtractData( const void* pSample ) const noexcept override {
        if ( pSample == nullptr ) { return nullptr; }
        const auto& dds = *static_cast< const DdsTypes::GreetingEvent* >( pSample );
        try {
            thread_local ::helloworld2::GreetingEvent appBuf;
            appBuf.text = dds.text();
            return &appBuf;
        } catch ( ... ) {
            return nullptr;
        }
    }

    void FreeSample( void* pSample ) const noexcept override {
        delete static_cast< DdsTypes::GreetingEvent* >( pSample );
    }
};

/// @brief DDS type adapter for event 'StatusChanged' (elementId=0x0002)
class HelloWorld2Service_StatusChanged_EventAdapter final
    : public ::lap::com::binding::IDdsTypeAdapter
{
public:
    eprosima::fastdds::dds::TypeSupport GetTypeSupport() const noexcept override {
        return eprosima::fastdds::dds::TypeSupport(
            new DdsTypes::StatusChangedEventPubSubType() );
    }

    void* CreateSample( const void* pData,
                        ::lap::core::Size dataSize ) const override {
        auto* pDds = new DdsTypes::StatusChangedEvent();
        if ( pData != nullptr && dataSize > 0u ) {
            const auto& app = *static_cast< const ::helloworld2::StatusChangedEvent* >( pData );
            pDds->status( static_cast< DdsTypes::ServerStatus >( static_cast< ::std::int32_t >( app.status ) ) );
        }
        return pDds;
    }

    const void* ExtractData( const void* pSample ) const noexcept override {
        if ( pSample == nullptr ) { return nullptr; }
        const auto& dds = *static_cast< const DdsTypes::StatusChangedEvent* >( pSample );
        try {
            thread_local ::helloworld2::StatusChangedEvent appBuf;
            appBuf.status = static_cast< ::helloworld2::HelloWorld2Types::ServerStatus >( static_cast< ::std::int32_t >( dds.status() ) );
            return &appBuf;
        } catch ( ... ) {
            return nullptr;
        }
    }

    void FreeSample( void* pSample ) const noexcept override {
        delete static_cast< DdsTypes::StatusChangedEvent* >( pSample );
    }
};

/// @brief DDS type adapter for event 'DataStream' (elementId=0x0003)
class HelloWorld2Service_DataStream_EventAdapter final
    : public ::lap::com::binding::IDdsTypeAdapter
{
public:
    eprosima::fastdds::dds::TypeSupport GetTypeSupport() const noexcept override {
        return eprosima::fastdds::dds::TypeSupport(
            new DdsTypes::DataStreamEventPubSubType() );
    }

    void* CreateSample( const void* pData,
                        ::lap::core::Size dataSize ) const override {
        auto* pDds = new DdsTypes::DataStreamEvent();
        if ( pData != nullptr && dataSize > 0u ) {
            const auto& app = *static_cast< const ::helloworld2::DataStreamEvent* >( pData );
            {
                DdsTypes::DataChunk dds_chunk;
                dds_chunk.sequenceNo( app.chunk.sequenceNo );
                dds_chunk.totalSize( app.chunk.totalSize );
                dds_chunk.payload( app.chunk.payload );
                pDds->chunk( ::std::move( dds_chunk ) );
            }
        }
        return pDds;
    }

    const void* ExtractData( const void* pSample ) const noexcept override {
        if ( pSample == nullptr ) { return nullptr; }
        const auto& dds = *static_cast< const DdsTypes::DataStreamEvent* >( pSample );
        try {
            thread_local ::helloworld2::DataStreamEvent appBuf;
            {
                const auto& ddsRef_chunk = dds.chunk();
                appBuf.chunk.sequenceNo = ddsRef_chunk.sequenceNo();
                appBuf.chunk.totalSize = ddsRef_chunk.totalSize();
                appBuf.chunk.payload = ddsRef_chunk.payload();
            }
            return &appBuf;
        } catch ( ... ) {
            return nullptr;
        }
    }

    void FreeSample( void* pSample ) const noexcept override {
        delete static_cast< DdsTypes::DataStreamEvent* >( pSample );
    }
};

// ================================================================
// Registration Function
// ================================================================

/**
 * @brief  Register all DDS adapters for HelloWorld2Service with CDdsTypeRegistry
 * @param  serviceId  32-bit service ID (matches kServiceId in Proxy/Skeleton)
 * @note   Call once at application startup before any DDS communication.
 *         The static adapter instances are zero-overhead singletons.
 */
inline void RegisterHelloWorld2ServiceDdsAdapters(
    ::lap::core::UInt64 serviceId ) noexcept
{
    static HelloWorld2Service_Greeting_EventAdapter s_greetingEventAdapter;
    ::lap::com::binding::CDdsTypeRegistry::Instance()
        .RegisterAdapter( serviceId, 1u, &s_greetingEventAdapter );

    static HelloWorld2Service_StatusChanged_EventAdapter s_statusChangedEventAdapter;
    ::lap::com::binding::CDdsTypeRegistry::Instance()
        .RegisterAdapter( serviceId, 2u, &s_statusChangedEventAdapter );

    static HelloWorld2Service_DataStream_EventAdapter s_dataStreamEventAdapter;
    ::lap::com::binding::CDdsTypeRegistry::Instance()
        .RegisterAdapter( serviceId, 3u, &s_dataStreamEventAdapter );

}

} // namespace dds_adapter

} // namespace helloworld2

#endif // HELLOWORLD2_HELLOWORLD2SERVICEDDSADAPTER_HPP
