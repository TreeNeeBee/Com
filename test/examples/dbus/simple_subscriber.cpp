#include "../../../source/binding/dbus/DBusConnectionManager.hpp"
#include "../../../source/binding/dbus/DBusEventBinding.hpp"
#include <log/CLog.hpp>
#include <csignal>
#include <thread>
#include <chrono>

struct RadarData {
    float distance;
    float angle;
    uint32_t id;
};

volatile bool g_running = true;
void SignalHandler(int) { g_running = false; }

int main() {
    LAP_LOG_INFO("COM.DBUS.Example") << "=== D-Bus Simple Subscriber ===";
    std::signal(SIGINT, SignalHandler);
    
    auto& mgr = lap::com::dbus::DBusConnectionManager::GetInstance();
    mgr.Initialize();
    
    auto conn = mgr.GetSessionConnection();
    lap::com::dbus::DBusEventSubscriber<RadarData> sub(
        conn, "com.example.Radar", "/radar", "com.example.Radar", "ObjectDetected");
    
    sub.Subscribe([](const RadarData& data) {
        LAP_LOG_INFO("COM.DBUS.Example") << "Received: distance=" << data.distance
                                          << ", angle=" << data.angle
                                          << ", id=" << data.id;
    });
    
    LAP_LOG_INFO("COM.DBUS.Example") << "Waiting for events (Ctrl+C to stop)...";
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    mgr.Deinitialize();
    return 0;
}
