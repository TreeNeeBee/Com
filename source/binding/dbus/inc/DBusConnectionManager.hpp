/**
 * @file        DBusConnectionManager.hpp
 * @author      ddkv587 ( ddkv587@gmail.com )
 * @brief       D-Bus Connection Manager - Singleton for managing D-Bus connections
 * @date        2025-10-30
 * @details     Manages system and session bus connections
 * @copyright   Copyright (c) 2025
 * @note        Thread-safe singleton
 * @version     1.0
 */
#ifndef LAP_COM_DBUS_CONNECTION_MANAGER_HPP
#define LAP_COM_DBUS_CONNECTION_MANAGER_HPP

#include <core/CResult.hpp>
#include <core/CString.hpp>
#include <core/CCoreErrorDomain.hpp>
#include <log/CLog.hpp>

#include <sdbus-c++/sdbus-c++.h>
#include <memory>
#include <mutex>
#include <iostream>  // deprecated: iostream will be removed (using Log module)

namespace lap
{
namespace com
{
namespace dbus
{
    /**
     * @brief D-Bus 总线类型
     */
    enum class BusType : lap::core::UInt8
    {
        System,  ///< 系统总线
        Session  ///< 会话总线
    };
    
    /**
     * @brief D-Bus 连接管理器 (单例)
     * @details 管理 D-Bus 连接的生命周期，提供连接池功能
     */
    class DBusConnectionManager
    {
    public:
        /**
         * @brief 获取单例实例
         */
        static DBusConnectionManager& GetInstance()
        {
            static DBusConnectionManager instance;
            return instance;
        }
        
        /**
         * @brief 初始化连接管理器
         */
        lap::core::Result<void> Initialize()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (m_initialized)
            {
                LAP_LOG_WARN("COM.DBUS.Conn") << "DBusConnectionManager already initialized";
                return lap::core::Result<void>::FromValue();
            }
            
            try
            {
                // 创建会话总线连接 (默认)
                m_sessionConnection = sdbus::createSessionBusConnection();
                m_sessionConnection->enterEventLoopAsync();
                
                LAP_LOG_INFO("COM.DBUS.Conn") << "D-Bus session bus connected";
                
                // 尝试创建系统总线连接 (可选)
                try
                {
                    m_systemConnection = sdbus::createSystemBusConnection();
                    m_systemConnection->enterEventLoopAsync();
                    LAP_LOG_INFO("COM.DBUS.Conn") << "D-Bus system bus connected";
                }
                catch (const sdbus::Error& e)
                {
            LAP_LOG_WARN("COM.DBUS.Conn") << "System bus connection failed (session-only): " << e.what();
                }
                
                m_initialized = true;
                return lap::core::Result<void>::FromValue();
            }
            catch (const sdbus::Error& e)
            {
                LAP_LOG_ERROR("COM.DBUS.Conn") << "Failed to initialize D-Bus connections: " << e.what();
                return lap::core::Result<void>::FromError(
                    lap::core::ErrorCode(1, lap::core::GetCoreErrorDomain()));
            }
        }
        
        /**
         * @brief 反初始化连接管理器
         */
        void Deinitialize()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (!m_initialized)
                return;
            
            // Reset connections without calling leaveEventLoop to avoid pure virtual issues
            m_sessionConnection.reset();
            m_systemConnection.reset();
            
            m_initialized = false;
            LAP_LOG_INFO("COM.DBUS.Conn") << "D-Bus connections closed";
        }
        
        /**
         * @brief 获取会话总线连接
         */
        std::shared_ptr<sdbus::IConnection> GetSessionConnection()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (!m_initialized || !m_sessionConnection)
            {
                LAP_LOG_ERROR("COM.DBUS.Conn") << "Session bus not initialized";
                return nullptr;
            }
            
            return m_sessionConnection;
        }
        
        /**
         * @brief 获取系统总线连接
         */
        std::shared_ptr<sdbus::IConnection> GetSystemConnection()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (!m_initialized || !m_systemConnection)
            {
                LAP_LOG_ERROR("COM.DBUS.Conn") << "System bus not initialized";
                return nullptr;
            }
            
            return m_systemConnection;
        }
        
        /**
         * @brief 获取指定类型的总线连接
         */
        std::shared_ptr<sdbus::IConnection> GetConnection(BusType type = BusType::Session)
        {
            return (type == BusType::System) ? GetSystemConnection() : GetSessionConnection();
        }
        
        /**
         * @brief 检查是否已初始化
         */
        bool IsInitialized() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_initialized;
        }
        
        /**
         * @brief 请求 D-Bus 服务名
         * @param serviceName 服务名 (例如: "com.example.MyService")
         * @param busType 总线类型
         */
        lap::core::Result<void> RequestServiceName(lap::core::StringView serviceName, 
                                                     BusType busType = BusType::Session)
        {
            try
            {
                auto connection = GetConnection(busType);
                if (!connection)
                {
                    return lap::core::Result<void>::FromError(
                        lap::core::ErrorCode(1, lap::core::GetCoreErrorDomain()));
                }
                
                connection->requestName(serviceName.data());
                LAP_LOG_INFO("COM.DBUS.Conn") << "Service name requested: " << serviceName;
                
                return lap::core::Result<void>::FromValue();
            }
            catch (const sdbus::Error& e)
            {
                LAP_LOG_ERROR("COM.DBUS.Conn") << "Failed to request service name: " << serviceName << ", error: " << e.what();
                return lap::core::Result<void>::FromError(
                    lap::core::ErrorCode(1, lap::core::GetCoreErrorDomain()));
            }
        }
        
        /**
         * @brief 释放 D-Bus 服务名
         */
        void ReleaseServiceName(lap::core::StringView serviceName, 
                               BusType busType = BusType::Session)
        {
            try
            {
                auto connection = GetConnection(busType);
                if (connection)
                {
                    connection->releaseName(serviceName.data());
                    LAP_LOG_INFO("COM.DBUS.Conn") << "Service name released: " << serviceName;
                }
            }
            catch (const sdbus::Error& e)
            {
                LAP_LOG_ERROR("COM.DBUS.Conn") << "Failed to release service name: " << serviceName << ", error: " << e.what();
            }
        }
        
        // 禁止拷贝和移动
        DBusConnectionManager(const DBusConnectionManager&) = delete;
        DBusConnectionManager& operator=(const DBusConnectionManager&) = delete;
        DBusConnectionManager(DBusConnectionManager&&) = delete;
        DBusConnectionManager& operator=(DBusConnectionManager&&) = delete;
        
    private:
        DBusConnectionManager() = default;
        ~DBusConnectionManager()
        {
            Deinitialize();
        }
        
        mutable std::mutex m_mutex;
        bool m_initialized{false};
        
        std::shared_ptr<sdbus::IConnection> m_sessionConnection;
        std::shared_ptr<sdbus::IConnection> m_systemConnection;
    };
    
} // namespace dbus
} // namespace com
} // namespace lap

#endif // LAP_COM_DBUS_CONNECTION_MANAGER_HPP
