/**
 * @file        DbusTypes.hpp
 * @author      LightAP Development Team
 * @brief       D-Bus binding — Configuration, constants, and shared type aliases
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Lean type definitions for the D-Bus binding module:
 *              - DbusConfig — binding configuration
 *              - Common using declarations from lap::core (CTypedef, CSync)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Initial stub for composition pattern
 * </table>
 */

#ifndef LAP_COM_DBUS_TYPES_HPP
#define LAP_COM_DBUS_TYPES_HPP

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

    /**
     * @brief   D-Bus Binding configuration
     * @details Uses sd-bus (libsystemd) for D-Bus communication
     */
    struct DbusConfig
    {
        String  m_strBusAddress;                ///< Bus address (empty = session bus)
        Bool    m_bUseSystemBus     = false;    ///< Use system bus instead of session bus
        UInt32  m_iTimeoutMs        = 5000;     ///< Default method call timeout (ms)
        String  m_strServicePrefix  = "com.lap.service";  ///< D-Bus service name prefix
        String  m_strObjectPath     = "/com/lap/service";  ///< D-Bus object path
        String  m_strInterfaceName  = "com.lap.service.IPC";  ///< D-Bus interface name
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_DBUS_TYPES_HPP
