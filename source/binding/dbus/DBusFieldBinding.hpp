/**
 * @file        DBusFieldBinding.hpp
 * @author      LightAP Team
 * @brief       D-Bus Field Binding - Property Access Implementation
 * @date        2024-10-30
 * @details     Binds ara::com Field to D-Bus Properties (Get/Set/Notify)
 * @copyright   Copyright (c) 2024
 * @note        AUTOSAR R22-11 compliant
 * @version     1.0
 */
#ifndef LAP_COM_DBUS_FIELD_BINDING_HPP
#define LAP_COM_DBUS_FIELD_BINDING_HPP

#include "DBusConnectionManager.hpp"
#include "../../inc/ComTypes.hpp"

#include <core/CResult.hpp>
#include <core/CString.hpp>
#include <core/CTypedef.hpp>
#include <log/CLog.hpp>

#include <sdbus-c++/sdbus-c++.h>
#include <memory>
#include <functional>
#include <mutex>
#include <type_traits>
#include <cstring>

namespace lap
{
namespace com
{
namespace dbus
{
    /**
     * @brief 属性序列化辅助类
     */
    class PropertySerializer
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
    };
    
    /**
     * @brief D-Bus Field Server (Skeleton 端)
     * 提供属性访问服务
     */
    template<typename FieldType>
    class DBusFieldServer
    {
    public:
        using GetterCallback = std::function<FieldType()>;
        using SetterCallback = std::function<void(const FieldType&)>;
        using NotifyCallback = std::function<void(const FieldType&)>;
        
        DBusFieldServer(std::shared_ptr<sdbus::IConnection> conn,
                        lap::core::String objectPath,
                        lap::core::String interfaceName,
                        lap::core::String propertyName)
            : m_connection(std::move(conn))
            , m_objectPath(std::move(objectPath))
            , m_interfaceName(std::move(interfaceName))
            , m_propertyName(std::move(propertyName))
        {
            m_object = sdbus::createObject(*m_connection, m_objectPath);
            LAP_LOG_INFO("COM.DBUS.Field") << "Server created: property=" << m_propertyName.c_str();
        }
        
        /**
         * @brief 注册 Getter (读取属性)
         */
        void RegisterGetter(GetterCallback getter)
        {
            m_getter = getter;
            
            m_object->registerProperty(m_propertyName)
                    .onInterface(m_interfaceName)
                    .withGetter([this]() -> std::vector<uint8_t> {
                        FieldType value = m_getter();
                        return PropertySerializer::Serialize(value);
                    });
            
            LAP_LOG_INFO("COM.DBUS.Field") << "Getter registered: " << m_propertyName.c_str();
        }
        
        /**
         * @brief 注册 Setter (写入属性)
         */
        void RegisterSetter(SetterCallback setter)
        {
            m_setter = setter;
            
            m_object->registerProperty(m_propertyName)
                    .onInterface(m_interfaceName)
                    .withSetter([this](const std::vector<uint8_t>& buffer) {
                        FieldType value = PropertySerializer::Deserialize<FieldType>(buffer);
                        m_setter(value);
                        
                        // 更新缓存值
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_currentValue = value;
                        
                        // 触发通知回调
                        if (m_notifyCallback)
                        {
                            m_notifyCallback(value);
                        }
                    });
            
            LAP_LOG_INFO("COM.DBUS.Field") << "Setter registered: " << m_propertyName.c_str();
        }
        
        /**
         * @brief 注册 Getter 和 Setter
         */
        void RegisterGetterSetter(GetterCallback getter, SetterCallback setter)
        {
            m_getter = getter;
            m_setter = setter;
            
            m_object->registerProperty(m_propertyName)
                    .onInterface(m_interfaceName)
                    .withGetter([this]() -> std::vector<uint8_t> {
                        FieldType value = m_getter();
                        return PropertySerializer::Serialize(value);
                    })
                    .withSetter([this](const std::vector<uint8_t>& buffer) {
                        FieldType value = PropertySerializer::Deserialize<FieldType>(buffer);
                        m_setter(value);
                        
                        // 更新缓存值
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_currentValue = value;
                        
                        // 触发通知回调
                        if (m_notifyCallback)
                        {
                            m_notifyCallback(value);
                        }
                    });
            
            LAP_LOG_INFO("COM.DBUS.Field") << "Getter/Setter registered: " << m_propertyName.c_str();
        }
        
        /**
         * @brief 设置属性变化通知回调
         */
        void SetNotifyCallback(NotifyCallback callback)
        {
            m_notifyCallback = callback;
        }
        
        /**
         * @brief 发送属性变化通知
         */
        void NotifyPropertyChanged(const FieldType& newValue) noexcept
        {
            try
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_currentValue = newValue;
                
                // 发送 D-Bus PropertiesChanged 信号
                auto signal = m_object->createSignal(
                    "org.freedesktop.DBus.Properties", "PropertiesChanged");
                
                signal << m_interfaceName;
                
                std::map<std::string, sdbus::Variant> changedProps;
                auto buffer = PropertySerializer::Serialize(newValue);
                changedProps[m_propertyName] = sdbus::Variant(buffer);
                
                signal << changedProps;
                signal << std::vector<std::string>{}; // invalidated properties
                
                m_object->emitSignal(signal);
                
                LAP_LOG_DEBUG("COM.DBUS.Field") << "Notify sent: " << m_propertyName.c_str();
            }
            catch (const sdbus::Error& e)
            {
                LAP_LOG_ERROR("COM.DBUS.Field") << "Notify failed: " << e.what();
            }
        }
        
        /**
         * @brief 完成注册
         */
        void FinishRegistration()
        {
            m_object->finishRegistration();
            LAP_LOG_INFO("COM.DBUS.Field") << "Server registration complete: " << m_propertyName.c_str();
        }
        
    private:
        std::shared_ptr<sdbus::IConnection> m_connection;
        std::unique_ptr<sdbus::IObject> m_object;
    lap::core::String m_objectPath;
    lap::core::String m_interfaceName;
    lap::core::String m_propertyName;
        
        GetterCallback m_getter;
        SetterCallback m_setter;
        NotifyCallback m_notifyCallback;
        
        mutable std::mutex m_mutex;
        FieldType m_currentValue{};
    };
    
    /**
     * @brief D-Bus Field Client (Proxy 端)
     * 访问远程属性
     */
    template<typename FieldType>
    class DBusFieldClient
    {
    public:
        using NotifyCallback = std::function<void(const FieldType&)>;
        
        DBusFieldClient(std::shared_ptr<sdbus::IConnection> conn,
                        lap::core::String serviceName,
                        lap::core::String objectPath,
                        lap::core::String interfaceName,
                        lap::core::String propertyName)
            : m_connection(std::move(conn))
            , m_serviceName(std::move(serviceName))
            , m_objectPath(std::move(objectPath))
            , m_interfaceName(std::move(interfaceName))
            , m_propertyName(std::move(propertyName))
        {
            m_proxy = sdbus::createProxy(*m_connection, m_serviceName, m_objectPath);
            LAP_LOG_INFO("COM.DBUS.Field") << "Client created: property=" << m_propertyName.c_str();
        }
        
        /**
         * @brief 获取属性值 (同步)
         */
        lap::core::Result<FieldType> Get()
        {
            try
            {
                // 调用 D-Bus GetProperty
                auto method = m_proxy->createMethodCall(
                    "org.freedesktop.DBus.Properties", "Get");
                
                method << m_interfaceName << m_propertyName;
                
                auto reply = m_proxy->callMethod(method);
                
                sdbus::Variant variant;
                reply >> variant;
                
                lap::core::Vector<lap::core::UInt8> buffer = variant.get<std::vector<uint8_t>>();
                FieldType value = PropertySerializer::Deserialize<FieldType>(buffer);
                LAP_LOG_DEBUG("COM.DBUS.Field") << "Get OK: " << m_propertyName.c_str();
                
                return lap::core::Result<FieldType>::FromValue(value);
            }
            catch (const sdbus::Error& e)
            {
                LAP_LOG_ERROR("COM.DBUS.Field") << "Get failed: " << m_propertyName.c_str() << ", error: " << e.what();
                return lap::core::Result<FieldType>::FromError(
                    MakeErrorCode(ComErrc::kCommunicationLinkError, 0));
            }
        }
        
        /**
         * @brief 设置属性值 (同步)
         */
        lap::core::Result<void> Set(const FieldType& value)
        {
            try
            {
                // 序列化值
                auto buffer = PropertySerializer::Serialize(value);
                
                // 调用 D-Bus SetProperty
                auto method = m_proxy->createMethodCall(
                    "org.freedesktop.DBus.Properties", "Set");
                
                method << m_interfaceName << m_propertyName << sdbus::Variant(buffer);
                
                m_proxy->callMethod(method);
                LAP_LOG_DEBUG("COM.DBUS.Field") << "Set OK: " << m_propertyName.c_str();
                
                return lap::core::Result<void>::FromValue();
            }
            catch (const sdbus::Error& e)
            {
                LAP_LOG_ERROR("COM.DBUS.Field") << "Set failed: " << m_propertyName.c_str() << ", error: " << e.what();
                return lap::core::Result<void>::FromError(
                    MakeErrorCode(ComErrc::kCommunicationLinkError, 0));
            }
        }
        
        /**
         * @brief 订阅属性变化通知
         */
        void SubscribeNotification(NotifyCallback callback)
        {
            m_notifyCallback = callback;
            
            m_proxy->uponSignal("PropertiesChanged")
                   .onInterface("org.freedesktop.DBus.Properties")
                   .call([this](const std::string& interfaceName,
                               const std::map<std::string, sdbus::Variant>& changedProps,
                               const std::vector<std::string>& /*invalidated*/) {
                       if (interfaceName == m_interfaceName && 
                           changedProps.count(m_propertyName) > 0)
                       {
                           lap::core::Vector<lap::core::UInt8> buffer = changedProps.at(m_propertyName)
                                        .get<std::vector<uint8_t>>();
                           FieldType value = PropertySerializer::Deserialize<FieldType>(buffer);
                           
                           if (m_notifyCallback)
                           {
                               m_notifyCallback(value);
                           }
                       }
                   });
            
            m_proxy->finishRegistration();
            LAP_LOG_INFO("COM.DBUS.Field") << "Subscribed to changes: " << m_propertyName.c_str();
        }
        
        /**
         * @brief 取消订阅
         */
        void UnsubscribeNotification() noexcept
        {
            try {
                m_proxy->unregister();
                m_notifyCallback = nullptr;
                LAP_LOG_INFO("COM.DBUS.Field") << "Unsubscribed from changes: " << m_propertyName.c_str();
            } catch (const sdbus::Error& e) {
                LAP_LOG_WARN("COM.DBUS.Field") << "Unsubscribe error: " << e.what();
            }
        }
        
    private:
        std::shared_ptr<sdbus::IConnection> m_connection;
        std::unique_ptr<sdbus::IProxy> m_proxy;
    lap::core::String m_serviceName;
    lap::core::String m_objectPath;
    lap::core::String m_interfaceName;
    lap::core::String m_propertyName;
        NotifyCallback m_notifyCallback;
    };
    
} // namespace dbus
} // namespace com
} // namespace lap

#endif // LAP_COM_DBUS_FIELD_BINDING_HPP
