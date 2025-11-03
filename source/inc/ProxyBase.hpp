/**
 * @file        ProxyBase.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR Adaptive Platform Service Proxy Base Class
 * @date        2025-10-30
 * @details     Base class for all service proxies (SWS_CM Section 8.4, 9.1)
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 SWS_CM compliant
 * @version     1.0
 */
#ifndef LAP_COM_PROXY_BASE_HPP
#define LAP_COM_PROXY_BASE_HPP

#include "ComTypes.hpp"
#include "ServiceHandleType.hpp"
#include <core/CResult.hpp>

namespace lap
{
namespace com
{
    /**
     * @brief Base class for all service proxies
     * @note SWS_CM_00500 - Abstract base for proxy implementations
     */
    class ProxyBase
    {
    public:
        /**
         * @brief Destructor
         * @note SWS_CM_00501
         */
        virtual ~ProxyBase() noexcept = default;
        
        /**
         * @brief Check if proxy is valid and connected
         * @return true if proxy is connected to a service, false otherwise
         * @note SWS_CM_00502
         */
        bool IsValid() const noexcept
        {
            return m_isValid;
        }
        
        /**
         * @brief Get the service availability state
         * @return Service availability state
         * @note SWS_CM_00503
         */
        ServiceAvailabilityState GetServiceAvailability() const noexcept
        {
            return m_availabilityState;
        }
        
        /**
         * @brief Register handler for service availability changes
         * @param handler Callback for availability state changes
         * @param maxSampleCount Maximum number of queued state changes (0 = unlimited)
         * @return Result indicating success or error
         * @note SWS_CM_00504
         */
        Result<void> SetServiceAvailabilityHandler(
            ServiceAvailabilityHandler handler,
            lap::core::UInt32 maxSampleCount = 0) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_availabilityHandler = std::move(handler);
            m_maxSampleCount = maxSampleCount;
            return Result<void>::FromValue();
        }
        
        /**
         * @brief Unregister service availability handler
         * @note SWS_CM_00505
         */
        void UnsetServiceAvailabilityHandler() noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_availabilityHandler = nullptr;
        }
        
    protected:
        /**
         * @brief Protected constructor
         * @note SWS_CM_00506
         */
        ProxyBase() noexcept = default;
        
        /**
         * @brief Copy constructor (deleted)
         * @note Proxies are not copyable
         */
        ProxyBase(const ProxyBase&) = delete;
        
        /**
         * @brief Move constructor
         * @note SWS_CM_00507
         */
        ProxyBase(ProxyBase&& other) noexcept
            : m_isValid(other.m_isValid)
            , m_availabilityState(other.m_availabilityState)
            , m_availabilityHandler(std::move(other.m_availabilityHandler))
            , m_maxSampleCount(other.m_maxSampleCount)
        {
            other.m_isValid = false;
        }
        
        /**
         * @brief Copy assignment (deleted)
         * @note Proxies are not copyable
         */
        ProxyBase& operator=(const ProxyBase&) = delete;
        
        /**
         * @brief Move assignment
         * @note SWS_CM_00508
         */
        ProxyBase& operator=(ProxyBase&& other) noexcept
        {
            if (this != &other)
            {
                m_isValid = other.m_isValid;
                m_availabilityState = other.m_availabilityState;
                m_availabilityHandler = std::move(other.m_availabilityHandler);
                m_maxSampleCount = other.m_maxSampleCount;
                
                other.m_isValid = false;
            }
            return *this;
        }
        
        /**
         * @brief Notify availability state change
         * @param state New availability state
         */
        void NotifyAvailabilityChange(ServiceAvailabilityState state) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_availabilityState = state;
            
            if (m_availabilityHandler)
            {
                m_availabilityHandler(state);
            }
        }
        
        /**
         * @brief Set proxy validity
         * @param valid Validity flag
         */
        void SetValid(bool valid) noexcept
        {
            m_isValid = valid;
        }
        
    private:
        bool m_isValid{false};
        ServiceAvailabilityState m_availabilityState{ServiceAvailabilityState::kNotOffered};
        ServiceAvailabilityHandler m_availabilityHandler{nullptr};
        lap::core::UInt32 m_maxSampleCount{0};
        mutable std::mutex m_mutex;
    };
    
    /**
     * @brief Service Proxy template
     * @tparam ServiceInterface Type of service interface
     * @note SWS_CM_00509 - Concrete proxy for specific service
     */
    template<typename ServiceInterface>
    class ServiceProxy : public ProxyBase
    {
    public:
        using HandleType = ServiceHandleType<ServiceInterface>;
        
        /**
         * @brief Create proxy from service handle
         * @param handle Service handle obtained from FindService
         * @param mode Method call processing mode
         * @return Result containing proxy instance or error
         * @note SWS_CM_00510
         */
        static Result<ServiceProxy> CreateProxy(
            HandleType handle,
            MethodCallProcessingMode mode = MethodCallProcessingMode::kEvent) noexcept
        {
            if (!handle.IsValid())
            {
                return Result<ServiceProxy>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            ServiceProxy proxy(handle, mode);
            
            // Initialize communication binding
            // Implementation will set up D-Bus/SOME-IP connection
            
            proxy.SetValid(true);
            
            return Result<ServiceProxy>::FromValue(std::move(proxy));
        }
        
        /**
         * @brief Destructor
         * @note SWS_CM_00511
         */
        ~ServiceProxy() noexcept override = default;
        
        /**
         * @brief Move constructor
         * @note SWS_CM_00512
         */
        ServiceProxy(ServiceProxy&&) noexcept = default;
        
        /**
         * @brief Move assignment
         * @note SWS_CM_00513
         */
        ServiceProxy& operator=(ServiceProxy&&) noexcept = default;
        
        /**
         * @brief Get service handle
         * @return Service handle
         * @note SWS_CM_00514
         */
        const HandleType& GetHandle() const noexcept
        {
            return m_handle;
        }
        
        /**
         * @brief Get method call processing mode
         * @return Method call processing mode
         * @note SWS_CM_00515
         */
        MethodCallProcessingMode GetMethodCallProcessingMode() const noexcept
        {
            return m_processingMode;
        }
        
    protected:
        /**
         * @brief Protected constructor
         * @param handle Service handle
         * @param mode Method call processing mode
         */
        explicit ServiceProxy(HandleType handle, 
                            MethodCallProcessingMode mode) noexcept
            : ProxyBase()
            , m_handle(handle)
            , m_processingMode(mode)
        {}
        
        // Delete copy operations
        ServiceProxy(const ServiceProxy&) = delete;
        ServiceProxy& operator=(const ServiceProxy&) = delete;
        
    private:
        HandleType m_handle;
        MethodCallProcessingMode m_processingMode;
    };
    
} // namespace com
} // namespace lap

#endif // LAP_COM_PROXY_BASE_HPP
