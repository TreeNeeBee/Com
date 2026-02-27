/**
 * @file        test_coreipc_unit.cpp
 * @author      Aii
 * @brief       CoreIPC binding — comprehensive unit tests
 * @date        2026/02/08
 * @details     Isolated unit tests covering:
 *              - CCoreIPCCodec: path/key generation, event/method encode/decode
 *              - CoreIPCConfig: default values
 *              - CoreIPCBinding: capability queries, lifecycle edge cases,
 *                pre-init error paths
 *              Does NOT require CRegistryDispatcher for pure-codec tests.
 * @copyright   Copyright (c) 2026
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/08  <td>1.0      <td>Aii     <td>Initial comprehensive unit tests
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CoreIPCTypes.hpp"
#include "CCoreIPCCodec.hpp"
#include "CoreIPCBinding.hpp"
#include "CRegistryDispatcher.hpp"

// ==================== Standard Library Headers ====================
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <chrono>

using namespace lap::com::binding;
using namespace lap::com::registry;

// Forward-declare C export functions from CoreIPCBinding.cpp
extern "C" {
    ITransportBinding* CreateBindingInstance();
    void DestroyBindingInstance( ITransportBinding* instance );
}

namespace
{
    int g_iTestsPassed = 0;
    int g_iTestsFailed = 0;

    void ReportResult( const char* testName, bool passed,
                       std::string_view detail = {} )
    {
        if ( passed ) {
            std::cout << "  [PASS] " << testName << std::endl;
            ++g_iTestsPassed;
        } else {
            std::cerr << "  [FAIL] " << testName;
            if ( !detail.empty() ) {
                std::cerr << " — " << detail;
            }
            std::cerr << std::endl;
            ++g_iTestsFailed;
        }
    }

    // ================================================================
    // Section 1: CCoreIPCCodec — Path Generation
    // ================================================================

    void TestMakeServicePath()
    {
        auto path = CCoreIPCCodec::MakeServicePath( 0x1234, 0x0001 );
        // Expected: "/lap_ipc_1234_0001"
        ReportResult( "MakeServicePath format",
                      path == "/lap_ipc_1234_0001",
                      std::string( "got: " ) + path );
    }

    void TestMakeServicePathZero()
    {
        auto path = CCoreIPCCodec::MakeServicePath( 0, 0 );
        // Expected: "/lap_ipc_0000_0000"
        ReportResult( "MakeServicePath(0,0) format",
                      path == "/lap_ipc_0000_0000",
                      std::string( "got: " ) + path );
    }

    void TestMakeMethodRequestPath()
    {
        auto path = CCoreIPCCodec::MakeMethodRequestPath( 0x1234, 0x0001 );
        // Expected: "/lap_ipc_method_req_1234_0001"
        ReportResult( "MakeMethodRequestPath format",
                      path == "/lap_ipc_method_req_1234_0001",
                      std::string( "got: " ) + path );
    }

    void TestMakeMethodResponsePath()
    {
        auto path = CCoreIPCCodec::MakeMethodResponsePath( 0x1234, 0x0001 );
        // Expected: "/lap_ipc_method_resp_1234_0001"
        ReportResult( "MakeMethodResponsePath format",
                      path == "/lap_ipc_method_resp_1234_0001",
                      std::string( "got: " ) + path );
    }

    void TestMakeMethodPathZero()
    {
        auto reqPath = CCoreIPCCodec::MakeMethodRequestPath( 0, 0 );
        auto respPath = CCoreIPCCodec::MakeMethodResponsePath( 0, 0 );
        ReportResult( "MakeMethodRequestPath(0,0)",
                      reqPath == "/lap_ipc_method_req_0000_0000",
                      std::string( "got: " ) + reqPath );
        ReportResult( "MakeMethodResponsePath(0,0)",
                      respPath == "/lap_ipc_method_resp_0000_0000",
                      std::string( "got: " ) + respPath );
    }

    void TestPathUniqueness()
    {
        auto a = CCoreIPCCodec::MakeServicePath( 0x1234, 0x0001 );
        auto b = CCoreIPCCodec::MakeServicePath( 0x1234, 0x0002 );
        auto c = CCoreIPCCodec::MakeServicePath( 0x5678, 0x0001 );
        ReportResult( "Path uniqueness (diff instanceId)", a != b );
        ReportResult( "Path uniqueness (diff serviceId)", a != c );
    }

    // ================================================================
    // Section 2: CCoreIPCCodec — Key Generation
    // ================================================================

    void TestMakeServiceKey()
    {
        auto key = CCoreIPCCodec::MakeServiceKey( 0x1234, 0x0001 );
        // Expected: (0x1234 << 32) | 0x0001 = 0x0000123400000001
        uint64_t expected = ( static_cast< uint64_t > ( 0x1234 ) << 32 ) | 0x0001;
        ReportResult( "MakeServiceKey value", key == expected,
                      "key mismatch" );
    }

    void TestMakeServiceKeyZero()
    {
        auto key = CCoreIPCCodec::MakeServiceKey( 0, 0 );
        ReportResult( "MakeServiceKey(0,0) == 0", key == 0U );
    }

    void TestMakeEventKey()
    {
        auto key = CCoreIPCCodec::MakeEventKey( 0x1234, 0x0001, 0x0101 );
        // Expected: (0x1234 << 32) | (0x0001 << 16) | 0x0101
        uint64_t expected = ( static_cast< uint64_t > ( 0x1234 ) << 32 )
                          | ( static_cast< uint64_t > ( 0x0001 ) << 16 )
                          | 0x0101;
        ReportResult( "MakeEventKey value", key == expected,
                      "key mismatch" );
    }

    void TestKeyUniqueness()
    {
        auto k1 = CCoreIPCCodec::MakeEventKey( 0x1234, 0x0001, 0x0101 );
        auto k2 = CCoreIPCCodec::MakeEventKey( 0x1234, 0x0001, 0x0102 );
        auto k3 = CCoreIPCCodec::MakeEventKey( 0x1234, 0x0002, 0x0101 );
        auto k4 = CCoreIPCCodec::MakeEventKey( 0x5678, 0x0001, 0x0101 );
        ReportResult( "EventKey uniqueness (diff eventId)", k1 != k2 );
        ReportResult( "EventKey uniqueness (diff instanceId)", k1 != k3 );
        ReportResult( "EventKey uniqueness (diff serviceId)", k1 != k4 );
    }

    // ================================================================
    // Section 3: CCoreIPCCodec — Event Encode/Decode
    // ================================================================

    void TestEventEncodeDecodeRoundtrip()
    {
        const uint32_t eventId = 0x0101;
        ByteBuffer payload { 0xAA, 0xBB, 0xCC, 0xDD };

        ByteBuffer encoded = CCoreIPCCodec::EncodeEventMessage( eventId, payload );

        // Verify encoded size = header(8) + payload(4)
        ReportResult( "Event encoded size",
                      encoded.size() == kCoreIPCEventHeaderSize + payload.size(),
                      "size mismatch" );

        // Decode
        uint32_t decodedEventId = 0;
        uint32_t decodedPayloadSize = 0;
        size_t decodedPayloadOffset = 0;

        bool ok = CCoreIPCCodec::DecodeEventMessage(
            encoded.data(), encoded.size(),
            decodedEventId, decodedPayloadSize, decodedPayloadOffset );

        ReportResult( "Event decode success", ok );
        ReportResult( "Event decode eventId match",
                      decodedEventId == eventId,
                      "eventId mismatch" );
        ReportResult( "Event decode payloadSize match",
                      decodedPayloadSize == static_cast< uint32_t > ( payload.size() ),
                      "payloadSize mismatch" );
        ReportResult( "Event decode payloadOffset",
                      decodedPayloadOffset == kCoreIPCEventHeaderSize,
                      "offset mismatch" );

        // Verify payload content
        ByteBuffer decodedPayload(
            encoded.begin() + static_cast< long > ( decodedPayloadOffset ),
            encoded.begin() + static_cast< long > ( decodedPayloadOffset + decodedPayloadSize ) );
        ReportResult( "Event payload roundtrip match",
                      decodedPayload == payload,
                      "payload content mismatch" );
    }

    void TestEventEncodeEmptyPayload()
    {
        const uint32_t eventId = 0xFFFF;
        ByteBuffer emptyPayload;

        ByteBuffer encoded = CCoreIPCCodec::EncodeEventMessage( eventId, emptyPayload );
        ReportResult( "Event encode empty payload size",
                      encoded.size() == kCoreIPCEventHeaderSize );

        uint32_t decodedEventId = 0;
        uint32_t decodedPayloadSize = 0;
        size_t decodedPayloadOffset = 0;

        bool ok = CCoreIPCCodec::DecodeEventMessage(
            encoded.data(), encoded.size(),
            decodedEventId, decodedPayloadSize, decodedPayloadOffset );

        ReportResult( "Event decode empty payload success", ok );
        ReportResult( "Event decode empty payload eventId",
                      decodedEventId == eventId );
        ReportResult( "Event decode empty payload size == 0",
                      decodedPayloadSize == 0U );
    }

    void TestEventDecodeTooSmall()
    {
        uint8_t tinyBuf[4] = { 0x01, 0x02, 0x03, 0x04 };
        uint32_t eventId = 0, payloadSize = 0;
        size_t offset = 0;

        bool ok = CCoreIPCCodec::DecodeEventMessage(
            tinyBuf, sizeof( tinyBuf ), eventId, payloadSize, offset );

        ReportResult( "Event decode too-small buffer → false", !ok );
    }

    void TestEventDecodeExactHeader()
    {
        // Encode with empty payload, then decode — should succeed
        ByteBuffer encoded = CCoreIPCCodec::EncodeEventMessage( 42, ByteBuffer{} );
        uint32_t eventId = 0, payloadSize = 0;
        size_t offset = 0;

        bool ok = CCoreIPCCodec::DecodeEventMessage(
            encoded.data(), encoded.size(), eventId, payloadSize, offset );

        ReportResult( "Event decode exact-header-only → true", ok );
        ReportResult( "Event decode exact-header eventId", eventId == 42U );
    }

    void TestEventDecodePayloadSizeLie()
    {
        // Encode a message, then truncate it so declared payload_size > actual
        ByteBuffer payload { 0x01, 0x02, 0x03, 0x04, 0x05 };
        ByteBuffer encoded = CCoreIPCCodec::EncodeEventMessage( 99, payload );

        // Truncate: remove last 2 bytes so payload is short
        encoded.resize( encoded.size() - 2 );

        uint32_t eventId = 0, payloadSize = 0;
        size_t offset = 0;

        bool ok = CCoreIPCCodec::DecodeEventMessage(
            encoded.data(), encoded.size(), eventId, payloadSize, offset );

        // Decode should fail: offset + payloadSize > size
        ReportResult( "Event decode truncated payload → false", !ok );
    }

    void TestEventLargeEventId()
    {
        // Max uint32_t event ID
        const uint32_t maxId = 0xFFFFFFFFU;
        ByteBuffer payload { 0x42 };

        ByteBuffer encoded = CCoreIPCCodec::EncodeEventMessage( maxId, payload );
        uint32_t decodedId = 0, payloadSize = 0;
        size_t offset = 0;

        bool ok = CCoreIPCCodec::DecodeEventMessage(
            encoded.data(), encoded.size(), decodedId, payloadSize, offset );

        ReportResult( "Event max eventId roundtrip", ok && decodedId == maxId );
    }

    // ================================================================
    // Section 4: CCoreIPCCodec — Method Encode/Decode
    // ================================================================

    void TestMethodEncodeDecodeRoundtrip()
    {
        const uint32_t methodId = 0x0202;
        const uint64_t token = 0xDEADBEEFCAFEBABEULL;
        const int32_t status = 0;
        ByteBuffer payload { 0x10, 0x20, 0x30 };

        ByteBuffer encoded = CCoreIPCCodec::EncodeMethodMessage(
            methodId, token, status, payload );

        ReportResult( "Method encoded size",
                      encoded.size() == kCoreIPCMethodHeaderSize + payload.size(),
                      "size mismatch" );

        uint32_t dMethodId = 0;
        uint64_t dToken = 0;
        int32_t dStatus = 0;
        uint32_t dPayloadSize = 0;
        size_t dOffset = 0;

        bool ok = CCoreIPCCodec::DecodeMethodMessage(
            encoded.data(), encoded.size(),
            dMethodId, dToken, dStatus, dPayloadSize, dOffset );

        ReportResult( "Method decode success", ok );
        ReportResult( "Method decode methodId",
                      dMethodId == methodId, "methodId mismatch" );
        ReportResult( "Method decode token",
                      dToken == token, "token mismatch" );
        ReportResult( "Method decode status",
                      dStatus == status, "status mismatch" );
        ReportResult( "Method decode payloadSize",
                      dPayloadSize == static_cast< uint32_t > ( payload.size() ),
                      "payloadSize mismatch" );
        ReportResult( "Method decode offset",
                      dOffset == kCoreIPCMethodHeaderSize, "offset mismatch" );

        ByteBuffer decodedPayload(
            encoded.begin() + static_cast< long > ( dOffset ),
            encoded.begin() + static_cast< long > ( dOffset + dPayloadSize ) );
        ReportResult( "Method payload roundtrip",
                      decodedPayload == payload, "payload mismatch" );
    }

    void TestMethodEncodeEmptyPayload()
    {
        ByteBuffer encoded = CCoreIPCCodec::EncodeMethodMessage( 1, 2, -1, ByteBuffer{} );
        ReportResult( "Method encode empty size",
                      encoded.size() == kCoreIPCMethodHeaderSize );

        uint32_t mid = 0;
        uint64_t tok = 0;
        int32_t st = 0;
        uint32_t ps = 0;
        size_t off = 0;

        bool ok = CCoreIPCCodec::DecodeMethodMessage(
            encoded.data(), encoded.size(), mid, tok, st, ps, off );

        ReportResult( "Method decode empty success", ok );
        ReportResult( "Method decode empty methodId", mid == 1U );
        ReportResult( "Method decode empty token", tok == 2U );
        ReportResult( "Method decode empty status == -1", st == -1 );
        ReportResult( "Method decode empty payloadSize == 0", ps == 0U );
    }

    void TestMethodDecodeTooSmall()
    {
        uint8_t buf[10] = {};
        uint32_t mid = 0;
        uint64_t tok = 0;
        int32_t st = 0;
        uint32_t ps = 0;
        size_t off = 0;

        bool ok = CCoreIPCCodec::DecodeMethodMessage(
            buf, sizeof( buf ), mid, tok, st, ps, off );

        ReportResult( "Method decode too-small → false", !ok );
    }

    void TestMethodNegativeStatus()
    {
        const int32_t negStatus = -42;
        ByteBuffer encoded = CCoreIPCCodec::EncodeMethodMessage( 0, 0, negStatus, ByteBuffer{} );

        uint32_t mid = 0;
        uint64_t tok = 0;
        int32_t st = 0;
        uint32_t ps = 0;
        size_t off = 0;

        bool ok = CCoreIPCCodec::DecodeMethodMessage(
            encoded.data(), encoded.size(), mid, tok, st, ps, off );

        ReportResult( "Method negative status roundtrip",
                      ok && st == negStatus, "status mismatch" );
    }

    void TestMethodLargeToken()
    {
        const uint64_t maxToken = 0xFFFFFFFFFFFFFFFFULL;
        ByteBuffer encoded = CCoreIPCCodec::EncodeMethodMessage( 7, maxToken, 0, ByteBuffer{} );

        uint32_t mid = 0;
        uint64_t tok = 0;
        int32_t st = 0;
        uint32_t ps = 0;
        size_t off = 0;

        bool ok = CCoreIPCCodec::DecodeMethodMessage(
            encoded.data(), encoded.size(), mid, tok, st, ps, off );

        ReportResult( "Method max token roundtrip",
                      ok && tok == maxToken, "token mismatch" );
    }

    void TestMethodDecodePayloadTruncated()
    {
        ByteBuffer payload { 0x01, 0x02, 0x03, 0x04 };
        ByteBuffer encoded = CCoreIPCCodec::EncodeMethodMessage( 1, 1, 0, payload );

        // Truncate: cut 2 bytes from the end
        encoded.resize( encoded.size() - 2 );

        uint32_t mid = 0;
        uint64_t tok = 0;
        int32_t st = 0;
        uint32_t ps = 0;
        size_t off = 0;

        bool ok = CCoreIPCCodec::DecodeMethodMessage(
            encoded.data(), encoded.size(), mid, tok, st, ps, off );

        ReportResult( "Method decode truncated payload → false", !ok );
    }

    // ================================================================
    // Section 5: CoreIPCConfig — Default Values
    // ================================================================

    void TestConfigDefaults()
    {
        CoreIPCConfig config;
        ReportResult( "Config default m_iMaxPayloadSize",
                      config.m_iMaxPayloadSize == 1024U );
        ReportResult( "Config default m_iSubscriberQueueCapacity",
                      config.m_iSubscriberQueueCapacity == 32U );
        ReportResult( "Config default m_iMaxChunks",
                      config.m_iMaxChunks == 64U );
        ReportResult( "Config default m_iListenerPollIntervalUs",
                      config.m_iListenerPollIntervalUs == 100U );
        ReportResult( "Config default m_iMethodCallTimeoutMs",
                      config.m_iMethodCallTimeoutMs == 1000U );
        ReportResult( "Config default m_iMethodPollIntervalUs",
                      config.m_iMethodPollIntervalUs == 100U );
    }

    // ================================================================
    // Section 6: Constants
    // ================================================================

    void TestConstants()
    {
        ReportResult( "kCoreIPCEventHeaderSize == 8",
                      kCoreIPCEventHeaderSize == 8U );
        ReportResult( "kCoreIPCMethodHeaderSize == 20",
                      kCoreIPCMethodHeaderSize == 20U );
    }

    // ================================================================
    // Section 7: CoreIPCBinding — Capability Queries
    // ================================================================

    void TestCapabilityQueries()
    {
        CoreIPCBinding binding;

        ReportResult( "GetName == \"coreipc\"",
                      std::string( binding.GetName() ) == "coreipc" );
        ReportResult( "GetPriority == 100",
                      binding.GetPriority() == 100U );
        ReportResult( "GetVersion == 0x010000",
                      binding.GetVersion() == 0x010000U );
        ReportResult( "SupportsZeroCopy == true",
                      binding.SupportsZeroCopy() == true );
        ReportResult( "SupportsService(0x1234) == true",
                      binding.SupportsService( 0x1234 ) == true );
        ReportResult( "SupportsService(0) == true",
                      binding.SupportsService( 0 ) == true );
    }

    // ================================================================
    // Section 8: CoreIPCBinding — Pre-Init Error Paths
    // ================================================================

    void TestPreInitErrors()
    {
        CoreIPCBinding binding;
        // All operations on un-initialized binding should fail with error

        auto offer = binding.OfferService( 0x1234, 0x0001 );
        ReportResult( "OfferService before Init → error", !offer.HasValue() );

        auto stop = binding.StopOfferService( 0x1234, 0x0001 );
        ReportResult( "StopOfferService before Init → error", !stop.HasValue() );

        auto find = binding.FindService( 0x1234 );
        ReportResult( "FindService before Init → error", !find.HasValue() );

        ByteBuffer data { 0x01 };
        auto send = binding.SendEvent( 0x1234, 0x0001, 0x0101, data );
        ReportResult( "SendEvent before Init → error", !send.HasValue() );

        auto sub = binding.SubscribeEvent< ByteBuffer >( 0x1234, 0x0001, 0x0101,
            []( uint64_t, uint64_t, uint32_t, const ByteBuffer& ) {} );
        ReportResult( "SubscribeEvent before Init → error", !sub.HasValue() );

        auto unsub = binding.UnsubscribeEvent( 0x1234, 0x0001, 0x0101 );
        ReportResult( "UnsubscribeEvent before Init → error", !unsub.HasValue() );

        auto call = binding.CallMethod< ByteBuffer >( 0x1234, 0x0001, 0x0202, data );
        ReportResult( "CallMethod before Init → error", !call.HasValue() );

        auto reg = binding.RegisterMethod< ByteBuffer, ByteBuffer >( 0x1234, 0x0001, 0x0202,
            []( uint64_t, uint64_t, uint32_t, const ByteBuffer& ) -> ByteBuffer {
                return {};
            } );
        ReportResult( "RegisterMethod before Init → error", !reg.HasValue() );

        auto get = binding.GetField< ByteBuffer >( 0x1234, 0x0001, 0x0303 );
        ReportResult( "GetField before Init → error", !get.HasValue() );

        auto set = binding.SetField( 0x1234, 0x0001, 0x0303, data );
        ReportResult( "SetField before Init → error", !set.HasValue() );
    }

    // ================================================================
    // Section 9: CoreIPCBinding — Lifecycle Edge Cases
    //   (requires CRegistryDispatcher)
    // ================================================================

    void TestLifecycleEdgeCases( CRegistryDispatcher& dispatcher )
    {
        (void)dispatcher;

        // --- Double Initialize ---
        {
            CoreIPCBinding binding;
            auto init1 = binding.Initialize();
            ReportResult( "Lifecycle: first Initialize OK", init1.HasValue(),
                          init1.HasValue() ? "" : init1.Error().Message() );

            auto init2 = binding.Initialize();
            // Should succeed idempotently or return already-init
            // Either way it should not crash
            ReportResult( "Lifecycle: double Initialize no crash", true );

            binding.Shutdown();
        }

        // --- Double Shutdown ---
        {
            CoreIPCBinding binding;
            auto init = binding.Initialize();
            ReportResult( "Lifecycle: init for double-shutdown",
                          init.HasValue(),
                          init.HasValue() ? "" : init.Error().Message() );

            auto shut1 = binding.Shutdown();
            ReportResult( "Lifecycle: first Shutdown OK", shut1.HasValue(),
                          shut1.HasValue() ? "" : shut1.Error().Message() );

            auto shut2 = binding.Shutdown();
            // Should not crash — either succeeds or returns error
            ReportResult( "Lifecycle: double Shutdown no crash", true );
        }

        // --- Shutdown without Initialize ---
        {
            CoreIPCBinding binding;
            auto shut = binding.Shutdown();
            // Should not crash
            ReportResult( "Lifecycle: Shutdown without Init no crash", true );
        }

        // --- Operations after Shutdown ---
        {
            CoreIPCBinding binding;
            binding.Initialize();
            std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
            binding.Shutdown();
            std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );

            auto offer = binding.OfferService( 0x1234, 0x0001 );
            ReportResult( "Post-shutdown OfferService → error",
                          !offer.HasValue() );

            ByteBuffer data { 0x01 };
            auto send = binding.SendEvent( 0x1234, 0x0001, 0x01, data );
            ReportResult( "Post-shutdown SendEvent → error",
                          !send.HasValue() );

            auto call = binding.CallMethod< ByteBuffer >( 0x1234, 0x0001, 0x01, data );
            ReportResult( "Post-shutdown CallMethod → error",
                          !call.HasValue() );
        }

        // Let IPC channels settle before dispatcher shutdown
        std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
    }

    // ================================================================
    // Section 10: CoreIPCBinding — Metrics Default
    // ================================================================

    void TestMetricsDefault()
    {
        CoreIPCBinding binding;
        auto metrics = binding.GetMetrics();

        ReportResult( "Default metrics messages_sent == 0",
                      metrics.messagesSent == 0U );
        ReportResult( "Default metrics messages_received == 0",
                      metrics.messagesReceived == 0U );
        ReportResult( "Default metrics bytes_sent == 0",
                      metrics.bytesSent == 0U );
        ReportResult( "Default metrics bytes_received == 0",
                      metrics.bytesReceived == 0U );
    }

    // ================================================================
    // Section 11: C Export Functions
    // ================================================================

    void TestCExportFunctions()
    {
        // C export functions declared in CoreIPCBinding.cpp
        auto* pBinding = ::CreateBindingInstance();
        ReportResult( "CreateBindingInstance != null", pBinding != nullptr );

        if ( pBinding ) {
            ReportResult( "C-export GetName == \"coreipc\"",
                          std::string( pBinding->GetName() ) == "coreipc" );
            ::DestroyBindingInstance( pBinding );
            ReportResult( "DestroyBindingInstance no crash", true );
        }
    }

} // anonymous namespace

int main()
{
    std::cout << "========================================" << std::endl;
    std::cout << "  CoreIPC Binding Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    // ------------------------------------------------------------------
    // Part A: Pure-static tests (no IPC infrastructure needed)
    // ------------------------------------------------------------------
    std::cout << "\n--- Section 1: Path Generation ---" << std::endl;
    TestMakeServicePath();
    TestMakeServicePathZero();
    TestMakeMethodRequestPath();
    TestMakeMethodResponsePath();
    TestMakeMethodPathZero();
    TestPathUniqueness();

    std::cout << "\n--- Section 2: Key Generation ---" << std::endl;
    TestMakeServiceKey();
    TestMakeServiceKeyZero();
    TestMakeEventKey();
    TestKeyUniqueness();

    std::cout << "\n--- Section 3: Event Encode/Decode ---" << std::endl;
    TestEventEncodeDecodeRoundtrip();
    TestEventEncodeEmptyPayload();
    TestEventDecodeTooSmall();
    TestEventDecodeExactHeader();
    TestEventDecodePayloadSizeLie();
    TestEventLargeEventId();

    std::cout << "\n--- Section 4: Method Encode/Decode ---" << std::endl;
    TestMethodEncodeDecodeRoundtrip();
    TestMethodEncodeEmptyPayload();
    TestMethodDecodeTooSmall();
    TestMethodNegativeStatus();
    TestMethodLargeToken();
    TestMethodDecodePayloadTruncated();

    std::cout << "\n--- Section 5: CoreIPCConfig Defaults ---" << std::endl;
    TestConfigDefaults();

    std::cout << "\n--- Section 6: Constants ---" << std::endl;
    TestConstants();

    // ------------------------------------------------------------------
    // Part B: Binding queries (no IPC, just object construction)
    // ------------------------------------------------------------------
    std::cout << "\n--- Section 7: Capability Queries ---" << std::endl;
    TestCapabilityQueries();

    std::cout << "\n--- Section 8: Pre-Init Error Paths ---" << std::endl;
    TestPreInitErrors();

    std::cout << "\n--- Section 10: Metrics Default ---" << std::endl;
    TestMetricsDefault();

    std::cout << "\n--- Section 11: C Export Functions ---" << std::endl;
    TestCExportFunctions();

    // ------------------------------------------------------------------
    // Part C: Lifecycle tests (need CRegistryDispatcher)
    // ------------------------------------------------------------------
    std::cout << "\n--- Section 9: Lifecycle Edge Cases ---" << std::endl;
    std::cout << "  Starting CRegistryDispatcher..." << std::endl;

    CRegistryDispatcher dispatcher;
    auto dispInit = dispatcher.Initialize();
    if ( !dispInit.HasValue() ) {
        std::cerr << "  [SKIP] Dispatcher init failed: "
                  << dispInit.Error().Message() << std::endl;
        std::cerr << "  Skipping lifecycle tests" << std::endl;
    } else {
        std::thread dispThread( [&dispatcher]() {
            dispatcher.Run();
        } );

        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
        TestLifecycleEdgeCases( dispatcher );

        dispatcher.Shutdown();
        if ( dispThread.joinable() ) { dispThread.join(); }
    }

    // ------------------------------------------------------------------
    // Final Report
    // ------------------------------------------------------------------
    std::cout << "\n========================================" << std::endl;
    std::cout << "  Results: " << g_iTestsPassed << " passed, "
              << g_iTestsFailed << " failed" << std::endl;
    std::cout << "========================================" << std::endl;

    return ( g_iTestsFailed == 0 ) ? 0 : 1;
}
