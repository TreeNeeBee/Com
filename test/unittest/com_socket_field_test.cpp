/**
 * @file        com_socket_field_test.cpp
 * @brief       Unit tests for SocketFieldBinding (Server/Client)
 */

#include <binding/socket/SocketFieldBinding.hpp>
#include <gtest/gtest.h>

#include "../../tools/protobuf/generated/field.pb.h"

#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

using namespace lap::com::binding::socket;

class SocketFieldTest : public ::testing::Test {
protected:
    void SetUp() override {
        socketPath_ = "/tmp/test_socket_field_" + std::to_string(std::time(nullptr)) + ".sock";
    }
    void TearDown() override {
        ::unlink(socketPath_.c_str());
    }
    std::string socketPath_;
};

TEST_F(SocketFieldTest, GetSetFlow) {
    lap::com::example::ValueInt init; init.set_value(1);
    SocketFieldServer<lap::com::example::ValueInt> server(socketPath_, init);
    ASSERT_TRUE(server.start().HasValue());

    SocketFieldClient<lap::com::example::ValueInt> client(socketPath_);
    ASSERT_TRUE(client.start().HasValue());

    auto g1 = client.get(); ASSERT_TRUE(g1.HasValue()); EXPECT_EQ(g1.Value().value(), 1);

    lap::com::example::ValueInt v; v.set_value(42);
    auto s1 = client.set(v); ASSERT_TRUE(s1.HasValue());

    auto g2 = client.get(); ASSERT_TRUE(g2.HasValue()); EXPECT_EQ(g2.Value().value(), 42);

    server.stop(); client.stop();
}

TEST_F(SocketFieldTest, SubscribeReceiveUpdates) {
    lap::com::example::ValueInt init; init.set_value(0);
    SocketFieldServer<lap::com::example::ValueInt> server(socketPath_, init);
    ASSERT_TRUE(server.start().HasValue());

    SocketFieldClient<lap::com::example::ValueInt> client(socketPath_);
    ASSERT_TRUE(client.start().HasValue());

    std::atomic<int> updates{0};
    auto subRes = client.subscribe([&updates](const lap::com::example::ValueInt&){ ++updates; });
    ASSERT_TRUE(subRes.HasValue());

    // trigger a few updates
    for (int i=1;i<=5;++i){ 
        lap::com::example::ValueInt v; v.set_value(i); 
        server.setLocal(v);
    }

    for (int i=0;i<100 && updates.load()<5;++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_GE(updates.load(), 5);

    client.unsubscribe(); client.stop(); server.stop();
}

TEST_F(SocketFieldTest, MultipleSubscribersFanout) {
    lap::com::example::ValueInt init; init.set_value(10);
    SocketFieldServer<lap::com::example::ValueInt> server(socketPath_, init);
    ASSERT_TRUE(server.start().HasValue());

    const int n = 5;
    std::vector<std::unique_ptr<SocketFieldClient<lap::com::example::ValueInt>>> clients;
    std::vector<int> counts(n, 0);
    for (int i=0;i<n;++i) {
        clients.emplace_back(new SocketFieldClient<lap::com::example::ValueInt>(socketPath_));
        ASSERT_TRUE(clients.back()->start().HasValue());
        auto subRes = clients.back()->subscribe([&counts,i](const lap::com::example::ValueInt&){ counts[i]++; });
        ASSERT_TRUE(subRes.HasValue());
    }

    // subscribe() blocks until initial value ACK is received, so all are ready here

    for (int k=0;k<5;++k){ 
        lap::com::example::ValueInt v; 
        v.set_value(100+k); 
        server.setLocal(v);
    }

    // wait until each subscriber sees at least 5 updates (bounded loop)
    for (int tries=0; tries<200; ++tries) {
        bool all = true;
        for (int i=0;i<n;++i) if (counts[i] < 5) { all = false; break; }
        if (all) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    for (int i=0;i<n;++i) EXPECT_GE(counts[i], 5);

    for (auto& c: clients) { c->unsubscribe(); c->stop(); }
    server.stop();
}

TEST_F(SocketFieldTest, StressSet1000) {
    lap::com::example::ValueInt init; init.set_value(0);
    SocketFieldServer<lap::com::example::ValueInt> server(socketPath_, init);
    ASSERT_TRUE(server.start().HasValue());

    SocketFieldClient<lap::com::example::ValueInt> client(socketPath_);
    ASSERT_TRUE(client.start().HasValue());

    for (int i=1;i<=1000;++i){ lap::com::example::ValueInt v; v.set_value(i); ASSERT_TRUE(client.set(v).HasValue()); }

    auto g = client.get(); ASSERT_TRUE(g.HasValue()); EXPECT_EQ(g.Value().value(), 1000);

    client.stop(); server.stop();
}

TEST_F(SocketFieldTest, SubscribeUnsubscribeBoundary) {
    lap::com::example::ValueInt init; init.set_value(5);
    SocketFieldServer<lap::com::example::ValueInt> server(socketPath_, init);
    ASSERT_TRUE(server.start().HasValue());

    SocketFieldClient<lap::com::example::ValueInt> client(socketPath_);
    ASSERT_TRUE(client.start().HasValue());

    std::atomic<int> cnt{0};
    client.subscribe([&cnt](const lap::com::example::ValueInt&){ ++cnt; });
    {
        lap::com::example::ValueInt v; v.set_value(6); server.setLocal(v);
        lap::com::example::ValueInt v2; v2.set_value(7); server.setLocal(v2);
    }
    // subscribe() returned after initial ACK; proceed immediately
    client.unsubscribe();
    // After unsubscribe, no more increments expected
    lap::com::example::ValueInt v3; v3.set_value(8); server.setLocal(v3);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    int after = cnt.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(cnt.load(), after);

    client.stop(); server.stop();
}

TEST_F(SocketFieldTest, BenchmarkGetSetQPS) {
    lap::com::example::ValueInt init; init.set_value(0);
    SocketFieldServer<lap::com::example::ValueInt> server(socketPath_, init);
    ASSERT_TRUE(server.start().HasValue());

    SocketFieldClient<lap::com::example::ValueInt> client(socketPath_);
    ASSERT_TRUE(client.start().HasValue());

    const int total = 5000;
    lap::com::example::ValueInt v;

    // Benchmark SET
    auto startSet = std::chrono::steady_clock::now();
    for (int i=1;i<=total;++i){ v.set_value(i); client.set(v); }
    auto endSet = std::chrono::steady_clock::now();
    auto setDuration = std::chrono::duration_cast<std::chrono::microseconds>(endSet - startSet).count();
    double setQPS = (total * 1000000.0) / setDuration;

    // Benchmark GET
    auto startGet = std::chrono::steady_clock::now();
    for (int i=0;i<total;++i){ client.get(); }
    auto endGet = std::chrono::steady_clock::now();
    auto getDuration = std::chrono::duration_cast<std::chrono::microseconds>(endGet - startGet).count();
    double getQPS = (total * 1000000.0) / getDuration;

    std::cout << "\n=== Field GET/SET QPS Benchmark ===\n";
    std::cout << "SET operations: " << total << "\n";
    std::cout << "SET time: " << setDuration << " us\n";
    std::cout << "SET QPS: " << static_cast<int>(setQPS) << " ops/s\n";
    std::cout << "SET avg latency: " << setDuration/total << " us/op\n";
    std::cout << "\n";
    std::cout << "GET operations: " << total << "\n";
    std::cout << "GET time: " << getDuration << " us\n";
    std::cout << "GET QPS: " << static_cast<int>(getQPS) << " ops/s\n";
    std::cout << "GET avg latency: " << getDuration/total << " us/op\n";
    std::cout << "===================================\n\n";

    client.stop(); server.stop();
}

TEST_F(SocketFieldTest, BenchmarkSubscribeLatency) {
    lap::com::example::ValueInt init; init.set_value(0);
    SocketFieldServer<lap::com::example::ValueInt> server(socketPath_, init);
    ASSERT_TRUE(server.start().HasValue());

    SocketFieldClient<lap::com::example::ValueInt> client(socketPath_);
    ASSERT_TRUE(client.start().HasValue());

    std::vector<int64_t> latencies;
    std::mutex latMutex;

    client.subscribe([&latencies, &latMutex](const lap::com::example::ValueInt& v){
        auto recvTime = std::chrono::steady_clock::now().time_since_epoch().count();
        int64_t sendTime = static_cast<int64_t>(v.value());
        if (sendTime > 0) {
            std::lock_guard<std::mutex> lock(latMutex);
            latencies.push_back(recvTime - sendTime);
        }
    });

    // subscribe() returned after initial ACK; proceed immediately

    const int samples = 1000;
    for (int i=0;i<samples;++i) {
        lap::com::example::ValueInt v;
        v.set_value(std::chrono::steady_clock::now().time_since_epoch().count());
        server.setLocal(v);
    }

    // bounded wait for latency samples to accumulate
    for (int i=0;i<200; ++i) {
        {
            std::lock_guard<std::mutex> lock(latMutex);
            if (latencies.size() >= static_cast<size_t>(samples)) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    client.unsubscribe();

    std::lock_guard<std::mutex> lock(latMutex);
    if (latencies.size() > 10) {
        std::sort(latencies.begin(), latencies.end());
        int64_t p50 = latencies[latencies.size()/2];
        int64_t p90 = latencies[static_cast<size_t>(latencies.size()*0.9)];
        int64_t p99 = latencies[static_cast<size_t>(latencies.size()*0.99)];
        int64_t sum = 0; for (auto l:latencies) sum+=l;
        int64_t avg = sum / static_cast<int64_t>(latencies.size());

        std::cout << "\n=== Field Subscribe Latency Benchmark ===\n";
        std::cout << "Samples: " << latencies.size() << "/" << samples << "\n";
        std::cout << "Avg latency: " << avg << " ns\n";
        std::cout << "P50 latency: " << p50 << " ns\n";
        std::cout << "P90 latency: " << p90 << " ns\n";
        std::cout << "P99 latency: " << p99 << " ns\n";
        std::cout << "=========================================\n\n";
    }

    client.stop(); server.stop();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
