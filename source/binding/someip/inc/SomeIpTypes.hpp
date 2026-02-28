/**
 * @file        SomeIpTypes.hpp
 * @author      LightAP Development Team
 * @brief       SOME/IP binding — Configuration, constants, and shared type aliases
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Lean type definitions for the SOME/IP binding module:
 *              - SomeIpConfig — binding configuration
 *              - Common using declarations from lap::core (CTypedef, CSync)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Initial stub for composition pattern
 * </table>
 */

#ifndef LAP_COM_SOMEIP_TYPES_HPP
#define LAP_COM_SOMEIP_TYPES_HPP

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
    // Configuration
    // ====================================================================

    // ====================================================================
    // SOME/IP Wire Format Constants (AUTOSAR PRS_SOMEIP_00041)
    // ====================================================================

    static constexpr UInt8  kSomeIpProtocolVersion  = 0x01;
    static constexpr UInt8  kSomeIpInterfaceVersion = 0x01;
    static constexpr UInt8  kSomeIpMsgTypeRequest   = 0x00;
    static constexpr UInt8  kSomeIpMsgTypeResponse  = 0x80;
    static constexpr UInt8  kSomeIpMsgTypeNotify    = 0x02;
    static constexpr UInt8  kSomeIpMsgTypeError     = 0x81;
    static constexpr UInt8  kSomeIpReturnCodeOk     = 0x00;
    static constexpr UInt32 kSomeIpHeaderSize       = 16;     ///< SOME/IP header: 16 bytes

    // ====================================================================
    // SOME/IP Header (on the wire, network byte order)
    // ====================================================================

    /**
     * @brief   SOME/IP message header (PRS_SOMEIP_00041)
     *
     * @details 16-byte fixed header:
     *   [0..1]  Service ID
     *   [2..3]  Method/Event ID
     *   [4..7]  Length (payload + 8 bytes of remaining header)
     *   [8..9]  Client ID
     *   [10..11] Session ID
     *   [12]    Protocol Version (0x01)
     *   [13]    Interface Version
     *   [14]    Message Type
     *   [15]    Return Code
     */
    struct SomeIpHeader
    {
        UInt16  m_iServiceId;
        UInt16  m_iMethodId;
        UInt32  m_iLength;
        UInt16  m_iClientId;
        UInt16  m_iSessionId;
        UInt8   m_iProtocolVersion;
        UInt8   m_iInterfaceVersion;
        UInt8   m_iMessageType;
        UInt8   m_iReturnCode;
    } __attribute__(( packed ));

    // ====================================================================
    // Configuration
    // ====================================================================

    /**
     * @brief   SOME/IP Binding configuration
     *
     * @details Lightweight SOME/IP-over-UDP transport.
     *          Uses raw UDP sockets with SOME/IP wire format.
     *          No vsomeip dependency — standalone implementation.
     */
    struct SomeIpConfig
    {
        String  m_strAppName        = "LightAP";       ///< Application name (for logging)
        String  m_strUnicastAddress = "127.0.0.1";      ///< Unicast IP address
        UInt16  m_iPort             = 30490;             ///< SOME/IP base port
        UInt16  m_iSdPort           = 30490;             ///< SOME/IP-SD port
        String  m_strMulticastGroup = "239.0.0.1";       ///< SD multicast group
        UInt32  m_iTimeoutMs        = 5000;              ///< Default method call timeout (ms)
        UInt16  m_iClientId         = 0x0001;            ///< SOME/IP client ID
        UInt32  m_iMaxPayloadSize   = 65536;             ///< Maximum payload size (bytes)
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_SOMEIP_TYPES_HPP
