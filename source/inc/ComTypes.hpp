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
#include <lap/log/CLog.hpp>

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
    // Logging Configuration
    // ========================================================================
    #define LAP_COM_LOG_CONTEXT_ID       "COM"
    #define LAP_COM_LOG_CONTEXT_DESC     "COM log ctx"

    #define LAP_DEBUG

#ifdef LAP_DEBUG
    #define LAP_COM_LOG                  LAP_LOG( LAP_COM_LOG_CONTEXT_ID, LAP_COM_LOG_CONTEXT_DESC, ::lap::log::LogLevel::kVerbose )
    #define LAP_COM_LOG_VERBOSE          LAP_COM_LOG.LogVerbose().WithLocation( __FILE__, __LINE__ )
    #define LAP_COM_LOG_DEBUG            LAP_COM_LOG.LogDebug().WithLocation( __FILE__, __LINE__ )
    #define LAP_COM_LOG_INFO             LAP_COM_LOG.LogInfo().WithLocation( __FILE__, __LINE__ )
#else
    #define LAP_COM_LOG                  LAP_LOG( LAP_COM_LOG_CONTEXT_ID, LAP_COM_LOG_CONTEXT_DESC, ::lap::log::LogLevel::kWarn )
    #define LAP_COM_LOG_VERBOSE          LAP_COM_LOG.LogOff()
    #define LAP_COM_LOG_DEBUG            LAP_COM_LOG.LogOff()
    #define LAP_COM_LOG_INFO             LAP_COM_LOG.LogOff()
#endif
    #define LAP_COM_LOG_WARN             LAP_COM_LOG.LogWarn().WithLocation( __FILE__, __LINE__ )
    #define LAP_COM_LOG_ERROR            LAP_COM_LOG.LogError().WithLocation( __FILE__, __LINE__ )
    #define LAP_COM_LOG_FATAL            LAP_COM_LOG.LogFatal().WithLocation( __FILE__, __LINE__ )
    
    // ========================================================================
    // Communication Management Error Codes (SWS_CM_00300)
    // ========================================================================
    
    /**
     * @brief Communication Management Error Domain enumeration
     * @note SWS_CM_00302
     */
    enum class ComErrc : lap::core::ErrorDomain::CodeType
    {
        // ====================================================================
        // General Communication Errors (0x01 - 0x1F)
        // ====================================================================
        kServiceNotAvailable        = 0x01,  ///< Service is not available
        kMaxSamplesExceeded         = 0x02,  ///< Maximum number of samples exceeded
        kNetworkBindingFailure      = 0x03,  ///< Network binding failed
        kGrantEnforcementError      = 0x04,  ///< Grant enforcement error
        kFieldValueIsNotValid       = 0x05,  ///< Field value is not valid
        kSetHandlerNotSet           = 0x06,  ///< Set handler not set
        kUnsetFailure               = 0x07,  ///< Unset operation failed
        kIllegalUseOfAllocate       = 0x08,  ///< Illegal use of Allocate
        kBindingConnectionError     = 0x09,  ///< Binding connection error
        kCommunicationLinkError     = 0x0A,  ///< Communication link error
        kNoClientsConnected         = 0x0B,  ///< No clients connected
        kInvalidArgument            = 0x0C,  ///< Invalid argument provided
        kServiceNotOffered          = 0x0D,  ///< Service not offered
        kWrongMethodCallProcessing  = 0x0E,  ///< Wrong method call processing mode
        kPeerIsUnreachable          = 0x0F,  ///< Peer is unreachable
        kSampleAllocationFailure    = 0x10,  ///< Sample allocation failed
        kMaxSampleCountNotRealizable = 0x11, ///< Maximum sample count not realizable
        kNotInitialized             = 0x12,  ///< Component not initialized
        kTimeout                    = 0x13,  ///< Operation timed out
        kMessageTooLarge            = 0x14,  ///< Message size exceeds limit
        kSerializationError         = 0x15,  ///< Serialization failed
        kDeserializationError       = 0x16,  ///< Deserialization failed
        kNotSupported               = 0x17,  ///< Operation not supported
        kInvalidState               = 0x18,  ///< Invalid state for operation
        kInternal                   = 0x19,  ///< Internal error
        kNotImplemented             = 0x1A,  ///< Feature not yet implemented
        
        // ====================================================================
        // Registry-Specific Errors (0x100 - 0x1FF)
        // ====================================================================
        kSharedMemoryCreationFailed = 0x100, ///< Failed to create shared memory
        kSharedMemoryResizeFailed   = 0x101, ///< Failed to resize shared memory
        kSharedMemoryMappingFailed  = 0x102, ///< Failed to mmap shared memory
        kSlotIndexInvalid           = 0x103, ///< Slot index out of range or reserved
        kSlotConflict               = 0x104, ///< Slot already occupied by different service
        kSlotAlreadyReserved        = 0x105, ///< Slot already reserved
        kMemfdCreateFailed          = 0x106, ///< memfd_create system call failed
        kMemfdSealingFailed         = 0x107, ///< Failed to seal memfd
        kSocketCreationFailed       = 0x108, ///< Failed to create Unix domain socket
        kSocketBindFailed           = 0x109, ///< Failed to bind socket
        kSocketConnectFailed        = 0x10A, ///< Failed to connect to socket
        kSocketListenFailed         = 0x10B, ///< Failed to listen on socket
        kFdPassingFailed            = 0x10C, ///< Failed to pass file descriptor via SCM_RIGHTS
        kFdReceiveFailed            = 0x10D, ///< Failed to receive file descriptor
        kPermissionDenied           = 0x10E, ///< Insufficient permissions
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
                case ComErrc::kNotImplemented:
                    return "Feature not yet implemented";
                case ComErrc::kSharedMemoryCreationFailed:
                    return "Failed to create shared memory";
                case ComErrc::kSharedMemoryResizeFailed:
                    return "Failed to resize shared memory";
                case ComErrc::kSharedMemoryMappingFailed:
                    return "Failed to mmap shared memory";
                case ComErrc::kSlotIndexInvalid:
                    return "Slot index out of range or reserved";
                case ComErrc::kSlotConflict:
                    return "Slot already occupied by different service";
                case ComErrc::kSlotAlreadyReserved:
                    return "Slot already reserved";
                case ComErrc::kMemfdCreateFailed:
                    return "memfd_create system call failed";
                case ComErrc::kMemfdSealingFailed:
                    return "Failed to seal memfd";
                case ComErrc::kSocketCreationFailed:
                    return "Failed to create Unix domain socket";
                case ComErrc::kSocketBindFailed:
                    return "Failed to bind socket";
                case ComErrc::kSocketConnectFailed:
                    return "Failed to connect to socket";
                case ComErrc::kSocketListenFailed:
                    return "Failed to listen on socket";
                case ComErrc::kFdPassingFailed:
                    return "Failed to pass file descriptor via SCM_RIGHTS";
                case ComErrc::kFdReceiveFailed:
                    return "Failed to receive file descriptor";
                case ComErrc::kPermissionDenied:
                    return "Insufficient permissions";
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
