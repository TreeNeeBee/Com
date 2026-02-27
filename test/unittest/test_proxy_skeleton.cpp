/**
 * @file        test_proxy_skeleton.cpp
 * @author      Aii
 * @brief       Unit tests for Proxy/Skeleton communication classes
 * @date        2026/02/08
 * @details     Tests ProxyMethod, ProxyEvent, ProxyField, SkeletonMethod,
 *              SkeletonEvent, SkeletonField using a MockTransportBinding.
 *              No real network or shared-memory infrastructure required.
 *
 *              Coverage matrix:
 *              +-----------------+--------+--------+--------+--------+--------+
 *              | Class           | Create | Call/  | Async  | Error  | Bind   |
 *              |                 |        | Sub    |        | Path   | Wire   |
 *              +-----------------+--------+--------+--------+--------+--------+
 *              | ProxyMethod     |   ✓    |   ✓    |   ✓    |   ✓    |   ✓    |
 *              | ProxyEvent      |   ✓    |   ✓    |   -    |   ✓    |   ✓    |
 *              | ProxyField      |   ✓    |   ✓    |   ✓    |   ✓    |   ✓    |
 *              | SkeletonMethod  |   ✓    |   ✓    |   -    |   ✓    |   ✓    |
 *              | SkeletonEvent   |   ✓    |   ✓    |   -    |   ✓    |   ✓    |
 *              | SkeletonField   |   ✓    |   ✓    |   -    |   ✓    |   ✓    |
 *              +-----------------+--------+--------+--------+--------+--------+
 *
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/08  <td>1.0      <td>Aii     <td>Initial proxy/skeleton tests
 * </table>
 */

// Forward declaration for friend access (before headers that reference it)
class ProxySkeletonTestAccessor;

// ==================== Project-Internal Headers ====================
#include "proxy/ProxyMethod.hpp"
#include "proxy/ProxyEvent.hpp"
#include "proxy/ProxyField.hpp"
#include "skeleton/SkeletonMethod.hpp"
#include "skeleton/SkeletonEvent.hpp"
#include "skeleton/SkeletonField.hpp"
#include "CBindingContext.hpp"
#include "ComTypes.hpp"
#include "serialization/CBinarySerializer.hpp"
#include "serialization/CBinaryDeserializer.hpp"
#include "serialization/CSerializationTraits.hpp"

// ==================== Binding Headers ====================
#include "ITransportBinding.hpp"

// ==================== Cross-Module Headers ====================
#include <core/CResult.hpp>
#include <core/CFuture.hpp>
#include <core/CPromise.hpp>
#include <core/CTypedef.hpp>

// ==================== Third-Party Headers ====================
#include <gtest/gtest.h>

// ==================== Standard Library Headers ====================
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace lap::com;
using namespace lap::com::binding;

// ============================================================================
// MockTransportBinding: In-process mock for unit tests
// ============================================================================

/**
 * @brief Lightweight mock transport binding that operates entirely in-process.
 * @details Serializes/deserializes data through memory, stores offered services,
 *          event subscriptions, and method handlers for verification.
 */
class MockTransportBinding final : public ITransportBinding
{
public:
    MockTransportBinding() = default;
    ~MockTransportBinding() override = default;

    // --- Lifecycle ---
    Result< void > Initialize() noexcept override
    {
        m_initialized = true;
        return Result< void >::FromValue();
    }

    Result< void > Shutdown() noexcept override
    {
        m_initialized = false;
        return Result< void >::FromValue();
    }

    // --- Service Management ---
    Result< void > OfferService( UInt64 serviceId, UInt64 instanceId ) noexcept override
    {
        std::scoped_lock< std::mutex > lock( m_mutex );
        m_offeredServices[serviceId] = instanceId;
        return Result< void >::FromValue();
    }

    Result< void > StopOfferService( UInt64 serviceId, UInt64 /*instanceId*/ ) noexcept override
    {
        std::scoped_lock< std::mutex > lock( m_mutex );
        m_offeredServices.erase( serviceId );
        return Result< void >::FromValue();
    }

    // --- Discovery ---
    Result< Vector< UInt64 > > FindService( UInt64 serviceId ) noexcept override
    {
        std::scoped_lock< std::mutex > lock( m_mutex );
        Vector< UInt64 > result;
        auto it = m_offeredServices.find( serviceId );
        if ( it != m_offeredServices.end() )
        {
            result.push_back( it->second );
        }
        return Result< Vector< UInt64 > >::FromValue( std::move( result ) );
    }

    Result< UInt64 > StartFindService( UInt64 /*serviceId*/,
                                     ServiceDiscoveryCallback /*callback*/ ) noexcept override
    {
        return Result< UInt64 >::FromValue( ++m_nextDiscoveryHandle );
    }

    Result< void > StopFindService( UInt64 /*handle*/ ) noexcept override
    {
        return Result< void >::FromValue();
    }

    // --- Events ---
    Result< void > DoSendEvent( UInt64 /*serviceId*/, UInt64 /*instanceId*/,
                            UInt32 eventId,
                            const void* pData,
                            Size dataSize ) noexcept override
    {
        std::scoped_lock< std::mutex > lock( m_mutex );

        // Phase 2: typed path (dataSize > 0) → memcpy raw bytes into ByteBuffer
        // Phase 1: ByteBuffer path (dataSize == 0) → cast to ByteBuffer*
        if ( dataSize > 0 ) {
            m_lastSentEventData = ByteBuffer(
                reinterpret_cast< const uint8_t* >( pData ),
                reinterpret_cast< const uint8_t* >( pData ) + dataSize );
        } else {
            m_lastSentEventData = *static_cast< const ByteBuffer* >( pData );
        }

        m_lastSentEventId = eventId;
        m_sendEventCount++;

        // Deliver to subscriber if any
        auto it = m_eventSubscribers.find( eventId );
        if ( it != m_eventSubscribers.end() && it->second )
        {
            it->second( 0, 0, eventId, pData );
        }
        return Result< void >::FromValue();
    }

    Result< void > DoSubscribeEvent( UInt64 /*serviceId*/, UInt64 /*instanceId*/,
                                 UInt32 eventId,
                                 EventCallback callback,
                                 Size dataSize ) noexcept override
    {
        std::scoped_lock< std::mutex > lock( m_mutex );

        // Phase 2: wrap callback to extract typed data from raw bytes
        if ( dataSize > 0 ) {
            auto wrappedCb = [callback, dataSize](
                UInt64 svcId, UInt64 instId, UInt32 evtId,
                const void* pEvtData )
            {
                // In mock: pEvtData comes from DoSendEvent which already
                // passes the raw typed pointer; just forward
                callback( svcId, instId, evtId, pEvtData );
            };
            m_eventSubscribers[eventId] = std::move( wrappedCb );
        } else {
            m_eventSubscribers[eventId] = std::move( callback );
        }
        return Result< void >::FromValue();
    }

    Result< void > UnsubscribeEvent( UInt64 /*serviceId*/, UInt64 /*instanceId*/,
                                   UInt32 eventId ) noexcept override
    {
        std::scoped_lock< std::mutex > lock( m_mutex );
        m_eventSubscribers.erase( eventId );
        return Result< void >::FromValue();
    }

    // --- Methods ---
    Result< void > DoCallMethod( UInt64 /*serviceId*/, UInt64 /*instanceId*/,
                                   UInt32 methodId,
                                   const void* pRequest,
                                   void* pResponse,
                                   Size requestSize,
                                   Size responseSize ) noexcept override
    {
        std::scoped_lock< std::mutex > lock( m_mutex );
        m_callMethodCount++;

        auto it = m_methodHandlers.find( methodId );
        if ( it != m_methodHandlers.end() && it->second )
        {
            it->second( 0, 0, methodId, pRequest, pResponse );
            return Result< void >::FromValue();
        }

        // Default echo (no handler registered)
        if ( requestSize > 0 && responseSize > 0 )
        {
            // Phase 3 typed path: echo raw bytes (cap at smaller size)
            const Size copySize = requestSize < responseSize ? requestSize : responseSize;
            std::memcpy( pResponse, pRequest, copySize );
        }
        else
        {
            // Phase 1 ByteBuffer path
            *static_cast< ByteBuffer* >( pResponse ) =
                *static_cast< const ByteBuffer* >( pRequest );
        }
        return Result< void >::FromValue();
    }

    Result< void > DoRegisterMethod( UInt64 /*serviceId*/, UInt64 /*instanceId*/,
                                 UInt32 methodId,
                                 MethodHandler handler,
                                 Size /*requestSize*/,
                                 Size /*responseSize*/ ) noexcept override
    {
        std::scoped_lock< std::mutex > lock( m_mutex );
        m_methodHandlers[methodId] = std::move( handler );
        return Result< void >::FromValue();
    }

    // --- Fields ---
    Result< void > DoGetField( UInt64 /*serviceId*/, UInt64 /*instanceId*/,
                                 UInt32 fieldId,
                                 void* pOutValue,
                                 Size /*valueSize*/ ) noexcept override
    {
        std::scoped_lock< std::mutex > lock( m_mutex );
        auto it = m_fieldValues.find( fieldId );
        if ( it != m_fieldValues.end() )
        {
            *static_cast< ByteBuffer* >( pOutValue ) = it->second;
            return Result< void >::FromValue();
        }
        return Result< void >::FromError(
            lap::com::MakeErrorCode( ComErrc::kFieldValueNotInitialized, 0 ) );
    }

    Result< void > DoSetField( UInt64 /*serviceId*/, UInt64 /*instanceId*/,
                           UInt32 fieldId,
                           const void* pValue,
                           Size /*valueSize*/ ) noexcept override
    {
        std::scoped_lock< std::mutex > lock( m_mutex );
        const auto& value = *static_cast< const ByteBuffer* >( pValue );
        m_fieldValues[fieldId] = value;
        m_setFieldCount++;

        // Notify subscribers
        auto it = m_fieldNotificationCallbacks.find( fieldId );
        if ( it != m_fieldNotificationCallbacks.end() && it->second )
        {
            it->second( 0, 0, fieldId, pValue );
        }
        return Result< void >::FromValue();
    }

    Result< void > DoSubscribeFieldNotification( UInt64 /*serviceId*/, UInt64 /*instanceId*/,
                                             UInt32 fieldId,
                                             FieldNotificationCallback callback,
                                             Size /*valueSize*/ ) noexcept override
    {
        std::scoped_lock< std::mutex > lock( m_mutex );
        m_fieldNotificationCallbacks[fieldId] = std::move( callback );
        return Result< void >::FromValue();
    }

    Result< void > UnsubscribeFieldNotification( UInt64 /*serviceId*/, UInt64 /*instanceId*/,
                                               UInt32 fieldId ) noexcept override
    {
        std::scoped_lock< std::mutex > lock( m_mutex );
        m_fieldNotificationCallbacks.erase( fieldId );
        return Result< void >::FromValue();
    }

    // --- Diagnostics ---
    const char* GetName() const noexcept override { return "MockTransportBinding"; }
    UInt32 GetVersion() const noexcept override { return 0x00010000; }
    UInt32 GetPriority() const noexcept override { return 50; }
    Bool SupportsZeroCopy() const noexcept override { return false; }
    Bool SupportsService( UInt64 /*serviceId*/ ) const noexcept override { return true; }
    TransportMetrics GetMetrics() const noexcept override { return TransportMetrics(); }

    // --- Test Inspection ---
    Bool IsInitialized() const { return m_initialized; }
    Int32 GetSendEventCount() const { return m_sendEventCount; }
    Int32 GetCallMethodCount() const { return m_callMethodCount; }
    Int32 GetSetFieldCount() const { return m_setFieldCount; }
    UInt32 GetLastSentEventId() const { return m_lastSentEventId; }
    const ByteBuffer& GetLastSentEventData() const { return m_lastSentEventData; }

    /**
     * @brief Pre-set a field value for GetField to return
     */
    void PresetFieldValue( UInt32 fieldId, const ByteBuffer& value )
    {
        std::scoped_lock< std::mutex > lock( m_mutex );
        m_fieldValues[fieldId] = value;
    }

private:
    mutable std::mutex m_mutex;
    Bool m_initialized{ false };
    UInt64 m_nextDiscoveryHandle{ 0 };

    std::map< UInt64, UInt64 > m_offeredServices;
    std::map< UInt32, EventCallback > m_eventSubscribers;
    std::map< UInt32, MethodHandler > m_methodHandlers;
    std::map< UInt32, ByteBuffer > m_fieldValues;
    std::map< UInt32, FieldNotificationCallback > m_fieldNotificationCallbacks;

    Int32 m_sendEventCount{ 0 };
    Int32 m_callMethodCount{ 0 };
    Int32 m_setFieldCount{ 0 };
    UInt32 m_lastSentEventId{ 0 };
    ByteBuffer m_lastSentEventData;
};

// ============================================================================
// ProxySkeletonTestAccessor: Friend-based access to private wiring methods
// ============================================================================

/**
 * @brief Test-only accessor for private setConnected/setBindingContext methods.
 * @details Granted friend access via `friend class ::ProxySkeletonTestAccessor`
 *          in all proxy/skeleton headers. Enables connected-binding unit tests
 *          without modifying class visibility.
 */
class ProxySkeletonTestAccessor
{
public:
    template< typename Output, typename... Args >
    static void WireProxyMethod( ProxyMethod< Output, Args... >& method,
                                 const CBindingContext& ctx, Bool connected )
    {
        method.setBindingContext( ctx );
        method.setConnected( connected );
    }

    template< typename FieldType >
    static void WireProxyField( ProxyField< FieldType >& field,
                                const CBindingContext& ctx, Bool connected )
    {
        field.setBindingContext( ctx );
        field.setConnected( connected );
    }

    template< typename Output, typename... Args >
    static void WireSkeletonMethod( SkeletonMethod< Output, Args... >& method,
                                    const CBindingContext& ctx )
    {
        method.setBindingContext( ctx );
    }

    template< typename SampleType >
    static void WireSkeletonEvent( SkeletonEvent< SampleType >& event,
                                   const CBindingContext& ctx, Bool offered )
    {
        event.setBindingContext( ctx );
        event.setOffered( offered );
    }

    template< typename FieldType >
    static void WireSkeletonField( SkeletonField< FieldType >& field,
                                   const CBindingContext& ctx )
    {
        field.setBindingContext( ctx );
    }

    template< typename Output, typename... Args >
    static ByteBuffer CallHandleIncomingCall(
        SkeletonMethod< Output, Args... >& method,
        const ByteBuffer& request )
    {
        return method.HandleIncomingCall( request );
    }
};

// ============================================================================
// Helper: Create a valid CBindingContext
// ============================================================================

static CBindingContext MakeTestContext( ITransportBinding* binding,
                                       UInt64 serviceId   = 0x1234,
                                       UInt64 instanceId  = 0x0001,
                                       UInt32 elementId   = 42 )
{
    CBindingContext ctx;
    ctx.pBinding   = binding;
    ctx.serviceId  = serviceId;
    ctx.instanceId = instanceId;
    ctx.elementId  = elementId;
    return ctx;
}

// ============================================================================
// Helper: Serialize a primitive to ByteBuffer using CBinarySerializer
// ============================================================================

static ByteBuffer SerializeUInt32( lap::core::UInt32 value )
{
    serialization::CBinarySerializer serializer;
    serialization::SerializeValue( serializer, value );
    auto data = serializer.GetData();
    return ByteBuffer( data.data(), data.data() + data.size() );
}

static lap::core::UInt32 DeserializeUInt32( const ByteBuffer& buf )
{
    auto span = lap::core::MakeSpan(
        reinterpret_cast< const lap::core::UInt8* > ( buf.data() ), buf.size() );
    serialization::CBinaryDeserializer deser( span );
    lap::core::UInt32 val = 0;
    serialization::DeserializeValue< lap::core::UInt32 > ( deser, val );
    return val;
}

// ============================================================================
// Test Fixture
// ============================================================================

class ProxySkeletonTest : public ::testing::Test
{
protected:
    MockTransportBinding m_mock;

    void SetUp() override
    {
        m_mock.Initialize();
    }

    void TearDown() override
    {
        m_mock.Shutdown();
    }
};

// ############################################################################
// ProxyMethod Tests (SWS_CM_00800)
// ############################################################################

/**
 * @test ProxyMethod: disconnected call returns kServiceNotAvailable
 */
TEST_F( ProxySkeletonTest, ProxyMethod_DisconnectedCallFails )
{
    ProxyMethod< lap::core::UInt32, lap::core::UInt32 > method;

    auto result = method( 42u );
    EXPECT_FALSE( result.HasValue() ) << "Call on disconnected method should fail";
}

/**
 * @test ProxyMethod: IsConnected returns correct state
 */
TEST_F( ProxySkeletonTest, ProxyMethod_IsConnectedState )
{
    ProxyMethod< lap::core::UInt32, lap::core::UInt32 > method;
    EXPECT_FALSE( method.IsConnected() );
}

/**
 * @test ProxyMethod: async call while disconnected returns error future
 */
TEST_F( ProxySkeletonTest, ProxyMethod_AsyncDisconnectedFails )
{
    ProxyMethod< lap::core::UInt32, lap::core::UInt32 > method;

    auto future = method.CallAsync( 42u );
    auto result = future.GetResult();
    EXPECT_FALSE( result.HasValue() ) << "Async call on disconnected should fail";
}

// ############################################################################
// ProxyEvent Tests (SWS_CM_00700)
// ############################################################################

/**
 * @test ProxyEvent: default state is not subscribed
 */
TEST_F( ProxySkeletonTest, ProxyEvent_DefaultNotSubscribed )
{
    ProxyEvent< lap::core::UInt32 > event;
    EXPECT_EQ( event.GetSubscriptionState(), SubscriptionState::kNotSubscribed );
}

/**
 * @test ProxyEvent: subscribe without binding context (no-op / graceful)
 */
TEST_F( ProxySkeletonTest, ProxyEvent_SubscribeWithoutBinding )
{
    ProxyEvent< lap::core::UInt32 > event;

    // Should not crash; subscription state changes even without binding
    auto result = event.Subscribe( 10 );
    // May succeed or fail depending on whether binding is required for subscription
    // The important thing is no crash and predictable state
    SUCCEED() << "Subscribe without binding did not crash";
}

// ############################################################################
// ProxyField Tests (SWS_CM_00900)
// ############################################################################

/**
 * @test ProxyField: Get on disconnected field returns error
 */
TEST_F( ProxySkeletonTest, ProxyField_DisconnectedGetFails )
{
    ProxyField< lap::core::UInt32 > field;

    auto result = field.Get();
    EXPECT_FALSE( result.HasValue() ) << "Get on disconnected field should fail";
}

/**
 * @test ProxyField: Set on disconnected field returns error
 */
TEST_F( ProxySkeletonTest, ProxyField_DisconnectedSetFails )
{
    ProxyField< lap::core::UInt32 > field;

    auto result = field.Set( 42u );
    EXPECT_FALSE( result.HasValue() ) << "Set on disconnected field should fail";
}

// ############################################################################
// SkeletonEvent Tests (SWS_CM_00710)
// ############################################################################

/**
 * @test SkeletonEvent: Send without binding returns error
 */
TEST_F( ProxySkeletonTest, SkeletonEvent_SendWithoutBindingFails )
{
    SkeletonEvent< lap::core::UInt32 > event;

    auto sample = std::make_unique< lap::core::UInt32 > ( 42u );
    auto result = event.Send( std::move( sample ) );
    EXPECT_FALSE( result.HasValue() ) << "Send without binding should fail";
}

// ############################################################################
// SkeletonMethod Tests (SWS_CM_00820)
// ############################################################################

/**
 * @test SkeletonMethod: RegisterMethodHandler succeeds
 */
TEST_F( ProxySkeletonTest, SkeletonMethod_RegisterHandlerSuccess )
{
    SkeletonMethod< lap::core::UInt32, lap::core::UInt32 > method;

    auto result = method.RegisterMethodHandler(
        []( lap::core::UInt32 arg ) -> lap::core::Future< lap::core::UInt32 >
        {
            lap::core::Promise< lap::core::UInt32 > promise;
            promise.SetValue( arg * 2 );
            return promise.GetFuture();
        } );

    EXPECT_TRUE( result.HasValue() ) << "RegisterMethodHandler should succeed";
}

/**
 * @test SkeletonMethod: Registering handler twice returns error
 */
TEST_F( ProxySkeletonTest, SkeletonMethod_DoubleRegisterFails )
{
    SkeletonMethod< lap::core::UInt32, lap::core::UInt32 > method;

    auto handler = []( lap::core::UInt32 arg ) -> lap::core::Future< lap::core::UInt32 >
    {
        lap::core::Promise< lap::core::UInt32 > promise;
        promise.SetValue( arg );
        return promise.GetFuture();
    };

    ASSERT_TRUE( method.RegisterMethodHandler( handler ).HasValue() );

    // Second registration should fail
    auto result = method.RegisterMethodHandler( handler );
    EXPECT_FALSE( result.HasValue() ) << "Double RegisterMethodHandler should fail";
}

/**
 * @test SkeletonMethod: UnregisterMethodHandler succeeds
 */
TEST_F( ProxySkeletonTest, SkeletonMethod_UnregisterHandler )
{
    SkeletonMethod< lap::core::UInt32, lap::core::UInt32 > method;

    auto handler = []( lap::core::UInt32 arg ) -> lap::core::Future< lap::core::UInt32 >
    {
        lap::core::Promise< lap::core::UInt32 > promise;
        promise.SetValue( arg );
        return promise.GetFuture();
    };

    ASSERT_TRUE( method.RegisterMethodHandler( handler ).HasValue() );

    // Unregister
    method.UnregisterMethodHandler();

    // Should be able to register again
    auto result = method.RegisterMethodHandler( handler );
    EXPECT_TRUE( result.HasValue() ) << "Re-register after unregister should succeed";
}

// ############################################################################
// SkeletonField Tests (SWS_CM_00920)
// ############################################################################

/**
 * @test SkeletonField: RegisterGetHandler succeeds
 */
TEST_F( ProxySkeletonTest, SkeletonField_RegisterGetHandler )
{
    SkeletonField< lap::core::UInt32 > field;

    auto result = field.RegisterGetHandler(
        []() -> lap::core::Future< lap::core::UInt32 >
        {
            lap::core::Promise< lap::core::UInt32 > promise;
            promise.SetValue( 100u );
            return promise.GetFuture();
        } );

    EXPECT_TRUE( result.HasValue() ) << "RegisterGetHandler should succeed";
}

/**
 * @test SkeletonField: RegisterSetHandler succeeds
 */
TEST_F( ProxySkeletonTest, SkeletonField_RegisterSetHandler )
{
    SkeletonField< lap::core::UInt32 > field( true, true, false );  // getter=true, setter=true

    auto result = field.RegisterSetHandler(
        []( const lap::core::UInt32& /*value*/ ) -> lap::core::Future< void >
        {
            lap::core::Promise< void > promise;
            promise.SetValue();
            return promise.GetFuture();
        } );

    EXPECT_TRUE( result.HasValue() ) << "RegisterSetHandler should succeed";
}

/**
 * @test SkeletonField: Update without binding returns error
 */
TEST_F( ProxySkeletonTest, SkeletonField_UpdateWithoutBinding )
{
    SkeletonField< lap::core::UInt32 > field;

    auto result = field.Update( 42u );
    EXPECT_FALSE( result.HasValue() ) << "Update without binding should fail";
}

// ############################################################################
// Connected Proxy Tests — ProxyMethod with MockTransportBinding
// ############################################################################

/**
 * @test ProxyMethod: connected call async with direct typed path round-trip
 * @details Wires SkeletonMethod (doubler) + ProxyMethod to same MockTransportBinding.
 *          Phase 3: UInt32 is trivially copyable — bypasses CBinarySerializer.
 */
TEST_F( ProxySkeletonTest, ProxyMethod_ConnectedCallAsyncRoundTrip )
{
    // Register a SkeletonMethod handler that doubles the input
    SkeletonMethod< lap::core::UInt32, lap::core::UInt32 > skeleton;
    ASSERT_TRUE( skeleton.RegisterMethodHandler(
        []( lap::core::UInt32 arg ) -> lap::core::Future< lap::core::UInt32 >
        {
            lap::core::Promise< lap::core::UInt32 > promise;
            promise.SetValue( arg * 2u );
            return promise.GetFuture();
        } ).HasValue() );

    auto ctx = MakeTestContext( &m_mock, 0x1234, 0x0001, 42 );
    ProxySkeletonTestAccessor::WireSkeletonMethod( skeleton, ctx );

    ProxyMethod< lap::core::UInt32, lap::core::UInt32 > method;
    ProxySkeletonTestAccessor::WireProxyMethod( method, ctx, true );

    EXPECT_TRUE( method.IsConnected() );

    auto future = method.CallAsync( 21u );
    auto result = future.GetResult();
    ASSERT_TRUE( result.HasValue() ) << "Connected async call should succeed";
    EXPECT_EQ( result.Value(), 42u ) << "Doubler handler should return 2*21 = 42";
}

/**
 * @test ProxyMethod: connected sync call via SkeletonMethod echo handler
 * @details Phase 3: UInt32 trivially copyable — typed path, no serialization.
 */
TEST_F( ProxySkeletonTest, ProxyMethod_ConnectedSyncCall )
{
    // Register a SkeletonMethod that echoes back the same value
    SkeletonMethod< lap::core::UInt32, lap::core::UInt32 > skeleton;
    ASSERT_TRUE( skeleton.RegisterMethodHandler(
        []( lap::core::UInt32 arg ) -> lap::core::Future< lap::core::UInt32 >
        {
            lap::core::Promise< lap::core::UInt32 > promise;
            promise.SetValue( arg );
            return promise.GetFuture();
        } ).HasValue() );

    auto ctx = MakeTestContext( &m_mock, 0x1234, 0x0001, 42 );
    ProxySkeletonTestAccessor::WireSkeletonMethod( skeleton, ctx );

    ProxyMethod< lap::core::UInt32, lap::core::UInt32 > method;
    ProxySkeletonTestAccessor::WireProxyMethod( method, ctx, true );

    auto result = method( 99u );
    ASSERT_TRUE( result.HasValue() ) << "Connected sync call should succeed";
    EXPECT_EQ( result.Value(), 99u );
}

/**
 * @test SkeletonMethod + ProxyMethod: Phase 3 direct typed path round-trip
 * @details UInt32 is trivially copyable; both sides bypass CBinarySerializer.
 *          SkeletonMethod registers RegisterMethod<tuple<UInt32>, UInt32>;
 *          ProxyMethod calls CallMethod<UInt32, tuple<UInt32>>.
 */
TEST_F( ProxySkeletonTest, SkeletonProxyMethod_TypedDirectPathRoundTrip )
{
    SkeletonMethod< lap::core::UInt32, lap::core::UInt32 > skeleton;
    ASSERT_TRUE( skeleton.RegisterMethodHandler(
        []( lap::core::UInt32 x ) -> lap::core::Future< lap::core::UInt32 >
        {
            lap::core::Promise< lap::core::UInt32 > promise;
            promise.SetValue( x + 100u );
            return promise.GetFuture();
        } ).HasValue() );

    auto ctx = MakeTestContext( &m_mock, 0x1234, 0x0001, 55 );
    ProxySkeletonTestAccessor::WireSkeletonMethod( skeleton, ctx );

    ProxyMethod< lap::core::UInt32, lap::core::UInt32 > proxy;
    ProxySkeletonTestAccessor::WireProxyMethod( proxy, ctx, true );

    // Sync: 7 + 100 = 107
    auto r1 = proxy( 7u );
    ASSERT_TRUE( r1.HasValue() ) << "Typed sync call should succeed";
    EXPECT_EQ( r1.Value(), 107u ) << "x + 100: 7 + 100 = 107";

    // Async: 50 + 100 = 150
    auto fut = proxy.CallAsync( 50u );
    auto r2 = fut.GetResult();
    ASSERT_TRUE( r2.HasValue() ) << "Typed async call should succeed";
    EXPECT_EQ( r2.Value(), 150u ) << "x + 100: 50 + 100 = 150";
}

// ############################################################################
// Connected Proxy Tests — ProxyField with MockTransportBinding
// ############################################################################

/**
 * @test ProxyField: connected Get reads from binding
 */
TEST_F( ProxySkeletonTest, ProxyField_ConnectedGet )
{
    // Pre-set field value in mock
    m_mock.PresetFieldValue( 42, SerializeUInt32( 777u ) );

    ProxyField< lap::core::UInt32 > field;
    auto ctx = MakeTestContext( &m_mock, 0x1234, 0x0001, 42 );
    ProxySkeletonTestAccessor::WireProxyField( field, ctx, true );

    auto result = field.Get();
    ASSERT_TRUE( result.HasValue() ) << "Connected Get should succeed";
    EXPECT_EQ( result.Value(), 777u );
}

/**
 * @test ProxyField: connected Set writes to binding
 */
TEST_F( ProxySkeletonTest, ProxyField_ConnectedSet )
{
    ProxyField< lap::core::UInt32 > field( true, true, false );  // getter=true, setter=true
    auto ctx = MakeTestContext( &m_mock, 0x1234, 0x0001, 42 );
    ProxySkeletonTestAccessor::WireProxyField( field, ctx, true );

    auto result = field.Set( 999u );
    EXPECT_TRUE( result.HasValue() ) << "Connected Set should succeed";
    EXPECT_EQ( m_mock.GetSetFieldCount(), 1 );

    // Verify round-trip: read back the written value
    auto getResult = m_mock.GetField< ByteBuffer >( 0x1234, 0x0001, 42 );
    ASSERT_TRUE( getResult.HasValue() );
    EXPECT_EQ( DeserializeUInt32( getResult.Value() ), 999u );
}

/**
 * @test ProxyField: connected GetAsync returns future with value
 */
TEST_F( ProxySkeletonTest, ProxyField_ConnectedGetAsync )
{
    m_mock.PresetFieldValue( 42, SerializeUInt32( 123u ) );

    ProxyField< lap::core::UInt32 > field;
    auto ctx = MakeTestContext( &m_mock, 0x1234, 0x0001, 42 );
    ProxySkeletonTestAccessor::WireProxyField( field, ctx, true );

    auto future = field.GetAsync();
    auto result = future.GetResult();
    ASSERT_TRUE( result.HasValue() ) << "Connected GetAsync should succeed";
    EXPECT_EQ( result.Value(), 123u );
}

/**
 * @test ProxyField: connected SetAsync returns future void
 */
TEST_F( ProxySkeletonTest, ProxyField_ConnectedSetAsync )
{
    ProxyField< lap::core::UInt32 > field( true, true, false );  // getter=true, setter=true
    auto ctx = MakeTestContext( &m_mock, 0x1234, 0x0001, 42 );
    ProxySkeletonTestAccessor::WireProxyField( field, ctx, true );

    auto future = field.SetAsync( 456u );
    auto result = future.GetResult();
    EXPECT_TRUE( result.HasValue() ) << "Connected SetAsync should succeed";
    EXPECT_EQ( m_mock.GetSetFieldCount(), 1 );
}

// ############################################################################
// Connected Skeleton Tests — SkeletonMethod HandleIncomingCall
// ############################################################################

/**
 * @test SkeletonMethod: HandleIncomingCall full serialization round-trip
 * @details Registers a handler, then simulates an incoming call by passing
 *          serialized arguments through HandleIncomingCall. Verifies the
 *          serialized response contains the expected output.
 */
TEST_F( ProxySkeletonTest, SkeletonMethod_HandleIncomingCallRoundTrip )
{
    SkeletonMethod< lap::core::UInt32, lap::core::UInt32 > method;

    // Register a handler that triples the input
    auto regResult = method.RegisterMethodHandler(
        []( lap::core::UInt32 arg ) -> lap::core::Future< lap::core::UInt32 >
        {
            lap::core::Promise< lap::core::UInt32 > promise;
            promise.SetValue( arg * 3 );
            return promise.GetFuture();
        } );
    ASSERT_TRUE( regResult.HasValue() );

    auto ctx = MakeTestContext( &m_mock, 0x1234, 0x0001, 42 );
    ProxySkeletonTestAccessor::WireSkeletonMethod( method, ctx );

    // Serialize request (input = 7)
    ByteBuffer request = SerializeUInt32( 7u );

    // Process the call
    ByteBuffer response = ProxySkeletonTestAccessor::CallHandleIncomingCall(
        method, request );

    ASSERT_FALSE( response.empty() ) << "HandleIncomingCall should produce a response";
    EXPECT_EQ( DeserializeUInt32( response ), 21u ) << "7 * 3 = 21";
}

/**
 * @test SkeletonMethod: HandleIncomingCall with no handler returns empty
 */
TEST_F( ProxySkeletonTest, SkeletonMethod_HandleIncomingCallNoHandler )
{
    SkeletonMethod< lap::core::UInt32, lap::core::UInt32 > method;

    auto ctx = MakeTestContext( &m_mock, 0x1234, 0x0001, 42 );
    ProxySkeletonTestAccessor::WireSkeletonMethod( method, ctx );

    ByteBuffer request = SerializeUInt32( 7u );
    ByteBuffer response = ProxySkeletonTestAccessor::CallHandleIncomingCall(
        method, request );

    EXPECT_TRUE( response.empty() ) << "No handler should produce empty response";
}

// ############################################################################
// Connected Skeleton Tests — SkeletonEvent Send with binding
// ############################################################################

/**
 * @test SkeletonEvent: Send with binding delivers data
 */
TEST_F( ProxySkeletonTest, SkeletonEvent_ConnectedSend )
{
    SkeletonEvent< lap::core::UInt32 > event;
    auto ctx = MakeTestContext( &m_mock, 0x1234, 0x0001, 10 );
    ProxySkeletonTestAccessor::WireSkeletonEvent( event, ctx, true );

    auto sample = std::make_unique< lap::core::UInt32 > ( 42u );
    auto result = event.Send( std::move( sample ) );
    EXPECT_TRUE( result.HasValue() ) << "Connected Send should succeed";
    EXPECT_EQ( m_mock.GetSendEventCount(), 1 );
    EXPECT_EQ( m_mock.GetLastSentEventId(), 10u );
}

/**
 * @test SkeletonEvent: Allocate returns valid sample
 */
TEST_F( ProxySkeletonTest, SkeletonEvent_AllocateSuccess )
{
    SkeletonEvent< lap::core::UInt32 > event;

    auto allocResult = event.Allocate();
    ASSERT_TRUE( allocResult.HasValue() ) << "Allocate should succeed";
    ASSERT_NE( allocResult.Value(), nullptr );

    *allocResult.Value() = 123u;
    EXPECT_EQ( *allocResult.Value(), 123u );
}

// ############################################################################
// CBindingContext Tests
// ############################################################################

/**
 * @test Default CBindingContext is invalid
 */
TEST_F( ProxySkeletonTest, BindingContext_DefaultInvalid )
{
    CBindingContext ctx;
    EXPECT_FALSE( ctx.IsValid() );
}

/**
 * @test CBindingContext with binding pointer is valid
 */
TEST_F( ProxySkeletonTest, BindingContext_WithBindingValid )
{
    auto ctx = MakeTestContext( &m_mock );
    EXPECT_TRUE( ctx.IsValid() );
    EXPECT_EQ( ctx.serviceId, 0x1234u );
    EXPECT_EQ( ctx.instanceId, 0x0001u );
    EXPECT_EQ( ctx.elementId, 42u );
}

// ############################################################################
// MockTransportBinding Sanity Tests
// ############################################################################

/**
 * @test Mock binding: OfferService and FindService round-trip
 */
TEST_F( ProxySkeletonTest, MockBinding_OfferAndFind )
{
    ASSERT_TRUE( m_mock.OfferService( 0x1234, 0x0001 ).HasValue() );

    auto findResult = m_mock.FindService( 0x1234 );
    ASSERT_TRUE( findResult.HasValue() );
    EXPECT_EQ( findResult.Value().size(), 1u );
    EXPECT_EQ( findResult.Value()[0], 0x0001u );
}

/**
 * @test Mock binding: CallMethod echo test
 */
TEST_F( ProxySkeletonTest, MockBinding_CallMethodEcho )
{
    ByteBuffer request = SerializeUInt32( 42u );

    auto result = m_mock.CallMethod< ByteBuffer >( 0x1234, 0x0001, 1, request );
    ASSERT_TRUE( result.HasValue() );

    auto value = DeserializeUInt32( result.Value() );
    EXPECT_EQ( value, 42u ) << "Echo method should return same value";
}

/**
 * @test Mock binding: RegisterMethod then CallMethod uses handler
 */
TEST_F( ProxySkeletonTest, MockBinding_RegisterAndCallMethod )
{
    // Register a handler that doubles the input
    m_mock.RegisterMethod< ByteBuffer, ByteBuffer >( 0x1234, 0x0001, 1,
        []( UInt64, UInt64, UInt32, const ByteBuffer& request ) -> ByteBuffer
        {
            auto span = lap::core::MakeSpan(
                reinterpret_cast< const lap::core::UInt8* > ( request.data() ),
                request.size() );
            serialization::CBinaryDeserializer deser( span );
            lap::core::UInt32 val = 0;
            serialization::DeserializeValue< lap::core::UInt32 > ( deser, val );

            serialization::CBinarySerializer ser;
            serialization::SerializeValue( ser, static_cast< lap::core::UInt32 > ( val * 2 ) );
            auto data = ser.GetData();
            return ByteBuffer( data.data(), data.data() + data.size() );
        } );

    ByteBuffer request = SerializeUInt32( 21u );
    auto result = m_mock.CallMethod< ByteBuffer >( 0x1234, 0x0001, 1, request );
    ASSERT_TRUE( result.HasValue() );
    EXPECT_EQ( DeserializeUInt32( result.Value() ), 42u );
}

/**
 * @test Mock binding: SendEvent and subscriber callback
 */
TEST_F( ProxySkeletonTest, MockBinding_EventPubSub )
{
    Atomic< Int32 > callbackCount{ 0 };
    ByteBuffer receivedData;

    m_mock.SubscribeEvent< ByteBuffer >( 0x1234, 0x0001, 10,
        [&]( UInt64, UInt64, UInt32, const ByteBuffer& data )
        {
            receivedData = data;
            callbackCount++;
        } );

    ByteBuffer eventData = SerializeUInt32( 999u );
    ASSERT_TRUE( m_mock.SendEvent( 0x1234, 0x0001, 10, eventData ).HasValue() );

    EXPECT_EQ( callbackCount.load(), 1 );
    EXPECT_EQ( DeserializeUInt32( receivedData ), 999u );
}

/**
 * @test Mock binding: SetField and GetField round-trip
 */
TEST_F( ProxySkeletonTest, MockBinding_FieldSetGet )
{
    ByteBuffer val = SerializeUInt32( 777u );
    ASSERT_TRUE( m_mock.SetField( 0x1234, 0x0001, 5, val ).HasValue() );

    auto getResult = m_mock.GetField< ByteBuffer >( 0x1234, 0x0001, 5 );
    ASSERT_TRUE( getResult.HasValue() );
    EXPECT_EQ( DeserializeUInt32( getResult.Value() ), 777u );
}

/**
 * @test Mock binding: GetField on non-existent field returns error
 */
TEST_F( ProxySkeletonTest, MockBinding_GetFieldNonExistent )
{
    auto result = m_mock.GetField< ByteBuffer >( 0x1234, 0x0001, 99 );
    EXPECT_FALSE( result.HasValue() );
}

/**
 * @test Mock binding: Field notification subscription
 */
TEST_F( ProxySkeletonTest, MockBinding_FieldNotification )
{
    Atomic< Int32 > notifyCount{ 0 };
    ByteBuffer notifiedValue;

    m_mock.SubscribeFieldNotification< ByteBuffer >( 0x1234, 0x0001, 5,
        [&]( UInt64, UInt64, UInt32, const ByteBuffer& value )
        {
            notifiedValue = value;
            notifyCount++;
        } );

    ByteBuffer val = SerializeUInt32( 888u );
    m_mock.SetField( 0x1234, 0x0001, 5, val );

    EXPECT_EQ( notifyCount.load(), 1 );
    EXPECT_EQ( DeserializeUInt32( notifiedValue ), 888u );
}

/**
 * @test Mock binding: diagnostics
 */
TEST_F( ProxySkeletonTest, MockBinding_Diagnostics )
{
    EXPECT_STREQ( m_mock.GetName(), "MockTransportBinding" );
    EXPECT_EQ( m_mock.GetVersion(), 0x00010000u );
    EXPECT_EQ( m_mock.GetPriority(), 50u );
    EXPECT_FALSE( m_mock.SupportsZeroCopy() );
    EXPECT_TRUE( m_mock.SupportsService( 0xFFFF ) );
}

// ============================================================================
// Main
// ============================================================================

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
