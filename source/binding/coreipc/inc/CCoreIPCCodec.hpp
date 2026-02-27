/**
 * @file        CCoreIPCCodec.hpp
 * @author      LightAP Development Team
 * @brief       Core IPC binding — Path/key generation and message encoding/decoding
 * @date        2026/02/07
 * @copyright   Copyright (c) 2026
 *
 * @details     Pure-static utility class providing:
 *              - SHM path generation (service, method-request, method-response)
 *              - Composite key generation for map lookups
 *              - Event / Method message binary encoding and decoding
 *              - Shared-memory segment creation helper
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii             <td>Extracted from monolithic CoreIPCBinding
 * </table>
 */

#ifndef LAP_COM_CORE_IPC_CCOREIPCCODEC_HPP
#define LAP_COM_CORE_IPC_CCOREIPCCODEC_HPP

// ==================== Project-Internal Headers ====================
#include "CoreIPCTypes.hpp"
#include "ITransportBinding.hpp"

// ==================== Cross-Module Headers ====================
#include <lap/core/CResult.hpp>

namespace lap
{
namespace com
{
namespace binding
{

    using lap::core::Result;

    // ====================================================================
    // CCoreIPCCodec
    // ====================================================================

    /**
     * @brief   Pure-static utility class for CoreIPC addressing and wire-format
     *
     * @details Provides all stateless helpers needed by the CoreIPC managers:
     *          path generation, key computation, binary codec, SHM creation.
     *          Not instantiable — all methods are static.
     *
     * @note    Thread-safe (all methods are stateless)
     */
    class CCoreIPCCodec
    {
    public:
        CCoreIPCCodec()                                     = delete;
        CCoreIPCCodec( const CCoreIPCCodec& )               = delete;
        CCoreIPCCodec& operator=( const CCoreIPCCodec& )    = delete;

    public:
        // ================================================================
        // Path Generation
        // ================================================================

        /**
         * @brief   Generate SHM path for a service's event channel
         */
        static String MakeServicePath( UInt64 serviceId,
                                        UInt64 instanceId ) noexcept;

        /**
         * @brief   Generate SHM path for method-request channel (MPSC)
         */
        static String MakeMethodRequestPath( UInt64 serviceId,
                                              UInt64 instanceId ) noexcept;

        /**
         * @brief   Generate SHM path for method-response channel (SPMC)
         */
        static String MakeMethodResponsePath( UInt64 serviceId,
                                               UInt64 instanceId ) noexcept;

    public:
        // ================================================================
        // Key Generation
        // ================================================================

        static UInt64 MakeServiceKey( UInt64 serviceId,
                                       UInt64 instanceId ) noexcept;

        static UInt64 MakeEventKey( UInt64 serviceId,
                                     UInt64 instanceId,
                                     UInt32 eventId ) noexcept;

    public:
        // ================================================================
        // Event Encoding / Decoding
        // ================================================================

        static ByteBuffer EncodeEventMessage( UInt32 eventId,
                                               const ByteBuffer& payload ) noexcept;

        static Bool DecodeEventMessage( const UInt8* data, Size size,
                                         UInt32& eventId,
                                         UInt32& payloadSize,
                                         Size& payloadOffset ) noexcept;

    public:
        // ================================================================
        // Method Encoding / Decoding
        // ================================================================

        static ByteBuffer EncodeMethodMessage( UInt32 methodId,
                                                UInt64 clientToken,
                                                Int32 status,
                                                const ByteBuffer& payload ) noexcept;

        static Bool DecodeMethodMessage( const UInt8* data, Size size,
                                          UInt32& methodId,
                                          UInt64& clientToken,
                                          Int32& status,
                                          UInt32& payloadSize,
                                          Size& payloadOffset ) noexcept;

    public:
        // ================================================================
        // Shared Memory Helper
        // ================================================================

        /**
         * @brief   Ensure a shared-memory segment exists (create or open)
         *
         * @param   shmPath         SHM path name
         * @param   config          SHM configuration
         * @param   mapShmSegments  Segment map — caller must hold its lock
         * @return  Result< void >  Success or error
         *
         * @note    Caller must hold the SHM-map lock before calling
         */
        static Result< void > EnsureSharedMemory(
            const String& shmPath,
            const lap::core::ipc::SharedMemoryConfig& config,
            ShmSegmentMap& mapShmSegments ) noexcept;
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_CORE_IPC_CCOREIPCCODEC_HPP
