#include "../../source/binding/dbus/DBusConnectionManager.hpp"
#include "../../source/binding/dbus/DBusFieldBinding.hpp"
#include <gtest/gtest.h>
#include <log/CLog.hpp>
#include <log/CLogManager.hpp>
#include <core/CMemory.hpp>
#include <unistd.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <chrono>

using namespace std::chrono_literals;

struct Speed { float cur; float avg; uint32_t ts; };

class DBusEnvGuard3 {
public:
    DBusEnvGuard3() {
    (void)lap::core::MemoryManager::getInstance();
        lap::log::LogManager::getInstance().initialize();
        (void)lap::com::dbus::DBusConnectionManager::GetInstance().Initialize();
    }
    ~DBusEnvGuard3() {
        // Let singletons clean up naturally to avoid destruction order issues
    }
};

TEST(Com_DBus_Field, GetSetAndNotify_POD)
{
    DBusEnvGuard3 guard;
    auto& mgr = lap::com::dbus::DBusConnectionManager::GetInstance();
    auto conn = mgr.GetSessionConnection();
    ASSERT_TRUE(conn != nullptr);

    char svc[128]; snprintf(svc, sizeof(svc), "com.lightap.test.FieldTest.p%d", (int)getpid());
    ASSERT_TRUE(mgr.RequestServiceName(svc).HasValue());

    {
        lap::com::dbus::DBusFieldServer<Speed> server(conn, "/ut/field", "com.lightap.vehicle", "Speed");

        Speed value{0.0f, 0.0f, 0};
        server.RegisterGetterSetter(
            [&](){ return value; },
            [&](const Speed& v){ value=v; }
        );

        std::mutex mtx; std::condition_variable cv; bool notified=false; Speed noted{};
        server.SetNotifyCallback([&](const Speed& v){ std::lock_guard<std::mutex> lk(mtx); noted=v; notified=true; cv.notify_all(); });

        server.FinishRegistration();

        lap::com::dbus::DBusFieldClient<Speed> client(conn, svc, "/ut/field", "com.lightap.vehicle", "Speed");

        // Set then Get
        Speed set{88.0f, 77.0f, 123};
        ASSERT_TRUE(client.Set(set).HasValue());

        auto got = client.Get();
        ASSERT_TRUE(got.HasValue());
        EXPECT_FLOAT_EQ(got.Value().cur, set.cur);
        EXPECT_FLOAT_EQ(got.Value().avg, set.avg);
        EXPECT_EQ(got.Value().ts, set.ts);

        // Subscribe to notifications
        std::mutex mtx2; std::condition_variable cv2; bool received=false; Speed rxData{};
        client.SubscribeNotification([&](const Speed& s){
            std::lock_guard<std::mutex> lk(mtx2); rxData=s; received=true; cv2.notify_all();
        });

        // Notify and wait
        server.NotifyPropertyChanged(Speed{99.0f, 80.0f, 456});
        {
            std::unique_lock<std::mutex> lk(mtx2);
            ASSERT_TRUE(cv2.wait_for(lk, 2s, [&]{return received;}));
        }

        EXPECT_FLOAT_EQ(rxData.cur, 99.0f);
        EXPECT_FLOAT_EQ(rxData.avg, 80.0f);
        EXPECT_EQ(rxData.ts, 456u);

        client.UnsubscribeNotification();
    }
    
    mgr.ReleaseServiceName(svc);
}
