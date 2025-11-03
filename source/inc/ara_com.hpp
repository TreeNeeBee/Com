/**
 * @file        ara_com.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR Adaptive Platform Communication Management - Main Header
 * @date        2025-10-30
 * @details     Main include file for ara::com - AUTOSAR Communication Management
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 SWS_CM compliant
 * @version     1.0
 * 
 * This file provides the complete AUTOSAR Adaptive Platform Communication Management API
 * according to AUTOSAR_AP_SWS_CommunicationManagement specification.
 * 
 * Key Features:
 * - Service-Oriented Architecture (SOA) communication
 * - Service Discovery (FindService, OfferService)
 * - Proxy/Skeleton pattern for client/server communication
 * - Event-based communication with subscriptions
 * - Method calls (synchronous/asynchronous, fire-and-forget)
 * - Field access (getter/setter/notification)
 * - End-to-End (E2E) protection
 * - Serialization/deserialization framework
 * - Multiple network bindings (D-Bus, SOME/IP, DDS)
 * 
 * Usage Example:
 * ```cpp
 * // Initialize communication runtime
 * auto initResult = ara::com::Runtime::Initialize();
 * 
 * // Find service instances
 * auto handles = ara::com::FindService<MyService>(instanceSpec);
 * 
 * // Create proxy and call methods
 * auto proxyResult = MyService::Proxy::CreateProxy(handles[0]);
 * if (proxyResult.HasValue()) {
 *     auto& proxy = proxyResult.Value();
 *     auto result = proxy.Method1(arg1, arg2);
 * }
 * 
 * // Create skeleton and offer service
 * MyService::Skeleton skeleton(instanceSpec);
 * skeleton.OfferService();
 * 
 * // Cleanup
 * ara::com::Runtime::Deinitialize();
 * ```
 */
#ifndef LAP_COM_ARA_COM_HPP
#define LAP_COM_ARA_COM_HPP

// ============================================================================
// Section 8: Communication Management Foundation
// ============================================================================

// 8.1: Error Codes and Types
#include "ComTypes.hpp"

// 8.2: Service Handle and Identification
#include "ServiceHandleType.hpp"

// 8.3: Runtime and Service Discovery
#include "Runtime.hpp"

// ============================================================================
// Section 9: Service-Oriented Communication
// ============================================================================

// 9.1: Proxy Pattern (Client-Side)
#include "ProxyBase.hpp"

// 9.2: Skeleton Pattern (Server-Side)
#include "SkeletonBase.hpp"

// 9.3: Event Communication
#include "Event.hpp"

// 9.4: Method Communication
#include "Method.hpp"

// 9.5: Field Communication
#include "Field.hpp"

// ============================================================================
// Section 10: Communication Binding and E2E Protection
// ============================================================================

// 10.1: End-to-End Protection
#include "E2EProtection.hpp"

// 10.2: Serialization Framework
#include "Serialization.hpp"

/**
 * @namespace lap::com
 * @brief AUTOSAR Adaptive Platform Communication Management namespace
 * 
 * This namespace contains all types, classes, and functions defined by the
 * AUTOSAR Adaptive Platform Communication Management specification (SWS_CM).
 * 
 * The ara::com API enables service-oriented communication between applications
 * in an AUTOSAR Adaptive Platform system. It provides:
 * 
 * - Service discovery and lifecycle management
 * - Type-safe client/server communication via Proxy/Skeleton pattern
 * - Event-based publish/subscribe communication
 * - Method calls with various invocation modes
 * - Field access with getter/setter/notification semantics
 * - End-to-end protection for safety-critical communication
 * - Multiple network binding support (D-Bus, SOME/IP, DDS, etc.)
 * 
 * @note All APIs follow AUTOSAR R22-11 specification
 * @note Thread-safety: All APIs are thread-safe unless explicitly documented otherwise
 * @note Real-time: Critical paths avoid dynamic memory allocation where possible
 */
namespace lap
{
namespace com
{
    /**
     * @brief API version information
     */
    struct Version
    {
        static constexpr lap::core::UInt8 Major = 1;
        static constexpr lap::core::UInt8 Minor = 0;
        static constexpr lap::core::UInt8 Patch = 0;
        
        static constexpr const char* String = "1.0.0";
        static constexpr const char* Specification = "AUTOSAR R22-11";
    };
    
    /**
     * @brief Get ara::com library version
     * @return Version string
     */
    inline const char* GetVersion() noexcept
    {
        return Version::String;
    }
    
    /**
     * @brief Get AUTOSAR specification version
     * @return Specification version string
     */
    inline const char* GetSpecificationVersion() noexcept
    {
        return Version::Specification;
    }
    
} // namespace com
} // namespace lap

#endif // LAP_COM_ARA_COM_HPP
