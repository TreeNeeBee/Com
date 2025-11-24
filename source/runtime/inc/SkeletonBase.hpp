/**
 * @file        SkeletonBase.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR Adaptive Platform Service Skeleton Base Class
 * @date        2025-10-30
 * @details     Base class for all service skeletons (SWS_CM Section 8.5, 9.2)
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 SWS_CM compliant
 * @version     1.0
 */
#ifndef LAP_COM_SKELETON_BASE_HPP
#define LAP_COM_SKELETONBASE_HPP

#include "ComTypes.hpp"
#include <core/CResult.hpp>
#include <core/CInstanceSpecifier.hpp>

#include <mutex>

namespace lap
{
namespace com
{
    /**
     * @brief Base class for all service skeletons
     * @note SWS_CM_00600 - Abstract base for skeleton implementations
     */
    class SkeletonBase
    {
    public:
        /**
         * @brief Destructor
         * @note SWS_CM_00601
         */
        virtual ~SkeletonBase() noexcept = default;
        
        /**
         * @brief Offer the service
         * @return Result indicating success or error
         * @note SWS_CM_00602
         */
        Result<void> OfferService() noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (m_isOffered)
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kServiceNotOffered, 0));
            }
            
            // Register with service discovery and network binding
            auto result = DoOfferService();
            if (result.HasValue())
            {
                m_isOffered = true;
            }
            
            return result;
        }
        
        /**
         * @brief Stop offering the service
         * @note SWS_CM_00603
         */
        void StopOfferService() noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (m_isOffered)
            {
                DoStopOfferService();
                m_isOffered = false;
            }
        }
        
        /**
         * @brief Check if service is offered
         * @return true if service is offered, false otherwise
         * @note SWS_CM_00604
         */
        bool IsOffered() const noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_isOffered;
        }
        
        /**
         * @brief Process incoming requests (for poll mode)
         * @return Result indicating number of requests processed or error
         * @note SWS_CM_00605 - Used in kPoll processing mode
         */
        Result<lap::core::UInt32> ProcessNextMethodCall() noexcept
        {
            if (!m_isOffered)
            {
                return Result<lap::core::UInt32>::FromError(
                    MakeErrorCode(ComErrc::kServiceNotOffered, 0));
            }
            
            return DoProcessNextMethodCall();
        }
        
    protected:
        /**
         * @brief Protected constructor
         * @param instanceSpec Instance specifier for the service
         * @param mode Method call processing mode
         * @note SWS_CM_00606
         */
        explicit SkeletonBase(lap::core::InstanceSpecifier instanceSpec,
                             MethodCallProcessingMode mode = MethodCallProcessingMode::kEvent) noexcept
            : m_instanceSpecifier(std::move(instanceSpec))
            , m_processingMode(mode)
            , m_isOffered(false)
        {}
        
        /**
         * @brief Copy constructor (deleted)
         * @note Skeletons are not copyable
         */
        SkeletonBase(const SkeletonBase&) = delete;
        
        /**
         * @brief Move constructor
         * @note SWS_CM_00607
         */
        SkeletonBase(SkeletonBase&& other) noexcept
            : m_instanceSpecifier(std::move(other.m_instanceSpecifier))
            , m_processingMode(other.m_processingMode)
            , m_isOffered(other.m_isOffered)
        {
            other.m_isOffered = false;
        }
        
        /**
         * @brief Copy assignment (deleted)
         * @note Skeletons are not copyable
         */
        SkeletonBase& operator=(const SkeletonBase&) = delete;
        
        /**
         * @brief Move assignment
         * @note SWS_CM_00608
         */
        SkeletonBase& operator=(SkeletonBase&& other) noexcept
        {
            if (this != &other)
            {
                // Stop offering if currently offered
                if (m_isOffered)
                {
                    StopOfferService();
                }
                
                m_instanceSpecifier = std::move(other.m_instanceSpecifier);
                m_processingMode = other.m_processingMode;
                m_isOffered = other.m_isOffered;
                
                other.m_isOffered = false;
            }
            return *this;
        }
        
        /**
         * @brief Get instance specifier
         * @return Instance specifier
         */
        const lap::core::InstanceSpecifier& GetInstanceSpecifier() const noexcept
        {
            return m_instanceSpecifier;
        }
        
        /**
         * @brief Get method call processing mode
         * @return Processing mode
         */
        MethodCallProcessingMode GetProcessingMode() const noexcept
        {
            return m_processingMode;
        }
        
        /**
         * @brief Implementation-specific service offering
         * @return Result indicating success or error
         */
        virtual Result<void> DoOfferService() noexcept = 0;
        
        /**
         * @brief Implementation-specific service stop
         */
        virtual void DoStopOfferService() noexcept = 0;
        
        /**
         * @brief Implementation-specific method call processing
         * @return Number of requests processed
         */
        virtual Result<lap::core::UInt32> DoProcessNextMethodCall() noexcept
        {
            return Result<lap::core::UInt32>::FromError(
                MakeErrorCode(ComErrc::kWrongMethodCallProcessing, 0));
        }
        
    private:
        lap::core::InstanceSpecifier m_instanceSpecifier;
        MethodCallProcessingMode m_processingMode;
        bool m_isOffered;
        mutable std::mutex m_mutex;
    };
    
    /**
     * @brief Service Skeleton template
     * @tparam ServiceInterface Type of service interface
     * @note SWS_CM_00609 - Concrete skeleton for specific service
     */
    template<typename ServiceInterface>
    class ServiceSkeleton : public SkeletonBase
    {
    public:
        /**
         * @brief Constructor
         * @param instanceSpec Instance specifier for the service
         * @param mode Method call processing mode
         * @note SWS_CM_00610
         */
        explicit ServiceSkeleton(lap::core::InstanceSpecifier instanceSpec,
                                MethodCallProcessingMode mode = MethodCallProcessingMode::kEvent) noexcept
            : SkeletonBase(std::move(instanceSpec), mode)
        {}
        
        /**
         * @brief Destructor
         * @note SWS_CM_00611
         */
        ~ServiceSkeleton() noexcept override
        {
            // Automatically stop offering on destruction
            if (IsOffered())
            {
                StopOfferService();
            }
        }
        
        /**
         * @brief Move constructor
         * @note SWS_CM_00612
         */
        ServiceSkeleton(ServiceSkeleton&&) noexcept = default;
        
        /**
         * @brief Move assignment
         * @note SWS_CM_00613
         */
        ServiceSkeleton& operator=(ServiceSkeleton&&) noexcept = default;
        
        // Delete copy operations
        ServiceSkeleton(const ServiceSkeleton&) = delete;
        ServiceSkeleton& operator=(const ServiceSkeleton&) = delete;
        
    protected:
        /**
         * @brief Offer service implementation
         * @return Result indicating success or error
         */
        Result<void> DoOfferService() noexcept override
        {
            // Initialize network binding (D-Bus, SOME/IP, etc.)
            // Register service with discovery backend
            
            return Result<void>::FromValue();
        }
        
        /**
         * @brief Stop offering service implementation
         */
        void DoStopOfferService() noexcept override
        {
            // Unregister from discovery backend
            // Close network connections
        }
    };
    
} // namespace com
} // namespace lap

#endif // LAP_COM_SKELETON_BASE_HPP
