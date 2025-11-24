/**
 * @file        ping_pong_example.cpp
 * @brief       iceoryx2 Bidirectional Example - Ping Pong Latency Test
 * @details     Measures round-trip latency using two services
 * @date        2025-11-23
 */

#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <atomic>
#include <vector>

#include "Iceoryx2Binding.hpp"

using namespace lap::com::binding;

// Ping/Pong message
struct PingPongMessage {
    uint32_t sequence;
    uint64_t send_timestamp_us;
    uint8_t payload[8];
} __attribute__((packed));

std::atomic<bool> running{true};
std::atomic<uint32_t> pong_count{0};
std::vector<uint64_t> latencies;

// Serialize message
ByteBuffer serialize(const PingPongMessage& msg)
{
    ByteBuffer buffer(sizeof(PingPongMessage));
    std::memcpy(buffer.data(), &msg, sizeof(PingPongMessage));
    return buffer;
}

// Deserialize message
PingPongMessage deserialize(const ByteBuffer& data)
{
    PingPongMessage msg;
    if (data.size() >= sizeof(PingPongMessage)) {
        std::memcpy(&msg, data.data(), sizeof(PingPongMessage));
    }
    return msg;
}

// Get current time in microseconds
uint64_t getCurrentTimeMicros()
{
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
}

class PingNode
{
public:
    PingNode() : binding_() {}

    void run(int num_pings)
    {
        std::cout << "=== PING NODE ===" << std::endl;
        
        // Initialize
        binding_.Initialize();
        
        // Offer PING service (for sending pings)
        binding_.OfferService(0x1000, 0x0001);
        
        // Subscribe to PONG service (for receiving pongs)
        binding_.SubscribeEvent(0x2000, 0x0001, 0x0001, 
            [this](uint64_t, uint64_t, uint32_t, const ByteBuffer& data) {
                this->handlePong(data);
            });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        std::cout << "Sending " << num_pings << " pings..." << std::endl;
        
        for (int i = 0; i < num_pings; i++) {
            PingPongMessage ping;
            ping.sequence = i;
            ping.send_timestamp_us = getCurrentTimeMicros();
            std::memset(ping.payload, i % 256, sizeof(ping.payload));
            
            ByteBuffer data = serialize(ping);
            binding_.SendEvent(0x1000, 0x0001, 0x0001, data);
            
            std::cout << "  Sent PING #" << i << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Wait for all pongs
        for (int i = 0; i < 50 && pong_count.load() < static_cast<uint32_t>(num_pings); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Print statistics
        printStatistics();
        
        // Cleanup
        binding_.UnsubscribeEvent(0x2000, 0x0001, 0x0001);
        binding_.StopOfferService(0x1000, 0x0001);
        binding_.Shutdown();
    }

private:
    void handlePong(const ByteBuffer& data)
    {
        PingPongMessage pong = deserialize(data);
        uint64_t now = getCurrentTimeMicros();
        uint64_t rtt_us = now - pong.send_timestamp_us;
        
        latencies.push_back(rtt_us);
        pong_count.fetch_add(1);
        
        std::cout << "  Received PONG #" << pong.sequence 
                  << " | RTT=" << rtt_us << "μs" << std::endl;
    }

    void printStatistics()
    {
        if (latencies.empty()) {
            std::cout << "No latency data collected" << std::endl;
            return;
        }

        uint64_t sum = 0;
        uint64_t min_lat = latencies[0];
        uint64_t max_lat = latencies[0];
        
        for (uint64_t lat : latencies) {
            sum += lat;
            min_lat = std::min(min_lat, lat);
            max_lat = std::max(max_lat, lat);
        }
        
        uint64_t avg_lat = sum / latencies.size();
        
        std::cout << "\n=== Latency Statistics ===" << std::endl;
        std::cout << "  Samples: " << latencies.size() << std::endl;
        std::cout << "  Min RTT: " << min_lat << " μs" << std::endl;
        std::cout << "  Max RTT: " << max_lat << " μs" << std::endl;
        std::cout << "  Avg RTT: " << avg_lat << " μs" << std::endl;
        std::cout << "=========================" << std::endl;
    }

    Iceoryx2Binding binding_;
};

class PongNode
{
public:
    PongNode() : binding_() {}

    void run()
    {
        std::cout << "=== PONG NODE ===" << std::endl;
        
        // Initialize
        binding_.Initialize();
        
        // Offer PONG service (for sending pongs)
        binding_.OfferService(0x2000, 0x0001);
        
        // Subscribe to PING service (for receiving pings)
        binding_.SubscribeEvent(0x1000, 0x0001, 0x0001,
            [this](uint64_t, uint64_t, uint32_t, const ByteBuffer& data) {
                this->handlePing(data);
            });
        
        std::cout << "Waiting for pings (press Ctrl+C to stop)..." << std::endl;
        
        while (running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Cleanup
        binding_.UnsubscribeEvent(0x1000, 0x0001, 0x0001);
        binding_.StopOfferService(0x2000, 0x0001);
        binding_.Shutdown();
    }

private:
    void handlePing(const ByteBuffer& data)
    {
        PingPongMessage ping = deserialize(data);
        
        std::cout << "  Received PING #" << ping.sequence << std::endl;
        
        // Send PONG back with same timestamp
        ByteBuffer pong_data = serialize(ping);
        binding_.SendEvent(0x2000, 0x0001, 0x0001, pong_data);
        
        std::cout << "  Sent PONG #" << ping.sequence << std::endl;
    }

    Iceoryx2Binding binding_;
};

int main(int argc, char* argv[])
{
    std::cout << "========================================" << std::endl;
    std::cout << "  iceoryx2 Ping-Pong Example" << std::endl;
    std::cout << "  Bidirectional Latency Test" << std::endl;
    std::cout << "========================================\n" << std::endl;

    if (argc < 2) {
        std::cout << "Usage:" << std::endl;
        std::cout << "  " << argv[0] << " ping [count]   - Run as PING node" << std::endl;
        std::cout << "  " << argv[0] << " pong           - Run as PONG node" << std::endl;
        std::cout << "\nExample:" << std::endl;
        std::cout << "  Terminal 1: " << argv[0] << " pong" << std::endl;
        std::cout << "  Terminal 2: " << argv[0] << " ping 10" << std::endl;
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "ping") {
        int count = (argc >= 3) ? std::atoi(argv[2]) : 10;
        PingNode node;
        node.run(count);
    }
    else if (mode == "pong") {
        PongNode node;
        node.run();
    }
    else {
        std::cerr << "Invalid mode: " << mode << std::endl;
        std::cerr << "Use 'ping' or 'pong'" << std::endl;
        return 1;
    }

    return 0;
}
