# Com 模块开发重构计划

**创建日期**: 2025-11-19  
**基于文档**: ARCHITECTURE_SUMMARY.md (R24-11标准)  
**当前状态**: Phase 1 基础完成，需重构为统一DDS架构  
**目标**: AUTOSAR R24-11 完全合规 + 性能优化

---

## 📊 现状分析

### ✅ 已完成组件 (Phase 1)

| 组件 | 文件 | 行数 | 完成度 | 说明 |
|------|------|------|--------|------|
| **Runtime API** | `source/inc/Runtime.hpp` | 171 | 80% | 基础框架完成，缺少服务发现集成 |
| **Runtime实现** | `source/comapi/src/Runtime.cpp` | 150 | 60% | 仅初始化/清理，无实际服务管理 |
| **ServiceProxy** | `source/inc/ProxyBase.hpp` | ~250 | 100% | 头文件定义完整 |
| **ServiceSkeleton** | `source/inc/SkeletonBase.hpp` | ~270 | 100% | 头文件定义完整 |
| **Method** | `source/inc/Method.hpp` | ~450 | 100% | Fire&Forget/同步/异步接口完整 |
| **Event** | `source/inc/Event.hpp` | ~350 | 100% | 订阅/通知接口完整 |
| **Field** | `source/inc/Field.hpp` | ~550 | 100% | Getter/Setter/Notifier完整 |
| **ComTypes** | `source/inc/ComTypes.hpp` | ~400 | 100% | 类型系统完整 |

**总计**: ~2,591 行代码，核心API定义完成

### 🔄 Binding层现状（需重构）

#### 当前实现（旧架构）

| Binding | 文件 | 状态 | 问题 |
|---------|------|------|------|
| **D-Bus** | `source/binding/dbus/*.hpp` | ⚠️ 手动实现 | 延迟高(1ms)，吞吐低(50MB/s) |
| **SOME/IP** | `source/binding/someip/*.hpp` | ⚠️ 依赖vsomeip | 需要CommonAPI工具链 |
| **Protobuf+Socket** | `source/binding/socket/*.hpp` | ✅ 可用 | 仅本地IPC，无跨ECU能力 |
| **CommonAPI适配器** | `source/binding/commonapi/*.hpp` | ⚠️ 工具链复杂 | 增加构建依赖 |

**核心问题**:
1. ❌ **架构不统一**: 4种Binding各自实现，代码重复
2. ❌ **性能瓶颈**: D-Bus延迟1ms，SOME/IP需vsomeip
3. ❌ **维护复杂**: 多个传输协议栈，测试困难
4. ❌ **缺少DDS**: 无分布式能力，无法支持V2X场景

---

## 🎯 重构目标

### 核心理念：统一DDS中间件架构

```
┌────────────────────────────────────────────────────────────┐
│              ara::com API (统一接口层)                      │
└────────────────────────────────────────────────────────────┘
                          ↓
┌────────────────────────────────────────────────────────────┐
│              Unified DDS Bridge Layer                      │
│  ┌──────────────┬──────────────┬──────────────────────┐   │
│  │ D-Bus Bridge │ SOME/IP Bridge│ Native DDS Binding │   │
│  │ (兼容层)      │ (兼容层)       │ (原生)              │   │
│  └──────┬───────┴──────┬────────┴──────┬──────────────┘   │
│         │              │               │                  │
│         └──────────────┴───────────────┘                  │
│                        ↓                                  │
│              Fast-DDS Core (统一中间件)                    │
│  ┌────────────────────────────────────────────────────┐  │
│  │ - DDS Topics (统一消息路由)                         │  │
│  │ - QoS Policies (服务质量保证)                       │  │
│  │ - Discovery (统一服务发现)                          │  │
│  │ - Security (DDS-Security标准)                       │  │
│  └────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────┘
                          ↓
┌────────────────────────────────────────────────────────────┐
│              DDS Transport Layer                           │
│  ┌───────────────┬──────────────┬────────────────────┐    │
│  │ Shared Memory │ UDP/TCP      │ Custom Transports  │    │
│  │ (零拷贝,本地)  │ (跨ECU网络)   │ (CAN,特殊硬件)      │    │
│  └───────────────┴──────────────┴────────────────────┘    │
└────────────────────────────────────────────────────────────┘
```

### AUTOSAR R24-11 新特性支持

#### 1. 静态服务连接 (SWS_CM_02201-02203)
- ✅ **零延迟发现**: ARXML清单预配置
- ✅ **确定性部署**: 编译期服务拓扑
- ✅ **安全加固**: 减少动态攻击面

#### 2. 中央服务发现 (EXP 7.2.1)
- ✅ **性能优化**: Unix Socket <0.5ms查询
- ✅ **集中管理**: 系统级服务监控
- ✅ **平滑降级**: 自动回退到动态发现

#### 3. 三层服务发现策略
| 优先级 | 机制 | 延迟 | AUTOSAR标准 |
|--------|------|------|------------|
| P1 | 静态配置 | ~0ms | SWS_CM_02201 |
| P2 | 中央注册 | 0.5ms | EXP 7.2.1 |
| P3 | 动态发现 | 5-100ms | SWS_CM_00001 |

---

## 📅 重构路线图

### Phase 2: 统一DDS架构 (4-6周)

#### Week 1-2: DDS核心集成

**目标**: 建立Fast-DDS基础设施

| 任务 | 文件 | 行数估算 | 优先级 |
|------|------|----------|--------|
| DDS Domain管理 | `binding/dds/DdsDomainManager.hpp/cpp` | 300 | P0 |
| DDS Participant管理 | `binding/dds/DdsParticipant.hpp/cpp` | 250 | P0 |
| DDS Topic绑定 | `binding/dds/DdsTopicBinding.hpp/cpp` | 350 | P0 |
| DDS Publisher/Subscriber | `binding/dds/DdsPubSub.hpp/cpp` | 400 | P0 |
| DDS QoS策略映射 | `binding/dds/DdsQoSMapper.hpp/cpp` | 280 | P1 |

**交付物**:
- ✅ DDS Domain创建与管理
- ✅ 基础Pub/Sub通信
- ✅ Topic映射规则
- ✅ QoS策略配置

**验证标准**:
```cpp
// 测试用例
TEST(DdsBinding, BasicPubSub) {
    // 1. 创建DDS Domain
    auto domain = DdsDomainManager::CreateDomain(0);
    
    // 2. 创建Topic
    auto topic = domain->CreateTopic<SpeedData>("VehicleSpeed");
    
    // 3. 发布消息
    auto publisher = topic->CreatePublisher();
    SpeedData data{.speed = 120.5};
    publisher->Publish(data);
    
    // 4. 订阅消息
    auto subscriber = topic->CreateSubscriber();
    subscriber->SetCallback([](const SpeedData& data) {
        EXPECT_EQ(data.speed, 120.5);
    });
}
```

#### Week 3-4: D-Bus → DDS Bridge

**目标**: 将D-Bus通信桥接到DDS

| 任务 | 文件 | 行数估算 | 优先级 |
|------|------|----------|--------|
| D-Bus Message → DDS Topic映射 | `binding/dbus_dds/DbusDdsBridge.hpp/cpp` | 600 | P0 |
| D-Bus Signal → DDS Event适配 | `binding/dbus_dds/DbusSignalAdapter.hpp/cpp` | 350 | P0 |
| D-Bus Method → DDS Req/Reply | `binding/dbus_dds/DbusMethodAdapter.hpp/cpp` | 450 | P0 |
| sdbus-c++兼容层 | `binding/dbus_dds/DbusCompatLayer.hpp/cpp` | 250 | P1 |
| 消息编解码器 | `binding/dbus_dds/DbusMessageCodec.hpp/cpp` | 400 | P0 |

**映射规则**:

| D-Bus概念 | DDS概念 | 实现 |
|----------|---------|------|
| Bus Name | Domain ID | Hash(BusName) % 230 |
| Object Path | Topic Prefix | `DBus_<path>_<interface>` |
| Method Call | Request/Reply Topics | `DBus_<method>_Req/Rep` |
| Signal | Single Topic | `DBus_<signal>` |
| Property | Request/Reply + Notify Topic | `DBus_<prop>_Get/Set/Changed` |

**性能目标**:
- 延迟: 1ms → <10μs (Shared Memory)
- 吞吐量: 50MB/s → 1.2GB/s
- CPU占用: 8% → 2%

**交付物**:
- ✅ D-Bus消息自动转换为DDS Topics
- ✅ 向后兼容sdbus-c++ API
- ✅ 性能测试报告

#### Week 5-6: SOME/IP → DDS Bridge

**目标**: SOME/IP协议桥接到DDS

| 任务 | 文件 | 行数估算 | 优先级 |
|------|------|----------|--------|
| SOME/IP → DDS协议转换 | `binding/someip_dds/SomeIpDdsBridge.hpp/cpp` | 800 | P0 |
| Service Discovery适配 | `binding/someip_dds/SomeIpSDAdapter.hpp/cpp` | 350 | P0 |
| 消息编解码 | `binding/someip_dds/SomeIpCodec.hpp/cpp` | 600 | P0 |
| vsomeip兼容层 | `binding/someip_dds/VsomeipCompatLayer.hpp/cpp` | 400 | P1 |
| DDS Service映射 | `binding/someip_dds/DdsServiceMapper.hpp/cpp` | 500 | P0 |

**映射规则**:

| SOME/IP概念 | DDS概念 | 实现 |
|------------|---------|------|
| Service ID | Topic Name | `SOMEIP_<ServiceID>_<MethodID>` |
| Method | Request/Reply Topics | Req/Rep分离 |
| Event | Single Topic | DDS Pub/Sub |
| Eventgroup | Content Filter | DDS Filter Expression |
| Field | Request/Reply + Notify | 组合模式 |

**交付物**:
- ✅ SOME/IP消息自动桥接
- ✅ 保持vsomeip配置兼容性
- ✅ 服务发现协议适配

**验证标准**:
```cpp
TEST(SomeIpDdsBridge, MethodCall) {
    // 使用SOME/IP客户端
    auto client = someip::CreateClient(0x1234, 0x5678);
    
    // 调用方法（自动桥接到DDS）
    auto response = client->CallMethod(0x01, {param1, param2});
    
    // DDS端验证收到请求
    EXPECT_TRUE(dds_bridge->ReceivedRequest(0x1234, 0x01));
}
```

### Phase 3: 服务发现架构升级 (3-4周)

#### Week 7-8: 静态服务连接 (SWS_CM_02201)

**目标**: ARXML清单加载与静态实例管理

| 任务 | 文件 | 行数估算 | 优先级 |
|------|------|----------|--------|
| ARXML清单解析器 | `src/manifest/ManifestParser.hpp/cpp` | 500 | P0 |
| 静态实例管理器 | `src/discovery/StaticInstanceManager.hpp/cpp` | 350 | P0 |
| 静态配置加载器 | `src/discovery/StaticConfigLoader.hpp/cpp` | 280 | P0 |
| 端点配置映射 | `src/manifest/EndpointMapper.hpp/cpp` | 220 | P1 |

**ARXML示例** (TPS_MANI_03312-03315):
```xml
<?xml version="1.0" encoding="UTF-8"?>
<AUTOSAR>
  <StaticServiceInstances>
    <StaticServiceInstance uuid="12345678-1234">
      <ServiceInterface>VehicleSpeed</ServiceInterface>
      <InstanceId>1</InstanceId>
      <Endpoint>
        <TransportProtocol>DDS</TransportProtocol>
        <DomainId>0</DomainId>
        <TopicName>VehicleSpeed_Instance1</TopicName>
      </Endpoint>
      <QoSProfile>
        <Reliability>RELIABLE</Reliability>
        <Durability>TRANSIENT_LOCAL</Durability>
      </QoSProfile>
    </StaticServiceInstance>
  </StaticServiceInstances>
</AUTOSAR>
```

**FindService重构** (三层发现):
```cpp
// Runtime.hpp
template<typename ServiceInterface>
ServiceHandleContainer<typename ServiceInterface::HandleType> 
FindService(InstanceSpecifier instanceId) noexcept {
    ServiceHandleContainer<HandleType> handles;
    
    // 1. 优先级P1: 静态配置 (~0ms)
    auto static_handles = StaticConfigLoader::GetInstances<ServiceInterface>(instanceId);
    if (!static_handles.empty()) {
        return static_handles;  // 零延迟路径
    }
    
    // 2. 优先级P2: 中央注册表 (~0.5ms)
    if (CentralRegistryClient::IsAvailable()) {
        auto registry_handles = CentralRegistryClient::FindService<ServiceInterface>(instanceId);
        if (!registry_handles.empty()) {
            return registry_handles;  // 低延迟路径
        }
    }
    
    // 3. 优先级P3: 动态发现 (5-100ms)
    // D-Bus/SOME/IP-SD/DDS Discovery
    return DynamicDiscovery::FindService<ServiceInterface>(instanceId);
}
```

**交付物**:
- ✅ ARXML清单解析器
- ✅ 静态实例零延迟查找
- ✅ 配置文件验证工具

#### Week 9-10: 中央服务注册表 (EXP 7.2.1)

**目标**: 集中式服务发现守护进程

| 任务 | 文件 | 行数估算 | 优先级 |
|------|------|----------|--------|
| 中央注册表守护进程 | `daemon/CentralServiceRegistry.cpp` | 800 | P0 |
| Protobuf IPC通信 | `src/discovery/RegistryClient.hpp/cpp` | 450 | P0 |
| 服务注册/查询API | `src/discovery/RegistryProtocol.proto` | 200 | P0 |
| 自动降级机制 | `src/discovery/FallbackDiscovery.hpp/cpp` | 300 | P1 |
| 监控与统计 | `daemon/RegistryMetrics.hpp/cpp` | 250 | P2 |

**架构**:
```
┌─────────────────────────────────────────────┐
│   应用进程 A          应用进程 B              │
│   FindService()       OfferService()         │
│        │                   │                 │
│        └───────┬───────────┘                 │
│                ↓                             │
│   ┌────────────────────────────┐            │
│   │ CentralRegistryClient      │            │
│   │ (Unix Domain Socket)       │            │
│   └────────────┬───────────────┘            │
└────────────────│────────────────────────────┘
                 │ Protobuf Messages
                 │ (~0.5ms latency)
                 ↓
┌─────────────────────────────────────────────┐
│   CentralServiceRegistry Daemon             │
│   ┌───────────────────────────────────┐    │
│   │ Service Registry                  │    │
│   │ - Map<ServiceID, InstanceList>   │    │
│   │ - Subscriptions                   │    │
│   │ - Health Monitoring               │    │
│   └───────────────────────────────────┘    │
│                                             │
│   Fallback: D-Bus Discovery / SOME/IP-SD   │
└─────────────────────────────────────────────┘
```

**Protobuf协议定义**:
```protobuf
// RegistryProtocol.proto
syntax = "proto3";

message FindServiceRequest {
    string service_name = 1;
    uint32 instance_id = 2;
}

message ServiceInstance {
    string service_name = 1;
    uint32 instance_id = 2;
    string endpoint_address = 3;  // DDS Topic / Unix Socket path
    uint32 version_major = 4;
    uint32 version_minor = 5;
}

message FindServiceResponse {
    repeated ServiceInstance instances = 1;
}

message OfferServiceRequest {
    ServiceInstance instance = 1;
}

message OfferServiceResponse {
    bool success = 1;
    string error_message = 2;
}
```

**交付物**:
- ✅ 守护进程（可独立运行）
- ✅ Protobuf IPC协议
- ✅ 性能测试：FindService < 0.5ms

### Phase 4: DDS Security集成 (2-3周)

#### Week 11-12: DDS Security Plugin

**目标**: AUTOSAR TR DDS Security集成

| 任务 | 文件 | 行数估算 | 优先级 |
|------|------|----------|--------|
| 证书管理器 | `binding/dds/security/DdsCertManager.hpp/cpp` | 400 | P0 |
| Governance文档解析 | `binding/dds/security/GovernanceParser.hpp/cpp` | 350 | P0 |
| Permissions文档解析 | `binding/dds/security/PermissionsParser.hpp/cpp` | 350 | P0 |
| 安全配置加载 | `binding/dds/security/SecurityConfig.hpp/cpp` | 280 | P1 |
| 密钥管理 | `binding/dds/security/KeyManager.hpp/cpp` | 320 | P1 |

**证书目录结构** (TR_DDSS_00001):
```
artifacts/dds_security/
├── ca/
│   ├── identity_ca.pem         # 身份CA证书
│   ├── permissions_ca.pem      # 权限CA证书
│   └── ca_key.pem             # CA私钥
├── certs/
│   ├── instance_cert.pem       # 服务实例证书
│   └── instance_key.pem        # 服务实例私钥
├── governance.xml              # 治理文档（域规则）
├── permissions.xml             # 权限文档（访问控制）
└── README.md                   # 证书部署指南
```

**Governance.xml示例** (TR_DDSS_00006):
```xml
<?xml version="1.0" encoding="UTF-8"?>
<dds xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <domain_access_rules>
    <domain_rule>
      <domains>
        <id>0</id>  <!-- DDS Domain 0 -->
      </domains>
      <allow_unauthenticated_participants>false</allow_unauthenticated_participants>
      <enable_join_access_control>true</enable_join_access_control>
      <discovery_protection_kind>ENCRYPT</discovery_protection_kind>
      <liveliness_protection_kind>SIGN</liveliness_protection_kind>
      <rtps_protection_kind>SIGN_WITH_ORIGIN_AUTHENTICATION</rtps_protection_kind>
    </domain_rule>
  </domain_access_rules>
</dds>
```

**Permissions.xml示例** (TR_DDSS_00007):
```xml
<?xml version="1.0" encoding="UTF-8"?>
<dds xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <permissions>
    <grant name="VehicleSpeedPublisher">
      <subject_name>CN=VehicleController</subject_name>
      <validity>
        <not_before>2025-01-01T00:00:00</not_before>
        <not_after>2026-12-31T23:59:59</not_after>
      </validity>
      <allow_rule>
        <domains>
          <id>0</id>
        </domains>
        <publish>
          <topics>
            <topic>VehicleSpeed_*</topic>  <!-- 允许发布VehicleSpeed主题 -->
          </topics>
        </publish>
      </allow_rule>
    </grant>
  </permissions>
</dds>
```

**交付物**:
- ✅ DDS Security插件集成
- ✅ 证书生成工具
- ✅ 安全测试套件

#### Week 13: 端到端安全测试

**测试场景**:
1. 认证测试：无效证书拒绝连接
2. 加密测试：DDS消息加密传输
3. 授权测试：权限控制验证
4. 性能测试：安全开销 < 5%

---

## 📁 新增文件清单

### DDS Core Binding (Week 1-2)

```
source/binding/dds/
├── DdsDomainManager.hpp          (300行) - DDS Domain管理
├── DdsDomainManager.cpp
├── DdsParticipant.hpp            (250行) - Participant生命周期
├── DdsParticipant.cpp
├── DdsTopicBinding.hpp           (350行) - Topic创建与映射
├── DdsTopicBinding.cpp
├── DdsPubSub.hpp                 (400行) - Publisher/Subscriber
├── DdsPubSub.cpp
├── DdsQoSMapper.hpp              (280行) - QoS策略映射
└── DdsQoSMapper.cpp
```

### D-Bus DDS Bridge (Week 3-4)

```
source/binding/dbus_dds/
├── DbusDdsBridge.hpp             (600行) - 主桥接器
├── DbusDdsBridge.cpp
├── DbusSignalAdapter.hpp         (350行) - Signal适配
├── DbusSignalAdapter.cpp
├── DbusMethodAdapter.hpp         (450行) - Method适配
├── DbusMethodAdapter.cpp
├── DbusCompatLayer.hpp           (250行) - sdbus-c++兼容
├── DbusCompatLayer.cpp
├── DbusMessageCodec.hpp          (400行) - 消息编解码
└── DbusMessageCodec.cpp
```

### SOME/IP DDS Bridge (Week 5-6)

```
source/binding/someip_dds/
├── SomeIpDdsBridge.hpp           (800行) - 主桥接器
├── SomeIpDdsBridge.cpp
├── SomeIpSDAdapter.hpp           (350行) - SD适配
├── SomeIpSDAdapter.cpp
├── SomeIpCodec.hpp               (600行) - 编解码
├── SomeIpCodec.cpp
├── VsomeipCompatLayer.hpp        (400行) - vsomeip兼容
├── VsomeipCompatLayer.cpp
├── DdsServiceMapper.hpp          (500行) - 服务映射
└── DdsServiceMapper.cpp
```

### Static Service Connection (Week 7-8)

```
source/manifest/
├── ManifestParser.hpp            (500行) - ARXML解析
├── ManifestParser.cpp
├── EndpointMapper.hpp            (220行) - 端点映射
└── EndpointMapper.cpp

source/discovery/
├── StaticInstanceManager.hpp     (350行) - 静态实例管理
├── StaticInstanceManager.cpp
├── StaticConfigLoader.hpp        (280行) - 配置加载器
└── StaticConfigLoader.cpp
```

### Central Service Registry (Week 9-10)

```
daemon/
├── CentralServiceRegistry.cpp    (800行) - 守护进程主程序
├── RegistryMetrics.hpp           (250行) - 监控统计
└── RegistryMetrics.cpp

source/discovery/
├── RegistryClient.hpp            (450行) - 注册表客户端
├── RegistryClient.cpp
├── RegistryProtocol.proto        (200行) - Protobuf协议
├── FallbackDiscovery.hpp         (300行) - 降级机制
└── FallbackDiscovery.cpp
```

### DDS Security (Week 11-13)

```
source/binding/dds/security/
├── DdsCertManager.hpp            (400行) - 证书管理
├── DdsCertManager.cpp
├── GovernanceParser.hpp          (350行) - Governance解析
├── GovernanceParser.cpp
├── PermissionsParser.hpp         (350行) - Permissions解析
├── PermissionsParser.cpp
├── SecurityConfig.hpp            (280行) - 安全配置
├── SecurityConfig.cpp
├── KeyManager.hpp                (320行) - 密钥管理
└── KeyManager.cpp

artifacts/dds_security/
├── ca/                           (证书颁发机构)
├── certs/                        (实例证书)
├── governance.xml                (治理文档)
├── permissions.xml               (权限文档)
└── README.md                     (部署指南)
```

**新增代码量统计**:
- DDS Core: ~1,580行
- D-Bus Bridge: ~2,050行
- SOME/IP Bridge: ~2,650行
- Static Service: ~1,350行
- Central Registry: ~2,000行
- DDS Security: ~1,700行
- **总计**: ~11,330行新代码

---

## 🔧 CMake构建系统更新

### 新增依赖

```cmake
# CMakeLists.txt 更新

# DDS依赖
find_package(fastcdr REQUIRED)
find_package(fastrtps REQUIRED)

# Protobuf依赖
find_package(Protobuf REQUIRED)

# 可选：DDS Security
option(ENABLE_DDS_SECURITY "Enable DDS Security features" ON)
if(ENABLE_DDS_SECURITY)
    find_package(OpenSSL REQUIRED)
endif()

# 新增库目标
add_library(lap_com_dds SHARED
    source/binding/dds/DdsDomainManager.cpp
    source/binding/dds/DdsParticipant.cpp
    source/binding/dds/DdsTopicBinding.cpp
    source/binding/dds/DdsPubSub.cpp
    source/binding/dds/DdsQoSMapper.cpp
)

target_link_libraries(lap_com_dds
    PRIVATE
        fastcdr
        fastrtps
        lap_core
)

# D-Bus DDS Bridge
add_library(lap_com_dbus_bridge SHARED
    source/binding/dbus_dds/DbusDdsBridge.cpp
    source/binding/dbus_dds/DbusSignalAdapter.cpp
    source/binding/dbus_dds/DbusMethodAdapter.cpp
    source/binding/dbus_dds/DbusCompatLayer.cpp
    source/binding/dbus_dds/DbusMessageCodec.cpp
)

target_link_libraries(lap_com_dbus_bridge
    PRIVATE
        lap_com_dds
        sdbus-c++
)

# SOME/IP DDS Bridge
add_library(lap_com_someip_bridge SHARED
    source/binding/someip_dds/SomeIpDdsBridge.cpp
    source/binding/someip_dds/SomeIpSDAdapter.cpp
    source/binding/someip_dds/SomeIpCodec.cpp
    source/binding/someip_dds/VsomeipCompatLayer.cpp
    source/binding/someip_dds/DdsServiceMapper.cpp
)

target_link_libraries(lap_com_someip_bridge
    PRIVATE
        lap_com_dds
        vsomeip3
)

# Central Service Registry Daemon
add_executable(lap_com_registry_daemon
    daemon/CentralServiceRegistry.cpp
    daemon/RegistryMetrics.cpp
)

target_link_libraries(lap_com_registry_daemon
    PRIVATE
        lap_com
        lap_core
        protobuf::libprotobuf
)
```

---

## ✅ 验证与测试

### 单元测试 (每个组件)

```cpp
// test/dds/DdsPubSubTest.cpp
TEST(DdsBinding, BasicPubSub) {
    auto domain = DdsDomainManager::CreateDomain(0);
    auto topic = domain->CreateTopic<SpeedData>("VehicleSpeed");
    
    auto publisher = topic->CreatePublisher();
    auto subscriber = topic->CreateSubscriber();
    
    bool received = false;
    subscriber->SetCallback([&](const SpeedData& data) {
        EXPECT_EQ(data.speed, 120.5);
        received = true;
    });
    
    publisher->Publish(SpeedData{.speed = 120.5});
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(received);
}
```

### 性能基准测试

| 测试场景 | 目标 | 当前(D-Bus) | DDS目标 |
|---------|------|------------|---------|
| 小消息延迟(64B) | <10μs | ~1ms | **<10μs** |
| 大消息延迟(1MB) | <100μs | ~20ms | **<50μs** |
| 吞吐量(1KB消息) | >500MB/s | ~50MB/s | **>1GB/s** |
| CPU占用(idle) | <2% | ~8% | **<2%** |
| 服务发现延迟 | <1ms | 5-50ms | **<0.5ms** |

### 集成测试

```bash
# 1. 启动中央注册表守护进程
./lap_com_registry_daemon &

# 2. 启动服务提供者（Skeleton）
./vehicle_speed_provider &

# 3. 启动服务消费者（Proxy）
./vehicle_speed_consumer

# 4. 验证日志
# [Registry] Service VehicleSpeed registered (Instance 1)
# [Provider] Offered service VehicleSpeed
# [Consumer] Found service VehicleSpeed (latency: 0.3ms)
# [Consumer] Received speed: 120.5 km/h
```

---

## 📊 风险评估

| 风险项 | 影响 | 概率 | 缓解措施 |
|--------|------|------|----------|
| Fast-DDS学习曲线 | 中 | 高 | 提前技术预研，参考官方示例 |
| 性能未达标 | 高 | 中 | 分阶段性能测试，及时优化 |
| Bridge兼容性问题 | 中 | 中 | 保留旧Binding作为降级方案 |
| DDS Security复杂度 | 中 | 高 | 使用Fast-DDS官方插件 |
| 时间超期 | 高 | 中 | 按周迭代，可延期非核心功能 |

---

## 📝 下一步行动

### 立即执行（本周）

1. ✅ **Week 1任务启动**: 创建DDS Core Binding目录结构
2. ✅ **环境准备**: 安装Fast-DDS开发环境
   ```bash
   # Ubuntu/Debian
   sudo apt install libfastrtps-dev libfastcdr-dev
   
   # 或从源码编译最新版
   git clone https://github.com/eProsima/Fast-DDS.git
   cd Fast-DDS && mkdir build && cd build
   cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
   make -j$(nproc) && sudo make install
   ```
3. ✅ **技术预研**: 运行Fast-DDS HelloWorld示例
4. ✅ **代码框架**: 创建DdsDomainManager.hpp骨架

### 本周交付（Week 1结束）

- [ ] DDS Domain创建与销毁
- [ ] DDS Participant生命周期管理
- [ ] 基础单元测试通过

### 下周计划（Week 2）

- [ ] DDS Topic创建与QoS配置
- [ ] DataWriter/DataReader绑定
- [ ] 第一个Pub/Sub示例运行成功

---

**负责人**: LightAP Com模块开发团队  
**审核**: AUTOSAR架构师  
**预期完成**: 2025-12-31 (Phase 2完成)  
**状态跟踪**: 每周五下午Code Review + 进度同步

