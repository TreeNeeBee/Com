/**
 * @file        lap-registry-ctl.cpp
 * @author      LightAP Development Team
 * @brief       Registry diagnostic & management CLI tool
 * @date        2026/02/06
 * @details     Developer / system-analyst tool for inspecting and manipulating
 *              the LightAP service registry.
 *
 *              Subcommands:
 *                list       - List all active service slots
 *                status     - Show registry summary and statistics
 *                register   - Register a service via Core IPC (standard flow)
 *                unregister - Unregister a service via Core IPC (standard flow)
 *                inspect    - Dump raw slot data for a given index
 *                watch      - Continuously monitor slot changes
 *
 * @copyright   Copyright (c) 2026
 * @note        Integration architecture:
 *              - Read operations (list/status/inspect/watch):
 *                CServiceRegistry::InitializeFromSocket() → read-only mmap
 *              - Write operations (register/unregister):
 *                CRegistryProxy → Core IPC MPSC → CRegistryDispatcher → slot write
 *              - Logging: LAP_COM_LOG_* macros → LogAndTrace module
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §2.4
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/06  <td>1.0      <td>Aii             <td>Initial standalone implementation
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Integrate Core IPC + LogAndTrace
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CServiceRegistry.hpp"
#include "CRegistryProxy.hpp"
#include "RegistryIpcMessage.hpp"
#include "ServiceSlot.hpp"
#include "ComTypes.hpp"

// ==================== Standard Library Headers ====================
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

// ==================== Namespace Imports ====================
using namespace lap::com::registry;
using namespace lap::com;
using lap::core::UInt16;
using lap::core::UInt32;
using lap::core::UInt64;
using lap::core::Int32;
using lap::core::Result;
using lap::core::Optional;

// =============================================================================
// Globals
// =============================================================================

static volatile sig_atomic_t g_running = 1;

static void SignalHandler( int )
{
    g_running = 0;
}

// =============================================================================
// Color helpers
// =============================================================================

static bool g_color = true;

static const char* Clr( const char* code )
{
    return g_color ? code : "";
}

static constexpr const char* kReset   = "\033[0m";
static constexpr const char* kBold    = "\033[1m";
static constexpr const char* kRed     = "\033[0;31m";
static constexpr const char* kGreen   = "\033[0;32m";
static constexpr const char* kYellow  = "\033[1;33m";
static constexpr const char* kBlue    = "\033[0;34m";
static constexpr const char* kCyan    = "\033[0;36m";
static constexpr const char* kDim     = "\033[2m";

// =============================================================================
// Helpers
// =============================================================================

static const char* StatusStr( UInt32 s )
{
    switch ( static_cast< SlotStatus > ( s ) )
    {
        case SlotStatus::kIdle:          return "IDLE";
        case SlotStatus::kActive:        return "ACTIVE";
        case SlotStatus::kUnregistering: return "UNREG";
        default:                         return "???";
    }
}

static const char* StatusColor( UInt32 s )
{
    switch ( static_cast< SlotStatus > ( s ) )
    {
        case SlotStatus::kActive:        return kGreen;
        case SlotStatus::kUnregistering: return kYellow;
        default:                         return kDim;
    }
}

static std::string FormatTimestamp( UInt64 ns )
{
    if ( ns == 0 ) return "-";
    const time_t sec = static_cast< time_t > ( ns / 1000000000ULL );
    const UInt32 ms  = static_cast< UInt32 > ( ( ns % 1000000000ULL ) / 1000000ULL );
    struct tm tm {};
    localtime_r( &sec, &tm );
    char buf[32];
    std::snprintf( buf, sizeof( buf ), "%02d:%02d:%02d.%03u",
                   tm.tm_hour, tm.tm_min, tm.tm_sec, ms );
    return std::string( buf );
}

static std::string AsilLevelStr( UInt64 instanceId )
{
    const UInt32 low32 = static_cast< UInt32 > ( instanceId );
    const UInt32 asil  = ( low32 >> 28 ) & 0x7;
    static const char* names[] = { "QM", "A", "B", "C", "D", "?", "?", "?" };
    return names[asil];
}

static std::string DomainStr( UInt64 instanceId )
{
    const UInt32 low32  = static_cast< UInt32 > ( instanceId );
    const UInt32 domain = ( low32 >> 24 ) & 0xF;
    static const char* names[] = {
        "Percept", "Control", "Infotain", "Diag",
        "Platform", "OEM", "Rsvd6", "Rsvd7",
        "Rsvd8", "Rsvd9", "Rsvd10", "Rsvd11",
        "Rsvd12", "Rsvd13", "Rsvd14", "Rsvd15"
    };
    return names[domain];
}

static bool IsProcessAlive( Int32 pid )
{
    if ( pid <= 0 ) return false;
    return ( kill( pid, 0 ) == 0 );
}

/**
 * @brief Format a 64-bit value as hex string for logging
 * @note LAP_COM_LOG stream does not support std::hex manipulator
 */
static std::string Hex64( UInt64 val )
{
    char buf[20];
    std::snprintf( buf, sizeof( buf ), "0x%llx",
                   static_cast< unsigned long long > ( val ) );
    return std::string( buf );
}

// =============================================================================
// Subcommand: list
// =============================================================================

static int CmdList( CServiceRegistry& registry, bool showAll )
{
    LAP_COM_LOG_DEBUG << "CmdList: showAll=" << showAll;

    // Header
    std::printf( "%s%-6s %-8s %-10s %-18s %-8s %-6s %-16s %-30s %-5s %-12s%s\n",
                 Clr( kBold ),
                 "SLOT", "STATUS", "SVC_ID", "INST_ID", "VER", "ASIL",
                 "BINDING", "ENDPOINT", "PID", "HEARTBEAT",
                 Clr( kReset ) );

    std::printf( "%s%s%s\n", Clr( kDim ),
                 "───── ──────── ────────── ────────────────── ──────── ────── "
                 "──────────────── ────────────────────────────── ───── ────────────",
                 Clr( kReset ) );

    int count = 0;
    for ( UInt32 i = 0; i < RegistryConfig::kMaxSlots; ++i )
    {
        auto optSlot = registry.ReadSlot( i );
        if ( !optSlot.has_value() ) continue;

        const ServiceSlot& s = optSlot.value();
        const bool active = s.IsActive();
        const bool unreg  = ( s.m_status.load( std::memory_order_acquire ) ==
                              static_cast< UInt32 > ( SlotStatus::kUnregistering ) );

        if ( !showAll && !active && !unreg ) continue;

        const UInt32 statusVal = s.m_status.load( std::memory_order_acquire );
        const bool pidAlive = IsProcessAlive( s.m_ownerPid );

        std::printf( "%-6u %s%-8s%s 0x%08llx 0x%016llx %-4u.%-3u %-6s %-16s %-30s %s%-5d%s %-12s\n",
                     i,
                     Clr( StatusColor( statusVal ) ),
                     StatusStr( statusVal ),
                     Clr( kReset ),
                     static_cast< unsigned long long > ( s.m_serviceId ),
                     static_cast< unsigned long long > ( s.m_instanceId ),
                     s.m_majorVersion, s.m_minorVersion,
                     AsilLevelStr( s.m_instanceId ).c_str(),
                     s.m_bindingType,
                     s.m_endpoint,
                     pidAlive ? Clr( kGreen ) : ( s.m_ownerPid > 0 ? Clr( kRed ) : "" ),
                     s.m_ownerPid,
                     Clr( kReset ),
                     FormatTimestamp( s.m_lastHeartbeatNs ).c_str() );
        ++count;
    }

    if ( count == 0 )
    {
        std::printf( "%s  (no active services)%s\n", Clr( kDim ), Clr( kReset ) );
    }

    LAP_COM_LOG_INFO << "CmdList: listed " << count << " service(s)";
    return 0;
}

// =============================================================================
// Subcommand: status
// =============================================================================

static int CmdStatus( CServiceRegistry& registry, const char* regType, const char* socketPath )
{
    LAP_COM_LOG_DEBUG << "CmdStatus: type=" << regType << " socket=" << socketPath;

    UInt32 activeCount   = 0;
    UInt32 unregCount    = 0;
    UInt32 zombieCount   = 0;
    UInt32 qmCount       = 0;
    UInt32 asilCount     = 0;
    UInt64 latestHb      = 0;
    UInt64 oldestHb      = UINT64_MAX;

    for ( UInt32 i = 1; i < RegistryConfig::kMaxSlots; ++i )
    {
        auto optSlot = registry.ReadSlot( i );
        if ( !optSlot.has_value() ) continue;

        const ServiceSlot& s = optSlot.value();
        const UInt32 statusVal = s.m_status.load( std::memory_order_acquire );

        if ( statusVal == static_cast< UInt32 > ( SlotStatus::kActive ) )
        {
            ++activeCount;

            // Check QM vs ASIL by service ID range
            if ( s.m_serviceId >= RegistryConfig::kAsilServiceIdMin &&
                 s.m_serviceId <= RegistryConfig::kAsilServiceIdMax )
            {
                ++asilCount;
            }
            else
            {
                ++qmCount;
            }

            // Zombie detection
            if ( s.m_ownerPid > 0 && !IsProcessAlive( s.m_ownerPid ) )
                ++zombieCount;

            // Heartbeat range
            if ( s.m_lastHeartbeatNs > 0 )
            {
                if ( s.m_lastHeartbeatNs > latestHb )  latestHb  = s.m_lastHeartbeatNs;
                if ( s.m_lastHeartbeatNs < oldestHb )   oldestHb  = s.m_lastHeartbeatNs;
            }
        }
        else if ( statusVal == static_cast< UInt32 > ( SlotStatus::kUnregistering ) )
        {
            ++unregCount;
        }
    }

    if ( oldestHb == UINT64_MAX ) oldestHb = 0;

    // Check broadcast slot (1023)
    auto optBcast = registry.ReadSlot( RegistryConfig::kBroadcastSlot );
    const bool bcastActive = optBcast.has_value() &&
        optBcast.value().m_status.load( std::memory_order_acquire ) ==
        static_cast< UInt32 > ( SlotStatus::kActive );

    // Discovery Server state
    std::string dsState = "unknown";
    {
        FILE* fp = fopen( "/run/lap/discovery_state", "r" );
        if ( fp )
        {
            char buf[32] = {};
            if ( fgets( buf, sizeof( buf ), fp ) )
            {
                buf[strcspn( buf, "\n" )] = '\0';
                dsState = buf;
            }
            fclose( fp );
        }
    }

    const UInt32 usableSlots = RegistryConfig::kMaxSlots - 2;  // exclude slot 0 and 1023

    std::printf( "\n" );
    std::printf( "%s══════════════════════════════════════════%s\n", Clr( kBold ), Clr( kReset ) );
    std::printf( "%s  LightAP Registry Status%s\n", Clr( kBold ), Clr( kReset ) );
    std::printf( "%s══════════════════════════════════════════%s\n", Clr( kBold ), Clr( kReset ) );
    std::printf( "\n" );

    std::printf( "  Registry Type:    %s%s%s\n",
                 Clr( kCyan ), regType, Clr( kReset ) );
    std::printf( "  Socket:           %s\n", socketPath );
    std::printf( "  Shared Memory:    %u slots × %zu bytes = %zu KB\n",
                 RegistryConfig::kMaxSlots, RegistryConfig::kSlotSize,
                 RegistryConfig::kRegistrySize / 1024 );
    std::printf( "\n" );

    // Slot utilization
    std::printf( "  %sSlot Utilization:%s\n", Clr( kBold ), Clr( kReset ) );
    std::printf( "    Active:         %s%u%s / %u (%.1f%%)\n",
                 Clr( kGreen ), activeCount, Clr( kReset ),
                 usableSlots,
                 static_cast< double > ( activeCount ) / usableSlots * 100.0 );
    std::printf( "    Unregistering:  %u\n", unregCount );
    std::printf( "    Free:           %u\n", usableSlots - activeCount - unregCount );
    std::printf( "    QM services:    %u\n", qmCount );
    std::printf( "    ASIL services:  %u\n", asilCount );
    std::printf( "\n" );

    // Health
    std::printf( "  %sHealth:%s\n", Clr( kBold ), Clr( kReset ) );
    if ( zombieCount > 0 )
    {
        std::printf( "    Zombie slots:   %s%u%s (owner PID dead)\n",
                     Clr( kRed ), zombieCount, Clr( kReset ) );
        LAP_COM_LOG_WARN << "CmdStatus: " << zombieCount << " zombie slot(s) detected";
    }
    else
    {
        std::printf( "    Zombie slots:   %s0%s\n", Clr( kGreen ), Clr( kReset ) );
    }
    std::printf( "    Latest HB:      %s\n", FormatTimestamp( latestHb ).c_str() );
    std::printf( "    Oldest HB:      %s\n", FormatTimestamp( oldestHb ).c_str() );
    std::printf( "    Broadcast slot:  %s%s%s\n",
                 bcastActive ? Clr( kGreen ) : Clr( kDim ),
                 bcastActive ? "ACTIVE" : "IDLE",
                 Clr( kReset ) );
    std::printf( "\n" );

    // Discovery Server
    std::printf( "  %sDiscovery Server:%s\n", Clr( kBold ), Clr( kReset ) );
    if ( dsState == "CLIENT" )
    {
        std::printf( "    Mode:           %s● CLIENT%s (centralized)\n",
                     Clr( kGreen ), Clr( kReset ) );
    }
    else if ( dsState == "SIMPLE" )
    {
        std::printf( "    Mode:           %s● SIMPLE%s (PDP/EDP fallback)\n",
                     Clr( kYellow ), Clr( kReset ) );
    }
    else
    {
        std::printf( "    Mode:           %s● %s%s\n",
                     Clr( kDim ), dsState.c_str(), Clr( kReset ) );
    }
    std::printf( "\n" );

    // Reserved slots
    std::printf( "  %sReserved Slots:%s\n", Clr( kBold ), Clr( kReset ) );
    std::printf( "    Slot 0:         Reserved (prohibited)\n" );

    auto dumpReserved = [&]( UInt32 idx, const char* label )
    {
        auto opt = registry.ReadSlot( idx );
        UInt32 st = opt.has_value()
            ? opt.value().m_status.load( std::memory_order_acquire ) : 0;
        std::printf( "    Slot %-4u       %-20s [%s%s%s]\n",
                     idx, label,
                     Clr( StatusColor( st ) ), StatusStr( st ), Clr( kReset ) );
    };

    dumpReserved( 1,    "SD Proxy Primary" );
    dumpReserved( 512,  "SD Proxy Backup" );
    dumpReserved( RegistryConfig::kBroadcastSlot, "Broadcast" );
    std::printf( "\n" );

    LAP_COM_LOG_INFO << "CmdStatus: active=" << activeCount
                     << " zombie=" << zombieCount
                     << " ds_mode=" << dsState;
    return 0;
}

// =============================================================================
// Subcommand: register (via CRegistryProxy → Core IPC standard flow)
// =============================================================================

static int CmdRegister( CRegistryProxy& proxy,
                        UInt64 serviceId, UInt64 instanceId,
                        UInt32 majorVer, UInt32 minorVer,
                        const char* bindingType, const char* endpoint,
                        UInt32 timeoutMs )
{
    LAP_COM_LOG_INFO << "CmdRegister: svc=" << Hex64( serviceId )
                     << " inst=" << Hex64( instanceId )
                     << " ver=" << majorVer << "." << minorVer
                     << " binding=" << bindingType
                     << " endpoint=" << endpoint
                     << " timeout=" << timeoutMs << "ms";

    std::printf( "Registering service 0x%llx via Core IPC...\n",
                 static_cast< unsigned long long > ( serviceId ) );

    auto result = proxy.RegisterService(
        serviceId, instanceId, majorVer, minorVer,
        bindingType, endpoint, timeoutMs );

    if ( !result.HasValue() )
    {
        std::fprintf( stderr, "%s✗%s Registration failed\n",
                      Clr( kRed ), Clr( kReset ) );
        LAP_COM_LOG_ERROR << "CmdRegister: failed for svc=" << Hex64( serviceId );
        return 1;
    }

    const UInt32 assignedSlot = result.Value();

    std::printf( "%s✓%s Registered via Core IPC (standard flow)\n",
                 Clr( kGreen ), Clr( kReset ) );
    std::printf( "  Service ID:  0x%llx\n", static_cast< unsigned long long > ( serviceId ) );
    std::printf( "  Instance:    0x%llx\n", static_cast< unsigned long long > ( instanceId ) );
    std::printf( "  Version:     %u.%u\n", majorVer, minorVer );
    std::printf( "  Binding:     %s\n", bindingType );
    std::printf( "  Endpoint:    %s\n", endpoint );
    std::printf( "  Slot:        %u\n", assignedSlot );
    std::printf( "  PID:         %d\n", getpid() );

    LAP_COM_LOG_INFO << "CmdRegister: success, assigned slot=" << assignedSlot;
    return 0;
}

// =============================================================================
// Subcommand: unregister (via CRegistryProxy → Core IPC standard flow)
// =============================================================================

static int CmdUnregister( CRegistryProxy& proxy, UInt64 serviceId, UInt32 timeoutMs )
{
    LAP_COM_LOG_INFO << "CmdUnregister: svc=" << Hex64( serviceId )
                     << " timeout=" << timeoutMs << "ms";

    std::printf( "Unregistering service 0x%llx via Core IPC...\n",
                 static_cast< unsigned long long > ( serviceId ) );

    auto result = proxy.UnregisterService( serviceId, timeoutMs );

    if ( !result.HasValue() )
    {
        std::fprintf( stderr, "%s✗%s Unregistration failed\n",
                      Clr( kRed ), Clr( kReset ) );
        LAP_COM_LOG_ERROR << "CmdUnregister: failed for svc=" << Hex64( serviceId );
        return 1;
    }

    std::printf( "%s✓%s Unregistered via Core IPC (standard flow)\n",
                 Clr( kGreen ), Clr( kReset ) );
    std::printf( "  Service ID:  0x%llx\n", static_cast< unsigned long long > ( serviceId ) );

    LAP_COM_LOG_INFO << "CmdUnregister: success for svc=" << Hex64( serviceId );
    return 0;
}

// =============================================================================
// Fallback: register via direct mmap write (when IPC dispatcher is unavailable)
// =============================================================================

static int CmdRegisterDirect( CServiceRegistry& registry, UInt64 serviceId,
                              UInt64 instanceId, UInt32 majorVer, UInt32 minorVer,
                              const char* bindingType, const char* endpoint )
{
    UInt32 slotIndex = static_cast< UInt32 > ( serviceId & 1023 );
    if ( slotIndex == 0 || slotIndex >= RegistryConfig::kMaxSlots )
    {
        std::fprintf( stderr, "%s✗%s Invalid slot %u for svc 0x%llx\n",
                      Clr( kRed ), Clr( kReset ), slotIndex,
                      static_cast< unsigned long long > ( serviceId ) );
        return 1;
    }

    LAP_COM_LOG_INFO << "CmdRegisterDirect: svc=" << Hex64( serviceId )
                     << " slot=" << slotIndex;

    std::printf( "Registering service 0x%llx via direct mmap (fallback)...\n",
                 static_cast< unsigned long long > ( serviceId ) );

    auto result = registry.RegisterService(
        slotIndex, serviceId, instanceId, majorVer, minorVer,
        bindingType, endpoint );

    if ( !result.HasValue() )
    {
        std::fprintf( stderr, "%s✗%s Registration failed (slot may be occupied)\n",
                      Clr( kRed ), Clr( kReset ) );
        LAP_COM_LOG_ERROR << "CmdRegisterDirect: failed for slot=" << slotIndex;
        return 1;
    }

    std::printf( "%s✓%s Registered via direct mmap (fallback)\n",
                 Clr( kGreen ), Clr( kReset ) );
    std::printf( "  Service ID:  0x%llx\n", static_cast< unsigned long long > ( serviceId ) );
    std::printf( "  Instance:    0x%llx\n", static_cast< unsigned long long > ( instanceId ) );
    std::printf( "  Version:     %u.%u\n", majorVer, minorVer );
    std::printf( "  Binding:     %s\n", bindingType );
    std::printf( "  Endpoint:    %s\n", endpoint );
    std::printf( "  Slot:        %u\n", slotIndex );
    std::printf( "  PID:         %d\n", getpid() );

    LAP_COM_LOG_INFO << "CmdRegisterDirect: success, slot=" << slotIndex;
    return 0;
}

// =============================================================================
// Fallback: unregister via direct mmap write
// =============================================================================

static int CmdUnregisterDirect( CServiceRegistry& registry, UInt64 serviceId )
{
    UInt32 slotIndex = static_cast< UInt32 > ( serviceId & 1023 );
    if ( slotIndex == 0 || slotIndex >= RegistryConfig::kMaxSlots )
    {
        std::fprintf( stderr, "%s✗%s Invalid slot %u for svc 0x%llx\n",
                      Clr( kRed ), Clr( kReset ), slotIndex,
                      static_cast< unsigned long long > ( serviceId ) );
        return 1;
    }

    LAP_COM_LOG_INFO << "CmdUnregisterDirect: svc=" << Hex64( serviceId )
                     << " slot=" << slotIndex;

    std::printf( "Unregistering service 0x%llx via direct mmap (fallback)...\n",
                 static_cast< unsigned long long > ( serviceId ) );

    auto result = registry.UnregisterService( slotIndex );

    if ( !result.HasValue() )
    {
        std::fprintf( stderr, "%s✗%s Unregistration failed\n",
                      Clr( kRed ), Clr( kReset ) );
        LAP_COM_LOG_ERROR << "CmdUnregisterDirect: failed for slot=" << slotIndex;
        return 1;
    }

    std::printf( "%s✓%s Unregistered via direct mmap (fallback)\n",
                 Clr( kGreen ), Clr( kReset ) );
    std::printf( "  Service ID:  0x%llx\n", static_cast< unsigned long long > ( serviceId ) );
    std::printf( "  Slot:        %u\n", slotIndex );

    LAP_COM_LOG_INFO << "CmdUnregisterDirect: success, slot=" << slotIndex;
    return 0;
}

// =============================================================================
// Subcommand: inspect
// =============================================================================

static int CmdInspect( CServiceRegistry& registry, UInt32 slotIndex )
{
    LAP_COM_LOG_DEBUG << "CmdInspect: slot=" << slotIndex;

    if ( slotIndex >= RegistryConfig::kMaxSlots )
    {
        std::fprintf( stderr, "Error: slot index must be 0~%u\n",
                      RegistryConfig::kMaxSlots - 1 );
        return 1;
    }

    auto optSlot = registry.ReadSlot( slotIndex );
    if ( !optSlot.has_value() )
    {
        std::fprintf( stderr, "Error: cannot read slot %u\n", slotIndex );
        LAP_COM_LOG_ERROR << "CmdInspect: ReadSlot(" << slotIndex << ") failed";
        return 1;
    }

    const ServiceSlot& s = optSlot.value();
    const UInt32 statusVal = s.m_status.load( std::memory_order_acquire );
    const bool pidAlive = IsProcessAlive( s.m_ownerPid );

    std::printf( "\n" );
    std::printf( "%s── Slot %u ──%s\n", Clr( kBold ), slotIndex, Clr( kReset ) );
    std::printf( "\n" );
    std::printf( "  Status:            %s%s%s\n",
                 Clr( StatusColor( statusVal ) ),
                 StatusStr( statusVal ),
                 Clr( kReset ) );
    std::printf( "  Service ID:        0x%016llx\n",
                 static_cast< unsigned long long > ( s.m_serviceId ) );
    std::printf( "  Instance ID:       0x%016llx\n",
                 static_cast< unsigned long long > ( s.m_instanceId ) );

    // Decode instance ID fields
    const UInt32 low32      = static_cast< UInt32 > ( s.m_instanceId );
    const UInt32 svcIdBits  = low32 & 0xFFFF;
    const UInt32 instNo     = ( low32 >> 16 ) & 0xFF;
    const UInt32 domain     = ( low32 >> 24 ) & 0xF;
    const UInt32 asilLevel  = ( low32 >> 28 ) & 0x7;
    const UInt32 redundancy = ( low32 >> 31 ) & 0x1;

    std::printf( "    ├─ service_id:   0x%04x\n", svcIdBits );
    std::printf( "    ├─ instance_no:  %u\n", instNo );
    std::printf( "    ├─ domain:       %u (%s)\n", domain, DomainStr( s.m_instanceId ).c_str() );
    std::printf( "    ├─ asil_level:   %u (%s)\n", asilLevel, AsilLevelStr( s.m_instanceId ).c_str() );
    std::printf( "    └─ redundancy:   %u (%s)\n", redundancy,
                 redundancy ? "backup" : "primary" );

    std::printf( "  Version:           %u.%u\n", s.m_majorVersion, s.m_minorVersion );
    std::printf( "  Binding Type:      \"%s\"\n", s.m_bindingType );
    std::printf( "  Endpoint:          \"%s\"\n", s.m_endpoint );
    std::printf( "  Owner PID:         %d %s%s%s\n",
                 s.m_ownerPid,
                 pidAlive ? Clr( kGreen ) : ( s.m_ownerPid > 0 ? Clr( kRed ) : Clr( kDim ) ),
                 pidAlive ? "(alive)" : ( s.m_ownerPid > 0 ? "(DEAD)" : "" ),
                 Clr( kReset ) );
    std::printf( "  Heartbeat:         %s\n",
                 FormatTimestamp( s.m_lastHeartbeatNs ).c_str() );
    std::printf( "  HB Interval:       %u ms\n", s.m_heartbeatIntervalMs );
    std::printf( "  Metadata:          \"%s\"\n", s.m_metadata );

    // Raw hex dump
    std::printf( "\n  %sRaw (256 bytes):%s\n  ", Clr( kDim ), Clr( kReset ) );
    const uint8_t* raw = reinterpret_cast< const uint8_t* > ( &s );
    for ( size_t j = 0; j < sizeof( ServiceSlot ); ++j )
    {
        if ( j > 0 && ( j % 32 ) == 0 ) std::printf( "\n  " );
        std::printf( "%02x ", raw[j] );
    }
    std::printf( "\n\n" );

    return 0;
}

// =============================================================================
// Subcommand: watch
// =============================================================================

static int CmdWatch( CServiceRegistry& registry, UInt32 intervalMs )
{
    signal( SIGINT,  SignalHandler );
    signal( SIGTERM, SignalHandler );

    LAP_COM_LOG_INFO << "CmdWatch: starting, interval=" << intervalMs << "ms";

    std::printf( "%sWatching registry (Ctrl+C to stop, interval=%ums)...%s\n\n",
                 Clr( kCyan ), intervalMs, Clr( kReset ) );

    // Snapshot of previous status for change detection
    std::vector< UInt32 > prevStatus( RegistryConfig::kMaxSlots, 0 );
    for ( UInt32 i = 0; i < RegistryConfig::kMaxSlots; ++i )
    {
        auto opt = registry.ReadSlot( i );
        if ( opt.has_value() )
        {
            prevStatus[i] = opt.value().m_status.load( std::memory_order_acquire );
        }
    }

    while ( g_running )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( intervalMs ) );

        for ( UInt32 i = 0; i < RegistryConfig::kMaxSlots; ++i )
        {
            auto opt = registry.ReadSlot( i );
            if ( !opt.has_value() ) continue;

            const ServiceSlot& s = opt.value();
            const UInt32 cur = s.m_status.load( std::memory_order_acquire );

            if ( cur != prevStatus[i] )
            {
                struct timespec ts {};
                clock_gettime( CLOCK_REALTIME, &ts );
                struct tm tm {};
                localtime_r( &ts.tv_sec, &tm );

                std::printf( "[%02d:%02d:%02d.%03ld] Slot %-4u: %s%-8s%s → %s%-8s%s",
                             tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000,
                             i,
                             Clr( StatusColor( prevStatus[i] ) ),
                             StatusStr( prevStatus[i] ),
                             Clr( kReset ),
                             Clr( StatusColor( cur ) ),
                             StatusStr( cur ),
                             Clr( kReset ) );

                if ( cur == static_cast< UInt32 > ( SlotStatus::kActive ) )
                {
                    std::printf( "  svc=0x%llx pid=%d %s",
                                 static_cast< unsigned long long > ( s.m_serviceId ),
                                 s.m_ownerPid,
                                 s.m_endpoint );
                }
                std::printf( "\n" );

                LAP_COM_LOG_INFO << "Watch: slot " << i
                                 << " " << StatusStr( prevStatus[i] )
                                 << " → " << StatusStr( cur );

                prevStatus[i] = cur;
            }
        }
    }

    std::printf( "\nWatch stopped.\n" );
    LAP_COM_LOG_INFO << "CmdWatch: stopped";
    return 0;
}

// =============================================================================
// Usage
// =============================================================================

static void PrintUsage( const char* prog )
{
    std::printf( "\n" );
    std::printf( "%sLightAP Registry Control Tool%s (v2.0 Core IPC)\n", Clr( kBold ), Clr( kReset ) );
    std::printf( "\n" );
    std::printf( "Usage: %s [options] <command> [args]\n", prog );
    std::printf( "\n" );
    std::printf( "%sOptions:%s\n", Clr( kBold ), Clr( kReset ) );
    std::printf( "  --socket=<path>   UDS socket path (default: /run/lap/registry_qm.sock)\n" );
    std::printf( "  --type=<qm|asil>  Registry type shortcut\n" );
    std::printf( "  --timeout=<ms>    IPC timeout for register/unregister (default: 5000)\n" );
    std::printf( "  --no-color        Disable colored output\n" );
    std::printf( "  --help, -h        Show this help\n" );
    std::printf( "\n" );
    std::printf( "%sCommands:%s\n", Clr( kBold ), Clr( kReset ) );
    std::printf( "  %slist%s [--all]                                 List active services\n",
                 Clr( kCyan ), Clr( kReset ) );
    std::printf( "  %sstatus%s                                       Registry summary & statistics\n",
                 Clr( kCyan ), Clr( kReset ) );
    std::printf( "  %sregister%s <svc_id> [options]                  Register via Core IPC\n",
                 Clr( kCyan ), Clr( kReset ) );
    std::printf( "    --instance=<id>  Instance ID (default: 0)\n" );
    std::printf( "    --version=<M.m>  Version (default: 1.0)\n" );
    std::printf( "    --binding=<type> Binding type (default: coreipc)\n" );
    std::printf( "    --endpoint=<ep>  Endpoint address\n" );
    std::printf( "  %sunregister%s <svc_id>                          Unregister via Core IPC\n",
                 Clr( kCyan ), Clr( kReset ) );
    std::printf( "  %sinspect%s <slot>                               Dump slot details + raw hex\n",
                 Clr( kCyan ), Clr( kReset ) );
    std::printf( "  %swatch%s [--interval=<ms>]                     Monitor slot changes live\n",
                 Clr( kCyan ), Clr( kReset ) );
    std::printf( "\n" );
    std::printf( "%sExamples:%s\n", Clr( kBold ), Clr( kReset ) );
    std::printf( "  %s list                                   # List active QM services\n", prog );
    std::printf( "  %s --type=asil list                       # List active ASIL services\n", prog );
    std::printf( "  %s status                                 # Show QM registry summary\n", prog );
    std::printf( "  %s register 0x100A --binding=dds          # Register svc 0x100A via IPC\n", prog );
    std::printf( "  %s unregister 0x100A                      # Unregister svc 0x100A via IPC\n", prog );
    std::printf( "  %s inspect 1                              # Dump SD Proxy Primary slot\n", prog );
    std::printf( "  %s watch                                  # Monitor changes (Ctrl+C stop)\n", prog );
    std::printf( "\n" );
    std::printf( "%sNote:%s register/unregister prefer Core IPC (MPSC request →\n", Clr( kDim ), Clr( kReset ) );
    std::printf( "      CRegistryDispatcher → slot write). If IPC dispatcher is not running,\n" );
    std::printf( "      falls back to direct mmap write via UDS shared memory.\n" );
    std::printf( "\n" );
}

// =============================================================================
// Main
// =============================================================================

int main( int argc, char* argv[] )
{
    std::string socketPath = "/run/lap/registry_qm.sock";
    std::string regType    = "QM";
    RegistryType registryType = RegistryType::kQM;
    std::string command;
    bool showAll           = false;

    // register args
    UInt64      regSvcId      = 0;
    UInt64      regInstId     = 0;
    UInt32      regMajor      = 1;
    UInt32      regMinor      = 0;
    std::string regBinding    = "coreipc";
    std::string regEndpoint;

    // unregister args
    UInt64      unregSvcId    = 0;

    // inspect args
    UInt32      targetSlot    = 0;

    // watch args
    UInt32      watchInterval = 500;

    // IPC timeout
    UInt32      ipcTimeout    = 5000;

    // Parse arguments
    std::vector< std::string > positional;

    for ( int i = 1; i < argc; ++i )
    {
        std::string arg( argv[i] );

        if ( arg == "--help" || arg == "-h" )
        {
            PrintUsage( argv[0] );
            return 0;
        }
        else if ( arg == "--no-color" )
        {
            g_color = false;
        }
        else if ( arg == "--all" )
        {
            showAll = true;
        }
        else if ( arg.rfind( "--socket=", 0 ) == 0 )
        {
            socketPath = arg.substr( 9 );
        }
        else if ( arg == "--type=qm" )
        {
            socketPath   = "/run/lap/registry_qm.sock";
            regType      = "QM";
            registryType = RegistryType::kQM;
        }
        else if ( arg == "--type=asil" )
        {
            socketPath   = "/run/lap/registry_asil.sock";
            regType      = "ASIL";
            registryType = RegistryType::kASIL;
        }
        else if ( arg.rfind( "--instance=", 0 ) == 0 )
        {
            regInstId = std::strtoull( arg.substr( 11 ).c_str(), nullptr, 0 );
        }
        else if ( arg.rfind( "--version=", 0 ) == 0 )
        {
            std::sscanf( arg.substr( 10 ).c_str(), "%u.%u", &regMajor, &regMinor );
        }
        else if ( arg.rfind( "--binding=", 0 ) == 0 )
        {
            regBinding = arg.substr( 10 );
        }
        else if ( arg.rfind( "--endpoint=", 0 ) == 0 )
        {
            regEndpoint = arg.substr( 11 );
        }
        else if ( arg.rfind( "--interval=", 0 ) == 0 )
        {
            watchInterval = static_cast< UInt32 > ( std::atoi( arg.substr( 11 ).c_str() ) );
        }
        else if ( arg.rfind( "--timeout=", 0 ) == 0 )
        {
            ipcTimeout = static_cast< UInt32 > ( std::atoi( arg.substr( 10 ).c_str() ) );
        }
        else if ( arg[0] != '-' )
        {
            positional.push_back( arg );
        }
        else
        {
            std::fprintf( stderr, "Unknown option: %s\n", arg.c_str() );
            return 1;
        }
    }

    if ( positional.empty() )
    {
        PrintUsage( argv[0] );
        return 1;
    }

    command = positional[0];

    // Parse positional args per command
    if ( command == "register" )
    {
        if ( positional.size() < 2 )
        {
            std::fprintf( stderr, "Usage: register <service_id> [options]\n" );
            return 1;
        }
        regSvcId = std::strtoull( positional[1].c_str(), nullptr, 0 );

        if ( regEndpoint.empty() )
        {
            char buf[80];
            std::snprintf( buf, sizeof( buf ), "shm://svc_%llx/default",
                           static_cast< unsigned long long > ( regSvcId ) );
            regEndpoint = buf;
        }
    }
    else if ( command == "unregister" )
    {
        if ( positional.size() < 2 )
        {
            std::fprintf( stderr, "Usage: unregister <service_id>\n" );
            return 1;
        }
        unregSvcId = std::strtoull( positional[1].c_str(), nullptr, 0 );
    }
    else if ( command == "inspect" )
    {
        if ( positional.size() < 2 )
        {
            std::fprintf( stderr, "Usage: inspect <slot>\n" );
            return 1;
        }
        targetSlot = static_cast< UInt32 > ( std::strtoul( positional[1].c_str(), nullptr, 0 ) );
    }

    // =========================================================================
    // Determine if we need Core IPC (write path) or read-only shared memory
    // =========================================================================
    const bool needIpc = ( command == "register" || command == "unregister" );

    LAP_COM_LOG_INFO << "lap-registry-ctl: command=" << command
                     << " type=" << regType
                     << " socket=" << socketPath
                     << " ipc=" << ( needIpc ? "yes" : "no" );

    int rc = 0;

    if ( needIpc )
    {
        // =================================================================
        // Write path: Try Core IPC first, fallback to direct mmap
        // =================================================================

        bool ipcOk = false;
        CRegistryProxy proxy;
        auto ipcResult = proxy.Initialize();
        if ( ipcResult.HasValue() )
        {
            ipcOk = true;
            LAP_COM_LOG_INFO << "CRegistryProxy initialized (IPC channels connected)";
        }
        else
        {
            LAP_COM_LOG_WARN << "Core IPC unavailable, falling back to direct mmap";
            std::fprintf( stderr,
                "%s⚠%s  Core IPC dispatcher not running, using direct mmap fallback\n",
                Clr( kYellow ), Clr( kReset ) );
        }

        if ( ipcOk )
        {
            if ( command == "register" )
            {
                rc = CmdRegister( proxy, regSvcId, regInstId,
                                  regMajor, regMinor,
                                  regBinding.c_str(), regEndpoint.c_str(),
                                  ipcTimeout );
            }
            else if ( command == "unregister" )
            {
                rc = CmdUnregister( proxy, unregSvcId, ipcTimeout );
            }
        }
        else
        {
            // Fallback: connect via UDS and write directly to shared memory
            CServiceRegistry registry( registryType );
            auto initResult = registry.InitializeFromSocket( socketPath );
            if ( !initResult.HasValue() )
            {
                std::fprintf( stderr, "Failed to connect to registry at %s\n",
                              socketPath.c_str() );
                std::fprintf( stderr, "Is the registry daemon running?\n" );
                LAP_COM_LOG_ERROR << "InitializeFromSocket(" << socketPath << ") failed";
                return 1;
            }

            if ( command == "register" )
            {
                rc = CmdRegisterDirect( registry, regSvcId, regInstId,
                                        regMajor, regMinor,
                                        regBinding.c_str(), regEndpoint.c_str() );
            }
            else if ( command == "unregister" )
            {
                rc = CmdUnregisterDirect( registry, unregSvcId );
            }
        }
    }
    else
    {
        // =================================================================
        // Read path: CServiceRegistry::InitializeFromSocket() → read-only mmap
        // =================================================================

        CServiceRegistry registry( registryType );
        auto initResult = registry.InitializeFromSocket( socketPath );
        if ( !initResult.HasValue() )
        {
            std::fprintf( stderr, "Failed to connect to registry at %s\n",
                          socketPath.c_str() );
            std::fprintf( stderr, "Is the registry daemon running?\n" );
            std::fprintf( stderr, "  Start: lap-registry-init --type=%s --socket=%s\n",
                          regType.c_str(), socketPath.c_str() );
            LAP_COM_LOG_ERROR << "InitializeFromSocket(" << socketPath << ") failed";
            return 1;
        }

        LAP_COM_LOG_INFO << "Registry connected via "
                         << socketPath << " (read-only mmap)";

        if ( command == "list" )
        {
            rc = CmdList( registry, showAll );
        }
        else if ( command == "status" )
        {
            rc = CmdStatus( registry, regType.c_str(), socketPath.c_str() );
        }
        else if ( command == "inspect" )
        {
            rc = CmdInspect( registry, targetSlot );
        }
        else if ( command == "watch" )
        {
            rc = CmdWatch( registry, watchInterval );
        }
        else
        {
            std::fprintf( stderr, "Unknown command: %s\n", command.c_str() );
            PrintUsage( argv[0] );
            rc = 1;
        }
    }

    return rc;
}
