/**
 * @file        ITransportBinding.hpp
 * @author      LightAP Development Team
 * @brief       Transport binding interface for ara::com
 * @date        2025-11-21
 * @details     Abstract interface for all transport bindings (iceoryx2, DDS, SOME/IP, etc.)
 *              Defines lifecycle management and communication primitives.
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R24-11 Compliance:
 *              - SWS_CM_00400: Transport Binding Interface
 *              - SWS_CM_00401: Binding Lifecycle Management
 * @reference   IMPLEMENTATION_PLAN_UPDATED.md Phase 2
 *              ARCHITECTURE_SUMMARY.md §7.2 ITransportBinding
 *              AUTOSAR_AP_SWS_CommunicationManagement.pdf §8.3
 * sdk:
 * platform:    Linux 5.10+
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/21  <td>1.0      <td>LightAP Team    <td>Initial transport binding interface
 * </table>
 */
#ifndef LAP_COM_BINDING_ITRANSPORT_BINDING_HPP
#define LAP_COM_BINDING_ITRANSPORT_BINDING_HPP

#include "BindingTypes.hpp"

#include <lap/core/CResult.hpp>
#include <lap/core/COptional.hpp>

#include <cstdint>
#include <vector>
#include <functional>
#include <memory>

namespace lap
{
namespace com
{
namespace binding
{
    using lap::core::Result;
    using lap::core::Optional;

    /**
     * @brief Byte buffer type for serialized data
     */
    using ByteBuffer = std::vector<uint8_t>;

    /**
     * @brief Event callback function type
     * @param service_id AUTOSAR service ID
     * @param instance_id AUTOSAR instance ID
     * @param event_id Event identifier
     * @param data Serialized event data
     */
    using EventCallback = std::function<void(
        uint64_t service_id,
        uint64_t instance_id,
        uint32_t event_id,
        const ByteBuffer& data
    )>;

    /**
     * @brief Method request callback function type
     * @param service_id AUTOSAR service ID
     * @param instance_id AUTOSAR instance ID
     * @param method_id Method identifier
     * @param request Serialized request data
     * @return ByteBuffer Serialized response data
     */
    using MethodCallback = std::function<ByteBuffer(
        uint64_t service_id,
        uint64_t instance_id,
        uint32_t method_id,
        const ByteBuffer& request
    )>;

    /**
     * @brief Abstract transport binding interface
     * 
     * @details All transport bindings must implement this interface.
     *          Bindings are loaded dynamically via dlopen() and created
     *          through exported factory functions.
     * 
     * @note Thread-safety: Implementations must be thread-safe for
     *       concurrent OfferService/FindService/Send operations.
     * 
     * @example Plugin implementation:
     *          extern "C" {
     *              ITransportBinding* CreateBindingInstance() {
     *                  return new MyBinding();
     *              }
     *              
     *              void DestroyBindingInstance(ITransportBinding* instance) {
     *                  delete instance;
     *              }
     *          }
     */
    class ITransportBinding
    {
    public:
        /**
         * @brief Virtual destructor
         */
        virtual ~ITransportBinding() = default;

        // ====================================================================
        // Lifecycle Management
        // ====================================================================

        /**
         * @brief Initialize binding with configuration
         * @return Result<void> Success or error code
         * 
         * @note Called once after binding is loaded
         * @note Must be idempotent (safe to call multiple times)
         */
        virtual Result<void> Initialize() noexcept = 0;

        /**
         * @brief Shutdown binding and release resources
         * @return Result<void> Success or error code
         * 
         * @note Called before unloading binding
         * @note Must cleanup all offered/subscribed services
         */
        virtual Result<void> Shutdown() noexcept = 0;

        // ====================================================================
        // Service Management (Provider Side)
        // ====================================================================

        /**
         * @brief Offer a service instance
         * @param service_id AUTOSAR service ID
         * @param instance_id AUTOSAR instance ID
         * @return Result<void> Success or error code
         * 
         * @note AUTOSAR SWS_CM_00002: OfferService
         * @note Makes service discoverable to consumers
         */
        virtual Result<void> OfferService(
            uint64_t service_id,
            uint64_t instance_id
        ) noexcept = 0;

        /**
         * @brief Stop offering a service instance
         * @param service_id AUTOSAR service ID
         * @param instance_id AUTOSAR instance ID
         * @return Result<void> Success or error code
         * 
         * @note AUTOSAR SWS_CM_00003: StopOfferService
         */
        virtual Result<void> StopOfferService(
            uint64_t service_id,
            uint64_t instance_id
        ) noexcept = 0;

        // ====================================================================
        // Service Discovery (Consumer Side)
        // ====================================================================

        /**
         * @brief Find available service instances
         * @param service_id AUTOSAR service ID
         * @return Result<std::vector<uint64_t>> List of available instance IDs
         * 
         * @note AUTOSAR SWS_CM_00001: FindService
         * @note Returns all instances currently offered
         */
        virtual Result<std::vector<uint64_t>> FindService(
            uint64_t service_id
        ) noexcept = 0;

        // ====================================================================
        // Event Communication
        // ====================================================================

        /**
         * @brief Send event to subscribers
         * @param service_id AUTOSAR service ID
         * @param instance_id AUTOSAR instance ID
         * @param event_id Event identifier
         * @param data Serialized event data
         * @return Result<void> Success or error code
         * 
         * @note AUTOSAR SWS_CM_00103: Send event
         * @note Called by service provider
         */
        virtual Result<void> SendEvent(
            uint64_t service_id,
            uint64_t instance_id,
            uint32_t event_id,
            const ByteBuffer& data
        ) noexcept = 0;

        /**
         * @brief Subscribe to service events
         * @param service_id AUTOSAR service ID
         * @param instance_id AUTOSAR instance ID
         * @param event_id Event identifier
         * @param callback Event notification callback
         * @return Result<void> Success or error code
         * 
         * @note AUTOSAR SWS_CM_00141: Subscribe
         * @note Called by service consumer
         */
        virtual Result<void> SubscribeEvent(
            uint64_t service_id,
            uint64_t instance_id,
            uint32_t event_id,
            EventCallback callback
        ) noexcept = 0;

        /**
         * @brief Unsubscribe from service events
         * @param service_id AUTOSAR service ID
         * @param instance_id AUTOSAR instance ID
         * @param event_id Event identifier
         * @return Result<void> Success or error code
         * 
         * @note AUTOSAR SWS_CM_00151: Unsubscribe
         */
        virtual Result<void> UnsubscribeEvent(
            uint64_t service_id,
            uint64_t instance_id,
            uint32_t event_id
        ) noexcept = 0;

        // ====================================================================
        // Method Communication
        // ====================================================================

        /**
         * @brief Call remote method (synchronous)
         * @param service_id AUTOSAR service ID
         * @param instance_id AUTOSAR instance ID
         * @param method_id Method identifier
         * @param request Serialized request data
         * @return Result<ByteBuffer> Serialized response or error
         * 
         * @note AUTOSAR SWS_CM_00191: Method call
         * @note Called by service consumer
         * @note Blocks until response received or timeout
         */
        virtual Result<ByteBuffer> CallMethod(
            uint64_t service_id,
            uint64_t instance_id,
            uint32_t method_id,
            const ByteBuffer& request
        ) noexcept = 0;

        /**
         * @brief Register method handler (provider side)
         * @param service_id AUTOSAR service ID
         * @param instance_id AUTOSAR instance ID
         * @param method_id Method identifier
         * @param handler Method implementation callback
         * @return Result<void> Success or error code
         * 
         * @note Called by service provider to handle incoming requests
         */
        virtual Result<void> RegisterMethod(
            uint64_t service_id,
            uint64_t instance_id,
            uint32_t method_id,
            MethodCallback handler
        ) noexcept = 0;

        // ====================================================================
        // Field Communication (Get/Set)
        // ====================================================================

        /**
         * @brief Get field value
         * @param service_id AUTOSAR service ID
         * @param instance_id AUTOSAR instance ID
         * @param field_id Field identifier
         * @return Result<ByteBuffer> Serialized field value or error
         * 
         * @note AUTOSAR SWS_CM_00120: Get field
         */
        virtual Result<ByteBuffer> GetField(
            uint64_t service_id,
            uint64_t instance_id,
            uint32_t field_id
        ) noexcept = 0;

        /**
         * @brief Set field value
         * @param service_id AUTOSAR service ID
         * @param instance_id AUTOSAR instance ID
         * @param field_id Field identifier
         * @param value Serialized field value
         * @return Result<void> Success or error code
         * 
         * @note AUTOSAR SWS_CM_00121: Set field
         */
        virtual Result<void> SetField(
            uint64_t service_id,
            uint64_t instance_id,
            uint32_t field_id,
            const ByteBuffer& value
        ) noexcept = 0;

        // ====================================================================
        // Diagnostics and Monitoring
        // ====================================================================

        /**
         * @brief Get binding name (for debugging)
         * @return Binding name string
         */
        virtual const char* GetName() const noexcept = 0;

        /**
         * @brief Get binding version
         * @return Version as uint32_t (e.g., 0x00010000 for 1.0.0)
         */
        virtual uint32_t GetVersion() const noexcept = 0;

        // ====================================================================
        // Performance and Capability Queries (ARCHITECTURE_SUMMARY.md §7.2)
        // ====================================================================

        /**
         * @brief Get binding priority for selection algorithm
         * @return Priority value (higher = preferred)
         * 
         * @note Priority scale (ARCHITECTURE_SUMMARY.md):
         *       - 100: iceoryx2 (zero-copy IPC)
         *       - 80:  DDS (network with AF_XDP)
         *       - 60:  SOME/IP (automotive standard)
         *       - 40:  Socket (fallback)
         *       - 20:  D-Bus (legacy)
         */
        virtual uint32_t GetPriority() const noexcept = 0;

        /**
         * @brief Check if binding supports zero-copy communication
         * @return true if zero-copy capable (e.g., iceoryx2)
         * 
         * @note Used by BindingSelector for optimization decisions
         */
        virtual bool SupportsZeroCopy() const noexcept = 0;

        /**
         * @brief Check if binding can handle a specific service
         * @param service_id AUTOSAR service ID
         * @return true if binding supports this service
         * 
         * @note Used by BindingManager::SelectBestBinding()
         * @example iceoryx2 may only support local services
         *          DDS may only support network services
         */
        virtual bool SupportsService(uint64_t service_id) const noexcept = 0;

        /**
         * @brief Get transport performance metrics
         * @return TransportMetrics Performance statistics
         * 
         * @note Metrics structure defined in BindingTypes.hpp
         */
        virtual TransportMetrics GetMetrics() const noexcept = 0;
    };

} // namespace binding
} // namespace com
} // namespace lap

#endif // LAP_COM_BINDING_ITRANSPORT_BINDING_HPP
