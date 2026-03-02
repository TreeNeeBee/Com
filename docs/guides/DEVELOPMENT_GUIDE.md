# LightAP Com 模块开发指南

> **文档版本**: 2.0  
> **最后更新**: 2026-03-02  
> **AUTOSAR 标准**: AP R25-11 (向前兼容 R24-11)  
> **维护者**: LightAP Team

---

## 目录

| 章节 | 内容 |
|------|------|
| [§1 开发环境准备](#1-开发环境准备) | 工具链、依赖安装 |
| [§2 标准开发流程](#2-标准开发流程) | FIDL → 生成 → 实现 → 构建 → 测试 |
| [§3 FIDL 接口定义](#3-fidl-接口定义) | 语法参考、类型系统 |
| [§4 代码生成](#4-代码生成) | lap-sidl-gen CLI 完整参考 |
| [§5 隔离生成架构 (Split Gen)](#5-隔离生成架构-split-gen) | gen_server/ + gen_client/ 目录分离 |
| [§6 服务端开发](#6-服务端开发skeleton) | Skeleton 创建、方法/事件/字段 |
| [§7 客户端开发](#7-客户端开发proxy) | Proxy 创建、服务发现、调用 |
| [§8 App Framework](#8-app-framework) | ServerApp / ClientApp 自动生成框架 |
| [§9 Binding 配置](#9-binding-配置) | 单 binding / 双 binding / 多 binding |
| [§10 CMake 集成](#10-cmake-集成) | 构建目标、依赖配置 |
| [§11 测试](#11-测试) | 单元测试、集成测试、CTest |
| [§12 示例参考](#12-示例参考) | HelloWorld / HelloWorld2 / HelloWorld3 |
| [§13 故障排除](#13-故障排除) | 常见问题 |

---

## 1. 开发环境准备

### 1.1 工具链要求

| 工具 | 最低版本 | 说明 |
|------|---------|------|
| C++ 编译器 | GCC 11+ / Clang 14+ | C++17 标准 |
| CMake | 3.20+ | 构建系统 |
| `lap-sidl-gen` | v1.0 | FIDL → C++ 代码生成器 (项目内置) |
| `fastddsgen` | 3.0+ | IDL → DDS PubSubType 生成器 (仅 DDS binding 需要) |
| eProsima Fast-DDS | 3.0+ | DDS 运行库 (仅 DDS binding 需要) |

### 1.2 构建依赖模块

```
lap_core       — 基础类型 (Result, Future, String, Mutex, SharedPtr...)
lap_log        — 日志框架
lap_com        — Com 运行时 (ProxyBase, SkeletonBase, Serialization)
```

### 1.3 可选 binding 依赖

| Binding | 库 | 说明 |
|---------|-----|------|
| CoreIPC | `lap_com_binding_coreipc` | 本地共享内存 IPC (零拷贝) |
| DDS | `lap_com_binding_dds` + Fast-DDS | 网络传输 (FastDDS SHM/UDP) |
| SOME/IP | `lap_com_binding_someip` | 车载 SOME/IP 协议 ⚠️ **待实现** |
| Socket | `lap_com_binding_socket` | Unix/TCP Socket ⚠️ **待实现** |
| D-Bus | `lap_com_binding_dbus` | sd-bus 系统总线 ⚠️ **待实现** |

### 1.4 快速构建

```bash
# 完整构建 (所有模块 + 示例 + 测试)
cd /workspace/LightAP/build
cmake .. -DENABLE_BUILD_TESTS=ON -DENABLE_BUILD_EXAMPLES=ON
cmake --build . -j$(nproc)

# 运行所有测试
ctest --output-on-failure
```

---

## 2. 标准开发流程

AUTOSAR AP R25-11 标准的服务通信开发遵循 **5 步流程**：

```
┌──────────────────────────────────────────────────────────────────┐
│  Step 1: 定义接口           .fidl 文件 (Franca IDL)              │
│                                                                    │
│  Step 2: 生成框架代码       lap-sidl-gen 隔离生成:                │
│                             --server → gen_server/                 │
│                             --client → gen_client/                 │
│                                                                    │
│  Step 3: 实现应用逻辑       Server (ServerApp) + Client (ClientApp)│
│                                                                    │
│  Step 4: 构建               CMake → 链接 binding 库 + 运行时      │
│                                                                    │
│  Step 5: 测试               CTest / GTest 集成测试                 │
└──────────────────────────────────────────────────────────────────┘
```

### 端到端命令示例

```bash
# Step 1: 编写 FIDL (手动)
vim examples/myservice/MyService.fidl

# Step 2: 隔离生成 — Server 代码 (Types + Skeleton + ServerApp + DDS)
generator/build/lap_sidl_gen \
    --input  examples/myservice/MyService.fidl \
    --output examples/myservice/gen_server \
    --author "YourName" --server --binding coreipc,dds

# Step 2: 隔离生成 — Client 代码 (Types + Proxy + ClientApp + DDS)
generator/build/lap_sidl_gen \
    --input  examples/myservice/MyService.fidl \
    --output examples/myservice/gen_client \
    --author "YourName" --client --binding coreipc,dds

# Step 3: 实现 server/client (手动)
vim examples/myservice/myservice_server.cpp
vim examples/myservice/myservice_client.cpp

# Step 4: 构建
cmake --build build/ --target myservice_server myservice_client

# Step 5: 测试
ctest --test-dir build/ -R MyServiceTest -V
```

---

## 3. FIDL 接口定义

### 3.1 基本结构

```fidl
/**
 * @file    MyService.fidl
 * @brief   服务接口定义
 */
package company.module.service

typeCollection MyTypes {
    enumeration ErrorCode {
        OK         = 0
        UNKNOWN    = 1
        INVALID    = 2
    }

    struct SensorData {
        UInt32    id
        Double    value
        UInt64    timestamp
    }
}

interface MyService {
    version { major 1 minor 0 }

    // 方法 (请求/响应)
    method GetData {
        in  { UInt32 sensorId }
        out { MyTypes.SensorData data }
    }

    // 方法 (fire-and-forget, 无响应)
    method LogMessage fireAndForget {
        in  { String message }
    }

    // 事件 (广播)
    broadcast DataUpdated {
        out { MyTypes.SensorData data }
    }

    // 字段 (属性)
    attribute UInt32 SensorCount readonly notify  // 只读 + 通知
    attribute String DeviceName notify            // 读写 + 通知
}
```

### 3.2 支持的类型系统

| FIDL 类型 | C++ 映射 | 说明 |
|-----------|---------|------|
| `Bool` | `lap::core::Bool` | 布尔 |
| `UInt8` | `lap::core::UInt8` | 无符号 8 位 |
| `UInt16` | `lap::core::UInt16` | 无符号 16 位 |
| `UInt32` | `lap::core::UInt32` | 无符号 32 位 |
| `UInt64` | `lap::core::UInt64` | 无符号 64 位 |
| `Int32` | `lap::core::Int32` | 有符号 32 位 |
| `Int64` | `lap::core::Int64` | 有符号 64 位 |
| `Float` | `lap::core::Float` | 单精度浮点 |
| `Double` | `lap::core::Double` | 双精度浮点 |
| `String` | `lap::core::String` | 字符串 |
| `ByteBuffer` | `std::vector<UInt8>` | 字节缓冲 |
| `enumeration` | `enum class` | 强类型枚举 |
| `struct` | `struct` | 数据结构 |

### 3.3 通信模式

| FIDL 关键字 | 模式 | 方向 | 返回值 |
|-------------|------|------|--------|
| `method` | 请求/响应 | 双向 | `Future<OutputType>` |
| `method ... fireAndForget` | 单向消息 | 客户端→服务端 | `Result<void>` |
| `broadcast` | 事件广播 | 服务端→客户端 | — |
| `attribute ... readonly` | 只读字段 | Get 只读 | `Result<T>` |
| `attribute ... readonly notify` | 只读 + 通知 | Get + 订阅 | `Result<T>` |
| `attribute ...` | 读写字段 | Get + Set | `Result<T>` / `Result<void>` |
| `attribute ... notify` | 读写 + 通知 | Get + Set + 订阅 | `Result<T>` |

---

## 4. 代码生成

### 4.1 lap-sidl-gen

位置: `modules/Com/generator/build/lap_sidl_gen`

```
Usage: lap-sidl-gen --input <file.fidl> --output <dir> [options]
       lap-sidl-gen --input <file.fidl> --validate
       lap-sidl-gen --input <file.fidl> --hash-only
```

#### 生成模式

| 模式 | 选项 | 说明 |
|------|------|------|
| 验证 | `--validate` | 仅验证 .fidl 语法 (不生成) |
| Hash | `--hash-only` | 输出 Schema Hash (不生成) |
| 全量 | `--all` 或无选项 | 生成所有输出类型 |
| **Server** | **`--server`** | **便捷: `--types --skeleton --server-app` (+ DDS 如果 binding 包含 dds)** |
| **Client** | **`--client`** | **便捷: `--types --proxy --client-app` (+ DDS 如果 binding 包含 dds)** |
| 精细 | `--proxy` `--skeleton` `--types` 等 | 分别控制单个输出 |

#### 关键选项

| 选项 | 说明 |
|------|------|
| `--input, -i <path>` | 输入 .fidl 文件 (必需) |
| `--output, -o <dir>` | 输出目录 (必需) |
| `--server` | 生成服务端代码 (Types + Skeleton + ServerApp + DDS) |
| `--client` | 生成客户端代码 (Types + Proxy + ClientApp + DDS) |
| `--all` | 生成全部输出 (默认，如无选择性选项) |
| `--no-server-app` | 抑制 ServerApp 生成 (配合 `--all` 使用) |
| `--no-client-app` | 抑制 ClientApp 生成 (配合 `--all` 使用) |
| `--binding <layers>` | 绑定层: `coreipc` (默认), `dds`, `someip` ⚠️待实现, `dbus` ⚠️待实现 |
| `--namespace, -n <ns>` | C++ 命名空间前缀 |
| `--author <name>` | 头文件注释作者 (默认: "Aii") |
| `--service-id <id>` | 覆盖自动生成的 Service ID |
| `--schema-hash <hash>` | 注入外部 Schema Hash |
| `--com-config <path>` | QoS YAML 配置文件路径 |

#### 生成文件列表

**`--server` 生成 (gen_server/)**:

| 文件 | 用途 |
|------|------|
| `<Service>Types.hpp` | 类型定义 + ADL 序列化函数 |
| `<Service>Skeleton.hpp` | 服务端骨架 (events, methods, fields) |
| `<Service>ServerApp.hpp` | 服务端 App Framework (自动初始化) |
| `<Service>DdsAdapter.hpp` | DDS 类型适配器 (仅 DDS binding) |
| `<Service>.idl` | OMG IDL v4.2 (仅 DDS binding) |
| `<Service>_qos.xml` | DDS QoS Profile (仅 DDS binding) |
| `<Service>PubSubTypes.cxx/hpp` | fastddsgen 自动生成 (仅 DDS binding) |
| `<Service>TypeObjectSupport.cxx/hpp` | DDS TypeObject (仅 DDS binding) |
| `<Service>CdrAux.hpp/ipp` | CDR 编码辅助 (仅 DDS binding) |

**`--client` 生成 (gen_client/)**:

| 文件 | 用途 |
|------|------|
| `<Service>Types.hpp` | 类型定义 + ADL 序列化函数 (与 server 相同) |
| `<Service>Proxy.hpp` | 客户端代理 (events, methods, fields) |
| `<Service>ClientApp.hpp` | 客户端 App Framework (自动初始化) |
| `<Service>DdsAdapter.hpp` | DDS 类型适配器 (仅 DDS binding) |
| `<Service>.idl` + PubSubTypes 等 | 同 server 侧 DDS 文件 |

> 详细的生成器架构见 [`GENERATOR.md`](../architecture/GENERATOR.md)

### 4.2 fastddsgen 自动调用

**lap-sidl-gen v1.0 起, 当生成 DDS IDL 时自动调用 fastddsgen**，无需手动执行：

```bash
# 生成 .idl 后自动执行:
# fastddsgen -d <output_dir> -replace -flat-output-dir <Service>.idl
```

这意味着执行 `--server --binding dds` 或 `--client --binding dds` 后，`PubSubTypes.cxx`、`TypeObjectSupport.cxx` 等文件会自动生成到同一输出目录。

---

## 5. 隔离生成架构 (Split Gen)

### 5.1 设计理念

**传统方式** (已废弃): 所有生成代码放在单一 `gen/` 目录
```
examples/myservice/
├── gen/                    ← 混合: Proxy + Skeleton + ServerApp + ClientApp
├── myservice_server.cpp
├── myservice_client.cpp
└── MyService.fidl
```

**隔离方式** (当前标准): Server 和 Client 代码分离到不同目录
```
examples/myservice/
├── gen_server/             ← 仅: Types + Skeleton + ServerApp (+ DDS)
├── gen_client/             ← 仅: Types + Proxy + ClientApp (+ DDS)
├── myservice_server.cpp    ← 只包含 gen_server/
├── myservice_client.cpp    ← 只包含 gen_client/
├── myservice_test.cpp      ← 包含 gen_server/ + gen_client/
└── MyService.fidl
```

### 5.2 优势

| 优势 | 说明 |
|------|------|
| **构建隔离** | Server 可执行文件不包含任何 Proxy/ClientApp 代码，反之亦然 |
| **依赖最小化** | 发布 Server 时只需分发 gen_server/；Client 同理 |
| **清晰职责** | 开发者一目了然——gen_server/ 是服务端内容，gen_client/ 是客户端内容 |
| **共享安全** | Types.hpp 和 DdsAdapter.hpp 在两个目录中相同 (由 `#ifndef` 头文件保护防重复) |
| **测试灵活** | 集成测试同时包含两个目录: Skeleton from gen_server/，Proxy from gen_client/ |

### 5.3 生成命令

```bash
# Server 侧 (CoreIPC-only)
lap-sidl-gen -i MyService.fidl -o gen_server --server

# Client 侧 (CoreIPC-only)
lap-sidl-gen -i MyService.fidl -o gen_client --client

# Server 侧 (双 binding: CoreIPC + DDS)
lap-sidl-gen -i MyService.fidl -o gen_server --server --binding coreipc,dds

# Client 侧 (双 binding: CoreIPC + DDS)
lap-sidl-gen -i MyService.fidl -o gen_client --client --binding coreipc,dds

# Server 侧 (DDS-only)
lap-sidl-gen -i MyService.fidl -o gen_server --server --binding dds

# Client 侧 (DDS-only)
lap-sidl-gen -i MyService.fidl -o gen_client --client --binding dds
```

### 5.4 CMake 生成目标

每个示例提供 3 个生成目标:

```bash
# 仅生成 server 侧代码
cmake --build build/ --target myservice_generate_server

# 仅生成 client 侧代码
cmake --build build/ --target myservice_generate_client

# 同时生成 server + client (--all 便捷)
cmake --build build/ --target myservice_generate
```

### 5.5 gen_server/ vs gen_client/ 文件对比

**CoreIPC-only 示例 (helloworld)**:

| 文件 | gen_server/ | gen_client/ |
|------|:-----------:|:-----------:|
| `Types.hpp` | ✅ | ✅ |
| `Skeleton.hpp` | ✅ | ❌ |
| `Proxy.hpp` | ❌ | ✅ |
| `ServerApp.hpp` | ✅ | ❌ |
| `ClientApp.hpp` | ❌ | ✅ |

**DDS binding 示例 (helloworld2, helloworld3)**:

| 文件 | gen_server/ | gen_client/ |
|------|:-----------:|:-----------:|
| `Types.hpp` | ✅ | ✅ |
| `Skeleton.hpp` | ✅ | ❌ |
| `Proxy.hpp` | ❌ | ✅ |
| `ServerApp.hpp` | ✅ | ❌ |
| `ClientApp.hpp` | ❌ | ✅ |
| `DdsAdapter.hpp` | ✅ | ✅ |
| `.idl` / `_qos.xml` | ✅ | ✅ |
| `PubSubTypes.cxx/hpp` | ✅ | ✅ |
| `TypeObjectSupport.cxx/hpp` | ✅ | ✅ |
| `CdrAux.hpp/ipp` | ✅ | ✅ |

---

## 6. 服务端开发（Skeleton）

### 6.1 使用 App Framework (推荐)

生成的 `<Service>ServerApp.hpp` 封装了 binding 初始化、BindingManager 注册等样板代码：

```cpp
#include "MyServiceServerApp.hpp"   // from gen_server/

int main() {
    // App framework 自动处理:
    // 1. CRegistryDispatcher 初始化与启动
    // 2. CoreIPC binding 注册
    // 3. DDS binding 注册 (如果 --binding 包含 dds)
    // 4. DDS Adapter 注册
    // 5. Skeleton 创建与 OfferService
    MyServiceServerApp app;

    // 只需注册业务逻辑处理器
    app.skeleton.getData.RegisterMethodHandler(
        [](UInt32 sensorId) -> lap::core::Future<SensorData> {
            return MakeReadyFuture<SensorData>({sensorId, 25.3, GetTimestamp()});
        });

    // ... 其他 handler 注册 ...

    app.Run();  // 阻塞运行
    return 0;
}
```

### 6.2 手动初始化 (高级)

```cpp
#include "MyServiceSkeleton.hpp"        // from gen_server/
#include "CoreIPCBinding.hpp"
#include "CRegistryDispatcher.hpp"
#include "BindingManager.hpp"

// 1. 启动注册表分发器
CRegistryDispatcher dispatcher;
dispatcher.Initialize();
std::thread dispThread([&]{ dispatcher.Run(); });

// 2. 初始化 CoreIPC binding
auto pBinding = MakeShared<CoreIPCBinding>();
pBinding->Initialize();

// 3. 注册到 BindingManager
auto& bindingMgr = BindingManager::GetInstance();
BindingConfig config;
config.name     = "coreipc";
config.priority = BindingPriority::kCoreIpc;  // 100
config.enabled  = true;
bindingMgr.RegisterBinding(config, pBinding);

// 4. 创建 Skeleton
MyServiceSkeleton skeleton(
    lap::core::InstanceSpecifier("MyService/Provider"));

// 5. 注册 Handler
skeleton.getData.RegisterMethodHandler([](UInt32 id) -> Future<SensorData> {
    return MakeReadyFuture<SensorData>({id, 25.3, GetTimestamp()});
});

// 6. 提供服务
skeleton.OfferService();
```

### 6.3 Handler 注册模式

```cpp
// ---- 方法处理器 ----

// 请求/响应方法
skeleton.getData.RegisterMethodHandler(
    [](UInt32 sensorId) -> lap::core::Future<SensorData> {
        SensorData data{sensorId, 25.3, GetTimestamp()};
        return MakeReadyFuture<SensorData>(std::move(data));
    });

// Fire-and-forget 方法
skeleton.logMessage.RegisterMethodHandler(
    [](String message) {
        std::cout << "[LOG] " << message << std::endl;
    });

// ---- 字段 Handler ----

// 只读字段 (仅 getter)
skeleton.sensorCount.RegisterGetHandler(
    []() -> lap::core::Future<UInt32> {
        return MakeReadyFuture<UInt32>(g_sensorCount);
    });

// 读写字段 (getter + setter)
skeleton.deviceName.RegisterGetHandler(
    []() -> lap::core::Future<String> {
        return MakeReadyFuture<String>(g_deviceName);
    });
skeleton.deviceName.RegisterSetHandler(
    [](const String& value) -> lap::core::Future<void> {
        g_deviceName = value;
        return MakeReadyVoidFuture();
    });
```

### 6.4 发送事件与字段通知

```cpp
// 发送事件
auto sample = skeleton.dataUpdated.Allocate();
if (sample.HasValue()) {
    sample.Value()->data = {1, 25.3, GetTimestamp()};
    skeleton.dataUpdated.Send(std::move(sample).Value());
}

// 字段通知 (push 变更给订阅者)
skeleton.sensorCount.Update(newCount);
skeleton.deviceName.Update(newName);
```

---

## 7. 客户端开发（Proxy）

### 7.1 使用 App Framework (推荐)

```cpp
#include "MyServiceClientApp.hpp"   // from gen_client/

int main() {
    // App framework 自动处理:
    // 1. Binding 初始化与注册
    // 2. 服务发现
    // 3. Proxy 创建
    MyServiceClientApp app;

    // 调用方法
    auto r = app.proxy.getData(UInt32(1));
    if (r.HasValue()) {
        std::cout << "Data: " << r.Value().value << std::endl;
    }

    // 订阅事件
    app.proxy.dataUpdated.Subscribe();
    app.proxy.dataUpdated.SetReceiveHandler([&] {
        auto s = app.proxy.dataUpdated.GetNextSample();
        if (s.HasValue() && s.Value()) {
            std::cout << "Update: " << s.Value()->data.value << std::endl;
        }
    });

    // 读取字段
    auto count = app.proxy.sensorCount.Get();

    return 0;
}
```

### 7.2 手动初始化 (高级)

```cpp
#include "MyServiceProxy.hpp"   // from gen_client/
#include "CoreIPCBinding.hpp"
#include "BindingManager.hpp"

auto pBinding = MakeShared<CoreIPCBinding>();
pBinding->Initialize();

auto& bindingMgr = BindingManager::GetInstance();
BindingConfig config;
config.name     = "coreipc";
config.priority = BindingPriority::kCoreIpc;
config.enabled  = true;
bindingMgr.RegisterBinding(config, pBinding);

// 服务发现
bool found = false;
for (int i = 0; i < 30 && !found; ++i) {
    auto result = pBinding->FindService(MyServiceProxy::kServiceId);
    if (result.HasValue() && !result.Value().empty()) {
        found = true;
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

// 创建 Proxy
using HandleType = MyServiceProxy::HandleType;
HandleType handle(static_cast<InstanceIdentifierType>(
    MyServiceProxy::kServiceId & 0xFFFFU));
auto proxy = MyServiceProxy::Create(handle).Value();
```

### 7.3 调用方法

```cpp
// 请求/响应
auto r = proxy.getData(UInt32(1));
if (r.HasValue()) {
    std::cout << "Sensor data: " << r.Value().value << std::endl;
}

// Fire-and-forget
proxy.logMessage(String("Client connected"));
```

### 7.4 订阅事件

```cpp
proxy.dataUpdated.Subscribe();
proxy.dataUpdated.SetReceiveHandler([&] {
    auto sample = proxy.dataUpdated.GetNextSample();
    if (sample.HasValue() && sample.Value()) {
        std::cout << "Data update: " << sample.Value()->data.value << std::endl;
    }
});

// ... 使用完毕
proxy.dataUpdated.Unsubscribe();
```

### 7.5 读写字段

```cpp
// Get
auto r = proxy.sensorCount.Get();

// Set (读写字段)
proxy.deviceName.Set(String("NewDevice"));

// 订阅字段变更通知
proxy.deviceName.Subscribe();
proxy.deviceName.SetReceiveHandler([&] {
    auto sample = proxy.deviceName.GetNextSample();
    if (sample.HasValue() && sample.Value()) {
        std::cout << "Name changed: " << *sample.Value() << std::endl;
    }
});
```

---

## 8. App Framework

### 8.1 概述

`--server` 和 `--client` 分别生成 `<Service>ServerApp.hpp` 和 `<Service>ClientApp.hpp`，封装所有初始化样板代码。

**ServerApp 自动处理:**
- CRegistryDispatcher 初始化与后台线程
- CoreIPC binding 注册 (默认)
- DDS binding 注册 + DDS Adapter 注册 (如果 `--binding` 包含 dds)
- Skeleton 创建与 OfferService

**ClientApp 自动处理:**
- binding 初始化与注册
- 服务发现等待 (自动重试)
- Proxy 创建

### 8.2 Binding Layer 控制

App Framework 的 binding 层由 `--binding` 选项决定:

```bash
# CoreIPC-only (默认)
lap-sidl-gen -i Service.fidl -o gen_server --server

# CoreIPC + DDS
lap-sidl-gen -i Service.fidl -o gen_server --server --binding coreipc,dds

# DDS-only
lap-sidl-gen -i Service.fidl -o gen_server --server --binding dds
```

`BindingLayer` 枚举使用位掩码支持多 binding 组合:
```cpp
enum BindingLayer : UInt8 {
    kBindingCoreIpc = 0x01,
    kBindingDds     = 0x02,
    // kBindingSomeip = 0x04,   // reserved (待实现)
    // kBindingDbus   = 0x08,   // reserved (待实现)
    // kBindingSocket = 0x10,   // reserved (待实现)
};
```

---

## 9. Binding 配置

### 9.1 Binding 优先级

BindingManager 根据已注册 binding 的优先级自动选择最佳传输方式：

| 优先级 | Binding | 枚举值 | 典型场景 |
|--------|---------|--------|---------|
| 100 | CoreIPC | `kCoreIpc` | 本地零拷贝 IPC，< 1µs |
| 80 | DDS | `kDds` | 跨网络 FastDDS，< 15µs |
| 60 | SOME/IP | `kSomeip` | 车载 SOME/IP 协议 ⚠️ **待实现** |
| 40 | Socket | `kSocket` | Unix/TCP 回退方案 ⚠️ **待实现** |
| 20 | D-Bus | `kDbus` | 系统总线集成 ⚠️ **待实现** |
| 10 | Custom | `kCustom` | 自定义协议 |

### 9.2 架构选择

```
┌──────────────────────────────────────────────────────────┐
│ 场景                         │ 推荐 Binding                │
├──────────────────────────────┼──────────────────────────── ┤
│ 单 ECU，高性能               │ CoreIPC only               │
│ 单 ECU + 跨网络通信          │ CoreIPC + DDS              │
│ 车载全栈 (AP + CP)           │ CoreIPC + SOME/IP (待实现)  │
│ 开发/测试环境                 │ Socket (待实现)            │
│ 系统管理集成                  │ D-Bus (待实现)             │
│ 纯网络服务                    │ DDS-only                  │
│ 全功能部署                    │ CoreIPC + DDS + SOME/IP (待实现) │
└──────────────────────────────────────────────────────────┘
```

### 9.3 DDS 类型适配器

使用 DDS binding 时，DDS 类型适配器由 `lap-sidl-gen` 自动生成到 `gen_server/` 和 `gen_client/`：

```cpp
#include "MyServiceDdsAdapter.hpp"

// 在 DDS binding 注册之后调用 (App Framework 自动处理)
myservice::dds_adapter::RegisterMyServiceDdsAdapters(
    MyServiceSkeleton::kServiceId);
```

### 9.4 DDS Discovery Server

跨 ECU 部署推荐使用 Fast-DDS Discovery Server:

```yaml
# config/bindings.yaml
dds:
  discovery_server: "tcp://192.168.1.10:42100"
  ds_health_check_interval_ms: 5000
  ds_max_failures: 3
  ds_reconnect_interval_ms: 10000
  ds_enable_fallback: true     # DS 不可用时退化至 PDP/EDP
  ds_enable_reconnect: true    # 退化后自动恢复至 DS
```

### 9.5 双 binding 架构图

```
┌──────────────────────────────────────────────────────┐
│                  Application Layer                     │
│  ┌──────────────┐          ┌──────────────┐           │
│  │   Skeleton     │          │    Proxy       │          │
│  └──────┬────────┘          └──────┬────────┘          │
│         │                          │                    │
│  ┌──────▼──────────────────────────▼──────────────┐   │
│  │          BindingManager (自动选择最优)            │   │
│  │  ┌─────────────────┐  ┌──────────────────────┐  │   │
│  │  │ CoreIPC (100)    │  │     DDS (80)          │  │   │
│  │  │ 共享内存零拷贝    │  │     FastDDS UDP/SHM   │  │   │
│  │  └─────────────────┘  └──────────────────────┘  │   │
│  └─────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────┘
```

---

## 10. CMake 集成

### 10.1 Split Gen CMake 模板

```cmake
if( ENABLE_BUILD_EXAMPLES )

    set( MY_FIDL_FILE     ${MODULE_ROOT_DIR}/examples/myservice/MyService.fidl )
    set( MY_GEN_SERVER    ${MODULE_ROOT_DIR}/examples/myservice/gen_server )
    set( MY_GEN_CLIENT    ${MODULE_ROOT_DIR}/examples/myservice/gen_client )
    set( MY_GENERATOR     ${MODULE_ROOT_DIR}/generator/build/lap_sidl_gen )

    # ------------------------------------------------------------------
    # 代码生成目标
    # ------------------------------------------------------------------

    add_custom_target( myservice_generate_server
        COMMAND ${MY_GENERATOR}
            --input ${MY_FIDL_FILE} --output ${MY_GEN_SERVER}
            --author Aii --server --binding coreipc,dds
        COMMENT "MyService: FIDL → gen_server/ (--server)"
    )

    add_custom_target( myservice_generate_client
        COMMAND ${MY_GENERATOR}
            --input ${MY_FIDL_FILE} --output ${MY_GEN_CLIENT}
            --author Aii --client --binding coreipc,dds
        COMMENT "MyService: FIDL → gen_client/ (--client)"
    )

    add_custom_target( myservice_generate
        DEPENDS myservice_generate_server myservice_generate_client
        COMMENT "MyService: FIDL → gen_server/ + gen_client/ (--all)"
    )

    # ------------------------------------------------------------------
    # 公共配置
    # ------------------------------------------------------------------

    set( MY_COMMON_INCLUDE_DIRS
        ${MODULE_ROOT_DIR}/examples/myservice
        ${MODULE_ROOT_DIR}/source/runtime/inc
        ${MODULE_ROOT_DIR}/source/binding/coreipc/inc
        ${MODULE_ROOT_DIR}/source/binding/dds/inc
        ${MODULE_ROOT_DIR}/source/binding/manager/inc
        ${MODULE_ROOT_DIR}/source/binding/common
        ${MODULE_ROOT_DIR}/source
        ${MODULE_ROOT_DIR}/registry/inc
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )

    set( MY_LINK_LIBS
        lap_com_binding_coreipc
        lap_com_binding_dds
        lap_com  lap_core  lap_log
        ${DDS_LIBRARIES}  pthread
    )

    # DDS 编译源文件 (from gen_server/)
    set( MY_SERVER_FASTDDS
        ${MY_GEN_SERVER}/MyServicePubSubTypes.cxx
        ${MY_GEN_SERVER}/MyServiceTypeObjectSupport.cxx
    )

    # ------------------------------------------------------------------
    # Server (gen_server/ only)
    # ------------------------------------------------------------------
    add_executable( myservice_server
        ${MODULE_ROOT_DIR}/examples/myservice/myservice_server.cpp
        ${MY_SERVER_FASTDDS}
    )
    target_include_directories( myservice_server PRIVATE
        ${MY_GEN_SERVER}  ${MY_COMMON_INCLUDE_DIRS} )
    target_link_libraries( myservice_server PRIVATE ${MY_LINK_LIBS} )

    # ------------------------------------------------------------------
    # Client (gen_client/ only)
    # ------------------------------------------------------------------
    set( MY_CLIENT_FASTDDS
        ${MY_GEN_CLIENT}/MyServicePubSubTypes.cxx
        ${MY_GEN_CLIENT}/MyServiceTypeObjectSupport.cxx
    )

    add_executable( myservice_client
        ${MODULE_ROOT_DIR}/examples/myservice/myservice_client.cpp
        ${MY_CLIENT_FASTDDS}
    )
    target_include_directories( myservice_client PRIVATE
        ${MY_GEN_CLIENT}  ${MY_COMMON_INCLUDE_DIRS} )
    target_link_libraries( myservice_client PRIVATE ${MY_LINK_LIBS} )

    # ------------------------------------------------------------------
    # Integration Test (BOTH gen_server/ + gen_client/)
    # ------------------------------------------------------------------
    add_executable( myservice_test
        ${MODULE_ROOT_DIR}/examples/myservice/myservice_test.cpp
        ${MY_SERVER_FASTDDS}
    )
    target_include_directories( myservice_test PRIVATE
        ${MY_GEN_SERVER}  ${MY_GEN_CLIENT}  ${MY_COMMON_INCLUDE_DIRS} )
    target_link_libraries( myservice_test PRIVATE ${MY_LINK_LIBS} )

    if( ENABLE_BUILD_TESTS )
        add_test( NAME MyServiceTest COMMAND myservice_test )
        set_tests_properties( MyServiceTest PROPERTIES TIMEOUT 60 )
    endif()

endif()
```

### 10.2 CoreIPC-only 简化版

CoreIPC-only 不需要 DDS 相关内容:

```cmake
    # 无 FASTDDS 源文件
    add_executable( myservice_server
        myservice_server.cpp
    )
    target_include_directories( myservice_server PRIVATE
        ${MY_GEN_SERVER}  ${MY_COMMON_INCLUDE_DIRS} )
    target_link_libraries( myservice_server PRIVATE
        lap_com_binding_coreipc lap_com lap_core lap_log pthread )
```

### 10.3 链接库速查

| Binding 组合 | 链接库 |
|-------------|--------|
| CoreIPC only | `lap_com_binding_coreipc lap_com lap_core lap_log pthread` |
| CoreIPC + DDS | 上述 + `lap_com_binding_dds ${DDS_LIBRARIES}` |
| DDS only | `lap_com_binding_coreipc lap_com_binding_dds lap_com lap_core lap_log ${DDS_LIBRARIES} pthread` |

---

## 11. 测试

### 11.1 集成测试模式

推荐的单进程集成测试模式（与所有 HelloWorld 测试一致）：

```cpp
#include "MyServiceSkeleton.hpp"    // from gen_server/
#include "MyServiceProxy.hpp"       // from gen_client/

// 创建两个独立的 CoreIPC binding 实例
auto pServerBinding = MakeShared<CoreIPCBinding>();
auto pClientBinding = MakeShared<CoreIPCBinding>();

// server binding 低优先级, client binding 高优先级
bindingMgr.RegisterBinding({.name="server", .priority=kCustom}, pServerBinding);
// ... 创建 skeleton, offer, 注册 handlers ...
bindingMgr.RegisterBinding({.name="client", .priority=kCoreIpc}, pClientBinding);
// ... 创建 proxy, 测试 ...
```

### 11.2 CHECK 宏

```cpp
#define CHECK(cond, msg) do {        \
    ++g_totalTests;                    \
    if (!(cond)) {                     \
        ++g_failedTests;               \
        std::cerr << "  FAIL: " << msg << std::endl;  \
    } else {                           \
        std::cout << "  PASS: " << msg << std::endl;  \
    }                                  \
} while(0)
```

### 11.3 CTest 完整测试套件

```bash
# 运行所有测试
ctest --test-dir build/ --output-on-failure

# 运行指定标签
ctest --test-dir build/ -L "example" -V

# 运行指定测试
ctest --test-dir build/ -R HelloWorld3DdsOnlyTest -V
```

当前 CTest 测试结果 (2026-03-02):

| 测试 | 状态 | 说明 |
|------|------|------|
| RuntimeSerializationTest | ✅ PASSED | 序列化框架 |
| ProxySkeletonTest | ✅ PASSED (34/34) | Proxy/Skeleton 基础 |
| FutureThenTest | ✅ PASSED | Future::then() 链式调用 |
| CoreIPCBindingTest | ✅ PASSED | CoreIPC binding |
| **HelloWorldIPCTest** | **✅ 46/46** | **CoreIPC-only, split gen** |
| DdsBindingTest | ✅ PASSED | DDS binding |
| DdsDiscoveryTest | ✅ PASSED | DDS 发现 |
| DdsDiscoveryServerMonitorTest | ✅ PASSED | DS 监控与退化 |
| CrossEcuDdsDiscoveryTest | ✅ PASSED | 跨 ECU DDS |
| **HelloWorld2DualBindingTest** | **✅ 66/66** | **CoreIPC + DDS, split gen** |
| **HelloWorld3DdsOnlyTest** | **✅ 99/99** | **DDS-only, split gen** |
| SomeIpBindingTest | ⚠️ 待实现 | SOME/IP binding |
| DbusBindingTest | ⚠️ 待实现 | D-Bus binding |
| SocketBindingTest | ⚠️ 待实现 | Socket binding |
| test_sd_proxy | ✅ PASSED | SD-Proxy |
| PHMTest | ✅ PASSED | 平台健康管理 |

**总计: 26 个测试全部通过**

---

## 12. 示例参考

### 12.1 HelloWorld (CoreIPC-only)

**位置**: `examples/helloworld/`  
**Binding**: CoreIPC only  
**测试**: 46/46  

```
helloworld/
├── HelloWorld.fidl           # 接口定义 (4 methods, 3 broadcasts, 3 fields)
├── gen_server/               # --server 生成 (3 files)
│   ├── HelloWorldTypes.hpp
│   ├── HelloWorldSkeleton.hpp
│   └── HelloWorldServerApp.hpp
├── gen_client/               # --client 生成 (3 files)
│   ├── HelloWorldTypes.hpp
│   ├── HelloWorldProxy.hpp
│   └── HelloWorldClientApp.hpp
├── helloworld_server.cpp     # 服务端 (使用 ServerApp)
├── helloworld_client.cpp     # 客户端 (使用 ClientApp)
└── helloworld_test.cpp       # 集成测试 (46 项)
```

**CMake 目标**:
```bash
cmake --build build/ --target helloworld_server
cmake --build build/ --target helloworld_client
cmake --build build/ --target helloworld_test
cmake --build build/ --target helloworld_generate          # 重新生成 server+client
cmake --build build/ --target helloworld_generate_server   # 仅 server
cmake --build build/ --target helloworld_generate_client   # 仅 client
```

### 12.2 HelloWorld2 (双 binding: CoreIPC + DDS)

**位置**: `examples/helloworld2/`  
**Binding**: CoreIPC + DDS  
**测试**: 66/66  

```
helloworld2/
├── HelloWorld2.fidl           # 接口定义 (双 binding)
├── gen_server/                # --server --binding coreipc,dds (13 files)
│   ├── HelloWorld2ServiceTypes.hpp
│   ├── HelloWorld2ServiceSkeleton.hpp
│   ├── HelloWorld2ServiceServerApp.hpp
│   ├── HelloWorld2ServiceDdsAdapter.hpp
│   ├── HelloWorld2Service.idl
│   ├── HelloWorld2Service_qos.xml
│   ├── HelloWorld2ServicePubSubTypes.cxx/hpp
│   ├── HelloWorld2ServiceTypeObjectSupport.cxx/hpp
│   ├── HelloWorld2ServiceCdrAux.hpp/ipp
│   └── HelloWorld2Service.hpp
├── gen_client/                # --client --binding coreipc,dds (13 files)
│   ├── HelloWorld2ServiceTypes.hpp
│   ├── HelloWorld2ServiceProxy.hpp
│   ├── HelloWorld2ServiceClientApp.hpp
│   └── ... (DDS 文件同 gen_server/)
├── helloworld2_server.cpp     # 双 binding 服务端
├── helloworld2_client.cpp     # 双 binding 客户端
└── helloworld2_test.cpp       # 集成测试 (66 项)
```

**CMake 目标**:
```bash
cmake --build build/ --target helloworld2_generate         # 重新生成 server+client
cmake --build build/ --target helloworld2_generate_server  # 仅 server
cmake --build build/ --target helloworld2_generate_client  # 仅 client
```

### 12.3 HelloWorld3 (DDS-only, 完整 FIDL 覆盖)

**位置**: `examples/helloworld3/`  
**Binding**: DDS-only  
**测试**: 99/99  

```
helloworld3/
├── HelloWorld3.fidl           # 综合接口 (SensorFusionService)
│                              # 3 enums, 4 structs, 2 typedefs
│                              # 5 methods, 3 broadcasts, 5 fields
├── gen_server/                # --server --binding dds (13 files)
│   ├── SensorFusionServiceTypes.hpp
│   ├── SensorFusionServiceSkeleton.hpp
│   ├── SensorFusionServiceServerApp.hpp
│   └── ... (DDS 文件)
├── gen_client/                # --client --binding dds (13 files)
│   ├── SensorFusionServiceTypes.hpp
│   ├── SensorFusionServiceProxy.hpp
│   ├── SensorFusionServiceClientApp.hpp
│   └── ... (DDS 文件)
├── helloworld3_server.cpp     # DDS-only 服务端
├── helloworld3_client.cpp     # DDS-only 客户端
└── helloworld3_test.cpp       # 集成测试 (99 项, 全模式覆盖)
```

### 12.4 三个示例对比

| 维度 | HelloWorld | HelloWorld2 | HelloWorld3 |
|------|-----------|------------|------------|
| Binding | CoreIPC | CoreIPC + DDS | DDS-only |
| FIDL 复杂度 | 中等 | 中等 | 综合 |
| Methods | 4 | 4 | 5 |
| Broadcasts | 3 | 3 | 3 |
| Fields | 3 | 3 | 5 |
| Enums | 2 | 2 | 3 |
| Structs | 2 | 2 | 4 |
| Typedefs | 0 | 0 | 2 |
| gen_server/ 文件数 | 3 | 13 | 13 |
| gen_client/ 文件数 | 3 | 13 | 13 |
| 测试数 | 46 | 66 | 99 |
| CTest 名 | HelloWorldIPCTest | HelloWorld2DualBindingTest | HelloWorld3DdsOnlyTest |

---

## 13. 故障排除

### 13.1 常见编译错误

| 错误 | 原因 | 解决方案 |
|------|------|---------|
| `undeclared 'CoreIPCBinding'` | 缺少头文件 | 添加 `#include "CoreIPCBinding.hpp"` |
| `undefined reference to lap_com_binding_dds` | 未链接 DDS 库 | CMake 添加 `lap_com_binding_dds` |
| `PubSubTypes.cxx: No such file` | 未运行 fastddsgen | 使用 `--server`/`--client` 自动调用 |
| `redefinition of 'Types'` | 两个 gen 目录的 Types.hpp 重复包含 | 正常——`#ifndef` 保护已处理 |

### 13.2 运行时错误

| 错误 | 原因 | 解决方案 |
|------|------|---------|
| `kServiceNotAvailable` | 服务未 Offer 或 binding 不匹配 | 确保 server 先启动并 OfferService |
| `kNoBindingAvailable` | 没有注册 binding | 检查 BindingManager.RegisterBinding |
| `No matched readers` | DDS 读者未就绪 | 增加等待时间或检查 QoS |
| `Service not found in registry` | CoreIPC 注册表未初始化 | 确保 CRegistryDispatcher.Initialize() + Run() |

### 13.3 测试技巧

- **同进程测试**: 需要两个独立的 CoreIPC binding 实例 (server/client 分离)
- **DDS 匹配延迟**: 首次连接可能需要 500ms-2s，测试中适当增加等待
- **字段通知测试**: `Update()` 应在 `Subscribe()` + `SetReceiveHandler()` 之后调用
- **事件发送重试**: 事件发送可能因 reader 未匹配而丢失，建议循环发送 + 超时等待

---

## 相关文档

| 文档 | 内容 |
|------|------|
| [`GENERATOR.md`](../architecture/GENERATOR.md) | lap-sidl-gen 生成器详细架构 |
| [`COM_ARCHITECTURE.md`](../architecture/COM_ARCHITECTURE.md) | Com 模块架构总览 |
| [`BINDING_ARCHITECTURE.md`](../architecture/BINDING_ARCHITECTURE.md) | Binding 层架构设计 |
| [`BINDING_SELECTION_GUIDE.md`](BINDING_SELECTION_GUIDE.md) | Binding 选择决策树 |
| [`DDS_INTEGRATION_GUIDE.md`](DDS_INTEGRATION_GUIDE.md) | DDS 集成指南 |
| [`SERVICE_DISCOVERY_ARCHITECTURE.md`](../architecture/SERVICE_DISCOVERY_ARCHITECTURE.md) | 服务发现架构 |
| [`TRANSPORT_MATRIX.md`](../architecture/TRANSPORT_MATRIX.md) | 传输矩阵对比 |

---

*本文档基于 LightAP Com 模块 v4.0 实际源码与测试结果编写，所有命令和代码均已验证。*
