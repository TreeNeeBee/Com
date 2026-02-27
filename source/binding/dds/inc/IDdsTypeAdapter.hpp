/**
 * @file        IDdsTypeAdapter.hpp
 * @author      Aii
 * @brief       Type adapter interface for per-service DDS type registration
 * @date        2026/02/24
 * @details     Defines the interface that generated DDS binding adapter code
 *              implements for each service interface.  When the lap-sidl-gen →
 *              fastddsgen pipeline produces per-service TypeSupport, a matching
 *              adapter class can be registered with CDdsTypeRegistry so the
 *              DDS binding uses the strongly-typed DDS message instead of the
 *              built-in DdsPayload envelope.
 *
 *              Phase 1 (current): Interface definition only.
 *              Phase 2 (future):  Generator produces adapter implementations.
 *
 * @copyright   Copyright (c) 2026
 * @reference   GENERATOR.md §11 — Dual-layer IDL Design
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/24  <td>1.0      <td>Aii     <td>init version
 * </table>
 */

#ifndef LAP_COM_DDS_IDDSTYPEADAPTER_HPP
#define LAP_COM_DDS_IDDSTYPEADAPTER_HPP

// ==================== Cross-Module Headers ====================
#include <lap/core/CTypedef.hpp>

// ==================== Project-Internal Headers ====================
#include "BindingTypes.hpp"

// ==================== Third-Party Headers ====================
#include <fastdds/dds/topic/TypeSupport.hpp>

namespace lap
{
namespace com
{
namespace binding
{

    using lap::core::UInt64;
    using lap::core::Size;

    // ====================================================================
    // IDdsTypeAdapter — Per-Service DDS Type Adapter Interface
    // ====================================================================

    /**
     * @brief   Abstract interface for per-service DDS type adapters
     *
     * @details A generated adapter bridges between the type-erased
     *          ITransportBinding NVI API (const void* / void*) and the
     *          strongly-typed DDS message produced by fastddsgen.
     *          The adapter knows how to:
     *
     *          1. Provide its TypeSupport for DDS topic registration
     *          2. Serialize a typed application object into a DDS sample
     *          3. Deserialize a received DDS sample into a typed object
     *          4. Manage correlation IDs for request/response RPC
     *
     *          Phase 1 (current): The runtime pre-serializes into ByteBuffer,
     *          so pData actually points to a ByteBuffer.  The default
     *          DdsPayload-based path (no adapter) handles this transparently.
     *
     *          Phase 2 (future): Generators produce per-service adapters
     *          that receive native typed objects and produce strongly-typed
     *          DDS samples (CDR via TypeSupport).
     *
     * @par Serialization Policy
     *          When Do* methods receive a void* data pointer, the binding
     *          MUST first look up an adapter via (serviceId, elementId) in
     *          CDdsTypeRegistry.  If found, use the adapter for serialization.
     *          If NOT found, fall back to memcpy with the provided dataSize.
     *
     * @note    Thread-safe implementations required (called from binding threads)
     */
    class IDdsTypeAdapter
    {
    public:
        virtual ~IDdsTypeAdapter() = default;

        // Non-copyable
        IDdsTypeAdapter( const IDdsTypeAdapter& )            = delete;
        IDdsTypeAdapter& operator=( const IDdsTypeAdapter& ) = delete;

    public:
        // ================================================================
        // Type Registration
        // ================================================================

        /**
         * @brief   Get the FastDDS TypeSupport for this service type
         * @return  TypeSupport wrapping the generated PubSubType
         */
        virtual eprosima::fastdds::dds::TypeSupport GetTypeSupport() const noexcept = 0;

    public:
        // ================================================================
        // Serialization (type-erased, matches NVI const void* convention)
        // ================================================================

        /**
         * @brief   Create a DDS sample from a type-erased application object
         * @param   pData     Pointer to the caller's typed object
         *                    (Phase 1: points to a ByteBuffer)
         * @param   dataSize  Size of the typed object in bytes (sizeof(T)),
         *                    0 if unknown.  Used as fallback for memcpy when
         *                    the adapter has no type-specific knowledge.
         * @return  Opaque pointer to a heap-allocated DDS sample
         * @note    Caller takes ownership — must call FreeSample() when done
         */
        virtual void* CreateSample( const void* pData,
                                    Size dataSize = 0 ) const = 0;

        /**
         * @brief   Extract application data pointer from a received DDS sample
         * @param   pSample  Opaque pointer to the received DDS sample
         * @return  Pointer to the application-level data within the sample;
         *          lifetime is bound to the sample (caller must NOT free it
         *          separately).  Returns nullptr on failure.
         * @note    The returned pointer points into the sample's internal
         *          storage — zero-copy extraction.  The caller should use
         *          the data before calling FreeSample().
         */
        virtual const void* ExtractData( const void* pSample ) const noexcept = 0;

        /**
         * @brief   Free a sample previously created by CreateSample()
         * @param   pSample  Opaque pointer to free
         */
        virtual void FreeSample( void* pSample ) const noexcept = 0;

    public:
        // ================================================================
        // Method RPC Correlation (optional — override for method types)
        // ================================================================

        /**
         * @brief   Set the correlation ID on a DDS sample (for method RPC)
         * @param   pSample    Opaque pointer to a DDS sample
         * @param   requestId  Correlation token
         */
        virtual void SetRequestId( void* pSample, UInt64 requestId ) const noexcept
        {
            static_cast< void >( pSample );
            static_cast< void >( requestId );
        }

        /**
         * @brief   Get the correlation ID from a received DDS sample
         * @param   pSample  Opaque pointer to a DDS sample
         * @return  Correlation token (0 if not applicable)
         */
        virtual UInt64 GetRequestId( const void* pSample ) const noexcept
        {
            static_cast< void >( pSample );
            return 0;
        }

    protected:
        IDdsTypeAdapter() = default;
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_DDS_IDDSTYPEADAPTER_HPP
