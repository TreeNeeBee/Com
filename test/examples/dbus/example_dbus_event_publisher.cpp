/**
 * @file        example_dbus_event_publisher.cpp
 * @brief       D-Bus Event Publisher Example - Skeleton Side
 * @date        2025-10-30
 * @details     Demonstrates how to publish events via D-Bus
 */

#include "../../../source/binding/dbus/DBusConnectionManager.hpp"
#include "../../../source/binding/dbus/DBusEventBinding.hpp"

#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>

// 示例事件数据
struct RadarObject
{
    float distance;
    float angle;
    uint32_t objectId;
    uint64_t timestamp;
    
    RadarObject() : distance(0), angle(0), objectId(0), timestamp(0) {}
    RadarObject(float d, float a, uint32_t id)
        : distance(d), angle(a), objectId(id)
        , timestamp(std::chrono::system_clock::now().time_since_epoch().count())
    {}
};

// 为 RadarObject 提供序列化支持
namespace lap { namespace com { namespace serialization {
    template<>
    lap::core::Result<void> BinarySerializer::Serialize(const RadarObject& obj)
    {
        Serialize(obj.distance);
        Serialize(obj.angle);
        Serialize(obj.objectId);
        Serialize(obj.timestamp);
        return lap::core::Result<void>::FromValue();
    }
}}}

volatile bool g_running = true;

void SignalHandler(int signal)
{
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
}

int main()
{
    std::cout << "=== D-Bus Event Publisher Example ===" << std::endl;
    
    // 设置信号处理
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    
    try
    {
        // 1. 初始化 D-Bus 连接管理器
        auto& connMgr = lap::com::dbus::DBusConnectionManager::GetInstance();
        auto initResult = connMgr.Initialize();
        if (!initResult.HasValue())
        {
            std::cerr << "Failed to initialize D-Bus connection" << std::endl;
            return 1;
        }
        std::cout << "✓ D-Bus connection initialized" << std::endl;
        
        // 2. 请求服务名
        const char* serviceName = "com.example.RadarService";
        auto nameResult = connMgr.RequestServiceName(serviceName);
        if (!nameResult.HasValue())
        {
            std::cerr << "Failed to request service name" << std::endl;
            return 1;
        }
        std::cout << "✓ Service name requested: " << serviceName << std::endl;
        
        // 3. 创建 Event Publisher
        auto connection = connMgr.GetSessionConnection();
        lap::com::dbus::DBusEventPublisher<RadarObject> publisher(
            connection,
            "/com/example/RadarService",           // Object Path
            "com.example.RadarService.Interface",  // Interface Name
            "ObjectDetected"                        // Signal Name
        );
        std::cout << "✓ Event publisher created" << std::endl;
        
        // 4. 发布事件
        std::cout << "\nPublishing radar object detection events..." << std::endl;
        std::cout << "Press Ctrl+C to stop\n" << std::endl;
        
        uint32_t objectId = 1000;
        while (g_running)
        {
            // 模拟雷达检测对象
            float distance = 10.0f + (rand() % 100) / 10.0f;
            float angle = -45.0f + (rand() % 90);
            
            RadarObject obj(distance, angle, objectId++);
            
            auto sendResult = publisher.Send(obj);
            if (sendResult.HasValue())
            {
                std::cout << "[" << objectId - 1 << "] Object detected: "
                         << "distance=" << obj.distance << "m, "
                         << "angle=" << obj.angle << "° "
                         << "✓" << std::endl;
            }
            else
            {
                std::cerr << "Failed to send event" << std::endl;
            }
            
            // 每秒发送一次
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        
        // 5. 清理
        std::cout << "\nCleaning up..." << std::endl;
        connMgr.ReleaseServiceName(serviceName);
        connMgr.Deinitialize();
        
        std::cout << "✓ Publisher stopped successfully" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
