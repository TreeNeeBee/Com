#include "../../../source/binding/dbus/DBusConnectionManager.hpp"
#include "../../../source/binding/dbus/DBusMethodBinding.hpp"
#include <log/CLog.hpp>
#include <csignal>
#include <thread>
#include <chrono>

// 请求和响应数据结构
struct CalculateRequest {
    float operand1;
    float operand2;
    char operation; // '+', '-', '*', '/'
};

struct CalculateResponse {
    float result;
    int errorCode;
};

volatile bool g_running = true;
void SignalHandler(int) { g_running = false; }

int main() {
    LAP_LOG_INFO("COM.DBUS.Example") << "=== D-Bus Method Server ===";
    std::signal(SIGINT, SignalHandler);
    
    auto& mgr = lap::com::dbus::DBusConnectionManager::GetInstance();
    mgr.Initialize();
    mgr.RequestServiceName("com.example.Calculator");
    
    auto conn = mgr.GetSessionConnection();
    lap::com::dbus::DBusMethodServer server(
        conn, "/calculator", "com.example.Calculator");
    
    // 注册 Calculate 方法
    server.RegisterMethod<CalculateRequest, CalculateResponse>(
        "Calculate",
        [](const CalculateRequest& req) -> CalculateResponse {
            CalculateResponse resp;
            resp.errorCode = 0;
            
            switch (req.operation) {
                case '+':
                    resp.result = req.operand1 + req.operand2;
                    break;
                case '-':
                    resp.result = req.operand1 - req.operand2;
                    break;
                case '*':
                    resp.result = req.operand1 * req.operand2;
                    break;
                case '/':
                    if (req.operand2 != 0.0f) {
                        resp.result = req.operand1 / req.operand2;
                    } else {
                        resp.result = 0.0f;
                        resp.errorCode = 1; // Division by zero
                    }
                    break;
                default:
                    resp.result = 0.0f;
                    resp.errorCode = 2; // Invalid operation
                    break;
            }
            
            LAP_LOG_INFO("COM.DBUS.Example") << "Calculate: " << req.operand1 << " " << req.operation 
                      << " " << req.operand2 << " = " << resp.result;
            
            return resp;
        });
    
    server.FinishRegistration();
    
    LAP_LOG_INFO("COM.DBUS.Example") << "Method server started (Ctrl+C to stop)...";
    
    // 保持运行
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    mgr.ReleaseServiceName("com.example.Calculator");
    mgr.Deinitialize();
    
    return 0;
}
