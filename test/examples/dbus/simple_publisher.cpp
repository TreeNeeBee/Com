#include "../../../source/binding/dbus/DBusConnectionManager.hpp"
#include "../../../source/binding/dbus/DBusEventBinding.hpp"
#include <log/CLog.hpp>
#include <thread>
#include <chrono>
#include <csignal>

struct RadarData {
    float distance;
    float angle;
    uint32_t id;
};

volatile bool g_running = true;
void SignalHandler(int) { g_running = false; }

int main() {
    LAP_LOG_INFO("COM.DBUS.Example") << "=== D-Bus Simple Publisher ===";
    std::signal(SIGINT, SignalHandler);
    
    auto& mgr = lap::com::dbus::DBusConnectionManager::GetInstance();
    mgr.Initialize();
    mgr.RequestServiceName("com.example.Radar");
    
    auto conn = mgr.GetSessionConnection();
    lap::com::dbus::DBusEventPublisher<RadarData> pub(
        conn, "/radar", "com.example.Radar", "ObjectDetected");
    
    uint32_t id = 1;
    while (g_running) {
        RadarData data{10.0f + id % 50, 45.0f, id++};
        (void)pub.Send(data);
        LAP_LOG_INFO("COM.DBUS.Example") << "Sent id=" << data.id;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    mgr.Deinitialize();
    return 0;
}
