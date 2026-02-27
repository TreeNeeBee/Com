/**
 * @file        CCoreIPCCodec.cpp
 * @author      LightAP Development Team
 * @brief       Core IPC binding — CCoreIPCCodec implementation
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Implements all static utility methods of CCoreIPCCodec:
 *              path/key generation, event/method binary codec, SHM helper.
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Extracted from monolithic CoreIPCBinding
 * </table>
 */

// ==================== Project-Internal Headers ====================
#include "CCoreIPCCodec.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/IPCFactory.hpp>
#include <lap/log/CLog.hpp>

// ==================== Standard Library Headers ====================
#include <cstring>
#include <cstdio>

namespace lap
{
namespace com
{
namespace binding
{

    using namespace lap::core;
    using namespace lap::core::ipc;

    // ====================================================================
    // Path Generation
    // ====================================================================

    String CCoreIPCCodec::MakeServicePath(
        UInt64 serviceId, UInt64 instanceId ) noexcept
    {
        Char buf[48];
        ::std::snprintf( buf, sizeof( buf ), "/lap_ipc_%04lx_%04lx",
                         static_cast< unsigned long >( serviceId ),
                         static_cast< unsigned long >( instanceId ) );
        return String( buf );
    }

    String CCoreIPCCodec::MakeMethodRequestPath(
        UInt64 serviceId, UInt64 instanceId ) noexcept
    {
        Char buf[48];
        ::std::snprintf( buf, sizeof( buf ), "/lap_ipc_method_req_%04lx_%04lx",
                         static_cast< unsigned long >( serviceId ),
                         static_cast< unsigned long >( instanceId ) );
        return String( buf );
    }

    String CCoreIPCCodec::MakeMethodResponsePath(
        UInt64 serviceId, UInt64 instanceId ) noexcept
    {
        Char buf[48];
        ::std::snprintf( buf, sizeof( buf ), "/lap_ipc_method_resp_%04lx_%04lx",
                         static_cast< unsigned long >( serviceId ),
                         static_cast< unsigned long >( instanceId ) );
        return String( buf );
    }

    // ====================================================================
    // Key Generation
    // ====================================================================

    UInt64 CCoreIPCCodec::MakeServiceKey(
        UInt64 serviceId, UInt64 instanceId ) noexcept
    {
        return ( serviceId << 32 ) | instanceId;
    }

    UInt64 CCoreIPCCodec::MakeEventKey(
        UInt64 serviceId, UInt64 instanceId, UInt32 eventId ) noexcept
    {
        return ( serviceId << 32 ) | ( instanceId << 16 ) | eventId;
    }

    // ====================================================================
    // Event Encoding / Decoding
    // ====================================================================

    ByteBuffer CCoreIPCCodec::EncodeEventMessage(
        UInt32 eventId, const ByteBuffer& payload ) noexcept
    {
        const UInt32 payloadSize = static_cast< UInt32 >( payload.size() );
        ByteBuffer result( kCoreIPCEventHeaderSize + payload.size() );

        // Native byte order (Linux LE) — single memcpy per field
        ::std::memcpy( result.data(), &eventId, sizeof( eventId ) );
        ::std::memcpy( result.data() + 4U, &payloadSize, sizeof( payloadSize ) );

        // payload
        if ( !payload.empty() ) {
            ::std::memcpy( result.data() + kCoreIPCEventHeaderSize,
                           payload.data(), payload.size() );
        }

        return result;
    }

    Bool CCoreIPCCodec::DecodeEventMessage(
        const UInt8* data, Size size,
        UInt32& eventId, UInt32& payloadSize,
        Size& payloadOffset ) noexcept
    {
        if ( size < kCoreIPCEventHeaderSize ) {
            return false;
        }

        // Native byte order — Linux LE
        ::std::memcpy( &eventId, data, sizeof( eventId ) );
        ::std::memcpy( &payloadSize, data + 4U, sizeof( payloadSize ) );

        payloadOffset = kCoreIPCEventHeaderSize;
        return payloadOffset + payloadSize <= size;
    }

    // ====================================================================
    // Method Encoding / Decoding
    // ====================================================================

    ByteBuffer CCoreIPCCodec::EncodeMethodMessage(
        UInt32 methodId, UInt64 clientToken,
        Int32 status, const ByteBuffer& payload ) noexcept
    {
        const UInt32 payloadSize = static_cast< UInt32 >( payload.size() );
        const UInt32 statusU = static_cast< UInt32 >( status );
        ByteBuffer result( kCoreIPCMethodHeaderSize + payload.size() );

        // Native byte order (Linux LE) — single memcpy per field
        ::std::memcpy( result.data(), &methodId, sizeof( methodId ) );
        ::std::memcpy( result.data() + 4U, &clientToken, sizeof( clientToken ) );
        ::std::memcpy( result.data() + 12U, &statusU, sizeof( statusU ) );
        ::std::memcpy( result.data() + 16U, &payloadSize, sizeof( payloadSize ) );

        // payload
        if ( !payload.empty() ) {
            ::std::memcpy( result.data() + kCoreIPCMethodHeaderSize,
                           payload.data(), payload.size() );
        }

        return result;
    }

    Bool CCoreIPCCodec::DecodeMethodMessage(
        const UInt8* data, Size size,
        UInt32& methodId, UInt64& clientToken,
        Int32& status, UInt32& payloadSize,
        Size& payloadOffset ) noexcept
    {
        if ( size < kCoreIPCMethodHeaderSize ) {
            return false;
        }

        // Native byte order — Linux LE
        ::std::memcpy( &methodId, data, sizeof( methodId ) );
        ::std::memcpy( &clientToken, data + 4U, sizeof( clientToken ) );

        UInt32 statusU = 0U;
        ::std::memcpy( &statusU, data + 12U, sizeof( statusU ) );
        status = static_cast< Int32 >( statusU );

        ::std::memcpy( &payloadSize, data + 16U, sizeof( payloadSize ) );

        payloadOffset = kCoreIPCMethodHeaderSize;
        return payloadOffset + payloadSize <= size;
    }

    // ====================================================================
    // Shared Memory Helper
    // ====================================================================

    Result< void > CCoreIPCCodec::EnsureSharedMemory(
        const String& shmPath,
        const SharedMemoryConfig& config,
        ShmSegmentMap& mapShmSegments ) noexcept
    {
        auto it = mapShmSegments.find( shmPath );
        if ( it != mapShmSegments.end() ) {
            return Result< void >::FromValue();
        }

        auto shmResult = IPCFactory::CreateSHM( shmPath.c_str(), config );
        if ( !shmResult ) {
            if ( shmResult.Error() == CoreErrc::kIPCShmAlreadyExists ) {
                auto pShm = lap::core::MakeUnique< SharedMemoryManager > ();
                auto openResult = pShm->Open( shmPath.c_str(), config );
                if ( !openResult ) {
                    return Result< void >::FromError( openResult.Error() );
                }
                mapShmSegments[shmPath] = ::std::move( pShm );
                return Result< void >::FromValue();
            }
            return Result< void >::FromError( shmResult.Error() );
        }

        mapShmSegments[shmPath] = ::std::move( shmResult ).Value();
        return Result< void >::FromValue();
    }

} // namespace binding
} // namespace com
} // namespace lap
