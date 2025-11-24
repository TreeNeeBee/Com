/**
 * @file        dds_subscriber.cpp
 * @brief       DDS Subscriber example - receives events from publishers
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
#include <mutex>
#include <iomanip>

using namespace lap::com::binding;
using namespace std::chrono_literals;

std::atomic<bool> running{true};
std::atomic<uint64_t> received_count{0};
std::atomic<uint64_t> last_sequence{0};
std::atomic<uint64_t> missing_count{0};
std::atomic<uint64_t> total_bytes{0};

std::mutex print_mutex;
auto start_time = std::chrono::steady_clock::now();

void signalHandler(int signum) {
    std::cout << "\n[Subscriber] Interrupt signal (" << signum << ") received. Shutting down..." << std::endl;
    running = false;
}

void eventCallback(
    uint64_t service_id,
    uint64_t instance_id,
    uint32_t event_id,
    const ByteBuffer& data
) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();

    // Extract sequence number
    uint64_t sequence = 0;
    if (data.size() >= 8) {
        const uint64_t* seq_ptr = reinterpret_cast<const uint64_t*>(data.data());
        sequence = *seq_ptr;
    }

    // Check for missing messages
    uint64_t expected_seq = last_sequence.load() + 1;
    if (received_count.load() > 0 && sequence != expected_seq) {
        uint64_t missed = sequence - expected_seq;
        missing_count += missed;
        
        std::lock_guard<std::mutex> lock(print_mutex);
        std::cout << "[Subscriber] WARNING: Missed " << missed << " messages! "
                  << "(expected=" << expected_seq << ", got=" << sequence << ")" << std::endl;
    }

    last_sequence = sequence;
    received_count++;
    total_bytes += data.size();

    // Verify payload pattern
    bool valid = true;
    for (size_t i = 8; i < data.size() && i < 64; ++i) {
        uint8_t expected = static_cast<uint8_t>(i + sequence);
        if (data[i] != expected) {
            valid = false;
            break;
        }
    }

    // Print every 10th message
    if (sequence % 10 == 0) {
        std::lock_guard<std::mutex> lock(print_mutex);
        std::cout << "[Subscriber] Received event #" << sequence 
                  << " (service=0x" << std::hex << service_id 
                  << ", instance=0x" << instance_id << std::dec
                  << ", event=" << event_id
                  << ", size=" << data.size() << " bytes"
                  << ", valid=" << (valid ? "YES" : "NO")
                  << ", time=" << elapsed_ms << "ms)" << std::endl;
    }
}

int main(int argc, char** argv) {
    // Register signal handler
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "=== DDS Subscriber Example ===" << std::endl;
    std::cout << "Press Ctrl+C to stop\n" << std::endl;

    // Parse command line arguments
    uint64_t service_id = 0x1000;
    uint64_t instance_id = 0x0001;
    uint32_t event_id = 100;

    if (argc > 1) service_id = std::stoull(argv[1], nullptr, 16);
    if (argc > 2) instance_id = std::stoull(argv[2], nullptr, 16);
    if (argc > 3) event_id = std::stoul(argv[3]);

    std::cout << "Configuration:" << std::endl;
    std::cout << "  Service ID:    0x" << std::hex << service_id << std::dec << std::endl;
    std::cout << "  Instance ID:   0x" << std::hex << instance_id << std::dec << std::endl;
    std::cout << "  Event ID:      " << event_id << std::endl;
    std::cout << std::endl;

    // Create DDS binding
    auto binding = std::make_unique<DdsBinding>();

    // Initialize
    auto init_result = binding->Initialize();
    if (!init_result.HasValue()) {
        std::cerr << "[Subscriber] Failed to initialize DDS binding" << std::endl;
        return 1;
    }
    std::cout << "[Subscriber] DDS binding initialized" << std::endl;

    // Subscribe to event
    auto subscribe_result = binding->SubscribeEvent(
        service_id,
        instance_id,
        event_id,
        eventCallback
    );

    if (!subscribe_result.HasValue()) {
        std::cerr << "[Subscriber] Failed to subscribe to event" << std::endl;
        return 1;
    }
    std::cout << "[Subscriber] Subscribed to event (0x" << std::hex << service_id 
              << "/0x" << instance_id << std::dec << "/" << event_id << ")" << std::endl;

    // Wait for discovery
    std::cout << "[Subscriber] Waiting for publishers..." << std::endl;
    std::this_thread::sleep_for(500ms);

    start_time = std::chrono::steady_clock::now();
    std::cout << "[Subscriber] Listening for events..." << std::endl;

    // Statistics reporting thread
    std::thread stats_thread([&]() {
        uint64_t last_count = 0;
        auto last_time = std::chrono::steady_clock::now();

        while (running) {
            std::this_thread::sleep_for(5s);

            auto now = std::chrono::steady_clock::now();
            uint64_t current_count = received_count.load();
            uint64_t delta_count = current_count - last_count;
            auto delta_time = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();

            if (delta_time > 0) {
                double rate = (delta_count * 1000.0) / delta_time;
                double total_rate = (current_count * 1000.0) / 
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();

                std::lock_guard<std::mutex> lock(print_mutex);
                std::cout << "\n[Statistics] Messages: " << current_count 
                          << ", Rate: " << std::fixed << std::setprecision(1) << rate << " msg/s"
                          << ", Avg Rate: " << total_rate << " msg/s"
                          << ", Missing: " << missing_count.load()
                          << ", Total Bytes: " << total_bytes.load() << std::endl;
            }

            last_count = current_count;
            last_time = now;
        }
    });

    // Main loop - just wait for events
    while (running) {
        std::this_thread::sleep_for(100ms);
    }

    // Cleanup
    std::cout << "\n[Subscriber] Unsubscribing..." << std::endl;
    binding->UnsubscribeEvent(service_id, instance_id, event_id);
    binding->Shutdown();

    // Wait for stats thread
    running = false;
    if (stats_thread.joinable()) {
        stats_thread.join();
    }

    // Print final statistics
    auto end_time = std::chrono::steady_clock::now();
    auto total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    auto metrics = binding->GetMetrics();
    
    std::cout << "\n=== Subscriber Statistics ===" << std::endl;
    std::cout << "  Runtime:            " << total_time_ms / 1000.0 << " seconds" << std::endl;
    std::cout << "  Messages Received:  " << received_count.load() << std::endl;
    std::cout << "  Messages Missing:   " << missing_count.load() << std::endl;
    std::cout << "  Total Bytes:        " << total_bytes.load() << std::endl;
    
    if (total_time_ms > 0) {
        double avg_rate = (received_count.load() * 1000.0) / total_time_ms;
        double throughput_kbps = (total_bytes.load() * 8.0) / total_time_ms;
        std::cout << "  Average Rate:       " << std::fixed << std::setprecision(2) 
                  << avg_rate << " msg/s" << std::endl;
        std::cout << "  Throughput:         " << throughput_kbps << " Kbps" << std::endl;
    }
    
    std::cout << "\n  DDS Metrics:" << std::endl;
    std::cout << "    Messages Received:  " << metrics.messages_received << std::endl;
    std::cout << "    Bytes Received:     " << metrics.bytes_received << std::endl;

    std::cout << "\n[Subscriber] Shutdown complete" << std::endl;
    return 0;
}
