/**
 * @file        DBusEventBinding.hpp
 * @brief       D-Bus Event Binding (publish/subscribe) - unified, no-exception API
 * @date        2025-10-30
 * @details     Clean event binding built on sdbus-c++ with minimal surface:
 *              - No exceptions at API boundary (lap::core::Result)
 *              - Core typedefs (String, Vector<UInt8>, UInt32, etc.)
 *              - Logging via Log module (no iostream)
 *
 * Minimal dependencies:
 *  - sdbus-c++ headers and library
 *  - lap/core: CTypedef, CString, CResult
 *  - lap/com: ComTypes (MakeErrorCode/ComErrc)
 *  - lap/log: CLog (logging macros)
 */

#ifndef LAP_COM_DBUS_EVENT_BINDING_HPP
#define LAP_COM_DBUS_EVENT_BINDING_HPP

#include <core/CTypedef.hpp>
#include <core/CString.hpp>
#include <core/CResult.hpp>
#include "../../inc/ComTypes.hpp"
#include <log/CLog.hpp>

#include <sdbus-c++/sdbus-c++.h>
#include <memory>
#include <functional>
#include <mutex>
#include <cstring>
#include <type_traits>

namespace lap
{
namespace com
{
namespace dbus
{
    /**
     * @brief D-Bus Signal 名称生成策略
     */
    /**
     * @brief Utility to generate signal/interface names (small helper)
     */
    class SignalNameGenerator
    {
    public:
        static lap::core::String GenerateSignalName(lap::core::StringView eventName)
        {
            lap::core::String signalName = "Event_";
            signalName.append(eventName.data(), eventName.size());
            return signalName;
        }

        static lap::core::String GenerateInterfaceName(lap::core::StringView serviceInterface)
        {
            lap::core::String interfaceName = "com.lap.service.";
            interfaceName.append(serviceInterface.data(), serviceInterface.size());
            return interfaceName;
        }
    };

    /**
     * @brief Simple, portable serializer supporting POD, std::string and std::vector<POD>.
     *        Designed to minimize project-specific dependencies.
     */
    class EventSerializer
    {
    public:
        // POD types
        template<typename T>
        static typename std::enable_if<std::is_trivially_copyable<T>::value, lap::core::Vector<lap::core::UInt8>>::type
        Serialize(const T& data)
        {
            const auto* ptr = reinterpret_cast<const lap::core::UInt8*>(&data);
            return lap::core::Vector<lap::core::UInt8>(ptr, ptr + sizeof(T));
        }

        // std::string
        static lap::core::Vector<lap::core::UInt8> Serialize(const lap::core::String& str)
        {
            lap::core::UInt32 sz = static_cast<lap::core::UInt32>(str.size());
            lap::core::Vector<lap::core::UInt8> buf(sizeof(lap::core::UInt32) + sz);
            std::memcpy(buf.data(), &sz, sizeof(lap::core::UInt32));
            if (sz)
                std::memcpy(buf.data() + sizeof(lap::core::UInt32), str.data(), sz);
            return buf;
        }

        // std::vector<T> where T is POD
        template<typename T>
        static typename std::enable_if<std::is_trivially_copyable<T>::value, lap::core::Vector<lap::core::UInt8>>::type
        Serialize(const lap::core::Vector<T>& vec)
        {
            lap::core::UInt32 cnt = static_cast<lap::core::UInt32>(vec.size());
            lap::core::Vector<lap::core::UInt8> buf(sizeof(lap::core::UInt32) + cnt * sizeof(T));
            std::memcpy(buf.data(), &cnt, sizeof(lap::core::UInt32));
            if (cnt)
                std::memcpy(buf.data() + sizeof(lap::core::UInt32), vec.data(), cnt * sizeof(T));
            return buf;
        }

        // POD deserialization
        template<typename T>
        static typename std::enable_if<std::is_trivially_copyable<T>::value, T>::type
        Deserialize(const lap::core::Vector<lap::core::UInt8>& buf)
        {
            T out{};
            if (buf.size() >= sizeof(T))
            {
                std::memcpy(&out, buf.data(), sizeof(T));
            }
            return out;
        }

        // std::string deserialization
        static lap::core::String DeserializeString(const lap::core::Vector<lap::core::UInt8>& buf)
        {
            if (buf.size() < sizeof(lap::core::UInt32))
                return {};
            lap::core::UInt32 sz{};
            std::memcpy(&sz, buf.data(), sizeof(lap::core::UInt32));
            if (buf.size() < sizeof(lap::core::UInt32) + sz)
                return {};
            return lap::core::String(reinterpret_cast<const char*>(buf.data() + sizeof(lap::core::UInt32)), sz);
        }

        // std::vector<T> deserialization
        template<typename T>
        static typename std::enable_if<std::is_trivially_copyable<T>::value, lap::core::Vector<T>>::type
        DeserializeVector(const lap::core::Vector<lap::core::UInt8>& buf)
        {
            lap::core::Vector<T> out;
            if (buf.size() < sizeof(lap::core::UInt32))
                return out;
            lap::core::UInt32 cnt{};
            std::memcpy(&cnt, buf.data(), sizeof(lap::core::UInt32));
            if (buf.size() < sizeof(lap::core::UInt32) + cnt * sizeof(T))
                return out;
            out.resize(cnt);
            std::memcpy(out.data(), buf.data() + sizeof(lap::core::UInt32), cnt * sizeof(T));
            return out;
        }
    };

    /**
     * @brief D-Bus Event Publisher (Skeleton side)
     * @tparam EventDataType event payload type
     */
    template<typename EventDataType>
    class DBusEventPublisher
    {
    public:
        DBusEventPublisher(std::shared_ptr<sdbus::IConnection> conn,
                           lap::core::String objectPath,
                           lap::core::String interfaceName,
                           lap::core::String signalName)
            : m_connection(std::move(conn))
            , m_objectPath(std::move(objectPath))
            , m_interfaceName(std::move(interfaceName))
            , m_signalName(std::move(signalName))
        {
            m_object = sdbus::createObject(*m_connection, m_objectPath);
            m_object->registerSignal(m_signalName).onInterface(m_interfaceName);
            m_object->finishRegistration();
            LAP_LOG_INFO("COM.DBUS.Event") << "Publisher created: signal=" << m_signalName.c_str();
        }

        // No-exception API: return Result<void>
        lap::core::Result<void> Send(const EventDataType& data) noexcept
        {
            try {
                std::lock_guard<std::mutex> lk(m_mutex);

                // Serialize and send
                auto buffer = EventSerializer::Serialize(data);
                auto signal = m_object->createSignal(m_interfaceName, m_signalName);
                signal << buffer; // sdbus-c++ accepts std::vector<uint8_t> (alias compatible)
                m_object->emitSignal(signal);

                ++m_sentCount;
                LAP_LOG_DEBUG("COM.DBUS.Event") << "Event sent: signal=" << m_signalName.c_str()
                                                << ", size=" << buffer.size()
                                                << ", count=" << m_sentCount;
                return lap::core::Result<void>::FromValue();
            } catch (const sdbus::Error& e) {
                LAP_LOG_ERROR("COM.DBUS.Event") << "D-Bus send failed: " << e.what();
                return lap::core::Result<void>::FromError(
                    MakeErrorCode(ComErrc::kCommunicationLinkError, 0));
            }
        }

        lap::core::UInt32 GetSubscriberCount() const noexcept { return m_subscriberCount; }
        void SetSubscriberCount(lap::core::UInt32 c) noexcept { m_subscriberCount = c; }

    private:
        std::shared_ptr<sdbus::IConnection> m_connection;
        std::unique_ptr<sdbus::IObject> m_object;
        lap::core::String m_objectPath;
        lap::core::String m_interfaceName;
        lap::core::String m_signalName;
        mutable std::mutex m_mutex;
        lap::core::UInt32 m_sentCount{0};
        lap::core::UInt32 m_subscriberCount{0};
    };

    /**
     * @brief D-Bus Event Subscriber (Proxy side)
     */
    template<typename EventDataType>
    class DBusEventSubscriber
    {
    public:
        using Callback = std::function<void(const EventDataType&)>;

        DBusEventSubscriber(std::shared_ptr<sdbus::IConnection> conn,
                            lap::core::String serviceName,
                            lap::core::String objectPath,
                            lap::core::String interfaceName,
                            lap::core::String signalName)
            : m_connection(std::move(conn))
            , m_serviceName(std::move(serviceName))
            , m_objectPath(std::move(objectPath))
            , m_interfaceName(std::move(interfaceName))
            , m_signalName(std::move(signalName))
        {
            m_proxy = sdbus::createProxy(*m_connection, m_serviceName, m_objectPath);
            LAP_LOG_INFO("COM.DBUS.Event") << "Subscriber created: signal=" << m_signalName.c_str();
        }

        lap::core::Result<void> Subscribe(Callback cb) noexcept
        {
            try {
                m_callback = std::move(cb);
                m_proxy->uponSignal(m_signalName)
                       .onInterface(m_interfaceName)
                       .call([this](const lap::core::Vector<lap::core::UInt8>& buf) {
                           try {
                               EventDataType data = EventSerializer::Deserialize<EventDataType>(buf);
                               if (m_callback) m_callback(data);
                           } catch (const std::exception& e) {
                               LAP_LOG_ERROR("COM.DBUS.Event") << "Deserialize failed: " << e.what();
                           }
                       });
                m_proxy->finishRegistration();
                LAP_LOG_INFO("COM.DBUS.Event") << "Subscribed: signal=" << m_signalName.c_str();
                return lap::core::Result<void>::FromValue();
            } catch (const sdbus::Error& e) {
                LAP_LOG_ERROR("COM.DBUS.Event") << "Subscribe failed: " << e.what();
                return lap::core::Result<void>::FromError(
                    MakeErrorCode(ComErrc::kCommunicationLinkError, 0));
            }
        }

        void Unsubscribe() noexcept
        {
            try {
                m_proxy->unregister();
                m_callback = nullptr;
                LAP_LOG_INFO("COM.DBUS.Event") << "Unsubscribed: signal=" << m_signalName.c_str();
            } catch (const sdbus::Error& e) {
                LAP_LOG_WARN("COM.DBUS.Event") << "Unsubscribe error: " << e.what();
            }
        }

    private:
        std::shared_ptr<sdbus::IConnection> m_connection;
        std::unique_ptr<sdbus::IProxy> m_proxy;
        lap::core::String m_serviceName;
        lap::core::String m_objectPath;
        lap::core::String m_interfaceName;
        lap::core::String m_signalName;
        Callback m_callback;
    };

} // namespace dbus
} // namespace com
} // namespace lap

#endif // LAP_COM_DBUS_EVENT_BINDING_HPP

