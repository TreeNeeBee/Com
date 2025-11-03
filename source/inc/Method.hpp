/**
 * @file        Method.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR Adaptive Platform Method Communication
 * @date        2025-10-30
 * @details     Method-based communication for proxies and skeletons (SWS_CM Section 9.4)
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 SWS_CM compliant
 * @version     1.0
 */
#ifndef LAP_COM_METHOD_HPP
#define LAP_COM_METHOD_HPP

#include "ComTypes.hpp"
#include <core/CResult.hpp>
#include <core/CFuture.hpp>

#include <functional>
#include <mutex>

namespace lap
{
namespace com
{
    // ========================================================================
    // Proxy-Side Method (SWS_CM_00800)
    // ========================================================================
    
    /**
     * @brief Proxy-side method for calling remote functions
     * @tparam Output Return type of the method
     * @tparam Args Argument types for the method
     * @note SWS_CM_00800 - Remote method invocation
     */
    template<typename Output, typename... Args>
    class ProxyMethod
    {
    public:
        /**
         * @brief Constructor
         * @note SWS_CM_00801
         */
        ProxyMethod() noexcept = default;
        
        /**
         * @brief Destructor
         * @note SWS_CM_00802
         */
        ~ProxyMethod() noexcept = default;
        
        /**
         * @brief Call method synchronously (blocking)
         * @param args Method arguments
         * @return Result containing method output or error
         * @note SWS_CM_00803
         */
        Result<Output> operator()(Args... args) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (!m_isConnected)
            {
                return Result<Output>::FromError(
                    MakeErrorCode(ComErrc::kServiceNotAvailable, 0));
            }
            
            // Serialize arguments and send request
            // Wait for response synchronously
            return DoSyncCall(std::forward<Args>(args)...);
        }
        
        /**
         * @brief Call method asynchronously (non-blocking)
         * @param args Method arguments
         * @return Future containing method output
         * @note SWS_CM_00804
         */
        lap::core::Future<Output> CallAsync(Args... args) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (!m_isConnected)
            {
                lap::core::Promise<Output> promise;
                promise.SetError(MakeErrorCode(ComErrc::kServiceNotAvailable, 0));
                return promise.GetFuture();
            }
            
            // Serialize arguments and send request
            // Return future for asynchronous result retrieval
            return DoAsyncCall(std::forward<Args>(args)...);
        }
        
        /**
         * @brief Check if method is connected
         * @return true if connected, false otherwise
         * @note SWS_CM_00805
         */
        bool IsConnected() const noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_isConnected;
        }
        
        // Move-only type
        ProxyMethod(ProxyMethod&&) noexcept = default;
        ProxyMethod& operator=(ProxyMethod&&) noexcept = default;
        ProxyMethod(const ProxyMethod&) = delete;
        ProxyMethod& operator=(const ProxyMethod&) = delete;
        
    private:
        mutable std::mutex m_mutex;
        bool m_isConnected{false};
        
        /**
         * @brief Implementation-specific synchronous call
         * @param args Method arguments
         * @return Result containing output or error
         */
        Result<Output> DoSyncCall(Args... args) noexcept
        {
            // Serialize and transmit request via network binding
            // Block waiting for response
            // Deserialize response
            
            // Placeholder implementation
            return Result<Output>::FromError(
                MakeErrorCode(ComErrc::kCommunicationLinkError, 0));
        }
        
        /**
         * @brief Implementation-specific asynchronous call
         * @param args Method arguments
         * @return Future for result
         */
        lap::core::Future<Output> DoAsyncCall(Args... args) noexcept
        {
            lap::core::Promise<Output> promise;
            
            // Serialize and transmit request via network binding
            // Register callback for response
            // Return future immediately
            
            // Placeholder implementation
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
    // Skeleton-Side Method (SWS_CM_00820)
    // ========================================================================
    
    /**
     * @brief Skeleton-side method for handling remote calls
     * @tparam Output Return type of the method
     * @tparam Args Argument types for the method
     * @note SWS_CM_00820 - Method request handling
     */
    template<typename Output, typename... Args>
    class SkeletonMethod
    {
    public:
        using HandlerType = std::function<lap::core::Future<Output>(Args...)>;
        
        /**
         * @brief Constructor
         * @note SWS_CM_00821
         */
        SkeletonMethod() noexcept = default;
        
        /**
         * @brief Destructor
         * @note SWS_CM_00822
         */
        ~SkeletonMethod() noexcept = default;
        
        /**
         * @brief Register method implementation handler
         * @param handler Function to handle method calls
         * @return Result indicating success or error
         * @note SWS_CM_00823
         */
        Result<void> RegisterMethodHandler(HandlerType handler) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (m_handler)
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kSetHandlerNotSet, 0));
            }
            
            m_handler = std::move(handler);
            return Result<void>::FromValue();
        }
        
        /**
         * @brief Unregister method handler
         * @note SWS_CM_00824
         */
        void UnregisterMethodHandler() noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_handler = nullptr;
        }
        
        /**
         * @brief Check if handler is registered
         * @return true if handler is set, false otherwise
         * @note SWS_CM_00825
         */
        bool HasHandler() const noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_handler != nullptr;
        }
        
        // Move-only type
        SkeletonMethod(SkeletonMethod&&) noexcept = default;
        SkeletonMethod& operator=(SkeletonMethod&&) noexcept = default;
        SkeletonMethod(const SkeletonMethod&) = delete;
        SkeletonMethod& operator=(const SkeletonMethod&) = delete;
        
    private:
        mutable std::mutex m_mutex;
        HandlerType m_handler{nullptr};
        
        /**
         * @brief Internal: Process incoming method call
         * @param args Deserialized arguments from request
         * @return Future containing method result
         */
        lap::core::Future<Output> ProcessCall(Args... args) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (!m_handler)
            {
                lap::core::Promise<Output> promise;
                promise.SetError(MakeErrorCode(ComErrc::kSetHandlerNotSet, 0));
                return promise.GetFuture();
            }
            
            // Invoke user-provided handler
            return m_handler(std::forward<Args>(args)...);
        }
        
        friend class SkeletonBase;
    };
    
    // ========================================================================
    // Fire-and-Forget Method (SWS_CM_00840)
    // ========================================================================
    
    /**
     * @brief Proxy-side fire-and-forget method (no response expected)
     * @tparam Args Argument types for the method
     * @note SWS_CM_00840
     */
    template<typename... Args>
    class ProxyFireAndForgetMethod
    {
    public:
        /**
         * @brief Constructor
         * @note SWS_CM_00841
         */
        ProxyFireAndForgetMethod() noexcept = default;
        
        /**
         * @brief Destructor
         * @note SWS_CM_00842
         */
        ~ProxyFireAndForgetMethod() noexcept = default;
        
        /**
         * @brief Call fire-and-forget method
         * @param args Method arguments
         * @return Result indicating transmission success or error
         * @note SWS_CM_00843
         */
        Result<void> operator()(Args... args) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (!m_isConnected)
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kServiceNotAvailable, 0));
            }
            
            // Serialize and transmit without waiting for response
            return DoCall(std::forward<Args>(args)...);
        }
        
        /**
         * @brief Check if method is connected
         * @return true if connected, false otherwise
         * @note SWS_CM_00844
         */
        bool IsConnected() const noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_isConnected;
        }
        
        // Move-only type
        ProxyFireAndForgetMethod(ProxyFireAndForgetMethod&&) noexcept = default;
        ProxyFireAndForgetMethod& operator=(ProxyFireAndForgetMethod&&) noexcept = default;
        ProxyFireAndForgetMethod(const ProxyFireAndForgetMethod&) = delete;
        ProxyFireAndForgetMethod& operator=(const ProxyFireAndForgetMethod&) = delete;
        
    private:
        mutable std::mutex m_mutex;
        bool m_isConnected{false};
        
        /**
         * @brief Implementation-specific transmission
         * @param args Method arguments
         * @return Result indicating transmission success
         */
        Result<void> DoCall(Args... args) noexcept
        {
            // Serialize and transmit without response handling
            return Result<void>::FromValue();
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
    
    /**
     * @brief Skeleton-side fire-and-forget method handler
     * @tparam Args Argument types for the method
     * @note SWS_CM_00850
     */
    template<typename... Args>
    class SkeletonFireAndForgetMethod
    {
    public:
        using HandlerType = std::function<void(Args...)>;
        
        /**
         * @brief Constructor
         * @note SWS_CM_00851
         */
        SkeletonFireAndForgetMethod() noexcept = default;
        
        /**
         * @brief Destructor
         * @note SWS_CM_00852
         */
        ~SkeletonFireAndForgetMethod() noexcept = default;
        
        /**
         * @brief Register method handler
         * @param handler Function to handle method calls
         * @return Result indicating success or error
         * @note SWS_CM_00853
         */
        Result<void> RegisterMethodHandler(HandlerType handler) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (m_handler)
            {
                return Result<void>::FromError(
                    MakeErrorCode(ComErrc::kSetHandlerNotSet, 0));
            }
            
            m_handler = std::move(handler);
            return Result<void>::FromValue();
        }
        
        /**
         * @brief Unregister method handler
         * @note SWS_CM_00854
         */
        void UnregisterMethodHandler() noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_handler = nullptr;
        }
        
        // Move-only type
        SkeletonFireAndForgetMethod(SkeletonFireAndForgetMethod&&) noexcept = default;
        SkeletonFireAndForgetMethod& operator=(SkeletonFireAndForgetMethod&&) noexcept = default;
        SkeletonFireAndForgetMethod(const SkeletonFireAndForgetMethod&) = delete;
        SkeletonFireAndForgetMethod& operator=(const SkeletonFireAndForgetMethod&) = delete;
        
    private:
        mutable std::mutex m_mutex;
        HandlerType m_handler{nullptr};
        
        /**
         * @brief Internal: Process incoming call
         * @param args Deserialized arguments
         */
        void ProcessCall(Args... args) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (m_handler)
            {
                m_handler(std::forward<Args>(args)...);
            }
        }
        
        friend class SkeletonBase;
    };
    
} // namespace com
} // namespace lap

#endif // LAP_COM_METHOD_HPP
