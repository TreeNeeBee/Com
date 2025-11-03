/**
 * @file    field_client.cpp
 * @brief   Example: Field client using SocketFieldBinding (ValueInt)
 */

#include <binding/socket/SocketFieldBinding.hpp>
#include "../../tools/protobuf/generated/field.pb.h"

#include <iostream>
#include <thread>
#include <chrono>

using namespace lap::com::binding::socket;

int main() {
    const std::string socketPath = "/tmp/socket_field_demo.sock";
    SocketFieldClient<lap::com::example::ValueInt> client(socketPath);
    auto start = client.start();
    if (!start.HasValue()) {
        std::cerr << "Failed to connect to field server\n";
        return 1;
    }

    // Get current value
    auto g = client.get();
    if (g.HasValue()) {
        std::cout << "GET value=" << g.Value().value() << "\n";
    } else {
        std::cerr << "GET failed\n";
    }

    // Set new value
    lap::com::example::ValueInt v; v.set_value(42);
    auto s = client.set(v);
    if (s.HasValue()) {
        std::cout << "SET value=42 OK\n";
    } else {
        std::cerr << "SET failed\n";
    }

    // Subscribe to updates for a few seconds
    size_t received = 0;
    client.subscribe([&received](const lap::com::example::ValueInt& nv) {
        ++received;
        std::cout << "UPDATE value=" << nv.value() << "\n";
    });

    std::this_thread::sleep_for(std::chrono::seconds(5));
    client.unsubscribe();

    std::cout << "Received " << received << " updates.\n";
    return 0;
}
