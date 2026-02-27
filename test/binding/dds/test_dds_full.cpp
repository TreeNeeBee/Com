/**
 * @file        test_dds_full.cpp
 * @brief       Cross-process DDS full functional test (event/method/field)
 * @date        2026-02-02
 */

#include "DdsBinding.hpp"
#include "ComTypes.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>

using namespace lap::com::binding;

namespace
{
    constexpr uint64_t kServiceId = 0x3344;
    constexpr uint64_t kInstanceId = 0x0001;
    constexpr uint32_t kEventId = 200;
    constexpr uint32_t kMethodId = 1;
    constexpr uint32_t kFieldId = 10;

    constexpr uint32_t kFieldGetterId = kFieldId | 0x10000U;
    constexpr uint32_t kFieldSetterId = kFieldId | 0x20000U;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <server|client>" << std::endl;
        return 1;
    }

    const std::string mode(argv[1]);
    DdsBinding binding;
    binding.SetDiscoveryServer("tcp://127.0.0.1:42102");

    if (!binding.Initialize().HasValue()) {
        std::cerr << "Failed to initialize DDS binding" << std::endl;
        return 1;
    }

    if (mode == "server") {
        std::cout << "[SERVER] Starting DDS full test server..." << std::endl;

        auto offer_result = binding.OfferService(kServiceId, kInstanceId);
        if (!offer_result.HasValue()) {
            std::cerr << "[SERVER] OfferService failed" << std::endl;
            return 2;
        }

        std::mutex field_mutex;
        ByteBuffer field_value = {0x0A};

        auto method_result = binding.RegisterMethod< ByteBuffer, ByteBuffer >(
            kServiceId,
            kInstanceId,
            kMethodId,
            [](uint64_t, uint64_t, uint32_t, const ByteBuffer& request) {
                ByteBuffer response = request;
                response.push_back(0xEE);
                return response;
            }
        );

        if (!method_result.HasValue()) {
            std::cerr << "[SERVER] RegisterMethod failed" << std::endl;
            return 2;
        }

        auto getter_result = binding.RegisterMethod< ByteBuffer, ByteBuffer >(
            kServiceId,
            kInstanceId,
            kFieldGetterId,
            [&field_mutex, &field_value](uint64_t, uint64_t, uint32_t, const ByteBuffer&) {
                std::scoped_lock< std::mutex > lock(field_mutex);
                return field_value;
            }
        );

        if (!getter_result.HasValue()) {
            std::cerr << "[SERVER] Register getter failed" << std::endl;
            return 2;
        }

        auto setter_result = binding.RegisterMethod< ByteBuffer, ByteBuffer >(
            kServiceId,
            kInstanceId,
            kFieldSetterId,
            [&field_mutex, &field_value](uint64_t, uint64_t, uint32_t, const ByteBuffer& request) {
                std::scoped_lock< std::mutex > lock(field_mutex);
                field_value = request;
                return ByteBuffer{};
            }
        );

        if (!setter_result.HasValue()) {
            std::cerr << "[SERVER] Register setter failed" << std::endl;
            return 2;
        }

        std::cout << "[SERVER] Waiting for discovery..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));

        ByteBuffer event_payload = {0x11, 0x22, 0x33};
        for (int i = 0; i < 5; ++i) {
            auto send_result = binding.SendEvent(kServiceId, kInstanceId, kEventId, event_payload);
            std::cout << "[SERVER] SendEvent #" << i << ": "
                      << (send_result.HasValue() ? "SUCCESS" : "FAILED") << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        std::cout << "[SERVER] Waiting for client requests..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(8));

        binding.Shutdown();
        std::cout << "[SERVER] Exiting..." << std::endl;
        return 0;
    }

    if (mode == "client") {
        std::cout << "[CLIENT] Starting DDS full test client..." << std::endl;

        std::mutex event_mutex;
        std::condition_variable event_cv;
        std::atomic< int > event_count{0};
        ByteBuffer last_event;

        auto sub_result = binding.SubscribeEvent< ByteBuffer >(
            kServiceId,
            kInstanceId,
            kEventId,
            [&event_mutex, &event_cv, &event_count, &last_event](
                uint64_t, uint64_t, uint32_t, const ByteBuffer& data) {
                {
                    std::scoped_lock< std::mutex > lock(event_mutex);
                    last_event = data;
                    event_count++;
                }
                event_cv.notify_one();
            }
        );

        if (!sub_result.HasValue()) {
            std::cerr << "[CLIENT] SubscribeEvent failed" << std::endl;
            return 2;
        }

        std::cout << "[CLIENT] Waiting for discovery..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));

        ByteBuffer method_req = {0x01, 0x02};
        auto method_result = binding.CallMethod< ByteBuffer >(kServiceId, kInstanceId, kMethodId, method_req);
        if (!method_result.HasValue()) {
            std::cerr << "[CLIENT] CallMethod failed" << std::endl;
            return 2;
        }

        ByteBuffer expected_resp = {0x01, 0x02, 0xEE};
        if (method_result.Value() != expected_resp) {
            std::cerr << "[CLIENT] Method response mismatch" << std::endl;
            return 3;
        }

        ByteBuffer field_value = {0xAA, 0xBB, 0xCC};
        auto set_result = binding.SetField(kServiceId, kInstanceId, kFieldId, field_value);
        if (!set_result.HasValue()) {
            std::cerr << "[CLIENT] SetField failed" << std::endl;
            return 2;
        }

        auto get_result = binding.GetField< ByteBuffer >(kServiceId, kInstanceId, kFieldId);
        if (!get_result.HasValue()) {
            std::cerr << "[CLIENT] GetField failed" << std::endl;
            return 2;
        }

        if (get_result.Value() != field_value) {
            std::cerr << "[CLIENT] Field value mismatch" << std::endl;
            return 3;
        }

        std::unique_lock< std::mutex > lock(event_mutex);
        bool got_event = event_cv.wait_for(lock, std::chrono::seconds(5), [&event_count]() {
            return event_count.load() > 0;
        });

        if (!got_event) {
            std::cerr << "[CLIENT] Timed out waiting for event" << std::endl;
            return 4;
        }

        std::cout << "[CLIENT] Received event count: " << event_count.load() << std::endl;
        std::cout << "[CLIENT] Full test SUCCESS" << std::endl;

        binding.Shutdown();
        return 0;
    }

    std::cerr << "Invalid mode: " << mode << " (use 'server' or 'client')" << std::endl;
    return 1;
}