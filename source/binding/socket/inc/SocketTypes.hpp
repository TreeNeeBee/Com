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

    // ====================================================================
    // Configuration
    // ====================================================================

    /**
     * @brief   Socket Binding configuration
     * @note    Reserved — Socket binding is not yet implemented
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
