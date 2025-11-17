#include "../../source/binding/dbus/DBusConnectionManager.hpp"
#include "../../source/binding/dbus/DBusMethodBinding.hpp"
#include <gtest/gtest.h>
#include <log/CLog.hpp>
#include <log/CLogManager.hpp>
#include <core/CMemory.hpp>
#include <unistd.h>
#include <chrono>

struct CalcReq { float a; float b; char op; };
struct CalcResp { float r; int ec; };

class DBusEnvGuard2 {
public:
    DBusEnvGuard2() {
    (void)lap::core::MemoryManager::getInstance();
        lap::log::LogManager::getInstance().initialize();
        (void)lap::com::dbus::DBusConnectionManager::GetInstance().Initialize();
    }
    ~DBusEnvGuard2() {
        // Let singletons clean up naturally to avoid destruction order issues
    }
};

TEST(Com_DBus_Method, RequestResponse_POD)
{
    DBusEnvGuard2 guard;
    auto& mgr = lap::com::dbus::DBusConnectionManager::GetInstance();
    auto conn = mgr.GetSessionConnection();
    ASSERT_TRUE(conn != nullptr);

    char svc[128]; snprintf(svc, sizeof(svc), "com.lightap.test.MethodTest.p%d", (int)getpid());
    ASSERT_TRUE(mgr.RequestServiceName(svc).HasValue());

    {
        lap::com::dbus::DBusMethodServer server(conn, "/ut/method", "com.lightap.calc");

        server.RegisterMethod<CalcReq, CalcResp>("Do",
            [](const CalcReq& in)->CalcResp {
                CalcResp o{0.0f, 0};
                switch(in.op){
                    case '+': o.r = in.a + in.b; break;
                    case '-': o.r = in.a - in.b; break;
                    case '*': o.r = in.a * in.b; break;
                    case '/': if(in.b!=0) o.r = in.a/in.b; else {o.ec=1;} break;
                    default: o.ec=2; break;
                }
                return o;
            });
        server.FinishRegistration();

        lap::com::dbus::DBusMethodClient client(conn, svc, "/ut/method", "com.lightap.calc");

        CalcReq req{10.0f, 5.0f, '+'};
        auto res = client.CallMethod<CalcReq, CalcResp>("Do", req, 1000);
        ASSERT_TRUE(res.HasValue());
        EXPECT_EQ(res.Value().ec, 0);
        EXPECT_FLOAT_EQ(res.Value().r, 15.0f);

        // Error path (divide by zero)
        CalcReq req2{1.0f, 0.0f, '/'};
        auto res2 = client.CallMethod<CalcReq, CalcResp>("Do", req2, 1000);
        ASSERT_TRUE(res2.HasValue());
        EXPECT_EQ(res2.Value().ec, 1);
    }

    mgr.ReleaseServiceName(svc);
}
