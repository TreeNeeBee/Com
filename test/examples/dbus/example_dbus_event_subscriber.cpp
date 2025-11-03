/**
 * @file        example_dbus_event_subscriber.cpp
 * @brief       D-Bus Event Subscriber Example - Proxy Side
 * @date        2025-10-30
 * @details     Demonstrates how to subscribe to events via D-Bus
 */

#include "../../../source/binding/dbus/DBusConnectionManager.hpp"
#include "../../../source/binding/dbus/DBusEventBinding.hpp"

#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>

// 示例事件数据 (与 Publisher 相同)
struct RadarObject
{
    float distance;
    float angle;
    uint32_t objectId;
    uint64_t timestamp;
    
    RadarObject() : distance(0), angle(0), objectId(0), timestamp(0) {}
};

// 为 RadarObject 提供反序列化支持
namespace lap { namespace com { namespace serialization {
    template<>
    lap::core::Result<void> BinaryDeserializer::Deserialize(RadarObject& obj)
    {
        Deserialize(obj.distance);
        Deserialize(obj.angle);
        Deserialize(obj.objectId);
        Deserialize(obj.timestamp);
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
    std::cout << "=== D-Bus Event Subscriber Example ===" << std::endl;
    
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
        
        // 2. 创建 Event Subscriber
        auto connection = connMgr.GetSessionConnection();
        lap::com::dbus::DBusEventSubscriber<RadarObject> subscriber(
            connection,
            "com.example.RadarService",            // Service Name
            "/com/example/RadarService",           // Object Path
            "com.example.RadarService.Interface",  // Interface Name
            "ObjectDetected"                        // Signal Name
        );
        std::cout << "✓ Event subscriber created" << std::endl;
        
        // 3. 设置接收回调
        subscriber.SetReceiveHandler(
            [](lap::com::SamplePtr<const RadarObject> sample) {
                auto now = std::chrono::system_clock::now().time_since_epoch().count();
                auto latency = (now - sample->timestamp) / 1000000; // ms
                
                std::cout << "[" << sample->objectId << "] Event received: "
                         << "distance=" << sample->distance << "m, "
                         << "angle=" << sample->angle << "°, "
                         << "latency=" << latency << "ms "
                         << "✓" << std::endl;
            });
        std::cout << "✓ Receive handler set" << std::endl;
        
        // 4. 订阅事件
        auto subResult = subscriber.Subscribe(10);
        if (!subResult.HasValue())
        {
            std::cerr << "Failed to subscribe to event" << std::endl;
            return 1;
        }
        std::cout << "✓ Subscribed to ObjectDetected signal" << std::endl;
        
        // 5. 等待事件
        std::cout << "\nListening for radar object detection events..." << std::endl;
        std::cout << "Press Ctrl+C to stop\n" << std::endl;
        
        while (g_running)
        {
            // 主循环保持运行，事件在回调中处理
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // 也可以使用轮询方式获取事件
            if (false) // 切换为 true 使用轮询模式
            {
                auto newSamples = subscriber.GetNewSamples();
                if (newSamples > 0)
                {
                    std::cout << "New samples available: " << newSamples << std::endl;
                    
                    auto sampleResult = subscriber.GetNextSample();
                    if (sampleResult.HasValue())
                    {
                        auto sample = sampleResult.Value();
                        std::cout << "Polled sample: id=" << sample->objectId << std::endl;
                    }
                }
            }
        }
        
        // 6. 清理
        std::cout << "\nCleaning up..." << std::endl;
        subscriber.Unsubscribe();
        connMgr.Deinitialize();
        
        std::cout << "✓ Subscriber stopped successfully" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
