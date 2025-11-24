/**
 * @file        Event.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR Adaptive Platform Event Communication
 * @date        2025-10-30
 * @details     Event-based communication for proxies and skeletons (SWS_CM Section 9.3)
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 SWS_CM compliant
 * @version     1.0
 */
#ifndef LAP_COM_EVENT_HPP
#define LAP_COM_EVENT_HPP

#include "ComTypes.hpp"
#include <core/CResult.hpp>

#include <memory>
#include <mutex>
#include <queue>
#include <functional>

namespace lap
{
namespace com
{
    // ========================================================================
    // Proxy-Side Event (SWS_CM_00700)
    // ========================================================================
    
    /**
     * @brief Proxy-side event for receiving data
     * @tparam SampleType Type of event data
     * @note SWS_CM_00700 - Event subscription and reception
     */
    template<typename SampleType>
    class ProxyEvent
    {
    public:
        /**
         * @brief Constructor
         * @note SWS_CM_00701
         */
        ProxyEvent() noexcept = default;
        
        /**
         * @brief Destructor
         * @note SWS_CM_00702
         */
        ~ProxyEvent() noexcept
        {
            Unsubscribe();
        }
        
        /**
         * @brief Subscribe to event
         * @param maxSampleCount Maximum number of cached samples (0 = unlimited)
         * @return Result indicating success or error
         * @note SWS_CM_00703
         */
        Result<void> Subscribe(lap::core::UInt32 maxSampleCount = 1) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (m_subscriptionState == SubscriptionState::kSubscribed)
            {
                return Result<void>::FromValue();
            }
            
            m_maxSampleCount = maxSampleCount;
            m_subscriptionState = SubscriptionState::kSubscribed;
            
            // Register with network binding
            return Result<void>::FromValue();
        }
        
        /**
         * @brief Unsubscribe from event
         * @note SWS_CM_00704
         */
        void Unsubscribe() noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (m_subscriptionState == SubscriptionState::kSubscribed)
            {
                m_subscriptionState = SubscriptionState::kNotSubscribed;
                m_sampleQueue = std::queue<SamplePtr<SampleType>>{};
                m_receiveHandler = nullptr;
            }
        }
        
        /**
         * @brief Get subscription state
         * @return Current subscription state
         * @note SWS_CM_00705
         */
        SubscriptionState GetSubscriptionState() const noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_subscriptionState;
        }
        
        /**
         * @brief Get number of available samples
         * @return Number of samples in queue
         * @note SWS_CM_00706
         */
        lap::core::UInt32 GetNewSamples() const noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return static_cast<lap::core::UInt32>(m_sampleQueue.size());
        }
        
        /**
         * @brief Get next sample (blocking)
         * @param timeout Maximum wait time (0 = no wait)
         * @return Result containing sample pointer or error
         * @note SWS_CM_00707
         */
        Result<SamplePtr<SampleType>> GetNextSample(
            std::chrono::milliseconds timeout = std::chrono::milliseconds(0)) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (m_subscriptionState != SubscriptionState::kSubscribed)
            {
                return Result<SamplePtr<SampleType>>::FromError(
                    MakeErrorCode(ComErrc::kServiceNotAvailable, 0));
            }
            
            if (m_sampleQueue.empty())
            {
                return Result<SamplePtr<SampleType>>::FromError(
                    MakeErrorCode(ComErrc::kMaxSamplesExceeded, 0));
            }
            
            auto sample = std::move(m_sampleQueue.front());
            m_sampleQueue.pop();
            
            return Result<SamplePtr<SampleType>>::FromValue(std::move(sample));
        }
        
        /**
         * @brief Set event receive handler
         * @param handler Callback for new event data
         * @return Result indicating success or error
         * @note SWS_CM_00708
         */
        Result<void> SetReceiveHandler(EventReceiveHandler<SampleType> handler) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_receiveHandler = std::move(handler);
            return Result<void>::FromValue();
        }
        
        /**
         * @brief Unset event receive handler
         * @note SWS_CM_00709
         */
        void UnsetReceiveHandler() noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_receiveHandler = nullptr;
        }
        
        /**
         * @brief Get E2E protection status (if enabled)
         * @return E2E check status
         * @note SWS_CM_00710
         */
        E2ECheckStatus GetE2ECheckStatus() const noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_e2eStatus;
        }
        
        // Move-only type
        ProxyEvent(ProxyEvent&&) noexcept = default;
        ProxyEvent& operator=(ProxyEvent&&) noexcept = default;
        ProxyEvent(const ProxyEvent&) = delete;
        ProxyEvent& operator=(const ProxyEvent&) = delete;
        
    private:
        mutable std::mutex m_mutex;
        SubscriptionState m_subscriptionState{SubscriptionState::kNotSubscribed};
        lap::core::UInt32 m_maxSampleCount{1};
        std::queue<SamplePtr<SampleType>> m_sampleQueue;
        EventReceiveHandler<SampleType> m_receiveHandler{nullptr};
        E2ECheckStatus m_e2eStatus{};
        
        /**
         * @brief Internal: Push received sample to queue
         * @param sample Sample to enqueue
         */
        void PushSample(SamplePtr<SampleType> sample) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            // Drop oldest if max samples exceeded
            if (m_maxSampleCount > 0 && m_sampleQueue.size() >= m_maxSampleCount)
            {
                m_sampleQueue.pop();
            }
            
            m_sampleQueue.push(std::move(sample));
            
            // Notify handler if set
            if (m_receiveHandler)
            {
                m_receiveHandler();
            }
        }
        
        friend class EventBinding;
    };
    
    // ========================================================================
    // Skeleton-Side Event (SWS_CM_00720)
    // ========================================================================
    
    /**
     * @brief Skeleton-side event for sending data
     * @tparam SampleType Type of event data
     * @note SWS_CM_00720 - Event transmission
     */
    template<typename SampleType>
    class SkeletonEvent
    {
    public:
        /**
         * @brief Constructor
         * @note SWS_CM_00721
         */
        SkeletonEvent() noexcept = default;
        
        /**
         * @brief Destructor
         * @note SWS_CM_00722
         */
        ~SkeletonEvent() noexcept = default;
        
        /**
         * @brief Allocate sample for sending
         * @return Result containing allocated sample or error
         * @note SWS_CM_00723
         */
        Result<SampleAllocateePtr<SampleType>> Allocate() noexcept
        {
            auto sample = std::make_unique<SampleType>();
            if (!sample)
            {
                return Result<SampleAllocateePtr<SampleType>>::FromError(
                    MakeErrorCode(ComErrc::kSampleAllocationFailure, 0));
            }
            
            return Result<SampleAllocateePtr<SampleType>>::FromValue(std::move(sample));
        }
        
        /**
         * @brief Send allocated sample
         * @param sample Sample to send (ownership transferred)
         * @return Result indicating success or error
         * @note SWS_CM_00724
         */
        Result<void> Send(SampleAllocateePtr<SampleType> sample) noexcept
        {
            if (!sample)
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (!m_isOffered)
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kServiceNotOffered, 0));
            }
            
            // Transmit via network binding
            return DoSend(std::move(sample));
        }
        
        /**
         * @brief Get number of connected subscribers
         * @return Number of subscribers
         * @note SWS_CM_00725
         */
        lap::core::UInt32 GetSubscriberCount() const noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_subscriberCount;
        }
        
        // Move-only type
        SkeletonEvent(SkeletonEvent&&) noexcept = default;
        SkeletonEvent& operator=(SkeletonEvent&&) noexcept = default;
        SkeletonEvent(const SkeletonEvent&) = delete;
        SkeletonEvent& operator=(const SkeletonEvent&) = delete;
        
    private:
        mutable std::mutex m_mutex;
        bool m_isOffered{false};
        lap::core::UInt32 m_subscriberCount{0};
        
        /**
         * @brief Implementation-specific send
         * @param sample Sample to transmit
         * @return Result indicating success or error
         */
        Result<void> DoSend(SampleAllocateePtr<SampleType> sample) noexcept
        {
            // Serialize and transmit via network binding
            // Implementation will use D-Bus signals, SOME/IP events, etc.
            
            return Result<void>::FromValue();
        }
        
        /**
         * @brief Internal: Set offered state
         * @param offered Offering state
         */
        void SetOffered(bool offered) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_isOffered = offered;
        }
        
        friend class SkeletonBase;
    };
    
} // namespace com
} // namespace lap

#endif // LAP_COM_EVENT_HPP
