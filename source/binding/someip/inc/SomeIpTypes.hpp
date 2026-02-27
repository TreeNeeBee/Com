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

    // ====================================================================
    // Configuration
    // ====================================================================

    /**
     * @brief   SOME/IP Binding configuration
     * @note    Reserved — SOME/IP binding is not yet implemented
     *
     * @details Future fields will include vsomeip application name,
     *          routing manager config, service discovery multicast,
     *          and network interface bindings.
     */
    struct SomeIpConfig
    {
        String  m_strAppName        = "LightAP";       ///< vsomeip application name
        String  m_strConfigPath;                        ///< vsomeip JSON config file (optional)
        String  m_strUnicastAddress = "127.0.0.1";      ///< Unicast IP address
        UInt16  m_iPort             = 30490;             ///< SOME/IP-SD port
        UInt32  m_iTimeoutMs        = 5000;              ///< Default method call timeout (ms)
        Bool    m_bRoutingManager   = false;             ///< Act as vsomeip routing manager
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_SOMEIP_TYPES_HPP
