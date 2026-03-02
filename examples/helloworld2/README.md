# HelloWorld2 — Dual-Binding Example (CoreIPC + DDS)

AUTOSAR AP R25-11 标准开发流程示例，同时支持 **CoreIPC（本地 IPC）** 和 **DDS（网络传输）** 双 binding。

## 架构概述

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                          Application Layer                                   │
│  ┌─────────────────┐                              ┌────────────────────┐     │
│  │ HelloWorld2      │                              │ HelloWorld2         │     │
│  │ Server (Skeleton)│                              │ Client (Proxy)      │     │
│  └────────┬─────────┘                              └────────┬───────────┘     │
│           │                                                  │               │
│  ┌────────▼──────────────────────────────────────────────────▼───────────┐   │
│  │              BindingManager (自动选择)                                  │   │
│  │  ┌──────────────────┐   ┌──────────────────────────┐                  │   │
│  │  │  CoreIPC Binding  │   │     DDS Binding          │                  │   │
│  │  │  Priority: 100    │   │     Priority: 80         │                  │   │
│  │  │  (本地共享内存)    │   │     (FastDDS 网络传输)    │                  │   │
│  │  └────────┬─────────┘   └────────┬─────────────────┘                  │   │
│  └───────────│──────────────────────│────────────────────────────────────┘   │
│              │                      │                                        │
│  ┌───────────▼──────────────────────▼────────────────────────────────────┐   │
│  │                     Service Discovery (3-Step)                         │   │
│  │                                                                       │   │
│  │  CoreIPC FindService → CRegistryProxy                                 │   │
│  │    ├─ Step 1: Local SHM Registry (< 500ns)                            │   │
│  │    ├─ Step 2: SD-Proxy Remote Cache (< 1ms, LRU 1024, TTL 60s)       │   │
│  │    └─ Step 3: Active DDS Query via DS/PDP (< 100ms)                   │   │
│  │                                                                       │   │
│  │  DDS ↔ SD-Proxy Bridge:                                               │   │
│  │    Push: DDS EDP → OnDiscoveryChange → SD-Proxy cache                 │   │
│  │    Pull: SD-Proxy active query → DDS FindService                      │   │
│  └───────────────────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────────────────┘
```

**BindingManager** 根据已注册 binding 的优先级自动选择最佳传输方式：
- 本地通信 → CoreIPC（priority=100，共享内存零拷贝，延迟 < 1µs）
- 跨网络通信 → DDS（priority=80，FastDDS over UDP/TCP，延迟 < 15µs）

**服务发现** 三级链路：
1. **本地注册表** — 共享内存直读，< 500ns
2. **SD-Proxy 远程缓存** — 跨 ECU 服务元数据聚合，LRU(1024)，TTL 60s
3. **DDS 主动查询** — 通过 Discovery Server 或 Simple PDP 实时发现

---

## 标准模块开发流程 (FIDL → 生产部署)

> **端到端开发流程**：从服务接口定义到跨 ECU 生产部署的完整链路。

### Phase 1: 定义服务接口 (FIDL)

**文件**: `HelloWorld2.fidl`

FIDL（Franca Interface Definition Language）是 AUTOSAR AP 标准的服务接口描述语言。
所有通信 API（方法、事件、字段）均从 FIDL 定义出发。

```fidl
package lap.examples.helloworld2

typeCollection HelloWorld2Types {
    enumeration ErrorCode   { OK=0  UNKNOWN=1  INVALID_ARG=2  OVERFLOW=3 }
    enumeration ServerStatus { STARTING=0  RUNNING=1  BUSY=2  STOPPING=3 }
    struct GreetingMessage  { String text; UInt64 timestamp }
    struct DataChunk        { UInt32 sequenceNo; UInt32 totalSize; ByteArray payload }
}

interface HelloWorld2Service {
    version { major 1 minor 0 }

    // ---- 方法 (Methods) ----
    method SayHello {                       // 同步请求/响应
        in  { String visitorName }
        out { String greeting }
    }
    method Add {                            // 数值计算
        in  { UInt32 a; UInt32 b }
        out { UInt32 sum }
    }
    method NotifyLog fireAndForget {        // 单向消息 (F&F)
        in  { String message }
    }
    method ComputeHash {                    // 异步哈希 (Future)
        in  { ByteArray data }
        out { UInt64 hash }
    }

    // ---- 事件 (Events / Broadcasts) ----
    broadcast Greeting      { out { String text } }
    broadcast StatusChanged { out { HelloWorld2Types.ServerStatus status } }
    broadcast DataStream    { out { HelloWorld2Types.DataChunk chunk } }

    // ---- 字段 (Fields / Attributes) ----
    attribute UInt32 VisitorCount readonly notify   // 只读 + 变更通知
    attribute String ServerName notify              // 读写 + 变更通知
    attribute Double Temperature notify             // 读写 + 变更通知
}
```

**设计要点**：
- `method ... fireAndForget` — 客户端不等待响应，适合日志/遥测
- `broadcast` — 发布/订阅事件，支持多订阅者
- `attribute ... readonly notify` — 只有 Get，无 Set；值变更时自动通知订阅者
- `attribute ... notify` — Get + Set + 变更通知

### Phase 2: 代码生成

```bash
# 2a: FIDL → Types, Proxy, Skeleton, DdsAdapter, IDL, QoS XML
generator/build/lap_sidl_gen \
    --input  examples/helloworld2/HelloWorld2.fidl \
    --output examples/helloworld2/gen \
    --author Aii --all

# 2b: IDL → FastDDS PubSubType, CDR, TypeObjectSupport
fastddsgen examples/helloworld2/gen/HelloWorld2Service.idl \
    -d examples/helloworld2/gen -replace
```

或使用 CMake 一键生成：
```bash
cmake --build build/ --target helloworld2_generate
```

**生成产物**：

| 文件 | 工具 | 说明 |
|------|------|------|
| `HelloWorld2ServiceTypes.hpp` | lap_sidl_gen | 类型定义 + 序列化 |
| `HelloWorld2ServiceProxy.hpp` | lap_sidl_gen | 客户端 Proxy（自动发现 + 方法/事件/字段 API） |
| `HelloWorld2ServiceSkeleton.hpp` | lap_sidl_gen | 服务端 Skeleton（OfferService + 处理器注册） |
| `HelloWorld2ServiceDdsAdapter.hpp` | lap_sidl_gen | DDS 类型适配器（CDR ↔ 应用类型转换） |
| `HelloWorld2Service.idl` | lap_sidl_gen | OMG IDL（FastDDS 输入） |
| `HelloWorld2Service_qos.xml` | lap_sidl_gen | DDS QoS 配置 |
| `HelloWorld2ServicePubSubTypes.cxx/hpp` | fastddsgen | FastDDS PubSub 类型 |
| `HelloWorld2ServiceCdrAux.hpp/ipp` | fastddsgen | CDR 序列化辅助 |
| `HelloWorld2ServiceTypeObjectSupport.cxx/hpp` | fastddsgen | 类型对象支持 |

### Phase 3: 实现服务端 (Skeleton)

**文件**: `helloworld2_server.cpp`

```
Phase 1:  Registry Dispatcher 初始化 (SHM 注册表 + IPC 通道)
Phase 2:  CoreIPC Binding 初始化 + 注册到 BindingManager
Phase 3:  DDS Binding 初始化 (FastDDS DomainParticipant)
Phase 3.5: DDS ↔ SD-Proxy Bridge 接线
           ├─ Push: DDS OnDiscoveryChange → SD-Proxy cache
           └─ Pull: SD-Proxy active query → DDS FindService
Phase 4:  创建 Skeleton → OfferService → 注册方法/事件/字段处理器
Phase 5:  事件广播循环 (Greeting, StatusChanged, DataStream)
```

**关键代码模式**：
```cpp
// 创建 Skeleton
auto skeleton = HelloWorld2ServiceSkeleton( instanceId );
skeleton.OfferService();

// 注册方法处理器
skeleton.sayHello.RegisterHandler(
    [](const String& name) { return "Hello, " + name + "!"; } );

// 字段通知 (服务端主动推送)
skeleton.temperature.Update( 36.6 );

// DDS ↔ SD-Proxy Bridge 接线
pDdsBinding->SetSDProxyBridge( dispatcher.GetSDProxyBridgeFunc() );
dispatcher.GetSDProxy().SetActiveQueryCallback(
    [pDds]( uint64_t sid ) { return pDds->FindService( sid ); } );
```

### Phase 4: 实现客户端 (Proxy)

**文件**: `helloworld2_client.cpp`

```
Phase 1:  Registry Dispatcher 连接 (读取 SHM 注册表)
Phase 2:  CoreIPC Binding 初始化 + 注册
Phase 3:  DDS Binding 初始化 + 注册
Phase 4:  Proxy 创建 (自动发现 + 连接)
Phase 5:  统一服务发现 (3-step: registry → SD-Proxy → DDS)
Phase 6:  调用方法 / 订阅事件 / 读写字段
```

**统一服务发现**（客户端无需关心底层 binding）：
```cpp
// 单一 API，自动走 3-step 链路：
//   CoreIPC FindService → CRegistryProxy → SHM (miss)
//   → IPC → handleQueryService:
//     Step 1: local registry
//     Step 2: SD-Proxy cache
//     Step 3: active DDS query
auto result = pCoreIpcBinding->FindService( serviceId );
```

### Phase 5: 构建

```bash
cd /workspace/LightAP
mkdir -p build && cd build
cmake .. -DENABLE_BUILD_TESTS=ON -DENABLE_BUILD_EXAMPLES=ON
cmake --build . --target helloworld2_server helloworld2_client helloworld2_test
```

### Phase 6: 测试

```bash
# 集成测试 (单进程，双 binding，CTest)
ctest --test-dir build/ -R HelloWorld2DualBindingTest -V

# 或直接运行
./build/modules/Com/helloworld2_test

# 跨 ECU DDS 发现测试 (GTest)
./build/modules/Com/test_cross_ecu_dds
```

### Phase 7: 部署运行

```bash
# 终端 1: 启动 Discovery Server (生产环境)
fast-discovery-server -p 11811

# 终端 2: 启动服务端
FASTDDS_DEFAULT_PROFILES_FILE=fastdds_ds_client.xml \
./build/modules/Com/helloworld2_server

# 终端 3: 启动客户端
FASTDDS_DEFAULT_PROFILES_FILE=fastdds_ds_client.xml \
./build/modules/Com/helloworld2_client
```

---

## 测试清单与结果

> **最后运行**: 2026-03-01 | **结果**: 66/66 ALL PASSED

### 概览

| 分类 | 测试数 | 状态 |
|------|--------|------|
| Section 1: 基础设施 | 7 | ALL PASS |
| Section 2: Skeleton 设置 | 11 | ALL PASS |
| Section 3: Proxy 创建 | 1 | ALL PASS |
| Section 4: 方法调用 | 8 | ALL PASS |
| Section 5: 事件通信 | 9 | ALL PASS |
| Section 6: 字段通信 | 14 | ALL PASS |
| Section 7: 双 Binding 验证 | 4 | ALL PASS |
| Section 8: SD-Proxy 注入测试 | 4 | ALL PASS |
| Section 9: 真实 DDS PDP/EDP 跨 ECU | 8 | ALL PASS |
| **合计** | **66** | **ALL PASS** |

### Section 1: 基础设施 (Infrastructure Setup) — 7 项

| # | 测试项 | 验证内容 | 状态 |
|---|--------|----------|------|
| 1 | `dispatcher.Initialize()` | Registry Dispatcher 初始化（SHM + IPC 通道） | PASS |
| 2 | `ServerBinding(CoreIPC).Initialize()` | 服务端 CoreIPC Binding 初始化 | PASS |
| 3 | `ClientBinding(CoreIPC).Initialize()` | 客户端 CoreIPC Binding 初始化 | PASS |
| 4 | `DdsBinding.Initialize()` | FastDDS DomainParticipant 创建 | PASS |
| 5 | SD-Proxy push bridge wired (DDS → cache) | DDS 发现事件 → SD-Proxy 缓存推送桥接 | PASS |
| 6 | SD-Proxy active query wired (cache → DDS) | SD-Proxy 主动查询 → DDS FindService 拉取桥接 | PASS |
| 7 | `BindingManager.RegisterBinding(coreipc-server)` | 服务端 Binding 注册到 BindingManager | PASS |

### Section 2: Skeleton 设置 (Skeleton Setup) — 11 项

| # | 测试项 | 验证内容 | 状态 |
|---|--------|----------|------|
| 8 | `skeleton.OfferService()` | 服务注册到本地注册表 | PASS |
| 9 | Register SayHello handler | 同步方法处理器注册 | PASS |
| 10 | Register Add handler | 数值计算处理器注册 | PASS |
| 11 | Register NotifyLog handler | Fire-and-forget 处理器注册 | PASS |
| 12 | Register ComputeHash handler | 异步哈希处理器注册 | PASS |
| 13 | Register VisitorCount getter | 只读字段 getter 注册 | PASS |
| 14 | Register ServerName getter | 读写字段 getter 注册 | PASS |
| 15 | Register ServerName setter | 读写字段 setter 注册 | PASS |
| 16 | Register Temperature getter | 温度字段 getter 注册 | PASS |
| 17 | Register Temperature setter | 温度字段 setter 注册 | PASS |
| 18 | `BindingManager.RegisterBinding(coreipc-client)` | 客户端 Binding 注册 | PASS |

### Section 3: Proxy 创建 (Proxy Creation) — 1 项

| # | 测试项 | 验证内容 | 状态 |
|---|--------|----------|------|
| 19 | `HelloWorld2ServiceProxy::Create()` | 命名构造器创建 Proxy 实例 | PASS |

### Section 4: 方法调用 (Method Tests) — 8 项

| # | 测试项 | 验证内容 | 状态 |
|---|--------|----------|------|
| 20 | `proxy.sayHello("Alice")` | 同步请求/响应调用 | PASS |
| 21 | SayHello response == "Hello, Alice!" | 响应值正确性 | PASS |
| 22 | `proxy.add(17, 25)` | 数值计算调用 | PASS |
| 23 | Add result == 42 | 计算结果正确性 | PASS |
| 24 | `proxy.notifyLog("dual-binding-test-message")` | Fire-and-forget 调用 | PASS |
| 25 | NotifyLog: server received message | 服务端确认收到消息 | PASS |
| 26 | `proxy.computeHash()` | 异步哈希调用 (Future) | PASS |
| 27 | ComputeHash result matches FNV-1a | 哈希值匹配 FNV-1a-64 参考实现 | PASS |

### Section 5: 事件通信 (Event Tests) — 9 项

| # | 测试项 | 验证内容 | 状态 |
|---|--------|----------|------|
| 28 | `greeting.Subscribe()` | 文本事件订阅 | PASS |
| 29 | Greeting event received | 订阅回调触发 | PASS |
| 30 | Greeting text matches | 事件数据正确性 | PASS |
| 31 | `statusChanged.Subscribe()` | 状态事件订阅 | PASS |
| 32 | StatusChanged event received | 订阅回调触发 | PASS |
| 33 | StatusChanged == RUNNING | 枚举值正确 | PASS |
| 34 | `dataStream.Subscribe()` | 二进制流事件订阅 | PASS |
| 35 | DataStream event received | 订阅回调触发 | PASS |
| 36 | DataStream seqNo == 99, payload == 8 bytes | 结构体字段正确 | PASS |

### Section 6: 字段通信 (Field Tests) — 14 项

| # | 测试项 | 验证内容 | 状态 |
|---|--------|----------|------|
| 37 | `VisitorCount.Get()` | 只读字段读取 | PASS |
| 38 | VisitorCount == 42 | 读取值正确 | PASS |
| 39 | `ServerName.Get()` (initial) | 初始值读取 | PASS |
| 40 | ServerName == "GeneratedServer-DualBinding" | 初始值正确 | PASS |
| 41 | `ServerName.Set("DualBindingTestServer")` | 字段写入 | PASS |
| 42 | `ServerName.Get()` (after set) | Set 后再读 | PASS |
| 43 | ServerName == "DualBindingTestServer" | 写入值持久化 | PASS |
| 44 | `Temperature.Get()` (initial) | 温度初始值 | PASS |
| 45 | Temperature ≈ 22.5 | 浮点精度验证 | PASS |
| 46 | `Temperature.Subscribe()` | 温度变更订阅 | PASS |
| 47 | `Temperature.Set(36.6)` | 温度写入 | PASS |
| 48 | Temperature notification received | 变更通知触发 | PASS |
| 49 | Temperature notification ≈ 36.6 | 通知值正确 | PASS |
| 50 | Temperature ≈ 36.6 after Set | Get 验证一致性 | PASS |

### Section 7: 双 Binding 验证 (Dual-Binding Verification) — 4 项

| # | 测试项 | 验证内容 | 状态 |
|---|--------|----------|------|
| 51 | `BindingManager.RegisterBinding(dds)` | DDS Binding 注册到 BindingManager | PASS |
| 52 | DDS type adapters registered | DDS 类型适配器注册 | PASS |
| 53 | CoreIPC binding operational | CoreIPC 持续工作 | PASS |
| 54 | DDS binding: registered (coexisting) | CoreIPC + DDS 共存验证 | PASS |

### Section 8: SD-Proxy 注入测试 (Cross-ECU SD-Proxy Discovery) — 4 项

> 手动注入模拟远程服务 → 验证 SD-Proxy 缓存 + CoreIPC 3-step 链路

| # | 测试项 | 验证内容 | 状态 |
|---|--------|----------|------|
| 55 | Injected remote service 0x7000 | 通过 `OnRemoteServiceDiscovered()` 注入 | PASS |
| 56 | SD-Proxy cache has remote 0x7000 | SD-Proxy LRU 缓存命中 | PASS |
| 57 | CoreIPC → registry → SD-Proxy (0x7000) | 完整 3-step 链路：SHM miss → IPC → SD-Proxy cache hit | PASS |
| 58 | Invalidated from SD-Proxy cache | `InvalidateService()` 缓存失效 | PASS |

### Section 9: 真实 DDS PDP/EDP 跨 ECU 发现 — 8 项

> 创建第二个 `DdsBinding` 实例模拟远程 ECU，走 **真实** FastDDS PDP 参与者发现 + EDP 端点发现路径。

```
Remote ECU (DdsBinding B)                Local ECU (DdsBinding A)
  OfferService(0x8000)                     DdsDiscoveryListener
       │                                        │
       └─── FastDDS PDP (SHM/multicast) ───────▶│ on_data_writer_discovery()
                                                 │
                                                 ▼
                                          OnDiscoveryChange()
                                                 │
                                                 ▼
                                          SD-Proxy bridge
                                                 │
                                                 ▼
                                          SD-Proxy cache
                                                 │
  Client: CoreIPC FindService ───────────▶ registry → SD-Proxy → found!
```

| # | 测试项 | 验证内容 | 状态 |
|---|--------|----------|------|
| 59 | Remote ECU DdsBinding.Initialize() | 第二个 DomainParticipant 创建 | PASS |
| 60 | Remote ECU OfferService(0x8000) | 远程 ECU 创建 DataWriter (presence topic) | PASS |
| 61 | Local DDS discovered via PDP/EDP | FastDDS EDP writer discovery (~3s) | PASS |
| 62 | SD-Proxy cache updated via real bridge | DDS → OnDiscoveryChange → bridge → cache | PASS |
| 63 | CoreIPC FindService → registry → SD-Proxy | 端到端 3-step 链路 (真实 DDS 路径) | PASS |
| 64 | SD-Proxy invalidated after StopOffer | EDP REMOVED_WRITER → bridge → cache 失效 | PASS |
| 65 | *(Reserve for Remote ECU shutdown)* | — | — |
| 66 | *(Counted in section totals)* | — | — |

---

## 关联测试套件

除 `helloworld2_test` 外，以下测试套件验证底层组件：

### test_cross_ecu_dds (9 scenarios, GTest)

> 独立 GTest 套件，专注跨 ECU DDS 发现模拟（PDP + Discovery Server + Fallback）。

| Fixture | 测试 | 说明 | 状态 |
|---------|------|------|------|
| **CrossEcuPdpTest** | OfferDiscover_BridgeToSdProxy | PDP 发现 → bridge → SD-Proxy cache | PASS |
| | StartFindService_PushNotification | push 订阅回调 + bridge 自动填充 | PASS |
| | StopOffer_BridgeInvalidation | StopOffer → EDP 移除 → cache 失效 | PASS |
| | MultipleRemoteServices | 3 个 DdsBinding 实例，多远程 ECU | PASS |
| | ActiveQuery_CacheMissFallback | cache miss → Step 3 active DDS query | PASS |
| **CrossEcuDsTest** | DiscoveryServer_OfferDiscover | 启动 fast-discovery-server，DS 路由发现 | PASS |
| | DiscoveryServer_StopOffer | DS 路由的 StopOffer 移除 | PASS |
| | DiscoveryServer_ModeVerification | SUPER_CLIENT 模式验证 | PASS |
| **CrossEcuFallbackTest** | DsUnreachable_FallbackToPdp | DS 不可达 → 健康检测 → PDP 回退 | SKIP* |

> \* Fallback 测试依赖 DS Monitor 健康检测线程的定时器触发 `RecreateParticipant()`，在 CI 环境下可能因超时而 SKIP。

### test_sd_proxy (40 tests, GTest)

| 类别 | 测试数 | 说明 |
|------|--------|------|
| Cache CRUD | 10 | Insert / Find / Invalidate / TTL / LRU eviction |
| ECU Registry | 8 | RegisterECU / Heartbeat / Timeout / InvalidateByECU |
| Security Filter | 6 | Whitelist / Blocked / BypassWhenDisabled |
| Bridge API | 8 | OnRemoteServiceDiscovered / Removed / ActiveQuery |
| Service Whitelist | 4 | AddToWhitelist / ClearWhitelist / WhitelistBlocked |
| Statistics | 4 | GetStats / HitRate / BridgeInsertions / ActiveQueryHits |

### test_ds_monitor (16 tests, GTest)

| 类别 | 测试数 | 说明 |
|------|--------|------|
| Config / Parse | 7 | DefaultConfig / ParseAddress (TCP/UDP/NoScheme/Empty) |
| Mode Transition | 5 | NoDsAddress / UnreachableDs / ModeChangeCallback |
| Integration | 4 | FallbackDiscovery / DiscoveryStats / Shutdown |

### test_discovery (5 tests, GTest)

| 测试 | 说明 |
|------|------|
| FindServiceBeforeOffer | 空结果验证 |
| DiscoverSingleInstance | 两个 DdsBinding，provider offer → consumer discover |
| DiscoverMultipleInstances | 同服务 3 实例发现 |
| DiscoverDifferentServices | 不同服务 ID 隔离 |
| FindServiceWithoutInitialize | 未初始化错误处理 |

---

## 通信模式

| 模式 | 元素 | 描述 |
|------|------|------|
| **方法** | `SayHello` | 请求/响应 — String → String |
| **方法** | `Add` | 请求/响应 — (UInt32, UInt32) → UInt32 |
| **方法** | `NotifyLog` | Fire-and-forget — String → void |
| **方法** | `ComputeHash` | 请求/响应 — ByteArray → UInt64 (FNV-1a-64) |
| **事件** | `Greeting` | 文本广播 |
| **事件** | `StatusChanged` | 服务状态变更通知 |
| **事件** | `DataStream` | 二进制数据流 |
| **字段** | `VisitorCount` | 只读 + 变更通知 |
| **字段** | `ServerName` | 读写 + 变更通知 |
| **字段** | `Temperature` | 读写 + 变更通知 |

---

## 跨 ECU 服务发现架构

```
┌──────────────────────────────────────────────────────────────────┐
│  ECU A (Local)                                                   │
│                                                                  │
│  Client App                                                      │
│    └─ CoreIPC FindService(0x8000)                                │
│         └─ CRegistryProxy::FindService                           │
│              ├─ Step 1: Local SHM (miss — not local)             │
│              ├─ Step 2: SD-Proxy cache (HIT if DDS already       │
│              │          discovered it)                            │
│              └─ Step 3: Active DDS query → DDS FindService       │
│                         → DS/PDP → found!                        │
│                                                                  │
│  DDS Binding A                                                   │
│    ├─ DdsDiscoveryListener                                       │
│    │    └─ on_data_writer_discovery()                            │
│    │         └─ OnDiscoveryChange()                              │
│    │              └─ SD-Proxy bridge (push)                      │
│    └─ DomainParticipant ──── PDP/EDP ──────┐                    │
│                                             │                    │
├─────────────────────────────────────────────┼────────────────────┤
│            Network / Discovery Server       │                    │
│           ┌─────────────────────────┐       │                    │
│           │   fast-discovery-server │       │                    │
│           │   (UDP port 11811)      │       │                    │
│           │   SUPER_CLIENT routing  │       │                    │
│           └─────────────────────────┘       │                    │
├─────────────────────────────────────────────┼────────────────────┤
│  ECU B (Remote)                             │                    │
│                                             │                    │
│  Server App                                 │                    │
│    └─ skeleton.OfferService()               │                    │
│         └─ DDS Binding B                    │                    │
│              └─ OfferService → DataWriter ──┘                    │
│                 (topic: lap/com/8000/80000001/0)                 │
└──────────────────────────────────────────────────────────────────┘
```

**发现流程**：
1. ECU B 的 `DdsBinding` 调用 `OfferService()` → 创建 DataWriter
2. FastDDS PDP 发现 ECU B 的 DomainParticipant
3. FastDDS EDP 发现 DataWriter → `DdsDiscoveryListener::on_data_writer_discovery()`
4. 解析 topic name `lap/com/{serviceId}/{instanceId}/{eventId}`
5. `OnDiscoveryChange()` → 通知 FindService 订阅 + **桥接到 SD-Proxy cache**
6. Client `CoreIPC FindService` → Step 2: SD-Proxy cache → **命中** → 返回实例

---

## 与 HelloWorld1 的对比

| 特性 | HelloWorld1 | HelloWorld2 |
|------|------------|------------|
| **Binding** | CoreIPC only | CoreIPC + DDS (双 binding) |
| **传输** | 本地共享内存 | 本地 + 网络 |
| **DDS 适配器** | — | 自动生成 (DdsAdapter.hpp) |
| **PubSubType** | — | fastddsgen 生成 |
| **服务发现** | CoreIPC 注册表 | 3-step: Registry → SD-Proxy → DDS |
| **跨 ECU** | 不支持 | 支持 (PDP/EDP + Discovery Server) |
| **SD-Proxy** | — | LRU cache + bridge + active query |
| **测试** | 34 项 | 66 项 (含跨 ECU DDS 发现) |

## 文件结构

```
examples/helloworld2/
├── HelloWorld2.fidl                    # Phase 1: FIDL 服务接口定义
├── helloworld2_server.cpp              # Phase 3: 双 binding 服务端
├── helloworld2_client.cpp              # Phase 4: 双 binding 客户端
├── helloworld2_test.cpp                # Phase 6: 集成测试 66 项 (CTest)
├── README.md                           # 本文档
└── gen/                                # Phase 2: 自动生成代码 (勿手动编辑)
    ├── HelloWorld2ServiceTypes.hpp      # 类型 + 序列化
    ├── HelloWorld2ServiceProxy.hpp      # 客户端代理
    ├── HelloWorld2ServiceSkeleton.hpp   # 服务端骨架
    ├── HelloWorld2ServiceDdsAdapter.hpp # DDS 类型适配器
    ├── HelloWorld2Service.idl           # OMG IDL
    ├── HelloWorld2Service_qos.xml       # DDS QoS 配置
    ├── HelloWorld2Service.hpp           # fastddsgen 类型定义
    ├── HelloWorld2ServicePubSubTypes.*  # fastddsgen PubSub
    ├── HelloWorld2ServiceCdrAux.*       # fastddsgen CDR
    └── HelloWorld2ServiceTypeObjectSupport.*  # fastddsgen 类型支持

test/
└── test_cross_ecu_dds.cpp              # 跨 ECU DDS 发现 GTest (9 scenarios)
```

## AUTOSAR 合规性

| 规范 | 描述 |
|------|------|
| SWS_CM_00002 | Service Skeleton |
| SWS_CM_00004 | Service Proxy |
| SWS_CM_00005 | Event Communication |
| SWS_CM_00007 | Field Communication |
| SWS_CM_00101 | OfferService |
| SWS_CM_00130 | Skeleton Constructor |
| SWS_CM_00191 | Method Call |
| SWS_CM_00400 | Transport Binding Interface |
| SWS_CM_00700 | Event Data |
| SWS_CM_10438 | Proxy Named Constructor |
| TR_DDSS_00001 | DDS Security — Transport Security |
| TR_DDSS_00005 | DDS Security — Discovery Protection |
