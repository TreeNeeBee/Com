/**
 * @file        dds_publisher.cpp
 * @brief       DDS Publisher example - sends events to subscribers
 * @author      LightAP Team
 * @date        2025-11-23
 * @details     Demonstrates cross-process DDS communication using DdsBinding
 */

#include "DdsBinding.hpp"
#include "ComTypes.hpp"

#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

using namespace lap::com::binding;
using namespace std::chrono_literals;

std::atomic<bool> running{true};

void signalHandler(int signum) {
    std::cout << "\n[Publisher] Interrupt signal (" << signum << ") received. Shutting down..." << std::endl;
    running = false;
}

int main(int argc, char** argv) {
    // Register signal handler
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "=== DDS Publisher Example ===" << std::endl;
    std::cout << "Press Ctrl+C to stop\n" << std::endl;

    // Parse command line arguments
    uint64_t service_id = 0x1000;
    uint64_t instance_id = 0x0001;
    uint32_t event_id = 100;
    int rate_hz = 10;  // 10 Hz default
    size_t payload_size = 64;  // 64 bytes default

    if (argc > 1) service_id = std::stoull(argv[1], nullptr, 16);
    if (argc > 2) instance_id = std::stoull(argv[2], nullptr, 16);
    if (argc > 3) event_id = std::stoul(argv[3]);
    if (argc > 4) rate_hz = std::stoi(argv[4]);
    if (argc > 5) payload_size = std::stoul(argv[5]);

    std::cout << "Configuration:" << std::endl;
    std::cout << "  Service ID:    0x" << std::hex << service_id << std::dec << std::endl;
    std::cout << "  Instance ID:   0x" << std::hex << instance_id << std::dec << std::endl;
    std::cout << "  Event ID:      " << event_id << std::endl;
    std::cout << "  Publish Rate:  " << rate_hz << " Hz" << std::endl;
    std::cout << "  Payload Size:  " << payload_size << " bytes" << std::endl;
    std::cout << std::endl;

    // Create DDS binding
    auto binding = std::make_unique<DdsBinding>();

    // Initialize
    auto init_result = binding->Initialize();
    if (!init_result.HasValue()) {
        std::cerr << "[Publisher] Failed to initialize DDS binding" << std::endl;
        return 1;
    }
    std::cout << "[Publisher] DDS binding initialized" << std::endl;

    // Offer service
    auto offer_result = binding->OfferService(service_id, instance_id);
    if (!offer_result.HasValue()) {
        std::cerr << "[Publisher] Failed to offer service" << std::endl;
        return 1;
    }
    std::cout << "[Publisher] Service offered (0x" << std::hex << service_id 
              << "/0x" << instance_id << std::dec << ")" << std::endl;

    // Wait for discovery
    std::cout << "[Publisher] Waiting for subscribers to discover..." << std::endl;
    std::this_thread::sleep_for(500ms);

    // Publish loop
    std::cout << "[Publisher] Starting to publish events..." << std::endl;
    uint64_t sequence = 0;
    auto interval = std::chrono::milliseconds(1000 / rate_hz);
    auto next_publish = std::chrono::steady_clock::now();

    while (running) {
        // Create payload
        ByteBuffer payload(payload_size);
        
        // Fill with test pattern: sequence number (8 bytes) + counter pattern
        uint64_t* seq_ptr = reinterpret_cast<uint64_t*>(payload.data());
        *seq_ptr = sequence;
        for (size_t i = 8; i < payload_size; ++i) {
            payload[i] = static_cast<uint8_t>(i + sequence);
        }

        // Send event
        auto send_result = binding->SendEvent(service_id, instance_id, event_id, payload);
        
        if (send_result.HasValue()) {
            if (sequence % 10 == 0) {  // Print every 10th message
                std::cout << "[Publisher] Sent event #" << sequence 
                          << " (size=" << payload_size << " bytes)" << std::endl;
            }
        } else {
            std::cerr << "[Publisher] Failed to send event #" << sequence << std::endl;
        }

        sequence++;

        // Rate control
        next_publish += interval;
        std::this_thread::sleep_until(next_publish);
    }

    // Cleanup
    std::cout << "\n[Publisher] Stopping service..." << std::endl;
    binding->StopOfferService(service_id, instance_id);
    binding->Shutdown();

    // Print statistics
    auto metrics = binding->GetMetrics();
    std::cout << "\n=== Publisher Statistics ===" << std::endl;
    std::cout << "  Messages Sent:     " << metrics.messages_sent << std::endl;
    std::cout << "  Bytes Sent:        " << metrics.bytes_sent << std::endl;
    std::cout << "  Messages Dropped:  " << metrics.messages_dropped << std::endl;
    std::cout << "  Avg Latency:       " << metrics.avg_latency_ns / 1000.0 << " µs" << std::endl;
    std::cout << "  Min Latency:       " << metrics.min_latency_ns / 1000.0 << " µs" << std::endl;
    std::cout << "  Max Latency:       " << metrics.max_latency_ns / 1000.0 << " µs" << std::endl;

    std::cout << "\n[Publisher] Shutdown complete" << std::endl;
    return 0;
}
