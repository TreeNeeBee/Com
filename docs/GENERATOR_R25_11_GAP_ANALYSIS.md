# Generator 代码生成器 R25-11 规范差距分析

> **日期**: 2026/02/11
> **作者**: Aii
> **参考规范**: AUTOSAR_AP_SWS_CommunicationManagement R25-11 (675 pages)
> **参考解释文档**: AUTOSAR_AP_EXP_ARAComAPI R25-11 (125 pages)
> **当前生成器**: `lap_sidl_gen v1.0`

---

## 一、R25-11 规范要求的生成文件结构

每个 `ServiceInterface` 生成 **3 个头文件**：

| 文件 | SWS 编号 | 说明 |
|------|----------|------|
| `{si}_common.h` | SWS_CM_01012 | 公共定义 — Service ID、版本、共享类型 |
| `{si}_proxy.h` | SWS_CM_11503 | 客户端 Proxy — include `_common.h` |
| `{si}_skeleton.h` | SWS_CM_01002 | 服务端 Skeleton — include `_common.h` |

文件路径: `{namespace-derived-directory-path-lower}/{si-shortname-lower}_xxx.h`

示例：
```
radar/services/radar_service_common.h
radar/services/radar_service_proxy.h
radar/services/radar_service_skeleton.h
```

---

## 二、R25-11 命名空间结构

### 2.1 Common Header (_common.h)

```cpp
// SWS_CM_11500 — 服务命名空间
namespace ara::com::radar::services {

    // SWS_CM_11501 — common 内层命名空间
    namespace common {

        // SWS_CM_01010 — 公共类
        class RadarService {
            // SWS_CM_11506
            static const ara::com::ServiceIdentifierType serviceIdentifier;
            // SWS_CM_11507
            static const ara::com::ServiceVersionType serviceVersion;
            // SWS_CM_11508
            static std::uint32_t serviceContractVersionMajor;
            // SWS_CM_11509
            static std::uint32_t serviceContractVersionMinor;
        };

    } // namespace common
} // namespace ara::com::radar::services
```

### 2.2 Proxy Header (_proxy.h)

```cpp
namespace ara::com::radar::services {

    // SWS_CM_01007 — proxy 内层命名空间
    namespace proxy {

        // SWS_CM_98447 — events 子命名空间
        namespace events {
            // SWS_CM_00005 — 每个 event 一个 final class
            class BrakeEvent final { ... };
            class ParkingBrakeEvent final { ... };
        }

        // SWS_CM_01015 — methods 子命名空间
        namespace methods {
            // SWS_CM_00191 — 每个 method 一个 final class
            class Calibrate final { ... };
            class Adjust final { ... };
        }

        // SWS_CM_98444 — fields 子命名空间
        namespace fields {
            // SWS_CM_00007 — 每个 field 一个 final class
            class UpdateRate final { ... };
        }

        // SWS_CM_11505 — triggers 子命名空间 (R25-11 新增)
        namespace triggers {
            class Alert final { ... };
        }

        // SWS_CM_00004 — Proxy class 本身为 final
        class RadarServiceProxy final {
        public:
            class HandleType { ... };

            // SWS_CM_99445 — event 成员变量
            events::BrakeEvent brakeEvent;
            events::ParkingBrakeEvent parkingBrakeEvent;

            // SWS_CM_99447 — method 成员变量
            methods::Calibrate calibrate;
            methods::Adjust adjust;

            // SWS_CM_99446 — field 成员变量
            fields::UpdateRate updateRate;

            // SWS_CM_00131 — 显式构造函数（抛异常版）
            explicit RadarServiceProxy(const HandleType& handle);

            // SWS_CM_10438 — Named Constructor（noexcept 版）
            static ara::core::Result<RadarServiceProxy> Create(
                const HandleType& handle) noexcept;

            // SWS_CM_11551 — copy 删除
            RadarServiceProxy& operator=(const RadarServiceProxy&) = delete;
            // SWS_CM_00136
            RadarServiceProxy(const RadarServiceProxy&) = delete;

            // SWS_CM_11552 — move 允许
            RadarServiceProxy& operator=(RadarServiceProxy&&) noexcept;
            // SWS_CM_00137
            RadarServiceProxy(RadarServiceProxy&&);

            // SWS_CM_00122 — FindService
            static ara::core::Result<ServiceHandleContainer<HandleType>>
                FindService(ara::com::InstanceIdentifier instanceId);

            // SWS_CM_00123 — StartFindService
            static ara::core::Result<ara::com::FindServiceHandle>
                StartFindService(
                    FindServiceHandler<HandleType> handler,
                    ara::com::InstanceIdentifier instanceId);
        };

    } // namespace proxy
} // namespace ara::com::radar::services
```

### 2.3 Skeleton Header (_skeleton.h)

```cpp
namespace ara::com::radar::services {

    // SWS_CM_01006 — skeleton 内层命名空间
    namespace skeleton {

        namespace events {
            // SWS_CM_00003
            class BrakeEvent { ... };
        }

        namespace fields {
            class UpdateRate { ... };
        }

        namespace triggers {
            // SWS_CM_00726 — R25-11 新增
            class Alert { ... };
        }

        // SWS_CM_00002 — Skeleton class (非 final，允许继承)
        class RadarServiceSkeleton {
        public:
            // SWS_CM_99557 — event 成员
            events::BrakeEvent brakeEvent;

            // SWS_CM_99558 — field 成员
            fields::UpdateRate updateRate;

            // SWS_CM_00130 — 构造
            RadarServiceSkeleton(
                ara::com::InstanceIdentifier instanceID,
                ara::com::MethodCallProcessingMode mode
                    = ara::com::MethodCallProcessingMode::kEvent) noexcept;

            // SWS_CM_00153 — 多实例构造
            RadarServiceSkeleton(
                ara::com::InstanceIdentifierContainer instanceIDs,
                ara::com::MethodCallProcessingMode mode
                    = ara::com::MethodCallProcessingMode::kEvent) noexcept;

            // SWS_CM_11370 — 虚析构
            virtual ~RadarServiceSkeleton();

            // SWS_CM_00134 — copy 删除
            RadarServiceSkeleton(const RadarServiceSkeleton&) = delete;
            RadarServiceSkeleton& operator=(const RadarServiceSkeleton&) = delete;

            // SWS_CM_00135 — move 允许
            RadarServiceSkeleton(RadarServiceSkeleton&&);
            // SWS_CM_11549
            RadarServiceSkeleton& operator=(RadarServiceSkeleton&&) noexcept;

            // SWS_CM_00101
            ara::core::Result<void> OfferService();
            void StopOfferService();
        };

    } // namespace skeleton
} // namespace ara::com::radar::services
```

---

## 三、R25-11 核心架构要求

### 3.1 Deployment 与编译解耦

**[SWS_CM_10384]** — 更改 Service Interface Deployment **不需要重新编译**应用：
- 可从 `SomeipServiceInterfaceDeployment` 改为 `UserDefinedServiceInterfaceDeployment`
- 可更改序列化属性（字节序、对齐、字符串编码等）
- 只需 re-link（静态或动态链接）

**[SWS_CM_10385]** — 更改 Service Instance Deployment **不需要重新编译**应用

**[SWS_CM_10386]** — 更改 Network Configuration **不需要重新编译**应用

### 3.2 Binding 不属于标准生成代码

Figure 7.1 架构：
```
┌────────────────────────────┐
│     Adaptive Application   │
├────────────────────────────┤  ← ara::com API (标准化)
│   C++ Language Binding     │
├────────────────────────────┤
│   Communication Binding    │  ← 非标准化，平台供应商实现
├─────────────┬──────────────┤
│  SOME/IP    │    IPC       │  ← 非标准化
│  Transport  │  Transport   │
├─────────────┴──────────────┤
│      TCP/IP    /   IPC     │
├────────────────────────────┤
│   Ethernet Driver          │
└────────────────────────────┘
```

**关键设计原则**（EXP_ARAComAPI Chapter 4.1）：
- Proxy/Skeleton 从 Service Interface Definition **生成**
- Binding 实现属于平台供应商，**不生成**
- Language Binding + Communication Binding 在**应用二进制**中部署
- Serialization 在 Communication Binding 中运行，在应用执行上下文内

### 3.3 Multi-Binding 概念

**定义**（SWS_CM Glossary）：
> Multi-Binding describes setups having multiple connections implemented by
> single proxy or skeleton class.

**使用场景**（EXP Chapter 7.3）：
1. **Simple** — 同一 Proxy class 的不同实例使用不同传输（同进程直调 vs IPC）
2. **Local/Network** — 一个实例本地 IPC，另一个实例走 SOME/IP 网络
3. **Typical SOME/IP** — 通过 SOME/IP daemon 做端口复用

**对 LightAP 的意义**：当前 `BindingManager.SelectBinding()` 机制已支持 Multi-Binding 的核心场景。

---

## 四、与 CommonAPI (COVESA) 的对比

| 维度 | AUTOSAR R25-11 标准 | CommonAPI 实现 | LightAP 当前 |
|------|-------------------|---------------|-------------|
| **生成文件** | 3 (`_common`, `_proxy`, `_skeleton`) | 5+ core + 3×N binding | 3 (`Types`, `Proxy`, `Skeleton`) |
| **Binding 代码** | **不生成** — 属于供应商实现 | **每个 binding 单独生成** | **不生成** ✅ |
| **Stub 概念** | 规范不定义 Stub | Stub = Abstract interface + StubDefault | Skeleton = 具体类 |
| **Deployment** | Manifest 配置，不编译入应用 | `.fdepl` 文件编译为 Deployment.cpp | config.json 运行时加载 ✅ |
| **Serialization** | binding 内部实现 | binding-specific 生成代码 | `ISerializer` 统一接口 ✅ |

**结论**：CommonAPI 的 3 层生成模型是 **一种实现选择**，不是 AUTOSAR 标准要求。R25-11 标准只定义了 `_proxy.h` 和 `_skeleton.h` 的 API 接口，不要求生成任何 binding 代码。

---

## 4.5 与 SOME/IP 代码生成实现对比

### 4.5.1 CommonAPI-SomeIP 生成模型

CommonAPI-SomeIP 使用独立的 `commonapi-someip-generator` 为每个 ServiceInterface 生成一组 binding-specific 源文件：

| 生成文件 | 说明 |
|----------|------|
| `{Si}SomeIPProxy.hpp / .cpp` | SOME/IP Proxy 桩代码 — 封装 `vsomeip::application` 发送/接收 |
| `{Si}SomeIPStubAdapter.hpp / .cpp` | SOME/IP Stub Adapter — 将 vsomeip 回调分发到 Skeleton |
| `{Si}SomeIPDeployment.hpp / .cpp` | SOME/IP 部署描述 — 字节序、字符串编码、对齐等 |

每个 binding 还需要一个 `.fdepl` (FRANCA Deployment) 文件来描述：
- Method/Event/Field 的 SOME/IP Method ID
- 序列化属性（big/little endian、string 编码等）
- 协议版本

**典型生成命令**:
```bash
# 核心层（binding 无关）
commonapi-core-generator --skel --proxy -d gen-core MyService.fidl
# SOME/IP 层（binding 特定）
commonapi-someip-generator -d gen-someip MyService.fidl MyService_SomeIP.fdepl
# D-Bus 层（可选）
commonapi-dbus-generator -d gen-dbus MyService.fidl
```

**总文件数**: Core (5+) + SOME/IP (6+) + D-Bus (6+) = **17+ 生成文件** / 每个 ServiceInterface

### 4.5.2 LightAP 的 SOME/IP 实现路径

LightAP **不生成任何 SOME/IP 特定代码**，而是通过运行时插件架构处理：

```
┌─────────────────────────────────────────────────┐
│  应用代码                                        │
│  - 使用 HelloWorldProxy / HelloWorldSkeleton     │
├─────────────────────────────────────────────────┤
│  生成代码 (lap_sidl_gen)                          │
│  - HelloWorldTypes.hpp (类型 + 序列化)            │
│  - HelloWorldProxy.hpp (Proxy API)               │
│  - HelloWorldSkeleton.hpp (Skeleton API)         │
├─────────────────────────────────────────────────┤
│  Runtime (ProxyBase / SkeletonBase)              │
│  - BindingManager::SelectBinding(kServiceId)     │
│  - ITransportBinding 接口                        │
├─────────────┬────────────┬──────────────────────┤
│  CoreIPC    │  DDS/RTPS  │  SOME/IP (stub)      │
│  binding    │  binding   │  binding             │
│  (pri=100)  │  (pri=80)  │  (pri=60)            │
└─────────────┴────────────┴──────────────────────┘
```

**关键接口** — `ITransportBinding`（所有 binding 统一实现）：
```cpp
class ITransportBinding {
public:
    virtual std::string GetName() const = 0;
    virtual int GetPriority() const = 0;
    virtual bool SupportsService( UInt16 serviceId ) const = 0;

    // 统一 ByteBuffer 通信接口
    virtual Result<void> Subscribe(
        UInt16 serviceId, UInt16 elementId, ... ) = 0;
    virtual Result<void> SendRequest(
        UInt16 serviceId, UInt16 elementId,
        const ByteBuffer& payload, ... ) = 0;
    virtual Result<void> PublishEvent(
        UInt16 serviceId, UInt16 elementId,
        const ByteBuffer& payload ) = 0;
    virtual Result<void> OfferService( UInt16 serviceId ) = 0;
    virtual Result<void> StopOffer( UInt16 serviceId ) = 0;
};
```

**对比关键点**：

| 维度 | CommonAPI-SomeIP | LightAP |
|------|-----------------|---------|
| Binding 切换 | 重新生成 + 重新编译 | 改 `config.json` 即可 |
| 部署配置 | `.fdepl` 编译入应用 | `config.json` 运行时加载 |
| 序列化 | 生成代码中硬编码 | `ISerializer` + ADL `Serialize()` |
| vsomeip 集成 | 生成代码直接调用 vsomeip API | binding 插件封装 vsomeip (待实现) |
| 新增 binding | 写新 generator + `.fdepl` | 实现 `ITransportBinding` 接口 |

### 4.5.3 LightAP SOME/IP Stub 现状

当前 SOME/IP binding 在 `modules/Com/source/binding/someip/` 下为 **stub 状态**，仅注册了插件框架：

```cpp
// CSomeIpBinding.cpp — 当前 stub 实现
class CSomeIpBinding : public ITransportBinding {
    std::string GetName() const override { return "SOME/IP"; }
    int GetPriority() const override { return 60; }
    bool SupportsService( UInt16 ) const override { return false; }
    // 所有操作返回 kNotImplemented ...
};
```

**完成 SOME/IP binding 不需要修改代码生成器**，只需：
1. 集成 `vsomeip` 库
2. 在 `CSomeIpBinding` 中实现 `ITransportBinding` 的所有接口
3. 在 `config.json` 中配置 SOME/IP 的 service instance mapping

---

## 4.6 与 DDS 代码生成实现对比

### 4.6.1 典型 DDS 代码生成流程

标准 DDS 开发通常使用 IDL 编译器（如 FastDDS 的 `fastddsgen`）从 IDL 文件生成类型支持代码：

```
MyService.idl
    │
    ▼  fastddsgen / rtiddsgen
┌──────────────────────────────┐
│ MyServicePubSubTypes.cxx/h   │  — DDS TypeSupport (序列化/反序列化)
│ MyService.cxx/h              │  — 类型定义 C++ 映射
│ MyServiceTypeObjectSupport   │  — XTypes 支持
└──────────────────────────────┘
```

每个 IDL `struct` 会生成对应的 C++ class + TypeSupport，用于 DDS DataWriter/DataReader 的序列化。

**典型 DDS-RPC (Request/Reply) 模式**：
- 为每个 method 生成 `{Method}Request` / `{Method}Reply` 结构
- 使用 DDS Topic 作为传输通道
- Event → DDS Topic publish
- Field → DDS Topic + Get/Set request/reply

### 4.6.2 LightAP 的 DDS 实现

LightAP 的 DDS 方案分为 **生成时** 和 **运行时** 两部分：

**生成时** — `lap_sidl_gen --dds-idl`：
```idl
// HelloWorld.idl — 为 DDS 序列化生成的 IDL
module lap { module com { module examples {
    // Event 结构
    struct GreetingEvent {
        string message;
        unsigned long timestamp;
    };

    // Method Request/Reply
    struct SayHello_Request {
        string name;
    };
    struct SayHello_Reply {
        string result;
    };

    // 通用传输消息 — 已移除 LapComMessage.idl
    // 现在由代码定义的 DdsPayload (CDdsPayload.hpp) 替代:
    // struct DdsPayload { uint64 request_id; sequence<octet> data; };
}; }; };
```

**运行时** — `CDdsBinding` (FastDDS)：

LightAP 的 DDS binding 使用代码定义的 `DdsPayload` 最小线协议类型（`CDdsPayload.hpp`）。
寻址信息（service/instance/event ID）编码在 DDS Topic 名称中，不在消息体内重复。
Per-service 强类型可通过 `IDdsTypeAdapter` / `CDdsTypeRegistry` 在运行时注册。

```cpp
// 通信流程:
// 1. 应用调用 proxy.sayHello("Alice")
// 2. ProxyMethod 用 ADL Serialize() 序列化为 ByteBuffer
// 3. CDdsBinding 将 ByteBuffer 封装到 DdsPayload{request_id, data}
// 4. FastDDS DataWriter 发布 DdsPayload 到 topic (topic 名编码 serviceId/instanceId/eventId)
// 5. 服务端 FastDDS DataReader 接收
// 6. CDdsBinding 解包 → SkeletonMethod 用 ADL Deserialize() 反序列化
```

**实际 DDS binding 核心代码**（`CDdsEventManager.cpp` 概要）：
```cpp
// SendEvent:
DdsPayload msg( std::vector<uint8_t>( data.begin(), data.end() ) );
writerIt->second->write( &msg );

// SubscribeEvent — listener 在构造时绑定 serviceId/instanceId/eventId:
auto pListener = MakeUnique<DdsReaderListener>(
    callback, serviceId, instanceId, eventId, m_metrics );
```

### 4.6.3 与标准 DDS 生成方案对比

| 维度 | 标准 DDS (fastddsgen) | LightAP DDS |
|------|----------------------|-------------|
| IDL 编译 | 每个 struct → C++ class + TypeSupport | 仅生成 IDL 供参考，可选编译 |
| DDS Topic 类型 | per-service 强类型 Topic | `DdsPayload` 最小线协议 + 可注册 per-service adapter |
| 序列化 | DDS TypeSupport (CDR) | ADL `Serialize()` → `ByteBuffer` → DdsPayload |
| 类型安全 | DDS 层面类型安全 | 应用层面类型安全（通过 serviceId/elementId 路由） |
| 性能 | **优** — zero-copy 场景可用 | **良** — 额外一层 ByteBuffer 封装 |
| 灵活性 | **低** — 每加一个 type 需重新生成 | **高** — 新 service 只需配 serviceId |
| 代码量 | 生成大量 TypeSupport 代码 | 零额外 DDS 生成代码 |
| 标准合规 | 纯 DDS 标准 | AUTOSAR AP 风格（Proxy/Skeleton + 多 binding） |

### 4.6.4 LightAP DDS IDL 的定位

`lap_sidl_gen --dds-idl` 生成的 `.idl` 文件当前用于：
1. **文档用途** — 定义 DDS 线上消息格式，供跨平台互操作参考
2. **可选编译** — 如需与标准 DDS 工具互操作，可用 `fastddsgen` 编译
3. **测试验证** — 与纯 DDS 系统集成测试时使用

**但不是 LightAP 运行时所必需的** — CDdsBinding 使用内置的代码定义 `DdsPayload` TypeSupport（`LapComMessage.idl` 已移除）。Per-service 生成的 TypeSupport 可通过 `CDdsTypeRegistry` 在运行时注册为强类型 adapter。

---

## 4.7 三种方案综合对比

| 维度 | CommonAPI (SOME/IP+D-Bus) | 标准 DDS (fastddsgen) | LightAP |
|------|--------------------------|----------------------|---------|
| **代码生成器数量** | 1 core + N binding gen | 1 IDL compiler | 1 (`lap_sidl_gen`) |
| **每 Service 生成文件** | 17+ (core 5 + binding 6×N) | 3-6 per IDL struct | 3-4 (Types+Proxy+Skeleton+IDL) |
| **切换传输协议** | 重新生成 + 编译 | N/A（DDS only） | 改 config.json |
| **序列化方式** | binding 生成代码硬编码 | CDR (DDS TypeSupport) | ADL `Serialize()` 统一 |
| **多 binding 支持** | 编译时选择 | 仅 DDS | 运行时 `BindingManager` |
| **Deployment 配置** | `.fdepl` 编译入应用 | DDS XML 配置 | `config.json` 运行时 |
| **Stub/桩代码** | 每个 binding 生成桩 | 无（IDL TypeSupport） | **无** — 不需要 ✅ |
| **AUTOSAR AP 合规** | 非标准（COVESA 项目） | 非标准（OMG 标准） | 目标 R25-11 合规 |
| **主要优势** | 成熟、广泛使用 | 强类型、零拷贝 | 轻量、灵活、运行时可配 |
| **主要劣势** | 生成代码庞大、绑定编译时 | 仅限 DDS 传输 | SOME/IP binding 待完成 |

### 4.7.1 结论

1. **不需要生成 binding 代码** — R25-11 规范明确分离了 Language Binding（标准化 API）和 Communication Binding（供应商实现）。LightAP 的 `ITransportBinding` 插件架构是正确的选择。

2. **不需要参考 CommonAPI 的 3 层生成模式** — 那是实现选择，不是标准要求。LightAP 用 1 个生成器 + 运行时 binding 管理，更贴近 R25-11 精神。

3. **DDS IDL 生成是有益补充** — 保留 `--dds-idl` 选项用于互操作和文档，但运行时不依赖。

4. **后续工作重点应在生成代码的 API 形态上**（命名空间、per-element classes、common header），而非在 binding 生成上。

---

## 五、当前 LightAP Generator 差距分析

### 5.1 结构差异对比

| 维度 | R25-11 规范 | LightAP 当前 | 差距等级 |
|------|-------------|-------------|---------|
| 生成文件数 | 3 (`_common`, `_proxy`, `_skeleton`) | 3 (`Types`, `Proxy`, `Skeleton`) | ≈等价 |
| Proxy class | `final`，在 `::proxy` 命名空间 | 非 final，无内层命名空间 | **高** |
| Skeleton class | 非 final，在 `::skeleton` 命名空间 | 无内层命名空间 | **高** |
| Event class | 独立 `final` class 在 `events::` | 直接 `ProxyEvent<T>` 模板 | **中** |
| Method class | 独立 `final` class 在 `methods::` | 直接 `ProxyMethod<Ret,Args>` 模板 | **中** |
| Field class | 独立 `final` class 在 `fields::` | 直接 `ProxyField<T>` 模板 | **中** |
| Trigger | `triggers::` 命名空间 | 不支持 | **低**（标准也标注未来版本） |
| Common Header | `_common.h` 独立文件 | Types.hpp 充当 common | **中** |
| ServiceIdentifier | `ServiceIdentifierType` in `::common` | `constexpr UInt16 kServiceId` | **中** |
| ServiceVersion | `ServiceVersionType` in `::common` | `constexpr UInt32 kVersionMajor/Minor` | **中** |
| 文件命名 | `{si}_proxy.h` 小写下划线 | `{Si}Proxy.hpp` PascalCase | **低**（风格差异） |
| Binding 注入 | 不编译入应用 [SWS_CM_10384] | 运行时 `BindingManager` | ✅ 已满足 |
| Stub 生成 | **规范不要求** | 无 | ✅ 正确选择 |

### 5.2 当前生成示例 vs 目标

**当前 LightAP 生成代码**：
```cpp
namespace lap::com::examples {

    class HelloWorldProxy : public ProxyBase {
    public:
        static constexpr UInt16 kServiceId = 0x02e0;

        ProxyEvent<GreetingEvent> greeting;
        ProxyMethod<String, String> sayHello;
        ProxyField<UInt32> visitorCount{true, false, false};

        static Result<HelloWorldProxy> Create(const HandleType& handle) noexcept;
    };

} // namespace lap::com::examples
```

**R25-11 标准目标结构**：
```cpp
namespace lap::com::examples {

    namespace common {
        class HelloWorld {
            static const ServiceIdentifierType serviceIdentifier;
            static const ServiceVersionType serviceVersion;
            static std::uint32_t serviceContractVersionMajor;
            static std::uint32_t serviceContractVersionMinor;
        };
    }

    namespace proxy {
        namespace events {
            class Greeting final { /* wraps ProxyEvent<GreetingEvent> */ };
        }
        namespace methods {
            class SayHello final { /* wraps ProxyMethod<String,String> */ };
        }
        namespace fields {
            class VisitorCount final { /* wraps ProxyField<UInt32> */ };
        }

        class HelloWorldProxy final {
            events::Greeting greeting;
            methods::SayHello sayHello;
            fields::VisitorCount visitorCount;

            static Result<HelloWorldProxy> Create(const HandleType& handle) noexcept;
        };
    }

    namespace skeleton {
        namespace events {
            class Greeting { /* wraps SkeletonEvent<GreetingEvent> */ };
        }
        namespace fields {
            class VisitorCount { /* wraps SkeletonField<UInt32> */ };
        }

        class HelloWorldSkeleton {
            events::Greeting greeting;
            fields::VisitorCount visitorCount;

            Result<void> OfferService();
            void StopOfferService();
        };
    }

} // namespace lap::com::examples
```

---

## 六、重构计划

### Phase 1 — 命名空间对齐（高优先级）

**目标**: 添加 `::proxy`、`::skeleton`、`::common` 内层命名空间

**修改文件**:
- `generator/src/CProxyGenerator.cpp` — 输出包裹在 `namespace proxy { ... }`
- `generator/src/CSkeletonGenerator.cpp` — 输出包裹在 `namespace skeleton { ... }`
- `generator/src/CTypesGenerator.cpp` — 添加 `namespace common { class Si { ... }; }`

**影响**: 所有使用生成代码的应用需更新 namespace 引用
- `HelloWorldProxy` → `proxy::HelloWorldProxy`
- `HelloWorldSkeleton` → `skeleton::HelloWorldSkeleton`

**可选**: 提供 using 别名保持向后兼容（但 rules.md 规定不需要向后兼容）

### Phase 2 — Events/Methods/Fields 子类化（中优先级）

**目标**: 为每个 Event/Method/Field 生成独立的具名 class

**方案**: 生成 thin wrapper final class:
```cpp
namespace events {
    class Greeting final : public ::lap::com::ProxyEvent<GreetingEvent> {
        using ::lap::com::ProxyEvent<GreetingEvent>::ProxyEvent;
    };
}
```

或组合模式（如果不想暴露基类）：
```cpp
namespace events {
    class Greeting final {
    public:
        using SampleType = GreetingEvent;  // SWS_CM_00005 要求
        // 转发所有公共接口 ...
    private:
        ::lap::com::ProxyEvent<GreetingEvent> m_impl;
    };
}
```

**选择建议**: 继承方案更简洁，且当前 `ProxyEvent<T>` 等已有完整接口。

### Phase 3 — Common Header 拆分（中优先级）

**目标**: 从 `Types.hpp` 拆分出 `_common.h`

**产出**:
- `HelloWorld_common.hpp` — ServiceIdentifier、ServiceVersion、共享类型
- `HelloWorld_proxy.hpp` — include `_common.hpp`，Proxy 定义
- `HelloWorld_skeleton.hpp` — include `_common.hpp`，Skeleton 定义
- `HelloWorldTypes.hpp` — 保持兼容或合并入 `_common.hpp`

### Phase 4 — Proxy final 修饰（中优先级）

**当前**: `class HelloWorldProxy : public ProxyBase { ... }`
**目标**: `class HelloWorldProxy final : public ProxyBase { ... }`

**注意**: R25-11 中 Skeleton **不是** final（允许用户继承实现方法），Proxy **是** final。

### Phase 5 — Trigger 支持（低优先级 / 未来）

R25-11 7.1.3 注释: "*ServiceInterface.triggers are not supported in the current version*"

仅需预留 `namespace triggers {}` 即可。

---

## 七、不需要更改的部分

以下方面 LightAP 当前设计 **已符合** R25-11 规范精神：

1. **不生成 Binding/Stub 代码** ✅ — 规范不要求，binding 属于平台供应商实现
2. **运行时 Binding 选择** ✅ — `BindingManager` + 插件架构符合 [SWS_CM_10384]
3. **ADL 序列化** ✅ — 序列化在 Communication Binding 层，不在生成代码中硬编码
4. **Move-only / Non-copyable** ✅ — 符合 [SWS_CM_11551..11554] 和 [SWS_CM_11544..11549]
5. **Named Constructor Create()** ✅ — 符合 [SWS_CM_10438]
6. **OfferService / StopOfferService** ✅ — 符合 [SWS_CM_00101] / [SWS_CM_00111]
7. **FindService** ✅ — 符合 [SWS_CM_00122]
8. **Element ID 分配** ✅ — Events 0x0001-00FF, Methods 0x0100-01FF, Fields 0x0200-02FF
9. **DDS IDL 生成** ✅ — 补充输出，符合 DDS Network Binding 需求

---

## 八、SWS 编号速查表

| SWS 编号 | 描述 | 当前状态 |
|----------|------|---------|
| SWS_CM_01012 | Common Header 文件结构 | ❌ 未拆分 |
| SWS_CM_11503 | Proxy Header 文件结构 | ⚠️ 部分符合 |
| SWS_CM_01002 | Skeleton Header 文件结构 | ⚠️ 部分符合 |
| SWS_CM_11500 | 服务命名空间 | ✅ 符合 |
| SWS_CM_11501 | common 命名空间 | ❌ 缺失 |
| SWS_CM_01007 | proxy 命名空间 | ❌ 缺失 |
| SWS_CM_01006 | skeleton 命名空间 | ❌ 缺失 |
| SWS_CM_98447 | events 命名空间 (proxy) | ❌ 缺失 |
| SWS_CM_01015 | methods 命名空间 (proxy) | ❌ 缺失 |
| SWS_CM_98444 | fields 命名空间 (proxy) | ❌ 缺失 |
| SWS_CM_11505 | triggers 命名空间 (proxy) | ❌ 缺失（标准也标注未来） |
| SWS_CM_00004 | Proxy class (final) | ⚠️ 缺 final |
| SWS_CM_00002 | Skeleton class | ✅ 基本符合 |
| SWS_CM_01010 | Common class (Si 标识) | ❌ 缺失 |
| SWS_CM_11506 | serviceIdentifier | ⚠️ 用 kServiceId 替代 |
| SWS_CM_11507 | serviceVersion | ⚠️ 用 kVersionMajor/Minor 替代 |
| SWS_CM_10438 | Proxy::Create() | ✅ 符合 |
| SWS_CM_00131 | Proxy 显式构造 | ❌ 仅有 Create() |
| SWS_CM_00130 | Skeleton 构造 | ✅ 符合 |
| SWS_CM_10384 | Deployment 更改不重编译 | ✅ BindingManager 运行时选择 |
| SWS_CM_00005 | Event class (proxy) | ⚠️ 用 ProxyEvent<T> 替代 |
| SWS_CM_00003 | Event class (skeleton) | ⚠️ 用 SkeletonEvent<T> 替代 |
| SWS_CM_00191 | Method class (proxy) | ⚠️ 用 ProxyMethod 替代 |
| SWS_CM_00007 | Field class | ⚠️ 用 ProxyField/SkeletonField 替代 |
