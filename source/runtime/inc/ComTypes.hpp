/**
 * @file        ComTypes.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR Adaptive Platform Communication Management Types
 * @date        2025-10-30
 * @details     Fundamental type definitions for ara::com according to AUTOSAR AP SWS Communication Management
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R25-11 SWS_CM compliant
 * @version     2.0
 */
#ifndef LAP_COM_COM_TYPES_HPP
#define LAP_COM_COM_TYPES_HPP

#include <core/CMacroDefine.hpp>
#include <core/CTypedef.hpp>
#include <core/CString.hpp>
#include <core/CResult.hpp>
#include <core/COptional.hpp>
#include <core/CInstanceSpecifier.hpp>
#include <core/CFunction.hpp>
#include <lap/log/CLog.hpp>

#include <cstdint>
#include <chrono>

namespace lap
{
namespace com
{
    #ifndef LAP_COM_API
        #if defined(LAP_COM_BUILDING)
            #define LAP_COM_API LAP_API_EXPORT
        #else
            #define LAP_COM_API LAP_API_IMPORT
        #endif
    #endif
    // Import commonly used types from lap::core
    using lap::core::Result;
    using lap::core::Optional;
    using lap::core::String;
    using lap::core::StringView;
    using lap::core::InstanceSpecifier;
    using lap::core::ErrorCode;
    using lap::core::Bool;
    using lap::core::Char;
    using lap::core::UInt8;
    using lap::core::UInt16;
    using lap::core::UInt32;
    using lap::core::UInt64;
    using lap::core::Int32;
    using lap::core::Int64;
    using lap::core::Float;
    using lap::core::Double;
    using lap::core::Size;

    template < typename T >
    using Atomic = lap::core::Atomic< T >;

    template < typename T >
    using UniqueHandle = lap::core::UniqueHandle< T >;

    template < typename T >
    using SharedHandle = lap::core::SharedHandle< T >;

    template < typename T >
    using WeakHandle = lap::core::WeakHandle< T >;

    template < typename Sig >
    using Function = lap::core::Function< Sig >;

    // Re-export factory functions (§7.2 — prefer project wrappers over std::make_*)
    using lap::core::MakeUnique;
    using lap::core::MakeShared;

    // Synchronization primitives
    using lap::core::Mutex;
    using lap::core::LockGuard;
    using lap::core::ScopedLock;
    using lap::core::UniqueLock;
    using lap::core::ConditionVariable;

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
    // Communication Management Error Codes [SWS_CM_10432]
    // ========================================================================
    
    /**
     * @brief Communication Management error code enumeration
     * @note [SWS_CM_10432] — Values MUST match AUTOSAR R25-11 SWS_CM spec exactly
     */
    enum class ComErrc : lap::core::ErrorDomain::CodeType
    {
        // ====================================================================
        // Standard AUTOSAR Error Codes [SWS_CM_10432]
        // ====================================================================
        kServiceNotAvailable            = 1,    ///< Service is not available after being previously offered
        kMaxSamplesExceeded             = 2,    ///< Application holds more SamplePtrs than committed in Subscribe()
        kCommunicationFailure           = 3,    ///< Recoverable communication error or secure communication failure (R25-11: renamed from kNetworkBindingFailure)
        kFieldValueNotInitialized       = 6,    ///< Field value has not been initialized via Update()
        kFieldSetHandlerNotSet          = 7,    ///< Field SetHandler has not been registered via RegisterSetHandler()
        kServiceNotOffered              = 11,   ///< Service has not yet been offered via OfferService()
        kInstanceIDNotResolvable        = 15,   ///< InstanceSpecifier is valid but could not be resolved to InstanceIdentifier
        kMaxSampleCountNotRealizable    = 16,   ///< Provided maxSampleCount for re-subscription does not match current subscription
        kUnknownApplicationError        = 22,   ///< Remote service returned an unconfigured application error
        kMinimumSendIntervalViolationError = 23, ///< Transmission request issued faster than configured minimumSendInterval

        // ====================================================================
        // Implementation-Specific Errors (0x100+, vendor range)
        // ====================================================================
        kNotInitialized                 = 0x100, ///< Component not initialized
        kTimeout                        = 0x101, ///< Operation timed out
        kInvalidArgument                = 0x102, ///< Invalid argument provided
        kInvalidState                   = 0x103, ///< Invalid state for operation
        kSerializationError             = 0x104, ///< Serialization failed
        kDeserializationError           = 0x105, ///< Deserialization failed
        kInternal                       = 0x106, ///< Internal error
        
        // ====================================================================
        // Registry-Specific Errors (0x200 - 0x2FF)
        // ====================================================================
        kSharedMemoryCreationFailed     = 0x200, ///< Failed to create shared memory
        kSharedMemoryMappingFailed      = 0x201, ///< Failed to mmap shared memory
        kSlotIndexInvalid               = 0x202, ///< Slot index out of range or reserved
        kSlotConflict                   = 0x203, ///< Slot already occupied by different service
        kSocketCreationFailed           = 0x204, ///< Failed to create Unix domain socket
        kSocketConnectFailed            = 0x205, ///< Failed to connect to socket
        kFdPassingFailed                = 0x206, ///< Failed to pass file descriptor via SCM_RIGHTS
        kPermissionDenied               = 0x207, ///< Insufficient permissions
        
        // ====================================================================
        // Binding Manager Errors (0x300 - 0x3FF)
        // ====================================================================
        kConfigLoadFailed               = 0x300, ///< Failed to load binding configuration
        kLibraryLoadFailed              = 0x301, ///< dlopen() failed for binding shared library
        kSymbolNotFound                 = 0x302, ///< Required factory symbol not exported by binding
        kBindingInitFailed              = 0x303, ///< Binding Initialize() returned error
        kNoBindingAvailable             = 0x304, ///< No suitable binding found for service
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
            auto code = static_cast< ComErrc > (errorCode);
            switch (code)
            {
                // Standard AUTOSAR errors [SWS_CM_10432]
                case ComErrc::kServiceNotAvailable:
                    return "Service is not available";
                case ComErrc::kMaxSamplesExceeded:
                    return "Maximum number of samples exceeded";
                case ComErrc::kCommunicationFailure:
                    return "Recoverable communication error or secure communication failure";
                case ComErrc::kFieldValueNotInitialized:
                    return "Field value has not been initialized via Update()";
                case ComErrc::kFieldSetHandlerNotSet:
                    return "Field SetHandler has not been registered";
                case ComErrc::kServiceNotOffered:
                    return "Service has not yet been offered via OfferService()";
                case ComErrc::kInstanceIDNotResolvable:
                    return "InstanceSpecifier could not be resolved to InstanceIdentifier";
                case ComErrc::kMaxSampleCountNotRealizable:
                    return "Provided maxSampleCount does not match current subscription";
                case ComErrc::kUnknownApplicationError:
                    return "Remote service returned an unconfigured application error";
                case ComErrc::kMinimumSendIntervalViolationError:
                    return "Transmission request issued faster than configured minimumSendInterval";
                // Implementation-specific errors
                case ComErrc::kNotInitialized:
                    return "Component not initialized";
                case ComErrc::kTimeout:
                    return "Operation timed out";
                case ComErrc::kInvalidArgument:
                    return "Invalid argument provided";
                case ComErrc::kInvalidState:
                    return "Invalid state for operation";
                case ComErrc::kSerializationError:
                    return "Serialization failed";
                case ComErrc::kDeserializationError:
                    return "Deserialization failed";
                case ComErrc::kInternal:
                    return "Internal error";
                // Registry errors
                case ComErrc::kSharedMemoryCreationFailed:
                    return "Failed to create shared memory";
                case ComErrc::kSharedMemoryMappingFailed:
                    return "Failed to mmap shared memory";
                case ComErrc::kSlotIndexInvalid:
                    return "Slot index out of range or reserved";
                case ComErrc::kSlotConflict:
                    return "Slot already occupied by different service";
                case ComErrc::kSocketCreationFailed:
                    return "Failed to create Unix domain socket";
                case ComErrc::kSocketConnectFailed:
                    return "Failed to connect to socket";
                case ComErrc::kFdPassingFailed:
                    return "Failed to pass file descriptor via SCM_RIGHTS";
                case ComErrc::kPermissionDenied:
                    return "Insufficient permissions";
                // Binding errors
                case ComErrc::kConfigLoadFailed:
                    return "Failed to load binding configuration";
                case ComErrc::kLibraryLoadFailed:
                    return "Failed to load binding shared library";
                case ComErrc::kSymbolNotFound:
                    return "Required binding factory symbol not found";
                case ComErrc::kBindingInitFailed:
                    return "Binding initialization failed";
                case ComErrc::kNoBindingAvailable:
                    return "No suitable binding available for service";
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
        return ErrorCode(static_cast< lap::core::ErrorDomain::CodeType > (code), 
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
        
        constexpr Bool operator==(const ServiceVersionType& other) const noexcept
        {
            return majorVersion == other.majorVersion && minorVersion == other.minorVersion;
        }
        
        constexpr Bool operator!=(const ServiceVersionType& other) const noexcept
        {
            return !(*this == other);
        }
        
        constexpr Bool operator< (const ServiceVersionType& other) const noexcept
        {
            if (majorVersion != other.majorVersion)
                return majorVersion < other.majorVersion;
            return minorVersion < other.minorVersion;
        }
    };
    
    // ========================================================================
    // Handle Types [SWS_CM_00303, SWS_CM_00304]
    // ========================================================================
    
    /**
     * @brief Service handle container type
     * @tparam T Type of service handle
     * @note [SWS_CM_00304]
     */
    template< typename T >
    using ServiceHandleContainer = lap::core::Vector< T >;
    
    /**
     * @brief FindServiceHandle — opaque handle for active find-service requests
     * @note [SWS_CM_00303] — struct satisfying EqualityComparable and LessThanComparable
     */
    struct FindServiceHandle
    {
        // Default constructor deleted per [SWS_CM_00353]
        FindServiceHandle() = delete;

        /**
         * @brief Construct from internal ID
         * @param id Implementation-specific handle identifier
         */
        explicit constexpr FindServiceHandle( lap::core::UInt64 id ) noexcept
            : m_id( id )
        {}

        FindServiceHandle( const FindServiceHandle& ) = default;
        FindServiceHandle& operator=( const FindServiceHandle& other ) = default;
        FindServiceHandle( FindServiceHandle&& ) noexcept = default;
        FindServiceHandle& operator=( FindServiceHandle&& ) noexcept = default;
        ~FindServiceHandle() = default;

        /**
         * @brief Equality operator [SWS_CM_11526]
         */
        constexpr Bool operator==( const FindServiceHandle& other ) const noexcept
        {
            return m_id == other.m_id;
        }

        /**
         * @brief Inequality operator
         */
        constexpr Bool operator!=( const FindServiceHandle& other ) const noexcept
        {
            return m_id != other.m_id;
        }

        /**
         * @brief Less-than operator [SWS_CM_11527]
         */
        constexpr Bool operator< ( const FindServiceHandle& other ) const noexcept
        {
            return m_id < other.m_id;
        }

        /**
         * @brief Get internal ID (implementation specific)
         */
        constexpr lap::core::UInt64 GetInternalId() const noexcept
        {
            return m_id;
        }

    private:
        lap::core::UInt64 m_id;
    };
    
    // ========================================================================
    // Event, Field and Trigger Types [SWS_CM_00308, SWS_CM_00309]
    // ========================================================================
    
    /**
     * @brief Sample pointer for event data (read-only)
     * @tparam SampleType Type of sample data
     * @note SWS_CM_00320
     */
    template< typename SampleType >
    using SamplePtr = std::unique_ptr< const SampleType >;
    
    /**
     * @brief Sample allocation pointer (writable)
     * @tparam SampleType Type of sample data
     * @note [SWS_CM_00308]
     */
    template< typename SampleType >
    using SampleAllocateePtr = std::unique_ptr< SampleType >;
    
    /**
     * @brief Event receive handler callback (non-template per R25-11)
     * @note [SWS_CM_00309] — Callback invoked when new event data arrives
     */
    using EventReceiveHandler = Function< void() >;

    /**
     * @brief Field receive handler callback
     * @note [SWS_CM_11615] — Callback invoked when new field notification arrives
     */
    using FieldReceiveHandler = Function< void() >;

    /**
     * @brief Trigger receive handler callback
     * @note [SWS_CM_00351] — Callback invoked when a new trigger arrives
     */
    using TriggerReceiveHandler = Function< void() >;
    
    /**
     * @brief Subscription state enumeration
     * @note [SWS_CM_00310] — Underlying type std::uint8_t per spec
     */
    enum class SubscriptionState : UInt8
    {
        kSubscribed         = 0,    ///< Subscription is active
        kNotSubscribed      = 1,    ///< Subscription is not active
        kSubscriptionPending = 2    ///< Subscription is pending
    };

    /**
     * @brief Subscription state change handler callback
     * @note [SWS_CM_00311]
     */
    using SubscriptionStateChangeHandler = Function< void( SubscriptionState ) >;
    
    // ========================================================================
    // Method Call Processing Modes [SWS_CM_00301]
    // ========================================================================
    
    /**
     * @brief Processing modes for the service implementation
     * @note [SWS_CM_00301] — Underlying type std::uint8_t per spec
     */
    enum class MethodCallProcessingMode : UInt8
    {
        kPoll               = 0,    ///< Polling mode
        kEvent              = 1,    ///< Event driven and concurrent mode
        kEventSingleThread  = 2     ///< Event driven, sequential mode
    };
    
    // ========================================================================
    // Service Discovery Types [SWS_CM_00383, SWS_CM_01071]
    // ========================================================================
    
    /**
     * @brief FindServiceHandler callback template
     * @tparam T HandleType — the service-specific handle type
     * @note [SWS_CM_00383] — Invoked when service availability changes
     */
    template< typename T >
    using FindServiceHandler = Function< void( ServiceHandleContainer< T >, 
                                                    FindServiceHandle ) >;

    /**
     * @brief InstanceIdentifierContainer type
     * @note [SWS_CM_00319]
     */
    using InstanceIdentifierContainer = lap::core::Vector< lap::core::InstanceSpecifier >;

    /**
     * @brief Service state enumeration
     * @note [SWS_CM_01071] — State of service from GetServiceState()
     */
    enum class ServiceState : UInt8
    {
        kNotAvailable   = 0,    ///< Service is not available
        kAvailable      = 1     ///< Service is available
    };

    /**
     * @brief Service state change handler callback
     * @note [SWS_CM_01072]
     */
    using ServiceStateHandler = Function< void( ServiceState ) >;
    
    // ========================================================================
    // E2E Protection Types [SWS_CM_00350]
    // ========================================================================
    
    /**
     * @brief End-to-End (E2E) protection result
     * @note SWS_CM_00350
     */
    enum class E2EResult : UInt8
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
        
        constexpr E2ECheckStatus( E2EResult res = E2EResult::kOk, 
                                  lap::core::UInt32 cnt = 0 ) noexcept
            : result( res ), counter( cnt )
        {}
    };
    
} // namespace com
} // namespace lap

#endif // LAP_COM_COM_TYPES_HPP
