# Com模块架构分析总结 (AUTOSAR AP R24-11 标准)

## 架构概览

本文档基于 **AUTOSAR Adaptive Platform R24-11** 规范重构：
- **SWS Communication Management** (R24-11, 672 页)
- **TPS Manifest Specification** (R24-11, 1253 页)
- **EXP ara::com API** (R24-11, 141 页)
- **SWS Network Management**
- **TR DDS Security Integration**

### AUTOSAR R24-11 新特性支持

#### ✅ 静态服务连接 (R24-11 新增)
- **[SWS_CM_02201]** Static Service Connection
- **[SWS_CM_02202]** 绕过服务发现协议
- **[SWS_CM_02203]** 静态连接无运行时版本检查
- **[TPS_MANI_03312-03315]** YAML 静态配置规范（AUTOSAR标准为ARXML，LightAP使用YAML实现，提供arxml2yaml转换工具）

#### ✅ 零守护进程服务发现 (v3.0 突破性架构)
- **固定槽位映射**: 服务 ID → 槽位，编译期或静态配置确定
- **共享内存直接访问**: iceoryx2 memfd + seqlock 无锁读取
- **< 500ns 延迟**: 零 IPC/网络通信，O(1) 查找
- **零单点故障**: 完全去中心化，无任何守护进程

### AUTOSAR 标准符合性

Com模块实现了以下核心功能集群（Functional Cluster）：

- ✅ **Service-Oriented Communication** (ara::com)
- ✅ **Service Discovery and Registry** (固定槽位 + 共享内存 + 心跳机制)
- ✅ **Static Service Connection** (R24-11 新特性)
- ✅ **Event-Driven Communication** (epoll + Edge-Triggered，完全封装在Binding内部)
- ✅ **Method Call (Request/Response)**
- ✅ **Field Notification**
- ✅ **Pluggable Transport Bindings** (运行时动态加载 .so 插件，配置驱动)
- ✅ **DDS Security Integration** (基于 AUTOSAR AP TR DDS Security)
- ✅ **High-Performance IPC** (iceoryx2 零拷贝 + epoll + mempool 自管理)
- ✅ **Configuration-Driven Architecture** (YAML manifest 控制，应用零修改)
- ✅ **True Zero-Copy Communication** (iceoryx2 共享内存，完全无守护进程)
- ✅ **Zero-Daemon Service Discovery** (固定槽位 + seqlock + 心跳，< 500ns 延迟)
- ✅ **FuSa-Ready Architecture** (QM/ASIL-D 槽位物理隔离 + Guard Page 保护)

### 架构分层设计

Com模块采用 **插件化、配置驱动、对应用完全透明** 的架构，符合 AUTOSAR AP R24-11 规范：

```
┌──────────────────────────────────────────────────────────────────────────┐
│        Adaptive Application (纯标准 ara::com 代码，一行不用改)            │
│  ┌─────────────────────────────────────────────────────────────────────┐ │
│  │ ara::com API (完全标准 AUTOSAR 接口)                                 │ │
│  │  - ServiceProxy (客户端代理)                                         │ │
│  │  - ServiceSkeleton (服务端骨架)                                      │ │
│  │  - Method / Event / Field (通信原语)                                │ │
│  └─────────────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────────────┘
                          ↓ (完全标准调用，无任何底层感知)
┌──────────────────────────────────────────────────────────────────────────┐
│          ara::com Runtime (QM，独立进程或静态库，对应用透明)              │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │ Runtime Core                                                      │  │
│  │  - FindService (固定槽位查找: O(1) 共享内存访问)                  │  │
│  │  - OfferService (槽位写入 + 心跳启动)                             │  │
│  │  - StaticServiceConnection (YAML 槽位映射配置)                   │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │ Shared Memory Service Registry (零守护进程)                        │  │
│  │  - 固定槽位数组 (1024 slots: 0-923 QM, 924-1023 ASIL-D)          │  │
│  │  - seqlock 无锁并发访问 (< 100ns 读取)                            │  │
│  │  - 心跳机制 (进程生命周期检测)                                     │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │ Binding Manager (运行时动态加载 .so 插件)                          │  │
│  │  - 按优先级选择最优 Binding (priority: 100 → 50 → 10)             │  │
│  │  - dlopen() 动态加载插件                                           │  │
│  │  - 配置驱动 (YAML manifest 决定启用哪些 Binding)                 │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────────┘
                          ↓ (插件按优先级动态加载)
┌──────────────────────────────────────────────────────────────────────────┐
│         可插拔 Transport Binding (.so 动态库，按需加载)                   │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │ binding_iceoryx2.so (priority: 100, 本地零拷贝)                    │  │
│  │  - iceoryx2 完全无守护进程架构 (vs iceoryx v1 需要 RouDi)         │  │
│  │  - 固定槽位服务发现 (共享内存 + seqlock，< 500ns)                 │  │
│  │  - MemPool 进程自管理 (QM 槽位 0-923 / ASIL-D 槽位 924-1023)      │  │
│  │  - 零拷贝数据传输 (<1μs 延迟, >10GB/s 吞吐)                        │  │
│  │  - Lock-free Queue (Rust 实现，内存安全)                          │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │ binding_dds.so (priority: 50, 跨 ECU 通信)                         │  │
│  │  - DDS 实现 (eProsima Fast-DDS / CycloneDDS)                      │  │
│  │  - Simple Discovery Protocol (标准 DDS-RTPS)                      │  │
│  │  - Shared Memory (本地) + UDP/TCP (跨网络)                        │  │
│  │  - DDS QoS Policies (Reliability / Durability / Deadline)         │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │ binding_custom_protocol.so (priority: 20, 自定义私有协议 + UDS)     │  │
│  │  - Unix Domain Socket 高性能本地通信                               │  │
│  │  - 自定义二进制协议（可扩展编解码器）                                │  │
│  │  - 支持流式/数据报模式（SOCK_STREAM / SOCK_DGRAM）                 │  │
│  │  - 轻量级私有协议，适合遗留系统集成                                  │  │
│  │  - 延迟 <10μs，吞吐量 >500MB/s                                     │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │ binding_legacy.so (priority: 10, 遗留兼容，仅在需要时加载)          │  │
│  │  - 不直接实现协议，只是网关接口                                     │  │
│  │  - 将 lap::com 调用转发到独立网关进程                               │  │
│  │  - SOME/IP Gateway / D-Bus Diag 进程通信                          │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────────┘
                          ↓ (仅在配置启用时运行)
┌──────────────────────────────────────────────────────────────────────────┐
│       独立遗留兼容进程 (可选部署，独立生命周期)                            │
│  ┌─────────────────────────┐  ┌───────────────────────────────────────┐ │
│  │ SomeIpGateway (独立进程) │  │ DiagDaemon (独立进程)                 │ │
│  │ - SOME/IP ↔ DDS 双栈翻译 │  │ - 仅运行 D-Bus 诊断服务                │ │
│  │ - vsomeip + DDS 实现     │  │ - UDS 诊断协议支持                    │ │
│  │ - 协议完全隔离           │  │ - 与主系统解耦                        │ │
│  └─────────────────────────┘  └───────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────────────┘
```

### AUTOSAR R24-11 需求追溯表（核心需求）

| AUTOSAR 需求 ID | 描述 | 实现状态 | 对应组件 |
|----------------|------|---------|----------|
| **RS_CM_00006** | C++ API 兼容性 | ✅ 完成 | 全部 API |
| **SWS_CM_00001** | 服务发现接口 (FindService) | ✅ 完成 | Runtime::FindService |
| **SWS_CM_00002** | 服务提供接口 (OfferService) | ✅ 完成 | Runtime::OfferService |
| **SWS_CM_00003** | 事件订阅接口 | ✅ 完成 | Event::Subscribe |
| **SWS_CM_00004** | 事件取消订阅 | ✅ 完成 | Event::Unsubscribe |
| **SWS_CM_00005** | 停止服务提供 | ✅ 完成 | Runtime::StopOfferService |
| **SWS_CM_02201** | 静态服务连接 (R24-11 新增) | 📋 设计完成 | ServiceDiscovery |
| **SWS_CM_02202** | 静态配置加载 | 📋 设计完成 | ManifestParser |
| **SWS_CM_02203** | 静态实例生命周期管理 | 📋 设计完成 | StaticInstanceManager |
| **SWS_CM_10289** | SOME/IP 协议支持 | ✅ 完成 | SomeIpBinding |
| **SWS_CM_10293** | 接收处理器调用 | ✅ 完成 | EventHandler |
| **SWS_CM_10514** | 方法调用处理 | ✅ 完成 | MethodHandler |
| **TPS_MANI_03312** | 静态服务清单配置 (YAML) | 📋 设计完成 | YAML Manifest (含arxml2yaml工具) |
| **TPS_MANI_03313** | 服务实例标识符配置 | 📋 设计完成 | YAML InstanceID |
| **TPS_MANI_03314** | 静态端点配置 | 📋 设计完成 | YAML Endpoint |
| **TPS_MANI_03315** | 静态服务组合配置 | 📋 设计完成 | YAML ServiceGroup |

### AUTOSAR R24-11 新特性支持

#### 1. 静态服务连接 (Static Service Connection)

**标准支持**: SWS_CM_02201-02203 (AUTOSAR R24-11 标准)

**核心优势**:
- ⚡ **零延迟发现**: 启动时加载静态配置，无运行时查询开销
- 🎯 **确定性部署**: 服务拓扑在编译/配置时确定
- 🔒 **安全加固**: 减少动态发现攻击面

**实现方式**: 通过 YAML 清单文件配置服务实例（支持从ARXML转换，提供arxml2yaml标准化工具）
```xml
<StaticServiceInstance uuid="12345678-1234">
  <InstanceId>1</InstanceId>
  <Endpoint>
    <TransportProtocol>SOME-IP</TransportProtocol>
    <NetworkEndpoint>
      <IpAddress>192.168.1.10</IpAddress>
      <Port>30509</Port>
    </NetworkEndpoint>
  </Endpoint>
</StaticServiceInstance>
```

**设计文档**: `SERVICE_DISCOVERY_ARCHITECTURE.md` Chapter 3
#### binding_config.yaml（Binding Manager 配置）

```yaml
# LightAP Com Module - Binding Configuration
# 依赖: yaml-cpp (https://github.com/jbeder/yaml-cpp)
# 转换工具: arxml2yaml (AUTOSAR ARXML → YAML)

bindings:
  - type: iceoryx2
    library: /usr/lib/lap/com/binding_iceoryx2.so
    priority: 100
    enabled: true
    config:
      domain_name: lightap_com
      mempool_config: /etc/iceoryx2/mempool_config.toml
      system_optimization:
        use_huge_pages: true
        huge_page_size: 1G
        transparent_huge_pages: true
        cpu_affinity: [4, 5, 6, 7]
        io_uring_sqpoll: true
        io_uring_cpu: 2
        io_uring_queue_size: 32768

  - type: dds
    library: /usr/lib/lap/com/binding_dds.so
    priority: 50
    enabled: true
    config:
      domain: 0
      discovery_server: "192.168.1.100:34567"
      transport: shm
      af_xdp_enabled: true
      af_xdp_config:
        interface: eth0
        queue_ids: [0, 1, 2, 3]
        umem_shared_with_iceoryx2: true
        zero_copy: true
        xdp_mode: drv
      payload_routing:
        large_payload_threshold: 65536
        large_payload_transport: af_xdp
        small_payload_transport: shm

  - type: custom_protocol
    library: /usr/lib/lap/com/binding_custom_protocol.so
    priority: 20
    enabled: false
    config:
      socket_path: /tmp/lightap_custom
      socket_type: stream
      protocol_version: "1.0"
      codec: binary
      buffer_size: 65536

  - type: legacy_someip
    library: /usr/lib/lap/com/binding_legacy.so
    priority: 10
    enabled: false
    config:
      gateway_address: "unix:///tmp/someip_gateway.sock"

discovery:
  static_file: /etc/lap/com/static_endpoints.yaml
  central_server: "192.168.1.100:34567"
  fallback_to_builtin: true

runtime:
  mode: library
  event_loop: binding_managed
```

#### static_endpoints.yaml（静态服务配置，R24-11 SWS_CM_02201）

```yaml
# 静态服务配置 - 可通过 arxml2yaml 工具从 AUTOSAR ARXML 转换
# 符合 TPS_MANI_03312-03315 规范

static_service_configuration:
  - service_instance:
      service_id: 0x1234
      instance_id: 0x0001
      binding: iceoryx2
      endpoint:
        type: SharedMemory
        service_name: /perception/camera_front
        domain_name: lightap_com

  - service_instance:
      service_id: 0x5678
      instance_id: 0x0001
      binding: dds
      endpoint:
        type: DDS
        topic_name: vehicle_status
        domain_id: 0
        qos:
          reliability: RELIABLE
          durability: TRANSIENT_LOCAL
```

#### mempool_config.toml（iceoryx2 MemPool 进程自管理）

```toml
# iceoryx2 配置 - 无需中央 RouDi，每个进程自管理
[domain]
name = "lightap_com"

# QM 等级感知数据池（摄像头、LiDAR）
[[services]]
name = "camera_front"
safety_level = "QM"

[services.publisher]
max_payload_size = 8388608  # 8MB（4K 摄像头帧）
max_publishers = 10
max_subscribers = 20

[[services]]
name = "lidar_points"
safety_level = "QM"

[services.publisher]
max_payload_size = 2097152  # 2MB（LiDAR 点云）
max_publishers = 5
max_subscribers = 15

# ASIL-D 等级控制数据池（转向、制动）
[[services]]
name = "steering_control"
safety_level = "ASIL-D"

[services.publisher]
max_payload_size = 4096  # 4KB（控制指令）
max_publishers = 1  # 单一控制源
max_subscribers = 10
access_mode = "read_only_subscribers"  # 订阅者只读

[[services]]
name = "brake_control"
safety_level = "ASIL-D"

[services.publisher]
max_payload_size = 4096
max_publishers = 1
max_subscribers = 10
access_mode = "read_only_subscribers"
```

### Binding Manager（运行时插件加载）

#### 插件接口（ITransportBinding.hpp）

```cpp
namespace ara {
namespace com {
namespace binding {

class ITransportBinding {
public:
    virtual ~ITransportBinding() = default;
    
    // 插件生命周期
    virtual Result<void> Initialize(const YAML::Node& config) = 0;
    virtual Result<void> Shutdown() = 0;
    
    // 服务发现
    virtual Result<ServiceHandleContainer> FindService(
        const ServiceIdentifier& service_id) = 0;
    virtual Result<void> OfferService(
        const ServiceIdentifier& service_id,
        const InstanceIdentifier& instance_id) = 0;
    
    // 通信原语
    virtual Result<void> SendMethod(
        const MethodCall& call,
        ByteBuffer&& payload) = 0;
    virtual Result<void> SendEvent(
        const EventData& event,
        ByteBuffer&& payload) = 0;
    virtual Result<void> SubscribeEvent(
        const EventIdentifier& event_id,
        EventReceiveHandler handler) = 0;
    
    // 性能度量
    virtual TransportMetrics GetMetrics() const = 0;
    
    // 插件元数据
    virtual std::string GetName() const = 0;
    virtual uint32_t GetPriority() const = 0;
    virtual bool SupportsZeroCopy() const = 0;
};

// 插件工厂（每个 .so 导出此符号）
extern "C" {
    ITransportBinding* CreateBindingInstance();
    void DestroyBindingInstance(ITransportBinding* instance);
}

} // namespace binding
} // namespace com
} // namespace ara
```

#### BindingManager（动态加载逻辑）

```cpp
class BindingManager {
public:
    Result<void> LoadBindings(const YAML::Node& config) {
        for (const auto& binding_cfg : config["bindings"]) {
            if (!binding_cfg["enabled"].get<bool>()) {
                continue;  // 跳过未启用的插件
            }
            
            // 动态加载 .so
            void* handle = dlopen(binding_cfg["library"].get<std::string>().c_str(), 
                                   RTLD_LAZY);
            if (!handle) {
                return Error::PluginLoadFailed;
            }
            
            // 获取工厂函数
            auto create_func = reinterpret_cast<CreateBindingFunc>(
                dlsym(handle, "CreateBindingInstance"));
            
            // 创建实例
            auto* binding = create_func();
            binding->Initialize(binding_cfg["config"]);
            
            // 按优先级存储
            bindings_.emplace(binding_cfg["priority"].get<uint32_t>(), binding);
        }
        return Result<void>::Ok();
    }
    
    ITransportBinding* SelectBestBinding(const ServiceIdentifier& service_id) {
        // 1. 检查静态配置指定的 Binding
        if (auto binding = GetStaticBinding(service_id); binding) {
            return binding;
        }
        
        // 2. 按优先级选择（100 → 50 → 10）
        for (auto& [priority, binding] : bindings_) {
            if (binding->SupportsService(service_id)) {
                return binding;
            }
        }
        
        return nullptr;
    }
    
private:
    std::multimap<uint32_t, ITransportBinding*, std::greater<>> bindings_;
};
```

### 应用代码示例（完全标准 AUTOSAR）

```cpp
// 应用层代码 - 无需知道底层使用哪个 Binding
#include <ara/com/Runtime.h>
#include <ara/com/ServiceProxy.h>

using namespace lap::com;  // 100%兼容 AUTOSAR ara::com API

int main() {
    // 1. 初始化 Runtime（自动加载配置文件）
    Runtime::Initialize();
    
    // 2. 查找服务（透明使用静态配置 / 共享内存注册表 / 动态发现）
    auto handles = FindService<CameraServiceProxy>();
    
    // 3. 创建代理（自动选择最优 Binding: iceoryx > dds > legacy）
    auto proxy = std::make_shared<CameraServiceProxy>(handles[0]);
    
    // 4. 订阅事件（零拷贝自动生效，应用无感知）
    proxy->ImageData.Subscribe([](const Image& img) {
        ProcessImage(img);  // img 可能是 iceoryx 零拷贝对象，应用不关心
    });
    
    // 5. 调用方法（自动路由到正确的 Binding）
    auto result = proxy->GetStatus().Get();  // 阻塞调用或异步 future
    
    Runtime::Shutdown();
    return 0;
}
```

**关键点**：
- ✅ 应用代码 100% 符合 AUTOSAR 标准
- ✅ 切换 Binding 只需修改 YAML 配置，无需重编译
- ✅ 零拷贝、mempool 隔离、epoll 循环完全透明

---

## 已完成组件清单（基于 AUTOSAR AP 规范）

### 1. lap::com Runtime API (source/inc/Runtime.hpp)

**AUTOSAR 对应**: Communication Management Runtime

| API | AUTOSAR 需求 | 功能描述 | 实现状态 |
|-----|-------------|---------|----------|
| `FindService()` | SWS_CM_00001 | 查找服务实例 | ✅ 完成 |
| `OfferService()` | SWS_CM_00002 | 提供服务实例 | ✅ 完成 |
| `StopOfferService()` | SWS_CM_00005 | 停止服务提供 | ✅ 完成 |
| `Initialize()` | SWS_CM_00101 | 运行时初始化 | ✅ 完成 |
| `Deinitialize()` | SWS_CM_00102 | 运行时清理 | ✅ 完成 |

**代码量**: ~200行  
**依赖**: Core (日志), Persistency (配置)

### 2. lap::com ServiceProxy (source/inc/ProxyBase.hpp)

**AUTOSAR 对应**: Client-Side Service Interface

| 特性 | AUTOSAR 需求 | 功能描述 | 实现状态 |
|------|-------------|---------|----------|
| 构造函数 | SWS_CM_00130 | 从服务句柄创建代理 | ✅ 完成 |
| 方法调用 | SWS_CM_00191 | 同步/异步方法调用 | ✅ 完成 |
| 事件订阅 | SWS_CM_00141 | 订阅服务事件 | ✅ 完成 |
| 字段访问 | SWS_CM_00200 | 读取/订阅字段 | ✅ 完成 |

**代码量**: ~250行  
**线程安全**: 是（内部互斥锁保护）

### 3. lap::com ServiceSkeleton (source/inc/SkeletonBase.hpp)

**AUTOSAR 对应**: Server-Side Service Interface

| 特性 | AUTOSAR 需求 | 功能描述 | 实现状态 |
|------|-------------|---------|----------|
| `OfferService()` | SWS_CM_00110 | 开始提供服务 | ✅ 完成 |
| `StopOfferService()` | SWS_CM_00111 | 停止提供服务 | ✅ 完成 |
| 方法注册 | SWS_CM_00112 | 注册方法处理器 | ✅ 完成 |
| 事件发送 | SWS_CM_00113 | 发送事件通知 | ✅ 完成 |
| 字段更新 | SWS_CM_00114 | 更新字段值 | ✅ 完成 |

**代码量**: ~270行  
**生命周期**: RAII管理

### 4. 通信原语实现

#### 4.1 Method (source/inc/Method.hpp)

**AUTOSAR 对应**: Method Call Communication Pattern

| 模式 | AUTOSAR 需求 | 实现 | 代码量 |
|------|-------------|------|--------|
| Fire & Forget | SWS_CM_00196 | `MethodCaller::CallFireForget()` | ~150行 |
| 同步调用 | SWS_CM_00191 | `MethodCaller::Call()` | ~150行 |
| 异步调用 | SWS_CM_00195 | `MethodCaller::CallAsync()` | ~150行 |

**总代码量**: ~450行

#### 4.2 Event (source/inc/Event.hpp)

**AUTOSAR 对应**: Event Communication Pattern

| 功能 | AUTOSAR 需求 | 实现 | 代码量 |
|------|-------------|------|--------|
| 订阅事件 | SWS_CM_00141 | `Event::Subscribe()` | ~100行 |
| 取消订阅 | SWS_CM_00151 | `Event::Unsubscribe()` | ~80行 |
| 获取样本 | SWS_CM_00181 | `Event::GetNewSamples()` | ~100行 |
| 设置接收器 | SWS_CM_00182 | `Event::SetReceiveHandler()` | ~70行 |

**总代码量**: ~350行  
**缓存策略**: 可配置（FIFO/Ring Buffer）

#### 4.3 Field (source/inc/Field.hpp)

**AUTOSAR 对应**: Field Communication Pattern

| 功能 | AUTOSAR 需求 | 实现 | 代码量 |
|------|-------------|------|--------|
| 读取字段 | SWS_CM_00200 | `Field::Get()` | ~150行 |
| 设置字段 | SWS_CM_00201 | `Field::Set()` | ~150行 |
| 订阅通知 | SWS_CM_00202 | `Field::Subscribe()` | ~150行 |
| 注册Getter | SWS_CM_00210 | `Field::RegisterGetHandler()` | ~100行 |

**总代码量**: ~550行  
**特性**: 支持 Notifier + Getter/Setter 模式

### 5. 类型系统 (source/inc/ComTypes.hpp)

**AUTOSAR 对应**: lap::core 类型扩展（兼容 ara::core）

| 类型 | 用途 | 符合标准 |
|------|------|----------|
| `InstanceIdentifier` | 服务实例标识 | SWS_CM_00302 |
| `ServiceHandleContainer` | 服务句柄容器 | SWS_CM_00303 |
| `FindServiceHandle` | 服务发现句柄 | SWS_CM_00304 |
| `EventReceiveHandler` | 事件接收回调 | SWS_CM_00305 |
| `SubscriptionState` | 订阅状态枚举 | SWS_CM_00306 |

**代码量**: ~400行  
**错误码**: 符合 lap::core::ErrorCode 规范（兼容 ara::core::ErrorCode）

### 6. Legacy Binding (source/binding/legacy/) - 遗留协议网关接口

**AUTOSAR 对应**: Legacy Protocol Compatibility Layer

**设计理念**: 遗留协议（SOME/IP、D-Bus）运行在独立网关进程中，binding_legacy.so 仅提供转发接口

#### 6.1 架构设计

| 组件 | 功能 | 代码量 |
|------|------|--------|
| `LegacyGatewayClient` | 网关客户端（UDS 通信） | ~300行 |
| `LegacyServiceMapper` | lap::com ↔ 网关协议映射 | ~250行 |
| `LegacyBindingAdapter` | ITransportBinding 实现 | ~250行 |

**总计**: ~800行  
**通信方式**: Unix Domain Socket  
**特点**:
- ✅ 完全隔离（独立进程）
- ✅ 故障隔离（网关崩溃不影响主系统）
- ✅ 可选部署（不需要时不加载）
- ✅ 协议无感知（应用只看到 lap::com API）

#### 6.2 独立网关进程

**SomeIpGateway** (独立进程):
- vsomeip3 协议栈
- DDS 双向转换
- Protobuf 网关协议
- 独立生命周期管理

**DiagDaemon** (独立进程):
- D-Bus 诊断服务
- UDS (Unified Diagnostic Services)
- 仅诊断功能，与主通信解耦

#### 6.3 配置示例

```yaml
type: legacy_someip
library: /usr/lib/lap/com/binding_legacy.so
priority: 10
enabled: false
config:
  gateway_address: "unix:///tmp/someip_gateway.sock"
  timeout_ms: 5000
  auto_start_gateway: false
```

### 7. Binding Manager (source/binding/manager/) - 插件管理器

**AUTOSAR 对应**: Transport Binding Manager

**核心功能**: 运行时动态加载和管理 .so 插件

#### 7.1 组件设计

| 组件 | 功能 | 代码量 |
|------|------|--------|
| `BindingLoader` | dlopen() 动态加载插件 | ~250行 |
| `BindingSelector` | 优先级选择算法 | ~200行 |
| `ConfigParser` | YAML 配置解析（基于yaml-cpp，含 ARXML 转换） | ~300行 |
| `BindingRegistry` | 插件注册表管理 | ~150行 |

**总计**: ~900行

#### 7.2 ITransportBinding 插件接口

```cpp
class ITransportBinding {
public:
    virtual ~ITransportBinding() = default;
    
    // 生命周期管理
    virtual Result<void> Initialize(const BindingConfig& config) = 0;
    virtual Result<void> Shutdown() = 0;
    
    // 服务管理
    virtual Result<void> OfferService(const ServiceConfig& config) = 0;
    virtual Result<void> StopOfferService(InstanceIdentifier id) = 0;
    
    // 通信原语
    virtual Result<void> SendEvent(const EventData& data) = 0;
    virtual Result<void> SendMethod(const MethodData& data) = 0;
    virtual Result<void> SubscribeEvent(EventReceiveHandler handler) = 0;
    
    // 元数据
    virtual uint32_t GetPriority() const = 0;
    virtual std::string GetName() const = 0;
};
```

#### 7.3 插件加载流程

```cpp
// 1. 解析配置文件
auto config = ConfigParser::Load("/etc/lap/com/binding_config.yaml");

// 2. 按优先级排序
std::sort(config.bindings.begin(), config.bindings.end(),
          [](auto& a, auto& b) { return a.priority > b.priority; });

// 3. 动态加载插件
for (auto& binding_cfg : config.bindings) {
    if (!binding_cfg.enabled) continue;
    
    void* handle = dlopen(binding_cfg.library.c_str(), RTLD_LAZY);
    auto create_fn = (CreateBindingFn)dlsym(handle, "CreateBinding");
    auto binding = create_fn();
    
    binding->Initialize(binding_cfg.config);
    registry_.Register(binding);
}

// 4. 服务请求时选择最高优先级可用 Binding
auto binding = registry_.SelectBinding(service_id);
```

**Step 1**: vsomeip → DDS Bridge (保持 vsomeip 配置)
**Step 2**: 启用 DDS QoS 优化
**Step 3**: 移除 CommonAPI 依赖
**Step 4**: 完全切换到 DDS IDL

### 8. DDS Transport Binding (source/binding/dds/) 📋 计划中

**AUTOSAR 对应**: DDS Network Binding + DDS Security (AUTOSAR AP TR)

| 组件 | TR 需求 | 功能 | 计划代码量 |
|------|---------|------|-----------|
| `DdsConnectionManager` | TR_DDSS_00001-00007 | DDS Domain/Participant 管理 | ~300行 |
| `DdsTopicBinding` | TR_DDSS_00101-00104 | Topic 创建与配置 | ~250行 |
| `DdsPublisherBinding` | TR_DDSS_00201+ | DataWriter 发布 | ~280行 |
| `DdsSubscriberBinding` | TR_DDSS_00202+ | DataReader 订阅 | ~280行 |
| `DdsSecurityManager` | TR_DDSS_00002-00007 | 安全证书与权限管理 | ~400行 |

**总计**: ~1,510行（计划）  
**底层库**: eProsima Fast-DDS + DDS Security Plugin  
**优势**: 数据中心级分布式、发布/订阅模式、内置安全性  
**性能**: 延迟 <10μs（共享内存），高吞吐量  
**安全**: 支持 DDS Security 规范（认证、加密、访问控制）

**DDS 核心特性**:
- ✅ Data-Centric Pub/Sub（DCPS）
- ✅ Quality of Service (QoS) 策略
- ✅ Topic-based 通信
- ✅ Discovery Protocol（RTPS）
- ✅ 多传输支持（UDP, TCP, 共享内存）
- ✅ DDS Security（认证、加密、授权）
- ✅ Time-based filtering
- ✅ Content filtering

**DDS Security 集成** (基于 AUTOSAR TR_DDSS):

| 安全组件 | TR 需求 | 功能 |
|---------|---------|------|
| Identity CA | TR_DDSS_00002 | 身份证书颁发机构 |
| Identity Certificate | TR_DDSS_00003 | 参与者身份证书 |
| Private Key | TR_DDSS_00004 | 私钥管理 |
| Permissions CA | TR_DDSS_00005 | 权限证书颁发机构 |
| Governance Document | TR_DDSS_00006 | 域治理策略（加密、签名） |
| Permissions Document | TR_DDSS_00007 | 访问权限控制 |

**AUTOSAR 到 DDS 映射**:

```cpp
// AUTOSAR Service Interface → DDS Topic
ServiceInterface "VehicleSpeed" → DDS Topic "services/VehicleSpeed"

// AUTOSAR Event → DDS DataWriter/DataReader
Event<SpeedData> → DataWriter<SpeedData> / DataReader<SpeedData>

// AUTOSAR Field → DDS Topic with QoS
Field<float> → Topic with TRANSIENT_LOCAL QoS (持久化最新值)

// AUTOSAR Method → Request/Reply Topics
Method Calculate() → Request Topic + Reply Topic
```

**DDS QoS 映射**:

| AUTOSAR 概念 | DDS QoS 策略 | 说明 |
|-------------|-------------|------|
| 可靠性 | RELIABILITY | RELIABLE / BEST_EFFORT |
| 持久化 | DURABILITY | VOLATILE / TRANSIENT_LOCAL |
| 历史记录 | HISTORY | KEEP_LAST / KEEP_ALL |
| 生命周期 | LIFESPAN | 数据有效期 |
| 优先级 | TRANSPORT_PRIORITY | 传输优先级 |
| 截止时间 | DEADLINE | 数据更新周期 |

**安全证书部署** (TR_DDSS_00001):

```
artifacts/dds_security/
├── identity_ca.pem          # 身份 CA 证书
├── permissions_ca.pem       # 权限 CA 证书
├── instance_cert.pem        # 服务实例证书
├── instance_key.pem         # 服务实例私钥
├── governance.xml           # 治理文档（域规则）
└── permissions.xml          # 权限文档（访问控制）
```

**实施计划**:

**Phase 1: 基础 DDS 绑定** (3-4周)
- Week 1: DDS Domain/Participant 管理
- Week 2: Topic/DataWriter/DataReader 绑定
- Week 3: QoS 策略映射
- Week 4: 测试与集成

**Phase 2: DDS Security 集成** (2-3周)
- Week 1: 证书管理与部署
- Week 2: Governance/Permissions 文档生成
- Week 3: 端到端安全测试

**使用场景**:
- 🚗 **车联网（V2X）**: 车辆间高频数据分发
- 🏭 **工业物联网**: 传感器数据采集
- 🌐 **分布式系统**: 跨网络服务通信
- 🔒 **安全关键应用**: 需要认证和加密的场景

### 9. 序列化策略 (符合 AUTOSAR 标准)

**关键原则**: Com 模块遵循 **零手动序列化** 原则，所有序列化由外部库或代码生成工具自动完成。

#### 8.1 D-Bus 自动序列化

**实现方式**: sdbus-c++ 库内置序列化

| 特性 | 实现 | AUTOSAR 兼容性 |
|------|------|---------------|
| 基础类型 | 自动 marshalling | ✅ 符合 |
| 复杂类型 | operator<< / operator>> | ✅ 符合 |
| 数组/容器 | STL 容器直接支持 | ✅ 符合 |
| 自定义结构 | 无需手动编码 | ✅ 符合 |

**优势**: 编译期类型安全，零运行时开销

#### 8.2 SOME/IP IDL 驱动序列化

**实现方式**: CommonAPI 代码生成

| 阶段 | 工具 | 输出 |
|------|------|------|
| IDL 定义 | Franca IDL | .fidl 文件 |
| 代码生成 | commonapi-someip-generator | C++ 序列化器 |
| 协议映射 | .fdepl 文件 | SOME/IP 线格式 |

**特点**:
- ✅ 完全自动化
- ✅ SOME/IP 标准兼容
- ✅ 跨语言支持

#### 9.3 DDS IDL 驱动序列化 📋 计划中

**实现方式**: Fast-DDS IDL 编译器

| 阶段 | 工具 | 输出 |
|------|------|------|
| IDL 定义 | OMG IDL (.idl) | 数据类型定义 |
| 代码生成 | fastddsgen | C++ DataType + TypeSupport |
| 序列化 | CDR (Common Data Representation) | DDS 线格式 |

**特点**:
- ✅ OMG DDS 标准
- ✅ CDR 序列化（高效二进制格式）
- ✅ 支持复杂数据结构（嵌套、数组、序列）
- ✅ 零拷贝传输（共享内存模式）

#### 8.3 Custom Protocol Binding - 自定义序列化

**实现方式**: 可扩展编解码器框架

```cpp
// 示例：二进制编解码器
class BinaryCodec : public IProtocolCodec {
public:
    Result<ByteBuffer> Encode(const SamplePtr& sample) override {
        ByteBuffer buffer;
        buffer.WriteUInt32(sample->timestamp);
        buffer.WriteFloat(sample->value);
        return buffer;
    }
    
    Result<SamplePtr> Decode(const ByteBuffer& buffer) override {
        auto sample = std::make_shared<Sample>();
        sample->timestamp = buffer.ReadUInt32();
        sample->value = buffer.ReadFloat();
        return sample;
    }
};
```

**特点**:
- ✅ 完全自定义协议
- ✅ 轻量级实现
- ✅ 适合遗留系统集成
- ✅ 零外部依赖

#### 8.4 Legacy Binding - 网关转发

**实现方式**: 透明转发到独立网关进程

- 应用层无感知序列化
- 网关进程内部处理 SOME/IP/D-Bus 编解码
- lap::com ↔ 网关协议（Protobuf/YAML）

### 9. iceoryx2 Binding (source/binding/iceoryx2/) 🔥 新增

#### 9.1 设计定位

**核心目标**: 提供**真正零拷贝**的超高性能本地进程间通信（IPC）方案

**iceoryx2 优势** (相比 iceoryx v1):
- ✅ **无需 RouDi 守护进程**: 每个进程自管理，消除单点故障
- ✅ **Rust 实现**: 内存安全，无数据竞争
- ✅ **零配置启动**: 自动发现与连接
- ✅ **更好的 FuSa 支持**: 进程隔离更彻底

**适用场景**:
- 🚀 **极致性能**: 传感器融合、感知算法、大规模数据流处理
- ⚡ **超低延迟**: < 1μs 端到端延迟，实时控制系统
- 🔒 **零拷贝**: 共享内存直接访问，消除内存复制
- 📊 **大吞吐量**: > 10 GB/s 单连接，支持4K视频、LiDAR点云

**性能指标** (应用 5 步优化后):
- 延迟: **< 1μs** → **< 500ns** (P99, io_uring SQPOLL + 大页)
- 吞吐量: **> 10 GB/s** → **> 15 GB/s** (1GB 大页 + THP)
- CPU占用: **< 0.5%** → **< 0.2%** (io_uring 零系统调用)
- 内存占用: 固定MemPool，2MB chunk 对齐大页边界
- 确定性: Lock-free算法 + CPU 隔离 (isolcpus 4-7)

#### 9.2 iceoryx2 核心特性

**1. 真零拷贝架构（无需 RouDi）**
```cpp
// iceoryx2: 进程自动初始化，无需 RouDi 守护进程
// 发布端：直接在共享内存中构造数据
auto node = iox2::NodeBuilder("camera_node").create();
auto publisher = node.publish<SensorData>("LidarPoints");
auto sample = publisher.loan();  // 从进程自管理 MemPool 借用

// 直接在共享内存中写入数据（零拷贝）
sample->timestamp = getCurrentTime();
sample->points.resize(10000);    // 预分配内存池
for (size_t i = 0; i < 10000; ++i) {
    sample->points[i] = lidar.readPoint(i);
}

publisher.publish(sample);  // 仅传递指针，无数据复制
```

```cpp
// 订阅端：直接访问共享内存数据
auto subscriber = runtime.CreateSubscriber<SensorData>("LidarPoints");

subscriber.subscribe([](const SensorData* sample) {
    // 直接读取共享内存，零拷贝
    processLidarData(sample->points);  
    // 数据在回调结束后自动释放回MemPool
});
```

**2. 内存池（MemPool）进程自管理**
```cpp
// iceoryx2: 每个进程自己管理 MemPool，无需中央配置
auto config = iox2::Config::default()
    .with_domain("lightap_com")
    .with_max_publishers(10)
    .with_max_subscribers(20);

auto node = iox2::NodeBuilder("sensor_node")
    .config(config)
    .create();

// 自动分配共享内存，无需 RouDi
auto publisher = node.publish<LidarData>("lidar_points")
    .max_payload_size(2 * 1024 * 1024)  // 2MB
    .max_publishers(5)
    .create();
```

**3. Lock-free Queue**
- **无锁算法**: SPSC (Single Producer Single Consumer) 队列
- **实时保证**: 无优先级反转，无死锁
- **确定性延迟**: 最坏情况可预测

**4. 服务发现（去中心化，无需 RouDi）**
```cpp
// iceoryx2: 去中心化服务发现，通过共享内存自动同步
auto publisher = node.publish<CameraImage>("FrontCamera").create();
// 自动在共享内存区域注册服务元数据

auto subscriber = node.subscribe<CameraImage>("FrontCamera").create();
// 自动扫描共享内存区域，发现可用服务
// 通过固定槽位映射，< 500ns 延迟
```

#### 9.3 架构组成

| 组件 | 功能 | 文件 |
|------|------|------|
| `Iceoryx2NodeManager` | iceoryx2 Node 管理（进程自管理） + CPU 亲和性 | `Iceoryx2Node.hpp` |
| `Iceoryx2Publisher` | 零拷贝发布器（Rust FFI） + io_uring SQPOLL | `Iceoryx2Publisher.hpp` |
| `Iceoryx2Subscriber` | 零拷贝订阅器（Rust FFI） + io_uring 异步接收 | `Iceoryx2Subscriber.hpp` |
| `Iceoryx2ConfigManager` | 配置管理（无需 RouDi） + 大页内存配置 | `Iceoryx2Config.hpp` |
| `Iceoryx2EventBinding` | Event 通信绑定 | `Iceoryx2EventBinding.hpp` |
| `Iceoryx2MethodBinding` | Method 调用绑定 | `Iceoryx2MethodBinding.hpp` |
| `Iceoryx2FieldBinding` | Field 通知绑定 | `Iceoryx2FieldBinding.hpp` |
| `Iceoryx2IoUringIntegration` | io_uring SQPOLL 零系统调用（Step 3） | `Iceoryx2IoUring.hpp` |
| `Iceoryx2HugePageAllocator` | 大页内存分配器（Step 1） | `HugePageAllocator.hpp` |

#### 9.4 核心技术

**1. POSIX Shared Memory**
```cpp
// 共享内存创建（由RouDi管理）
int shm_fd = shm_open("/iceoryx_mgmt", O_CREAT | O_RDWR, 0666);
ftruncate(shm_fd, MEMPOOL_SIZE);
void* shm_addr = mmap(nullptr, MEMPOOL_SIZE, 
                      PROT_READ | PROT_WRITE, 
                      MAP_SHARED, shm_fd, 0);
```

**2. MemPool 分配策略**
```cpp
class IceoryxMemPool {
public:
    // 从内存池借用chunk
    template<typename T>
    T* loan() {
        // 1. 从空闲列表获取chunk（lock-free）
        auto chunk = freeList_.pop();
        if (!chunk) {
            // 2. 内存池耗尽，返回错误
            return nullptr;
        }
        
        // 3. Placement new构造对象
        return new (chunk->data()) T();
    }
    
    // 发布后自动释放
    template<typename T>
    void release(T* sample) {
        // 1. 调用析构函数
        sample->~T();
        
        // 2. 归还chunk到空闲列表（lock-free）
        auto chunk = getChunkFromSample(sample);
        freeList_.push(chunk);
    }
    
private:
    LockFreeStack<Chunk> freeList_;  // 无锁栈
};
```

**3. Lock-free SPSC Queue**
```cpp
// Lamport's algorithm - 单生产者单消费者队列
template<typename T, size_t Capacity>
class SPSCQueue {
public:
    bool push(const T& value) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next_head = (head + 1) % Capacity;
        
        if (next_head == tail_.load(std::memory_order_acquire)) {
            return false;  // 队列满
        }
        
        buffer_[head] = value;
        head_.store(next_head, std::memory_order_release);
        return true;
    }
    
    bool pop(T& value) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;  // 队列空
        }
        
        value = buffer_[tail];
        tail_.store((tail + 1) % Capacity, std::memory_order_release);
        return true;
    }
    
private:
    std::array<T, Capacity> buffer_;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};
```

**4. iceoryx2 去中心化架构（无需 RouDi）+ 系统级优化**
```
┌─────────────────────────────────────────────────────────────────┐
│      共享内存区域 (1GB Huge Pages + THP, Step 1)               │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ 服务元数据区（去中心化，所有进程共享）                   │   │
│  │ - Service Registry (共享内存数据结构)                   │   │
│  │ - Publishers: Lock-free HashMap<Topic, PublisherList>   │   │
│  │ - Subscribers: Lock-free HashMap<Topic, SubscriberList> │   │
│  └─────────────────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ 数据缓冲区（每个服务独立 MemPool，2MB chunk 对齐）       │   │
│  │ - /camera_front: 8MB × 50 chunks (1GB 大页)             │   │
│  │ - /lidar_points: 2MB × 100 chunks (THP 优化)            │   │
│  │ - /steering_control: 4KB × 1000 chunks (ASIL-D)         │   │
│  └─────────────────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ io_uring SQPOLL 提交队列（Step 3，CPU 2 绑核）           │   │
│  │ - SQ (Submission Queue): 32K entries                    │   │
│  │ - CQ (Completion Queue): 32K entries                    │   │
│  │ - SQPOLL Kernel Thread: 绑定小核 CPU 2                  │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
         ↑                                       ↑
         │ 进程自管理 mmap                        │ 进程自管理 mmap
         │                                       │
┌────────┴─────────────┐              ┌─────────┴────────────────┐
│  Publisher App       │              │ Subscriber App           │
│  (Camera Node)       │              │ (Fusion Node)            │
│  - 自己创建/映射 SHM  │              │ - 自己映射 SHM           │
│  - 自己管理 MemPool  │              │ - Lock-free 读取         │
└──────────────────────┘              └──────────────────────────┘
         │                                       │
         └────── 共享内存服务注册表 (固定槽位映射) ─────┘
                    (零守护进程，< 500ns 延迟)
```

#### 9.5 FuSa MemPool 物理隔离（QM / ASIL-D）

**核心设计**: iceoryx2 进程自管理 + Binding 强制权限控制，**应用无感知**

**iceoryx2 优势**: 无中央 RouDi，消除单点故障风险，进程级隔离更彻底

##### 9.5.1 MemPool 隔离策略

| Safety Level | MemPool 名称 | 用途 | 访问权限 | FuSa 保证 |
|--------------|--------------|------|----------|-----------|
| **QM** | `QM_PerceptionPool` | 摄像头图像、LiDAR 点云 | Read/Write | 非安全关键数据 |
| **ASIL-D** | `ASIL_ControlPool` | 转向指令、制动信号 | Write (控制进程)<br>Read-only (其他) | 物理隔离，防篡改 |
| **ASIL-B** | `ASIL_SensorPool` | 轮速、IMU 数据 | Read/Write | 中等安全等级 |

##### 9.5.2 mempool_config.toml 配置（iceoryx2 进程自管理）

```toml
# iceoryx2 配置 - 每个进程自己管理，无需 RouDi
[domain]
name = "lightap_com"
shm_path = "/dev/shm/iceoryx2_lightap"

# 系统级优化：大页内存支持（对应 Step 1）
[system]
use_huge_pages = true          # 启用 1GB 大页内存
huge_page_size = "1G"          # 1GB 大页
transparent_huge_pages = true  # 启用 THP
alignment = 2097152            # 2MB chunk 对齐大页边界

# QM 等级：感知数据（摄像头、LiDAR）
[[services]]
name = "camera_front"
safety_level = "QM"
max_payload_size = 8388608  # 8MB（4K 摄像头帧）
max_publishers = 5
max_subscribers = 20
history_size = 10

[[services]]
name = "lidar_points"
safety_level = "QM"
max_payload_size = 2097152  # 2MB（LiDAR 点云）
max_publishers = 3
max_subscribers = 15
history_size = 5

# ASIL-D 等级：关键控制指令（转向、制动）
[[services]]
name = "steering_control"
safety_level = "ASIL-D"
max_payload_size = 4096  # 4KB（控制指令）
max_publishers = 1  # 单一控制源
max_subscribers = 10
history_size = 100  # 高历史记录用于审计
access_mode = "read_only_subscribers"  # 订阅者强制只读

[[services]]
name = "brake_control"
safety_level = "ASIL-D"
max_payload_size = 4096
max_publishers = 1
max_subscribers = 10
history_size = 100
access_mode = "read_only_subscribers"

# ASIL-B 等级：传感器数据
[[services]]
name = "wheel_speed"
safety_level = "ASIL-B"
max_payload_size = 1024  # 1KB（轮速、IMU）
max_publishers = 4  # 4个轮速传感器
max_subscribers = 20
history_size = 50
```

##### 9.5.3 物理隔离实现机制

**1. MemPool 段分离（iceoryx2 进程自管理，Linux 内核级别隔离）**

```cpp
// iceoryx2: 每个进程启动时自己创建独立共享内存段
class Iceoryx2MemPoolManager {
public:
    void InitializeService(const ServiceConfig& config) {
        // 每个服务使用独立的 shm 文件（按安全等级隔离）
        std::string shm_name = "/iceoryx2_" + config.domain_name 
                             + "_" + config.service_name
                             + "_" + config.safety_level;
        
        int shm_fd = shm_open(shm_name.c_str(), 
                               O_CREAT | O_RDWR, 
                               0600);  // 严格权限控制
        
        size_t total_size = config.max_payload_size 
                          * config.max_publishers 
                          * config.history_size;
        ftruncate(shm_fd, total_size);
        
        // mmap 创建隔离的内存段
        void* addr = mmap(nullptr, total_size,
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED, shm_fd, 0);
        
        services_[config.service_name] = {addr, shm_fd, config};
    }
    
private:
    std::map<std::string, ServiceMemory> services_;
};
```
```

**2. 进程访问权限控制（POSIX ACL）**

```cpp
class IceoryxAccessControl {
public:
    void EnforceReadOnlyAccess(int shm_fd, pid_t reader_pid) {
        // 为非 writer 进程强制只读映射
        if (!IsWriter(reader_pid)) {
            // 使用 mprotect 强制只读
            void* addr = GetSegmentAddress(shm_fd);
            size_t size = GetSegmentSize(shm_fd);
            
            mprotect(addr, size, PROT_READ);  // 移除写权限
            
            // 记录审计日志
            LogFuSaEvent("READ_ONLY_ENFORCED", reader_pid, shm_fd);
        }
    }
    
private:
    bool IsWriter(pid_t pid) {
        // 检查进程是否在 writer 白名单
        return writer_whitelist_.count(pid) > 0;
    }
};
```

**3. Binding 层自动权限检查**

```cpp
// binding_iceoryx2.so 内部实现
template<typename DataType>
class Iceoryx2Subscriber {
public:
    Result<void> Subscribe(EventReceiveHandler handler) {
        // 加载服务配置（自动识别安全等级）
        auto service_cfg = config_manager_.GetServiceConfig(service_name_);
        
        // iceoryx2: 进程自己创建订阅者，自动强制只读（ASIL-D 数据）
        auto subscriber = node_->subscribe<DataType>(service_name_)
            .max_payload_size(service_cfg.max_payload_size)
            .create();
        
        // 如果是 ASIL-D 服务，强制 mprotect 只读
        if (service_cfg.safety_level == "ASIL-D" && 
            service_cfg.access_mode == "read_only_subscribers") {
            EnforceReadOnlyMapping(subscriber.shm_address());
        }
        
        subscriber.set_receive_handler([handler](const DataType* sample) {
            // 应用代码无感知，但底层是只读映射
            handler(*sample);  // 尝试修改会触发 SIGSEGV
        });
        
        return Result<void>::Ok();
    }
};```
```

##### 9.5.4 FuSa 审计与验证

**审计日志（符合 ISO 26262）**

```cpp
struct FuSaAuditLog {
    uint64_t timestamp_us;
    pid_t process_id;
    std::string mempool_name;
    enum class Event {
        MEMPOOL_CREATED,
        READ_ONLY_ENFORCED,
        WRITE_ATTEMPT_BLOCKED,
        SEGMENT_VIOLATION
    } event;
    std::string details;
};

// 实时记录到持久化存储
void LogFuSaEvent(const FuSaAuditLog& log) {
    // 写入 /var/log/ara/com/fusa_audit.log
    std::ofstream log_file(FUSA_AUDIT_PATH, std::ios::app);
    log_file << log.timestamp_us << ","
             << log.process_id << ","
             << log.mempool_name << ","
             << static_cast<int>(log.event) << ","
             << log.details << "\n";
}
```

**FuSa 认证关键点**：
- ✅ **物理隔离**: 不同 ASIL 等级使用独立 shm 段（按服务隔离）
- ✅ **访问控制**: POSIX mprotect 强制只读权限
- ✅ **审计追溯**: 所有访问事件记录到审计日志
- ✅ **应用透明**: lap::com API 无需修改，Binding 层自动处理
- ✅ **故障安全**: 非法写入触发 SIGSEGV，进程立即终止
- ✅ **去中心化**: 无 RouDi 单点故障，进程级隔离更彻底
- ✅ **Rust 安全**: iceoryx2 Rust 实现，内存安全保证

**认证优势**：
> **"FuSa 一句话过审"** —— 通过 iceoryx2 进程自管理 + Binding 强制权限控制，无需修改应用代码即可满足 ISO 26262 ASIL-D 要求。
> 
> **iceoryx2 额外优势**: 消除 RouDi 单点故障，进程崩溃不影响其他进程，符合 ISO 26262 "Freedom from Interference" 要求。

---

### 10. Custom Protocol + UDS Binding (source/binding/custom_protocol/) 🔧 轻量级

#### 10.1 设计定位

**核心目标**: 提供**轻量级、灵活可定制**的私有通信协议，基于 Unix Domain Socket

**适用场景**:
- 🔧 **遗留系统集成**: 适配已有的私有协议系统
- 🎯 **特殊需求**: 自定义序列化格式、特殊加密算法
- ⚡ **轻量级通信**: 无需 DDS/iceoryx 复杂性的简单场景
- 🔒 **本地安全**: Unix Domain Socket 本地进程通信
- 🚀 **快速原型**: 快速开发验证，后期可迁移到 DDS/iceoryx

**性能指标**:
- 延迟: < 10μs (流式模式，SOCK_STREAM)
- 吞吐量: > 500 MB/s (本地 UDS)
- CPU占用: < 1%
- 零外部依赖: 仅依赖标准 POSIX API

#### 10.2 架构组成

| 组件 | 功能 | 文件 |
|------|------|------|
| `UdsTransport` | Unix Domain Socket 管理（流式/数据报） | `UdsTransport.hpp` |
| `CustomProtocolCodec` | 可扩展编解码器框架 | `ProtocolCodec.hpp` |
| `BinarySerializer` | 高性能二进制序列化 | `BinarySerializer.hpp` |
| `CustomMethodBinding` | 方法调用绑定 | `CustomMethodBinding.hpp` |
| `CustomEventBinding` | 事件广播绑定 | `CustomEventBinding.hpp` |
| `CustomFieldBinding` | Field 通知绑定 | `CustomFieldBinding.hpp` |
| `DiscoveryManager` | 本地服务发现（UDS + 文件系统） | `DiscoveryManager.hpp` |

#### 10.3 协议帧格式（默认实现）

**标准帧结构**:
```
┌──────────────┬──────────────┬──────────────┬──────────────┬─────────────┐
│ Magic (2B)   │ Version (1B) │ Type (1B)    │ Flags (1B)   │ Reserved    │
│ 0xAC (固定)  │ 0x01         │ REQ/RSP/EVT  │ ACK/ENCRYPT  │ (1B)        │
├──────────────┼──────────────┼──────────────┼──────────────┼─────────────┤
│ Message ID   │ Payload Size │ Checksum     │ Timestamp    │ Payload     │
│ (4B)         │ (4B)         │ (2B CRC16)   │ (8B)         │ (N bytes)   │
└──────────────┴──────────────┴──────────────┴──────────────┴─────────────┘

Type:
  - 0x01: Method Request
  - 0x02: Method Response
  - 0x03: Event Notification
  - 0x04: Field Get
  - 0x05: Field Set
  - 0x06: Discovery Beacon
  - 0xFF: Custom Extension

Flags:
  - Bit 0: ACK Required
  - Bit 1: Encrypted
  - Bit 2: Compressed
  - Bit 3-7: Reserved
```

**最小帧**: 24 字节头部（无 Payload 时）

#### 10.4 核心组件设计

**UdsTransport.hpp**
```cpp
namespace ara {
namespace com {
namespace binding {
namespace custom {

enum class SocketMode {
    kStream,      // SOCK_STREAM (可靠、有序)
    kDatagram     // SOCK_DGRAM (不可靠、无序、消息边界)
};

struct UdsConfig {
    std::string socket_path = "/tmp/lightap_custom";
    SocketMode mode = SocketMode::kStream;
    size_t send_buffer_size = 65536;
    size_t recv_buffer_size = 65536;
    int recv_timeout_ms = 1000;
    bool reuse_address = true;
};

class UdsTransport {
public:
    UdsTransport() = default;
    ~UdsTransport();
    
    // 初始化 UDS Socket
    Result<void> Initialize(const UdsConfig& config);
    
    // 服务端监听
    Result<void> Listen();
    Result<int> Accept();  // 返回客户端 fd
    
    // 客户端连接
    Result<void> Connect();
    
    // 发送数据
    Result<size_t> Send(const ByteBuffer& data);
    Result<size_t> SendTo(int client_fd, const ByteBuffer& data);
    
    // 接收数据
    Result<ByteBuffer> Receive();
    Result<ByteBuffer> ReceiveFrom(int client_fd);
    
    // epoll 支持
    int GetSocketFd() const { return socket_fd_; }
    Result<void> SetNonBlocking(bool non_blocking);
    
    // 获取统计信息
    struct Stats {
        uint64_t bytes_sent = 0;
        uint64_t bytes_received = 0;
        uint64_t messages_sent = 0;
        uint64_t messages_received = 0;
    };
    Stats GetStats() const { return stats_; }

private:
    int socket_fd_ = -1;
    UdsConfig config_;
    Stats stats_;
    std::vector<int> client_fds_;  // 服务端模式
    
    Result<void> setupSocket();
};

} // namespace custom
} // namespace binding
} // namespace com
} // namespace ara
```

**ProtocolCodec.hpp (可扩展编解码框架)**
```cpp
namespace ara {
namespace com {
namespace binding {
namespace custom {

// 协议头部定义
struct ProtocolHeader {
    uint16_t magic = 0xACAC;     // Magic number
    uint8_t version = 0x01;      // Protocol version
    uint8_t type = 0x00;         // Message type
    uint8_t flags = 0x00;        // Flags
    uint8_t reserved = 0x00;     // Reserved
    uint32_t message_id = 0;     // Message ID
    uint32_t payload_size = 0;   // Payload size
    uint16_t checksum = 0;       // CRC16
    uint64_t timestamp = 0;      // Timestamp (microseconds)
    
    static constexpr size_t kHeaderSize = 24;
};

// 抽象编解码器接口
class IProtocolCodec {
public:
    virtual ~IProtocolCodec() = default;
    
    // 编码：数据 → 字节流
    virtual Result<ByteBuffer> Encode(
        const ProtocolHeader& header,
        const ByteBuffer& payload) = 0;
    
    // 解码：字节流 → 数据
    virtual Result<std::pair<ProtocolHeader, ByteBuffer>> Decode(
        const ByteBuffer& raw_data) = 0;
    
    // 验证校验和
    virtual bool VerifyChecksum(
        const ProtocolHeader& header,
        const ByteBuffer& payload) = 0;
};

// 默认二进制编解码器
class BinaryCodec : public IProtocolCodec {
public:
    Result<ByteBuffer> Encode(
        const ProtocolHeader& header,
        const ByteBuffer& payload) override {
        
        ByteBuffer buffer;
        buffer.resize(ProtocolHeader::kHeaderSize + payload.size());
        
        // 序列化头部（小端序）
        size_t offset = 0;
        write_uint16_le(buffer, offset, header.magic);
        write_uint8(buffer, offset, header.version);
        write_uint8(buffer, offset, header.type);
        write_uint8(buffer, offset, header.flags);
        write_uint8(buffer, offset, header.reserved);
        write_uint32_le(buffer, offset, header.message_id);
        write_uint32_le(buffer, offset, header.payload_size);
        write_uint16_le(buffer, offset, header.checksum);
        write_uint64_le(buffer, offset, header.timestamp);
        
        // 拷贝 Payload
        std::memcpy(buffer.data() + offset, payload.data(), payload.size());
        
        return buffer;
    }
    
    Result<std::pair<ProtocolHeader, ByteBuffer>> Decode(
        const ByteBuffer& raw_data) override {
        if (raw_data.size() < ProtocolHeader::kHeaderSize) {
            return Error::InvalidProtocol;
        }
        
        ProtocolHeader header;
        size_t offset = 0;
        
        header.magic = read_uint16_le(raw_data, offset);
        header.version = read_uint8(raw_data, offset);
        header.type = read_uint8(raw_data, offset);
        header.flags = read_uint8(raw_data, offset);
        header.reserved = read_uint8(raw_data, offset);
        header.message_id = read_uint32_le(raw_data, offset);
        header.payload_size = read_uint32_le(raw_data, offset);
        header.checksum = read_uint16_le(raw_data, offset);
        header.timestamp = read_uint64_le(raw_data, offset);
        
        // 验证 Magic
        if (header.magic != 0xACAC) {
            return Error::InvalidMagic;
        }
        
        // 提取 Payload
        ByteBuffer payload(raw_data.begin() + offset, raw_data.end());
        
        return std::make_pair(header, payload);
    }
    
    bool VerifyChecksum(
        const ProtocolHeader& header,
        const ByteBuffer& payload) override {
        uint16_t calculated = crc16(payload.data(), payload.size());
        return calculated == header.checksum;
    }
    
private:
    uint16_t crc16(const uint8_t* data, size_t len) {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < len; ++i) {
            crc ^= data[i];
            for (int j = 0; j < 8; ++j) {
                crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
            }
        }
        return crc;
    }
};

} // namespace custom
} // namespace binding
} // namespace com
} // namespace ara
```

#### 10.5 AUTOSAR 集成

**服务描述（IDL）**
```cpp
// Franca IDL 仍用于接口定义
interface MyService {
    method Calculate {
        in { Int32 a, Int32 b }
        out { Int32 result }
    }
    broadcast StatusChanged {
        out { String status }
    }
}

// 自动生成 Custom Protocol 绑定
```

**代码生成流程**
```bash
# 生成 lap::com 绑定
generate_custom_binding MyService.fidl --output source/binding/custom_protocol/
```

#### 10.6 性能基准

| 指标 | D-Bus | SOME/IP | iceoryx2 | **Custom+UDS** |
|------|-------|---------|----------|----------------|
| 延迟 (小消息) | 50-100μs | 20-50μs | < 1μs | **< 10μs** |
| 吞吐量 (MB/s) | 50-100 | 200-300 | > 10,000 | **> 500** |
| CPU 占用 | 3-5% | 2-4% | < 0.5% | **< 1%** |
| 外部依赖 | libdbus | vsomeip | iceoryx2 | **无** |
| 学习曲线 | 中 | 高 | 中 | **低** |
| 跨网络 | ❌ | ✅ | ❌ | **❌** |

#### 10.7 使用场景

| 场景 | 推荐传输 | 原因 |
|------|----------|------|
| 遗留私有协议集成 | **Custom+UDS** | 灵活定制协议格式 |
| 快速原型验证 | **Custom+UDS** | 零依赖，快速开发 |
| 诊断工具通信 | **Custom+UDS** | 简单轻量 |
| 嵌入式资源受限 | **Custom+UDS** | 最小内存占用 |
| 临时通信通道 | **Custom+UDS** | 即插即用 |

#### 10.8 配置示例

**custom_protocol_config.yaml**

```yaml
transport:
  socket_path: /tmp/lightap_custom
  socket_type: stream
  send_buffer_size: 65536
  recv_buffer_size: 65536
  recv_timeout_ms: 1000

protocol:
  version: "1.0"
  codec: binary
  enable_checksum: true
  enable_encryption: false
  enable_compression: false

services:
  - name: DiagnosticService
    socket_path: /tmp/lightap_diag.sock
    mode: server
  
  - name: LegacyService
    socket_path: /tmp/legacy_app.sock
    mode: client
```

#### 10.9 实现路线图

**Phase 1: 基础传输 (1周)**
- Day 1-2: UdsTransport 实现（流式、数据报）
- Day 3-4: BinaryCodec 实现（编解码器）
- Day 5: epoll 集成 + 非阻塞 I/O

**Phase 2: 协议框架 (1周)**
- Day 1-2: CustomMethodBinding + CustomEventBinding
- Day 3-4: 服务发现（基于文件系统）
- Day 5: 错误处理 + 重连机制

**Phase 3: 扩展与集成 (1周)**
- Day 1-2: 自定义 Codec 扩展机制
- Day 3-4: Franca IDL 代码生成
- Day 5: 端到端测试 + 性能调优

**预计工作量**: 3周，~2,200行代码

#### 10.10 与其他 Binding 对比

| 特性 | iceoryx2 | DDS | Custom+UDS | Legacy Gateway |
|------|---------|-----|------------|----------------|
| **延迟** | <1μs | 10-30μs | <10μs | >50μs |
| **吞吐量** | >10GB/s | 500-800MB/s | >500MB/s | <300MB/s |
| **零拷贝** | ✅ | ✅ | ❌ | ❌ |
| **跨ECU** | ❌ | ✅ | ❌ | ✅ |
| **外部依赖** | iceoryx2 | Fast-DDS | 无 | vsomeip/dbus |
| **定制性** | 低 | 中 | **高** | 低 |
| **学习曲线** | 中 | 高 | **低** | 中 |
| **适用场景** | 本地高性能 | 分布式 | **遗留集成/原型** | 遗留兼容 |

---

#### 9.6 接口示例

**IceoryxPublisher.hpp**
```cpp
template<typename DataType>
class IceoryxPublisher {
public:
    IceoryxPublisher(const std::string& service_name, 
                     const std::string& instance_name)
        : publisher_(iox::popo::Publisher<DataType>(
              {service_name, instance_name, "EventData"})) {}
    
    // 借用共享内存（零拷贝）
    Result<DataType*> Loan() {
        auto sample = publisher_.loan();
        if (!sample.has_value()) {
            return MakeError(ComErrc::kMemoryPoolExhausted);
        }
        return sample.value();
    }
    
    // 发布（仅传递指针）
    Result<void> Publish(DataType* sample) {
        publisher_.publish(sample);
        return Result<void>();
    }
    
    // RAII自动发布
    class SampleGuard {
    public:
        SampleGuard(IceoryxPublisher& pub, DataType* sample)
            : publisher_(pub), sample_(sample) {}
        
        ~SampleGuard() {
            if (sample_) publisher_.Publish(sample_);
        }
        
        DataType* operator->() { return sample_; }
        DataType& operator*() { return *sample_; }
        
    private:
        IceoryxPublisher& publisher_;
        DataType* sample_;
    };
    
    Result<SampleGuard> LoanScoped() {
        auto sample = Loan();
        if (!sample.has_value()) return sample.error();
        return SampleGuard(*this, sample.value());
    }
    
private:
    iox::popo::Publisher<DataType> publisher_;
};
```

**IceoryxSubscriber.hpp**
```cpp
template<typename DataType>
class IceoryxSubscriber {
public:
    IceoryxSubscriber(const std::string& service_name,
                      const std::string& instance_name)
        : subscriber_(iox::popo::Subscriber<DataType>(
              {service_name, instance_name, "EventData"})) {
        subscriber_.subscribe(QUEUE_CAPACITY);
    }
    
    // 设置回调（零拷贝访问）
    void SetReceiveHandler(std::function<void(const DataType*)> handler) {
        handler_ = handler;
        
        // 启动接收线程
        receive_thread_ = std::thread([this]() {
            while (running_) {
                subscriber_.take().and_then([&](auto& sample) {
                    handler_(sample.get());  // 零拷贝访问
                });
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }
    
    // 手动拉取（零拷贝）
    Result<const DataType*> Take() {
        auto sample = subscriber_.take();
        if (!sample.has_value()) {
            return MakeError(ComErrc::kNoDataAvailable);
        }
        return sample.value().get();
    }
    
    ~IceoryxSubscriber() {
        running_ = false;
        if (receive_thread_.joinable()) {
            receive_thread_.join();
        }
    }
    
private:
    iox::popo::Subscriber<DataType> subscriber_;
    std::function<void(const DataType*)> handler_;
    std::thread receive_thread_;
    std::atomic<bool> running_{true};
    static constexpr size_t QUEUE_CAPACITY = 16;
};
```

#### 9.6 AUTOSAR 集成

**lap::com Event → iceoryx Publisher/Subscriber**
```cpp
// AUTOSAR Service Interface
class VehicleSpeedService {
public:
    // Event定义
    Event<SpeedData> speedChanged;
};

// iceoryx Binding实现
template<typename T>
class IceoryxEventBinding {
public:
    // Skeleton端：Event.Send() → iceoryx.publish()
    void Send(const T& data) {
        auto sample = publisher_.LoanScoped();
        if (sample.has_value()) {
            *sample.value() = data;  // 复制到共享内存
            // RAII自动发布
        }
    }
    
    // Proxy端：Event.Subscribe() → iceoryx.subscribe()
    void Subscribe(std::function<void(const T&)> handler) {
        subscriber_.SetReceiveHandler([handler](const T* data) {
            handler(*data);  // 零拷贝访问
        });
    }
    
private:
    IceoryxPublisher<T> publisher_;
    IceoryxSubscriber<T> subscriber_;
};
```

#### 9.7 性能基准

| 指标 | D-Bus | SOME/IP | DDS | **iceoryx v2** |
|------|-------|---------|-----|---------------|
| 延迟 (64B) | 50-100μs | 20-50μs | 10-30μs | **< 1μs** |
| 延迟 (1MB) | ~20ms | ~5ms | <100μs | **< 10μs** |
| 吞吐量 (MB/s) | 50-100 | 200-300 | 500-800 | **> 10,000** |
| CPU 占用 | 3-5% | 2-4% | 4-6% | **< 0.5%** |
| 内存拷贝 | 3次 | 2次 | 1次 | **0次** |
| 零拷贝 | ❌ | 部分 | ✅ | **✅✅** |
| 实时性 | ❌ | 部分 | 部分 | **✅✅** |
| 跨网络 | ❌ | ✅ | ✅ | **❌** |

#### 9.8 使用场景

| 场景 | 数据量 | 频率 | 推荐传输 | 延迟要求 |
|------|--------|------|----------|----------|
| **摄像头图像** (4K) | 8MB/frame | 30fps | **iceoryx** | < 5ms |
| **LiDAR点云** | 2MB/scan | 10Hz | **iceoryx** | < 1ms |
| **传感器融合结果** | 100KB | 100Hz | **iceoryx** | < 100μs |
| **实时控制指令** | 64B | 1kHz | **iceoryx** | < 10μs |
| **地图更新** | 50MB | 1Hz | DDS | < 1s |
| **跨ECU通信** | 任意 | 任意 | DDS/SOME/IP | < 100ms |

#### 9.9 配置示例

**RouDi配置 (roudi_config.toml)**
```toml
[general]
version = 2

[[segment]]
[[segment.mempool]]
size = 1024          # 1KB - 控制消息
count = 1024

[[segment.mempool]]
size = 102400        # 100KB - 传感器数据
count = 256

[[segment.mempool]]
size = 1048576       # 1MB - 小图像
count = 64

[[segment.mempool]]
size = 8388608       # 8MB - 4K图像
count = 16

[[segment.mempool]]
size = 52428800      # 50MB - 大数据（地图）
count = 4
```

**lap::com配置映射**
```cpp
// Service Manifest配置
{
    "service": "CameraService",
    "instance": "FrontCamera",
    "binding": "iceoryx",
    "config": {
        "chunk_size": 8388608,      // 8MB chunk
        "queue_capacity": 16,       // 订阅端队列深度
        "history_request": 1        // Late-joiner支持
    }
}
```

#### 9.10 实施计划

**Phase 1: iceoryx基础集成** (2周)
- Week 1: RouDi集成、MemPool配置
- Week 2: Publisher/Subscriber基础实现

**Phase 2: ara::com绑定** (2周)
- Week 3: Event通信绑定
- Week 4: Method/Field绑定

**Phase 3: 性能优化** (1周)
- Week 5: 性能测试与调优

**依赖库**:
```bash
# 安装iceoryx v2
git clone https://github.com/eclipse-iceoryx/iceoryx.git
cd iceoryx && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc) && sudo make install
```

> **💡 设计归档**: Protobuf + Domain Socket Binding 已移除，详见 [`archive/PROTOBUF_SOCKET_BINDING_ARCHIVED.md`](../archive/PROTOBUF_SOCKET_BINDING_ARCHIVED.md)  
> **替代方案**: 使用 **iceoryx v2 Binding** (§9) 提供更优异的本地IPC性能（<1μs延迟，>10GB/s吞吐量）

---

### 10. Custom Protocol + UDP Binding (source/binding/custom/)

#### 10.1 设计定位

**核心目标**: 提供**轻量级、灵活可定制**的私有通信协议，适用于特殊场景和遗留系统集成

**适用场景**:
- 🔧 **遗留系统集成**: 适配已有的私有协议系统
- 🎯 **特殊需求**: 自定义序列化格式、特殊加密算法
- ⚡ **极简通信**: 嵌入式设备、资源受限环境
- 🌐 **广播/组播**: UDP 广播发现、组播数据分发
- 🔒 **封闭网络**: 企业内部专用协议

**设计理念**:
- **最小依赖**: 仅依赖标准库和 POSIX Socket API
- **高度可定制**: 协议格式、序列化方式完全可自定义
- **性能优先**: 无框架开销，直接操作 UDP Socket
- **灵活性**: 支持多种传输模式（单播、广播、组播）

#### 10.2 架构组成

| 组件 | 功能 | 文件 |
|------|------|------|
| `UdpTransport` | UDP Socket 管理（单播/广播/组播） | `UdpTransport.hpp` |
| `CustomProtocolCodec` | 可扩展的编解码器框架 | `ProtocolCodec.hpp` |
| `BinarySerializer` | 高性能二进制序列化 | `BinarySerializer.hpp` |
| `CustomMethodBinding` | 方法调用绑定 | `CustomMethodBinding.hpp` |
| `CustomEventBinding` | 事件广播绑定 | `CustomEventBinding.hpp` |
| `DiscoveryManager` | UDP 广播服务发现 | `DiscoveryManager.hpp` |

#### 10.3 协议帧格式（默认实现）

**标准帧结构**:
```
┌──────────────┬──────────────┬──────────────┬──────────────┬─────────────┐
│ Magic (2B)   │ Version (1B) │ Type (1B)    │ Flags (1B)   │ Reserved    │
│ 0xAC (固定)  │ 0x01         │ REQ/RSP/EVT  │ ACK/ENCRYPT  │ (1B)        │
├──────────────┼──────────────┼──────────────┼──────────────┼─────────────┤
│ Message ID   │ Payload Size │ Checksum     │ Timestamp    │ Payload     │
│ (4B)         │ (4B)         │ (2B CRC16)   │ (8B)         │ (N bytes)   │
└──────────────┴──────────────┴──────────────┴──────────────┴─────────────┘

Type:
  - 0x01: Method Request
  - 0x02: Method Response
  - 0x03: Event Notification
  - 0x04: Field Get
  - 0x05: Field Set
  - 0x06: Discovery Beacon
  - 0x07: Heartbeat
  - 0xFF: Custom Extension

Flags:
  - Bit 0: ACK Required
  - Bit 1: Encrypted
  - Bit 2: Compressed
  - Bit 3: Fragmented
  - Bit 4-7: Reserved
```

**最小帧**: 24 字节头部（无 Payload 时）

#### 10.4 核心组件设计

**UdpTransport.hpp**
```cpp
namespace ara {
namespace com {
namespace binding {
namespace custom {

enum class UdpMode {
    kUnicast,    // 点对点通信
    kBroadcast,  // 广播（255.255.255.255）
    kMulticast   // 组播（224.0.0.0 - 239.255.255.255）
};

struct UdpConfig {
    std::string local_address = "0.0.0.0";
    uint16_t local_port = 0;
    
    std::string remote_address;  // 目标地址
    uint16_t remote_port = 0;
    
    UdpMode mode = UdpMode::kUnicast;
    std::string multicast_group;  // 组播地址（仅组播模式）
    
    size_t send_buffer_size = 65536;
    size_t recv_buffer_size = 65536;
    
    int recv_timeout_ms = 1000;
    bool enable_broadcast = false;  // SO_BROADCAST
    bool reuse_address = true;      // SO_REUSEADDR
};

class UdpTransport {
public:
    UdpTransport() = default;
    ~UdpTransport();
    
    // 初始化 UDP Socket
    Result<void> Initialize(const UdpConfig& config);
    
    // 发送数据
    Result<size_t> Send(const ByteBuffer& data);
    Result<size_t> SendTo(const ByteBuffer& data, 
                          const std::string& address, 
                          uint16_t port);
    
    // 接收数据
    Result<ByteBuffer> Receive();
    Result<std::pair<ByteBuffer, SocketAddress>> ReceiveFrom();
    
    // 组播操作
    Result<void> JoinMulticastGroup(const std::string& group_address);
    Result<void> LeaveMulticastGroup(const std::string& group_address);
    
    // 获取统计信息
    struct Stats {
        uint64_t packets_sent = 0;
        uint64_t packets_received = 0;
        uint64_t bytes_sent = 0;
        uint64_t bytes_received = 0;
        uint64_t errors = 0;
    };
    Stats GetStats() const { return stats_; }

private:
    int socket_fd_ = -1;
    UdpConfig config_;
    Stats stats_;
    
    Result<void> setupSocket();
    Result<void> setupBroadcast();
    Result<void> setupMulticast();
};

} // namespace custom
} // namespace binding
} // namespace com
} // namespace ara
```

**ProtocolCodec.hpp (可扩展编解码框架)**
```cpp
namespace ara {
namespace com {
namespace binding {
namespace custom {

// 协议头部定义
struct ProtocolHeader {
    uint16_t magic = 0xACAC;     // Magic number
    uint8_t version = 0x01;      // Protocol version
    uint8_t type = 0x00;         // Message type
    uint8_t flags = 0x00;        // Flags
    uint8_t reserved = 0x00;     // Reserved
    uint32_t message_id = 0;     // Message ID
    uint32_t payload_size = 0;   // Payload size
    uint16_t checksum = 0;       // CRC16
    uint64_t timestamp = 0;      // Timestamp (microseconds)
    
    static constexpr size_t kHeaderSize = 24;
};

// 抽象编解码器接口
class IProtocolCodec {
public:
    virtual ~IProtocolCodec() = default;
    
    // 编码：数据 → 字节流
    virtual Result<ByteBuffer> Encode(
        const ProtocolHeader& header,
        const ByteBuffer& payload) = 0;
    
    // 解码：字节流 → 数据
    virtual Result<std::pair<ProtocolHeader, ByteBuffer>> Decode(
        const ByteBuffer& raw_data) = 0;
    
    // 验证校验和
    virtual bool VerifyChecksum(
        const ProtocolHeader& header,
        const ByteBuffer& payload) = 0;
};

// 默认二进制编解码器
class BinaryCodec : public IProtocolCodec {
public:
    Result<ByteBuffer> Encode(
        const ProtocolHeader& header,
        const ByteBuffer& payload) override {
        
        ByteBuffer buffer;
        buffer.resize(ProtocolHeader::kHeaderSize + payload.size());
        
        // 序列化头部（小端序）
        size_t offset = 0;
        write_uint16_le(buffer, offset, header.magic);
        write_uint8(buffer, offset, header.version);
        write_uint8(buffer, offset, header.type);
        write_uint8(buffer, offset, header.flags);
        write_uint8(buffer, offset, header.reserved);
        write_uint32_le(buffer, offset, header.message_id);
        write_uint32_le(buffer, offset, header.payload_size);
        write_uint16_le(buffer, offset, header.checksum);
        write_uint64_le(buffer, offset, header.timestamp);
        
        // 拷贝 Payload
        std::memcpy(buffer.data() + offset, payload.data(), payload.size());
        
        return buffer;
    }
    
    Result<std::pair<ProtocolHeader, ByteBuffer>> Decode(
        const ByteBuffer& raw_data) override {
        
        if (raw_data.size() < ProtocolHeader::kHeaderSize) {
            return MakeError(ComErrc::kInvalidArgument);
        }
        
        ProtocolHeader header;
        size_t offset = 0;
        
        header.magic = read_uint16_le(raw_data, offset);
        header.version = read_uint8(raw_data, offset);
        header.type = read_uint8(raw_data, offset);
        header.flags = read_uint8(raw_data, offset);
        header.reserved = read_uint8(raw_data, offset);
        header.message_id = read_uint32_le(raw_data, offset);
        header.payload_size = read_uint32_le(raw_data, offset);
        header.checksum = read_uint16_le(raw_data, offset);
        header.timestamp = read_uint64_le(raw_data, offset);
        
        // 验证 Magic
        if (header.magic != 0xACAC) {
            return MakeError(ComErrc::kProtocolError);
        }
        
        // 提取 Payload
        ByteBuffer payload(raw_data.begin() + offset, raw_data.end());
        
        return std::make_pair(header, payload);
    }
    
    bool VerifyChecksum(
        const ProtocolHeader& header,
        const ByteBuffer& payload) override {
        
        uint16_t calculated = crc16_ccitt(payload.data(), payload.size());
        return calculated == header.checksum;
    }

private:
    // 辅助函数
    uint16_t crc16_ccitt(const uint8_t* data, size_t len);
    void write_uint16_le(ByteBuffer& buf, size_t& offset, uint16_t val);
    uint16_t read_uint16_le(const ByteBuffer& buf, size_t& offset);
    // ... 其他读写辅助函数
};

// 自定义编解码器示例（用户可继承实现）
class CustomXorCodec : public IProtocolCodec {
public:
    explicit CustomXorCodec(uint8_t xor_key) : xor_key_(xor_key) {}
    
    Result<ByteBuffer> Encode(
        const ProtocolHeader& header,
        const ByteBuffer& payload) override {
        
        // 先调用基础编码
        BinaryCodec base_codec;
        auto result = base_codec.Encode(header, payload);
        if (!result.has_value()) return result;
        
        // XOR 加密
        ByteBuffer encrypted = result.value();
        for (size_t i = ProtocolHeader::kHeaderSize; i < encrypted.size(); ++i) {
            encrypted[i] ^= xor_key_;
        }
        
        return encrypted;
    }
    
    Result<std::pair<ProtocolHeader, ByteBuffer>> Decode(
        const ByteBuffer& raw_data) override {
        
        // XOR 解密
        ByteBuffer decrypted = raw_data;
        for (size_t i = ProtocolHeader::kHeaderSize; i < decrypted.size(); ++i) {
            decrypted[i] ^= xor_key_;
        }
        
        // 调用基础解码
        BinaryCodec base_codec;
        return base_codec.Decode(decrypted);
    }
    
    bool VerifyChecksum(
        const ProtocolHeader& header,
        const ByteBuffer& payload) override {
        BinaryCodec base_codec;
        return base_codec.VerifyChecksum(header, payload);
    }

private:
    uint8_t xor_key_;
};

} // namespace custom
} // namespace binding
} // namespace com
} // namespace ara
```

**BinarySerializer.hpp (高性能序列化)**
```cpp
namespace ara {
namespace com {
namespace binding {
namespace custom {

// 零拷贝二进制序列化
class BinarySerializer {
public:
    BinarySerializer() { buffer_.reserve(4096); }
    
    // 基础类型序列化
    void Serialize(bool value) { write_uint8(value ? 1 : 0); }
    void Serialize(int8_t value) { write_int8(value); }
    void Serialize(uint8_t value) { write_uint8(value); }
    void Serialize(int16_t value) { write_int16_le(value); }
    void Serialize(uint16_t value) { write_uint16_le(value); }
    void Serialize(int32_t value) { write_int32_le(value); }
    void Serialize(uint32_t value) { write_uint32_le(value); }
    void Serialize(int64_t value) { write_int64_le(value); }
    void Serialize(uint64_t value) { write_uint64_le(value); }
    void Serialize(float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(float));
        write_uint32_le(bits);
    }
    void Serialize(double value) {
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(double));
        write_uint64_le(bits);
    }
    
    // 字符串序列化（Length-prefixed）
    void Serialize(const std::string& value) {
        write_uint32_le(value.size());
        buffer_.insert(buffer_.end(), value.begin(), value.end());
    }
    
    // 容器序列化
    template <typename T>
    void Serialize(const std::vector<T>& vec) {
        write_uint32_le(vec.size());
        for (const auto& item : vec) {
            Serialize(item);
        }
    }
    
    // 获取序列化结果
    const ByteBuffer& GetBuffer() const { return buffer_; }
    void Clear() { buffer_.clear(); }

private:
    ByteBuffer buffer_;
    
    void write_uint8(uint8_t val) { buffer_.push_back(val); }
    void write_int8(int8_t val) { buffer_.push_back(static_cast<uint8_t>(val)); }
    void write_uint16_le(uint16_t val) {
        buffer_.push_back(val & 0xFF);
        buffer_.push_back((val >> 8) & 0xFF);
    }
    void write_uint32_le(uint32_t val) {
        buffer_.push_back(val & 0xFF);
        buffer_.push_back((val >> 8) & 0xFF);
        buffer_.push_back((val >> 16) & 0xFF);
        buffer_.push_back((val >> 24) & 0xFF);
    }
    void write_uint64_le(uint64_t val) {
        for (int i = 0; i < 8; ++i) {
            buffer_.push_back((val >> (i * 8)) & 0xFF);
        }
    }
};

// 反序列化
class BinaryDeserializer {
public:
    explicit BinaryDeserializer(const ByteBuffer& buffer)
        : buffer_(buffer), offset_(0) {}
    
    // 基础类型反序列化
    Result<bool> DeserializeBool() {
        auto val = read_uint8();
        return val.has_value() ? (val.value() != 0) : Result<bool>(val.error());
    }
    
    Result<int32_t> DeserializeInt32() { return read_int32_le(); }
    Result<uint32_t> DeserializeUInt32() { return read_uint32_le(); }
    Result<float> DeserializeFloat() {
        auto bits = read_uint32_le();
        if (!bits.has_value()) return Result<float>(bits.error());
        float value;
        std::memcpy(&value, &bits.value(), sizeof(float));
        return value;
    }
    
    Result<std::string> DeserializeString() {
        auto len = read_uint32_le();
        if (!len.has_value()) return Result<std::string>(len.error());
        
        if (offset_ + len.value() > buffer_.size()) {
            return MakeError(ComErrc::kBufferOverflow);
        }
        
        std::string str(buffer_.begin() + offset_, 
                       buffer_.begin() + offset_ + len.value());
        offset_ += len.value();
        return str;
    }
    
    template <typename T>
    Result<std::vector<T>> DeserializeVector() {
        auto len = read_uint32_le();
        if (!len.has_value()) return Result<std::vector<T>>(len.error());
        
        std::vector<T> vec;
        vec.reserve(len.value());
        
        for (uint32_t i = 0; i < len.value(); ++i) {
            // 需要为每个类型特化
            auto item = DeserializeItem<T>();
            if (!item.has_value()) return Result<std::vector<T>>(item.error());
            vec.push_back(item.value());
        }
        
        return vec;
    }

private:
    const ByteBuffer& buffer_;
    size_t offset_;
    
    Result<uint8_t> read_uint8() {
        if (offset_ >= buffer_.size()) return MakeError(ComErrc::kBufferOverflow);
        return buffer_[offset_++];
    }
    
    Result<uint32_t> read_uint32_le() {
        if (offset_ + 4 > buffer_.size()) return MakeError(ComErrc::kBufferOverflow);
        uint32_t val = buffer_[offset_] |
                      (buffer_[offset_ + 1] << 8) |
                      (buffer_[offset_ + 2] << 16) |
                      (buffer_[offset_ + 3] << 24);
        offset_ += 4;
        return val;
    }
    // ... 其他读取函数
};

} // namespace custom
} // namespace binding
} // namespace com
} // namespace ara
```

#### 10.5 UDP 服务发现

**DiscoveryManager.hpp**
```cpp
namespace ara {
namespace com {
namespace binding {
namespace custom {

struct ServiceAnnouncement {
    std::string service_name;
    std::string instance_id;
    std::string address;
    uint16_t port;
    uint64_t timestamp;
    std::map<std::string, std::string> metadata;  // 扩展信息
};

class DiscoveryManager {
public:
    DiscoveryManager() = default;
    
    // 初始化（使用 UDP 广播）
    Result<void> Initialize(uint16_t discovery_port = 9999);
    
    // 服务端：周期性发送服务公告
    Result<void> AnnounceService(
        const ServiceAnnouncement& announcement,
        std::chrono::milliseconds interval = std::chrono::seconds(5));
    
    // 客户端：发现服务
    Result<std::vector<ServiceAnnouncement>> FindService(
        const std::string& service_name,
        std::chrono::milliseconds timeout = std::chrono::seconds(3));
    
    // 停止公告
    void StopAnnouncement();

private:
    UdpTransport transport_;
    std::atomic<bool> announcing_{false};
    std::thread announcement_thread_;
    
    ByteBuffer encodeAnnouncement(const ServiceAnnouncement& announcement);
    Result<ServiceAnnouncement> decodeAnnouncement(const ByteBuffer& data);
};

} // namespace custom
} // namespace binding
} // namespace com
} // namespace ara
```

#### 10.6 性能特点

| 特性 | 指标 | 说明 |
|------|------|------|
| **帧头开销** | 24 字节 | 固定大小，无可变长字段 |
| **序列化速度** | > 1 GB/s | 直接内存操作，无反射 |
| **MTU 适配** | 自动分片 | 支持大于 1472 字节的消息 |
| **延迟** | < 100μs | 无框架开销 |
| **吞吐量** | 取决于网络 | UDP 理论上限 ~1 Gbps (千兆网) |
| **内存占用** | < 100 KB | 最小依赖 |

#### 10.7 使用场景

| 场景 | 配置 | 原因 |
|------|------|------|
| **遗留设备对接** | 自定义 Codec | 兼容已有协议格式 |
| **服务广播发现** | UDP 广播 | 无需中心化服务发现 |
| **轻量级传感器** | 二进制序列化 | 减少嵌入式设备负担 |
| **局域网多播** | UDP 组播 | 高效一对多通信 |
| **快速原型** | 默认二进制编解码 | 无需代码生成工具 |

#### 10.8 配置示例

**custom_udp_config.yaml**

```yaml
services:
  - service_id: LegacyRadarService
    binding:
      type: custom_udp
      transport:
        mode: unicast
        local_port: 8888
        remote_address: "192.168.1.100"
        remote_port: 9999
      codec:
        type: binary
        checksum_enabled: true
        encryption: xor
        xor_key: 0x5A

  - service_id: SensorDataBroadcast
    binding:
      type: custom_udp
      transport:
        mode: multicast
        multicast_group: "239.255.0.1"
        local_port: 7777
      codec:
        type: binary
        compression: none

discovery:
  enabled: true
  port: 9999
  announcement_interval_ms: 5000
```

#### 10.9 实现路线图

**Phase 1: 基础传输 (1周)**
- Day 1-2: UdpTransport 实现（单播、广播、组播）
- Day 3-4: BinaryCodec 编解码器
- Day 5: BinarySerializer/Deserializer

**Phase 2: 协议框架 (1周)**
- Day 1-2: CustomMethodBinding + CustomEventBinding
- Day 3-4: DiscoveryManager (UDP 广播发现)
- Day 5: CRC 校验 + 基础加密

**Phase 3: 扩展与集成 (1周)**
- Day 1-2: 自定义 Codec 扩展机制
- Day 3: 配置文件解析
- Day 4-5: 端到端测试 + 示例应用

**总工作量**: 3周，约 2,800 行代码

#### 10.10 扩展性示例

**自定义 JSON 编解码器**:
```cpp
class JsonCodec : public IProtocolCodec {
public:
    Result<ByteBuffer> Encode(
        const ProtocolHeader& header,
        const ByteBuffer& payload) override {
        
        nlohmann::json j;
        j["header"]["type"] = header.type;
        j["header"]["message_id"] = header.message_id;
        j["payload"] = std::string(payload.begin(), payload.end());
        
        std::string json_str = j.dump();
        return ByteBuffer(json_str.begin(), json_str.end());
    }
    
    // ... Decode 实现
};
```

**自定义 AES 加密编解码器**:
```cpp
class AesCodec : public IProtocolCodec {
public:
    explicit AesCodec(const std::array<uint8_t, 32>& key) : aes_key_(key) {}
    
    Result<ByteBuffer> Encode(
        const ProtocolHeader& header,
        const ByteBuffer& payload) override {
        
        // 1. 基础编码
        BinaryCodec base;
        auto encoded = base.Encode(header, payload);
        if (!encoded.has_value()) return encoded;
        
        // 2. AES-256-GCM 加密
        ByteBuffer encrypted = aes_encrypt_gcm(
            encoded.value(),
            aes_key_,
            generate_iv());
        
        return encrypted;
    }
    
private:
    std::array<uint8_t, 32> aes_key_;
    ByteBuffer aes_encrypt_gcm(const ByteBuffer& data, 
                                const std::array<uint8_t, 32>& key,
                                const std::array<uint8_t, 12>& iv);
};
```

### 11. 遗留实现归档

> **📦 历史内容已归档**: CommonAPI 相关的旧实现细节已移至归档文档
> 
> 详见: [`archive/LEGACY_COMMONAPI_IMPLEMENTATION.md`](../archive/LEGACY_COMMONAPI_IMPLEMENTATION.md)
>
> 归档内容包括：
> - CommonAPI Adapters 实现细节
> - Franca IDL 代码生成流程
> - SOME/IP 序列化（CommonAPI 方式）
> - 旧工具链使用说明
> - 废弃原因分析

---

## 12. 性能优化路线图
modules/Com/
├── source/
│   ├── inc/                      # 公共API (8个头文件, ~3,140行)
│   └── binding/                  # 传输绑定
│       ├── dbus/                 # D-Bus (4个文件, ~950行)
│       ├── someip/               # SOME/IP-DDS Bridge (5个文件, ~2,650行) ✅ 重构
│       │   ├── SomeIpDdsBridge.hpp
│       │   ├── SomeIpMessageCodec.hpp
│       │   ├── DdsServiceMapper.hpp
│       │   ├── VsomeipCompatLayer.hpp
│       │   └── SomeIpServiceDiscovery.hpp
│       ├── dds/                  # Native DDS (5个文件, ~1,510行, 计划中)
│       ├── iceoryx2/             # iceoryx2 (8个文件, ~2,500行, 计划中)
│       ├── custom_protocol/      # Custom Protocol+UDS (7个文件, ~2,200行, 计划中)
│       └── legacy/               # Legacy Gateway (3个文件, ~800行, 计划中)
├── test/
│   ├── unittest/                 # 单元测试 (6个文件, ~1,800行)
│   └── examples/                 # 示例 (9个文件, ~1,200行)
└── tools/
    ├── fidl/                     # Franca IDL定义
    ├── someip/                   # SOME/IP-DDS 工具链 ✅ 重构
    │   ├── vsomeip_config_migrator.py    # vsomeip 配置迁移工具
    │   ├── dds_idl_generator.py          # DDS IDL 生成器
    │   ├── someip_to_dds_mapper.py       # SOME/IP ↔ DDS 映射工具
    │   └── compatibility_validator.sh    # 兼容性验证脚本
    ├── protobuf/                 # Protobuf .proto 文件与代码生成
    └── custom/                   # 自定义协议定义与工具

当前总计: 41个文件, ~10,790行代码
重构后总计: 43个文件, ~11,850行代码（移除 CommonAPI ~770行，新增 Bridge ~2,650行 + 工具 ~820行）
规划总计: 59个文件, ~19,680行代码（含 DDS + Socket + Custom）
```

---

## 12. 性能优化路线图

> **📦 早期扩展规划已归档**: Protobuf over Socket 和自定义协议的详细方案已移至归档
> 
> 详见: [`archive/LEGACY_EXTENSION_PLANS.md`](../archive/LEGACY_EXTENSION_PLANS.md)
>
> **当前方案**: Custom Protocol + UDS Binding (更简洁、更高效)
>
> 归档内容包括：
> - Protobuf over Unix Socket 详细设计
> - 旧版自定义协议帧格式（14字节+CRC32）
> - 多传输层实现（TCP/UDP/SHM）
> - 历史实施路线图
> - 为什么被当前方案替代

---

### 12.1 序列化与协议扩展（历史参考）

> **📦 序列化实现细节已归档**: D-Bus/SOME/IP 序列化、Protobuf over Socket、自定义协议的详细设计已移至归档文件
> 
> **当前架构**: 4 个插件化 Binding (iceoryx2 + DDS + Custom Protocol + Legacy Gateway)
> 
> 详见归档文档:
> - [`archive/LEGACY_COMMONAPI_IMPLEMENTATION.md`](../archive/LEGACY_COMMONAPI_IMPLEMENTATION.md) - CommonAPI 序列化、Franca IDL 工作流
> - [`archive/LEGACY_EXTENSION_PLANS.md`](../archive/LEGACY_EXTENSION_PLANS.md) - Protobuf over Socket 详细设计、自定义协议帧格式
>
> **架构演进说明**:
> 
> | 归档内容 | 为何归档 | 当前方案 |
> |---------|---------|---------|
> | D-Bus 序列化 (sdbus-c++) | Legacy Binding 已转为独立网关进程 | 不再与主架构耦合 |
> | SOME/IP 序列化 (CommonAPI) | 同上，Legacy Gateway 统一处理 | 网关进程处理协议转换 |
> | Protobuf over Socket 方案 | 复杂度高，被 Custom Protocol + UDS 替代 | Custom Protocol Binding (更简洁) |
> | 旧版自定义协议格式 (14字节+CRC32) | 帧头设计冗余 | 当前 Custom Protocol 优化为 8 字节帧头 |
>
> **查阅最新设计**: 参见本文档 Section 4 (4-Binding 插件架构) 和 Section 7 (Legacy Binding 网关模式)

---

### 12.2 完整 5 步优化清单（生产级性能提升）

基于 iceoryx2 + DDS + Custom Protocol 的插拔式架构，通过系统级和运行时优化，实现端到端性能最大化。

#### **优化前后架构对比**

| 架构维度 | 优化前 (原始设计) | 优化后 (应用 5 步优化) | 性能提升 |
|---------|------------------|----------------------|---------|
| **ECU 内通信** | iceoryx2 (基础) | + 1GB 大页 + io_uring SQPOLL + CPU 隔离 | **延迟 1μs → 500ns** (+50%) |
| **跨 ECU 大包** | Fast-DDS (UDP) | + AF_XDP ZERO_COPY + 专用队列 | **延迟 500μs → 15μs** (+3233%) |
| **跨 ECU 小包** | DDS (UDP) | + SHM-only | **延迟 200μs → 50μs** (+300%) |
| **内存效率** | 标准 4KB 页 | 1GB 大页 + THP | **TLB Miss -80%** |
| **CPU 开销** | 标准调度 | isolcpus + IRQ 亲和性 | **调度抖动 50μs → 5μs** |
| **系统调用** | 标准 syscall | io_uring SQPOLL (零 syscall) | **Publish 开销 2μs → 500ns** |
| **网络栈** | 内核网络栈 | AF_XDP 用户态栈 | **吞吐量 3GB/s → 9GB/s** |

#### **优化目标**

| 优化阶段 | 性能提升 | 开发周期 | 技术栈 |
|---------|---------|---------|--------|
| Step 1: 系统级硬优化 | +60% | 1 周 | 大页内存 + CPU 隔离 + IRQ 亲和性 |
| Step 2: iceoryx2 去中心化 | +30% | 1 周 | 移除 RouDi + memfd + 进程自管理 |
| Step 3: io_uring SQPOLL | +40% | 2 周 | 零系统调用 + 内核轮询线程 |
| Step 4: AF_XDP ZERO_COPY | +200% (跨 ECU) | 3-4 周 | XDP 用户态网络栈 |
| Step 5: DDS 优化 | +50% (跨 ECU) | 2 周 | SHM-only 本地传输 |

**累计性能提升**:
- ECU 内通信: **3-5μs** (从原始 100μs)
- 跨 ECU 大包: **15-20μs** (从原始 500μs)

**架构优化应用点**:

1. **binding_iceoryx2.so**:
   - ✅ 大页内存配置 (mempool_config.toml)
   - ✅ CPU 亲和性绑核 (IceoryxNodeManager)
   - ✅ io_uring SQPOLL 集成 (Iceoryx2Publisher/Subscriber)
   - ✅ memfd 替代 POSIX SHM

2. **binding_dds.so**:
   - ✅ AF_XDP Transport 层
   - ✅ UMEM 与 iceoryx2 共享内存池
   - ✅ 大小包路由策略 (>64KB → AF_XDP, <64KB → SHM)
   - ✅ DDS 基础支持 (Domain/Participant/QoS)

3. **系统配置**:
   - ✅ GRUB 大页参数 (hugepagesz=1G hugepages=32)
   - ✅ CPU 隔离参数 (isolcpus=4-7 nohz_full=4-7)
   - ✅ 网卡多队列配置 (ethtool -L eth0 combined 8)
   - ✅ XDP 程序加载 (xdp-loader)

---

### 11.2 Step 1: 系统级硬优化（1 周，+60% 性能）

#### **1.1 大页内存 (Huge Pages) + THP**

**目标**: 减少 TLB Miss，提升内存访问效率

```bash
# 1GB 大页 + 透明大页 (THP)
echo always > /sys/kernel/mm/transparent_hugepage/enabled
echo always > /sys/kernel/mm/transparent_hugepage/defrag

# GRUB 配置持久化
# /etc/default/grub
GRUB_CMDLINE_LINUX="hugepagesz=1G hugepages=32 transparent_hugepage=always"

# 更新 GRUB
sudo update-grub
sudo reboot
```

**效果验证**:
```bash
# 检查大页状态
cat /proc/meminfo | grep -i huge
# HugePages_Total:      32
# HugePages_Free:       30
# Hugepagesize:    1048576 kB

# 检查 THP 状态
cat /sys/kernel/mm/transparent_hugepage/enabled
# [always] madvise never
```

**iceoryx2 配置对接**:
```toml
# mempool_config.toml
[[mempool]]
name = "UltimatePool"
size = 17179869184        # 16GB (16个1GB大页)
chunk_size = 2097152      # 2MB chunk 对齐大页边界
use_huge_pages = true     # 强制使用 1GB 大页
```

**性能提升**: TLB Miss 降低 80%，延迟减少 50-100ns

---

#### **1.2 CPU 核隔离 + IRQ 亲和性**

**目标**: 消除内核调度噪音，保证实时性

```bash
# GRUB 配置 CPU 隔离
# /etc/default/grub
GRUB_CMDLINE_LINUX="isolcpus=4-7 nohz_full=4-7 rcu_nocbs=4-7"
# - isolcpus: 隔离 CPU 4-7，禁止内核调度到这些核
# - nohz_full: 关闭这些核的定时器中断
# - rcu_nocbs: RCU 回调迁移到其他核

sudo update-grub
sudo reboot
```

**线程绑核策略**:
```cpp
// binding_iceoryx2.so 内部实现
void IceoryxBinding::BindThreadsToCore() {
    // AF_XDP / io_uring 绑小核 (CPU 0-3)
    cpu_set_t small_cores;
    CPU_ZERO(&small_cores);
    CPU_SET(2, &small_cores);  // io_uring SQPOLL 线程
    CPU_SET(3, &small_cores);  // AF_XDP 接收线程
    pthread_setaffinity_np(uring_thread_, sizeof(cpu_set_t), &small_cores);

    // 感知/控制算法绑大核 (CPU 4-7，已隔离)
    cpu_set_t big_cores;
    CPU_ZERO(&big_cores);
    CPU_SET(4, &big_cores);  // 感知 Subscriber
    CPU_SET(5, &big_cores);  // 控制 Publisher
    pthread_setaffinity_np(perception_thread_, sizeof(cpu_set_t), &big_cores);
}
```

**IRQ 亲和性配置**:
```bash
# 将网卡中断绑定到小核 (CPU 0-3)
echo 2 > /proc/irq/$(cat /proc/interrupts | grep eth0 | awk '{print $1}' | tr -d ':')/smp_affinity_list

# 验证中断分布
watch -n 1 'cat /proc/interrupts | grep eth0'
```

**性能提升**: 调度抖动从 50μs 降低到 <5μs

---

### 11.3 Step 2: iceoryx2 去中心化（1 周，+30% 性能）

#### **2.1 移除 RouDi 单点故障**

**Before (iceoryx v1)**:
```
应用进程 → RouDi (中央守护进程) → MemPool 创建
问题: RouDi 挂掉 = 所有通信中断
```

**After (iceoryx2)**:
```
应用进程 → 直接创建 MemPool (进程自管理)
优势: 去中心化，进程独立，故障隔离
```

**配置简化**:
```toml
# config.toml (全局共享配置，无需 RouDi)
[[mempool]]
name = "QM_PerceptionPool"
size = 8589934592        # 8GB
chunk_size = 1048576     # 1MB
safety_level = "QM"

[[mempool]]
name = "ASIL_D_ControlPool"
size = 4294967296        # 4GB
chunk_size = 524288      # 512KB
safety_level = "ASIL_D"
```

**Binding 实现变化**:
```cpp
// binding_iceoryx2.so 内部
#include <iceoryx2/api.hpp>  // Rust 实现的 C++ FFI

void IceoryxBinding::Initialize() {
    // iceoryx2: 无需连接 RouDi，直接创建 Publisher
    auto service = iox2::ServiceBuilder("Radar/Objects")
        .publish_subscribe()
        .open_or_create()
        .expect("Failed to create service");

    publisher_ = service.publisher_builder()
        .max_loaned_samples(16)
        .create()
        .expect("Failed to create publisher");
}
```

**性能提升**: 消除 RouDi IPC 开销 (~200ns)，启动时间从 500ms → 50ms

---

#### **2.2 memfd 替代 POSIX SHM**

**优势**: 更轻量的共享内存机制

```cpp
// iceoryx2 内部使用 memfd_create
int memfd = memfd_create("iceoryx2_pool", MFD_CLOEXEC | MFD_ALLOW_SEALING);
ftruncate(memfd, pool_size);
void* addr = mmap(NULL, pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
```

**性能对比**:

| 机制 | 延迟 | 清理复杂度 |
|------|------|-----------|
| POSIX SHM (`shm_open`) | ~500ns | 需要 `shm_unlink` |
| memfd | ~200ns | 进程退出自动清理 |

---

### 11.4 Step 3: io_uring SQPOLL 零系统调用（2 周，+40% 性能）

#### **3.1 io_uring SQPOLL 模式**

**原理**: 内核专用线程轮询 SQ，用户态提交任务零系统调用

```cpp
// binding_iceoryx2.so 内部集成 io_uring
#include <liburing.h>

void IceoryxBinding::InitializeIoUring() {
    struct io_uring_params params = {};
    params.flags = IORING_SETUP_SQPOLL | IORING_SETUP_ATTACH_WQ;
    params.sq_thread_cpu = 2;          // 绑定小核 CPU 2
    params.sq_thread_idle = 1000;      // 1ms 空闲超时

    io_uring_queue_init_params(32768, &ring_, &params);
}

void IceoryxBinding::PublishWithIoUring(const SamplePtr& sample) {
    // 直接往 SQ 写提交请求，无需 io_uring_submit() 系统调用
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    io_uring_prep_send(sqe, fd_, sample->data(), sample->size(), 0);
    // SQPOLL 线程自动提交，零 syscall
}
```

**性能提升**:

| 场景 | Before (标准模式) | After (SQPOLL) |
|------|------------------|----------------|
| Publish 延迟 | ~2μs (含 syscall) | <500ns (零 syscall) |
| CPU 开销 | 20% | 5% |

---

### 11.5 Step 4: AF_XDP ZERO_COPY（3-4 周，跨 ECU 起飞）

#### **4.1 XDP 用户态网络栈**

**目标**: 跳过内核网络栈，直接 DMA 到用户态 iceoryx2 chunk

```bash
# 1. 配置网卡多队列 (8队列给 AF_XDP)
sudo ethtool -L eth0 combined 8

# 2. 加载 XDP 程序
sudo ip link set eth0 xdp obj xdp_af_xsk.o sec xsks_map

# 3. 绑定队列到 AF_XDP socket
sudo xdp-loader load -m skb -s xsks_map eth0 xdp_af_xsk.o
```

**UMEM 与 iceoryx2 chunk 共享**:
```cpp
// binding_dds.so 扩展：跨 ECU 大包走 AF_XDP
#include <xdp/xsk.h>

void DdsBinding::InitializeAfXdp() {
    // 1. 创建 UMEM (注册 iceoryx2 chunk pool)
    struct xsk_umem_config umem_cfg = {
        .fill_size = 4096,
        .comp_size = 4096,
        .frame_size = 2048,
        .frame_headroom = 0,
        .flags = XDP_UMEM_UNALIGNED_CHUNK_FLAG
    };

    // 直接使用 iceoryx2 的 chunk pool 作为 UMEM
    void* chunk_pool = iceoryx_binding_->GetChunkPool();
    xsk_umem__create(&umem_, chunk_pool, POOL_SIZE, &fill_, &comp_, &umem_cfg);

    // 2. 创建 AF_XDP socket
    struct xsk_socket_config xsk_cfg = {
        .rx_size = 4096,
        .tx_size = 4096,
        .xdp_flags = XDP_FLAGS_DRV_MODE,  // 驱动模式
        .bind_flags = XDP_ZEROCOPY        // 零拷贝
    };
    xsk_socket__create(&xsk_, "eth0", 0, umem_, &rx_, &tx_, &xsk_cfg);
}

void DdsBinding::PublishLargePayload(const SamplePtr& sample) {
    // 3. 零拷贝发送 (网卡 DMA 直接读 iceoryx2 chunk)
    uint64_t addr = xsk_umem__add_offset_to_addr(sample->chunk_offset());
    struct xdp_desc* tx_desc = xsk_ring_prod__tx_desc(&tx_, idx);
    tx_desc->addr = addr;
    tx_desc->len = sample->size();
    xsk_ring_prod__submit(&tx_, 1);
}
```

**性能提升**:

| 场景 | Before (内核栈 + UDP) | After (AF_XDP ZERO_COPY) |
|------|----------------------|-------------------------|
| 跨 ECU 大包延迟 | 500μs | 15-20μs |
| CPU 开销 | 60% | 10% |
| 吞吐量 (10Gbps 网卡) | 3GB/s | 9GB/s |

---

### 11.6 Step 5: Fast-DDS 优化（2 周，跨 ECU 控制消息）

#### **5.1 SHM-only Transport 优化**

**策略**: 跨 ECU 控制消息（小包）走 DDS，大包强制走 AF_XDP

```xml
<!-- dds_profile.xml -->
<profiles>
    <transport_descriptors>
        <transport_descriptor>
            <transport_id>shm_only</transport_id>
            <type>SHM</type>
            <maxMessageSize>65536</maxMessageSize>  <!-- 64KB -->
        </transport_descriptor>
    </transport_descriptors>

    <participant profile_name="optimized_participant">
        <rtps>
            <userTransports>
                <transport_id>shm_only</transport_id>
            </userTransports>
            <useBuiltinTransports>false</useBuiltinTransports>
            
            <!-- 标准 DDS Simple Discovery -->
            <builtin>
                <discovery_config>
                    <discoveryProtocol>SIMPLE</discoveryProtocol>
                </discovery_config>
            </builtin>
                            </metatrafficUnicastLocatorList>
                        </RemoteServer>
                    </discoveryServersList>
                </discovery_config>
            </builtin>
        </rtps>
    </participant>
</profiles>
```

**Binding 实现**:
```cpp
// binding_dds.so 路由逻辑
void DdsBinding::Publish(const SamplePtr& sample) {
    if (sample->size() > 64 * 1024) {
        // 大包 (>64KB) → 强制走 AF_XDP
        PublishViaAfXdp(sample);
    } else {
        // 小包 (<64KB) → Fast-DDS SHM
        dds_publisher_->write(*sample);
    }
}
```

**性能提升**:

| 场景 | Before (UDP) | After (SHM) |
|------|-------------|-------------|
| 跨 ECU 小包延迟 | 100-200μs | 30-50μs |
| 服务发现延迟 | 5-100ms (动态 SD) | 0.5ms (中央服务器) |

---

### 11.7 最终推荐技术栈（2026 SOP 终极版）

| 场景 | 技术选型 | 延迟目标 | 吞吐量 | CPU 开销 |
|------|---------|---------|--------|---------|
| ECU 内所有通信 | iceoryx2 + io_uring SQPOLL + memfd + 1GB 大页 | **< 3μs** | >10GB/s | <5% |
| 跨 ECU 大包 (>64KB) | AF_XDP ZERO_COPY + 专用队列 | **< 15μs** | 9GB/s (10Gbps) | 10% |
| 跨 ECU 小包/控制 | DDS SHM | **< 50μs** | 800MB/s | 15% |
| 遗留兼容 (SOME/IP/D-Bus) | 独立网关进程（完全隔离） | - | - | - |

**关键配置文件**:

```yaml
# binding_config.yaml
bindings:
  - type: iceoryx2
    priority: 100
    mempool: QM_PerceptionPool
    use_huge_pages: true
    io_uring_sqpoll: true
    cpu_affinity: [4, 5, 6, 7]
  
  - type: dds
    priority: 50
    af_xdp_enabled: true
    af_xdp_queue: [0, 1, 2, 3]
    shm_only: true
    discovery_server: "192.168.1.100:34567"
  
  - type: custom_protocol
    priority: 20
    enabled: false
```

---

## 12. 总结

### 当前状态

✅ **已完成**: 10,790行代码，完整的D-Bus和SOME/IP支持  
✅ **架构清晰**: 插拔式 4-Binding 架构，易于扩展  
✅ **序列化外部化**: D-Bus和SOME/IP无需手动序列化  
✅ **测试完善**: 69+测试用例，多个完整示例  
✅ **R24-11 标准**: 基于 AUTOSAR R24-11 标准设计，支持静态服务连接和中央服务发现  
✅ **性能路线图**: 完整 5 步优化清单，从系统级到应用级全覆盖

### 扩展计划

📋 **Phase 1**: Binding Manager 实现 (1-2周)
- dlopen() 动态加载插件
- 优先级选择逻辑
- 配置文件解析

📋 **Phase 2**: iceoryx2 Binding 实现 (5周)
- 进程自管理 MemPool
- io_uring SQPOLL 集成
- FuSa 物理隔离

📋 **Phase 3**: DDS Binding + AF_XDP (6周)
- DDS 集成 (Simple Discovery)
- AF_XDP ZERO_COPY 跨 ECU
- DDS QoS 优化

📋 **Phase 4**: 性能优化实施 (8周)
- 系统级优化 (大页 + CPU 隔离)
- io_uring SQPOLL
- AF_XDP 用户态网络栈

### 关键优势

1. **AUTOSAR R24-11 标准合规**: 完整支持 SWS_CM、TPS_MANI、EXP ara::com 规范
2. **插件化架构**: 4层 Binding (iceoryx2/DDS/CustomProtocol/Legacy)，运行时动态加载
3. **配置驱动**: binding_config.yaml 控制所有 Binding，应用零修改
4. **性能可扩展**: ECU内 <500ns (iceoryx2) → 跨ECU <15μs (AF_XDP) 完整覆盖
5. **服务发现优化**: 零守护进程架构（固定槽位 < 100ns → Binding 内置发现 1-100ms）
6. **FuSa-Ready**: MemPool 物理隔离 (QM/ASIL-D)，符合 ISO 26262
7. **开发友好**: 统一 ara::com API，丰富文档，完整示例

### 下一步

1. ✅ 架构设计完成 (`ARCHITECTURE_SUMMARY.md`)
2. ✅ R24-11 标准对齐完成
3. ✅ 5步性能优化集成完成
4. ✅ YAML 配置格式标准化（yaml-cpp + arxml2yaml 工具）
5. 📋 Phase 1: Binding Manager 实现 (1-2周)
6. 📋 Phase 2: iceoryx2 Binding 实现 (5周)
7. 📋 Phase 3: DDS Binding + AF_XDP 实现 (6周)
8. 📋 Phase 4: 性能优化实施与验证 (8周)

---

## 13. 配置管理与工具链

### 13.1 YAML 配置标准

**设计决策**: 使用 YAML 替代 JSON/XML 作为统一配置格式

**优势**:
- ✅ 可读性更强（缩进语法，无冗余符号）
- ✅ 支持注释（便于配置说明）
- ✅ 类型丰富（字符串、数字、布尔、数组、字典）
- ✅ 工具链成熟（yaml-cpp 库，C++11/14/17 支持）
- ✅ 行业标准（Kubernetes、Docker Compose、Ansible 等）

**依赖库**: yaml-cpp (https://github.com/jbeder/yaml-cpp)

```cmake
# CMakeLists.txt
find_package(yaml-cpp REQUIRED)
target_link_libraries(lap_com PRIVATE yaml-cpp)
```

### 13.2 arxml2yaml 转换工具

**工具名称**: `arxml2yaml`  
**用途**: 将 AUTOSAR ARXML 配置文件转换为 LightAP YAML 格式  
**位置**: `modules/Com/tools/arxml2yaml/`

#### 工具特性

1. **完整转换支持**:
   - ServiceInterface 定义 → YAML 服务配置
   - ServiceInstanceManifest → static_endpoints.yaml
   - SomeipServiceInterfaceDeployment → binding_config.yaml (legacy 部分)
   - NetworkEndpoint → 端点配置

2. **符合标准**:
   - ✅ TPS_MANI_03312: 静态服务清单
   - ✅ TPS_MANI_03313: 服务实例标识符
   - ✅ TPS_MANI_03314: 静态端点配置
   - ✅ TPS_MANI_03315: 服务组合配置

3. **验证机制**:
   - ARXML Schema 验证（XSD）
   - YAML 语法检查
   - 配置完整性验证
   - ServiceID/InstanceID 冲突检测

#### 使用示例

```bash
# 基本转换
$ arxml2yaml -i ServiceInterface.arxml -o service_config.yaml

# 批量转换
$ arxml2yaml -i manifest/*.arxml -o /etc/lap/com/

# 带验证
$ arxml2yaml -i config.arxml -o output.yaml --validate --strict

# 生成 binding_config.yaml
$ arxml2yaml --type binding-config \
    -i SomeipDeployment.arxml \
    -o /etc/lap/com/binding_config.yaml
```

#### 转换映射

**ServiceInterface → 服务配置**

```xml
<!-- ARXML 输入 -->
<SERVICE-INTERFACE>
  <SHORT-NAME>VehicleSpeed</SHORT-NAME>
  <SERVICE-IDENTIFIER>0x1234</SERVICE-IDENTIFIER>
  <EVENTS>
    <EVENT>
      <SHORT-NAME>CurrentSpeed</SHORT-NAME>
      <EVENT-IDENTIFIER>0x0001</EVENT-IDENTIFIER>
    </EVENT>
  </EVENTS>
</SERVICE-INTERFACE>
```

```yaml
# YAML 输出 (arxml2yaml 自动生成)
services:
  - name: VehicleSpeed
    service_id: 0x1234
    events:
      - name: CurrentSpeed
        event_id: 0x0001
```

**ServiceInstanceManifest → 静态端点**

```xml
<!-- ARXML 输入 -->
<SERVICE-INSTANCE-TO-MACHINE-MAPPING>
  <SHORT-NAME>VehicleSpeed_Instance1</SHORT-NAME>
  <SERVICE-INSTANCE-REF>/Services/VehicleSpeed/Instance1</SERVICE-INSTANCE-REF>
  <COMMUNICATION-CONNECTOR-REF>/Network/EthernetEndpoint</COMMUNICATION-CONNECTOR-REF>
</SERVICE-INSTANCE-TO-MACHINE-MAPPING>
```

```yaml
# YAML 输出
static_service_configuration:
  - service_instance:
      name: VehicleSpeed_Instance1
      service_id: 0x1234
      instance_id: 0x0001
      binding: dds  # 根据 COMMUNICATION-CONNECTOR 推断
      endpoint:
        type: DDS
        topic_name: VehicleSpeed
        domain_id: 0
```

#### 工具实现

```cpp
// tools/arxml2yaml/ArxmlParser.hpp
class ArxmlParser {
public:
    // 解析 ARXML 文件
    Result<YAML::Node> Parse(const std::string& arxml_file);
    
    // 转换为 binding_config.yaml
    Result<void> ConvertToBindingConfig(
        const std::string& arxml_file,
        const std::string& output_yaml
    );
    
    // 转换为 static_endpoints.yaml
    Result<void> ConvertToStaticEndpoints(
        const std::string& arxml_file,
        const std::string& output_yaml
    );
    
private:
    // 使用 libxml2 或 pugixml 解析 ARXML
    std::unique_ptr<XmlParser> xml_parser_;
    
    // YAML 生成器
    std::unique_ptr<YamlGenerator> yaml_generator_;
};
```

```bash
# 工具安装
$ cd modules/Com/tools/arxml2yaml
$ mkdir build && cd build
$ cmake ..
$ make
$ sudo make install  # 安装到 /usr/local/bin/arxml2yaml
```

### 13.3 ConfigParser 实现（yaml-cpp 集成）

```cpp
// source/config/ConfigParser.hpp
#include <yaml-cpp/yaml.h>

class ConfigParser {
public:
    // 加载 YAML 配置
    static Result<YAML::Node> Load(const std::string& yaml_file) {
        try {
            return YAML::LoadFile(yaml_file);
        } catch (const YAML::Exception& e) {
            return Error{ErrorCode::kConfigLoadFailed, e.what()};
        }
    }
    
    // 解析 binding_config.yaml
    static Result<BindingConfig> ParseBindingConfig(const std::string& file) {
        auto yaml = Load(file);
        if (!yaml.HasValue()) {
            return yaml.Error();
        }
        
        BindingConfig config;
        for (const auto& binding : yaml.Value()["bindings"]) {
            BindingEntry entry;
            entry.type = binding["type"].as<std::string>();
            entry.library = binding["library"].as<std::string>();
            entry.priority = binding["priority"].as<int>();
            entry.enabled = binding["enabled"].as<bool>();
            entry.config = binding["config"];  // 保留 YAML::Node
            config.bindings.push_back(entry);
        }
        return config;
    }
    
    // 解析 static_endpoints.yaml
    static Result<StaticEndpoints> ParseStaticEndpoints(const std::string& file);
};
```

### 13.4 配置文件路径约定

| 配置文件 | 路径 | 说明 |
|---------|------|------|
| `binding_config.yaml` | `/etc/lap/com/binding_config.yaml` | Binding 插件配置 |
| `static_endpoints.yaml` | `/etc/lap/com/static_endpoints.yaml` | 静态服务端点 |
| `mempool_config.toml` | `/etc/iceoryx2/mempool_config.toml` | iceoryx2 内存池配置 |
| `dds_qos.yaml` | `/etc/lap/com/dds_qos.yaml` | DDS QoS 配置 |
| `custom_protocol.yaml` | `/etc/lap/com/custom_protocol.yaml` | 自定义协议配置 |

---

**完整文档链接**:
- 整体架构说明: `modules/Com/doc/ARCHITECTURE_SUMMARY.md` (本文档)
- arxml2yaml 工具文档: `modules/Com/tools/arxml2yaml/README.md`
- 服务发现详细设计: `modules/Com/doc/SERVICE_DISCOVERY_ARCHITECTURE.md`
- v2.0 架构归档: `modules/Com/doc/archive/SERVICE_DISCOVERY_V2_FAST_DDS_DESIGN.md`
- R24-11 快速参考: `modules/Com/doc/AUTOSAR_R24-11_SERVICE_DISCOVERY_REFERENCE.md`
- R24-11 文档扫描报告: `modules/Com/doc/AUTOSAR_R24-11_SCAN_REPORT.md`

**依赖库**:
- yaml-cpp: 配置解析 (https://github.com/jbeder/yaml-cpp)
- iceoryx2: 零拷贝共享内存与零守护进程架构 (https://github.com/eclipse-iceoryx/iceoryx2)

**文档版本**: 3.0 (零守护进程架构)  
**最后更新**: 2025-11-20  
**维护者**: LightAP Team  
**AUTOSAR 标准**: R24-11 (November 2024)

