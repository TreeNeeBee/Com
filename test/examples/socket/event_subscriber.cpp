/**
 * @file    event_subscriber.cpp
 * @brief   Example: Event subscriber using SocketEventBinding
 */

#include <binding/socket/SocketEventBinding.hpp>
#include "../../tools/protobuf/generated/calculator.pb.h"

#include <iostream>
#include <csignal>
#include <chrono>
#include <thread>

using namespace lap::com::binding::socket;

static std::atomic<bool> running{true};

void handleSignal(int) { running.store(false); }

int main() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    const std::string socketPath = "/tmp/socket_event_demo.sock";
    size_t received = 0;

    SocketEventSubscriber<lap::com::example::EchoResponse> sub(
        socketPath,
        [&received](const lap::com::example::EchoResponse& evt) {
            ++received;
            std::string lastMsg = evt.messages_size() > 0 ? evt.messages(evt.messages_size()-1) : std::string("");
            std::cout << "Event: last_message='" << lastMsg << "' total_count=" << evt.message_count() << "\n";
        }
    );
    std::cout << "Attempting to connect to " << socketPath << "...\n";
    auto res = sub.start();
    if (!res.HasValue()) {
        std::cerr << "Failed to connect to event publisher (error code: " 
                  << res.Error().Value() << ")\n";
        return 1;
    }

    std::cout << "Subscribed to events at " << socketPath << "\n";
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (received >= 5) break; // auto-exit after a few events
    }

    sub.stop();
    std::cout << "Received " << received << " events, exiting\n";
    return 0;
}
