/**
 * @file        SocketTypes.hpp
 * @author      LightAP Development Team
 * @brief       Socket binding — Configuration, constants, and shared type aliases
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Lean type definitions for the Socket binding module:
 *              - SocketConfig — binding configuration
 *              - Common using declarations from lap::core (CTypedef, CSync)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Initial stub for composition pattern
 * </table>
 */

#ifndef LAP_COM_SOCKET_TYPES_HPP
#define LAP_COM_SOCKET_TYPES_HPP

// ==================== Cross-Module Headers ====================
#include <lap/core/CTypedef.hpp>
#include <lap/core/CSync.hpp>
#include <lap/core/CString.hpp>

// ==================== Project-Internal Headers ====================
#include "BindingTypes.hpp"

namespace lap
{
namespace com
{
namespace binding
{

    // ====================================================================
    // Common Type Aliases
    // ====================================================================

    using lap::core::Atomic;
    using lap::core::Bool;
    using lap::core::Byte;
    using lap::core::Int32;
    using lap::core::Map;
    using lap::core::MakeShared;
    using lap::core::MakeUnique;
    using lap::core::SharedHandle;
    using lap::core::Size;
    using lap::core::String;
    using lap::core::UInt8;
    using lap::core::UInt16;
    using lap::core::UInt32;
    using lap::core::UInt64;
    using lap::core::UniqueHandle;
    using lap::core::Vector;

    using lap::core::Mutex;
    using lap::core::LockGuard;
    using lap::core::UniqueLock;
    using lap::core::ConditionVariable;

    // ====================================================================
    // Socket Message Header (simple TLV framing)
    // ====================================================================

    /**
     * @brief   Operation codes for socket wire protocol
     */
    static constexpr UInt8 kSockOpEvent     = 0x01;
    static constexpr UInt8 kSockOpRequest   = 0x02;
    static constexpr UInt8 kSockOpResponse  = 0x03;
    static constexpr UInt8 kSockOpNotify    = 0x04;
    static constexpr UInt32 kSockHeaderSize = 20;  ///< Fixed header size

    /**
     * @brief   Socket message header (TLV framing)
     * @details 20-byte fixed header:
     *   [0]     opCode (event/request/response/notify)
     *   [1..2]  serviceId
     *   [3..4]  instanceId (low 16 bits)
     *   [5..6]  methodOrEventId
     *   [7..8]  sessionId
     *   [9..12] payloadLength
     *   [13..19] reserved (zero)
     */
    struct SocketMsgHeader
    {
        UInt8   m_iOpCode;
        UInt16  m_iServiceId;
        UInt16  m_iInstanceId;
        UInt16  m_iMethodOrEventId;
        UInt16  m_iSessionId;
        UInt32  m_iPayloadLength;
        UInt8   m_reserved[7];
    } __attribute__(( packed ));

    // ====================================================================
    // Configuration
    // ====================================================================

    /**
     * @brief   Socket Binding configuration
     * @details Unix domain socket or TCP-based IPC transport
     */
    struct SocketConfig
    {
        String  m_strSocketPath     = "/tmp/lap_com.sock";  ///< Unix domain socket path
        UInt32  m_iTimeoutMs        = 5000;                 ///< Default operation timeout (ms)
        UInt32  m_iMaxConnections   = 64;                   ///< Maximum concurrent connections
        Bool    m_bUseTcp           = false;                ///< Use TCP instead of Unix domain socket
        String  m_strBindAddress    = "127.0.0.1";          ///< TCP bind address (if m_bUseTcp)
        UInt16  m_iPort             = 9876;                 ///< TCP port (if m_bUseTcp)
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_SOCKET_TYPES_HPP
