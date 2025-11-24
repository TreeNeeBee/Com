/**
 * @file        publisher_example.cpp
 * @brief       iceoryx2 Publisher Example - Radar Object Publisher
 * @details     Demonstrates how to publish structured data using iceoryx2 binding
 * @date        2025-11-23
 */

#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <iomanip>

#include "Iceoryx2Binding.hpp"

using namespace lap::com::binding;

// Radar object structure (simplified)
struct RadarObject {
    uint32_t object_id;
    float distance;      // meters
    float velocity;      // m/s
    float angle;         // degrees
    uint8_t confidence;  // 0-100%
    uint64_t timestamp;  // microseconds
} __attribute__((packed));

// Serialize RadarObject to ByteBuffer
ByteBuffer serializeRadarObject(const RadarObject& obj)
{
    ByteBuffer buffer(sizeof(RadarObject));
    std::memcpy(buffer.data(), &obj, sizeof(RadarObject));
    return buffer;
}

int main(int /* argc */, char* /* argv */[])
{
    std::cout << "========================================" << std::endl;
    std::cout << "  iceoryx2 Publisher Example" << std::endl;
    std::cout << "  Radar Object Publisher" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Configuration
    const uint64_t SERVICE_ID = 0x1234;
    const uint64_t INSTANCE_ID = 0x0001;
    const uint32_t EVENT_ID = 0x0100;  // RADAR_OBJECT_EVENT
    const int PUBLISH_RATE_MS = 100;   // 10 Hz

    // Create and initialize binding
    Iceoryx2Binding binding;
    
    std::cout << "1. Initializing iceoryx2 binding..." << std::endl;
    auto result = binding.Initialize();
    std::cout << "   ✓ Initialized" << std::endl;

    // Offer service
    std::cout << "\n2. Offering radar service..." << std::endl;
    std::cout << "   Service ID:  0x" << std::hex << SERVICE_ID << std::endl;
    std::cout << "   Instance ID: 0x" << INSTANCE_ID << std::dec << std::endl;
    
    binding.OfferService(SERVICE_ID, INSTANCE_ID);
    std::cout << "   ✓ Service offered" << std::endl;

    std::cout << "\n3. Starting to publish radar objects..." << std::endl;
    std::cout << "   Press Ctrl+C to stop\n" << std::endl;

    // Simulate radar data
    uint32_t object_counter = 0;
    
    try {
        while (true) {
            // Create radar object with simulated data
            RadarObject obj;
            obj.object_id = object_counter++;
            obj.distance = 10.0f + (object_counter % 50) * 0.5f;  // 10-35 meters
            obj.velocity = -5.0f + (object_counter % 20) * 0.5f;  // -5 to +5 m/s
            obj.angle = -30.0f + (object_counter % 60);           // -30 to +30 degrees
            obj.confidence = 70 + (object_counter % 30);          // 70-100%
            
            auto now = std::chrono::steady_clock::now();
            obj.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count();

            // Serialize and publish
            ByteBuffer data = serializeRadarObject(obj);
            auto send_result = binding.SendEvent(SERVICE_ID, INSTANCE_ID, EVENT_ID, data);

            // Print status
            std::cout << "Published object #" << std::setw(4) << obj.object_id 
                      << " | dist=" << std::fixed << std::setprecision(1) << std::setw(5) << obj.distance << "m"
                      << " | vel=" << std::setw(5) << obj.velocity << "m/s"
                      << " | angle=" << std::setw(5) << obj.angle << "°"
                      << " | conf=" << std::setw(3) << static_cast<int>(obj.confidence) << "%"
                      << std::endl;

            // Publish at configured rate
            std::this_thread::sleep_for(std::chrono::milliseconds(PUBLISH_RATE_MS));

            // Print metrics every 10 objects
            if (object_counter % 10 == 0) {
                auto metrics = binding.GetMetrics();
                std::cout << "\n--- Metrics ---" << std::endl;
                std::cout << "  Messages sent: " << metrics.messages_sent << std::endl;
                std::cout << "  Bytes sent: " << metrics.bytes_sent << std::endl;
                std::cout << "  Avg latency: " << metrics.avg_latency_ns / 1000.0 << " μs" << std::endl;
                std::cout << "---------------\n" << std::endl;
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    // Cleanup
    std::cout << "\n4. Cleaning up..." << std::endl;
    binding.StopOfferService(SERVICE_ID, INSTANCE_ID);
    binding.Shutdown();
    std::cout << "   ✓ Cleanup complete" << std::endl;

    return 0;
}
