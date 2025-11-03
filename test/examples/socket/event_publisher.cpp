/**
 * @file    event_publisher.cpp
 * @brief   Example: Event publisher using SocketEventBinding
 */

#include <binding/socket/SocketEventBinding.hpp>
#include "../../tools/protobuf/generated/calculator.pb.h"

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

    const std::string socketPath = "/tmp/socket_event_demo.sock";
    SocketEventPublisher<lap::com::example::EchoResponse> publisher(socketPath);
    auto res = publisher.start();
    if (!res.HasValue()) {
        std::cerr << "Failed to start event publisher\n";
        return 1;
    }

    int counter = 0;
    std::cout << "Event publisher started at " << socketPath << "\n";
    while (running.load()) {
        lap::com::example::EchoResponse evt;
            evt.add_messages("tick #" + std::to_string(counter));
            evt.set_message_count(counter++);
        publisher.publish(evt);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    publisher.stop();
    std::cout << "Event publisher stopped\n";
    return 0;
}
