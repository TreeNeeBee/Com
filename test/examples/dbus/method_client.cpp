#include "../../../source/binding/dbus/DBusConnectionManager.hpp"
#include "../../../source/binding/dbus/DBusMethodBinding.hpp"
#include <log/CLog.hpp>

// 请求和响应数据结构
struct CalculateRequest {
    float operand1;
    float operand2;
    char operation;
};

struct CalculateResponse {
    float result;
    int errorCode;
};

int main() {
    LAP_LOG_INFO("COM.DBUS.Example") << "=== D-Bus Method Client ===";
    
    auto& mgr = lap::com::dbus::DBusConnectionManager::GetInstance();
    mgr.Initialize();
    
    auto conn = mgr.GetSessionConnection();
    lap::com::dbus::DBusMethodClient client(
        conn, "com.example.Calculator", "/calculator", "com.example.Calculator");
    
    // 测试多种运算
    struct TestCase {
        float a, b;
        char op;
    };
    
    TestCase tests[] = {
        {10.0f, 5.0f, '+'},
        {10.0f, 5.0f, '-'},
        {10.0f, 5.0f, '*'},
        {10.0f, 5.0f, '/'},
        {10.0f, 0.0f, '/'},  // 除零测试
    };
    
    for (const auto& test : tests) {
        CalculateRequest req;
        req.operand1 = test.a;
        req.operand2 = test.b;
        req.operation = test.op;
        
        LAP_LOG_INFO("COM.DBUS.Example") << "Calling: " << test.a << " " << test.op << " " << test.b;
        
        auto result = client.CallMethod<CalculateRequest, CalculateResponse>(
            "Calculate", req, 1000);
        
        if (result.HasValue()) {
            auto resp = result.Value();
            if (resp.errorCode == 0) {
                LAP_LOG_INFO("COM.DBUS.Example") << "Result: " << resp.result;
            } else {
                LAP_LOG_WARN("COM.DBUS.Example") << "Error code: " << resp.errorCode;
            }
        } else {
            LAP_LOG_ERROR("COM.DBUS.Example") << "Method call failed!";
        }
    }
    
    // 测试异步调用
    LAP_LOG_INFO("COM.DBUS.Example") << "=== Testing Async Call ===";
    CalculateRequest asyncReq{100.0f, 7.0f, '*'};
    auto future = client.CallMethodAsync<CalculateRequest, CalculateResponse>(
        "Calculate", asyncReq);
    
    LAP_LOG_INFO("COM.DBUS.Example") << "Async call initiated, waiting for result...";
    auto asyncResult = future.get();
    
    if (asyncResult.HasValue()) {
        LAP_LOG_INFO("COM.DBUS.Example") << "Async result: " << asyncResult.Value().result;
    }
    
    mgr.Deinitialize();
    
    return 0;
}
