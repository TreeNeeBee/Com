# LightAP Com 模块 — Transport 矩阵

> **文档版本**: 2.0  
> **最后更新**: 2026-03-02  
> **AUTOSAR 标准**: AP R25-11  
> **维护者**: LightAP Team

---

## 实现状态总览

| Transport | 协议 | 范围 | 状态 | 优先级 |
|-----------|------|------|------|--------|
| **CoreIPC** | 共享内存 IPC | 进程间 (同 ECU) | ✅ **已实现** | 100 |
| **DDS** | eProsima Fast-DDS | 跨 ECU / 跨网络 | ✅ **已实现** | 80 |
| **SOME/IP** | 车载 SOME/IP 协议 | 跨 ECU | ⚠️ **待实现** | 60 |
| **Socket** | Unix/TCP Socket | 进程间 | ⚠️ **待实现** | 40 |
| **D-Bus** | sd-bus 系统总线 | 进程间 (同 ECU) | ⚠️ **待实现** | 20 |

---

## 快速决策指南

```
需要跨 ECU 通信?
├─ YES → 使用 DDS (eProsima Fast-DDS)
│        适用: 局域网/广域网、AUTOSAR AP、动态发现、QoS 配置
│
└─ NO (同 ECU 进程间)
   └─ 使用 CoreIPC (共享内存)
      适用: 极致性能、零拷贝、实时系统、传感器数据流

                         [ 待实现 ]
需要 AUTOSAR CP 互通?   → SOME/IP ⚠️
需要 D-Bus 系统集成?    → D-Bus  ⚠️
需要轻量 Socket 回退?   → Socket ⚠️
```

---

## 1. CoreIPC Binding ✅

**源码**: `source/binding/coreipc/`  
**底层**: `lap::core::ipc` 共享内存 + POSIX memfd + seqlock  

### 架构图

```
┌─────────────────────────────────────────────────────────┐
│                    应用层 (Skeleton / Proxy)              │
└──────────────────────────┬──────────────────────────────┘
                           │ ITransportBinding NVI
┌──────────────────────────▼──────────────────────────────┐
│                   CoreIPCBinding                         │
│  ┌──────────────────┐   ┌──────────────────────────┐   │
│  │ CCoreIPCService  │   │ CCoreIPCEventManager     │   │
│  │ Manager          │   │ CCoreIPCMethodManager    │   │
│  └────────┬─────────┘   └─────────────┬────────────┘   │
│           │                            │                 │
│  ┌────────▼────────────────────────────▼────────────┐   │
│  │       CRegistryDispatcher (服务注册表)             │   │
│  │       固定槽位 O(1) — seqlock 无锁并发             │   │
│  └────────────────────────┬─────────────────────────┘   │
└───────────────────────────┼─────────────────────────────┘
                            │ POSIX 共享内存 (memfd)
                    ┌───────▼────────┐
                    │  ChunkPool     │
                    │  MemPool 零拷贝 │
                    └────────────────┘
```

### 关键特性

| 特性 | 说明 |
|------|------|
| **零拷贝** | 共享内存直接访问，无序列化开销 |
| **服务发现** | 固定槽位映射 `service_id & 0x03FF`，< 500ns |
| **延迟** | < 5µs (P99, 64B payload) |
| **吞吐量** | > 10 GB/s |
| **CPU 占用** | < 0.5% |
| **无守护进程** | 进程自管理，无单点故障 |
| **FuSa 就绪** | QM/ASIL-D 槽位物理隔离 |

### 核心组件

| 文件 | 组件 | 功能 |
|------|------|------|
| `CoreIPCBinding.hpp/cpp` | `CoreIPCBinding` | 主 Facade |
| `CCoreIPCServiceManager` | 服务 offer/find | 固定槽位注册 |
| `CCoreIPCEventManager` | 事件发布/订阅 | Publisher/Subscriber 管理 |
| `CCoreIPCMethodManager` | 方法调用 | Request-Reply MethodChannel |
| `CCoreIPCCodec` | 编解码 | Binary 序列化 |

---

## 2. DDS Binding ✅

**源码**: `source/binding/dds/`  
**底层**: eProsima Fast-DDS 3.x + FastCDR 2.2  

### 架构图

```
┌─────────────────────────────────────────────────────────┐
│                    应用层 (Skeleton / Proxy)              │
└──────────────────────────┬──────────────────────────────┘
                           │ ITransportBinding NVI
┌──────────────────────────▼──────────────────────────────┐
│                   DdsBinding (Facade)                    │
│  ┌──────────────────┐   ┌──────────────────────────┐   │
│  │ CDdsServiceManager│  │ CDdsEventManager         │   │
│  │ (offer/find)      │  │ CDdsMethodManager        │   │
│  └────────┬──────────┘  └──────────┬───────────────┘   │
│           │                         │                    │
│  ┌────────▼─────────────────────────▼───────────────┐  │
│  │  DomainParticipant / Publisher / Subscriber       │  │
│  │  Topic / DataWriter / DataReader                  │  │
│  │  CDdsTypeRegistry → IDdsTypeAdapter (per service) │  │
│  └──────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
                           │
              ┌────────────┼──────────────┐
              │ Fast-DDS   │              │
         ┌────▼─────┐ ┌───▼─────┐  ┌────▼─────┐
         │  SHM     │ │  UDP    │  │ Discovery│
         │ (intra)  │ │ (cross) │  │ Server   │
         └──────────┘ └─────────┘  └──────────┘
```

### 关键特性

| 特性 | 说明 |
|------|------|
| **跨 ECU** | UDP/TCP + FastDDS Discovery Server |
| **强类型适配** | `IDdsTypeAdapter` + `CDdsTypeRegistry` 进行类型桥接 |
| **CDR 序列化** | fastddsgen 自动生成，无手动序列化 |
| **Push 发现** | `DdsDiscoveryListener` 推送服务变更通知 |
| **Discovery Server** | 支持集中式 DS + 故障退化 (PDP/EDP fallback) + 自动重连 |
| **延迟** | SHM < 10µs，UDP < 30µs (P99) |
| **吞吐量** | SHM > 1 GB/s，UDP ~900 MB/s |
| **DDS Security** | 基于 AUTOSAR TR_DDSS (证书 + 权限 + 治理) |

### 核心组件

| 文件 | 组件 | 功能 |
|------|------|------|
| `DdsBinding.hpp/cpp` | `DdsBinding` | Facade + 生命周期 |
| `CDdsServiceManager` | 服务管理 | OfferService / FindService |
| `CDdsEventManager` | 事件管理 | DataWriter / DataReader |
| `CDdsMethodManager` | 方法管理 | Request/Reply Topic |
| `CDdsTypeRegistry` | 类型注册 | per-service IDdsTypeAdapter |
| `CDdsCodec` | Topic 工厂 | 命名 + Entity 懒创建 |
| `CDdsDiscoveryServerMonitor` | DS 监控 | 健康检查 + 退化 + 重连 |
| `DdsDiscoveryListener` | Push 发现 | on_data_writer_discovery |

### AUTOSAR → DDS QoS 映射

| AUTOSAR 概念 | DDS QoS | 说明 |
|-------------|---------|------|
| 可靠性 | RELIABILITY | RELIABLE / BEST_EFFORT |
| 持久化 | DURABILITY | VOLATILE / TRANSIENT_LOCAL |
| 历史深度 | HISTORY | KEEP_LAST(N) |
| 截止时间 | DEADLINE | 更新周期约束 |

---

## 3. SOME/IP Binding ⚠️ 待实现

**源码骨架**: `source/binding/someip/`  
**计划底层**: vsomeip 3.x  
**状态**: 仅有框架文件，核心逻辑待实现

### 主要工作项

- [ ] `SomeIpBinding::Initialize()` — vsomeip Application 初始化
- [ ] `SomeIpBinding::OfferService()` — SOME/IP SD 广播
- [ ] `SomeIpBinding::FindService()` — SD 发现订阅
- [ ] Method 调用 (request_id 分配 + 超时)
- [ ] Event 发布/订阅 (event group)
- [ ] Field (getter/setter/notifier)
- [ ] SOME/IP 序列化 (BOM + UTF-8 + NUL wire format)
- [ ] 与 BindingManager 集成测试

---

## 4. Socket Binding ⚠️ 待实现

**源码骨架**: `source/binding/socket/`  
**计划底层**: POSIX Unix Domain Socket / TCP  
**状态**: 仅有框架文件，核心逻辑待实现

### 主要工作项

- [ ] Unix Domain Socket 连接管理
- [ ] 帧协议设计 (长度前缀)
- [ ] Method request-reply 通道
- [ ] Event 广播机制
- [ ] Binary 序列化集成

---

## 5. D-Bus Binding ⚠️ 待实现

**源码骨架**: `source/binding/dbus/`  
**计划底层**: sd-bus (systemd 内置)  
**状态**: 仅有框架文件，核心逻辑待实现

### 主要工作项

- [ ] sd-bus 连接池管理
- [ ] Method (D-Bus method call + reply)
- [ ] Event (D-Bus signal)
- [ ] Field (D-Bus property)
- [ ] `org.freedesktop.DBus.Introspectable` 集成

---

## 性能对比

| 指标 | CoreIPC | DDS (SHM) | DDS (UDP) | SOME/IP (计划) |
|------|---------|-----------|-----------|---------------|
| **延迟 (64B)** | < 5µs | < 10µs | < 30µs | ~50µs |
| **延迟 (1MB)** | < 20µs | < 100µs | < 5ms | ~10ms |
| **吞吐量** | > 10 GB/s | > 1 GB/s | ~900 MB/s | ~500 MB/s |
| **CPU 占用** | < 0.5% | ~1% | ~4% | ~5% |
| **零拷贝** | ✅ | ✅ (SHM) | ❌ | ❌ |
| **跨 ECU** | ❌ | ✅ | ✅ | ✅ |
| **AUTOSAR 符合** | ✅ | ✅ | ✅ | ✅ (计划) |

---

## 场景选择建议

| 场景 | 推荐 Binding | 说明 |
|------|-------------|------|
| 同 ECU 高性能 (传感器、融合) | **CoreIPC** | 零拷贝，< 5µs |
| 同 ECU + 跨 ECU 通信 | **CoreIPC + DDS** | 自动优先 CoreIPC |
| 跨 ECU 纯网络 | **DDS** | FastDDS UDP/TCP |
| 车载全栈 AP+CP | **CoreIPC + SOME/IP** ⚠️ | SOME/IP 待实现 |
| 系统 D-Bus 集成 | **D-Bus** ⚠️ | D-Bus 待实现 |

---

## Binding 注册与选择

```cpp
// Binding 优先级由 BindingManager 自动排序
auto& mgr = BindingManager::GetInstance();

BindingConfig cfg;
cfg.name     = "coreipc";
cfg.priority = BindingPriority::kCoreIpc;  // 100
cfg.enabled  = true;
mgr.RegisterBinding(cfg, MakeShared<CoreIPCBinding>());

cfg.name     = "dds";
cfg.priority = BindingPriority::kDds;       // 80
mgr.RegisterBinding(cfg, MakeShared<DdsBinding>());

// FindService 时自动使用优先级最高的可用 Binding
// 静态配置 (YAML) 可强制指定 Binding
```

---

*本文档基于 `source/binding/` 实际代码状态编写。⚠️ 标记项为规划中功能，仅有骨架文件尚无实现。*
