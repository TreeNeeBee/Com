/**
 * @file        DdsTypes.hpp
 * @author      LightAP Development Team
 * @brief       DDS binding — Configuration, constants, and shared type aliases
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Lean type definitions for the DDS binding module:
 *              - DdsConfig — binding configuration (domain, QoS, transports)
 *              - Common using declarations from lap::core (CTypedef, CSync)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/23  <td>1.0      <td>LightAP Team    <td>Initial (in DdsBinding.hpp)
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style refactor per code_rules.md
 * <tr><td>2026/02/07  <td>3.0      <td>Aii             <td>Lean types header; composition refactor
 * </table>
 */

#ifndef LAP_COM_DDS_TYPES_HPP
#define LAP_COM_DDS_TYPES_HPP

// ==================== Cross-Module Headers ====================
#include <lap/core/CTypedef.hpp>
#include <lap/core/CSync.hpp>
#include <lap/core/CString.hpp>
#include <lap/core/CFunction.hpp>

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

    using lap::core::Function;

    using lap::core::Mutex;
    using lap::core::LockGuard;
    using lap::core::UniqueLock;
    using lap::core::ConditionVariable;

    // ====================================================================
    // Configuration
    // ====================================================================

    /**
     * @brief   DDS Binding configuration
     * @note    Controls DDS domain, discovery, transports, QoS
     */
    struct DdsConfig
    {
        UInt32  m_iDomainId                 = 0;            ///< DDS domain ID (default: 0)
        String  m_strDiscoveryServer;                       ///< Discovery server address (optional)
        Bool    m_bUseSharedMemory          = true;         ///< Enable DDS shared memory transport
        Bool    m_bAfXdpEnabled             = false;        ///< Enable AF_XDP zero-copy for network
        String  m_strAfXdpInterface         = "eth0";       ///< Network interface for AF_XDP
        Vector< UInt32 > m_vecAfXdpQueues   = { 0, 1 };    ///< AF_XDP queue IDs
        UInt32  m_iLargePayloadThreshold    = 65536;        ///< > 64KB uses AF_XDP (bytes)
        UInt32  m_iMaxPayloadSize           = 10485760;     ///< Max payload: 10MB

        // QoS defaults
        Bool    m_bReliable                 = true;         ///< RELIABLE vs BEST_EFFORT
        Bool    m_bTransientLocal           = false;        ///< TRANSIENT_LOCAL durability
        UInt32  m_iHistoryDepth             = 10;           ///< KEEP_LAST depth
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_DDS_TYPES_HPP
