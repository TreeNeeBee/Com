/**
 * @file        Runtime.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR Adaptive Platform Communication Management Runtime
 * @date        2025-10-30
 * @details     Service discovery and lifecycle management (SWS_CM Section 8.2, 10.1)
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 SWS_CM compliant
 * @version     1.0
 */
#ifndef LAP_COM_RUNTIME_HPP
#define LAP_COM_RUNTIME_HPP

#include "ComTypes.hpp"
#include "ServiceHandleType.hpp"
#include <core/CInstanceSpecifier.hpp>

#include <mutex>
#include <map>
#include <memory>

namespace lap
{
namespace com
{
    /**
     * @brief Communication Management Runtime
     * @note SWS_CM_00400 - Central class for ara::com initialization and service discovery
     */
    class Runtime
    {
    public:
        /**
         * @brief Initialize the Communication Management Runtime
         * @return Result indicating success or error
         * @note SWS_CM_00401
         */
        static Result<void> Initialize() noexcept;
        
        /**
         * @brief Deinitialize the Communication Management Runtime
         * @return Result indicating success or error
         * @note SWS_CM_00402
         */
        static Result<void> Deinitialize() noexcept;
        
        /**
         * @brief Get the singleton Runtime instance
         * @return Reference to Runtime instance
         * @note Internal API for accessing runtime state
         */
        static Runtime& GetInstance() noexcept;
        
        /**
         * @brief Check if runtime is initialized
         * @return true if initialized, false otherwise
         */
        static bool IsInitialized() noexcept;
        
        // Prevent copying and moving
        Runtime(const Runtime&) = delete;
        Runtime(Runtime&&) = delete;
        Runtime& operator=(const Runtime&) = delete;
        Runtime& operator=(Runtime&&) = delete;
        
    private:
        Runtime() = default;
        ~Runtime() = default;
        
        bool m_initialized{false};
        mutable std::mutex m_mutex;
        
        // Internal state for service management
        std::map<lap::core::String, lap::core::UInt64> m_serviceRegistry;
        
        friend class RuntimeImpl;
    };
    
    // ========================================================================
    // Service Discovery APIs (SWS_CM Section 8.2)
    // ========================================================================
    
    /**
     * @brief Find service instances (synchronous)
     * @tparam ServiceInterface Type of service interface
     * @param instanceIdentifier Instance specifier for the service
     * @return Container of service handles
     * @note SWS_CM_00410
     */
    template<typename ServiceInterface>
    ServiceHandleContainer<typename ServiceInterface::HandleType> FindService(
        lap::core::InstanceSpecifier instanceIdentifier) noexcept
    {
        ServiceHandleContainer<typename ServiceInterface::HandleType> result;
        
        // Query service discovery backend (D-Bus, SOME/IP, etc.)
        // For now, return empty container - implementation will integrate with ServiceDiscovery.hpp
        
        return result;
    }
    
    /**
     * @brief Find service instances (asynchronous with callback)
     * @tparam ServiceInterface Type of service interface
     * @param instanceIdentifier Instance specifier for the service
     * @param handler Callback function for service availability
     * @return FindServiceHandle for managing the search
     * @note SWS_CM_00411
     */
    template<typename ServiceInterface>
    FindServiceHandle StartFindService(
        lap::core::InstanceSpecifier instanceIdentifier,
        FindServiceHandler<typename ServiceInterface::HandleType> handler) noexcept
    {
        // Register callback with service discovery backend
        // Implementation will integrate with existing ServiceDiscovery
        
        return 0; // Return unique handle for this search
    }
    
    /**
     * @brief Stop finding service instances
     * @param handle FindServiceHandle returned by StartFindService
     * @note SWS_CM_00412
     */
    void StopFindService(FindServiceHandle handle) noexcept;
    
    // ========================================================================
    // Service Offering APIs (SWS_CM Section 8.3)
    // ========================================================================
    
    /**
     * @brief Offer a service instance
     * @tparam ServiceInterface Type of service interface
     * @param instanceIdentifier Instance specifier for the service
     * @return Result indicating success or error
     * @note SWS_CM_00420
     */
    template<typename ServiceInterface>
    Result<void> OfferService(lap::core::InstanceSpecifier instanceIdentifier) noexcept
    {
        if (!Runtime::IsInitialized())
        {
            return Result<void>::FromError(
                MakeErrorCode(ComErrc::kServiceNotOffered, 0));
        }
        
        // Register service with discovery backend
        // Implementation will announce service via D-Bus/SOME-IP
        
        return Result<void>::FromValue();
    }
    
    /**
     * @brief Stop offering a service instance
     * @tparam ServiceInterface Type of service interface
     * @param instanceIdentifier Instance specifier for the service
     * @note SWS_CM_00421
     */
    template<typename ServiceInterface>
    void StopOfferService(lap::core::InstanceSpecifier instanceIdentifier) noexcept
    {
        // Unregister service from discovery backend
        // Implementation will withdraw service announcement
    }
    
} // namespace com
} // namespace lap

#endif // LAP_COM_RUNTIME_HPP
