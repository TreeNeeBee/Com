/**
 * @file        test_iceoryx2_pubsub.cpp
 * @brief       Comprehensive pub/sub test suite for iceoryx2 binding
 * @date        2025-11-23
 */

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <vector>
#include <cassert>

#include "Iceoryx2Binding.hpp"

using namespace lap::com::binding;

// Test counters
std::atomic<int> received_count{0};
std::atomic<int> large_msg_count{0};
std::atomic<int> multi_sub_count1{0};
std::atomic<int> multi_sub_count2{0};

// Callback for basic test
void basicEventCallback(uint64_t service_id, uint64_t instance_id, uint32_t event_id, const ByteBuffer& data)
{
    std::cout << "  [Callback] Received: service=0x" << std::hex << service_id
              << ", instance=0x" << instance_id
              << ", event=0x" << event_id << std::dec
              << ", size=" << data.size() << " bytes";
    
    if (data.size() > 0) {
        std::cout << ", data[0]=" << static_cast<int>(data[0]);
    }
    std::cout << std::endl;
    
    received_count.fetch_add(1);
}

// Callback for large message test
void largeMessageCallback(uint64_t, uint64_t, uint32_t, const ByteBuffer& data)
{
    large_msg_count.fetch_add(1);
    std::cout << "  [Large] Received " << data.size() << " bytes" << std::endl;
}

// Callbacks for multi-subscriber test
void multiSubCallback1(uint64_t, uint64_t, uint32_t, const ByteBuffer& data)
{
    multi_sub_count1.fetch_add(1);
    std::cout << "  [Sub1] Received " << data.size() << " bytes" << std::endl;
}

void multiSubCallback2(uint64_t, uint64_t, uint32_t, const ByteBuffer& data)
{
    multi_sub_count2.fetch_add(1);
    std::cout << "  [Sub2] Received " << data.size() << " bytes" << std::endl;
}

// Test 1: Basic Pub/Sub
bool test_basic_pubsub()
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "TEST 1: Basic Pub/Sub" << std::endl;
    std::cout << "========================================" << std::endl;
    
    received_count.store(0);
    
    Iceoryx2Binding publisher;
    Iceoryx2Binding subscriber;

    std::cout << "1. Initializing..." << std::endl;
    publisher.Initialize();
    subscriber.Initialize();

    const uint64_t service_id = 0x1001;
    const uint64_t instance_id = 0x2001;
    const uint32_t event_id = 0x3001;

    std::cout << "2. Offering service..." << std::endl;
    publisher.OfferService(service_id, instance_id);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::cout << "3. Subscribing..." << std::endl;
    subscriber.SubscribeEvent(service_id, instance_id, event_id, basicEventCallback);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::cout << "4. Sending 10 messages..." << std::endl;
    for (int i = 0; i < 10; i++) {
        ByteBuffer data(1);
        data[0] = static_cast<uint8_t>(i);
        publisher.SendEvent(service_id, instance_id, event_id, data);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "5. Results: Sent=10, Received=" << received_count.load() << std::endl;

    auto metrics = publisher.GetMetrics();
    std::cout << "   Publisher metrics: sent=" << metrics.messages_sent 
              << ", bytes=" << metrics.bytes_sent
              << ", latency=" << metrics.avg_latency_ns << "ns" << std::endl;

    subscriber.UnsubscribeEvent(service_id, instance_id, event_id);
    publisher.StopOfferService(service_id, instance_id);
    subscriber.Shutdown();
    publisher.Shutdown();

    bool passed = (received_count.load() == 10);
    std::cout << "Result: " << (passed ? "✓ PASSED" : "✗ FAILED") << std::endl;
    return passed;
}

// Test 2: Multiple Messages
bool test_multiple_messages()
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "TEST 2: High Frequency Messages" << std::endl;
    std::cout << "========================================" << std::endl;
    
    received_count.store(0);
    
    Iceoryx2Binding publisher;
    Iceoryx2Binding subscriber;

    publisher.Initialize();
    subscriber.Initialize();

    const uint64_t service_id = 0x1002;
    const uint64_t instance_id = 0x2002;
    const uint32_t event_id = 0x3002;

    publisher.OfferService(service_id, instance_id);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    subscriber.SubscribeEvent(service_id, instance_id, event_id, basicEventCallback);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::cout << "Sending 100 messages rapidly..." << std::endl;
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < 100; i++) {
        ByteBuffer data(1);
        data[0] = static_cast<uint8_t>(i % 256);
        publisher.SendEvent(service_id, instance_id, event_id, data);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "Sent 100 messages in " << duration << "ms" << std::endl;
    std::cout << "Received: " << received_count.load() << " messages" << std::endl;

    subscriber.UnsubscribeEvent(service_id, instance_id, event_id);
    publisher.StopOfferService(service_id, instance_id);
    subscriber.Shutdown();
    publisher.Shutdown();

    bool passed = (received_count.load() >= 95); // Allow 5% loss
    std::cout << "Result: " << (passed ? "✓ PASSED" : "✗ FAILED") << std::endl;
    return passed;
}

// Test 3: Multi-Subscriber
bool test_multi_subscriber()
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "TEST 3: Multiple Subscribers (1-to-N)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    multi_sub_count1.store(0);
    multi_sub_count2.store(0);
    
    Iceoryx2Binding publisher;
    Iceoryx2Binding subscriber1;
    Iceoryx2Binding subscriber2;

    publisher.Initialize();
    subscriber1.Initialize();
    subscriber2.Initialize();

    const uint64_t service_id = 0x1003;
    const uint64_t instance_id = 0x2003;
    const uint32_t event_id = 0x3003;

    std::cout << "1. Publisher offering service..." << std::endl;
    publisher.OfferService(service_id, instance_id);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::cout << "2. Subscriber 1 subscribing..." << std::endl;
    subscriber1.SubscribeEvent(service_id, instance_id, event_id, multiSubCallback1);
    
    std::cout << "3. Subscriber 2 subscribing..." << std::endl;
    subscriber2.SubscribeEvent(service_id, instance_id, event_id, multiSubCallback2);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "4. Sending 5 messages..." << std::endl;
    for (int i = 0; i < 5; i++) {
        ByteBuffer data(1);
        data[0] = static_cast<uint8_t>(i);
        publisher.SendEvent(service_id, instance_id, event_id, data);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "5. Results:" << std::endl;
    std::cout << "   Subscriber 1 received: " << multi_sub_count1.load() << std::endl;
    std::cout << "   Subscriber 2 received: " << multi_sub_count2.load() << std::endl;

    subscriber1.UnsubscribeEvent(service_id, instance_id, event_id);
    subscriber2.UnsubscribeEvent(service_id, instance_id, event_id);
    publisher.StopOfferService(service_id, instance_id);
    
    subscriber1.Shutdown();
    subscriber2.Shutdown();
    publisher.Shutdown();

    bool passed = (multi_sub_count1.load() == 5 && multi_sub_count2.load() == 5);
    std::cout << "Result: " << (passed ? "✓ PASSED" : "✗ FAILED") << std::endl;
    return passed;
}

// Test 4: Subscribe Before Offer
bool test_subscribe_before_offer()
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "TEST 4: Subscribe Before Service Offered" << std::endl;
    std::cout << "========================================" << std::endl;
    
    received_count.store(0);
    
    Iceoryx2Binding publisher;
    Iceoryx2Binding subscriber;

    publisher.Initialize();
    subscriber.Initialize();

    const uint64_t service_id = 0x1004;
    const uint64_t instance_id = 0x2004;
    const uint32_t event_id = 0x3004;

    std::cout << "1. Trying to subscribe (service not offered yet)..." << std::endl;
    subscriber.SubscribeEvent(service_id, instance_id, event_id, basicEventCallback);
    // This should fail gracefully or wait
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "2. Now offering service..." << std::endl;
    publisher.OfferService(service_id, instance_id);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::cout << "3. Re-subscribing after service is offered..." << std::endl;
    subscriber.UnsubscribeEvent(service_id, instance_id, event_id);
    subscriber.SubscribeEvent(service_id, instance_id, event_id, basicEventCallback);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "4. Sending messages..." << std::endl;
    for (int i = 0; i < 3; i++) {
        ByteBuffer data(1);
        data[0] = static_cast<uint8_t>(i);
        publisher.SendEvent(service_id, instance_id, event_id, data);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "5. Received: " << received_count.load() << " messages" << std::endl;

    subscriber.UnsubscribeEvent(service_id, instance_id, event_id);
    publisher.StopOfferService(service_id, instance_id);
    subscriber.Shutdown();
    publisher.Shutdown();

    bool passed = (received_count.load() == 3);
    std::cout << "Result: " << (passed ? "✓ PASSED" : "✗ FAILED") << std::endl;
    return passed;
}

// Test 5: Cleanup and Restart
bool test_cleanup_restart()
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "TEST 5: Cleanup and Restart" << std::endl;
    std::cout << "========================================" << std::endl;
    
    received_count.store(0);
    
    const uint64_t service_id = 0x1005;
    const uint64_t instance_id = 0x2005;
    const uint32_t event_id = 0x3005;

    // First session
    {
        std::cout << "1. First session..." << std::endl;
        Iceoryx2Binding publisher;
        Iceoryx2Binding subscriber;

        publisher.Initialize();
        subscriber.Initialize();
        publisher.OfferService(service_id, instance_id);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        subscriber.SubscribeEvent(service_id, instance_id, event_id, basicEventCallback);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        ByteBuffer data(1);
        data[0] = 1;
        publisher.SendEvent(service_id, instance_id, event_id, data);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        subscriber.UnsubscribeEvent(service_id, instance_id, event_id);
        publisher.StopOfferService(service_id, instance_id);
        subscriber.Shutdown();
        publisher.Shutdown();
        
        std::cout << "   First session: received " << received_count.load() << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Second session
    {
        std::cout << "2. Second session (after cleanup)..." << std::endl;
        received_count.store(0);
        
        Iceoryx2Binding publisher;
        Iceoryx2Binding subscriber;

        publisher.Initialize();
        subscriber.Initialize();
        publisher.OfferService(service_id, instance_id);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        subscriber.SubscribeEvent(service_id, instance_id, event_id, basicEventCallback);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        ByteBuffer data(1);
        data[0] = 2;
        publisher.SendEvent(service_id, instance_id, event_id, data);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        std::cout << "   Second session: received " << received_count.load() << std::endl;
        
        subscriber.UnsubscribeEvent(service_id, instance_id, event_id);
        publisher.StopOfferService(service_id, instance_id);
        subscriber.Shutdown();
        publisher.Shutdown();
    }

    bool passed = (received_count.load() == 1);
    std::cout << "Result: " << (passed ? "✓ PASSED" : "✗ FAILED") << std::endl;
    return passed;
}

int main()
{
    std::cout << "==========================================" << std::endl;
    std::cout << "  iceoryx2 Binding Test Suite" << std::endl;
    std::cout << "==========================================" << std::endl;

    int passed = 0;
    int total = 5;

    if (test_basic_pubsub()) passed++;
    if (test_multiple_messages()) passed++;
    if (test_multi_subscriber()) passed++;
    if (test_subscribe_before_offer()) passed++;
    if (test_cleanup_restart()) passed++;

    std::cout << "\n==========================================" << std::endl;
    std::cout << "  Test Summary" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Passed: " << passed << "/" << total << std::endl;
    std::cout << "Result: " << (passed == total ? "✓ ALL TESTS PASSED" : "✗ SOME TESTS FAILED") << std::endl;
    std::cout << "==========================================" << std::endl;

    return (passed == total) ? 0 : 1;
}
