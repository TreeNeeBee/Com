/**
 * @file        DBusMethodBinding.hpp
 * @author      LightAP Team
 * @brief       D-Bus Method Binding - RPC Implementation
 * @date        2024-10-30
 * @details     Binds ara::com Method calls to D-Bus Methods (RPC)
 * @copyright   Copyright (c) 2024
 * @note        AUTOSAR R22-11 compliant
 * @version     1.0
 */
#ifndef LAP_COM_DBUS_METHOD_BINDING_HPP
#define LAP_COM_DBUS_METHOD_BINDING_HPP

#include "DBusConnectionManager.hpp"
#include "../../inc/ComTypes.hpp"

#include <core/CResult.hpp>
#include <core/CString.hpp>
#include <core/CTypedef.hpp>
#include <core/CFuture.hpp>
#include <log/CLog.hpp>

#include <sdbus-c++/sdbus-c++.h>
#include <memory>
#include <functional>
#include <future>
#include <type_traits>
#include <cstring>

namespace lap
{
namespace com
{
namespace dbus
{
    /**
     * @brief 方法序列化辅助类
     */
    class MethodSerializer
    {
    public:
        // POD 类型序列化
        template<typename T>
        static typename std::enable_if<std::is_trivially_copyable<T>::value, lap::core::Vector<lap::core::UInt8>>::type
        Serialize(const T& data)
        {
            const auto* ptr = reinterpret_cast<const lap::core::UInt8*>(&data);
            return lap::core::Vector<lap::core::UInt8>(ptr, ptr + sizeof(T));
        }
        
        // POD 类型反序列化
        template<typename T>
        static typename std::enable_if<std::is_trivially_copyable<T>::value, T>::type
        Deserialize(const lap::core::Vector<lap::core::UInt8>& buffer)
        {
            T result{};
            if (buffer.size() >= sizeof(T))
            {
                std::memcpy(&result, buffer.data(), sizeof(T));
            }
            return result;
        }
        
        // void 类型特化（无返回值）
        static lap::core::Vector<lap::core::UInt8> SerializeVoid()
        {
            return {};
        }
    };
    
    /**
     * @brief D-Bus Method Server (Skeleton 端)
     * 提供 RPC 服务
     */
    class DBusMethodServer
    {
    public:
        DBusMethodServer(std::shared_ptr<sdbus::IConnection> conn,
                         lap::core::String objectPath,
                         lap::core::String interfaceName)
            : m_connection(std::move(conn))
            , m_objectPath(std::move(objectPath))
            , m_interfaceName(std::move(interfaceName))
        {
            m_object = sdbus::createObject(*m_connection, m_objectPath);
            LAP_LOG_INFO("COM.DBUS.Method") << "Server created: interface=" << m_interfaceName.c_str();
        }
        
        /**
         * @brief 注册方法处理器 (Request -> Response)
         * @tparam RequestType 请求数据类型
         * @tparam ResponseType 响应数据类型
         */
        template<typename RequestType, typename ResponseType>
        void RegisterMethod(const lap::core::String& methodName,
                            std::function<ResponseType(const RequestType&)> handler)
        {
            m_object->registerMethod(methodName)
                    .onInterface(m_interfaceName)
                    .implementedAs([handler](const lap::core::Vector<lap::core::UInt8>& requestBuffer) 
                                   -> lap::core::Vector<lap::core::UInt8> {
                        // 反序列化请求
                        RequestType request = MethodSerializer::Deserialize<RequestType>(requestBuffer);
                        
                        // 调用处理器
                        ResponseType response = handler(request);
                        
                        // 序列化响应
                        return MethodSerializer::Serialize(response);
                    });
            LAP_LOG_INFO("COM.DBUS.Method") << "Method registered: " << methodName.c_str();
        }
        
        /**
         * @brief 注册方法处理器 (Request -> void)
         */
        template<typename RequestType>
        void RegisterMethod(const lap::core::String& methodName,
                            std::function<void(const RequestType&)> handler)
        {
            m_object->registerMethod(methodName)
                    .onInterface(m_interfaceName)
                    .implementedAs([handler](const lap::core::Vector<lap::core::UInt8>& requestBuffer) 
                                   -> lap::core::Vector<lap::core::UInt8> {
                        // 反序列化请求
                        RequestType request = MethodSerializer::Deserialize<RequestType>(requestBuffer);
                        
                        // 调用处理器（无返回值）
                        handler(request);
                        
                        // 返回空响应
                        return MethodSerializer::SerializeVoid();
                    });
            LAP_LOG_INFO("COM.DBUS.Method") << "Method registered (void): " << methodName.c_str();
        }
        
        /**
         * @brief 完成注册
         */
        void FinishRegistration()
        {
            m_object->finishRegistration();
            LAP_LOG_INFO("COM.DBUS.Method") << "Server registration complete";
        }
        
    private:
        std::shared_ptr<sdbus::IConnection> m_connection;
        std::unique_ptr<sdbus::IObject> m_object;
    lap::core::String m_objectPath;
    lap::core::String m_interfaceName;
    };
    
    /**
     * @brief D-Bus Method Client (Proxy 端)
     * 调用远程 RPC 服务
     */
    class DBusMethodClient
    {
    public:
        DBusMethodClient(std::shared_ptr<sdbus::IConnection> conn,
                         lap::core::String serviceName,
                         lap::core::String objectPath,
                         lap::core::String interfaceName)
            : m_connection(std::move(conn))
            , m_serviceName(std::move(serviceName))
            , m_objectPath(std::move(objectPath))
            , m_interfaceName(std::move(interfaceName))
        {
            m_proxy = sdbus::createProxy(*m_connection, m_serviceName, m_objectPath);
            LAP_LOG_INFO("COM.DBUS.Method") << "Client created: interface=" << m_interfaceName.c_str();
        }
        
        /**
         * @brief 同步调用方法 (Request -> Response)
         * @tparam RequestType 请求数据类型
         * @tparam ResponseType 响应数据类型
         */
        template<typename RequestType, typename ResponseType>
        lap::core::Result<ResponseType> CallMethod(const lap::core::String& methodName,
                                                   const RequestType& request,
                                                   lap::core::UInt32 timeoutMs = 5000)
        {
            try
            {
                // 序列化请求
                auto requestBuffer = MethodSerializer::Serialize(request);
                
                // 调用 D-Bus 方法
                lap::core::Vector<lap::core::UInt8> responseBuffer;
                auto method = m_proxy->createMethodCall(m_interfaceName, methodName);
                method << requestBuffer;
                
                auto reply = m_proxy->callMethod(method, timeoutMs * 1000); // 转换为微秒
                reply >> responseBuffer;
                
                // 反序列化响应
                ResponseType response = MethodSerializer::Deserialize<ResponseType>(responseBuffer);
                LAP_LOG_DEBUG("COM.DBUS.Method") << "Method called OK: " << methodName.c_str();
                
                return lap::core::Result<ResponseType>::FromValue(response);
            }
            catch (const sdbus::Error& e)
            {
                LAP_LOG_ERROR("COM.DBUS.Method") << "Call failed: " << methodName.c_str() << ", error: " << e.what();
                return lap::core::Result<ResponseType>::FromError(
                    MakeErrorCode(ComErrc::kCommunicationLinkError, 0));
            }
        }
        
        /**
         * @brief 同步调用方法 (Request -> void)
         */
        template<typename RequestType>
        lap::core::Result<void> CallMethod(const lap::core::String& methodName,
                                           const RequestType& request,
                                           lap::core::UInt32 timeoutMs = 5000)
        {
            try
            {
                // 序列化请求
                auto requestBuffer = MethodSerializer::Serialize(request);
                
                // 调用 D-Bus 方法
                auto method = m_proxy->createMethodCall(m_interfaceName, methodName);
                method << requestBuffer;
                
                m_proxy->callMethod(method, timeoutMs * 1000);
                LAP_LOG_DEBUG("COM.DBUS.Method") << "Method called OK (void): " << methodName.c_str();
                
                return lap::core::Result<void>::FromValue();
            }
            catch (const sdbus::Error& e)
            {
                LAP_LOG_ERROR("COM.DBUS.Method") << "Call failed (void): " << methodName.c_str() << ", error: " << e.what();
                return lap::core::Result<void>::FromError(
                    MakeErrorCode(ComErrc::kCommunicationLinkError, 0));
            }
        }
        
        /**
         * @brief 异步调用方法 (Request -> Response)
         * @return Future 对象
         */
        template<typename RequestType, typename ResponseType>
        std::future<lap::core::Result<ResponseType>> CallMethodAsync(
            const std::string& methodName,
            const RequestType& request)
        {
            return std::async(std::launch::async, [this, methodName, request]() {
                return CallMethod<RequestType, ResponseType>(methodName, request);
            });
        }
        
    private:
        std::shared_ptr<sdbus::IConnection> m_connection;
        std::unique_ptr<sdbus::IProxy> m_proxy;
    lap::core::String m_serviceName;
    lap::core::String m_objectPath;
    lap::core::String m_interfaceName;
    };
    
} // namespace dbus
} // namespace com
} // namespace lap

#endif // LAP_COM_DBUS_METHOD_BINDING_HPP
