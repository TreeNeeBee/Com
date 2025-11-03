/**
 * @file        Field.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR Adaptive Platform Field Communication
 * @date        2025-10-30
 * @details     Field-based communication (getter/setter/notification) (SWS_CM Section 9.5)
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 SWS_CM compliant
 * @version     1.0
 */
#ifndef LAP_COM_FIELD_HPP
#define LAP_COM_FIELD_HPP

#include "ComTypes.hpp"
#include "Event.hpp"
#include <core/CResult.hpp>
#include <core/CFuture.hpp>

#include <functional>
#include <mutex>

namespace lap
{
namespace com
{
    // ========================================================================
    // Proxy-Side Field (SWS_CM_00900)
    // ========================================================================
    
    /**
     * @brief Proxy-side field for accessing remote data
     * @tparam FieldType Type of field data
     * @note SWS_CM_00900 - Field getter/setter/notification
     */
    template<typename FieldType>
    class ProxyField
    {
    public:
        /**
         * @brief Constructor
         * @param hasGetter Field supports Get operation
         * @param hasSetter Field supports Set operation
         * @param hasNotifier Field supports event notification
         * @note SWS_CM_00901
         */
        explicit ProxyField(bool hasGetter = true, 
                           bool hasSetter = false, 
                           bool hasNotifier = false) noexcept
            : m_hasGetter(hasGetter)
            , m_hasSetter(hasSetter)
            , m_hasNotifier(hasNotifier)
        {}
        
        /**
         * @brief Destructor
         * @note SWS_CM_00902
         */
        ~ProxyField() noexcept = default;
        
        /**
         * @brief Get field value synchronously
         * @return Result containing field value or error
         * @note SWS_CM_00903
         */
        Result<FieldType> Get() noexcept
        {
            if (!m_hasGetter)
            {
                return Result<FieldType>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (!m_isConnected)
            {
                return Result<FieldType>::FromError(
                    MakeErrorCode(ComErrc::kServiceNotAvailable, 0));
            }
            
            return DoGet();
        }
        
        /**
         * @brief Get field value asynchronously
         * @return Future containing field value
         * @note SWS_CM_00904
         */
        lap::core::Future<FieldType> GetAsync() noexcept
        {
            if (!m_hasGetter)
            {
                lap::core::Promise<FieldType> promise;
                promise.SetError(MakeErrorCode(ComErrc::kInvalidArgument, 0));
                return promise.GetFuture();
            }
            
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (!m_isConnected)
            {
                lap::core::Promise<FieldType> promise;
                promise.SetError(MakeErrorCode(ComErrc::kServiceNotAvailable, 0));
                return promise.GetFuture();
            }
            
            return DoGetAsync();
        }
        
        /**
         * @brief Set field value synchronously
         * @param value New field value
         * @return Result indicating success or error
         * @note SWS_CM_00905
         */
        Result<void> Set(const FieldType& value) noexcept
        {
            if (!m_hasSetter)
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (!m_isConnected)
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kServiceNotAvailable, 0));
            }
            
            return DoSet(value);
        }
        
        /**
         * @brief Set field value asynchronously
         * @param value New field value
         * @return Future for operation result
         * @note SWS_CM_00906
         */
        lap::core::Future<void> SetAsync(const FieldType& value) noexcept
        {
            if (!m_hasSetter)
            {
                lap::core::Promise<void> promise;
                promise.SetError(MakeErrorCode(ComErrc::kInvalidArgument, 0));
                return promise.GetFuture();
            }
            
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (!m_isConnected)
            {
                lap::core::Promise<void> promise;
                promise.SetError(MakeErrorCode(ComErrc::kServiceNotAvailable, 0));
                return promise.GetFuture();
            }
            
            return DoSetAsync(value);
        }
        
        /**
         * @brief Subscribe to field change notifications
         * @param maxSampleCount Maximum number of cached updates
         * @return Result indicating success or error
         * @note SWS_CM_00907
         */
        Result<void> Subscribe(lap::core::UInt32 maxSampleCount = 1) noexcept
        {
            if (!m_hasNotifier)
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            return m_event.Subscribe(maxSampleCount);
        }
        
        /**
         * @brief Unsubscribe from field change notifications
         * @note SWS_CM_00908
         */
        void Unsubscribe() noexcept
        {
            if (m_hasNotifier)
            {
                m_event.Unsubscribe();
            }
        }
        
        /**
         * @brief Get subscription state
         * @return Subscription state
         * @note SWS_CM_00909
         */
        SubscriptionState GetSubscriptionState() const noexcept
        {
            return m_event.GetSubscriptionState();
        }
        
        /**
         * @brief Get number of available update notifications
         * @return Number of cached updates
         * @note SWS_CM_00910
         */
        lap::core::UInt32 GetNewSamples() const noexcept
        {
            return m_event.GetNewSamples();
        }
        
        /**
         * @brief Get next field update notification
         * @param timeout Maximum wait time
         * @return Result containing updated field value or error
         * @note SWS_CM_00911
         */
        Result<SamplePtr<FieldType>> GetNextSample(
            std::chrono::milliseconds timeout = std::chrono::milliseconds(0)) noexcept
        {
            return m_event.GetNextSample(timeout);
        }
        
        /**
         * @brief Set handler for field update notifications
         * @param handler Callback for field changes
         * @return Result indicating success or error
         * @note SWS_CM_00912
         */
        Result<void> SetReceiveHandler(EventReceiveHandler<FieldType> handler) noexcept
        {
            if (!m_hasNotifier)
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            return m_event.SetReceiveHandler(std::move(handler));
        }
        
        /**
         * @brief Unset field update handler
         * @note SWS_CM_00913
         */
        void UnsetReceiveHandler() noexcept
        {
            m_event.UnsetReceiveHandler();
        }
        
        /**
         * @brief Check if field has getter
         * @return true if getter is available
         */
        bool HasGetter() const noexcept
        {
            return m_hasGetter;
        }
        
        /**
         * @brief Check if field has setter
         * @return true if setter is available
         */
        bool HasSetter() const noexcept
        {
            return m_hasSetter;
        }
        
        /**
         * @brief Check if field has notifier
         * @return true if notifier is available
         */
        bool HasNotifier() const noexcept
        {
            return m_hasNotifier;
        }
        
        // Move-only type
        ProxyField(ProxyField&&) noexcept = default;
        ProxyField& operator=(ProxyField&&) noexcept = default;
        ProxyField(const ProxyField&) = delete;
        ProxyField& operator=(const ProxyField&) = delete;
        
    private:
        mutable std::mutex m_mutex;
        bool m_hasGetter;
        bool m_hasSetter;
        bool m_hasNotifier;
        bool m_isConnected{false};
        ProxyEvent<FieldType> m_event;
        
        /**
         * @brief Implementation-specific synchronous get
         * @return Field value result
         */
        Result<FieldType> DoGet() noexcept
        {
            // Send getter request and wait for response
            return Result<FieldType>::FromError(
                MakeErrorCode(ComErrc::kCommunicationLinkError, 0));
        }
        
        /**
         * @brief Implementation-specific asynchronous get
         * @return Future for field value
         */
        lap::core::Future<FieldType> DoGetAsync() noexcept
        {
            lap::core::Promise<FieldType> promise;
            promise.SetError(MakeErrorCode(ComErrc::kCommunicationLinkError, 0));
            return promise.GetFuture();
        }
        
        /**
         * @brief Implementation-specific synchronous set
         * @param value New value
         * @return Operation result
         */
        Result<void> DoSet(const FieldType& value) noexcept
        {
            // Send setter request and wait for confirmation
            return Result<void>::FromError(
                MakeErrorCode(ComErrc::kCommunicationLinkError, 0));
        }
        
        /**
         * @brief Implementation-specific asynchronous set
         * @param value New value
         * @return Future for operation result
         */
        lap::core::Future<void> DoSetAsync(const FieldType& value) noexcept
        {
            lap::core::Promise<void> promise;
            promise.SetError(MakeErrorCode(ComErrc::kCommunicationLinkError, 0));
            return promise.GetFuture();
        }
        
        /**
         * @brief Internal: Set connection state
         * @param connected Connection state
         */
        void SetConnected(bool connected) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_isConnected = connected;
        }
        
        friend class ProxyBase;
    };
    
    // ========================================================================
    // Skeleton-Side Field (SWS_CM_00920)
    // ========================================================================
    
    /**
     * @brief Skeleton-side field for managing remote-accessible data
     * @tparam FieldType Type of field data
     * @note SWS_CM_00920 - Field value management
     */
    template<typename FieldType>
    class SkeletonField
    {
    public:
        using GetterHandlerType = std::function<lap::core::Future<FieldType>()>;
        using SetterHandlerType = std::function<lap::core::Future<void>(const FieldType&)>;
        
        /**
         * @brief Constructor
         * @param hasGetter Field supports Get operation
         * @param hasSetter Field supports Set operation
         * @param hasNotifier Field supports event notification
         * @note SWS_CM_00921
         */
        explicit SkeletonField(bool hasGetter = true, 
                              bool hasSetter = false, 
                              bool hasNotifier = false) noexcept
            : m_hasGetter(hasGetter)
            , m_hasSetter(hasSetter)
            , m_hasNotifier(hasNotifier)
        {}
        
        /**
         * @brief Destructor
         * @note SWS_CM_00922
         */
        ~SkeletonField() noexcept = default;
        
        /**
         * @brief Register getter handler
         * @param handler Function to provide field value
         * @return Result indicating success or error
         * @note SWS_CM_00923
         */
        Result<void> RegisterGetHandler(GetterHandlerType handler) noexcept
        {
            if (!m_hasGetter)
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            std::lock_guard<std::mutex> lock(m_mutex);
            m_getterHandler = std::move(handler);
            return Result<void>::FromValue();
        }
        
        /**
         * @brief Register setter handler
         * @param handler Function to update field value
         * @return Result indicating success or error
         * @note SWS_CM_00924
         */
        Result<void> RegisterSetHandler(SetterHandlerType handler) noexcept
        {
            if (!m_hasSetter)
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            std::lock_guard<std::mutex> lock(m_mutex);
            m_setterHandler = std::move(handler);
            return Result<void>::FromValue();
        }
        
        /**
         * @brief Update field value and notify subscribers
         * @param value New field value
         * @return Result indicating success or error
         * @note SWS_CM_00925
         */
        Result<void> Update(const FieldType& value) noexcept
        {
            if (!m_hasNotifier)
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kInvalidArgument, 0));
            }
            
            std::lock_guard<std::mutex> lock(m_mutex);
            
            // Allocate and send notification
            auto sampleResult = m_event.Allocate();
            if (!sampleResult.HasValue())
            {
                return Result<void>::FromError(sampleResult.Error());
            }
            
            auto sample = std::move(sampleResult).Value();
            *sample = value;
            
            return m_event.Send(std::move(sample));
        }
        
        /**
         * @brief Get number of subscribers
         * @return Subscriber count
         * @note SWS_CM_00926
         */
        lap::core::UInt32 GetSubscriberCount() const noexcept
        {
            return m_event.GetSubscriberCount();
        }
        
        // Move-only type
        SkeletonField(SkeletonField&&) noexcept = default;
        SkeletonField& operator=(SkeletonField&&) noexcept = default;
        SkeletonField(const SkeletonField&) = delete;
        SkeletonField& operator=(const SkeletonField&) = delete;
        
    private:
        mutable std::mutex m_mutex;
        bool m_hasGetter;
        bool m_hasSetter;
        bool m_hasNotifier;
        GetterHandlerType m_getterHandler{nullptr};
        SetterHandlerType m_setterHandler{nullptr};
        SkeletonEvent<FieldType> m_event;
        
        /**
         * @brief Internal: Process getter request
         * @return Future containing field value
         */
        lap::core::Future<FieldType> ProcessGet() noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (!m_getterHandler)
            {
                lap::core::Promise<FieldType> promise;
                promise.SetError(MakeErrorCode(ComErrc::kSetHandlerNotSet, 0));
                return promise.GetFuture();
            }
            
            return m_getterHandler();
        }
        
        /**
         * @brief Internal: Process setter request
         * @param value New field value
         * @return Future for operation result
         */
        lap::core::Future<void> ProcessSet(const FieldType& value) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (!m_setterHandler)
            {
                lap::core::Promise<void> promise;
                promise.SetError(MakeErrorCode(ComErrc::kSetHandlerNotSet, 0));
                return promise.GetFuture();
            }
            
            return m_setterHandler(value);
        }
        
        friend class SkeletonBase;
    };
    
} // namespace com
} // namespace lap

#endif // LAP_COM_FIELD_HPP
