/**
 * @file        test_someip_stub.cpp
 * @brief       SOME/IP binding stub — verify all interfaces return kCommunicationFailure
 * @date        2026/02/07
 */

#include "SomeIpBinding.hpp"
#include "ComTypes.hpp"

#include <cstdio>
#include <cstdlib>

using namespace lap::com::binding;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK( cond, msg )                                              \
    do {                                                                \
        if ( cond ) {                                                   \
            std::printf( "  [PASS] %s\n", msg );                       \
            ++g_passed;                                                 \
        } else {                                                        \
            std::printf( "  [FAIL] %s\n", msg );                       \
            ++g_failed;                                                 \
        }                                                               \
    } while ( false )

int main()
{
    std::printf( "========================================\n" );
    std::printf( "  SOME/IP Binding Stub Test\n" );
    std::printf( "========================================\n\n" );

    SomeIpBinding binding;

    // Phase 0 — Capability queries (before init)
    std::printf( "[Phase 0] Capability queries...\n" );
    CHECK( std::string( binding.GetName() ) == "SOME/IP", "GetName() == SOME/IP" );
    CHECK( binding.GetVersion() == 0x00010000U,           "GetVersion()" );
    CHECK( binding.GetPriority() == 60U,                  "GetPriority() == 60" );
    CHECK( binding.SupportsZeroCopy() == false,           "SupportsZeroCopy() == false" );
    CHECK( binding.SupportsService( 0x1234 ) == false,    "SupportsService() == false" );

    // Phase 1 — Lifecycle
    std::printf( "\n[Phase 1] Lifecycle...\n" );
    auto initResult = binding.Initialize();
    CHECK( static_cast< bool > ( initResult ), "Initialize() succeeds" );

    auto initAgain = binding.Initialize();
    CHECK( static_cast< bool > ( initAgain ), "Initialize() idempotent" );

    // Phase 2 — Service management stubs
    std::printf( "\n[Phase 2] Service management stubs...\n" );
    auto offer = binding.OfferService( 0x100, 0x01 );
    CHECK( !offer, "OfferService returns error" );

    auto stop = binding.StopOfferService( 0x100, 0x01 );
    CHECK( !stop, "StopOfferService returns error" );

    auto find = binding.FindService( 0x100 );
    CHECK( !find, "FindService returns error" );

    auto startFind = binding.StartFindService( 0x100, nullptr );
    CHECK( !startFind, "StartFindService returns error" );

    auto stopFind = binding.StopFindService( 42 );
    CHECK( !stopFind, "StopFindService returns error" );

    // Phase 3 — Event communication stubs
    std::printf( "\n[Phase 3] Event communication stubs...\n" );
    ByteBuffer data = { 0x01, 0x02, 0x03 };
    auto send = binding.SendEvent( 0x100, 0x01, 1, data );
    CHECK( !send, "SendEvent returns error" );

    auto sub = binding.SubscribeEvent< ByteBuffer >( 0x100, 0x01, 1, nullptr );
    CHECK( !sub, "SubscribeEvent returns error" );

    auto unsub = binding.UnsubscribeEvent( 0x100, 0x01, 1 );
    CHECK( !unsub, "UnsubscribeEvent returns error" );

    // Phase 4 — Method communication stubs
    std::printf( "\n[Phase 4] Method communication stubs...\n" );
    auto call = binding.CallMethod< ByteBuffer >( 0x100, 0x01, 1, data );
    CHECK( !call, "CallMethod returns error" );

    auto asyncCall = binding.CallMethodAsync< ByteBuffer >( 0x100, 0x01, 1, data );
    CHECK( asyncCall.valid(), "CallMethodAsync returns valid Future" );
    auto asyncResult = asyncCall.GetResult();
    CHECK( !asyncResult, "CallMethodAsync Future resolves to error" );

    auto reg = binding.RegisterMethod< ByteBuffer, ByteBuffer >( 0x100, 0x01, 1, nullptr );
    CHECK( !reg, "RegisterMethod returns error" );

    // Phase 5 — Field communication stubs
    std::printf( "\n[Phase 5] Field communication stubs...\n" );
    auto getf = binding.GetField< ByteBuffer >( 0x100, 0x01, 1 );
    CHECK( !getf, "GetField returns error" );

    auto setf = binding.SetField( 0x100, 0x01, 1, data );
    CHECK( !setf, "SetField returns error" );

    auto subField = binding.SubscribeFieldNotification< ByteBuffer >( 0x100, 0x01, 1, nullptr );
    CHECK( !subField, "SubscribeFieldNotification returns error" );

    auto unsubField = binding.UnsubscribeFieldNotification( 0x100, 0x01, 1 );
    CHECK( !unsubField, "UnsubscribeFieldNotification returns error" );

    // Phase 6 — Metrics
    std::printf( "\n[Phase 6] Metrics...\n" );
    auto metrics = binding.GetMetrics();
    CHECK( metrics.messagesSent == 0, "No messages sent (stub)" );
    CHECK( metrics.messagesReceived == 0, "No messages received (stub)" );

    // Phase 7 — Shutdown
    std::printf( "\n[Phase 7] Shutdown...\n" );
    auto shutResult = binding.Shutdown();
    CHECK( static_cast< bool > ( shutResult ), "Shutdown() succeeds" );

    auto shutAgain = binding.Shutdown();
    CHECK( static_cast< bool > ( shutAgain ), "Shutdown() idempotent" );

    std::printf( "\n========================================\n" );
    std::printf( "  Results: %d passed, %d failed\n", g_passed, g_failed );
    std::printf( "========================================\n" );

    return g_failed > 0 ? 1 : 0;
}
