# Transport Binding Architecture

> 本文档从 [ARCHITECTURE_SUMMARY.md](ARCHITECTURE_SUMMARY.md) 拆分而来，
> 包含所有 Transport Binding 相关的架构设计、配置与实现细节。
>
> **最后更新**: 2026-03-01  
> **AUTOSAR 标准**: AP R25-11 (向前兼容 R24-11)

---

## 目录

- [1. Binding 配置](#1-binding-配置)
- [2. Binding Manager（运行时插件系统）](#2-binding-manager运行时插件系统)
- [3. Proxy/Skeleton ↔ Binding 数据路径](#3-proxyskeleton--binding-数据路径-transport-wiring)
- [4. Core IPC Binding](#4-core-ipc-binding-sourcebindingcoreipc-)
- [5. DDS Transport Binding](#5-dds-transport-binding-sourcebindingdds-)
- [6. 序列化策略](#6-序列化策略-符合-autosar-标准)
- [7. Custom Protocol + UDS Binding](#7-custom-protocol--uds-binding-sourcebindingcustom_protocol-)
- [8. Custom Protocol + UDP Binding](#8-custom-protocol--udp-binding-sourcebindingcustom-)
- [9. Legacy Binding](#9-legacy-binding-sourcebindinglegacy-)
- [10. 遗留实现归档](#10-遗留实现归档)

---

## 1. Binding 配置

### 1.1 binding_config.yaml（Binding Manager 配置）

```yaml
# LightAP Com Module - Binding Configuration
# 依赖: yaml-cpp (https://github.com/jbeder/yaml-cpp)
# 转换工具: arxml2yaml (AUTOSAR ARXML → YAML)

bindings:
  - type: coreipc
    library: /usr/lib/lap/com/binding_coreipc.so
    priority: 100
    enabled: true
    config:
      domain_name: lightap_com
      mempool_config: /etc/lightap/core_ipc_config.toml
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
      # AF_XDP: 优化升级项，独立处理（当前未实现）
      # af_xdp_enabled: false
      # af_xdp_config:
      #   interface: eth0
      #   queue_ids: [0, 1, 2, 3]
      #   umem_shared_with_coreipc: true
      #   zero_copy: true
      #   xdp_mode: drv
      payload_routing:
        large_payload_threshold: 65536
        large_payload_transport: shm  # 当前默认 SHM，AF_XDP 作为未来优化项
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

### 1.2 static_endpoints.yaml（静态服务配置，R25-11 SWS_CM_02201）


```yaml
# 静态服务配置 - 可通过 arxml2yaml 工具从 AUTOSAR ARXML 转换
# 符合 TPS_MANI_03312-03315 规范

static_service_configuration:
  - service_instance:
      service_id: 0x1234
      instance_id: 0x0001
      binding: coreipc
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

### 1.3 core_ipc_config.toml（Core IPC 配置示例）

```toml
# Core IPC 配置 - 进程自管理，基于 lap::core::ipc
[domain]
name = "lightap_com"

# 摄像头服务
[[services]]
name = "camera_front"

[services.publisher]
max_chunks = 64
chunk_size = 8388608  # 8MB（4K 摄像头帧）
publish_timeout = 100000000  # 100ms

[[services]]
name = "lidar_points"

[services.publisher]
max_chunks = 128
chunk_size = 2097152  # 2MB（LiDAR 点云）
max_publishers = 5
max_channels = 15

# ASIL-D 等级控制数据池（转向、制动）
[[services]]
name = "steering_control"
safety_level = "ASIL-D"

[services.publisher]
max_payload_size = 4096  # 4KB（控制指令）
max_publishers = 1  # 单一控制源
max_channels = 10
access_mode = "read_only_subscribers"  # 订阅者只读

[[services]]
name = "brake_control"
safety_level = "ASIL-D"

[services.publisher]
max_payload_size = 4096
max_publishers = 1
max_channels = 10
access_mode = "read_only_subscribers"

---

## 2. Binding Manager（运行时插件系统）

### 2.1 插件接口（ITransportBinding.hpp — NVI 强类型模式）

采用 **NVI（Non-Virtual Interface）** 模式：
- **公有 template 方法**：调用方看到 `SendEvent<T>()`，编译期类型安全
- **保护 virtual Do* 方法**：binding 插件仅需实现 `DoSendEvent(... const void* pData)`
- **不依赖 RTTI**：binding 通过 `(serviceId, instanceId, eventId)` 组合键查找预注册的 writer/reader

```cpp
namespace lap {
namespace com {
namespace binding {

// ---- 类型擦除回调（binding 实现侧使用） ----
using EventCallback = Function<void(UInt64 serviceId, UInt64 instanceId,
                                    UInt32 eventId, const void* pData)>;
using MethodHandler  = Function<void(UInt64 serviceId, UInt64 instanceId,
                                     UInt32 methodId, const void* pReq, void* pResp)>;
using FieldNotificationCallback = Function<void(UInt64 serviceId, UInt64 instanceId,
                                                UInt32 fieldId, const void* pValue)>;

// ---- 强类型回调（调用方使用） ----
template<typename T>
using TypedEventCallback = Function<void(UInt64, UInt64, UInt32, const T&)>;
template<typename TReq, typename TResp>
using TypedMethodHandler  = Function<TResp(UInt64, UInt64, UInt32, const TReq&)>;

class ITransportBinding {
public:
    virtual ~ITransportBinding() = default;

    // ---- 生命周期 ----
    virtual Result<void> Initialize() noexcept = 0;
    virtual void Configure(const std::map<std::string,std::string>& params) noexcept {}
    virtual Result<void> Shutdown() noexcept = 0;

    // ---- 服务管理 ----
    virtual Result<void> OfferService(UInt64 serviceId, UInt64 instanceId) noexcept = 0;
    virtual Result<void> StopOfferService(UInt64 serviceId, UInt64 instanceId) noexcept = 0;

    // ---- 服务发现 ----
    virtual Result<Vector<UInt64>> FindService(UInt64 serviceId) noexcept = 0;
    virtual Result<UInt64> StartFindService(UInt64 serviceId,
                                            ServiceDiscoveryCallback cb) noexcept = 0;
    virtual Result<void>   StopFindService(UInt64 handle) noexcept = 0;

    // ---- Event（NVI 模板 → Do*） ----
    template<typename T>
    Result<void> SendEvent(UInt64 sid, UInt64 iid, UInt32 eid, const T& data) noexcept {
        return DoSendEvent(sid, iid, eid, static_cast<const void*>(&data), sizeof(T));
    }
    template<typename T>
    Result<void> SubscribeEvent(UInt64 sid, UInt64 iid, UInt32 eid,
                                TypedEventCallback<T> cb) noexcept {
        auto erased = [cb=std::move(cb)](UInt64 s,UInt64 i,UInt32 e,const void* p){
            cb(s, i, e, *static_cast<const T*>(p));
        };
        return DoSubscribeEvent(sid, iid, eid, std::move(erased));
    }
    virtual Result<void> UnsubscribeEvent(UInt64, UInt64, UInt32) noexcept = 0;

    // ---- Method（NVI 模板 → Do*） ----
    template<typename TResp, typename TReq>
    Result<TResp> CallMethod(UInt64 sid, UInt64 iid, UInt32 mid, const TReq& req) noexcept {
        TResp resp{};
        auto r = DoCallMethod(sid, iid, mid, &req, &resp, sizeof(TReq), sizeof(TResp));
        if (!r) return Result<TResp>::FromError(std::move(r).Error());
        return Result<TResp>(std::move(resp));
    }
    template<typename TResp, typename TReq>
    Future<TResp> CallMethodAsync(UInt64 sid, UInt64 iid, UInt32 mid,
                                  const TReq& req) noexcept;  // wraps CallMethod in thread
    template<typename TReq, typename TResp>
    Result<void> RegisterMethod(UInt64 sid, UInt64 iid, UInt32 mid,
                                TypedMethodHandler<TReq,TResp> h) noexcept;

    // ---- Field（NVI 模板 → Do*） ----
    template<typename T> Result<T>    GetField(UInt64, UInt64, UInt32) noexcept;
    template<typename T> Result<void> SetField(UInt64, UInt64, UInt32, const T&) noexcept;
    template<typename T> Result<void> SubscribeFieldNotification(UInt64, UInt64, UInt32,
                                         TypedFieldCallback<T>) noexcept;
    virtual Result<void> UnsubscribeFieldNotification(UInt64, UInt64, UInt32) noexcept = 0;

    // ---- 诊断 ----
    virtual const char* GetName() const noexcept = 0;
    virtual UInt32 GetVersion() const noexcept = 0;
    virtual UInt32 GetPriority() const noexcept = 0;
    virtual Bool   SupportsZeroCopy() const noexcept = 0;
    virtual Bool   SupportsService(UInt64) const noexcept = 0;
    virtual TransportMetrics GetMetrics() const noexcept = 0;

protected:
    // ---- Binding 插件实现这些虚方法（接收 const void*，通过组合键查 writer） ----
    //      writer_map_.find({serviceId, instanceId, eventId})->Write(pData)
    //
    // 【序列化策略】Do* 实现 MUST:
    //   1. 通过 (serviceId, elementId) 在 TypeRegistry 中查找类型适配器
    //   2. 若找到适配器 → 使用适配器进行序列化/反序列化
    //   3. 若未找到适配器 → 使用 memcpy + dataSize 作为默认实现
    //
    virtual Result<void> DoSendEvent(UInt64, UInt64, UInt32, const void*,
                                     Size dataSize = 0) noexcept = 0;
    virtual Result<void> DoSubscribeEvent(UInt64, UInt64, UInt32, EventCallback,
                                          Size dataSize = 0) noexcept = 0;
    virtual Result<void> DoCallMethod(UInt64, UInt64, UInt32,
                                      const void* pReq, void* pResp,
                                      Size requestSize = 0,
                                      Size responseSize = 0) noexcept = 0;
    virtual Result<void> DoRegisterMethod(UInt64, UInt64, UInt32, MethodHandler,
                                          Size requestSize = 0,
                                          Size responseSize = 0) noexcept = 0;
    virtual Result<void> DoGetField(UInt64, UInt64, UInt32, void* pOut,
                                    Size valueSize = 0) noexcept = 0;
    virtual Result<void> DoSetField(UInt64, UInt64, UInt32, const void*,
                                    Size valueSize = 0) noexcept = 0;
    virtual Result<void> DoSubscribeFieldNotification(UInt64, UInt64, UInt32,
                                                      FieldNotificationCallback,
                                                      Size valueSize = 0) noexcept = 0;
};

// 插件工厂（每个 .so 导出此符号）
extern "C" {
    ITransportBinding* CreateBindingInstance();
    void DestroyBindingInstance(ITransportBinding* instance);
}

} // namespace binding
} // namespace com
} // namespace lap
```

**NVI 分层数据流**：
```
调用方                        template 层                         binding 实现
─────                        ──────────                         ─────────────
SendEvent<RadarData>(...)  →  static_cast<const void*>(&data)  →  DoSendEvent(sid, iid, eid, pData, sizeof(T))
                                                                   ├→ TypeRegistry 查找适配器，未找到则 memcpy(dataSize)
                                                                   └→ writer->Write(pData)  // CDR inside
```

### 2.2 BindingManager（动态加载逻辑）

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

### 2.3 应用代码示例（完全标准 AUTOSAR）

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
    
    // 3. 创建代理（自动选择最优 Binding: coreipc > dds > legacy）
    auto proxy = std::make_shared<CameraServiceProxy>(handles[0]);
    
    // 4. 订阅事件（零拷贝自动生效，应用无感知）
    proxy->ImageData.Subscribe([](const Image& img) {
        ProcessImage(img);  // img 可能是 Core IPC 零拷贝对象，应用不关心
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

### 2.4 Binding Manager 组件设计 (source/binding/manager/)

**AUTOSAR 对应**: Transport Binding Manager

**核心功能**: 运行时动态加载和管理 .so 插件

#### 2.4.1 组件设计

| 组件 | 功能 | 代码量 |
|------|------|--------|
| `BindingLoader` | dlopen() 动态加载插件 | ~250行 |
| `BindingSelector` | 优先级选择算法 | ~200行 |
| `ConfigParser` | YAML 配置解析（基于yaml-cpp，含 ARXML 转换） | ~300行 |
| `BindingRegistry` | 插件注册表管理 | ~150行 |

**总计**: ~900行

#### 2.4.2 ITransportBinding 插件接口（NVI 模式）

Binding 插件**只需实现 protected 虚方法**，公有 template API 由基类提供：

```cpp
class ITransportBinding {
public:
    // ---- 公有 template 方法（调用方使用，编译期类型安全） ----
    template<typename T>
    Result<void> SendEvent(UInt64 sid, UInt64 iid, UInt32 eid, const T& data);
    template<typename T>
    Result<void> SubscribeEvent(UInt64, UInt64, UInt32, TypedEventCallback<T>);
    template<typename TResp, typename TReq>
    Result<TResp> CallMethod(UInt64, UInt64, UInt32, const TReq&);
    template<typename TReq, typename TResp>
    Result<void> RegisterMethod(UInt64, UInt64, UInt32, TypedMethodHandler<TReq,TResp>);
    // ... GetField / SetField / SubscribeFieldNotification 同样为 template

protected:
    // ---- 插件实现这组虚方法（类型擦除，通过 ID 组合键查 writer） ----
    virtual Result<void> DoSendEvent(UInt64 sid, UInt64 iid, UInt32 eid,
                                     const void* pData, Size dataSize = 0) = 0;
    virtual Result<void> DoSubscribeEvent(UInt64, UInt64, UInt32, EventCallback,
                                          Size dataSize = 0) = 0;
    virtual Result<void> DoCallMethod(UInt64, UInt64, UInt32,
                                      const void* pReq, void* pResp,
                                      Size requestSize = 0,
                                      Size responseSize = 0) = 0;
    virtual Result<void> DoRegisterMethod(UInt64, UInt64, UInt32, MethodHandler,
                                          Size requestSize = 0,
                                          Size responseSize = 0) = 0;
    virtual Result<void> DoGetField(UInt64, UInt64, UInt32, void* pOut,
                                    Size valueSize = 0) = 0;
    virtual Result<void> DoSetField(UInt64, UInt64, UInt32, const void*,
                                    Size valueSize = 0) = 0;
    virtual Result<void> DoSubscribeFieldNotification(UInt64, UInt64, UInt32,
                                                      FieldNotificationCallback,
                                                      Size valueSize = 0) = 0;
};
```

**Writer 查找方式**：binding 在 `OfferService` / `RegisterMethod` 阶段建立 `writer_map_`，
Do* 实现内通过 `(serviceId, instanceId, elementId)` 组合键查找对应的 writer/reader。

#### 2.4.3 插件加载流程

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

---

## 3. Proxy/Skeleton ↔ Binding 数据路径 (Transport Wiring)

**新增文件**:

| 文件 | 用途 | 代码量 |
|------|------|--------|
| `CBindingContext.hpp` | 轻量上下文（binding指针 + serviceId/instanceId/elementId） | ~60行 |
| `CSerializationTraits.hpp` | ADL序列化分发（原语→ISerializer，用户类型→ADL自由函数） | ~140行 |

**数据路径绑定状态**:

| 组件 | 方法 | Binding API | 序列化 | 状态 |
|------|------|-------------|--------|------|
| SkeletonEvent | `DoSend()` | `SendEvent()` | CBinarySerializer → SerializeValue | ✅ 完成 |
| ProxyEvent | `Subscribe()` | `SubscribeEvent()` / `UnsubscribeEvent()` | CBinaryDeserializer → DeserializeValue | ✅ 完成 |
| ProxyMethod | `DoSyncCall()` | `CallMethod()` | Serialize args + Deserialize response | ✅ 完成 |
| ProxyMethod | `DoAsyncCall()` | `CallMethodAsync()` | Serialize args + Deserialize response | ✅ 完成 |
| ProxyFireAndForget | `DoCall()` | `CallMethod()` (fire-and-forget) | Serialize args only | ✅ 完成 |
| ProxyField | `DoGet()` | `GetField()` | Deserialize response | ✅ 完成 |
| ProxyField | `DoSet()` | `SetField()` | Serialize value | ✅ 完成 |
| SkeletonField | `Update()` | via SkeletonEvent `SendEvent()` | CBinarySerializer | ✅ 完成 |
| SkeletonMethod | `ProcessCall()` | via `RegisterMethod()` callback | 由调用方序列化 | ✅ 结构完成 |
| ProxyTrigger | `Subscribe()` | `SubscribeEvent()` (empty payload) | 无序列化（data-less） | ✅ 完成 |
| SkeletonTrigger | `Trigger()` | `SendEvent()` (empty payload) | 无序列化（data-less） | ✅ 完成 |

**Binding Context 传递流程**:
```
SkeletonBase::OfferService()
  └→ ServiceSkeleton<T>::doOfferService()
      ├→ Runtime::OfferService<T>()              // registry + binding->OfferService
      ├→ BindingManager::SelectBinding()          // 获取 ITransportBinding*
      ├→ setBindingContext(context)               // 存储到 m_bindingContext
      └→ onBindingContextReady(context)           // 虚钩子 → 派生类重写
          └→ SetBindingContext() on each SkeletonEvent/SkeletonField/SkeletonMethod/SkeletonTrigger

ProxyBase::Create(HandleType)
  └→ ServiceProxy<T>::Create()
      ├→ BindingManager::SelectBinding()          // 获取 ITransportBinding*
      ├→ setBindingContext(context)               // 存储到 m_bindingContext
      └→ onBindingContextReady(context)           // 虚钩子 → 派生类重写
          └→ SetBindingContext() on each ProxyEvent/ProxyMethod/ProxyField/ProxyTrigger
```

**发送流程**（以 SkeletonEvent 为例 — NVI 强类型路径）:
```
SkeletonEvent<RadarData>::Send(sample)
  └→ DoSend(sample)
      └→ ITransportBinding::SendEvent<RadarData>(sid, iid, eid, sample)
          └→ DoSendEvent(sid, iid, eid, static_cast<const void*>(&sample), sizeof(T))
              ├→ TypeRegistry 查找适配器，未找到则 memcpy(dataSize)
              └→ writer->Write(pData)                   // writer 内部 CDR 序列化
```

**接收流程**（以 ProxyEvent 为例 — NVI 强类型路径）:
```
Binding 接收层（DDS Reader / IPC Subscriber）
  ├→ reader->Read() → 反序列化为 typed object
  └→ EventCallback(sid, iid, eid, const void* pData)
      └→ NVI lambda: cb(sid, iid, eid, *static_cast<const RadarData*>(pData))
          └→ ProxyEvent<RadarData>::OnEventReceived(const RadarData& data)
              └→ PushSample() → queue + notify handler
```


---

## 4. Core IPC Binding (source/binding/coreipc/) 🔥 新增

### 4.1 设计定位

**核心目标**: 提供**真正零拷贝**的超高性能本地进程间通信（IPC）方案，基于LightAP Core模块的IPC实现

**Core IPC 优势**:
- ✅ **无需守护进程**: 每个进程自管理，消除单点故障
- ✅ **C++17实现**: 类型安全，现代C++特性
- ✅ **零配置启动**: 自动发现与连接
- ✅ **更好的集成**: 与LightAP架构无缝集成
- ✅ **内置FuSa支持**: AUTOSAR标准ErrorCode/Result模式
- ✅ **固定槽位注册**: 使用Com模块的ServiceRegistry实现O(1)服务发现

**适用场景**:
- 🚀 **极致性能**: 传感器融合、感知算法、大规模数据流处理
- ⚡ **超低延迟**: < 5μs 端到端延迟，实时控制系统
- 🔒 **零拷贝**: 共享内存直接访问，消除内存复制
- 📊 **大吞吐量**: > 10 GB/s 单连接，支持4K视频、LiDAR点云

**性能指标**:
- 延迟: **< 5μs** (P99)
- 吞吐量: **> 10 GB/s**
- CPU占用: **< 0.5%**
- 内存占用: 固定MemPool，2MB chunk对齐
- 确定性: Lock-free算法 + RAII管理

### 4.2 Core IPC 核心特性

**1. 真零拷贝架构（无需守护进程）**
```cpp
// Core IPC: 进程自动初始化，基于lap::core::ipc API
// 发布端：直接在共享内存中构造数据
using namespace lap::core::ipc;

PublisherConfig config;
config.max_chunks = 64;
config.chunk_size = 2 * 1024 * 1024;  // 2MB

auto pub_result = Publisher::Create("/lap_ipc_1234_5678", config);
auto publisher = std::move(pub_result).Value();

// 使用lambda写入数据（零拷贝）
publisher.Send([&](Byte* buffer, Size size) -> Size {
    auto* data = reinterpret_cast<SensorData*>(buffer);
    data->timestamp = getCurrentTime();
    data->points.resize(10000);
    for (size_t i = 0; i < 10000; ++i) {
        data->points[i] = lidar.readPoint(i);
    }
    return sizeof(SensorData);
});
```

```cpp
// 订阅端：直接访问共享内存数据
SubscriberConfig sub_config;
sub_config.channel_capacity = 32;

auto sub_result = Subscriber::Create("/lap_ipc_1234_5678", sub_config);
auto subscriber = std::move(sub_result).Value();

// 接收数据（零拷贝）
auto sample_result = subscriber.Receive();
if (sample_result) {
    auto sample = std::move(sample_result).Value();
    auto* data = reinterpret_cast<const SensorData*>(sample.RawData());
    processLidarData(data->points);
    // Sample析构时自动释放回MemPool
}
```

**2. 内存池（MemPool）管理**
```cpp
// Core IPC: 每个Publisher/Subscriber管理自己的MemPool
PublisherConfig config;
config.max_chunks = 64;              // 最大chunk数量
config.chunk_size = 2 * 1024 * 1024; // 2MB per chunk
config.publish_timeout = 100000000;   // 100ms
config.policy = PublishPolicy::kOverwrite;

// 自动创建共享内存和chunk pool
auto publisher = Publisher::Create(shm_path, config).Value();
```

**3. Lock-free Queue**
- **无锁算法**: RingBufferBlock实现，支持SPSC/MPMC模式
- **实时保证**: 无优先级反转，无死锁
- **确定性延迟**: 最坏情况可预测
- **原子引用计数**: 安全的多订阅者消息共享

**4. 服务发现（基于Com ServiceRegistry）**
```cpp
// CoreIPCBinding使用Com的固定槽位注册表
// Publisher注册服务
String shm_path = "/lap_ipc_" + std::to_string(service_id) + 
                  "_" + std::to_string(instance_id);

// 1. 计算固定槽位
uint32_t slot_index = service_id & 0x03FF;  // Slots 1-1023

// 2. 注册到ServiceRegistry
registry_->RegisterService(
    slot_index,
    service_id,
    instance_id,
    major_version,
    minor_version,
    "coreipc",
    shm_path.c_str()  // endpoint = Core IPC共享内存路径
);

// Consumer查找服务
auto result = registry_->FindService(service_id);
if (result) {
    auto& slot = result.Value();
    String shm_path = slot.endpoint;  // 获取共享内存路径
    // 使用shm_path创建Subscriber
    auto subscriber = Subscriber::Create(shm_path, config);
}
```

### 4.3 架构组成

| 组件 | 功能 | 文件 |
|------|------|------|
| `CoreIPCBinding` | Core IPC传输绑定主类 | `CoreIPCBinding.hpp` |
| `PublisherWrapper` | Publisher封装 + Com注册表服务管理 | `CoreIPCBinding.cpp` |
| `SubscriberWrapper` | Subscriber封装 + 事件监听 | `CoreIPCBinding.cpp` |
| `Com::ServiceRegistry` | 固定槽位服务注册表（QM/ASIL双注册表） | `registry/ServiceRegistry.hpp` |
| `EventProtocol` | Event ID协议封装（4字节头） | `CoreIPCBinding.cpp` |

### 4.4 核心技术

**1. POSIX Shared Memory**
```cpp
// 共享内存创建（由Publisher自动管理）
// Core IPC使用memfd创建匿名共享内存
String shm_path = "/lap_ipc_" + std::to_string(service_id) + 
                  "_" + std::to_string(instance_id);
auto publisher = Publisher::Create(shm_path, config);
```

**2. MemPool 分配策略**
```cpp
// Core IPC ChunkPoolAllocator
// 从内存池借用chunk (loan-based API)
auto sample_result = publisher.Loan();
if (sample_result) {
    auto sample = std::move(sample_result).Value();
    // 写入数据...
    publisher.Send(std::move(sample));
}

// 或使用lambda API（推荐）
publisher.Send([](Byte* buf, Size size) -> Size {
    // 直接写入共享内存
    std::memcpy(buf, data, data_size);
    return data_size;
});
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

**4. Core IPC 进程自管理架构**
```
┌─────────────────────────────────────────────────────────────────┐
│   Com ServiceRegistry (固定槽位，QM/ASIL双注册表)          │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ ServiceSlot[1024] (256 bytes/slot, 256KB total)         │   │
│  │ - service_id → slot mapping (O(1) lookup)               │   │
│  │ - instance_id, binding_type, endpoint (shm path)        │   │
│  │ - seqlock无锁并发控制 (<100ns read)                      │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
         ↓ endpoint指向Core IPC共享内存路径
┌─────────────────────────────────────────────────────────────────┐
│      Core IPC 共享内存区域 (per-service, POSIX shm)             │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ ControlBlock (128KB)                                     │   │
│  │ - pool_state: 块分配状态                                 │   │
│  │ - registry: ChannelRegistry (128个订阅者位图)         │   │
│  └─────────────────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ SubscriberQueues (800KB, 100 × 8KB)                     │   │
│  │ - RingBufferBlock: lock-free环形队列                     │   │
│  └─────────────────────────────────────────────────────────┘   │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ ChunkPool (动态大小)                                      │   │
│  │ - 2MB × 64 chunks = 128MB                                │   │
│  │ - ChunkPoolAllocator: SHRINK/NORMAL/EXTEND策略           │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
         ↑                                       ↑
         │ Publisher::Create(shm_path)           │ Subscriber::Create(shm_path)
         │                                       │
┌────────┴─────────────┐              ┌─────────┴────────────────┐
│  Publisher App       │              │ Subscriber App           │
│  1. OfferService()   │              │  1. FindService()        │
│     → 注册到Registry  │              │     → 查询Registry        │
│     → 写入shm_path   │              │     → 获取shm_path       │
│  2. Publisher::      │              │  2. Subscriber::         │
│     Create(path)     │              │     Create(path)         │
│     → 创建/打开SHM    │              │     → 打开已存在SHM       │
└──────────────────────┘              └──────────────────────────┘
```

### 4.5 配置示例

Core IPC Binding使用Com模块的统一配置文件格式：

#### 4.5.1 运行时配置 (runtime_config.yaml)

```yaml
services:
  - service_id: 0x1234
    instance_id: 0x5678
    interfaces:
      events:
        - event_id: 0x0001
          transport_binding:
            type: coreipc
            priority: 100
            config:
              max_chunks: 64
              chunk_size: 2097152  # 2MB
              channel_capacity: 32
              timeout_ms: 100
```

#### 4.5.2 使用建议

**推荐使用场景**:
- ✅ **本地高性能通信**: 同设备进程间大数据量传输
- ✅ **实时系统**: 传感器数据融合、感知算法管道
- ✅ **零拷贝需求**: 4K视频流、LiDAR点云处理

**性能优化建议**:
- 调整 `chunk_size` 以匹配数据负载大小
- 使用 `max_chunks` 控制内存使用
- 考虑 `PublishPolicy::kOverwrite` 减少阻塞

#### 4.5.3 后续增强计划

根据 [CORE_IPC_INTERFACE_REQUIREMENTS.md](../../../CORE_IPC_INTERFACE_REQUIREMENTS.md)，计划增强：

| 功能 | 优先级 | 说明 | 状态 |
|------|-------|------|------|
| ServiceRegistry集成 | **已完成** | 使用Com模块的固定槽位注册表 | ✅ 架构已定义 |
| MethodChannel | **已完成** | 支持Method调用（请求-响应模式） | ✅ CCoreIPCMethodManager 544行 |
| BindingContext传播 | **已完成** | SkeletonBase/ProxyBase → Sub-component CBindingContext | ✅ onBindingContextReady 虚钩子 |
| Trigger绑定 | **已完成** | ProxyTrigger/SkeletonTrigger → ITransportBinding | ✅ 数据-less Event收发 |
| FastDdsDiscoveryClient | **已完成** | 真实DDS Participant发现客户端 | ✅ USER_DATA编解码 + DiscoveryProtocol::CLIENT |
| DDS Push Discovery | **已完成** | StartFindService/StopFindService 实时推送 | ✅ DdsDiscoveryListener → DdsBinding 回调链 |
| ITransportBinding::Configure | **已完成** | 绑定加载后、初始化前传入键值参数 | ✅ BindingManager自动调用 |
| SOME/IP序列化器 | **已完成** | TPS_SOME/IP wire format (BOM+UTF-8+NUL) | ✅ CSomeIpSerializer/Deserializer |
| DDS CDR序列化器 | **已完成** | OMG CDR via eProsima FastCDR 2.2 | ✅ CCdrSerializer/Deserializer |
| JSON序列化器 | **已完成** | nlohmann/json 3.11 后端 | ✅ CJsonSerializer/Deserializer |
| Async生命周期管理 | **已完成** | CallMethodAsync detached线程安全Shutdown | ✅ atomic counter + condition_variable drain |
| EventID Protocol | P1 | 标准化Event ID编码格式 | 📋 待实现 |
| FuSa MemPool | P2 | 安全等级隔离（QM/ASIL-D） | 📋 待实现 |

**服务注册流程**:
1. CoreIPCBinding调用`OfferService()`时：
   - 创建Core IPC Publisher（生成shm_path）
   - 计算固定槽位：`slot = service_id & 0x03FF`
   - 注册到ServiceRegistry，endpoint字段存储shm_path
   
2. CoreIPCBinding调用`FindService()`时：
   - 查询ServiceRegistry获取ServiceSlot
   - 从slot.endpoint读取shm_path
   - 使用shm_path创建Core IPC Subscriber

---


### 4.6 接口示例

**CoreIPCPublisher示例**
```cpp
// Core IPC Publisher使用示例
using namespace lap::core::ipc;

PublisherConfig config;
config.max_chunks = 64;
config.chunk_size = 2 * 1024 * 1024;

auto pub_result = Publisher::Create("/lap_ipc_1234_5678", config);
auto publisher = std::move(pub_result).Value();

// 使用lambda发布数据（零拷贝）
publisher.Send([&](Byte* buffer, Size size) -> Size {
    auto* data = reinterpret_cast<SensorData*>(buffer);
    data->timestamp = getCurrentTime();
    return sizeof(SensorData);
});
```

**CoreIPCSubscriber示例**
```cpp
// Core IPC Subscriber使用示例
using namespace lap::core::ipc;

SubscriberConfig config;
config.channel_capacity = 32;

auto sub_result = Subscriber::Create("/lap_ipc_1234_5678", config);
auto subscriber = std::move(sub_result).Value();

// 接收数据（零拷贝）
auto sample_result = subscriber.Receive();
if (sample_result) {
    auto sample = std::move(sample_result).Value();
    auto* data = reinterpret_cast<const SensorData*>(sample.RawData());
    processData(data);
    // Sample 析构时自动释放回 MemPool
}
```

### 4.7 AUTOSAR 集成（NVI Do* 实现）

`CoreIPCBinding` 实现 `ITransportBinding` 的 NVI 保护虚方法，内部使用 Core IPC Publisher/Subscriber 完成零拷贝传输：

```cpp
// CoreIPCBinding — 实现 ITransportBinding 保护虚方法
class CoreIPCBinding final : public ITransportBinding
{
protected:
    // ── Event ──
    Result<void> DoSendEvent(UInt64 serviceId, UInt64 instanceId,
                             UInt32 eventId, const void* pData,
                             Size dataSize) noexcept override
    {
        // 组合键查找对应 Publisher
        auto it = publisher_map_.find({serviceId, instanceId, eventId});
        if (it == publisher_map_.end())
            return Result<void>::FromError(ErrorCode::kNotOffered);

        // 零拷贝写入共享内存
        return it->second->Send([&](Byte* buffer, Size size) -> Size {
            std::memcpy(buffer, pData, dataSize);
            return dataSize;
        });
    }

    Result<void> DoSubscribeEvent(UInt64 serviceId, UInt64 instanceId,
                                  UInt32 eventId,
                                  EventCallback callback) noexcept override
    {
        // 创建 Subscriber，启动接收线程
        auto key = MakeKey(serviceId, instanceId, eventId);
        auto sub = Subscriber::Create(MakeShmPath(key));
        if (!sub.HasValue())
            return Result<void>::FromError(ErrorCode::kResourceError);

        // 接收线程：零拷贝读取 → 回调上层
        StartReceiveThread(key, std::move(sub).Value(),
            [=](const Sample& s) {
                callback(serviceId, instanceId, eventId, s.RawData());
            });
        return {};
    }

    // ── Method ──
    Result<void> DoCallMethod(UInt64 sid, UInt64 iid, UInt32 mid,
                              const void* pReq, void* pResp,
                              Size requestSize,
                              Size responseSize) noexcept override
    {
        // MethodChannel 同步 Request-Reply
        auto it = method_channel_map_.find({sid, iid, mid});
        if (it == method_channel_map_.end())
            return Result<void>::FromError(ErrorCode::kNotFound);
        return it->second->Call(pReq, requestSize, pResp, responseSize);
    }

private:
    // (serviceId, instanceId, elementId) → Core IPC Publisher
    Map<CompositeKey, std::unique_ptr<Publisher>>  publisher_map_;
    Map<CompositeKey, std::unique_ptr<Subscriber>> subscriber_map_;
    Map<CompositeKey, std::unique_ptr<MethodChannel>> method_channel_map_;
};
```

**数据流对比**：

| 路径 | 旧 ByteBuffer 方式 | NVI Do* 方式 |
|------|--------------------|--------------|
| **发送** | `serialize → ByteBuffer → Publisher.Send()` | `SendEvent<T>() → DoSendEvent(void*) → Publisher.Send()` |
| **接收** | `Subscriber.Receive() → ByteBuffer → deserialize` | `Subscriber.Receive() → EventCallback(void*) → static_cast<T*>` |
| **拷贝次数** | 2 (序列化+传输) | 1 (仅 memcpy 到 SHM) |

### 4.8 性能基准

| 指标 | D-Bus | SOME/IP | DDS | **Core IPC** |
|------|-------|---------|-----|---------------|
| 延迟 (64B) | 50-100μs | 20-50μs | 10-30μs | **< 5μs** |
| 延迟 (1MB) | ~20ms | ~5ms | <100μs | **< 20μs** |
| 吞吐量 (MB/s) | 50-100 | 200-300 | 500-800 | **> 10,000** |
| CPU 占用 | 3-5% | 2-4% | 4-6% | **< 0.5%** |
| 内存拷贝 | 3次 | 2次 | 1次 | **0次** |
| 零拷贝 | ❌ | 部分 | ✅ | **✅✅** |
| 实时性 | ❌ | 部分 | 部分 | **✅✅** |
| 跨网络 | ❌ | ✅ | ✅ | **❌** |

### 4.9 使用场景

| 场景 | 数据量 | 频率 | 推荐传输 | 延迟要求 |
|------|--------|------|----------|----------|
| **摄像头图像** (4K) | 8MB/frame | 30fps | **Core IPC** | < 5ms |
| **LiDAR点云** | 2MB/scan | 10Hz | **Core IPC** | < 1ms |
| **传感器融合结果** | 100KB | 100Hz | **Core IPC** | < 100μs |
| **实时控制指令** | 64B | 1kHz | **Core IPC** | < 10μs |
| **地图更新** | 50MB | 1Hz | DDS | < 1s |
| **跨ECU通信** | 任意 | 任意 | DDS/SOME/IP | < 100ms |

### 4.10 配置示例

**runtime_config.yaml 配置**
```yaml
services:
  - service_id: 0x1234
    instance_id: 0x5678
    interfaces:
      events:
        - event_id: 0x0001
          transport_binding:
            type: coreipc
            priority: 100
            config:
              max_chunks: 64
              chunk_size: 8388608  # 8MB
              channel_capacity: 32
              timeout_ms: 100
```

**lap::com配置映射**
```cpp
// Service Manifest配置
{
    "service": "CameraService",
    "instance": "FrontCamera",
    "binding": "coreipc",
    "config": {
        "max_chunks": 64,           // 最大chunk数量
        "chunk_size": 8388608,      // 8MB chunk
        "channel_capacity": 32,       // 订阅端队列深度
        "timeout_ms": 100           // 发布超时
    }
}
```

### 4.11 实施现状

**已完成**:
- ✅ Core IPC基础集成（基于lap::core::ipc）
- ✅ Event通信绑定（Publisher/Subscriber）
- ✅ 服务注册与发现（临时内存实现）
- ✅ CMake构建系统集成
- ✅ 编译验证（liblap_com_binding_coreipc.so）

**后续计划**:
- 📋 迁移服务注册到Core模块
- 📋 实现Method通信（请求-响应）
- 📋 实现Field通信（Getter/Setter）
- 📋 性能优化与测试

> **💡 设计归档**: Protobuf + Domain Socket Binding 已移除，详见 [`archive/PROTOBUF_SOCKET_BINDING_ARCHIVED.md`](../archive/PROTOBUF_SOCKET_BINDING_ARCHIVED.md)  
> **替代方案**: 使用 **Core IPC Binding** (§4) 提供更优异的本地IPC性能（<5μs延迟，>10GB/s吞吐量）

---

## 5. DDS Transport Binding (source/binding/dds/)

**AUTOSAR 对应**: DDS Network Binding + DDS Security (AUTOSAR AP TR)  
**底层库**: eProsima Fast-DDS 3.x + FastCDR 2.2  
**架构**: 强类型 Facade — 三层 Manager 分治，per-service TypeAdapter 运行时注入

### 5.1 强类型 DDS 接口

DDS Binding 实现了 `ITransportBinding` 接口，通过 **Facade + 3 Manager** 模式组织：

```
DdsBinding (Facade, implements ITransportBinding)
├── CDdsServiceManager   — OfferService / StopOfferService / FindService
├── CDdsEventManager     — SendEvent / SubscribeEvent / UnsubscribeEvent
└── CDdsMethodManager    — CallMethod / RegisterMethod / GetField / SetField
```

**强类型原则**：应用层数据类型通过 `IDdsTypeAdapter` 桥接到 DDS 原生 `TopicDataType`，
每个 service×element 组合拥有独立的 **Topic + Writer/Reader**，FastDDS 自动处理 CDR 序列化。

```cpp
// DdsBinding — 强类型 Facade（实现 ITransportBinding 的 Do* 虚方法）
class DdsBinding final : public ITransportBinding
{
public:
    // --- 生命周期 ---
    Result<void> Initialize() noexcept override;
    void Configure(const std::map<std::string,std::string>& params) noexcept override;
    Result<void> Shutdown() noexcept override;

    // --- 服务管理 ---
    Result<void> OfferService(UInt64 serviceId, UInt64 instanceId) noexcept override;
    Result<void> StopOfferService(UInt64 serviceId, UInt64 instanceId) noexcept override;
    Result<Vector<UInt64>> FindService(UInt64 serviceId) noexcept override;

    // --- Push-Based Discovery ---
    Result<UInt64> StartFindService(
        UInt64 serviceId, ServiceDiscoveryCallback callback) noexcept override;
    Result<void> StopFindService(UInt64 handle) noexcept override;

    // --- 非虚方法（继承自 ITransportBinding） ---
    Result<void> UnsubscribeEvent(UInt64, UInt64, UInt32) noexcept override;
    Result<void> UnsubscribeFieldNotification(UInt64, UInt64, UInt32) noexcept override;

    // --- 度量 ---
    const char* GetName() const noexcept override { return "DDS"; }
    UInt32 GetVersion() const noexcept override { return 0x00020000; }
    UInt32 GetPriority() const noexcept override { return 80; }
    Bool SupportsZeroCopy() const noexcept override { return false; }
    Bool SupportsService(UInt64) const noexcept override { return true; } // cross-ECU
    TransportMetrics GetMetrics() const noexcept override;

protected:
    // --- NVI Do* 实现（通过组合键查找 writer/reader） ---
    Result<void> DoSendEvent(UInt64 sid, UInt64 iid, UInt32 eid,
                             const void* pData,
                             Size dataSize = 0) noexcept override;
    Result<void> DoSubscribeEvent(UInt64 sid, UInt64 iid, UInt32 eid,
                                  EventCallback callback,
                                  Size dataSize = 0) noexcept override;
    Result<void> DoCallMethod(UInt64 sid, UInt64 iid, UInt32 mid,
                              const void* pReq, void* pResp,
                              Size requestSize = 0,
                              Size responseSize = 0) noexcept override;
    Result<void> DoRegisterMethod(UInt64 sid, UInt64 iid, UInt32 mid,
                                  MethodHandler handler,
                                  Size requestSize = 0,
                                  Size responseSize = 0) noexcept override;
    Result<void> DoGetField(UInt64 sid, UInt64 iid, UInt32 fid,
                            void* pOut,
                            Size valueSize = 0) noexcept override;
    Result<void> DoSetField(UInt64 sid, UInt64 iid, UInt32 fid,
                            const void* pValue,
                            Size valueSize = 0) noexcept override;
    Result<void> DoSubscribeFieldNotification(UInt64 sid, UInt64 iid, UInt32 fid,
                                              FieldNotificationCallback cb,
                                              Size valueSize = 0) noexcept override;

private:
    CDdsServiceManager  m_serviceManager;
    CDdsEventManager    m_eventManager;
    CDdsMethodManager   m_methodManager;
};
```

**DoSendEvent 实现示意**：
```cpp
Result<void> DdsBinding::DoSendEvent(UInt64 sid, UInt64 iid, UInt32 eid,
                                     const void* pData,
                                     Size dataSize) noexcept
{
    // 1. 组合键查找 writer
    auto key = CDdsCodec::MakeEventKey(sid, iid, eid);
    auto it  = m_mapWriters.find(key);
    if (it == m_mapWriters.end()) {
        // 懒创建: Topic + Writer
        auto* adapter = CDdsTypeRegistry::Instance().FindAdapter(sid, eid);
        auto* topic   = CDdsCodec::GetOrCreateEventTopic(m_pParticipant, ...);
        it = m_mapWriters.emplace(key, CDdsCodec::CreateWriter(m_pPublisher, topic, m_config)).first;
    }
    // 2. writer 内部集成 CDR 序列化
    it->second->write(const_cast<void*>(pData));   // FastDDS 自动 CDR
    return {};
}
```

**共享 DDS 资源**（Facade 持有，Manager 引用）：

| 资源 | 类型 | 说明 |
|------|------|------|
| `m_pParticipant` | `DomainParticipant*` | 全局唯一 Participant |
| `m_pPublisher` | `Publisher*` | 默认 Publisher |
| `m_pSubscriber` | `Subscriber*` | 默认 Subscriber |
| `m_typeSupport` | `TypeSupport` | 默认 `DdsPayloadPubSubType`（后备） |
| `m_mapTopics` | `Map<String, Topic*>` | Topic 注册表 |
| `m_mapWriters` | `Map<String, DataWriter*>` | Writer 注册表 |
| `m_mapReaders` | `Map<String, DataReader*>` | Reader 注册表 |
| `m_mapListeners` | `Map<String, UniqueHandle<DdsReaderListener>>` | 事件监听器 |
| `m_pDiscoveryListener` | `DdsDiscoveryListener*` | Writer 发现监听 |

### 5.2 Type Registry — 强类型注入

`CDdsTypeRegistry` 是进程级单例，在启动阶段由生成代码注册 per-service 的 `IDdsTypeAdapter`。

```cpp
// IDdsTypeAdapter — 每个 service×element 的类型桥接接口
// NVI 路径下 DoSendEvent 等方法接收 const void* 指向应用层强类型对象，
// Adapter 负责桥接到 DDS 生成类型（若应用类型 ≠ DDS wire type）。
class IDdsTypeAdapter
{
public:
    virtual ~IDdsTypeAdapter() = default;

    // 返回 FastDDS TypeSupport（由 fastddsgen 生成）
    virtual eprosima::fastdds::dds::TypeSupport GetTypeSupport() const noexcept = 0;

    // ── NVI void* ↔ DDS 强类型 sample 桥接 ──

    // pData: 来自 DoSendEvent/DoSetField 的 const void*（指向应用层对象）
    // 返回: 堆分配的 DDS sample（调用者通过 FreeSample 释放）
    virtual void* CreateSample(const void* pData, Size dataSize) const = 0;

    // sample: DataReader::take() 返回的 DDS 类型对象
    // 返回: 指向 sample 内部应用数据的 const void*（生命周期跟随 sample）
    virtual const void* ExtractData(const void* sample) const noexcept = 0;

    // 释放 CreateSample 创建的 DDS sample
    virtual void FreeSample(void* sample) const noexcept = 0;

    // RPC 关联 ID（Method/Field 使用，Event 可忽略）
    virtual void   SetRequestId(void* sample, UInt64 requestId) const noexcept {}
    virtual UInt64 GetRequestId(const void* sample) const noexcept { return 0; }
};
```

```cpp
// CDdsTypeRegistry — 进程级单例，O(1) 查找
class CDdsTypeRegistry
{
public:
    static CDdsTypeRegistry& Instance() noexcept;

    void RegisterAdapter(UInt64 serviceId, UInt32 elementId,
                         const IDdsTypeAdapter* pAdapter) noexcept;
    void UnregisterAdapter(UInt64 serviceId, UInt32 elementId) noexcept;

    const IDdsTypeAdapter* FindAdapter(
        UInt64 serviceId, UInt32 elementId) const noexcept;
    bool HasAdapter(UInt64 serviceId, UInt32 elementId) const noexcept;

private:
    // key: "{serviceId_hex}_{elementId_hex}"
    mutable Mutex m_mutex;
    std::unordered_map<String, const IDdsTypeAdapter*> m_mapAdapters;
};
```

**注册时机与流程**：

```
应用启动
  └→ 生成代码静态初始化 / main() 初始化
      └→ CDdsTypeRegistry::Instance().RegisterAdapter(serviceId, eventId, &adapter)
          └→ 后续 SendEvent/SubscribeEvent 自动查找并使用 adapter
```

**双层类型策略**：

| 场景 | TypeSupport | Topic 数据类型 | CDR 序列化 |
|------|-------------|---------------|------------|
| **有 Adapter** | `adapter->GetTypeSupport()` | 生成的强类型 (e.g. `VehicleSpeedPubSubType`) | FastDDS 自动 CDR |
| **无 Adapter（后备）** | `DdsPayloadPubSubType` | `DdsPayload { requestId, data[] }` | 手写 CDR 特化 |

### 5.3 Topic + Writer/Reader 自动管理

每个 `(serviceId, instanceId, eventId)` 组合拥有独立的 Topic + Writer/Reader，**延迟创建**：

```cpp
// CDdsCodec — 静态工具类
class CDdsCodec
{
public:
    // Topic 命名: "lap/com/{serviceId}/{instanceId}/{eventId}"
    static String MakeEventTopicName(UInt64 serviceId, UInt64 instanceId, UInt32 eventId);

    // Method Topic: "LapComMethod_{serviceId}_{instanceId}_{methodId}_{req|rep}"
    static String MakeMethodTopicName(UInt64 serviceId, UInt64 instanceId,
                                      UInt32 methodId, Bool bIsRequest);

    // Key 生成: "{serviceId_hex}_{instanceId_hex}_{eventId_hex}"
    static String MakeEventKey(UInt64 serviceId, UInt64 instanceId, UInt32 eventId);
    static String MakeMethodKey(UInt64 serviceId, UInt64 instanceId, UInt32 methodId);

    // DDS Entity 懒创建
    static Topic*      GetOrCreateEventTopic(DomainParticipant* participant,
                                             TypeSupport& typeSupport,
                                             /* ids... */,
                                             Map<String, Topic*>& mapTopics);
    static DataWriter* CreateWriter(Publisher* publisher, Topic* topic, const DdsConfig& config);
    static DataReader* CreateReader(Subscriber* subscriber, Topic* topic,
                                    const DdsConfig& config,
                                    DataReaderListener* listener = nullptr);
};
```

**Event 发布数据流**（NVI 强类型路径）：

```
SkeletonEvent<VehicleSpeed>::Send(sample)
  └→ ITransportBinding::SendEvent<VehicleSpeed>(sid, iid, eid, sample)
      └→ DdsBinding::DoSendEvent(sid, iid, eid, const void* pData, Size dataSize)
          ├→ CDdsTypeRegistry::FindAdapter(sid, eid)
          ├→ [有 Adapter] adapter->CreateSample(pData, dataSize) → writer->write()
          └→ [无 Adapter] memcpy(pData, dataSize) → DdsPayload 后备路径
```

**Event 订阅数据流**（NVI 强类型路径）：

```
DdsReaderListener::on_data_available(reader)
  ├→ reader->take_next_sample(&msg, &info)
  ├→ [有 Adapter] adapter->ExtractData(sample) → typed object ptr
  └→ EventCallback(sid, iid, eid, const void* pData)
      └→ NVI lambda: cb(sid, iid, eid, *static_cast<const VehicleSpeed*>(pData))
          └→ ProxyEvent<VehicleSpeed>::OnEventReceived(data)
```

### 5.4 组件清单

| 文件 | 组件 | 功能 | 代码量 |
|------|------|------|--------|
| `DdsBinding.hpp/cpp` | `DdsBinding` | Facade，生命周期 + 发现 + 异步管理 | ~742行 |
| `CDdsPayload.hpp` | `DdsPayload` + `DdsPayloadPubSubType` | 后备 wire type + FastCDR 序列化 | ~376行 |
| `IDdsTypeAdapter.hpp` | `IDdsTypeAdapter` | Per-service 类型桥接接口 | ~50行 |
| `CDdsTypeRegistry.hpp` | `CDdsTypeRegistry` | 单例 Adapter 注册表 | ~80行 |
| `CDdsEventManager.hpp` | `CDdsEventManager` | Event 发布/订阅 | ~200行 |
| `CDdsMethodManager.hpp` | `CDdsMethodManager` | Method/Field RPC | ~350行 |
| `CDdsServiceManager.hpp` | `CDdsServiceManager` | 服务生命周期 | ~150行 |
| `CDdsCodec.hpp` | `CDdsCodec` | Topic 命名 + Entity 工厂（static） | ~200行 |
| `DdsTypes.hpp` | `DdsConfig` | Binding 配置结构体 | ~60行 |
| `DdsReaderListener.hpp/cpp` | `DdsReaderListener` | DataReader 事件回调 | ~80行 |
| `DdsDiscoveryListener.hpp/cpp` | `DdsDiscoveryListener` | Writer 发现 + Push 通知 | ~150行 |

**总计**: ~2,400行（实际已实现）

### 5.5 DDS 配置

```cpp
struct DdsConfig
{
    UInt32  m_iDomainId              = 0;
    String  m_strDiscoveryServer;              // e.g. "tcp://192.168.1.1:42100"
    Bool    m_bUseSharedMemory       = true;
    // AF_XDP: 优化升级项，独立处理（当前未实现）
    // Bool    m_bAfXdpEnabled       = false;
    // String  m_strAfXdpInterface   = "eth0";
    // Vector<UInt32> m_vecAfXdpQueues = {0, 1};
    UInt32  m_iLargePayloadThreshold = 65536;   // >64KB → SHM (未来: AF_XDP)
    UInt32  m_iMaxPayloadSize        = 10485760; // 10MB
    Bool    m_bReliable              = true;
    Bool    m_bTransientLocal        = false;
    UInt32  m_iHistoryDepth          = 10;
};
```

**AUTOSAR → DDS QoS 映射**：

| AUTOSAR 概念 | DDS QoS 策略 | 说明 |
|-------------|-------------|------|
| 可靠性 | RELIABILITY | `m_bReliable` → RELIABLE / BEST_EFFORT |
| 持久化 | DURABILITY | `m_bTransientLocal` → VOLATILE / TRANSIENT_LOCAL |
| 历史记录 | HISTORY | `m_iHistoryDepth` → KEEP_LAST(N) |
| 生命周期 | LIFESPAN | 数据有效期 |
| 优先级 | TRANSPORT_PRIORITY | 传输优先级 |
| 截止时间 | DEADLINE | 数据更新周期 |

### 5.6 Push-Based 服务发现

```
DomainParticipant
  └→ DdsDiscoveryListener::on_data_writer_discovery()
      ├→ 解析 topic name "lap/com/{serviceId}/{instanceId}/{eventId}"
      ├→ 更新 m_mapDiscoveredServices[serviceId] += instanceId
      └→ 触发 m_changeCallback(serviceId, instances)
          └→ DdsBinding::OnDiscoveryChange()
              └→ 遍历 m_mapFindSubscriptions，匹配 serviceId 则回调
```

支持两种发现模式：
- **Simple EDP**: 标准 RTPS 多播发现（局域网）
- **Discovery Server**: 配置 `m_strDiscoveryServer` 使用集中式发现服务器（跨子网）

### 5.7 DDS Security 集成

基于 AUTOSAR TR_DDSS 规范：

| 安全组件 | TR 需求 | 功能 |
|---------|---------|------|
| Identity CA | TR_DDSS_00002 | 身份证书颁发机构 |
| Identity Certificate | TR_DDSS_00003 | 参与者身份证书 |
| Private Key | TR_DDSS_00004 | 私钥管理 |
| Permissions CA | TR_DDSS_00005 | 权限证书颁发机构 |
| Governance Document | TR_DDSS_00006 | 域治理策略（加密、签名） |
| Permissions Document | TR_DDSS_00007 | 访问权限控制 |

**证书部署结构**：

```
artifacts/dds_security/
├── identity_ca.pem          # 身份 CA 证书
├── permissions_ca.pem       # 权限 CA 证书
├── instance_cert.pem        # 服务实例证书
├── instance_key.pem         # 服务实例私钥
├── governance.xml           # 治理文档（域规则）
└── permissions.xml          # 权限文档（访问控制）
```

### 5.8 架构关系图

```
ITransportBinding (abstract)
       ▲
       │ implements
DdsBinding (Facade, ~742 lines)
       │
       ├── owns → DomainParticipant, Publisher, Subscriber
       ├── owns → DdsDiscoveryListener → on_data_writer_discovery
       │                                 → fires DiscoveryChangeCallback
       ├── owns → Map<Topic*>, Map<DataWriter*>, Map<DataReader*>
       │
       ├── CDdsServiceManager (refs shared maps)
       │   └── OfferService → 创建 presence DataWriter
       │
       ├── CDdsEventManager (refs shared maps)
       │   ├── SendEvent → CDdsTypeRegistry::FindAdapter()
       │   │   ├── [Adapter] → 强类型 DataWriter::write()
       │   │   └── [Fallback] → DdsPayload DataWriter::write()
       │   └── SubscribeEvent → 创建 DdsReaderListener
       │
       └── CDdsMethodManager (owns method maps)
           ├── CallMethod → Request/Reply Topic pair
           ├── RegisterMethod → DdsMethodReaderListener
           └── GetField/SetField → mapped to CallMethod

CDdsTypeRegistry (Singleton, O(1) lookup)
       │
       ▼ per (serviceId, elementId)
IDdsTypeAdapter*
       │ provides
       ├── GetTypeSupport() → FastDDS auto-CDR
       ├── CreateSample(const void*, Size) / FreeSample(void*)
       └── ExtractData(const void* sample) → const void*

DdsPayload (fallback wire type)
       └── DdsPayloadPubSubType (手写 CDR 特化)
```

### 5.9 使用场景

| 场景 | 推荐配置 | 说明 |
|------|----------|------|
| 🚗 **车联网 V2X** | `m_bReliable=true`, Discovery Server | 跨车高频数据分发 |
| 🏭 **工业 IoT** | `m_bTransientLocal=true` | 传感器数据 + 历史缓存 |
| 🌐 **分布式跨 ECU** | TCP transport + Security | 广域网服务通信 |
| 🔒 **安全关键** | DDS Security 完整配置 | 认证 + 加密 + 授权 |
| ⚡ **本地高吞吐** | `m_bUseSharedMemory=true` | 共享内存 <10μs 延迟 |


---

## 6. 序列化策略 (符合 AUTOSAR 标准)

> **📖 代码生成器详细文档**: Franca IDL 代码生成器 `lap-sidl-gen` 的完整架构、AST 设计、CLI 使用和 AUTOSAR 标准追溯详见 [`GENERATOR.md`](GENERATOR.md)

**关键原则**: Com 模块遵循 **零手动序列化** 原则，所有序列化由外部库或代码生成工具自动完成。

### 6.1 D-Bus 自动序列化

**实现方式**: sdbus-c++ 库内置序列化

| 特性 | 实现 | AUTOSAR 兼容性 |
|------|------|---------------|
| 基础类型 | 自动 marshalling | ✅ 符合 |
| 复杂类型 | operator<< / operator>> | ✅ 符合 |
| 数组/容器 | STL 容器直接支持 | ✅ 符合 |
| 自定义结构 | 无需手动编码 | ✅ 符合 |

**优势**: 编译期类型安全，零运行时开销

### 6.2 IDL 驱动序列化

| 传输层 | IDL | 序列化路径 | 输出格式 |
|-------|-----|-----------|--------|
| SOME/IP (Legacy) | Franca IDL (.fidl) | `lap-sidl-gen` → commonapi-someip-generator | C++ 序列化器 + SOME/IP 线格式 |
| DDS（强类型） | Franca IDL (.fidl) → OMG IDL (.idl) | `lap-sidl-gen --dds-idl` → fastddsgen → `IDdsTypeAdapter` 实现 | FastDDS 自动 CDR |
| DDS（后备） | — | `DdsPayloadPubSubType` 手写 CDR 特化 | `DdsPayload { requestId, data[] }` |

> **DDS 强类型路径**：NVI 模板方法 `SendEvent<T>()` 将应用层强类型对象通过 `const void*` 传递给
> `DoSendEvent()`，DDS Binding 通过 `CDdsTypeRegistry` 查找 `IDdsTypeAdapter`，
> 再由 FastDDS `DataWriter::write()` 自动执行 OMG CDR 序列化 —— 全程无中间 ByteBuffer 拷贝。
> 详见 [§2.1 NVI 接口](#21-itransportbinding-nvi-接口) 和 [§5.2 Type Registry](#52-type-registry--强类型注入)。
>
> 完整的 IDL 代码生成流水线、类型映射和 Schema Hash 机制详见 [`GENERATOR.md`](GENERATOR.md)

### 6.3 Custom Protocol Binding - 自定义序列化

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

### 6.4 Legacy Binding - 网关转发

**实现方式**: 透明转发到独立网关进程

- 应用层无感知序列化
- 网关进程内部处理 SOME/IP/D-Bus 编解码
- lap::com ↔ 网关协议（Protobuf/YAML）


---

## 7. Custom Protocol + UDS Binding (source/binding/custom_protocol/) 🔧 轻量级

### 7.1 设计定位

**核心目标**: 提供**轻量级、灵活可定制**的私有通信协议，基于 Unix Domain Socket

**适用场景**:
- 🔧 **遗留系统集成**: 适配已有的私有协议系统
- 🎯 **特殊需求**: 自定义序列化格式、特殊加密算法
- ⚡ **轻量级通信**: 无需 DDS/Core IPC 复杂性的简单场景
- 🔒 **本地安全**: Unix Domain Socket 本地进程通信
- 🚀 **快速原型**: 快速开发验证，后期可迁移到 DDS/Core IPC

**性能指标**:
- 延迟: < 10μs (流式模式，SOCK_STREAM)
- 吞吐量: > 500 MB/s (本地 UDS)
- CPU占用: < 1%
- 零外部依赖: 仅依赖标准 POSIX API

### 7.2 架构组成

| 组件 | 功能 | 文件 |
|------|------|------|
| `UdsTransport` | Unix Domain Socket 管理（流式/数据报） | `UdsTransport.hpp` |
| `CustomProtocolCodec` | 可扩展编解码器框架 | `ProtocolCodec.hpp` |
| `BinarySerializer` | 高性能二进制序列化 | `BinarySerializer.hpp` |
| `CustomMethodBinding` | 方法调用绑定 | `CustomMethodBinding.hpp` |
| `CustomEventBinding` | 事件广播绑定 | `CustomEventBinding.hpp` |
| `CustomFieldBinding` | Field 通知绑定 | `CustomFieldBinding.hpp` |
| `DiscoveryManager` | 本地服务发现（UDS + 文件系统） | `DiscoveryManager.hpp` |

### 7.3 协议帧格式（默认实现）

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

### 7.4 核心组件设计

**UdsTransport.hpp**
```cpp
namespace lap {
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
} // namespace lap
```

**ProtocolCodec.hpp (可扩展编解码框架)**
```cpp
namespace lap {
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
} // namespace lap
```

### 7.5 AUTOSAR 集成

> Franca IDL 用于接口定义 → `lap-sidl-gen` 自动生成 Proxy/Skeleton/Types 头文件，详见 [`GENERATOR.md`](GENERATOR.md)

### 7.6 性能基准

| 指标 | D-Bus | SOME/IP | Core IPC | **Custom+UDS** |
|------|-------|---------|----------|----------------|
| 延迟 (小消息) | 50-100μs | 20-50μs | < 5μs | **< 10μs** |
| 吞吐量 (MB/s) | 50-100 | 200-300 | > 10,000 | **> 500** |
| CPU 占用 | 3-5% | 2-4% | < 0.5% | **< 1%** |
| 外部依赖 | libdbus | vsomeip | lap_core | **无** |
| 学习曲线 | 中 | 高 | 中 | **低** |
| 跨网络 | ❌ | ✅ | ❌ | **❌** |

### 7.7 使用场景

| 场景 | 推荐传输 | 原因 |
|------|----------|------|
| 遗留私有协议集成 | **Custom+UDS** | 灵活定制协议格式 |
| 快速原型验证 | **Custom+UDS** | 零依赖，快速开发 |
| 诊断工具通信 | **Custom+UDS** | 简单轻量 |
| 嵌入式资源受限 | **Custom+UDS** | 最小内存占用 |
| 临时通信通道 | **Custom+UDS** | 即插即用 |

### 7.8 配置示例

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

### 7.9 实现路线图

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

### 7.10 与其他 Binding 对比

| 特性 | Core IPC | DDS | Custom+UDS | Legacy Gateway |
|------|---------|-----|------------|----------------|
| **延迟** | <5μs | 10-30μs | <10μs | >50μs |
| **吞吐量** | >10GB/s | 500-800MB/s | >500MB/s | <300MB/s |
| **零拷贝** | ✅ | ✅ | ❌ | ❌ |
| **跨ECU** | ❌ | ✅ | ❌ | ✅ |
| **外部依赖** | lap_core | Fast-DDS | 无 | vsomeip/dbus |
| **定制性** | 低 | 中 | **高** | 低 |
| **学习曲线** | 中 | 高 | **低** | 中 |
| **适用场景** | 本地高性能 | 分布式 | **遗留集成/原型** | 遗留兼容 |

---

## 8. Custom Protocol + UDP Binding (source/binding/custom/) 

### 8.1 设计定位

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

### 8.2 架构组成

| 组件 | 功能 | 文件 |
|------|------|------|
| `UdpTransport` | UDP Socket 管理（单播/广播/组播） | `UdpTransport.hpp` |
| `CustomProtocolCodec` | 可扩展的编解码器框架 | `ProtocolCodec.hpp` |
| `BinarySerializer` | 高性能二进制序列化 | `BinarySerializer.hpp` |
| `CustomMethodBinding` | 方法调用绑定 | `CustomMethodBinding.hpp` |
| `CustomEventBinding` | 事件广播绑定 | `CustomEventBinding.hpp` |
| `DiscoveryManager` | UDP 广播服务发现 | `DiscoveryManager.hpp` |

### 8.3 协议帧格式（默认实现）

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

### 8.4 核心组件设计

**UdpTransport.hpp**
```cpp
namespace lap {
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
} // namespace lap
```

**ProtocolCodec.hpp (可扩展编解码框架)**
```cpp
namespace lap {
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
} // namespace lap
```

**BinarySerializer.hpp (高性能序列化)**
```cpp
namespace lap {
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
} // namespace lap
```

### 8.5 UDP 服务发现

**DiscoveryManager.hpp**
```cpp
namespace lap {
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
} // namespace lap
```

### 8.6 性能特点

| 特性 | 指标 | 说明 |
|------|------|------|
| **帧头开销** | 24 字节 | 固定大小，无可变长字段 |
| **序列化速度** | > 1 GB/s | 直接内存操作，无反射 |
| **MTU 适配** | 自动分片 | 支持大于 1472 字节的消息 |
| **延迟** | < 100μs | 无框架开销 |
| **吞吐量** | 取决于网络 | UDP 理论上限 ~1 Gbps (千兆网) |
| **内存占用** | < 100 KB | 最小依赖 |

### 8.7 使用场景

| 场景 | 配置 | 原因 |
|------|------|------|
| **遗留设备对接** | 自定义 Codec | 兼容已有协议格式 |
| **服务广播发现** | UDP 广播 | 无需中心化服务发现 |
| **轻量级传感器** | 二进制序列化 | 减少嵌入式设备负担 |
| **局域网多播** | UDP 组播 | 高效一对多通信 |
| **快速原型** | 默认二进制编解码 | 无需代码生成工具 |

### 8.8 配置示例

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

### 8.9 实现路线图

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

### 8.10 扩展性示例

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


---

## 9. Legacy Binding (source/binding/legacy/) - 遗留协议网关接口

**AUTOSAR 对应**: Legacy Protocol Compatibility Layer

**设计理念**: 遗留协议（SOME/IP、D-Bus）运行在独立网关进程中，binding_legacy.so 仅提供转发接口

### 9.1 架构设计

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

### 9.2 独立网关进程

**SomeIpGateway** (独立进程):
- vsomeip3 协议栈
- DDS 双向转换
- Protobuf 网关协议
- 独立生命周期管理

**DiagDaemon** (独立进程):
- D-Bus 诊断服务
- UDS (Unified Diagnostic Services)
- 仅诊断功能，与主通信解耦

### 9.3 配置示例

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


---

## 10. 遗留实现归档

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
