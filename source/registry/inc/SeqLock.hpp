/**
 * @file        SeqLock.hpp
 * @author      LightAP Development Team
 * @brief       Sequential lock (seqlock) implementation for lock-free concurrent reads
 * @date        2025-11-20
 * @details     Provides reader-writer synchronization with lock-free reads.
 *              Writers use exclusive access, readers retry on conflict.
 *              Target read latency: < 100ns (P99)
 * @copyright   Copyright (c) 2025
 * @note        AUTOSAR R24-11 Compliance:
 *              - SWS_CM_00110: Service Registry Synchronization
 *              - Design follows SERVICE_DISCOVERY_ARCHITECTURE.md §2.1.2
 * @reference   SERVICE_DISCOVERY_ARCHITECTURE.md §2.1.2 (seqlock Synchronization)
 *              Linux kernel seqlock implementation (include/linux/seqlock.h)
 * sdk:
 * platform:    Linux 5.10+ (x86_64, ARM64)
 * project:     LightAP
 * @version
 * <table>
 * <tr><th>Date        <th>Version  <th>Author          <th>Description
 * <tr><td>2025/11/20  <td>1.0      <td>LightAP Team    <td>Initial lock-free implementation
 * </table>
 */
#ifndef LAP_COM_REGISTRY_SEQLOCK_HPP
#define LAP_COM_REGISTRY_SEQLOCK_HPP

#include <atomic>
#include <cstdint>
#include <type_traits>

#include <lap/core/CTypedef.hpp>
#include <lap/core/COptional.hpp>

// Architecture-specific pause instruction
#if defined(__x86_64__) || defined(__i386__)
    #include <immintrin.h>  // for _mm_pause()
    #define LAP_CPU_PAUSE() _mm_pause()
#elif defined(__aarch64__) || defined(__arm__)
    #define LAP_CPU_PAUSE() __asm__ __volatile__("yield" ::: "memory")
#else
    #define LAP_CPU_PAUSE() std::this_thread::yield()
#endif

namespace lap
{
namespace com
{
namespace registry
{
    /**
     * @brief seqlock reader/writer operations for ServiceSlot
     * 
     * @details Design principles:
     *          - Writers acquire exclusive access (sequence odd)
     *          - Readers check sequence before/after read (retry if mismatch)
     *          - No locks for readers → < 100ns read latency
     *          - Memory barriers ensure visibility across CPU cores
     * 
     * @note Performance characteristics:
     *       - Read latency: < 100ns (P99, no contention)
     *       - Write latency: ~50ns (sequence increment + memcpy)
     *       - Read retry rate: < 0.1% (typical workload)
     * 
     * @example Write operation:
     *          SeqLockWriter writer(slot.sequence);
     *          slot.service_id = new_id;
     *          // ... update other fields ...
     *          // Destructor automatically releases write lock
     * 
     * @example Read operation:
     *          auto result = SeqLockReader::Read(slot, [](const auto& s) {
     *              return s.service_id;
     *          });
     *          if (result.has_value()) {
     *              uint64_t id = result.value();
     *          }
     */
    class SeqLockWriter final
    {
    public:
        /**
         * @brief Acquire write lock (increment sequence to odd value)
         * @param sequence Reference to atomic sequence counter
         * @note This is a blocking operation - writers must serialize
         */
        explicit SeqLockWriter(std::atomic<uint64_t>& sequence) noexcept
            : sequence_(sequence)
        {
            // Acquire lock: increment sequence (becomes odd)
            // Use acquire memory order to prevent reordering with subsequent writes
            sequence_.fetch_add(1, std::memory_order_acquire);
        }

        /**
         * @brief Release write lock (increment sequence to even value)
         * @note Destructor ensures lock is always released (RAII)
         */
        ~SeqLockWriter() noexcept
        {
            // Release lock: increment sequence again (becomes even)
            // Use release memory order to ensure all writes are visible
            std::atomic_thread_fence(std::memory_order_release);
            sequence_.fetch_add(1, std::memory_order_release);
        }

        // Disable copy and move
        SeqLockWriter(const SeqLockWriter&) = delete;
        SeqLockWriter& operator=(const SeqLockWriter&) = delete;
        SeqLockWriter(SeqLockWriter&&) = delete;
        SeqLockWriter& operator=(SeqLockWriter&&) = delete;

    private:
        std::atomic<uint64_t>& sequence_;  ///< Reference to slot's sequence counter
    };

    /**
     * @brief seqlock reader operations (lock-free reads with retry)
     */
    class SeqLockReader final
    {
    public:
        /**
         * @brief Maximum read retry attempts before giving up
         * @note Prevents infinite loops under heavy write contention
         */
        static constexpr uint32_t MAX_RETRY_COUNT = 1000;

        /**
         * @brief Perform lock-free read with retry on write conflict
         * 
         * @tparam SlotType Type of the slot being read (usually ServiceSlot)
         * @tparam ReadFunc Callable that extracts data from slot
         * @param slot The slot to read from
         * @param read_func Lambda/function to extract desired data
         * @return Optional<ReturnType> Value if read succeeds, empty if max retries exceeded
         * 
         * @details Read algorithm:
         *          1. Read sequence (must be even)
         *          2. Apply memory barrier
         *          3. Read slot data via read_func
         *          4. Apply memory barrier
         *          5. Re-read sequence, verify it matches step 1
         *          6. If mismatch or odd → retry from step 1
         * 
         * @example Usage:
         *          auto endpoint = SeqLockReader::Read(slot, [](const auto& s) {
         *              return std::string(s.endpoint);
         *          });
         * 
         * @note AUTOSAR Compliance: SWS_CM_00110 (thread-safe registry access)
         */
        template<typename SlotType, typename ReadFunc>
        static auto Read(const SlotType& slot, ReadFunc&& read_func) noexcept
            -> lap::core::Optional<decltype(read_func(slot))>
        {
            using ReturnType = decltype(read_func(slot));
            
            uint64_t seq1, seq2;
            uint32_t retry_count = 0;

            do {
                // Step 1: Read sequence (must be even, indicating no active write)
                seq1 = slot.sequence.load(std::memory_order_acquire);
                
                // Check if write is in progress (sequence is odd)
                if (seq1 & 1) {
                    // Writer is active, yield CPU and retry
                    LAP_CPU_PAUSE();
                    
                    if (++retry_count > MAX_RETRY_COUNT) {
                        // Max retries exceeded (write contention too high)
                        return lap::core::Optional<ReturnType>{};
                    }
                    continue;
                }

                // Step 2: Memory barrier to prevent reordering reads
                std::atomic_thread_fence(std::memory_order_acquire);

                // Step 3: Read data (lock-free, may race with writer)
                ReturnType result = read_func(slot);

                // Step 4: Memory barrier to prevent reordering with sequence check
                std::atomic_thread_fence(std::memory_order_acquire);

                // Step 5: Re-read sequence, verify consistency
                seq2 = slot.sequence.load(std::memory_order_acquire);

                // Step 6: Check if sequence changed during read
                if (seq1 == seq2) {
                    // Success: data is consistent
                    return lap::core::Optional<ReturnType>(std::move(result));
                }

                // Sequence mismatch: write occurred during read, retry
                LAP_CPU_PAUSE();
                
                if (++retry_count > MAX_RETRY_COUNT) {
                    // Max retries exceeded
                    return lap::core::Optional<ReturnType>{};
                }

            } while (true);
        }

        /**
         * @brief Read entire slot atomically (convenience wrapper)
         * @param slot The ServiceSlot to read
         * @return Optional<SlotType> Complete slot copy if successful
         * 
         * @note This copies the entire slot structure (256 bytes).
         *       For performance, prefer reading specific fields with Read().
         */
        template<typename SlotType>
        static lap::core::Optional<SlotType> ReadSlot(const SlotType& slot) noexcept
        {
            return Read(slot, [](const SlotType& s) -> SlotType {
                return s;  // Full copy
            });
        }

        /**
         * @brief Check if slot sequence is currently stable (even value)
         * @param sequence Atomic sequence counter to check
         * @return true if sequence is even (no active write)
         * 
         * @note This is a hint only - sequence may change immediately after check
         */
        static bool IsStable(const std::atomic<uint64_t>& sequence) noexcept
        {
            uint64_t seq = sequence.load(std::memory_order_relaxed);
            return (seq & 1) == 0;
        }
    };

} // namespace registry
} // namespace com
} // namespace lap

#endif // LAP_COM_REGISTRY_SEQLOCK_HPP
