#include "../../../source/binding/dbus/DBusConnectionManager.hpp"
#include "../../../source/binding/dbus/DBusFieldBinding.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>

// 车辆状态数据
struct VehicleSpeed {
    float currentSpeed;
    float averageSpeed;
    uint32_t timestamp;
};

volatile bool g_running = true;
void SignalHandler(int) { g_running = false; }

int main() {
    std::cout << "=== D-Bus Field Client ===" << std::endl;
    std::signal(SIGINT, SignalHandler);
    
    auto& mgr = lap::com::dbus::DBusConnectionManager::GetInstance();
    mgr.Initialize();
    
    auto conn = mgr.GetSessionConnection();
    
    // 创建 Speed 属性客户端
    lap::com::dbus::DBusFieldClient<VehicleSpeed> speedField(
        conn, "com.example.Vehicle", "/vehicle", "com.example.Vehicle", "Speed");
    
    // 订阅属性变化通知
    speedField.SubscribeNotification([](const VehicleSpeed& speed) {
        std::cout << "[NOTIFY] Speed changed: " 
                  << speed.currentSpeed << " km/h, "
                  << "avg: " << speed.averageSpeed << " km/h, "
                  << "timestamp: " << speed.timestamp << std::endl;
    });
    
    std::cout << "Subscribed to Speed property changes" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    
    // 定期读取属性值
    std::thread readThread([&speedField]() {
        int count = 0;
        while (g_running && count < 5) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            
            std::cout << "\n[GET] Reading Speed property..." << std::endl;
            auto result = speedField.Get();
            
            if (result.HasValue()) {
                auto speed = result.Value();
                std::cout << "[GET] Current speed: " << speed.currentSpeed 
                          << " km/h, avg: " << speed.averageSpeed << " km/h" << std::endl;
            } else {
                std::cout << "[GET] Failed to read Speed property" << std::endl;
            }
            
            count++;
        }
    });
    
    // 等待一段时间后尝试设置属性
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    std::cout << "\n[SET] Setting new speed value..." << std::endl;
    VehicleSpeed newSpeed{120.0f, 90.0f, 12345};
    auto setResult = speedField.Set(newSpeed);
    
    if (setResult.HasValue()) {
        std::cout << "[SET] Speed set successfully" << std::endl;
    } else {
        std::cout << "[SET] Failed to set Speed property" << std::endl;
    }
    
    // 等待事件循环
    readThread.join();
    
    speedField.UnsubscribeNotification();
    mgr.Deinitialize();
    
    return 0;
}
