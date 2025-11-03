/**
 * @file        com_socket_event_test.cpp
 * @brief       Unit tests for SocketEventBinding (Publisher/Subscriber)
 */

#include <binding/socket/SocketEventBinding.hpp>
#include <gtest/gtest.h>

#include "../../tools/protobuf/generated/calculator.pb.h"

#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <string>

using namespace lap::com::binding::socket;

class SocketEventTest : public ::testing::Test {
protected:
    void SetUp() override {
        // unique socket path
        socketPath_ = "/tmp/test_socket_event_" + std::to_string(std::time(nullptr)) + ".sock";
    }
    void TearDown() override {
        ::unlink(socketPath_.c_str());
    }
    std::string socketPath_;
};

TEST_F(SocketEventTest, BasicPublishSubscribe) {
    SocketEventPublisher<lap::com::example::EchoResponse> pub(socketPath_);
    ASSERT_TRUE(pub.start().HasValue());

    std::atomic<int> received{0};
    SocketEventSubscriber<lap::com::example::EchoResponse> sub(
        socketPath_,
        [&received](const lap::com::example::EchoResponse& msg){
            if (msg.messages_size()>0 && msg.messages(0) == std::string("hello") && msg.message_count() == 1) ++received;
        }
    );
    ASSERT_TRUE(sub.start().HasValue());

    lap::com::example::EchoResponse evt;
    evt.add_messages("hello");
    evt.set_message_count(1);
    ASSERT_TRUE(pub.publish(evt).HasValue());

    // wait up to 1s
    for (int i=0;i<20 && received.load()<1;++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(received.load(), 1);

    sub.stop();
    pub.stop();
}

TEST_F(SocketEventTest, MultipleSubscribersReceive) {
    SocketEventPublisher<lap::com::example::EchoResponse> pub(socketPath_);
    ASSERT_TRUE(pub.start().HasValue());

    const int nSubs = 5;
    std::vector<std::unique_ptr<SocketEventSubscriber<lap::com::example::EchoResponse>>> subs;
    std::vector<int> counts(nSubs, 0);
    for (int i=0;i<nSubs;++i) {
        subs.emplace_back(new SocketEventSubscriber<lap::com::example::EchoResponse>(
            socketPath_,
            [&counts,i](const lap::com::example::EchoResponse& m){
                if (m.messages_size()>0 && m.messages(0)==std::string("fanout")) counts[i]++; }
        ));
        ASSERT_TRUE(subs.back()->start().HasValue());
    }

    lap::com::example::EchoResponse evt; evt.add_messages("fanout"); evt.set_message_count(0);
    for (int k=0;k<10;++k) ASSERT_TRUE(pub.publish(evt).HasValue());

    // wait for delivery
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    for (int i=0;i<nSubs;++i) {
        EXPECT_GE(counts[i], 10);
    }

    for (auto& s: subs) s->stop();
    pub.stop();
}

TEST_F(SocketEventTest, LargePayloadOneMB) {
    SocketEventPublisher<lap::com::example::EchoResponse> pub(socketPath_);
    ASSERT_TRUE(pub.start().HasValue());

    std::atomic<size_t> gotLen{0};
    SocketEventSubscriber<lap::com::example::EchoResponse> sub(
        socketPath_,
    [&gotLen](const lap::com::example::EchoResponse& m){ gotLen = (m.messages_size()>0? m.messages(0).size() : 0); }
    );
    ASSERT_TRUE(sub.start().HasValue());

    std::string big(1<<20, 'A');
    lap::com::example::EchoResponse evt; evt.add_messages(big); evt.set_message_count(7);
    ASSERT_TRUE(pub.publish(evt).HasValue());

    for (int i=0;i<40 && gotLen.load()==0;++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(gotLen.load(), static_cast<size_t>(1<<20));

    sub.stop(); pub.stop();
}

TEST_F(SocketEventTest, StressBurst1000) {
    SocketEventPublisher<lap::com::example::EchoResponse> pub(socketPath_);
    ASSERT_TRUE(pub.start().HasValue());
    std::atomic<int> received{0};
    SocketEventSubscriber<lap::com::example::EchoResponse> sub(
        socketPath_,
        [&received](const lap::com::example::EchoResponse&){ ++received; }
    );
    ASSERT_TRUE(sub.start().HasValue());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    lap::com::example::EchoResponse e; e.add_messages("burst"); e.set_message_count(0);
    for (int i=0;i<1000;++i) ASSERT_TRUE(pub.publish(e).HasValue());

    // wait up to 3s
    for (int i=0;i<60 && received.load()<1000;++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_GE(received.load(), 1000);

    sub.stop(); pub.stop();
}

TEST_F(SocketEventTest, BenchmarkQPS) {
    SocketEventPublisher<lap::com::example::EchoResponse> pub(socketPath_);
    ASSERT_TRUE(pub.start().HasValue());

    std::atomic<int> received{0};
    SocketEventSubscriber<lap::com::example::EchoResponse> sub(
        socketPath_,
        [&received](const lap::com::example::EchoResponse&){ ++received; }
    );
    ASSERT_TRUE(sub.start().HasValue());

    lap::com::example::EchoResponse evt; evt.add_messages("bench"); evt.set_message_count(0);
    const int total = 10000;
    
    auto start = std::chrono::steady_clock::now();
    for (int i=0;i<total;++i) pub.publish(evt);
    auto end = std::chrono::steady_clock::now();

    auto sendDuration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    double sendQPS = (total * 1000000.0) / sendDuration;

    // Wait for all to arrive
    for (int i=0;i<100 && received.load()<total;++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    auto recvEnd = std::chrono::steady_clock::now();
    auto totalDuration = std::chrono::duration_cast<std::chrono::microseconds>(recvEnd - start).count();
    double e2eQPS = (received.load() * 1000000.0) / totalDuration;

    std::cout << "\n=== Event QPS Benchmark ===\n";
    std::cout << "Total events: " << total << "\n";
    std::cout << "Received: " << received.load() << "\n";
    std::cout << "Send time: " << sendDuration << " us\n";
    std::cout << "Send QPS: " << static_cast<int>(sendQPS) << " msg/s\n";
    std::cout << "E2E time: " << totalDuration << " us\n";
    std::cout << "E2E QPS: " << static_cast<int>(e2eQPS) << " msg/s\n";
    std::cout << "Avg latency: " << (received.load() > 0 ? totalDuration / received.load() : 0) << " us/msg\n";
    std::cout << "===========================\n\n";

    sub.stop(); pub.stop();
}

TEST_F(SocketEventTest, BenchmarkLatency) {
    SocketEventPublisher<lap::com::example::EchoResponse> pub(socketPath_);
    ASSERT_TRUE(pub.start().HasValue());

    std::vector<int64_t> latencies;
    std::mutex latMutex;

    SocketEventSubscriber<lap::com::example::EchoResponse> sub(
        socketPath_,
        [&latencies, &latMutex](const lap::com::example::EchoResponse& evt){
            auto recvTime = std::chrono::steady_clock::now().time_since_epoch().count();
            int64_t sendTime = 0;
            if (evt.messages_size()>0) {
                sendTime = std::atoll(evt.messages(0).c_str());
            }
            latencies.push_back(recvTime - sendTime);
        }
    );
    ASSERT_TRUE(sub.start().HasValue());

    lap::com::example::EchoResponse evt; evt.add_messages("0");
    const int samples = 1000;
    for (int i=0;i<samples;++i) {
        auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
        evt.mutable_messages()->Clear();
        evt.add_messages(std::to_string(ts));
        evt.set_message_count(0);
        pub.publish(evt);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::lock_guard<std::mutex> lock(latMutex);
    if (latencies.size() > 10) {
        std::sort(latencies.begin(), latencies.end());
        int64_t p50 = latencies[latencies.size()/2];
        int64_t p90 = latencies[static_cast<size_t>(latencies.size()*0.9)];
        int64_t p99 = latencies[static_cast<size_t>(latencies.size()*0.99)];
        int64_t sum = 0; for (auto l:latencies) sum+=l;
        int64_t avg = sum / static_cast<int64_t>(latencies.size());

        std::cout << "\n=== Event Latency Benchmark ===\n";
        std::cout << "Samples: " << latencies.size() << "/" << samples << "\n";
        std::cout << "Avg latency: " << avg << " ns\n";
        std::cout << "P50 latency: " << p50 << " ns\n";
        std::cout << "P90 latency: " << p90 << " ns\n";
        std::cout << "P99 latency: " << p99 << " ns\n";
        std::cout << "===============================\n\n";
    }

    sub.stop(); pub.stop();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
