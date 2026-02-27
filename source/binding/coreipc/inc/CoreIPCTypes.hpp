/**
 * @file        CoreIPCTypes.hpp
 * @author      LightAP Development Team
 * @brief       Core IPC binding — Configuration, constants, and shared type aliases
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Lean type definitions for the CoreIPC binding module:
 *              - CoreIPCConfig — binding configuration
 *              - kCoreIPCEventHeaderSize / kCoreIPCMethodHeaderSize — wire-format constants
 *              - ShmSegmentMap — shared-memory segment storage alias
 *              - Common using declarations from lap::core
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/01/19  <td>1.0      <td>LightAP Team    <td>Initial implementation
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style refactor per code_rules.md
 * <tr><td>2026/02/07  <td>3.0      <td>Aii             <td>Lean types header; wrappers moved to managers
 * </table>
 */

#ifndef LAP_COM_CORE_IPC_TYPES_HPP
#define LAP_COM_CORE_IPC_TYPES_HPP

// ==================== Cross-Module Headers ====================
#include <lap/core/CTypedef.hpp>
#include <lap/core/CSync.hpp>
#include <lap/core/CString.hpp>
#include <lap/core/ipc/SharedMemoryManager.hpp>

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
    // Constants
    // ====================================================================

    constexpr Size kCoreIPCEventHeaderSize  = 8U;   ///< eventId(4) + payload_size(4)
    constexpr Size kCoreIPCMethodHeaderSize = 20U;  ///< methodId(4) + token(8) + status(4) + size(4)

    // ====================================================================
    // Configuration
    // ====================================================================

    /**
     * @brief   Configuration for Core IPC binding
     * @note    All sizes in bytes, intervals in microseconds/milliseconds as noted
     */
    struct CoreIPCConfig
    {
        Size    m_iMaxPayloadSize           = 1024;  ///< Maximum payload size (bytes)
        Size    m_iSubscriberQueueCapacity  = 32;    ///< Subscriber queue capacity (chunks)
        Size    m_iMaxChunks                = 64;    ///< Maximum chunks per publisher
        UInt32  m_iListenerPollIntervalUs   = 100;   ///< Listener poll interval (microseconds)
        UInt32  m_iMethodCallTimeoutMs      = 1000;  ///< Method call timeout (milliseconds)
        UInt32  m_iMethodPollIntervalUs     = 100;   ///< Method poll interval (microseconds)
    };

    // ====================================================================
    // Shared Type Aliases
    // ====================================================================

    /**
     * @brief   Map type for shared-memory segment storage
     * @details Key = SHM path name, Value = owning handle to SharedMemoryManager
     */
    using ShmSegmentMap = Map< String, UniqueHandle< lap::core::ipc::SharedMemoryManager > >;

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_CORE_IPC_TYPES_HPP
