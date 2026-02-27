/**
 * @file        CBindingContext.hpp
 * @author      Aii
 * @brief       Binding context for proxy/skeleton sub-component transport wiring
 * @date        2026/02/07
 * @details     Lightweight context struct passed from ProxyBase/SkeletonBase to
 *              sub-components (ProxyEvent, ProxyMethod, ProxyField, SkeletonEvent,
 *              SkeletonField, SkeletonMethod). Holds the transport binding pointer,
 *              service/instance IDs and element ID (event/method/field).
 *
 *              Flow:
 *              ProxyBase::Create() → setBindingContext() on each sub-component
 *              SkeletonBase::OfferService() → setBindingContext() on each sub-component
 *
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @reference   ARCHITECTURE_SUMMARY.md §7 Binding Manager
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>Initial binding context
 * </table>
 */
#ifndef LAP_COM_CBINDING_CONTEXT_HPP
#define LAP_COM_CBINDING_CONTEXT_HPP

// ==================== Cross-Module Headers ====================
#include <core/CTypedef.hpp>

namespace lap
{
namespace com
{
    // Forward declaration (avoid circular include)
    namespace binding
    {
        class ITransportBinding;
    } // namespace binding

    /**
     * @brief Binding context for sub-component transport wiring
     *
     * @details Provides a lightweight, non-owning view of the transport binding
     *          and service identification needed by proxy/skeleton sub-components
     *          to perform network operations.
     *
     *          Design rationale:
     *          - POD-like struct: trivially copyable, no heap allocation
     *          - Non-owning: raw pointer to ITransportBinding (lifetime managed by BindingManager)
     *          - Late-binding: default-constructed as invalid, populated after proxy/skeleton connects
     *
     * @note Thread-safety: The struct itself is not thread-safe.
     *       Callers must hold their own mutex before accessing.
     */
    struct CBindingContext
    {
        binding::ITransportBinding* pBinding{ nullptr };    ///< Non-owning transport binding
        lap::core::UInt64           serviceId{ 0 };         ///< AUTOSAR service ID
        lap::core::UInt64           instanceId{ 0 };        ///< AUTOSAR instance ID
        lap::core::UInt32           elementId{ 0 };         ///< Event/Method/Field ID

        /**
         * @brief Check if context is valid (binding assigned)
         * @return true if binding pointer is non-null
         */
        Bool IsValid() const noexcept
        {
            return ( pBinding != nullptr );
        }
    };

} // namespace com
} // namespace lap

#endif // LAP_COM_CBINDING_CONTEXT_HPP
