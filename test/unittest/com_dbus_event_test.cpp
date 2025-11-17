#include "../../source/binding/dbus/DBusConnectionManager.hpp"
#include "../../source/binding/dbus/DBusEventBinding.hpp"
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

namespace {
struct PodData {
    int32_t x;
    float y;
    uint32_t id;
};

class DBusEnvGuard {
public:
    DBusEnvGuard() {
        // Initialize in order: MemoryManager -> LogManager -> DBusConnectionManager
    (void)lap::core::MemoryManager::getInstance();
        lap::log::LogManager::getInstance().initialize();
        
        auto& mgr = lap::com::dbus::DBusConnectionManager::GetInstance();
        (void)mgr.Initialize();
    }
    ~DBusEnvGuard() {
        // Let singletons clean up naturally to avoid destruction order issues
    }
};
}

TEST(Com_DBus_Event, PublishSubscribe_POD)
{
    DBusEnvGuard guard;

    auto& mgr = lap::com::dbus::DBusConnectionManager::GetInstance();
    auto conn = mgr.GetSessionConnection();
    ASSERT_TRUE(conn != nullptr);

    // Unique service name for test isolation (D-Bus requires service names NOT end with digit)
    char svc[128];
    snprintf(svc, sizeof(svc), "com.lightap.test.EventTest.p%d", static_cast<int>(getpid()));
    ASSERT_TRUE(mgr.RequestServiceName(svc).HasValue());

    {
        // Setup publisher and subscriber in local scope so they destruct before deinitialize
        lap::com::dbus::DBusEventPublisher<PodData> pub(
            conn, "/ut/event", "com.lightap.test", "PodArrived");

        std::mutex mtx; std::condition_variable cv; bool received=false; PodData got{};

        lap::com::dbus::DBusEventSubscriber<PodData> sub(
            conn, svc, "/ut/event", "com.lightap.test", "PodArrived");

        auto r = sub.Subscribe([&](const PodData& d){
            std::lock_guard<std::mutex> lk(mtx);
            got = d; received = true; cv.notify_all();
        });
        ASSERT_TRUE(r.HasValue());

        // Send one event
        PodData send{42, 3.14f, 7};
        ASSERT_TRUE(pub.Send(send).HasValue());

        // Wait for delivery
        {
            std::unique_lock<std::mutex> lk(mtx);
            ASSERT_TRUE(cv.wait_for(lk, 2s, [&]{return received;}));
        }

        EXPECT_EQ(got.x, send.x);
        EXPECT_FLOAT_EQ(got.y, send.y);
        EXPECT_EQ(got.id, send.id);

        sub.Unsubscribe();
    }
    
    mgr.ReleaseServiceName(svc);
}
