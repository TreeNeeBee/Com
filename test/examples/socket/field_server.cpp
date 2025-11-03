/**
 * @file    field_server.cpp
 * @brief   Example: Field server using SocketFieldBinding (ValueInt)
 */

#include <binding/socket/SocketFieldBinding.hpp>
#include "../../tools/protobuf/generated/field.pb.h"

#include <csignal>
#include <chrono>
#include <thread>
#include <iostream>

using namespace lap::com::binding::socket;

static std::atomic<bool> running{true};
void handleSignal(int) { running.store(false); }

int main() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    const std::string socketPath = "/tmp/socket_field_demo.sock";

    lap::com::example::ValueInt init; init.set_value(0);
    SocketFieldServer<lap::com::example::ValueInt> server(socketPath, init);
    auto res = server.start();
    if (!res.HasValue()) {
        std::cerr << "Failed to start field server\n";
        return 1;
    }

    std::cout << "Field server started at " << socketPath << ", initial value=0\n";
    int tick = 1;
    while (running.load()) {
        // Periodically update value locally (broadcast to subscribers)
        lap::com::example::ValueInt v; v.set_value(tick++);
        server.setLocal(v);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    server.stop();
    std::cout << "Field server stopped\n";
    return 0;
}
