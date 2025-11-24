/**
 * @file test_multi_size_messages.cpp
 * @brief Test iceoryx2 binding with various message sizes
 */

#include "Iceoryx2Binding.hpp"
#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <cstring>
#include <vector>

using namespace lap::com::binding;

// Test results
struct TestResult {
    size_t message_size;
    int sent;
    int received;
    bool passed;
    uint64_t avg_latency_ns;
};

std::vector<TestResult> test_results;

// Callback counters
std::atomic<int> received_count_1{0};
std::atomic<int> received_count_16{0};
std::atomic<int> received_count_64{0};
std::atomic<int> received_count_256{0};
std::atomic<int> received_count_512{0};
std::atomic<int> received_count_1024{0};

// Latency tracking
std::atomic<uint64_t> total_latency_ns{0};
std::atomic<int> latency_samples{0};

void callback_1(uint64_t /* service_id */, uint64_t /* instance_id */,
                uint32_t /* event_id */, const ByteBuffer& data)
{
    if (data.size() == 1) {
        received_count_1.fetch_add(1, std::memory_order_relaxed);
    }
}

void callback_16(uint64_t /* service_id */, uint64_t /* instance_id */,
                 uint32_t /* event_id */, const ByteBuffer& data)
{
    if (data.size() == 16) {
        received_count_16.fetch_add(1, std::memory_order_relaxed);
    }
}

void callback_64(uint64_t /* service_id */, uint64_t /* instance_id */,
                 uint32_t /* event_id */, const ByteBuffer& data)
{
    if (data.size() == 64) {
        received_count_64.fetch_add(1, std::memory_order_relaxed);
    }
}

void callback_256(uint64_t /* service_id */, uint64_t /* instance_id */,
                  uint32_t /* event_id */, const ByteBuffer& data)
{
    if (data.size() == 256) {
        received_count_256.fetch_add(1, std::memory_order_relaxed);
    }
}

void callback_512(uint64_t /* service_id */, uint64_t /* instance_id */,
                  uint32_t /* event_id */, const ByteBuffer& data)
{
    if (data.size() == 512) {
        received_count_512.fetch_add(1, std::memory_order_relaxed);
    }
}

void callback_1024(uint64_t /* service_id */, uint64_t /* instance_id */,
                   uint32_t /* event_id */, const ByteBuffer& data)
{
    if (data.size() == 1024) {
        received_count_1024.fetch_add(1, std::memory_order_relaxed);
    }
}

bool test_message_size(Iceoryx2Binding& binding, size_t message_size,
                       uint64_t service_id, EventCallback callback)
{
    std::cout << "\n======================================" << std::endl;
    std::cout << "Testing message size: " << message_size << " bytes" << std::endl;
    std::cout << "======================================" << std::endl;

    const uint64_t instance_id = 0x1000 + message_size;
    const uint32_t event_id = 0x2000;
    const int num_messages = 20;

    // Reset counter based on message size
    std::atomic<int>* counter = nullptr;
    if (message_size == 1) counter = &received_count_1;
    else if (message_size == 16) counter = &received_count_16;
    else if (message_size == 64) counter = &received_count_64;
    else if (message_size == 256) counter = &received_count_256;
    else if (message_size == 512) counter = &received_count_512;
    else if (message_size == 1024) counter = &received_count_1024;
    
    if (counter) {
        counter->store(0, std::memory_order_release);
    }

    // 1. Offer service
    std::cout << "1. Offering service..." << std::endl;
    if (!binding.OfferService(service_id, instance_id).HasValue()) {
        std::cout << "   ✗ Failed to offer service" << std::endl;
        return false;
    }
    std::cout << "   ✓ Service offered" << std::endl;

    // 2. Subscribe
    std::cout << "2. Subscribing..." << std::endl;
    if (!binding.SubscribeEvent(service_id, instance_id, event_id, callback).HasValue()) {
        std::cout << "   ✗ Failed to subscribe" << std::endl;
        binding.StopOfferService(service_id, instance_id);
        return false;
    }
    std::cout << "   ✓ Subscribed" << std::endl;

    // Wait for subscription to be ready
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 3. Send messages
    std::cout << "3. Sending " << num_messages << " messages of " << message_size << " bytes..." << std::endl;
    
    ByteBuffer data(message_size);
    for (int i = 0; i < num_messages; i++) {
        // Fill with pattern
        for (size_t j = 0; j < message_size; j++) {
            data[j] = static_cast<uint8_t>((i + j) & 0xFF);
        }

        auto start = std::chrono::steady_clock::now();
        auto send_result = binding.SendEvent(service_id, instance_id, event_id, data);
        auto end = std::chrono::steady_clock::now();

        if (!send_result.HasValue()) {
            std::cout << "   ✗ Failed to send message " << i << std::endl;
            binding.UnsubscribeEvent(service_id, instance_id, event_id);
            binding.StopOfferService(service_id, instance_id);
            return false;
        }

        uint64_t latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        total_latency_ns.fetch_add(latency, std::memory_order_relaxed);
        latency_samples.fetch_add(1, std::memory_order_relaxed);
    }

    // Wait for messages to be received
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 4. Check results
    int received = counter ? counter->load(std::memory_order_acquire) : 0;
    std::cout << "4. Results: Sent=" << num_messages << ", Received=" << received << std::endl;

    auto metrics = binding.GetMetrics();
    std::cout << "   Metrics: sent=" << metrics.messages_sent 
              << ", bytes=" << metrics.bytes_sent
              << ", latency=" << metrics.avg_latency_ns << "ns" << std::endl;

    // Cleanup
    binding.UnsubscribeEvent(service_id, instance_id, event_id);
    binding.StopOfferService(service_id, instance_id);

    bool passed = (received == num_messages);
    std::cout << "Result: " << (passed ? "✓ PASSED" : "✗ FAILED") << std::endl;

    // Record result
    uint64_t avg_latency = latency_samples.load() > 0 ? 
        total_latency_ns.load() / latency_samples.load() : 0;
    
    test_results.push_back({message_size, num_messages, received, passed, avg_latency});

    return passed;
}

int main()
{
    std::cout << "==========================================" << std::endl;
    std::cout << "  iceoryx2 Multi-Size Message Test" << std::endl;
    std::cout << "==========================================" << std::endl;

    Iceoryx2Binding binding;

    // Initialize
    std::cout << "\nInitializing iceoryx2 binding..." << std::endl;
    if (!binding.Initialize().HasValue()) {
        std::cout << "✗ Failed to initialize binding" << std::endl;
        return 1;
    }
    std::cout << "✓ Binding initialized" << std::endl;

    // Test different message sizes
    std::vector<std::pair<size_t, EventCallback>> test_cases = {
        {1, callback_1},
        {16, callback_16},
        {64, callback_64},
        {256, callback_256},
        {512, callback_512},
        {1024, callback_1024}
    };

    int passed = 0;
    int total = test_cases.size();

    for (const auto& [size, callback] : test_cases) {
        uint64_t service_id = 0x5000 + size;
        if (test_message_size(binding, size, service_id, callback)) {
            passed++;
        }
        
        // Small delay between tests
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Summary
    std::cout << "\n==========================================" << std::endl;
    std::cout << "  Test Summary" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "\n┌────────┬──────┬──────────┬────────┬──────────────┐" << std::endl;
    std::cout << "│ Size   │ Sent │ Received │ Status │ Avg Latency  │" << std::endl;
    std::cout << "├────────┼──────┼──────────┼────────┼──────────────┤" << std::endl;
    
    for (const auto& result : test_results) {
        printf("│ %4zuB  │  %2d  │    %2d    │   %s   │  %6lu ns   │\n",
               result.message_size, result.sent, result.received,
               result.passed ? "✓" : "✗", result.avg_latency_ns);
    }
    
    std::cout << "└────────┴──────┴──────────┴────────┴──────────────┘" << std::endl;
    std::cout << "\nPassed: " << passed << "/" << total << std::endl;
    std::cout << "Result: " << (passed == total ? "✓ ALL TESTS PASSED" : "✗ SOME TESTS FAILED") << std::endl;
    std::cout << "==========================================" << std::endl;

    // Shutdown
    binding.Shutdown();

    return (passed == total) ? 0 : 1;
}
