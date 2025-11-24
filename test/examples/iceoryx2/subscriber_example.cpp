/**
 * @file        subscriber_example.cpp
 * @brief       iceoryx2 Subscriber Example - Radar Object Subscriber
 * @details     Demonstrates how to receive structured data using iceoryx2 binding
 * @date        2025-11-23
 */

#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <atomic>
#include <signal.h>

#include "Iceoryx2Binding.hpp"

using namespace lap::com::binding;

// Radar object structure (must match publisher)
struct RadarObject {
    uint32_t object_id;
    float distance;      // meters
    float velocity;      // m/s
    float angle;         // degrees
    uint8_t confidence;  // 0-100%
    uint64_t timestamp;  // microseconds
} __attribute__((packed));

// Statistics
std::atomic<uint32_t> messages_received{0};
std::atomic<uint32_t> last_object_id{0};
std::atomic<bool> running{true};

// Signal handler
void signalHandler(int signum)
{
    std::cout << "\nInterrupt signal (" << signum << ") received." << std::endl;
    running.store(false);
}

// Deserialize ByteBuffer to RadarObject
RadarObject deserializeRadarObject(const ByteBuffer& data)
{
    RadarObject obj;
    if (data.size() >= sizeof(RadarObject)) {
        std::memcpy(&obj, data.data(), sizeof(RadarObject));
    } else {
        std::memset(&obj, 0, sizeof(RadarObject));
    }
    return obj;
}

// Event callback
void radarObjectCallback(uint64_t /* service_id */, uint64_t /* instance_id */,
                        uint32_t /* event_id */, const ByteBuffer& data)
{
    messages_received.fetch_add(1);
    
    // Deserialize
    RadarObject obj = deserializeRadarObject(data);
    last_object_id.store(obj.object_id);

    // Calculate reception time
    auto now = std::chrono::steady_clock::now();
    auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    
    int64_t latency_us = now_us - static_cast<int64_t>(obj.timestamp);

    // Print received data
    std::cout << "Received object #" << std::setw(4) << obj.object_id
              << " | dist=" << std::fixed << std::setprecision(1) << std::setw(5) << obj.distance << "m"
              << " | vel=" << std::setw(5) << obj.velocity << "m/s"
              << " | angle=" << std::setw(5) << obj.angle << "°"
              << " | conf=" << std::setw(3) << static_cast<int>(obj.confidence) << "%"
              << " | latency=" << std::setw(4) << latency_us << "μs"
              << std::endl;
}

int main(int /* argc */, char* /* argv */[])
{
    std::cout << "========================================" << std::endl;
    std::cout << "  iceoryx2 Subscriber Example" << std::endl;
    std::cout << "  Radar Object Subscriber" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Setup signal handler
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Configuration
    const uint64_t SERVICE_ID = 0x1234;
    const uint64_t INSTANCE_ID = 0x0001;
    const uint32_t EVENT_ID = 0x0100;

    // Create and initialize binding
    Iceoryx2Binding binding;
    
    std::cout << "1. Initializing iceoryx2 binding..." << std::endl;
    auto result = binding.Initialize();
    std::cout << "   ✓ Initialized" << std::endl;

    // Subscribe to service
    std::cout << "\n2. Subscribing to radar service..." << std::endl;
    std::cout << "   Service ID:  0x" << std::hex << SERVICE_ID << std::endl;
    std::cout << "   Instance ID: 0x" << INSTANCE_ID << std::dec << std::endl;
    
    binding.SubscribeEvent(SERVICE_ID, INSTANCE_ID, EVENT_ID, radarObjectCallback);
    std::cout << "   ✓ Subscribed" << std::endl;

    std::cout << "\n3. Waiting for radar objects..." << std::endl;
    std::cout << "   Press Ctrl+C to stop\n" << std::endl;

    // Statistics thread
    auto stats_start = std::chrono::steady_clock::now();
    uint32_t last_count = 0;

    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - stats_start).count();
        
        if (elapsed > 0 && running.load()) {
            uint32_t current_count = messages_received.load();
            uint32_t new_messages = current_count - last_count;
            last_count = current_count;
            
            auto metrics = binding.GetMetrics();
            
            std::cout << "\n--- Statistics (5s window) ---" << std::endl;
            std::cout << "  Messages received: " << new_messages << " (" << new_messages/5.0 << " msg/s)" << std::endl;
            std::cout << "  Total received: " << current_count << std::endl;
            std::cout << "  Last object ID: " << last_object_id.load() << std::endl;
            std::cout << "  Total bytes: " << metrics.bytes_received << std::endl;
            std::cout << "------------------------------\n" << std::endl;
        }
    }

    // Cleanup
    std::cout << "\n4. Cleaning up..." << std::endl;
    binding.UnsubscribeEvent(SERVICE_ID, INSTANCE_ID, EVENT_ID);
    binding.Shutdown();
    std::cout << "   ✓ Cleanup complete" << std::endl;

    std::cout << "\nFinal Statistics:" << std::endl;
    std::cout << "  Total messages received: " << messages_received.load() << std::endl;

    return 0;
}
