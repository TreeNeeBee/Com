/**
 * @file        ProxyEvent.hpp
 * @author      Aii
 * @brief       AUTOSAR Adaptive Platform Proxy-Side Event Communication
 * @date        2026/02/07
 * @details     Proxy-side event subscription, sample queue, and receive handler.
 *              [SWS_CM Section 9.3] — Consumer-side event subscription and reception.
 *              Thread-safe: internal mutex protects sample queue and handler.
 * @copyright   Copyright (c) 2026
 * @note        AUTOSAR R25-11 SWS_CM compliant
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026/02/07  <td>1.0      <td>Aii     <td>Split from Event.hpp (SRP refactoring)
 * <tr><td>2026/02/27  <td>1.1      <td>Aii     <td>Restored after accidental overwrite
 * </table>
 */
#ifndef LAP_COM_PROXY_EVENT_HPP
#define LAP_COM_PROXY_EVENT_HPP

// ==================== Project-Internal Headers ====================
#include "ComTypes.hpp"
#include "CBindingContext.hpp"

// ==================== Binding Headers ====================
#include "ITransportBinding.hpp"

// ==================== Cross-Module Headers ====================
#include <core/CResult.hpp>
#include <core/CSpan.hpp>
#include <core/CSync.hpp>

// ==================== Serialization Headers ====================
#include "serialization/CBinaryDeserializer.hpp"
#include "serialization/CSerializationTraits.hpp"

// ==================== Standard Library Headers ====================
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>

class ProxySkeletonTestAccessor;

namespace lap
{
namespace com
{
    // ========================================================================
    // Type Aliases (prefer lap::core project types)
    // ========================================================================
    using lap::core::Bool;

    // Forward declarations
    class ProxyBase;
    template< typename FieldType > class ProxyField;

    /**
     * @brief Proxy-side typed event for receiving event notifications
     * @tparam SampleType   Type of the event data
     * @note [SWS_CM_00701] — Proxy-side event subscription and reception
     *
     * @details Thread-safe event subscription and sample management:
     *          - Subscribe() registers a typed EventCallback with the binding
     *          - Incoming samples are pushed into a bounded circular queue
     *          - Application reads samples via GetNextSample()
     *          - Optional SetReceiveHandler() for immediate notification
     */
    template< typename SampleType >
    class ProxyEvent
    {
    public:
        /**
         * @brief Default constructor
         * @note [SWS_CM_00702]
         */
        ProxyEvent() noexcept = default;

        /**
         * @brief Destructor — auto-unsubscribes if subscribed and not moved-from
         * @note [SWS_CM_00703]
         */
        ~ProxyEvent() noexcept
        {
            if ( m_state == SubscriptionState::kSubscribed && m_pMutex != nullptr )
            {
                Unsubscribe();
            }
        }

        // Move-only type (AUTOSAR C++ A12-8-6)
        // Explicit move constructor resets source state to avoid double-unsubscribe
        ProxyEvent( ProxyEvent&& other ) noexcept
            : m_pMutex{ ::std::move( other.m_pMutex ) }
            , m_pCv{ ::std::move( other.m_pCv ) }
            , m_maxSampleCount{ other.m_maxSampleCount }
            , m_sampleQueue{ ::std::move( other.m_sampleQueue ) }
            , m_state{ other.m_state }
            , m_receiveHandler{ ::std::move( other.m_receiveHandler ) }
            , m_bindingContext{ other.m_bindingContext }
        {
            other.m_state = SubscriptionState::kNotSubscribed;
        }

        ProxyEvent& operator=( ProxyEvent&& other ) noexcept
        {
            if ( this != &other )
            {
                if ( m_state == SubscriptionState::kSubscribed && m_pMutex != nullptr )
                {
                    Unsubscribe();
                }
                m_pMutex         = ::std::move( other.m_pMutex );
                m_pCv            = ::std::move( other.m_pCv );
                m_maxSampleCount = other.m_maxSampleCount;
                m_sampleQueue    = ::std::move( other.m_sampleQueue );
                m_state          = other.m_state;
                m_receiveHandler = ::std::move( other.m_receiveHandler );
                m_bindingContext = other.m_bindingContext;
                other.m_state    = SubscriptionState::kNotSubscribed;
            }
            return *this;
        }

        ProxyEvent( const ProxyEvent& )            = delete;
        ProxyEvent& operator=( const ProxyEvent& ) = delete;

        // ================================================================
        // Subscription Control  [SWS_CM_00141, SWS_CM_00151]
        // ================================================================

        /**
         * @brief Subscribe to this event with bounded sample queue
         * @param maxSampleCount Maximum number of samples to cache
         * @return Result<void> Success, or error if not connected
         * @note [SWS_CM_00141] — Registers typed callback with transport binding
         */
        Result< void > Subscribe( lap::core::UInt32 maxSampleCount = 1u ) noexcept
        {
            if ( !m_bindingContext.IsValid() )
            {
                return Result< void >::FromError(
                    MakeErrorCode( ComErrc::kServiceNotAvailable, 0 ) );
            }

            {
                ::std::lock_guard< Mutex > lk( *m_pMutex );
                m_maxSampleCount = maxSampleCount;
                m_sampleQueue.clear();
                m_state = SubscriptionState::kSubscriptionPending;
            }

            // ── Subscription strategy based on type characteristics ──
            //
            // Path A — Trivially-copyable (Double, UInt32, etc.):
            //   Binding delivers raw sizeof(T) bytes; NVI erased callback casts
            //   buf.data() → const T*  (memcpy-equivalent).  Always correct.
            //
            // Path B — Non-trivially-copyable WITH DDS type adapter (String, struct):
            //   Binding's adapter ExtractData() returns const T* directly.
            //   NVI erased callback casts void* → const T*  — correct.
            //
            // Path C — Non-trivially-copyable WITHOUT adapter (String field notify):
            //   Server used CBinarySerializer → ByteBuffer → DDS DdsPayload.
            //   WireSize<T>() = sizeof(T) but serialized size ≠ sizeof(T) for
            //   heap-allocated types ⇒ NVI guard (buf.size()>=sizeof(T)) discards
            //   short strings.  FIX: subscribe as ByteBuffer + deserialize here.

            // Use IIFE to return Result via function return (not assignment),
            // since lap::core::optional<ErrorCode> has deleted move-assignment.
            auto subscribeImpl = [ this ]() noexcept -> Result< void >
            {
                if constexpr ( ::std::is_trivially_copyable_v< SampleType > )
                {
                    // Path A: trivially-copyable — raw bytes delivered by binding
                    auto cb = [ this ](
                        lap::core::UInt64 /*serviceId*/,
                        lap::core::UInt64 /*instanceId*/,
                        lap::core::UInt32 /*eventId*/,
                        const SampleType& data ) noexcept
                    {
                        onEventReceived( data );
                    };
                    return m_bindingContext.pBinding->template SubscribeEvent< SampleType >(
                        m_bindingContext.serviceId,
                        m_bindingContext.instanceId,
                        m_bindingContext.elementId,
                        ::std::move( cb ) );
                }
                else if ( m_bindingContext.pBinding->HasEventAdapter(
                              m_bindingContext.serviceId,
                              m_bindingContext.elementId ) )
                {
                    // Path B: non-trivially-copyable WITH typed DDS adapter.
                    // Binding's ExtractData returns const T*; cast directly.
                    auto cb = [ this ](
                        lap::core::UInt64 /*serviceId*/,
                        lap::core::UInt64 /*instanceId*/,
                        lap::core::UInt32 /*eventId*/,
                        const SampleType& data ) noexcept
                    {
                        onEventReceived( data );
                    };
                    return m_bindingContext.pBinding->template SubscribeEvent< SampleType >(
                        m_bindingContext.serviceId,
                        m_bindingContext.instanceId,
                        m_bindingContext.elementId,
                        ::std::move( cb ) );
                }
                else
                {
                    // Path C: non-trivially-copyable WITHOUT adapter.
                    // Server serialized via CBinarySerializer -> ByteBuffer -> DDS.
                    // Subscribe via ByteBuffer (WireSize=0, no size-guard in binding)
                    // and deserialize back to SampleType using CBinaryDeserializer.
                    auto cb = [ this ](
                        lap::core::UInt64 /*serviceId*/,
                        lap::core::UInt64 /*instanceId*/,
                        lap::core::UInt32 /*eventId*/,
                        const ::lap::com::binding::ByteBuffer& buf ) noexcept
                    {
                        SampleType value{};
                        auto span = lap::core::MakeSpan(
                            reinterpret_cast< const lap::core::UInt8* >( buf.data() ),
                            buf.size() );
                        serialization::CBinaryDeserializer deserializer( span );
                        auto r = serialization::DeserializeValue< SampleType >(
                            deserializer, value );
                        if ( r.HasValue() )
                        {
                            onEventReceived( value );
                        }
                    };
                    return m_bindingContext.pBinding->template SubscribeEvent<
                        ::lap::com::binding::ByteBuffer >(
                            m_bindingContext.serviceId,
                            m_bindingContext.instanceId,
                            m_bindingContext.elementId,
                            ::std::move( cb ) );
                }
            };

            auto result = subscribeImpl();

            {
                ::std::lock_guard< Mutex > lk( *m_pMutex );
                m_state = result.HasValue()
                    ? SubscriptionState::kSubscribed
                    : SubscriptionState::kNotSubscribed;
            }

            return result;
        }

        /**
         * @brief Unsubscribe from this event
         * @note [SWS_CM_00151]
         */
        void Unsubscribe() noexcept
        {
            if ( m_bindingContext.IsValid() )
            {
                static_cast< void >(
                    m_bindingContext.pBinding->UnsubscribeEvent(
                        m_bindingContext.serviceId,
                        m_bindingContext.instanceId,
                        m_bindingContext.elementId ) );
            }

            ::std::lock_guard< Mutex > lk( *m_pMutex );
            m_state = SubscriptionState::kNotSubscribed;
            m_sampleQueue.clear();
        }

        /**
         * @brief Query current subscription state
         * @return SubscriptionState enum value
         * @note [SWS_CM_00316]
         */
        SubscriptionState GetSubscriptionState() const noexcept
        {
            ::std::lock_guard< Mutex > lk( *m_pMutex );
            return m_state;
        }

        // ================================================================
        // Sample Access  [SWS_CM_00701]
        // ================================================================

        /**
         * @brief Get the number of available unread samples
         * @return Count of queued samples
         * @note [SWS_CM_00701]
         */
        lap::core::UInt32 GetNewSamples() const noexcept
        {
            ::std::lock_guard< Mutex > lk( *m_pMutex );
            return static_cast< lap::core::UInt32 >( m_sampleQueue.size() );
        }

        /**
         * @brief Get next available event sample
         * @param timeout  Maximum time to wait (0 = non-blocking)
         * @return Result<SamplePtr<SampleType>> Sample pointer, null if no sample available
         * @note [SWS_CM_00701]
         *
         * @details Returns a null SamplePtr (not an error) when no sample is
         *          available within the timeout.  Returns error only on
         *          'not subscribed' or internal failure conditions.
         */
        Result< SamplePtr< SampleType > > GetNextSample(
            ::std::chrono::milliseconds timeout =
                ::std::chrono::milliseconds( 0 ) ) noexcept
        {
            if ( m_state == SubscriptionState::kNotSubscribed )
            {
                return Result< SamplePtr< SampleType > >::FromError(
                    MakeErrorCode( ComErrc::kNotInitialized, 0 ) );
            }

            ::std::unique_lock< Mutex > lk( *m_pMutex );

            if ( m_sampleQueue.empty() && timeout > ::std::chrono::milliseconds( 0 ) )
            {
                m_pCv->wait_for( lk, timeout,
                    [ this ]() noexcept { return !m_sampleQueue.empty(); } );
            }

            if ( m_sampleQueue.empty() )
            {
                // No sample available — return null SamplePtr (not an error)
                return Result< SamplePtr< SampleType > >::FromValue(
                    SamplePtr< SampleType >() );
            }

            // Pop oldest sample
            SampleType snapshot = ::std::move( m_sampleQueue.front() );
            m_sampleQueue.pop_front();

            return Result< SamplePtr< SampleType > >::FromValue(
                ::std::make_unique< const SampleType >( ::std::move( snapshot ) ) );
        }

        // ================================================================
        // Receive Handler  [SWS_CM_00309]
        // ================================================================

        /**
         * @brief Set an asynchronous receive handler (Observer pattern)
         * @param handler  Void callback invoked on each new sample
         * @return Result<void> Success or error
         * @note [SWS_CM_00309] — Handler fires in the binding's callback thread
         */
        Result< void > SetReceiveHandler( EventReceiveHandler handler ) noexcept
        {
            ::std::lock_guard< Mutex > lk( *m_pMutex );
            m_receiveHandler = ::std::move( handler );
            return Result< void >::FromValue();
        }

        /**
         * @brief Unset the asynchronous receive handler
         * @note [SWS_CM_00309]
         */
        void UnsetReceiveHandler() noexcept
        {
            ::std::lock_guard< Mutex > lk( *m_pMutex );
            m_receiveHandler = nullptr;
        }

    private:
        // ================================================================
        // Internal
        // ================================================================

        /**
         * @brief Receive callback — invoked by the binding's event dispatch thread
         * @param data  Newly arrived event data (shallow-copied into queue)
         */
        void onEventReceived( const SampleType& data ) noexcept
        {
            EventReceiveHandler handlerCopy;

            {
                ::std::lock_guard< Mutex > lk( *m_pMutex );

                // Bounded queue: discard oldest sample when at capacity
                if ( m_sampleQueue.size() >= static_cast< ::std::size_t >( m_maxSampleCount ) )
                {
                    m_sampleQueue.pop_front();
                }
                m_sampleQueue.push_back( data );
                m_pCv->notify_one();

                handlerCopy = m_receiveHandler;
            }

            // Fire handler outside the lock to prevent re-entrant deadlock
            if ( handlerCopy )
            {
                handlerCopy();
            }
        }

        /**
         * @brief Internal: inject binding context (called by ProxyBase / ProxyField)
         * @param context  Binding context providing transport handle and IDs
         */
        void setBindingContext( const CBindingContext& context ) noexcept
        {
            m_bindingContext = context;
        }

        // ================================================================
        // Member Variables
        // ================================================================
        mutable UniqueHandle< Mutex >                    m_pMutex{ MakeUnique< Mutex >() };
        ::std::unique_ptr< ::std::condition_variable >   m_pCv{
            ::std::make_unique< ::std::condition_variable >() };

        lap::core::UInt32               m_maxSampleCount{ 1u };
        ::std::deque< SampleType >      m_sampleQueue;
        SubscriptionState               m_state{ SubscriptionState::kNotSubscribed };
        EventReceiveHandler             m_receiveHandler;
        CBindingContext                 m_bindingContext;

        friend class ProxyBase;
        friend class ProxyField< SampleType >;
        friend class ::ProxySkeletonTestAccessor;
    };

} // namespace com
} // namespace lap

#endif // LAP_COM_PROXY_EVENT_HPP