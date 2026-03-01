# LightAP Com 模块开发指南

> **文档版本**: 1.0  
> **最后更新**: 2026-03-01  
> **AUTOSAR 标准**: AP R25-11 (向前兼容 R24-11)  
> **维护者**: LightAP Team

---

## 目录

| 章节 | 内容 |
|------|------|
| [§1 开发环境准备](#1-开发环境准备) | 工具链、依赖安装 |
| [§2 标准开发流程](#2-标准开发流程) | FIDL → 生成 → 实现 → 构建 → 测试 |
| [§3 FIDL 接口定义](#3-fidl-接口定义) | 语法参考、类型系统 |
| [§4 代码生成](#4-代码生成) | lap-sidl-gen + fastddsgen |
| [§5 服务端开发](#5-服务端开发skeleton) | Skeleton 创建、方法/事件/字段 |
| [§6 客户端开发](#6-客户端开发proxy) | Proxy 创建、服务发现、调用 |
| [§7 Binding 配置](#7-binding-配置) | 单 binding / 双 binding / 多 binding |
| [§8 CMake 集成](#8-cmake-集成) | 构建目标、依赖配置 |
| [§9 测试](#9-测试) | 单元测试、集成测试、CTest |
| [§10 示例参考](#10-示例参考) | HelloWorld / HelloWorld2 |
| [§11 故障排除](#11-故障排除) | 常见问题 |

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
| SOME/IP | `lap_com_binding_someip` | 车载 SOME/IP 协议 |
| Socket | `lap_com_binding_socket` | Unix/TCP Socket |
| D-Bus | `lap_com_binding_dbus` | sd-bus 系统总线 |

---

## 2. 标准开发流程

AUTOSAR AP R25-11 标准的服务通信开发遵循 5 步流程：

```
┌─────────────────────────────────────────────────────────────────┐
│  Step 1: 定义接口          .fidl 文件 (Franca IDL)               │
│                                                                   │
│  Step 2: 生成框架代码      lap-sidl-gen → Types/Proxy/Skeleton    │
│          (+ DDS)           fastddsgen → PubSubType/CdrAux         │
│                                                                   │
│  Step 3: 实现应用逻辑      Server (Skeleton) + Client (Proxy)      │
│                                                                   │
│  Step 4: 构建              CMake → 链接 binding 库 + 运行时        │
│                                                                   │
│  Step 5: 测试              CTest / GTest 集成测试                  │
└─────────────────────────────────────────────────────────────────┘
```

### 端到端命令示例

```bash
# Step 1: 编写 FIDL (手动)
vim examples/myservice/MyService.fidl

# Step 2a: 生成 AUTOSAR API + DDS IDL
generator/build/lap_sidl_gen \
    --input  examples/myservice/MyService.fidl \
    --output examples/myservice/gen \
    --author "YourName" --all

# Step 2b: 生成 DDS TypeSupport (仅 DDS binding 需要)
fastddsgen examples/myservice/gen/MyService.idl \
    -d examples/myservice/gen -replace

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
| `ByteArray` | `std::vector<UInt8>` | 字节数组 |
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

```bash
lap-sidl-gen --input <file.fidl> --output <dir> [options]
```

**关键选项**:

| 选项 | 说明 |
|------|------|
| `--all` | 生成所有输出 (默认) |
| `--proxy` | 仅生成 Proxy 头文件 |
| `--skeleton` | 仅生成 Skeleton 头文件 |
| `--types` | 仅生成 Types 头文件 |
| `--dds-idl` | 生成 OMG IDL + QoS XML |
| `--dds-adapter` | 生成 DDS 类型适配器 |
| `--namespace <ns>` | C++ 命名空间前缀 |
| `--author <name>` | 头文件注释作者 |
| `--validate` | 仅验证语法 |
| `--hash-only` | 仅输出 Schema Hash |

**生成文件列表**:

| 文件 | 用途 |
|------|------|
| `<Service>Types.hpp` | 类型定义 + ADL 序列化函数 |
| `<Service>Proxy.hpp` | 客户端代理 (events, methods, fields) |
| `<Service>Skeleton.hpp` | 服务端骨架 (events, methods, fields) |
| `<Service>DdsAdapter.hpp` | DDS 类型适配器 (app ↔ DDS 类型转换) |
| `<Service>.idl` | OMG IDL v4.2 (供 fastddsgen 等处理) |
| `<Service>_qos.xml` | DDS QoS Profile 配置 |

> 详细的生成器架构见 [`GENERATOR.md`](architecture/GENERATOR.md)

### 4.2 fastddsgen (DDS binding 专用)

```bash
fastddsgen <Service>.idl -d <output_dir> -replace
```

**生成文件**:

| 文件 | 用途 |
|------|------|
| `<Service>.hpp` | DDS 类型定义 (fastdds namespace) |
| `<Service>PubSubTypes.cxx/hpp` | DDS TypeSupport (序列化/反序列化) |
| `<Service>CdrAux.hpp/ipp` | CDR 编码辅助 |
| `<Service>TypeObjectSupport.cxx/hpp` | DDS TypeObject 支持 |

### 4.3 CMake 生成目标

可在 CMakeLists 中定义 `add_custom_target` 实现一键重新生成：

```cmake
add_custom_target( myservice_generate
    COMMAND ${GENERATOR}
        --input  ${FIDL_FILE}
        --output ${GEN_DIR}
        --author Aii --all
    COMMAND ${FASTDDSGEN_EXECUTABLE}
        ${GEN_DIR}/MyService.idl
        -d ${GEN_DIR} -replace
    COMMENT "Regenerating MyService from FIDL"
    WORKING_DIRECTORY ${MODULE_ROOT_DIR}
)
```

使用: `cmake --build build/ --target myservice_generate`

---

## 5. 服务端开发（Skeleton）

### 5.1 基本结构

```cpp
#include "MyServiceSkeleton.hpp"         // 生成的 Skeleton 头文件
#include "CoreIPCBinding.hpp"            // CoreIPC binding
#include "DdsBinding.hpp"                // DDS binding (双 binding 时)
#include "CRegistryDispatcher.hpp"       // CoreIPC 注册表
#include "BindingManager.hpp"            // Binding 管理器

using namespace myservice::skeleton;     // 生成的 Skeleton 命名空间
```

### 5.2 初始化 Binding

**单 binding (CoreIPC)**:

```cpp
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
```

**双 binding (CoreIPC + DDS)**:

```cpp
// CoreIPC (本地高优先级)
auto pCoreIpc = MakeShared<CoreIPCBinding>();
pCoreIpc->Initialize();
{
    BindingConfig config;
    config.name     = "coreipc";
    config.priority = BindingPriority::kCoreIpc;  // 100
    config.enabled  = true;
    bindingMgr.RegisterBinding(config, pCoreIpc);
}

// DDS (网络传输)
auto pDds = MakeShared<DdsBinding>();
pDds->Initialize();
{
    BindingConfig config;
    config.name     = "dds";
    config.priority = BindingPriority::kDds;  // 80
    config.enabled  = true;
    bindingMgr.RegisterBinding(config, pDds);
}

// 注册 DDS 类型适配器 (DDS binding 必须)
#include "MyServiceDdsAdapter.hpp"
myservice::dds_adapter::RegisterMyServiceDdsAdapters(
    MyServiceSkeleton::kServiceId);
```

### 5.3 创建 Skeleton 并注册处理器

```cpp
// 创建 Skeleton 实例
MyServiceSkeleton skeleton(
    lap::core::InstanceSpecifier("MyService/Provider"));

// 辅助函数: 创建已完成的 Future
template<typename T>
lap::core::Future<T> MakeReadyFuture(T value) {
    std::promise<lap::core::Result<T>> p;
    p.set_value(lap::core::Result<T>::FromValue(std::move(value)));
    return lap::core::Future<T>(std::move(p.get_future()));
}

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

// ---- 字段处理器 ----

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

### 5.4 提供服务与发送事件

```cpp
// 提供服务 (binding-agnostic, BindingManager 自动选择)
skeleton.OfferService();

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

### 5.5 清理

```cpp
skeleton.StopOfferService();
pBinding->Shutdown();       // 或各个 binding 分别 Shutdown
dispatcher.Shutdown();
bindingMgr.Shutdown();
```

---

## 6. 客户端开发（Proxy）

### 6.1 初始化 Binding

与服务端类似，注册 binding 到 BindingManager：

```cpp
#include "MyServiceProxy.hpp"
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
```

### 6.2 服务发现

```cpp
using namespace myservice::proxy;

// 等待服务可用
bool found = false;
for (int i = 0; i < 30 && !found; ++i) {
    auto result = pBinding->FindService(MyServiceProxy::kServiceId);
    if (result.HasValue() && !result.Value().empty()) {
        found = true;
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}
```

### 6.3 创建 Proxy

```cpp
using HandleType = MyServiceProxy::HandleType;
HandleType handle(static_cast<InstanceIdentifierType>(
    MyServiceProxy::kServiceId & 0xFFFFU));

auto proxyResult = MyServiceProxy::Create(handle);
auto proxy = std::move(proxyResult).Value();
```

### 6.4 调用方法

```cpp
// 请求/响应方法
auto r = proxy.getData(UInt32(1));
if (r.HasValue()) {
    std::cout << "Sensor data: " << r.Value().value << std::endl;
}

// Fire-and-forget 方法
proxy.logMessage(String("Client connected"));
```

### 6.5 订阅事件

```cpp
proxy.dataUpdated.Subscribe();
proxy.dataUpdated.SetReceiveHandler([&] {
    auto sample = proxy.dataUpdated.GetNextSample();
    if (sample.HasValue() && sample.Value()) {
        std::cout << "Data update: " << sample.Value()->data.value << std::endl;
    }
});

// ... 使用完毕后取消订阅
proxy.dataUpdated.Unsubscribe();
```

### 6.6 读写字段

```cpp
// Get (只读 / 读写)
auto r = proxy.sensorCount.Get();
if (r.HasValue()) {
    std::cout << "Count: " << r.Value() << std::endl;
}

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

## 7. Binding 配置

### 7.1 Binding 优先级

BindingManager 根据已注册 binding 的优先级自动选择最佳传输方式：

| 优先级 | Binding | 枚举值 | 典型场景 |
|--------|---------|--------|---------|
| 100 | CoreIPC | `kCoreIpc` | 本地零拷贝 IPC，< 1µs |
| 80 | DDS | `kDds` | 跨网络 FastDDS，< 15µs |
| 60 | SOME/IP | `kSomeip` | 车载 SOME/IP 协议 |
| 40 | Socket | `kSocket` | Unix/TCP 回退方案 |
| 20 | D-Bus | `kDbus` | 系统总线集成 |
| 10 | Custom | `kCustom` | 自定义协议 |

### 7.2 架构选择

```
┌──────────────────────────────────────────────────────────┐
│ 场景                         │ 推荐 Binding 配置          │
├──────────────────────────────┼────────────────────────────┤
│ 单 ECU，高性能               │ CoreIPC only               │
│ 单 ECU + 跨网络通信          │ CoreIPC + DDS              │
│ 车载全栈 (AP + CP)           │ CoreIPC + SOME/IP          │
│ 开发/测试环境                 │ Socket (最简单)            │
│ 系统管理集成                  │ D-Bus                     │
│ 全功能部署                    │ CoreIPC + DDS + SOME/IP   │
└──────────────────────────────────────────────────────────┘
```

### 7.3 DDS 类型适配器

使用 DDS binding 时，必须注册 DDS 类型适配器（由 `lap-sidl-gen` 自动生成）：

```cpp
#include "MyServiceDdsAdapter.hpp"

// 在 DDS binding 注册之后、通信开始之前调用
myservice::dds_adapter::RegisterMyServiceDdsAdapters(
    MyServiceSkeleton::kServiceId);
```

适配器负责 **应用类型 ↔ DDS 类型** 的双向转换：
- `CreateSample()`: 应用层 struct → DDS PubSubType
- `ExtractData()`: DDS PubSubType → 应用层 struct

### 7.4 双 binding 架构

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

应用代码 (Skeleton/Proxy) 完全不依赖具体 binding 实现，
BindingManager 在运行时根据优先级选择最佳可用 binding。

---

## 8. CMake 集成

### 8.1 包含目录

```cmake
set( MY_INCLUDE_DIRS
    ${MODULE_ROOT_DIR}/examples/myservice/gen        # 生成代码
    ${MODULE_ROOT_DIR}/source/runtime/inc             # Com 运行时 API
    ${MODULE_ROOT_DIR}/source/binding/coreipc/inc     # CoreIPC binding
    ${MODULE_ROOT_DIR}/source/binding/dds/inc         # DDS binding
    ${MODULE_ROOT_DIR}/source/binding/manager/inc     # BindingManager
    ${MODULE_ROOT_DIR}/source/binding/common          # ITransportBinding
    ${MODULE_ROOT_DIR}/source                         # 顶级 source
    ${MODULE_ROOT_DIR}/registry/inc                   # Registry (CoreIPC 需要)
    ${CMAKE_CURRENT_BINARY_DIR}/include               # 构建输出
)
```

### 8.2 链接库

**CoreIPC only**:
```cmake
set( MY_LINK_LIBS
    lap_com_binding_coreipc
    lap_com
    lap_core
    lap_log
    pthread
)
```

**双 binding (CoreIPC + DDS)**:
```cmake
set( MY_LINK_LIBS
    lap_com_binding_coreipc
    lap_com_binding_dds
    lap_com
    lap_core
    lap_log
    ${DDS_LIBRARIES}
    pthread
)
```

### 8.3 DDS 编译源文件

DDS binding 需要编译 fastddsgen 生成的源文件：

```cmake
set( MY_FASTDDS_SOURCES
    ${GEN_DIR}/MyServicePubSubTypes.cxx
    ${GEN_DIR}/MyServiceTypeObjectSupport.cxx
)

add_executable( myservice_server
    ${MODULE_ROOT_DIR}/examples/myservice/myservice_server.cpp
    ${MY_FASTDDS_SOURCES}
)
```

### 8.4 完整 CMake 示例

```cmake
if( ENABLE_BUILD_EXAMPLES )
    set( MY_GEN_DIR ${MODULE_ROOT_DIR}/examples/myservice/gen )
    set( MY_GENERATOR ${MODULE_ROOT_DIR}/generator/build/lap_sidl_gen )

    find_program( FASTDDSGEN_EXECUTABLE fastddsgen )

    # 重新生成目标
    add_custom_target( myservice_generate
        COMMAND ${MY_GENERATOR}
            --input  ${MODULE_ROOT_DIR}/examples/myservice/MyService.fidl
            --output ${MY_GEN_DIR}
            --author Aii --all
        COMMAND ${FASTDDSGEN_EXECUTABLE}
            ${MY_GEN_DIR}/MyService.idl -d ${MY_GEN_DIR} -replace
        COMMENT "Regenerating MyService from FIDL"
    )

    set( MY_FASTDDS_SOURCES
        ${MY_GEN_DIR}/MyServicePubSubTypes.cxx
        ${MY_GEN_DIR}/MyServiceTypeObjectSupport.cxx
    )

    set( MY_INCLUDE_DIRS
        ${MY_GEN_DIR}
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
        lap_com lap_core lap_log
        ${DDS_LIBRARIES} pthread
    )

    # Server
    add_executable( myservice_server
        ${MODULE_ROOT_DIR}/examples/myservice/myservice_server.cpp
        ${MY_FASTDDS_SOURCES}
    )
    target_include_directories( myservice_server PRIVATE ${MY_INCLUDE_DIRS} )
    target_link_libraries( myservice_server PRIVATE ${MY_LINK_LIBS} )

    # Client
    add_executable( myservice_client
        ${MODULE_ROOT_DIR}/examples/myservice/myservice_client.cpp
        ${MY_FASTDDS_SOURCES}
    )
    target_include_directories( myservice_client PRIVATE ${MY_INCLUDE_DIRS} )
    target_link_libraries( myservice_client PRIVATE ${MY_LINK_LIBS} )

    # Test
    add_executable( myservice_test
        ${MODULE_ROOT_DIR}/examples/myservice/myservice_test.cpp
        ${MY_FASTDDS_SOURCES}
    )
    target_include_directories( myservice_test PRIVATE ${MY_INCLUDE_DIRS} )
    target_link_libraries( myservice_test PRIVATE ${MY_LINK_LIBS} )

    if( ENABLE_BUILD_TESTS )
        add_test( NAME MyServiceTest COMMAND myservice_test )
        set_tests_properties( MyServiceTest PROPERTIES TIMEOUT 60 )
    endif()
endif()
```

---

## 9. 测试

### 9.1 集成测试模式

推荐的单进程集成测试模式（与 HelloWorld 测试一致）：

```cpp
// 创建两个独立的 CoreIPC binding 实例
auto pServerBinding = MakeShared<CoreIPCBinding>();  // Skeleton 侧
auto pClientBinding = MakeShared<CoreIPCBinding>();  // Proxy 侧

// 关键: server binding 低优先级, client binding 高优先级
// 确保 Skeleton 用 server binding, Proxy 用 client binding
bindingMgr.RegisterBinding({.name="server", .priority=kCustom}, pServerBinding);

// ... 创建 skeleton, offer, 注册 handlers ...

bindingMgr.RegisterBinding({.name="client", .priority=kCoreIpc}, pClientBinding);

// ... 创建 proxy, 测试方法/事件/字段 ...
```

### 9.2 CHECK 宏

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

### 9.3 CTest 完整测试套件

```bash
# 运行所有测试
ctest --test-dir build/ --output-on-failure

# 运行指定标签
ctest --test-dir build/ -L "example" -V

# 运行指定测试
ctest --test-dir build/ -R HelloWorld2DualBindingTest -V
```

当前 CTest 测试结果 (2026-03-01):

```
  RuntimeSerializationTest      ✅ PASSED (4/4)
  ProxySkeletonTest             ✅ PASSED (34/34)
  FutureThenTest                ✅ PASSED
  CoreIPCBindingTest            ✅ PASSED
  HelloWorldIPCTest             ✅ PASSED (CoreIPC-only)
  DdsBindingTest                ✅ PASSED
  DdsDiscoveryTest              ✅ PASSED
  HelloWorld2DualBindingTest    ✅ PASSED (54/54, CoreIPC + DDS 双 binding)
  SomeIpBindingTest             ✅ PASSED
  DbusBindingTest               ✅ PASSED
  SocketBindingTest             ✅ PASSED
```

---

## 10. 示例参考

### 10.1 HelloWorld (CoreIPC-only)

**位置**: `examples/helloworld/`

| 文件 | 说明 |
|------|------|
| `HelloWorld.fidl` | 接口定义 |
| `gen/HelloWorldTypes.hpp` | 生成的类型 |
| `gen/HelloWorldProxy.hpp` | 生成的代理 |
| `gen/HelloWorldSkeleton.hpp` | 生成的骨架 |
| `helloworld_server.cpp` | CoreIPC 服务端 |
| `helloworld_client.cpp` | CoreIPC 客户端 |
| `helloworld_test.cpp` | 集成测试 (CTest) |

**特点**:
- 单 binding (CoreIPC)
- 命名空间: `lap::com::examples`
- CTest: `HelloWorldIPCTest`

### 10.2 HelloWorld2 (双 binding: CoreIPC + DDS)

**位置**: `examples/helloworld2/`

| 文件 | 说明 |
|------|------|
| `HelloWorld2.fidl` | 接口定义 (v2.0) |
| `gen/HelloWorld2ServiceTypes.hpp` | 生成的类型 + 序列化 |
| `gen/HelloWorld2ServiceProxy.hpp` | 生成的代理 |
| `gen/HelloWorld2ServiceSkeleton.hpp` | 生成的骨架 |
| `gen/HelloWorld2ServiceDdsAdapter.hpp` | DDS 类型适配器 |
| `gen/HelloWorld2Service.idl` | OMG IDL |
| `gen/HelloWorld2Service_qos.xml` | DDS QoS 配置 |
| `gen/HelloWorld2ServicePubSubTypes.*` | fastddsgen 生成 |
| `helloworld2_server.cpp` | 双 binding 服务端 |
| `helloworld2_client.cpp` | 双 binding 客户端 |
| `helloworld2_test.cpp` | 集成测试 54 项 (CTest) |

**特点**:
- 双 binding (CoreIPC + DDS)
- 命名空间: `helloworld2`
- DDS 类型适配器自动注册
- BindingManager 自动选择
- CTest: `HelloWorld2DualBindingTest`

### 10.3 通信模式完整性

两个示例共同覆盖 AUTOSAR AP 的所有通信模式：

| 模式 | HelloWorld | HelloWorld2 |
|------|-----------|------------|
| 方法 (请求/响应) | ✅ SayHello, Add, ComputeHash | ✅ SayHello, Add, ComputeHash |
| 方法 (F&F) | ✅ NotifyLog | ✅ NotifyLog |
| 事件 | ✅ Greeting, StatusChanged, DataStream | ✅ Greeting, StatusChanged, DataStream |
| 字段 (只读) | ✅ VisitorCount | ✅ VisitorCount |
| 字段 (读写) | ✅ ServerName | ✅ ServerName |
| 字段 (读写+通知) | ✅ Temperature | ✅ Temperature |

---

## 11. 故障排除

### 11.1 常见编译错误

| 错误 | 原因 | 解决方案 |
|------|------|---------|
| `undeclared identifier 'CoreIPCBinding'` | 缺少头文件 | 添加 `#include "CoreIPCBinding.hpp"` |
| `undefined reference to lap_com_binding_dds` | 未链接 DDS 库 | CMake 添加 `lap_com_binding_dds` |
| `HelloWorld2ServicePubSubTypes.cxx: No such file` | 未运行 fastddsgen | 执行 `fastddsgen <Service>.idl -d gen/ -replace` |
| `unused parameter` (Werror) | 未使用的 main 参数 | 用 `int /* argc */` 注释掉参数名 |

### 11.2 运行时错误

| 错误 | 原因 | 解决方案 |
|------|------|---------|
| `kServiceNotAvailable` | 服务未 Offer 或 binding 不匹配 | 确保 server 先启动并 OfferService |
| `kNoBindingAvailable` | 没有注册 binding | 检查 BindingManager.RegisterBinding |
| `No matched readers for RELIABLE writer` | DDS 读者未就绪 | 增加等待时间，或检查 QoS 兼容性 |
| `Service not found in registry` | CoreIPC 注册表未初始化 | 确保 CRegistryDispatcher.Initialize() + Run() |

### 11.3 测试技巧

- **同进程测试**: 需要两个独立的 CoreIPC binding 实例（server/client 分离）
- **DDS 匹配延迟**: 首次连接可能需要 500ms-2s，测试中需适当增加等待
- **字段通知测试**: `Update()` 应在 `Subscribe()` + `SetReceiveHandler()` 之后调用
- **事件发送重试**: 事件发送可能因 reader 未匹配而丢失，建议循环发送 + 超时等待

---

## 相关文档

| 文档 | 内容 |
|------|------|
| [`ARCHITECTURE_SUMMARY.md`](architecture/ARCHITECTURE_SUMMARY.md) | Com 模块架构总览 |
| [`GENERATOR.md`](architecture/GENERATOR.md) | lap-sidl-gen 生成器详细架构 |
| [`BINDING_ARCHITECTURE.md`](architecture/BINDING_ARCHITECTURE.md) | Binding 层架构设计 |
| [`BINDING_SELECTION_GUIDE.md`](guides/BINDING_SELECTION_GUIDE.md) | Binding 选择决策树 |
| [`DDS_INTEGRATION_GUIDE.md`](guides/DDS_INTEGRATION_GUIDE.md) | DDS 集成指南 |
| [`SERVICE_DISCOVERY_ARCHITECTURE.md`](architecture/SERVICE_DISCOVERY_ARCHITECTURE.md) | 服务发现架构 |
| [`TRANSPORT_MATRIX.md`](architecture/TRANSPORT_MATRIX.md) | 传输矩阵对比 |
