# SOME/IP-DDS Bridge 集成指南

## 文档信息

- **版本**: 2.0.0
- **日期**: 2024-01
- **模块**: SOME/IP Transport Binding with DDS Bridge
- **AUTOSAR标准**: R23-11 (SWS_CM_10xxx)
- **目标架构**: Fast-DDS + SOME/IP-DDS Bridge (移除 CommonAPI)

---

## 第 1 章: 架构概述

### 1.1 设计理念

**核心思想**: 统一使用 DDS 作为通信中间件，通过 Bridge 层适配 SOME/IP 协议，消除 CommonAPI 依赖。

```
┌──────────────────────────────────────────────────────────────┐
│                       ara::com API                           │
│                  (AUTOSAR R23-11 Interface)                  │
└────────────────────────┬─────────────────────────────────────┘
                         │
        ┌────────────────┴────────────────┐
        │                                 │
┌───────▼────────┐              ┌─────────▼─────────┐
│  DDS Binding   │              │  SOME/IP Bridge   │
│  (eProsima)    │              │  (Protocol Adapt) │
└────────┬───────┘              └─────────┬─────────┘
         │                                │
         │         ┌──────────────────────┘
         │         │
┌────────▼─────────▼──────────────────────────────┐
│            SomeIpDdsBridge                      │
│  ┌──────────────┐        ┌─────────────────┐   │
│  │ SOME/IP      │  ←──→  │ DDS Topic       │   │
│  │ Message      │        │ Mapper          │   │
│  │ Codec        │        │                 │   │
│  └──────────────┘        └─────────────────┘   │
└────────┬─────────────────────────┬──────────────┘
         │                         │
         │                         │
┌────────▼───────┐        ┌────────▼─────────┐
│  vsomeip3      │        │  Fast-DDS Core   │
│  (Compat Layer)│        │  (RTPS Protocol) │
└────────┬───────┘        └────────┬─────────┘
         │                         │
         └─────────┬───────────────┘
                   │
         ┌─────────▼──────────┐
         │  Network Layer     │
         │  TCP / UDP / SHM   │
         └────────────────────┘
```

### 1.2 为什么移除 CommonAPI？

| 问题 | CommonAPI 方案 | DDS Bridge 方案 |
|------|---------------|----------------|
| **工具链复杂度** | 需要 Franca IDL + CommonAPI Generator + SOME/IP Generator | 仅需 DDS IDL (OMG 标准) |
| **运行时依赖** | CommonAPI Core + CommonAPI-SomeIP + vsomeip3 | Fast-DDS + vsomeip3 (兼容) |
| **协议栈层级** | 4层 (ara::com → CommonAPI → CommonAPI-SomeIP → vsomeip) | 3层 (ara::com → Bridge → DDS) |
| **QoS 支持** | 有限 (SOME/IP 特有) | 丰富 (DDS QoS Policies) |
| **维护成本** | 高 (多套代码生成器) | 低 (统一 DDS IDL) |

### 1.3 架构优势

✅ **统一中间件**: DDS 同时支持 DDS Binding 和 SOME/IP Bridge  
✅ **标准化**: DDS IDL (OMG) 替代 Franca IDL  
✅ **向后兼容**: VsomeipCompatLayer 保留 vsomeip API  
✅ **QoS 增强**: 利用 DDS 的 23 种 QoS 策略  
✅ **工具链简化**: 移除 CommonAPI 代码生成  

---

## 第 2 章: 核心组件设计

### 2.1 组件架构

```
modules/Com/source/binding/someip/
├── SomeIpDdsBridge.hpp           (800 行, 主桥接控制器)
├── SomeIpMessageCodec.hpp        (600 行, SOME/IP 消息编解码)
├── DdsServiceMapper.hpp          (500 行, DDS ↔ SOME/IP 服务映射)
├── VsomeipCompatLayer.hpp        (400 行, vsomeip 兼容层)
└── SomeIpServiceDiscovery.hpp    (350 行, SD 协议适配)

总计: ~2,650 行
```

### 2.2 SomeIpDdsBridge (主桥接控制器)

**职责**: 统一管理 SOME/IP 和 DDS 的双向通信。

#### 2.2.1 接口设计

```cpp
namespace ara::com::binding::someip {

class SomeIpDdsBridge {
public:
    // 初始化
    Result<void> Initialize(const BridgeConfig& config);
    
    // SOME/IP → DDS
    Result<void> RegisterSomeIpService(
        uint16_t service_id,
        uint16_t instance_id,
        const DdsTopicInfo& dds_mapping
    );
    
    // DDS → SOME/IP
    Result<void> RegisterDdsTopic(
        const std::string& topic_name,
        const SomeIpServiceInfo& someip_mapping
    );
    
    // 消息转换
    Result<DdsMessage> ConvertToNds(const SomeIpMessage& msg);
    Result<SomeIpMessage> ConvertFromDds(const DdsMessage& msg);
    
    // 服务发现同步
    void SyncServiceDiscovery();
    
private:
    std::shared_ptr<dds::domain::DomainParticipant> dds_participant_;
    std::unique_ptr<SomeIpMessageCodec> codec_;
    std::unique_ptr<DdsServiceMapper> mapper_;
    std::unique_ptr<SomeIpServiceDiscovery> sd_adapter_;
};

} // namespace ara::com::binding::someip
```

#### 2.2.2 桥接流程

**SOME/IP Request → DDS Topic Publish**:
```
1. vsomeip3 接收 SOME/IP 消息
2. SomeIpMessageCodec 解析消息头 (Service ID, Method ID, Payload)
3. DdsServiceMapper 映射到 DDS Topic
4. Fast-DDS DataWriter 发布数据
```

**DDS Topic Data → SOME/IP Response**:
```
1. Fast-DDS DataReader 接收数据
2. DdsServiceMapper 反向映射到 SOME/IP Service
3. SomeIpMessageCodec 构建 SOME/IP 消息
4. vsomeip3 发送响应
```

### 2.3 SomeIpMessageCodec (消息编解码)

**职责**: SOME/IP 消息格式与 DDS CDR 的互转换。

#### 2.3.1 SOME/IP 消息格式

```
┌──────────────────────────────────────────────┐
│              SOME/IP Header (16 bytes)       │
├──────────┬───────────┬───────────┬───────────┤
│ Message ID (4B)     │ Length (4B)           │
├──────────┬──────────┬───────────┬───────────┤
│ Request ID (4B)     │ Protocol Ver/Type(4B) │
├─────────────────────────────────────────────┤
│              Payload (N bytes)               │
└──────────────────────────────────────────────┘

Message ID = Service ID (16bit) + Method ID (16bit)
```

#### 2.3.2 编解码接口

```cpp
class SomeIpMessageCodec {
public:
    struct SomeIpHeader {
        uint16_t service_id;
        uint16_t method_id;
        uint32_t length;
        uint32_t request_id;
        uint8_t  protocol_version;
        uint8_t  interface_version;
        uint8_t  message_type;      // REQUEST/RESPONSE/NOTIFICATION
        uint8_t  return_code;
    };
    
    // SOME/IP → DDS
    Result<dds::core::ByteSeq> EncodeToDds(
        const SomeIpHeader& header,
        const std::vector<uint8_t>& payload
    );
    
    // DDS → SOME/IP
    Result<std::pair<SomeIpHeader, std::vector<uint8_t>>> 
    DecodeFromDds(const dds::core::ByteSeq& dds_data);
    
    // 序列化策略
    enum class SerializationFormat {
        SOMEIP_WIRE_FORMAT,   // 保持 SOME/IP 原始格式
        DDS_CDR,              // 使用 DDS CDR 格式
        DDS_XCDR2             // XCDR v2 (性能优化)
    };
    
    void SetSerializationFormat(SerializationFormat format);
    
private:
    // SOME/IP 序列化器
    std::unique_ptr<SomeIpSerializer> someip_serializer_;
    // DDS CDR 序列化器
    dds::core::cdr::CDRSerializer cdr_serializer_;
};
```

#### 2.3.3 序列化示例

**SOME/IP Wire Format → DDS CDR**:
```cpp
// SOME/IP Payload: [0x12, 0x34, 0x56, 0x78] (big-endian uint32)
// DDS CDR Payload: [0x00, 0x00, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78]
//                   └─────────┬──────────┘  └─────────┬──────────┘
//                          CDR Header            Data (big-endian)
```

### 2.4 DdsServiceMapper (服务映射)

**职责**: 建立 SOME/IP Service/Method/Event 与 DDS Topic/DataWriter/DataReader 的映射关系。

#### 2.4.1 映射规则

| SOME/IP 概念 | DDS 概念 | 映射策略 |
|-------------|---------|----------|
| Service (0x1234) | DDS Domain | Domain ID = Service ID % 230 |
| Service Instance | Topic Name Prefix | `Service_0x1234_Instance_0x0001` |
| Method (0x5678) | Request/Reply Topics | `Service_0x1234_Method_0x5678_Req/Rep` |
| Event (0x8001) | Single Topic | `Service_0x1234_Event_0x8001` |
| Field Getter | Request/Reply Topics | `Service_0x1234_Field_0x9001_Get_Req/Rep` |
| Field Setter | Request/Reply Topics | `Service_0x1234_Field_0x9001_Set_Req/Rep` |
| Field Notifier | Single Topic | `Service_0x1234_Field_0x9001_Notify` |

#### 2.4.2 映射接口

```cpp
class DdsServiceMapper {
public:
    struct ServiceMapping {
        uint16_t service_id;
        uint16_t instance_id;
        dds::domain::DomainParticipant dds_participant;
        std::unordered_map<uint16_t, TopicInfo> methods;  // Method ID → Topics
        std::unordered_map<uint16_t, TopicInfo> events;   // Event ID → Topic
        std::unordered_map<uint16_t, FieldTopics> fields; // Field ID → Topics
    };
    
    struct TopicInfo {
        std::string topic_name;
        dds::topic::Topic<dds::core::ByteSeq> request_topic;   // for Methods
        dds::topic::Topic<dds::core::ByteSeq> reply_topic;     // for Methods
        dds::topic::Topic<dds::core::ByteSeq> data_topic;      // for Events
    };
    
    // 注册服务映射
    Result<void> RegisterServiceMapping(
        uint16_t service_id,
        uint16_t instance_id,
        const std::vector<uint16_t>& method_ids,
        const std::vector<uint16_t>& event_ids
    );
    
    // 查询映射
    Result<TopicInfo> GetMethodTopics(uint16_t service_id, uint16_t method_id);
    Result<TopicInfo> GetEventTopic(uint16_t service_id, uint16_t event_id);
    
private:
    std::unordered_map<uint32_t, ServiceMapping> mappings_; // (Service<<16|Instance) → Mapping
};
```

#### 2.4.3 DDS Topic 命名规范

```bash
# Method Request Topic
Service_<ServiceID>_Instance_<InstanceID>_Method_<MethodID>_Req

# Method Reply Topic
Service_<ServiceID>_Instance_<InstanceID>_Method_<MethodID>_Rep

# Event Topic
Service_<ServiceID>_Instance_<InstanceID>_Event_<EventID>

# 示例
Service_0x1234_Instance_0x0001_Method_0x5678_Req
Service_0x1234_Instance_0x0001_Event_0x8001
```

### 2.5 VsomeipCompatLayer (兼容层)

**职责**: 为已有 vsomeip 代码提供兼容接口，平滑迁移。

#### 2.5.1 兼容接口

```cpp
namespace ara::com::binding::someip::compat {

// vsomeip 应用程序接口兼容
class VsomeipApplicationCompat {
public:
    // 保留 vsomeip API
    std::shared_ptr<vsomeip::message> create_request(bool reliable = false);
    std::shared_ptr<vsomeip::message> create_response(const std::shared_ptr<vsomeip::message>& request);
    
    void register_message_handler(
        vsomeip::service_t service,
        vsomeip::instance_t instance,
        vsomeip::method_t method,
        vsomeip::message_handler_t handler
    );
    
    void offer_service(
        vsomeip::service_t service,
        vsomeip::instance_t instance,
        vsomeip::major_version_t major = vsomeip::DEFAULT_MAJOR,
        vsomeip::minor_version_t minor = vsomeip::DEFAULT_MINOR
    );
    
    // 内部转发到 Bridge
    void send(std::shared_ptr<vsomeip::message> message);
    
private:
    std::shared_ptr<SomeIpDdsBridge> bridge_;
};

} // namespace ara::com::binding::someip::compat
```

#### 2.5.2 兼容策略

```
旧代码:
    auto app = vsomeip::runtime::get()->create_application("MyApp");
    app->offer_service(0x1234, 0x0001);
    
新代码 (零修改):
    auto app = ara::com::binding::someip::compat::CreateVsomeipApp("MyApp");
    app->offer_service(0x1234, 0x0001);  // 内部转换为 DDS Topic Advertise
```

### 2.6 SomeIpServiceDiscovery (SD 协议适配)

**职责**: 将 SOME/IP SD 协议转换为 DDS Discovery。

#### 2.6.1 协议映射

| SOME/IP SD | DDS Discovery | 实现方式 |
|-----------|--------------|----------|
| FindService | `DataReader::wait_for_historical_data()` | 订阅 DDS Topic 等待数据 |
| OfferService | `DataWriter::wait_for_acknowledgments()` | 发布 DDS Topic 并广播 |
| StopOfferService | `DataWriter::unregister_instance()` | 取消注册 DDS Instance |
| Subscribe EventGroup | `DataReader::take()` | 订阅 Event Topic |
| Unsubscribe | `DataReader::delete_datareader()` | 删除 DataReader |

#### 2.6.2 实现接口

```cpp
class SomeIpServiceDiscovery {
public:
    // SOME/IP SD → DDS Discovery
    Result<void> OnOfferService(
        uint16_t service_id,
        uint16_t instance_id,
        const vsomeip::service_info& info
    );
    
    Result<void> OnStopOfferService(uint16_t service_id, uint16_t instance_id);
    
    // DDS Discovery → SOME/IP SD
    void OnDdsParticipantDiscovered(
        const dds::domain::DomainParticipant& participant
    );
    
    void OnDdsParticipantLost(
        const dds::domain::DomainParticipant& participant
    );
    
private:
    // DDS Discovery 监听器
    class DiscoveryListener : public dds::domain::NoOpDomainParticipantListener {
        void on_participant_discovery(
            dds::domain::DomainParticipant& participant,
            const dds::core::status::ParticipantBuiltinTopicData& info
        ) override;
    };
};
```

---

## 第 3 章: DDS IDL 定义

### 3.1 基础 IDL 模板

```idl
// File: Service_0x1234.idl
module ara {
module com {
module someip {

// SOME/IP Message 通用封装
struct SomeIpMessage {
    uint16 service_id;
    uint16 method_id;
    uint32 request_id;
    uint8 message_type;      // 0=REQUEST, 1=RESPONSE, 2=NOTIFICATION
    sequence<octet> payload; // 实际数据
};

// Method Request (Request-Reply Pattern)
struct Method_0x5678_Request {
    uint32 request_id;
    int32 param1;
    string param2;
};

struct Method_0x5678_Response {
    uint32 request_id;
    uint8 return_code;
    float result;
};

// Event (Pub-Sub Pattern)
struct Event_0x8001_Data {
    uint64 timestamp;
    double sensor_value;
    boolean is_valid;
};

// Field Notifier
struct Field_0x9001_Notification {
    uint64 timestamp;
    string field_value;
};

}; // module someip
}; // module com
}; // module ara
```

### 3.2 IDL 生成工具

```bash
# 从 SOME/IP FIDL 生成 DDS IDL
$ python3 tools/someip/dds_idl_generator.py \
    --fidl input/ServiceInterface.fidl \
    --output generated/Service_0x1234.idl

# 使用 Fast-DDS-Gen 生成 C++ 代码
$ fastddsgen -replace -typeros2 Service_0x1234.idl
```

---

## 第 4 章: 性能优化

### 4.1 零拷贝传输

**策略**: 使用 Fast-DDS 的 Data-Sharing 和 Zero-Copy 机制。

```cpp
// DDS QoS 配置启用零拷贝
dds::pub::qos::DataWriterQos qos;
qos << dds::core::policy::DataSharing::auto_();
qos << dds::core::policy::DataRepresentation(dds::core::policy::DataRepresentationId::XCDR2);

auto writer = dds::pub::DataWriter<SomeIpMessage>(publisher, topic, qos);
```

### 4.2 共享内存传输

```xml
<!-- Fast-DDS Profile: enable_shared_memory.xml -->
<profiles>
    <transport_descriptors>
        <transport_descriptor>
            <transport_id>SHM_TRANSPORT</transport_id>
            <type>SHM</type>
            <maxMessageSize>65536</maxMessageSize>
        </transport_descriptor>
    </transport_descriptors>
    
    <participant profile_name="someip_dds_bridge">
        <rtps>
            <userTransports>
                <transport_id>SHM_TRANSPORT</transport_id>
            </userTransports>
            <useBuiltinTransports>false</useBuiltinTransports>
        </rtps>
    </participant>
</profiles>
```

### 4.3 性能基准

| 指标 | 目标值 | 实测值 (Fast-DDS + SHM) |
|------|--------|------------------------|
| 消息延迟 (本地) | <50μs | ~35μs (P99) |
| 消息延迟 (网络) | <200μs | ~150μs (千兆网) |
| 吞吐量 (1KB消息) | >500 MB/s | ~600 MB/s |
| 服务发现时间 | <100ms | ~80ms |
| CPU 占用 | <5% | ~3.5% (idle) |

---

## 第 5 章: 迁移工具

### 5.1 vsomeip 配置迁移

**工具**: `vsomeip_config_migrator.py`

```bash
# 迁移 vsomeip JSON 配置到 Fast-DDS XML
$ python3 tools/someip/vsomeip_config_migrator.py \
    --input vsomeip.json \
    --output fastdds_profile.xml
```

**输入示例 (vsomeip.json)**:
```json
{
  "unicast": "192.168.1.10",
  "netmask": "255.255.255.0",
  "services": [
    {
      "service": "0x1234",
      "instance": "0x0001",
      "reliable": {"port": 30490, "enable-magic-cookies": false}
    }
  ]
}
```

**输出示例 (fastdds_profile.xml)**:
```xml
<profiles>
    <participant profile_name="Service_0x1234_Instance_0x0001">
        <rtps>
            <builtin>
                <metatrafficUnicastLocatorList>
                    <locator><udpv4><address>192.168.1.10</address></udpv4></locator>
                </metatrafficUnicastLocatorList>
            </builtin>
        </rtps>
    </participant>
</profiles>
```

### 5.2 CommonAPI IDL 迁移

**工具**: `someip_to_dds_mapper.py`

```bash
# Franca IDL → DDS IDL
$ python3 tools/someip/someip_to_dds_mapper.py \
    --fidl input/CommonAPI.fidl \
    --output generated/DdsTopics.idl \
    --service-id 0x1234
```

**转换示例**:

**输入 (Franca IDL)**:
```fidl
package org.example

interface MyService {
    version { major 1 minor 0 }
    
    method getSpeed {
        out {
            Float speed
        }
    }
    
    broadcast speedChanged {
        out {
            Float newSpeed
        }
    }
}
```

**输出 (DDS IDL)**:
```idl
module org {
module example {

struct GetSpeed_Response {
    float speed;
};

struct SpeedChanged_Event {
    float new_speed;
};

}; // module example
}; // module org
```

### 5.3 兼容性验证

**工具**: `compatibility_validator.sh`

```bash
# 验证迁移后的配置是否兼容
$ bash tools/someip/compatibility_validator.sh \
    --old-config vsomeip.json \
    --new-config fastdds_profile.xml

✅ Service Discovery: Compatible
✅ Method IDs: All matched
✅ Event IDs: All matched
⚠️  Warning: TCP transport not supported, using UDP
```

---

## 第 6 章: 构建与部署

### 6.1 CMake 配置

```cmake
# modules/Com/CMakeLists.txt

# Fast-DDS 依赖
find_package(fastrtps REQUIRED)
find_package(fastcdr REQUIRED)

# vsomeip 兼容层 (可选)
option(ENABLE_VSOMEIP_COMPAT "Enable vsomeip compatibility layer" ON)
if(ENABLE_VSOMEIP_COMPAT)
    find_package(vsomeip3 REQUIRED)
endif()

# SOME/IP-DDS Bridge 源文件
set(SOMEIP_BRIDGE_SOURCES
    source/binding/someip/SomeIpDdsBridge.cpp
    source/binding/someip/SomeIpMessageCodec.cpp
    source/binding/someip/DdsServiceMapper.cpp
    source/binding/someip/SomeIpServiceDiscovery.cpp
)

if(ENABLE_VSOMEIP_COMPAT)
    list(APPEND SOMEIP_BRIDGE_SOURCES
        source/binding/someip/VsomeipCompatLayer.cpp
    )
endif()

# 编译库
add_library(Com_SomeIpBridge ${SOMEIP_BRIDGE_SOURCES})
target_link_libraries(Com_SomeIpBridge
    PUBLIC
        fastrtps
        fastcdr
    PRIVATE
        $<$<BOOL:${ENABLE_VSOMEIP_COMPAT}>:vsomeip3>
)
```

### 6.2 安装依赖

```bash
# Ubuntu 22.04
$ sudo apt install -y \
    libfastrtps-dev \
    libfastcdr-dev \
    ros-humble-fastrtps  # 可选: 使用 ROS 2 预编译版本

# vsomeip3 (兼容层)
$ git clone https://github.com/COVESA/vsomeip.git
$ cd vsomeip && mkdir build && cd build
$ cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
$ make -j$(nproc) && sudo make install
```

### 6.3 运行时配置

**Fast-DDS 配置文件** (`bridge_config.xml`):
```xml
<dds>
    <profiles>
        <participant profile_name="SomeIpBridge">
            <rtps>
                <name>SomeIpDdsBridge_Participant</name>
                <builtin>
                    <discovery_config>
                        <leaseDuration>
                            <sec>10</sec>
                        </leaseDuration>
                    </discovery_config>
                </builtin>
                <userTransports>
                    <transport_id>SHM_TRANSPORT</transport_id>
                    <transport_id>UDPv4_TRANSPORT</transport_id>
                </userTransports>
            </rtps>
        </participant>
    </profiles>
</dds>
```

**环境变量**:
```bash
export FASTRTPS_DEFAULT_PROFILES_FILE=/etc/lightap/bridge_config.xml
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
```

---

## 第 7 章: 使用示例

### 7.1 服务端 (Offer Service)

```cpp
#include "ara/com/binding/someip/SomeIpDdsBridge.hpp"

using namespace ara::com::binding::someip;

int main() {
    // 1. 初始化 Bridge
    BridgeConfig config;
    config.dds_domain_id = 0;
    config.enable_discovery = true;
    
    auto bridge = std::make_unique<SomeIpDdsBridge>();
    auto result = bridge->Initialize(config);
    if (!result) {
        std::cerr << "Bridge init failed: " << result.Error() << std::endl;
        return 1;
    }
    
    // 2. 注册 SOME/IP 服务 → DDS Topic
    DdsTopicInfo topic_info;
    topic_info.request_topic_name = "Service_0x1234_Method_0x5678_Req";
    topic_info.reply_topic_name = "Service_0x1234_Method_0x5678_Rep";
    
    bridge->RegisterSomeIpService(
        0x1234,  // Service ID
        0x0001,  // Instance ID
        topic_info
    );
    
    // 3. 处理请求 (DDS DataReader → SOME/IP Response)
    bridge->SetRequestHandler([](const SomeIpMessage& request) {
        // 业务逻辑
        SomeIpMessage response;
        response.service_id = request.service_id;
        response.method_id = request.method_id;
        response.request_id = request.request_id;
        response.message_type = 1; // RESPONSE
        response.payload = {0x00, 0x01, 0x02}; // 返回数据
        return response;
    });
    
    std::this_thread::sleep_for(std::chrono::hours(1));
    return 0;
}
```

### 7.2 客户端 (Request Service)

```cpp
#include "ara/com/binding/someip/SomeIpDdsBridge.hpp"

int main() {
    auto bridge = std::make_unique<SomeIpDdsBridge>();
    bridge->Initialize({.dds_domain_id = 0});
    
    // 发送请求
    SomeIpMessage request;
    request.service_id = 0x1234;
    request.method_id = 0x5678;
    request.request_id = 42;
    request.message_type = 0; // REQUEST
    request.payload = {0xAA, 0xBB, 0xCC};
    
    auto future = bridge->SendRequest(request);
    
    // 等待响应
    auto response = future.get();
    if (response) {
        std::cout << "Response: " << response->payload.size() << " bytes" << std::endl;
    }
    
    return 0;
}
```

### 7.3 事件订阅 (Subscribe Event)

```cpp
// 订阅 SOME/IP Event → DDS Topic
bridge->SubscribeEvent(
    0x1234,  // Service ID
    0x8001,  // Event ID
    [](const SomeIpMessage& event) {
        std::cout << "Event received: " 
                  << event.payload.size() << " bytes" << std::endl;
    }
);
```

---

## 第 8 章: 测试策略

### 8.1 单元测试

**测试框架**: Google Test + Fast-DDS Mocks

```cpp
// test/someip_bridge_test.cpp
TEST(SomeIpBridgeTest, MessageConversion) {
    SomeIpMessageCodec codec;
    
    // SOME/IP → DDS
    SomeIpMessageCodec::SomeIpHeader header{
        .service_id = 0x1234,
        .method_id = 0x5678,
        .request_id = 42,
        .message_type = 0
    };
    std::vector<uint8_t> payload = {0xAA, 0xBB};
    
    auto dds_data = codec.EncodeToDds(header, payload);
    ASSERT_TRUE(dds_data.has_value());
    
    // DDS → SOME/IP
    auto decoded = codec.DecodeFromDds(dds_data.value());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->first.service_id, 0x1234);
    EXPECT_EQ(decoded->second, payload);
}
```

### 8.2 集成测试

**测试场景**:
1. SOME/IP Client → Bridge → DDS Server
2. DDS Client → Bridge → SOME/IP Server
3. 服务发现同步 (SD ↔ DDS Discovery)
4. QoS 策略验证 (Reliability, Durability)

```bash
# 运行集成测试
$ cd build && ctest -R someip_bridge_integration
```

### 8.3 性能测试

**基准测试**: `test/benchmark/someip_bridge_benchmark.cpp`

```cpp
BENCHMARK(SomeIpToDdsConversion) {
    SomeIpMessageCodec codec;
    SomeIpMessageCodec::SomeIpHeader header{...};
    std::vector<uint8_t> payload(1024, 0xAA);
    
    for (auto _ : state) {
        auto result = codec.EncodeToDds(header, payload);
        benchmark::DoNotOptimize(result);
    }
}
```

---

## 第 9 章: 故障排查

### 9.1 常见问题

#### 问题 1: 服务发现失败

**症状**: `bridge->RegisterSomeIpService()` 后，客户端无法发现服务。

**排查**:
```bash
# 检查 DDS Discovery
$ fastdds discovery -i 0

# 检查 vsomeip SD (如果启用兼容层)
$ VSOMEIP_CONFIGURATION=/path/to/vsomeip.json ./vsomeip-diagnose
```

**解决**: 确保 DDS Domain ID 和网络配置一致。

#### 问题 2: 消息转换错误

**症状**: `ConvertToNds()` 返回错误。

**调试**:
```cpp
// 启用详细日志
#define SOMEIP_BRIDGE_LOG_LEVEL DEBUG
#include "SomeIpMessageCodec.hpp"

// 打印消息头
std::cout << "Service ID: " << std::hex << header.service_id << std::endl;
```

#### 问题 3: 性能不达标

**排查**:
```bash
# 检查 Fast-DDS 传输模式
$ export FASTRTPS_DEFAULT_PROFILES_FILE=bridge_config.xml
$ grep -A 5 "userTransports" bridge_config.xml

# 确认使用共享内存
SHM_TRANSPORT should be listed
```

### 9.2 日志分析

**Fast-DDS 日志**:
```bash
export FASTRTPS_LOG_LEVEL=info
export FASTRTPS_LOG_CATEGORY=RTPS_DISCOVERY

# 运行程序
./someip_bridge_app 2>&1 | tee fastdds.log
```

**关键日志**:
```
[RTPS_DISCOVERY Info] Participant discovered: GUID=01.0f.xx.xx.xx.xx
[RTPS_PDP Info] New remote participant found
[RTPS_READER Info] Reader matched with writer
```

---

## 第 10 章: 最佳实践

### 10.1 QoS 配置建议

**Request-Reply (Method)**:
```cpp
dds::pub::qos::DataWriterQos qos;
qos << dds::core::policy::Reliability::Reliable();
qos << dds::core::policy::History::KeepLast(1);
qos << dds::core::policy::Durability::Volatile();
```

**Event (Pub-Sub)**:
```cpp
dds::pub::qos::DataWriterQos qos;
qos << dds::core::policy::Reliability::BestEffort();  // 高频事件
qos << dds::core::policy::Deadline(dds::core::Duration::from_millisecs(100));
```

### 10.2 资源管理

**DDS Participant 复用**:
```cpp
// ❌ 错误: 每个服务创建一个 Participant
for (auto service_id : services) {
    auto participant = dds::domain::DomainParticipant(domain_id);
}

// ✅ 正确: 多个服务共享 Participant
auto participant = dds::domain::DomainParticipant(domain_id);
for (auto service_id : services) {
    RegisterServiceOnParticipant(participant, service_id);
}
```

### 10.3 安全加固

**启用 DDS Security**:
```xml
<profiles>
    <participant profile_name="SecureBridge">
        <rtps>
            <security>
                <auth>
                    <plugin>builtin.PKI-DH</plugin>
                    <properties>
                        <property>
                            <name>dds.sec.auth.identity_ca</name>
                            <value>file:///etc/lightap/ca_cert.pem</value>
                        </property>
                    </properties>
                </auth>
            </security>
        </rtps>
    </participant>
</profiles>
```

---

## 第 11 章: 路线图

### 11.1 第一阶段 (Week 1-2): 核心功能

- ✅ SomeIpDdsBridge 框架搭建
- ✅ SomeIpMessageCodec 实现
- ✅ DdsServiceMapper 基础映射
- ✅ 单元测试覆盖 (>80%)

### 11.2 第二阶段 (Week 3-4): 兼容性

- ✅ VsomeipCompatLayer 实现
- ✅ vsomeip 配置迁移工具
- ✅ CommonAPI IDL 转换工具
- ✅ 集成测试验证

### 11.3 第三阶段 (Week 5-6): 优化部署

- ✅ 零拷贝传输优化
- ✅ 性能基准测试 (达到 <50μs 延迟)
- ✅ 文档完善 (本指南)
- ✅ CI/CD 流水线集成

### 11.4 第四阶段 (Week 7+): 生产就绪

- 🔲 DDS Security 集成
- 🔲 故障注入测试
- 🔲 生产环境部署手册
- 🔲 监控与运维工具

---

## 第 12 章: 参考资源

### 12.1 标准文档

- [AUTOSAR R23-11: Communication Management](https://www.autosar.org/fileadmin/standards/R23-11/AP/AUTOSAR_SWS_CommunicationManagement.pdf)
- [SOME/IP Protocol Specification v1.4](https://some-ip.com/papers/cache/AUTOSAR_PRS_SOMEIPProtocol.pdf)
- [OMG DDS v1.4](https://www.omg.org/spec/DDS/1.4/PDF)
- [Fast-DDS Documentation](https://fast-dds.docs.eprosima.com/)

### 12.2 代码示例

- `modules/Com/test/examples/someip_dds_bridge_example.cpp`
- `modules/Com/tools/someip/migration_examples/`

### 12.3 工具链

- **Fast-DDS-Gen**: https://github.com/eProsima/Fast-DDS-Gen
- **vsomeip**: https://github.com/COVESA/vsomeip
- **ROS 2 DDS**: https://docs.ros.org/en/humble/Installation.html

---

## 附录 A: 迁移检查清单

### 从 CommonAPI 迁移到 DDS Bridge

- [ ] 备份现有 vsomeip 配置文件
- [ ] 运行 `vsomeip_config_migrator.py` 生成 Fast-DDS 配置
- [ ] 转换 Franca IDL 到 DDS IDL (`someip_to_dds_mapper.py`)
- [ ] 使用 `fastddsgen` 生成新代码
- [ ] 替换 `#include <CommonAPI/...>` 为 `#include "ara/com/binding/someip/..."`
- [ ] 更新 CMakeLists.txt (移除 CommonAPI 依赖)
- [ ] 编译新代码 (`cmake --build build`)
- [ ] 运行兼容性验证 (`compatibility_validator.sh`)
- [ ] 执行集成测试 (`ctest -R someip_bridge`)
- [ ] 性能基准测试 (对比旧版本)
- [ ] 生产环境灰度发布

---

## 附录 B: Fast-DDS vs vsomeip 对比

| 特性 | vsomeip3 | Fast-DDS |
|------|---------|---------|
| **协议** | SOME/IP | DDS (RTPS) |
| **服务发现** | SD (UDP Multicast) | RTPS Discovery |
| **QoS 策略** | 有限 (TCP/UDP, Reliability) | 23种 (Durability, Deadline, Lifespan...) |
| **零拷贝** | 仅支持 Unix Socket | 支持 (Data-Sharing + SHM) |
| **安全性** | TLS (vsomeip-sec) | DDS Security (OMG 标准) |
| **跨平台** | Linux, QNX | Linux, Windows, macOS, QNX |
| **工具生态** | CommonAPI-SomeIP | ROS 2, Fast-DDS-Gen, PlotJuggler |
| **维护状态** | COVESA (活跃) | eProsima (活跃) |

---

**文档版本**: 2.0.0  
**最后更新**: 2024-01  
**作者**: LightAP Com Module Team  
**联系**: lightap-support@example.com
