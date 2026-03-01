# HelloWorld2 — Dual-Binding Example (CoreIPC + DDS)

AUTOSAR AP R25-11 标准开发流程示例，同时支持 **CoreIPC（本地 IPC）** 和 **DDS（网络传输）** 双 binding。

## 架构概述

```
┌──────────────────────────────────────────────────────────┐
│                    Application Layer                      │
│  ┌─────────────────┐              ┌────────────────────┐ │
│  │ HelloWorld2      │              │ HelloWorld2         │ │
│  │ Server (Skeleton)│              │ Client (Proxy)      │ │
│  └────────┬─────────┘              └────────┬───────────┘ │
│           │                                  │             │
│  ┌────────▼──────────────────────────────────▼───────────┐│
│  │              BindingManager (自动选择)                  ││
│  │  ┌──────────────────┐   ┌──────────────────────────┐  ││
│  │  │  CoreIPC Binding  │   │     DDS Binding          │  ││
│  │  │  Priority: 100    │   │     Priority: 80         │  ││
│  │  │  (本地共享内存)    │   │     (FastDDS 网络传输)    │  ││
│  │  └──────────────────┘   └──────────────────────────┘  ││
│  └───────────────────────────────────────────────────────┘│
└──────────────────────────────────────────────────────────┘
```

**BindingManager** 根据已注册 binding 的优先级自动选择最佳传输方式：
- 本地通信 → CoreIPC（priority=100，共享内存零拷贝，延迟 < 1µs）
- 跨网络通信 → DDS（priority=80，FastDDS over UDP/TCP，延迟 < 15µs）

## 标准开发流程

### Step 1: 定义服务接口 (FIDL)

```fidl
// HelloWorld2.fidl
package lap.examples.helloworld2

interface HelloWorld2Service {
    version { major 1 minor 0 }
    
    method SayHello { ... }       // 请求/响应
    method Add { ... }            // 数值计算
    method NotifyLog fireAndForget { ... }  // 单向消息
    method ComputeHash { ... }    // 异步哈希
    
    broadcast Greeting { ... }    // 文本广播
    broadcast StatusChanged { ... } // 状态变更
    broadcast DataStream { ... }   // 二进制流
    
    attribute UInt32 VisitorCount readonly notify  // 只读 + 通知
    attribute String ServerName notify             // 读写 + 通知
    attribute Double Temperature notify            // 读写 + 通知
}
```

### Step 2: 生成框架代码

```bash
# Step 2a: 生成 Types, Proxy, Skeleton, DDS Adapter, IDL, QoS XML
generator/build/lap_sidl_gen \
    --input  examples/helloworld2/HelloWorld2.fidl \
    --output examples/helloworld2/gen \
    --author Aii --all

# Step 2b: 生成 DDS PubSubType (fastddsgen)
fastddsgen examples/helloworld2/gen/HelloWorld2Service.idl \
    -d examples/helloworld2/gen -replace
```

或使用 CMake 目标：
```bash
cmake --build build/ --target helloworld2_generate
```

生成文件列表：
| 文件 | 来源 | 说明 |
|------|------|------|
| `HelloWorld2ServiceTypes.hpp` | lap_sidl_gen | 类型定义 + 序列化 |
| `HelloWorld2ServiceProxy.hpp` | lap_sidl_gen | 客户端代理 |
| `HelloWorld2ServiceSkeleton.hpp` | lap_sidl_gen | 服务端骨架 |
| `HelloWorld2ServiceDdsAdapter.hpp` | lap_sidl_gen | DDS 类型适配器 |
| `HelloWorld2Service.idl` | lap_sidl_gen | OMG IDL |
| `HelloWorld2Service_qos.xml` | lap_sidl_gen | DDS QoS 配置 |
| `HelloWorld2ServicePubSubTypes.cxx/hpp` | fastddsgen | DDS PubSub 类型 |
| `HelloWorld2ServiceCdrAux.hpp/ipp` | fastddsgen | CDR 序列化辅助 |
| `HelloWorld2ServiceTypeObjectSupport.cxx/hpp` | fastddsgen | 类型对象支持 |

### Step 3: 实现应用代码

- **`helloworld2_server.cpp`** — 服务端：注册双 binding → 创建 Skeleton → 注册处理器 → OfferService → 广播事件
- **`helloworld2_client.cpp`** — 客户端：注册双 binding → 发现服务 → 创建 Proxy → 订阅事件 → 调用方法/字段
- **`helloworld2_test.cpp`** — 集成测试：单进程验证所有 API（CTest 兼容）

### Step 4: 构建

```bash
cd /workspace/LightAP
cmake --build build/ --target helloworld2_server helloworld2_client helloworld2_test
```

### Step 5: 运行

```bash
# 终端 1: 启动服务端
./build/modules/Com/helloworld2_server

# 终端 2: 启动客户端
./build/modules/Com/helloworld2_client

# 或运行集成测试
ctest --test-dir build/ -R HelloWorld2DualBindingTest -V
```

## 通信模式

| 模式 | 元素 | 描述 |
|------|------|------|
| **方法** | `SayHello` | 请求/响应 — String → String |
| **方法** | `Add` | 请求/响应 — (UInt32, UInt32) → UInt32 |
| **方法** | `NotifyLog` | Fire-and-forget — String → void |
| **方法** | `ComputeHash` | 请求/响应 — ByteArray → UInt64 |
| **事件** | `Greeting` | 文本广播 |
| **事件** | `StatusChanged` | 服务状态变更通知 |
| **事件** | `DataStream` | 二进制数据流 |
| **字段** | `VisitorCount` | 只读 + 变更通知 |
| **字段** | `ServerName` | 读写 + 变更通知 |
| **字段** | `Temperature` | 读写 + 变更通知 |

## 与 HelloWorld1 的对比

| 特性 | HelloWorld1 | HelloWorld2 |
|------|------------|------------|
| **Binding** | CoreIPC only | CoreIPC + DDS (双 binding) |
| **传输** | 本地共享内存 | 本地 + 网络 |
| **DDS 适配器** | — | 自动生成 (DdsAdapter.hpp) |
| **PubSubType** | — | fastddsgen 生成 |
| **命名空间** | `lap::com::examples` | `helloworld2` |
| **服务发现** | CoreIPC 注册表 | BindingManager 多 binding 发现 |

## 文件结构

```
examples/helloworld2/
├── HelloWorld2.fidl                    # FIDL 服务接口定义
├── helloworld2_server.cpp              # 双 binding 服务端
├── helloworld2_client.cpp              # 双 binding 客户端
├── helloworld2_test.cpp                # 集成测试 (CTest)
├── README.md                           # 本文档
└── gen/                                # 自动生成代码 (勿手动编辑)
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
