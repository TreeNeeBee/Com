/**
 * @file        Runtime.cpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       AUTOSAR Adaptive Platform Communication Management Runtime Implementation
 * @date        2025-10-30
 * @details     Service discovery and lifecycle management implementation
 */
#include "../../inc/Runtime.hpp"
#include "../../inc/ServiceDiscovery.hpp"

#include <mutex>
#include <unordered_map>

namespace lap
{
namespace com
{
    // Static instance
    static Runtime* g_runtimeInstance = nullptr;
    static std::mutex g_runtimeMutex;

    struct DiscoveryContext {
        ServiceDiscovery discovery;
        std::unordered_map<FindServiceHandle, std::function<void()>> cancelers;
        FindServiceHandle nextHandle{1};
    };

    static DiscoveryContext& GetDiscoveryCtx()
    {
        static DiscoveryContext ctx;
        return ctx;
    }

    Result<void> Runtime::Initialize() noexcept
    {
        std::lock_guard<std::mutex> lock(g_runtimeMutex);

        if (g_runtimeInstance == nullptr)
        {
            g_runtimeInstance = new (std::nothrow) Runtime();
            if (g_runtimeInstance == nullptr)
            {
                return Result<void>::FromError(MakeErrorCode(ComErrc::kNetworkBindingFailure, 0));
            }
        }

        if (g_runtimeInstance->m_initialized)
        {
            return Result<void>::FromValue();
        }

        {
            std::lock_guard<std::mutex> instanceLock(g_runtimeInstance->m_mutex);
            g_runtimeInstance->m_initialized = true;
        }

        return Result<void>::FromValue();
    }

    Result<void> Runtime::Deinitialize() noexcept
    {
        std::lock_guard<std::mutex> lock(g_runtimeMutex);

        if (g_runtimeInstance == nullptr)
        {
            return Result<void>::FromValue();
        }

        if (!g_runtimeInstance->m_initialized)
        {
            delete g_runtimeInstance;
            g_runtimeInstance = nullptr;
            return Result<void>::FromValue();
        }

        {
            std::lock_guard<std::mutex> instanceLock(g_runtimeInstance->m_mutex);
            g_runtimeInstance->m_initialized = false;
            g_runtimeInstance->m_serviceRegistry.clear();
        }

        delete g_runtimeInstance;
        g_runtimeInstance = nullptr;

        return Result<void>::FromValue();
    }

    Runtime& Runtime::GetInstance() noexcept
    {
        std::lock_guard<std::mutex> lock(g_runtimeMutex);

        if (g_runtimeInstance == nullptr)
        {
            g_runtimeInstance = new Runtime();
        }

        return *g_runtimeInstance;
    }

    bool Runtime::IsInitialized() noexcept
    {
        std::lock_guard<std::mutex> lock(g_runtimeMutex);

        if (g_runtimeInstance == nullptr)
        {
            return false;
        }

        return g_runtimeInstance->m_initialized;
    }

    void StopFindService(FindServiceHandle handle) noexcept
    {
        auto& ctx = GetDiscoveryCtx();
        auto it = ctx.cancelers.find(handle);
        if (it != ctx.cancelers.end())
        {
            // Invoke canceler placeholder
            it->second();
            ctx.cancelers.erase(it);
        }
    }

} // namespace com
} // namespace lap
