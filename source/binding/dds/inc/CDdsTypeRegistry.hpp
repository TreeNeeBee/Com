/**
 * @file        CDdsTypeRegistry.hpp
 * @author      Aii
 * @brief       Type registry for per-service DDS type adapters
 * @date        2026/02/24
 * @details     Stores IDdsTypeAdapter instances keyed by (serviceId, elementId).
 *              When the DDS binding creates a topic it queries this registry;
 *              if a per-service adapter is registered, the adapter's TypeSupport
 *              is used instead of the built-in DdsPayload fallback.
 *
 *              Registration happens at process startup when generated service
 *              binding code calls RegisterAdapter().
 *
 * @copyright   Copyright (c) 2026
 * @reference   GENERATOR.md §11 — Dual-layer IDL Design
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/24  <td>1.0      <td>Aii     <td>init version
 * </table>
 */

#ifndef LAP_COM_DDS_CDDSTYPEREGISTRY_HPP
#define LAP_COM_DDS_CDDSTYPEREGISTRY_HPP

// ==================== Project-Internal Headers ====================
#include "IDdsTypeAdapter.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/CTypedef.hpp>
#include <lap/core/CSync.hpp>

// ==================== Standard Library Headers ====================
#include <unordered_map>

namespace lap
{
namespace com
{
namespace binding
{

    using lap::core::Mutex;
    using lap::core::LockGuard;
    using lap::core::UInt32;
    using lap::core::UInt64;
    using lap::core::String;

    // ====================================================================
    // CDdsTypeRegistry — Singleton Type Registry
    // ====================================================================

    /**
     * @brief   Singleton registry for per-service DDS type adapters
     *
     * @details Thread-safe.  Lookup is O(1) hash-map access.
     *          Key format: "{serviceId}_{elementId}" (hex strings).
     *
     * @note    Adapters are NOT owned by the registry — the caller must
     *          ensure the adapter outlives the registry.  Typically,
     *          adapters are process-global singletons.
     */
    class CDdsTypeRegistry
    {
    public:
        /**
         * @brief   Access the process-global singleton
         */
        static CDdsTypeRegistry& Instance() noexcept
        {
            static CDdsTypeRegistry instance;
            return instance;
        }

        // Non-copyable, non-movable
        CDdsTypeRegistry( const CDdsTypeRegistry& )            = delete;
        CDdsTypeRegistry& operator=( const CDdsTypeRegistry& ) = delete;
        CDdsTypeRegistry( CDdsTypeRegistry&& )                  = delete;
        CDdsTypeRegistry& operator=( CDdsTypeRegistry&& )       = delete;

    public:
        // ================================================================
        // Registration
        // ================================================================

        /**
         * @brief   Register an adapter for a specific (serviceId, elementId)
         * @param   serviceId  AUTOSAR service identifier
         * @param   elementId  Event / method / field identifier
         * @param   pAdapter   Non-owning pointer to the adapter
         * @note    Overwrites any previously registered adapter for the same key
         */
        void RegisterAdapter( UInt64 serviceId, UInt32 elementId,
                              const IDdsTypeAdapter* pAdapter ) noexcept
        {
            LockGuard lock( m_mutex );
            m_mapAdapters[makeKey( serviceId, elementId )] = pAdapter;
        }

        /**
         * @brief   Unregister an adapter
         */
        void UnregisterAdapter( UInt64 serviceId, UInt32 elementId ) noexcept
        {
            LockGuard lock( m_mutex );
            m_mapAdapters.erase( makeKey( serviceId, elementId ) );
        }

    public:
        // ================================================================
        // Lookup
        // ================================================================

        /**
         * @brief   Find a registered adapter for the given key
         * @return  Pointer to the adapter, or nullptr if none registered
         */
        const IDdsTypeAdapter* FindAdapter( UInt64 serviceId,
                                            UInt32 elementId ) const noexcept
        {
            LockGuard lock( m_mutex );
            auto it = m_mapAdapters.find( makeKey( serviceId, elementId ) );
            if ( it != m_mapAdapters.end() ) {
                return it->second;
            }
            return nullptr;
        }

        /**
         * @brief   Check if any adapter is registered for a service
         */
        bool HasAdapter( UInt64 serviceId, UInt32 elementId ) const noexcept
        {
            return FindAdapter( serviceId, elementId ) != nullptr;
        }

    private:
        CDdsTypeRegistry() = default;

        static String makeKey( UInt64 serviceId, UInt32 elementId ) noexcept
        {
            // Reuse the same hex key format as CDdsCodec
            char buf[32];
            ::std::snprintf( buf, sizeof( buf ), "%llx_%x",
                             static_cast< unsigned long long >( serviceId ),
                             static_cast< unsigned >( elementId ) );
            return String( buf );
        }

    private:
        mutable Mutex   m_mutex;
        ::std::unordered_map< String, const IDdsTypeAdapter* >  m_mapAdapters;
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_DDS_CDDSTYPEREGISTRY_HPP
