#include "../../../source/binding/dbus/DBusConnectionManager.hpp"
#include "../../../source/binding/dbus/DBusFieldBinding.hpp"
#include <log/CLog.hpp>
#include <csignal>
#include <thread>
#include <chrono>

// 车辆状态数据
struct VehicleSpeed {
    float currentSpeed;  // km/h
    float averageSpeed;  // km/h
    uint32_t timestamp;  // ms
};

volatile bool g_running = true;
void SignalHandler(int) { g_running = false; }

int main() {
    LAP_LOG_INFO("COM.DBUS.Example") << "=== D-Bus Field Server ===";
    std::signal(SIGINT, SignalHandler);
    
    auto& mgr = lap::com::dbus::DBusConnectionManager::GetInstance();
    mgr.Initialize();
    mgr.RequestServiceName("com.example.Vehicle");
    
    auto conn = mgr.GetSessionConnection();
    
    // 创建 Speed 属性服务器
    lap::com::dbus::DBusFieldServer<VehicleSpeed> speedField(
        conn, "/vehicle", "com.example.Vehicle", "Speed");
    
    // 当前速度值（内部存储）
    VehicleSpeed currentSpeed{0.0f, 0.0f, 0};
    
    // 注册 Getter
    speedField.RegisterGetter([&currentSpeed]() -> VehicleSpeed {
        LAP_LOG_DEBUG("COM.DBUS.Example") << "[GET] Speed requested: " << currentSpeed.currentSpeed << " km/h";
        return currentSpeed;
    });
    
    // 注册 Setter
    speedField.RegisterSetter([&currentSpeed](const VehicleSpeed& newSpeed) {
        LAP_LOG_INFO("COM.DBUS.Example") << "[SET] Speed updated: " << newSpeed.currentSpeed << " km/h";
        currentSpeed = newSpeed;
    });
    
    // 注册变化通知回调
    speedField.SetNotifyCallback([](const VehicleSpeed& speed) {
        LAP_LOG_DEBUG("COM.DBUS.Example") << "[NOTIFY] Speed changed notification sent: " 
                  << speed.currentSpeed << " km/h";
    });
    
    speedField.FinishRegistration();
    
    LAP_LOG_INFO("COM.DBUS.Example") << "Field server started (Ctrl+C to stop)...";
    LAP_LOG_INFO("COM.DBUS.Example") << "Speed will update every 2 seconds...";
    
    // 模拟速度变化
    uint32_t counter = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 更新速度
        currentSpeed.currentSpeed = 60.0f + (counter % 40);  // 60-100 km/h
        currentSpeed.averageSpeed = (currentSpeed.averageSpeed + currentSpeed.currentSpeed) / 2.0f;
        currentSpeed.timestamp = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        
        // 发送变化通知
        speedField.NotifyPropertyChanged(currentSpeed);
        
        counter++;
    }
    
    mgr.ReleaseServiceName("com.example.Vehicle");
    mgr.Deinitialize();
    
    return 0;
}
