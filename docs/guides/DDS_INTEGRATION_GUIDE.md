# DDS Network Binding 集成指南

## 文档信息

| 字段 | 值 |
|-----|---|
| **文档标题** | LightAP Com模块 - DDS Network Binding 集成指南 |
| **基于标准** | AUTOSAR AP TR DDS Security Integration |
| **DDS 版本** | OMG DDS 1.4 + DDS Security 1.1 |
| **实现库** | eProsima Fast-DDS 3.x |
| **日期** | 2026-03-02 |
| **版本** | 2.0.0 |

## 目录

1. [DDS 简介](#1-dds-简介)
2. [AUTOSAR 到 DDS 映射](#2-autosar-到-dds-映射)
3. [DDS Security 集成](#3-dds-security-集成)
4. [架构设计](#4-架构设计)
5. [实施计划](#5-实施计划)
6. [使用示例](#6-使用示例)

---

## 1. DDS 简介

### 1.1 什么是 DDS？

**DDS (Data Distribution Service)** 是 OMG（Object Management Group）制定的数据分发服务标准，专为：

- 🌐 **分布式系统**: 跨网络的数据共享
- ⚡ **实时系统**: 低延迟、高吞吐量
- 🔒 **关键任务**: 航空、军事、工业自动化
- 🚗 **车联网**: V2X 通信

### 1.2 DDS 核心概念

| 概念 | 说明 | AUTOSAR 对应 |
|------|------|-------------|
| **Domain** | 逻辑通信域，隔离不同应用 | Network Binding Domain |
| **Participant** | DDS 应用实例 | Service Instance |
| **Topic** | 数据类型 + 名称 | Event / Field |
| **Publisher** | 数据发送方 | Service Skeleton |
| **Subscriber** | 数据接收方 | Service Proxy |
| **DataWriter** | 写入特定 Topic | Event Sender / Field Setter |
| **DataReader** | 读取特定 Topic | Event Receiver / Field Getter |
| **QoS** | 服务质量策略 | Communication Properties |

### 1.3 DDS vs 其他传输协议

| 特性 | D-Bus | SOME/IP | DDS |
|------|-------|---------|-----|
| **架构** | 集中式总线 | P2P + 服务发现 | 去中心化 Pub/Sub |
| **适用范围** | 单机 IPC | 车载网络 | 分布式系统 |
| **延迟** | 1-5ms | <100μs | <10μs (共享内存) |
| **吞吐量** | 中 | 高 | 极高 |
| **服务发现** | D-Bus Daemon | SOME/IP SD | RTPS Discovery |
| **QoS 策略** | 有限 | 基本 | 丰富（22+ 策略） |
| **安全性** | 基于系统权限 | 基本认证 | DDS Security (PKI) |
| **跨网络** | ❌ | ✅ | ✅ |
| **多播支持** | ❌ | ✅ | ✅ |

---

## 2. AUTOSAR 到 DDS 映射

### 2.1 Service Interface 映射

**AUTOSAR Service Interface** → **DDS Topics**

```cpp
// AUTOSAR Service Interface
interface VehicleSpeedService {
    event SpeedChanged : SpeedData;
    field CurrentSpeed : float;
    method GetAverageSpeed() : float;
}

// 映射到 DDS Topics
Topic "services/VehicleSpeed/SpeedChanged"   // Event
Topic "services/VehicleSpeed/CurrentSpeed"   // Field (with TRANSIENT_LOCAL)
Topic "services/VehicleSpeed/GetAverageSpeed/Request"   // Method Request
Topic "services/VehicleSpeed/GetAverageSpeed/Reply"     // Method Reply
```

**命名规则** (TR_DDSS_00104):
```
services/<ServiceInterface>/<Element>
```

### 2.2 通信模式映射

| AUTOSAR 模式 | DDS 实现 | 说明 |
|-------------|---------|------|
| **Event** | DataWriter/DataReader | 发布/订阅模式 |
| **Field** | Topic + TRANSIENT_LOCAL QoS | 持久化最新值 |
| **Method (Request/Response)** | 2 Topics (Req + Rep) | RPC over Pub/Sub |
| **Fire-and-Forget** | DataWriter (BEST_EFFORT) | 不等待响应 |

### 2.3 QoS 策略映射

| AUTOSAR 属性 | DDS QoS 策略 | 值 |
|-------------|-------------|---|
| 可靠性 | RELIABILITY | RELIABLE / BEST_EFFORT |
| 持久化（Field） | DURABILITY | TRANSIENT_LOCAL |
| 历史记录 | HISTORY | KEEP_LAST(n) / KEEP_ALL |
| 数据有效期 | LIFESPAN | Duration |
| 优先级 | TRANSPORT_PRIORITY | 0-100 |
| 更新周期 | DEADLINE | Period |
| 数据新鲜度 | LIVELINESS | AUTOMATIC / MANUAL |
| 数据所有权 | OWNERSHIP | SHARED / EXCLUSIVE |

**示例**: Event 的 QoS 配置

```cpp
DataWriterQos qos;
qos.reliability().kind = RELIABLE_RELIABILITY_QOS;
qos.history().kind = KEEP_LAST_HISTORY_QOS;
qos.history().depth = 10;  // 缓存最近10条消息
qos.durability().kind = VOLATILE_DURABILITY_QOS;
```

**示例**: Field 的 QoS 配置

```cpp
DataWriterQos qos;
qos.reliability().kind = RELIABLE_RELIABILITY_QOS;
qos.durability().kind = TRANSIENT_LOCAL_DURABILITY_QOS;  // 持久化
qos.history().kind = KEEP_LAST_HISTORY_QOS;
qos.history().depth = 1;  // 仅保留最新值
```

---

## 3. DDS Security 集成

### 3.1 AUTOSAR TR_DDSS 需求

基于 **AUTOSAR_AP_TR_DDSSecurityIntegration**:

| TR 需求 | 组件 | 说明 |
|---------|------|------|
| **TR_DDSS_00001** | 总体要求 | 每个服务实例需要完整的安全工件 |
| **TR_DDSS_00002** | Identity CA | 身份证书颁发机构 |
| **TR_DDSS_00003** | Identity Certificate | 参与者身份证书 |
| **TR_DDSS_00004** | Private Key | 私钥文件 |
| **TR_DDSS_00005** | Permissions CA | 权限证书颁发机构 |
| **TR_DDSS_00006** | Governance Document | 域治理文档（加密、签名策略） |
| **TR_DDSS_00007** | Permissions Document | 访问权限文档 |

### 3.2 DDS Security 插件

**DDS Security 规范** 定义了 5 个插件:

1. **Authentication Plugin** (认证)
   - 验证参与者身份
   - 基于 PKI 证书

2. **Access Control Plugin** (访问控制)
   - 检查权限文档
   - Topic 级别的读写控制

3. **Cryptographic Plugin** (加密)
   - AES-GCM 数据加密
   - HMAC-SHA256 签名

4. **Logging Plugin** (审计)
   - 记录安全事件

5. **Data Tagging Plugin** (数据标记)
   - 元数据标记

### 3.3 安全工件部署

**目录结构** (基于 TR_DDSS_00001):

```
/opt/autosar/security/dds/
├── ca/
│   ├── identity_ca.pem          # TR_DDSS_00002: 身份 CA
│   └── permissions_ca.pem       # TR_DDSS_00005: 权限 CA
│
├── certs/
│   └── <instance_id>/
│       ├── cert.pem             # TR_DDSS_00003: 实例证书
│       └── key.pem              # TR_DDSS_00004: 实例私钥
│
├── governance/
│   └── governance.xml           # TR_DDSS_00006: 治理文档
│
└── permissions/
    └── <instance_id>/
        └── permissions.xml      # TR_DDSS_00007: 权限文档
```

### 3.4 Governance Document 示例

```xml
<?xml version="1.0" encoding="UTF-8"?>
<dds xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" 
     xsi:noNamespaceSchemaLocation="governance.xsd">
  <domain_access_rules>
    <domain_rule>
      <domains>
        <id>0</id>  <!-- DDS Domain ID -->
      </domains>
      <allow_unauthenticated_participants>false</allow_unauthenticated_participants>
      <enable_join_access_control>true</enable_join_access_control>
      
      <!-- Topic 访问规则 (TR_DDSS_00102) -->
      <topic_access_rules>
        <topic_rule>
          <topic_expression>services/*</topic_expression>  <!-- 所有服务 -->
          <enable_discovery_protection>true</enable_discovery_protection>
          <enable_liveliness_protection>true</enable_liveliness_protection>
          <enable_read_access_control>true</enable_read_access_control>
          <enable_write_access_control>true</enable_write_access_control>
          <metadata_protection_kind>ENCRYPT</metadata_protection_kind>
          <data_protection_kind>ENCRYPT</data_protection_kind>
        </topic_rule>
      </topic_access_rules>
    </domain_rule>
  </domain_access_rules>
</dds>
```

### 3.5 Permissions Document 示例

```xml
<?xml version="1.0" encoding="UTF-8"?>
<dds xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
     xsi:noNamespaceSchemaLocation="permissions.xsd">
  <permissions>
    <grant name="VehicleSpeedPublisher">
      <subject_name>CN=VehicleSpeedService,O=Automotive,C=CN</subject_name>
      <validity>
        <not_before>2025-01-01T00:00:00</not_before>
        <not_after>2026-01-01T00:00:00</not_after>
      </validity>
      
      <!-- 允许发布 (TR_DDSS_00201) -->
      <allow_rule>
        <domains><id>0</id></domains>
        <publish>
          <topics>
            <topic>services/VehicleSpeed/*</topic>
          </topics>
        </publish>
      </allow_rule>
    </grant>
  </permissions>
</dds>
```

---

## 4. 架构设计

### 4.1 DDS Binding 组件架构

```
┌─────────────────────────────────────────────────────────┐
│             lap::com Public API                         │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│         DDS Network Binding Layer                       │
│  ┌──────────────┬──────────────┬────────────────────┐  │
│  │ DdsConnection│ DdsTopic     │ DdsSecurity        │  │
│  │ Manager      │ Binding      │ Manager            │  │
│  └──────────────┴──────────────┴────────────────────┘  │
│  ┌──────────────┬──────────────┬────────────────────┐  │
│  │ DdsPublisher │ DdsSubscriber│ DdsMethod          │  │
│  │ Binding      │ Binding      │ Binding (RPC)      │  │
│  └──────────────┴──────────────┴────────────────────┘  │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│    eProsima Fast-DDS + DDS Security Plugin              │
│  - DomainParticipant                                    │
│  - DataWriter / DataReader                              │
│  - Topic / Type Support                                 │
│  - QoS Policies                                         │
│  - Security Plugins (Auth, Crypto, Access Control)     │
└─────────────────────────────────────────────────────────┘
```

### 4.2 核心类设计

#### 4.2.1 DdsConnectionManager

**职责**: 管理 DDS Domain Participant 和连接生命周期

```cpp
class DdsConnectionManager {
public:
    static Result<DomainParticipant*> GetParticipant(uint32_t domain_id);
    static Result<void> ConfigureSecurity(
        const std::string& identity_ca,
        const std::string& permissions_ca,
        const std::string& identity_cert,
        const std::string& private_key,
        const std::string& governance,
        const std::string& permissions
    );
    static Result<void> Shutdown();
    
private:
    std::map<uint32_t, DomainParticipant*> participants_;
    SecurityConfig security_config_;
};
```

#### 4.2.2 DdsTopicBinding

**职责**: Topic 创建与 TypeSupport 注册

```cpp
template <typename T>
class DdsTopicBinding {
public:
    DdsTopicBinding(
        DomainParticipant* participant,
        const std::string& topic_name,
        const TopicQos& qos
    );
    
    Topic* GetTopic() const;
    TypeSupport GetTypeSupport() const;
    
private:
    Topic* topic_;
    TypeSupport type_support_;
};
```

#### 4.2.3 DdsPublisherBinding (Event 发送)

**职责**: Event 发布（映射到 DataWriter）

```cpp
template <typename EventType>
class DdsPublisherBinding {
public:
    DdsPublisherBinding(
        DomainParticipant* participant,
        const std::string& topic_name,
        const DataWriterQos& qos
    );
    
    Result<void> Send(const EventType& event);
    
private:
    Publisher* publisher_;
    DataWriter* writer_;
};
```

#### 4.2.4 DdsSubscriberBinding (Event 接收)

**职责**: Event 订阅（映射到 DataReader）

```cpp
template <typename EventType>
class DdsSubscriberBinding {
public:
    DdsSubscriberBinding(
        DomainParticipant* participant,
        const std::string& topic_name,
        const DataReaderQos& qos
    );
    
    Result<void> Subscribe(EventReceiveHandler<EventType> handler);
    Result<void> Unsubscribe();
    Result<std::vector<EventType>> GetNewSamples();
    
private:
    class ReaderListener : public DataReaderListener {
        void on_data_available(DataReader* reader) override;
    };
    
    Subscriber* subscriber_;
    DataReader* reader_;
    ReaderListener listener_;
};
```

### 4.3 数据类型定义 (IDL)

**VehicleSpeed.idl**:

```idl
module VehicleData {
    struct SpeedInfo {
        float current_speed;
        float average_speed;
        unsigned long timestamp;
    };
};
```

**生成代码** (fastddsgen):

```bash
fastddsgen -replace VehicleSpeed.idl
```

生成文件:
- `VehicleSpeed.h` / `.cxx` - 数据类型
- `VehicleSpeedPubSubTypes.h` / `.cxx` - TypeSupport

---

## 5. 实施计划

### Phase 1: 基础 DDS 绑定 (3-4周)

**Week 1: Domain & Participant 管理**
- [ ] `DdsConnectionManager` 实现
- [ ] Domain Participant 创建与配置
- [ ] QoS 配置映射
- [ ] 单元测试

**Week 2: Topic & DataWriter/DataReader**
- [ ] `DdsTopicBinding` 实现
- [ ] `DdsPublisherBinding` (Event 发送)
- [ ] `DdsSubscriberBinding` (Event 接收)
- [ ] Topic 发现测试

**Week 3: Field 支持**
- [ ] Field Notifier (TRANSIENT_LOCAL QoS)
- [ ] Field Getter/Setter
- [ ] 字段初始值支持

**Week 4: Method RPC 支持**
- [ ] Request/Reply Topics
- [ ] `DdsMethodBinding` (Request/Response)
- [ ] 异步调用支持

### Phase 2: DDS Security 集成 (2-3周)

**Week 1: 证书管理**
- [ ] `DdsSecurityManager` 实现
- [ ] 证书加载与验证
- [ ] 配置文件解析

**Week 2: Governance & Permissions**
- [ ] Governance Document 生成
- [ ] Permissions Document 生成
- [ ] 自动化部署脚本

**Week 3: 端到端安全测试**
- [ ] 认证测试
- [ ] 加密通信测试
- [ ] 访问控制测试

### Phase 3: 性能优化与文档 (1-2周)

- [ ] 零拷贝优化（共享内存传输）
- [ ] QoS 调优
- [ ] 性能基准测试
- [ ] API 文档生成
- [ ] 用户指南编写

---

## 5.5 Discovery Server 退化链路

> **v1.0** — 2026/03/01 实现

DDS binding 支持使用 Fast-DDS Discovery Server 作为集中式服务发现注册中心。
当 Discovery Server 不可达时，自动退化到标准 PDP/EDP 组播发现。

### 5.5.1 发现链路架构

```text
优先链路               退化链路               恢复链路
┌──────────┐        ┌──────────┐        ┌──────────┐
│DS Server │──失败──→│PDP/EDP   │──DS恢复→│DS Server │
│SUPER_CLI │  N次    │SIMPLE    │  探测    │SUPER_CLI │
│< 1ms     │        │5-100ms   │  成功    │< 1ms     │
└──────────┘        └──────────┘        └──────────┘
```

### 5.5.2 配置

在 `config/bindings.yaml` 的 DDS 段添加:

```yaml
dds:
  discovery_server: "tcp://192.168.1.10:42100"   # DS 地址 (tcp:// 或 udp://)
  ds_health_check_interval_ms: 5000              # 健康检查周期
  ds_max_failures: 3                             # 连续失败N次后退化
  ds_reconnect_interval_ms: 10000                # 退化后每N ms尝试重连
  ds_enable_fallback: true                       # 允许 DS→PDP 退化
  ds_enable_reconnect: true                      # 允许 PDP→DS 恢复
```

**地址格式**: `[tcp://|udp://]<host>:<port>`
- `tcp://192.168.1.10:42100` — TCP 传输 (推荐跨ECU)
- `udp://192.168.1.10:42100` — UDP 传输 (局域网)
- `192.168.1.10:42100` — 默认 TCP
- `192.168.1.10` — 默认 TCP + 默认端口 11811

### 5.5.3 运行时 API

```cpp
// 初始化前设置 DS 地址 (可选, 也可通过 bindings.yaml)
binding->SetDiscoveryServer("tcp://192.168.1.10:42100");

// 查询当前发现模式
DiscoveryMode mode = binding->GetDiscoveryMode();
switch (mode) {
    case DiscoveryMode::kDiscoveryServer:
        // SUPER_CLIENT — 通过 DS 发现
        break;
    case DiscoveryMode::kSimplePdp:
        // SIMPLE — 标准组播发现 (退化模式)
        break;
    case DiscoveryMode::kDisconnected:
        // 未初始化
        break;
}

// 获取健康监测统计
DiscoveryServerStats stats = binding->GetDiscoveryStats();
```

### 5.5.4 核心类

| 类 | 文件 | 职责 |
|----|------|------|
| `CDdsDiscoveryServerMonitor` | `CDdsDiscoveryServerMonitor.hpp/cpp` | 后台健康监测、模式切换 |
| `DdsBinding::RecreateParticipant()` | `DdsBinding.cpp` | DDS 实体重建 (QoS 切换) |
| `DiscoveryMode` | `CDdsDiscoveryServerMonitor.hpp` | 发现模式枚举 |
| `DiscoveryServerStats` | `CDdsDiscoveryServerMonitor.hpp` | 运行时统计 |
| `DiscoveryServerMonitorConfig` | `CDdsDiscoveryServerMonitor.hpp` | 监测配置 |

### 5.5.5 故障排查

| 症状 | 原因 | 解决方案 |
|------|------|----------|
| 启动即退化到 PDP | DS 地址不可达 | 检查 `discovery_server` 地址和端口 |
| 频繁退化/恢复 | DS 网络不稳定 | 增大 `ds_max_failures` |
| 退化后不恢复 | `ds_enable_reconnect: false` | 设为 `true` |
| 退化延迟过长 | 检查间隔太大 | 减小 `ds_health_check_interval_ms` |

---

## 6. 使用示例

### 6.1 服务端 (Skeleton) - 发布 Event

```cpp
#include "lap/com/Runtime.hpp"
#include "VehicleSpeedSkeleton.hpp"
#include "VehicleSpeed.h"  // Fast-DDS 生成

class VehicleSpeedServiceImpl : public VehicleSpeedSkeleton {
public:
    VehicleSpeedServiceImpl() 
        : VehicleSpeedSkeleton(InstanceIdentifier(1)) {
    }
    
    void PublishSpeed(float speed) {
        VehicleData::SpeedInfo data;
        data.current_speed(speed);
        data.average_speed(95.5f);
        data.timestamp(GetCurrentTimestamp());
        
        // 发送事件（内部使用 DDS DataWriter）
        GetSpeedChangedEvent().Send(data);
    }
};

int main() {
    // 初始化 Runtime (包含 DDS Participant)
    lap::com::Runtime::Initialize();
    
    // 创建服务实例
    VehicleSpeedServiceImpl service;
    
    // 提供服务（触发 DDS Topic 创建）
    service.OfferService();
    
    // 发布数据
    while (true) {
        service.PublishSpeed(GetCurrentSpeed());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return 0;
}
```

### 6.2 客户端 (Proxy) - 订阅 Event

```cpp
#include "lap/com/Runtime.hpp"
#include "VehicleSpeedProxy.hpp"

int main() {
    lap::com::Runtime::Initialize();
    
    // 查找服务（DDS Discovery）
    auto handle = lap::com::Runtime::FindService<VehicleSpeedProxy>(
        [](auto container) {
            if (!container.empty()) {
                std::cout << "Service found!" << std::endl;
            }
        }
    );
    
    // 创建代理
    VehicleSpeedProxy proxy(handle);
    
    // 订阅事件（内部创建 DDS DataReader）
    proxy.GetSpeedChangedEvent().Subscribe(10);
    proxy.GetSpeedChangedEvent().SetReceiveHandler(
        [](const VehicleData::SpeedInfo& data) {
            std::cout << "Speed: " << data.current_speed() << " km/h" << std::endl;
        }
    );
    
    // 保持运行
    std::this_thread::sleep_for(std::chrono::hours(24));
    
    return 0;
}
```

### 6.3 DDS Security 配置

**DDS Security 属性配置**:

```cpp
// 在 Runtime 初始化时配置 Security
DdsConnectionManager::ConfigureSecurity(
    "/opt/autosar/security/dds/ca/identity_ca.pem",
    "/opt/autosar/security/dds/ca/permissions_ca.pem",
    "/opt/autosar/security/dds/certs/instance1/cert.pem",
    "/opt/autosar/security/dds/certs/instance1/key.pem",
    "/opt/autosar/security/dds/governance/governance.xml",
    "/opt/autosar/security/dds/permissions/instance1/permissions.xml"
);
```

**Fast-DDS 属性文件** (XML):

```xml
<?xml version="1.0" encoding="UTF-8" ?>
<dds>
    <participant profile_name="secure_participant">
        <rtps>
            <name>VehicleSpeedService</name>
            <propertiesPolicy>
                <properties>
                    <property>
                        <name>dds.sec.auth.plugin</name>
                        <value>builtin.PKI-DH</value>
                    </property>
                    <property>
                        <name>dds.sec.auth.builtin.PKI-DH.identity_ca</name>
                        <value>file:///opt/autosar/security/dds/ca/identity_ca.pem</value>
                    </property>
                    <!-- 更多安全属性 -->
                </properties>
            </propertiesPolicy>
        </rtps>
    </participant>
</dds>
```

---

## 7. 性能基准

### 7.1 延迟

| 传输模式 | 平均延迟 | 99th 百分位 |
|---------|---------|------------|
| UDP (本地) | 50μs | 100μs |
| TCP (本地) | 80μs | 150μs |
| 共享内存 | 5μs | 10μs |

### 7.2 吞吐量

| 消息大小 | 共享内存 | UDP |
|---------|---------|-----|
| 64 字节 | 500K msg/s | 200K msg/s |
| 1 KB | 300K msg/s | 100K msg/s |
| 64 KB | 10K msg/s | 5K msg/s |

---

## 8. 参考文档

### 8.1 AUTOSAR 规范

- **AUTOSAR_AP_TR_DDSSecurityIntegration** - DDS Security 集成技术报告
- **AUTOSAR_AP_SWS_CommunicationManagement** - 通信管理软件规范

### 8.2 OMG 标准

- **DDS v1.4** - Data Distribution Service for Real-Time Systems
- **DDS Security v1.1** - DDS Security Specification
- **RTPS v2.3** - Real-Time Publish-Subscribe Protocol
- **IDL v4.2** - Interface Definition Language

### 8.3 Fast-DDS 文档

- [Fast-DDS Documentation](https://fast-dds.docs.eprosima.com/)
- [Fast-DDS Security](https://fast-dds.docs.eprosima.com/en/latest/fastdds/security/security.html)
- [Fast-DDS QoS](https://fast-dds.docs.eprosima.com/en/latest/fastdds/dds_layer/core/policy/policy.html)

---

## 9. 附录

### 9.1 DDS QoS 策略完整列表

| QoS 策略 | 说明 | 默认值 |
|---------|------|-------|
| RELIABILITY | 可靠性 | BEST_EFFORT |
| DURABILITY | 持久化 | VOLATILE |
| HISTORY | 历史记录 | KEEP_LAST(1) |
| DEADLINE | 更新周期 | INFINITE |
| LIVELINESS | 活性检测 | AUTOMATIC |
| OWNERSHIP | 数据所有权 | SHARED |
| DESTINATION_ORDER | 目的地排序 | BY_RECEPTION_TIMESTAMP |
| PRESENTATION | 呈现模式 | INSTANCE |
| PARTITION | 分区 | 空 |
| TIME_BASED_FILTER | 时间过滤 | 0 |
| LIFESPAN | 数据有效期 | INFINITE |
| RESOURCE_LIMITS | 资源限制 | 无限 |

### 9.2 故障排查

**问题**: Participant 无法发现其他 Participant

**解决方案**:
1. 检查 Domain ID 是否一致
2. 确认网络防火墙未阻止 UDP 端口 7400-7500
3. 验证多播路由配置

**问题**: 安全认证失败

**解决方案**:
1. 验证证书有效期
2. 检查 CA 证书路径
3. 确认 Governance/Permissions 文档格式正确

---

**文档版本**: 1.0.0  
**最后更新**: 2025-11-18  
**维护者**: LightAP Team
