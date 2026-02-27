/**
 * @file        RegistryIpcMessage.hpp
 * @author      LightAP Development Team
 * @brief       Registry IPC request/response message definitions
 * @date        2026/02/06
 * @details     Defines messages for Core IPC-based registry service.
 *              Architecture: MPSC (REQ) + SPMC (RESP) channels
 *              - MPSC: Multiple publishers -> Registry service (requests)
 *              - SPMC: Registry service -> Multiple subscribers (responses)
 * @copyright   Copyright (c) 2026
 * @note        Design rationale:
 *              - Registry shared memory is read-only for clients
 *              - All modifications go through IPC request/response
 *              - Ensures atomic updates and consistency
 *              - Supports distributed tracing and auditing
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §2.3 (Registry Service)
 *              Core IPC_DESIGN_ARCHITECTURE.md §4 (Message Patterns)
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2026/02/05  <td>1.0      <td>LightAP Team    <td>Initial implementation
 * <tr><td>2026/02/06  <td>2.0      <td>Aii             <td>Code style optimization per code_rules.md
 * </table>
 */
#ifndef LAP_COM_REGISTRY_IPC_MESSAGE_HPP
#define LAP_COM_REGISTRY_IPC_MESSAGE_HPP

// ==================== Cross-Module Headers ====================
#include <lap/core/CTypedef.hpp>

// ==================== Standard Library Headers ====================
#include <cstdint>
#include <cstring>
#include <unistd.h>

namespace lap
{
namespace com
{
namespace registry
{
    using lap::core::UInt8;
    using lap::core::UInt32;
    using lap::core::UInt64;
    using lap::core::Bool;

    /**
     * @brief Registry operation types
     */
    enum class RegistryOpType : UInt8
    {
        kRegisterService    = 1,  ///< Register a new service
        kUnregisterService  = 2,  ///< Unregister an existing service
        kUpdateHeartbeat    = 3,  ///< Update service heartbeat timestamp
        kQueryService       = 4   ///< Query service info (future extension)
    };

    /**
     * @brief Registry operation result codes
     */
    enum class RegistryResultCode : UInt8
    {
        kSuccess            = 0,  ///< Operation successful
        kSlotOccupied       = 1,  ///< Target slot already occupied
        kServiceNotFound    = 2,  ///< Service not found in registry
        kInvalidSlot        = 3,  ///< Invalid slot index (0 or >1023)
        kPermissionDenied   = 4,  ///< Insufficient permissions (ASIL registry)
        kInternalError      = 5   ///< Internal registry error
    };

    /**
     * @brief Registry request message (sent to MPSC channel)
     *
     * @details Message layout (160 bytes, 32-byte aligned):
     *          - [0-7]    request_id (correlation ID)
     *          - [8]      op_type
     *          - [9-15]   padding
     *          - [16-23]  service_id
     *          - [24-31]  instance_id
     *          - [32-35]  major_version
     *          - [36-39]  minor_version
     *          - [40-55]  binding_type (16 bytes)
     *          - [56-135] endpoint (80 bytes)
     *          - [136-143] timestamp_ns (for heartbeat updates)
     *          - [144-147] sender_pid (for access control)
     *          - [148-151] reserved
     *          - [152-159] reserved
     *          Total: 160 bytes (aligned to 32 bytes)
     * @note Not thread-safe
     */
    struct alignas( 32 ) RegistryRequest
    {
        UInt64              m_requestId;           ///< Unique request ID (correlation ID)
        RegistryOpType      m_opType;              ///< Operation type
        UInt8               m_padding1[7];         ///< Alignment padding

        UInt64              m_serviceId;           ///< Service ID
        UInt64              m_instanceId;          ///< Instance ID
        UInt32              m_majorVersion;        ///< Major version
        UInt32              m_minorVersion;        ///< Minor version

        char                m_bindingType[16];     ///< Binding type ("iceoryx2", "dds", etc.)
        char                m_endpoint[80];        ///< Endpoint address

        UInt64              m_timestampNs;         ///< Timestamp (for heartbeat updates)
        UInt32              m_senderPid;           ///< Sender process ID (for auditing)
        UInt32              m_reserved1;           ///< Reserved for future use
        UInt64              m_reserved2;           ///< Reserved for future use

        /**
         * @brief Default constructor
         */
        RegistryRequest() noexcept
            : m_requestId( 0 )
            , m_opType( RegistryOpType::kRegisterService )
            , m_padding1{}
            , m_serviceId( 0 )
            , m_instanceId( 0 )
            , m_majorVersion( 0 )
            , m_minorVersion( 0 )
            , m_bindingType{}
            , m_endpoint{}
            , m_timestampNs( 0 )
            , m_senderPid( 0 )
            , m_reserved1( 0 )
            , m_reserved2( 0 )
        {
            std::memset( m_bindingType, 0, sizeof( m_bindingType ) );
            std::memset( m_endpoint, 0, sizeof( m_endpoint ) );
        }

        /**
         * @brief Create a register service request
         * @param reqId     Unique request ID
         * @param svcId     Service ID
         * @param instId    Instance ID
         * @param majorVer  Major version
         * @param minorVer  Minor version
         * @param bindType  Binding type string
         * @param endp      Endpoint address string
         * @return RegistryRequest Populated request message
         */
        static RegistryRequest CreateRegisterRequest(
            UInt64 reqId,
            UInt64 svcId,
            UInt64 instId,
            UInt32 majorVer,
            UInt32 minorVer,
            const char* bindType,
            const char* endp ) noexcept
        {
            RegistryRequest req;
            req.m_requestId = reqId;
            req.m_opType = RegistryOpType::kRegisterService;
            req.m_serviceId = svcId;
            req.m_instanceId = instId;
            req.m_majorVersion = majorVer;
            req.m_minorVersion = minorVer;
            req.m_senderPid = static_cast< UInt32 > ( getpid() );

            std::strncpy( req.m_bindingType, bindType, sizeof( req.m_bindingType ) - 1 );
            req.m_bindingType[sizeof( req.m_bindingType ) - 1] = '\0';

            std::strncpy( req.m_endpoint, endp, sizeof( req.m_endpoint ) - 1 );
            req.m_endpoint[sizeof( req.m_endpoint ) - 1] = '\0';

            return req;
        }

        /**
         * @brief Create an unregister service request
         * @param reqId Unique request ID
         * @param svcId Service ID
         * @return RegistryRequest Populated request message
         */
        static RegistryRequest CreateUnregisterRequest(
            UInt64 reqId,
            UInt64 svcId ) noexcept
        {
            RegistryRequest req;
            req.m_requestId = reqId;
            req.m_opType = RegistryOpType::kUnregisterService;
            req.m_serviceId = svcId;
            req.m_senderPid = static_cast< UInt32 > ( getpid() );
            return req;
        }

        /**
         * @brief Create a heartbeat update request
         * @param reqId     Unique request ID
         * @param svcId     Service ID
         * @param timestamp Heartbeat timestamp in nanoseconds
         * @return RegistryRequest Populated request message
         */
        static RegistryRequest CreateHeartbeatRequest(
            UInt64 reqId,
            UInt64 svcId,
            UInt64 timestamp ) noexcept
        {
            RegistryRequest req;
            req.m_requestId = reqId;
            req.m_opType = RegistryOpType::kUpdateHeartbeat;
            req.m_serviceId = svcId;
            req.m_timestampNs = timestamp;
            req.m_senderPid = static_cast< UInt32 > ( getpid() );
            return req;
        }

        /**
         * @brief Create a query service request
         * @param reqId Unique request ID
         * @param svcId Service ID to query
         * @return RegistryRequest Populated request message
         */
        static RegistryRequest CreateQueryRequest(
            UInt64 reqId,
            UInt64 svcId ) noexcept
        {
            RegistryRequest req;
            req.m_requestId = reqId;
            req.m_opType = RegistryOpType::kQueryService;
            req.m_serviceId = svcId;
            req.m_senderPid = static_cast< UInt32 > ( getpid() );
            return req;
        }
    };

    /**
     * @brief Registry response message (sent to SPMC channel)
     *
     * @details Message layout (96 bytes, 32-byte aligned):
     *          - [0-7]   request_id (correlation ID)
     *          - [8]     result_code
     *          - [9]     op_type (echo from request)
     *          - [10-15] padding
     *          - [16-23] service_id (echo from request)
     *          - [24-27] assigned_slot_index (for successful registrations)
     *          - [28-31] reserved
     *          - [32-95] error_message (64 bytes, human-readable)
     *          Total: 96 bytes (aligned to 32 bytes)
     * @note Not thread-safe
     */
    struct alignas( 32 ) RegistryResponse
    {
        UInt64              m_requestId;                ///< Correlation ID (matches request)
        RegistryResultCode  m_resultCode;               ///< Operation result
        RegistryOpType      m_opType;                   ///< Operation type (echo)
        UInt8               m_padding[6];               ///< Alignment padding

        UInt64              m_serviceId;                ///< Service ID (echo)
        UInt32              m_assignedSlotIndex;        ///< Assigned slot (for register ops)
        UInt32              m_instanceId;               ///< Instance ID (for query ops)

        char                m_errorMessage[64];         ///< Error message or endpoint (for query ops)

        /**
         * @brief Default constructor
         */
        RegistryResponse() noexcept
            : m_requestId( 0 )
            , m_resultCode( RegistryResultCode::kSuccess )
            , m_opType( RegistryOpType::kRegisterService )
            , m_padding{}
            , m_serviceId( 0 )
            , m_assignedSlotIndex( 0 )
            , m_instanceId( 0 )
            , m_errorMessage{}
        {
            std::memset( m_errorMessage, 0, sizeof( m_errorMessage ) );
        }

        /**
         * @brief Create a success response
         * @param reqId   Correlation ID from request
         * @param op      Operation type from request
         * @param svcId   Service ID from request
         * @param slotIdx Assigned slot index (for register ops)
         * @return RegistryResponse Success response message
         */
        static RegistryResponse CreateSuccess(
            UInt64 reqId,
            RegistryOpType op,
            UInt64 svcId,
            UInt32 slotIdx = 0 ) noexcept
        {
            RegistryResponse resp;
            resp.m_requestId = reqId;
            resp.m_resultCode = RegistryResultCode::kSuccess;
            resp.m_opType = op;
            resp.m_serviceId = svcId;
            resp.m_assignedSlotIndex = slotIdx;
            return resp;
        }

        /**
         * @brief Create an error response
         * @param reqId     Correlation ID from request
         * @param op        Operation type from request
         * @param svcId     Service ID from request
         * @param errorCode Error result code
         * @param errorMsg  Human-readable error message
         * @return RegistryResponse Error response message
         */
        static RegistryResponse CreateError(
            UInt64 reqId,
            RegistryOpType op,
            UInt64 svcId,
            RegistryResultCode errorCode,
            const char* errorMsg ) noexcept
        {
            RegistryResponse resp;
            resp.m_requestId = reqId;
            resp.m_resultCode = errorCode;
            resp.m_opType = op;
            resp.m_serviceId = svcId;

            std::strncpy( resp.m_errorMessage, errorMsg, sizeof( resp.m_errorMessage ) - 1 );
            resp.m_errorMessage[sizeof( resp.m_errorMessage ) - 1] = '\0';

            return resp;
        }

        /**
         * @brief Check if operation was successful
         * @return true if result code is kSuccess
         */
        [[nodiscard]] Bool IsSuccess() const noexcept
        {
            return m_resultCode == RegistryResultCode::kSuccess;
        }

        /**
         * @brief Create a successful query response with service info
         * @param reqId      Correlation ID from request
         * @param svcId      Service ID
         * @param instId     Instance ID
         * @param slotIdx    Slot index
         * @param endpoint   Service endpoint string
         * @return RegistryResponse Query success response
         */
        static RegistryResponse CreateQuerySuccess(
            UInt64 reqId,
            UInt64 svcId,
            UInt32 instId,
            UInt32 slotIdx,
            const char* endpoint ) noexcept
        {
            RegistryResponse resp;
            resp.m_requestId = reqId;
            resp.m_resultCode = RegistryResultCode::kSuccess;
            resp.m_opType = RegistryOpType::kQueryService;
            resp.m_serviceId = svcId;
            resp.m_assignedSlotIndex = slotIdx;
            resp.m_instanceId = instId;

            // Store endpoint in m_errorMessage field (reused for query responses)
            std::strncpy( resp.m_errorMessage, endpoint, sizeof( resp.m_errorMessage ) - 1 );
            resp.m_errorMessage[sizeof( resp.m_errorMessage ) - 1] = '\0';

            return resp;
        }
    };

    // ==================== Static Assertions ====================
    static_assert( sizeof( RegistryRequest ) == 160,
                   "RegistryRequest must be 160 bytes" );
    static_assert( sizeof( RegistryResponse ) == 96,
                   "RegistryResponse must be 96 bytes" );
    static_assert( alignof( RegistryRequest ) == 32,
                   "RegistryRequest must be 32-byte aligned" );
    static_assert( alignof( RegistryResponse ) == 32,
                   "RegistryResponse must be 32-byte aligned" );

} // namespace registry
} // namespace com
} // namespace lap

#endif // LAP_COM_REGISTRY_IPC_MESSAGE_HPP
