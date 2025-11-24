/**
 * @file        test_dds_cross_process.cpp
 * @brief       Cross-process DDS binding test (publisher)
 * @date        2025-01-09
 */

#include "DdsBinding.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace lap::com::binding;

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <pub|sub>" << std::endl;
        return 1;
    }

    std::string mode(argv[1]);
    DdsBinding binding;

    if (!binding.Initialize().HasValue()) {
        std::cerr << "Failed to initialize DDS binding" << std::endl;
        return 1;
    }

    uint64_t service_id = 0x1234;
    uint64_t instance_id = 0x0001;
    uint32_t event_id = 100;

    if (mode == "pub") {
        std::cout << "[PUBLISHER] Starting..." << std::endl;
        std::cout << "[PUBLISHER] Service ID: 0x" << std::hex << service_id << std::dec << std::endl;
        std::cout << "[PUBLISHER] Instance ID: 0x" << std::hex << instance_id << std::dec << std::endl;
        std::cout << "[PUBLISHER] Event ID: " << event_id << std::endl;
        
        auto offer_result = binding.OfferService(service_id, instance_id);
        std::cout << "[PUBLISHER] OfferService: " << (offer_result.HasValue() ? "SUCCESS" : "FAILED") << std::endl;
        
        std::cout << "[PUBLISHER] Waiting for discovery..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));  // Increased discovery time

        ByteBuffer test_data = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
        
        for (int i = 0; i < 10; ++i) {
            auto result = binding.SendEvent(service_id, instance_id, event_id, test_data);
            std::cout << "[PUBLISHER] Sent event #" << i << ": " 
                      << (result.HasValue() ? "SUCCESS" : "FAILED") << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    else if (mode == "sub") {
        std::cout << "[SUBSCRIBER] Starting..." << std::endl;
        std::cout << "[SUBSCRIBER] Service ID: 0x" << std::hex << service_id << std::dec << std::endl;
        std::cout << "[SUBSCRIBER] Instance ID: 0x" << std::hex << instance_id << std::dec << std::endl;
        std::cout << "[SUBSCRIBER] Event ID: " << event_id << std::endl;
        
        int received_count = 0;
        auto sub_result = binding.SubscribeEvent(service_id, instance_id, event_id,
            [&received_count](uint64_t sid, uint64_t iid, uint32_t eid, const ByteBuffer& data) {
                std::cout << "[SUBSCRIBER] Received event: service=0x" << std::hex << sid
                          << ", instance=0x" << iid << std::dec
                          << ", event=" << eid
                          << ", size=" << data.size() << " bytes, data=[";
                for (auto byte : data) {
                    std::cout << "0x" << std::hex << (int)byte << std::dec << " ";
                }
                std::cout << "]" << std::endl;
                received_count++;
            });
        
        std::cout << "[SUBSCRIBER] SubscribeEvent: " << (sub_result.HasValue() ? "SUCCESS" : "FAILED") << std::endl;
        std::cout << "[SUBSCRIBER] Waiting for events (Ctrl+C to exit)..." << std::endl;
        
        // Wait for events
        for (int i = 0; i < 15; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cout << "[SUBSCRIBER] Total received: " << received_count << std::endl;
        }
    }
    else {
        std::cerr << "Invalid mode: " << mode << " (use 'pub' or 'sub')" << std::endl;
        return 1;
    }

    binding.Shutdown();
    std::cout << "[" << (mode == "pub" ? "PUBLISHER" : "SUBSCRIBER") << "] Exiting..." << std::endl;
    
    return 0;
}
