/**
 * @file        ComTypes.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR Adaptive Platform Communication Management Types
 * @date        2025-10-30
 * @details     Fundamental type definitions for ara::com according to AUTOSAR AP SWS Communication Management
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R22-11 SWS_CM compliant
 * @version     1.0
 */
#ifndef LAP_COM_COM_TYPES_HPP
#define LAP_COM_COM_TYPES_HPP

#include <core/CTypedef.hpp>
#include <core/CString.hpp>
#include <core/CResult.hpp>
#include <core/COptional.hpp>
#include <core/CInstanceSpecifier.hpp>

#include <cstdint>
#include <chrono>

namespace lap
{
namespace com
{
    // Import commonly used types from lap::core
    using lap::core::Result;
    using lap::core::Optional;
    using lap::core::String;
    using lap::core::StringView;
    using lap::core::InstanceSpecifier;
    using lap::core::ErrorCode;
    
    // ========================================================================
    // Communication Management Error Codes (SWS_CM_00300)
    // ========================================================================
    
    /**
     * @brief Communication Management Error Domain enumeration
     * @note SWS_CM_00302
     */
    enum class ComErrc : lap::core::ErrorDomain::CodeType
    {
        kServiceNotAvailable        = 1,    ///< Service is not available
        kMaxSamplesExceeded         = 2,    ///< Maximum number of samples exceeded
        kNetworkBindingFailure      = 3,    ///< Network binding failed
        kGrantEnforcementError      = 4,    ///< Grant enforcement error
        kFieldValueIsNotValid       = 5,    ///< Field value is not valid
        kSetHandlerNotSet           = 6,    ///< Set handler not set
        kUnsetFailure               = 7,    ///< Unset operation failed
        kIllegalUseOfAllocate       = 8,    ///< Illegal use of Allocate
        kBindingConnectionError     = 9,    ///< Binding connection error
        kCommunicationLinkError     = 10,   ///< Communication link error
        kNoClientsConnected         = 11,   ///< No clients connected
        kInvalidArgument            = 12,   ///< Invalid argument provided
        kServiceNotOffered          = 13,   ///< Service not offered
        kWrongMethodCallProcessing  = 14,   ///< Wrong method call processing mode
        kPeerIsUnreachable          = 15,   ///< Peer is unreachable
        kSampleAllocationFailure    = 16,   ///< Sample allocation failed
        kMaxSampleCountNotRealizable = 17,  ///< Maximum sample count not realizable
        kNotInitialized             = 18,   ///< Component not initialized
        kTimeout                    = 19,   ///< Operation timed out
        kMessageTooLarge            = 20,   ///< Message size exceeds limit
        kSerializationError         = 21,   ///< Serialization failed
        kDeserializationError       = 22,   ///< Deserialization failed
        kNotSupported               = 23,   ///< Operation not supported
        kInvalidState               = 24,   ///< Invalid state for operation
        kInternal                   = 25,   ///< Internal error
    };
    
    /**
     * @brief Communication Management Error Domain
     * @note SWS_CM_00301
     */
    class ComErrorDomain final : public lap::core::ErrorDomain
    {
    public:
        using Errc = ComErrc;
        using Exception = lap::core::Exception;
        
        constexpr ComErrorDomain() noexcept
            : ErrorDomain(ErrorDomain::IdType{0x8000000000000015})
        {}
        
        const char* Name() const noexcept override
        {
            return "Com";
        }
        
        const char* Message(CodeType errorCode) const noexcept override
        {
            auto code = static_cast<ComErrc>(errorCode);
            switch (code)
            {
                case ComErrc::kServiceNotAvailable:
                    return "Service is not available";
                case ComErrc::kMaxSamplesExceeded:
                    return "Maximum number of samples exceeded";
                case ComErrc::kNetworkBindingFailure:
                    return "Network binding failed";
                case ComErrc::kGrantEnforcementError:
                    return "Grant enforcement error";
                case ComErrc::kFieldValueIsNotValid:
                    return "Field value is not valid";
                case ComErrc::kSetHandlerNotSet:
                    return "Set handler not set";
                case ComErrc::kUnsetFailure:
                    return "Unset operation failed";
                case ComErrc::kIllegalUseOfAllocate:
                    return "Illegal use of Allocate";
                case ComErrc::kBindingConnectionError:
                    return "Binding connection error";
                case ComErrc::kCommunicationLinkError:
                    return "Communication link error";
                case ComErrc::kNoClientsConnected:
                    return "No clients connected";
                case ComErrc::kInvalidArgument:
                    return "Invalid argument provided";
                case ComErrc::kServiceNotOffered:
                    return "Service not offered";
                case ComErrc::kWrongMethodCallProcessing:
                    return "Wrong method call processing mode";
                case ComErrc::kPeerIsUnreachable:
                    return "Peer is unreachable";
                case ComErrc::kSampleAllocationFailure:
                    return "Sample allocation failed";
                case ComErrc::kMaxSampleCountNotRealizable:
                    return "Maximum sample count not realizable";
                case ComErrc::kNotInitialized:
                    return "Component not initialized";
                case ComErrc::kTimeout:
                    return "Operation timed out";
                case ComErrc::kMessageTooLarge:
                    return "Message size exceeds limit";
                case ComErrc::kSerializationError:
                    return "Serialization failed";
                case ComErrc::kDeserializationError:
                    return "Deserialization failed";
                case ComErrc::kNotSupported:
                    return "Operation not supported";
                case ComErrc::kInvalidState:
                    return "Invalid state for operation";
                case ComErrc::kInternal:
                    return "Internal error";
                default:
                    return "Unknown Communication Management error";
            }
        }
        
        void ThrowAsException(const ErrorCode& errorCode) const noexcept(false) override
        {
            throw Exception(errorCode);
        }
    };
    
    // Global instance of ComErrorDomain
    constexpr ComErrorDomain g_comErrorDomain;
    
    /**
     * @brief Get the Communication Management Error Domain
     * @return Reference to the global ComErrorDomain instance
     * @note SWS_CM_00303
     */
    constexpr const lap::core::ErrorDomain& GetComErrorDomain() noexcept
    {
        return g_comErrorDomain;
    }
    
    /**
     * @brief Create an ErrorCode for Communication Management errors
     * @param code Error code enumeration value
     * @param data Optional support data
     * @return ErrorCode instance
     * @note SWS_CM_00304
     */
    constexpr ErrorCode MakeErrorCode(ComErrc code, 
                                      lap::core::ErrorDomain::SupportDataType data = 
                                      lap::core::ErrorDomain::SupportDataType()) noexcept
    {
        return ErrorCode(static_cast<lap::core::ErrorDomain::CodeType>(code), 
                        GetComErrorDomain(), data);
    }
    
    // ========================================================================
    // Service Identifier Types (SWS_CM_00310)
    // ========================================================================
    
    /**
     * @brief Service identifier type
     * @note SWS_CM_00310
     */
    using ServiceIdentifierType = lap::core::UInt16;
    
    /**
     * @brief Instance identifier type
     * @note SWS_CM_00311
     */
    using InstanceIdentifierType = lap::core::UInt16;
    
    /**
     * @brief Service version
     * @note SWS_CM_00312
     */
    struct ServiceVersionType
    {
        lap::core::UInt8 majorVersion;  ///< Major version number
        lap::core::UInt32 minorVersion; ///< Minor version number
        
        constexpr ServiceVersionType(lap::core::UInt8 major = 0, 
                                    lap::core::UInt32 minor = 0) noexcept
            : majorVersion(major), minorVersion(minor)
        {}
        
        constexpr bool operator==(const ServiceVersionType& other) const noexcept
        {
            return majorVersion == other.majorVersion && minorVersion == other.minorVersion;
        }
        
        constexpr bool operator!=(const ServiceVersionType& other) const noexcept
        {
            return !(*this == other);
        }
        
        constexpr bool operator<(const ServiceVersionType& other) const noexcept
        {
            if (majorVersion != other.majorVersion)
                return majorVersion < other.majorVersion;
            return minorVersion < other.minorVersion;
        }
    };
    
    // ========================================================================
    // Handle Types (SWS_CM_00315)
    // ========================================================================
    
    /**
     * @brief Service handle container type
     * @tparam HandleType Type of service handle
     * @note SWS_CM_00315
     */
    template<typename HandleType>
    using ServiceHandleContainer = lap::core::Vector<HandleType>;
    
    /**
     * @brief FindServiceHandle type for searching services
     * @note SWS_CM_00316
     */
    using FindServiceHandle = lap::core::UInt64;
    
    // ========================================================================
    // Event and Method Types (SWS_CM_00320)
    // ========================================================================
    
    /**
     * @brief Sample pointer for event data
     * @tparam SampleType Type of sample data
     * @note SWS_CM_00320
     */
    template<typename SampleType>
    using SamplePtr = std::unique_ptr<const SampleType>;
    
    /**
     * @brief Sample allocation result
     * @tparam SampleType Type of sample data
     * @note SWS_CM_00321
     */
    template<typename SampleType>
    using SampleAllocateePtr = std::unique_ptr<SampleType>;
    
    /**
     * @brief Event receive handler callback
     * @tparam SampleType Type of event data
     * @note SWS_CM_00322
     */
    template<typename SampleType>
    using EventReceiveHandler = std::function<void()>;
    
    /**
     * @brief Subscription state enumeration
     * @note SWS_CM_00323
     */
    enum class SubscriptionState : lap::core::UInt8
    {
        kSubscribed         = 0,    ///< Subscription is active
        kNotSubscribed      = 1,    ///< No subscription
        kSubscriptionPending = 2     ///< Subscription is pending
    };
    
    // ========================================================================
    // Method Call Processing Modes (SWS_CM_00330)
    // ========================================================================
    
    /**
     * @brief Method call processing mode
     * @note SWS_CM_00330
     */
    enum class MethodCallProcessingMode : lap::core::UInt8
    {
        kPoll           = 0,    ///< Application polls for method results
        kEvent          = 1,    ///< Middleware triggers callback on completion
        kEventSingleThread = 2  ///< Single-threaded event processing
    };
    
    // ========================================================================
    // Service Discovery Types (SWS_CM_00340)
    // ========================================================================
    
    /**
     * @brief Service availability handler callback
     * @tparam HandleType Type of service handle
     * @note SWS_CM_00340
     */
    template<typename HandleType>
    using FindServiceHandler = std::function<void(ServiceHandleContainer<HandleType>, 
                                                  FindServiceHandle)>;
    
    /**
     * @brief Service availability state
     * @note SWS_CM_00341
     */
    enum class ServiceAvailabilityState : lap::core::UInt8
    {
        kOffered        = 0,    ///< Service is offered
        kNotOffered     = 1     ///< Service is not offered
    };
    
    /**
     * @brief Service availability handler
     * @note SWS_CM_00342
     */
    using ServiceAvailabilityHandler = std::function<void(ServiceAvailabilityState)>;
    
    // ========================================================================
    // E2E Protection Types (SWS_CM_00350)
    // ========================================================================
    
    /**
     * @brief End-to-End (E2E) protection result
     * @note SWS_CM_00350
     */
    enum class E2EResult : lap::core::UInt8
    {
        kOk                 = 0,    ///< E2E check passed
        kNotAvailable       = 1,    ///< E2E protection not available
        kNoNewData          = 2,    ///< No new data received
        kRepeated           = 3,    ///< Repeated data detected
        kWrongSequence      = 4,    ///< Wrong sequence number
        kError              = 5     ///< E2E check failed
    };
    
    /**
     * @brief E2E protection check status
     * @note SWS_CM_00351
     */
    struct E2ECheckStatus
    {
        E2EResult result;           ///< E2E check result
        lap::core::UInt32 counter;  ///< Message counter
        
        constexpr E2ECheckStatus(E2EResult res = E2EResult::kOk, 
                                lap::core::UInt32 cnt = 0) noexcept
            : result(res), counter(cnt)
        {}
    };
    
    // ========================================================================
    // Trigger Types (SWS_CM_00360)
    // ========================================================================
    
    /**
     * @brief Trigger for selective event subscription
     * @note SWS_CM_00360
     */
    class Trigger
    {
    public:
        virtual ~Trigger() = default;
        
        /**
         * @brief Check if trigger condition is met
         * @return true if triggered, false otherwise
         */
        virtual bool IsTriggered() const noexcept = 0;
        
        /**
         * @brief Reset trigger state
         */
        virtual void Reset() noexcept = 0;
    };
    
} // namespace com
} // namespace lap

#endif // LAP_COM_COM_TYPES_HPP
