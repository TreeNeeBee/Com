# HelloWorld 示例 — 标准 AUTOSAR AP 开发流程

## 概述

本示例演示使用 LightAP Com 模块的 **标准开发流程**：

```
FIDL定义 → 代码生成 → Server实现(Skeleton) → Client实现(Proxy) → 测试
```

开发者只需编写 `.fidl` 接口定义，然后使用 `lap_sidl_gen` 生成框架代码（Types、Proxy、Skeleton），
在此基础上实现业务逻辑。所有序列化、IPC 传输细节由框架自动处理。

## 目录结构

```
examples/helloworld/
├── HelloWorld.fidl              # Step 1: 服务接口定义 (Franca IDL)
├── gen/                         # Step 2: 生成的框架代码 (自动生成, 勿手动修改)
│   ├── HelloWorldTypes.hpp      #   类型定义 + 序列化
│   ├── HelloWorldProxy.hpp      #   客户端代理 (Proxy)
│   ├── HelloWorldSkeleton.hpp   #   服务端骨架 (Skeleton)
│   └── HelloWorld.idl           #   DDS IDL (可选)
├── helloworld_server.cpp        # Step 3: 服务端实现 (使用 Skeleton)
├── helloworld_client.cpp        # Step 4: 客户端实现 (使用 Proxy)
├── helloworld_test.cpp          # Step 5: 单进程集成测试 (CTest)
├── run_test.sh                  # 多进程测试脚本
├── README.md                    # 本文档
└── archive/                     # 旧版原始绑定代码 (仅供参考)
```

## 开发流程详解

### Step 1 — 定义服务接口 (FIDL)

编辑 `HelloWorld.fidl`，定义方法、事件、字段：

```fidl
interface HelloWorld {
    // 请求/响应方法
    method SayHello {
        in  { String name }
        out { String greeting }
    }

    // Fire-and-forget (无响应)
    method NotifyLog fireAndForget {
        in  { String message }
    }

    // 广播事件
    broadcast Greeting {
        out { HelloWorldTypes.GreetingMessage message }
    }

    // 字段 (只读)
    attribute UInt32 VisitorCount readonly

    // 字段 (读写 + 变更通知)
    attribute Double Temperature notify
}
```

### Step 2 — 生成框架代码

```bash
# 使用 CMake 目标
cd build && cmake --build . --target helloworld_generate

# 或手动运行生成器
./generator/build/lap_sidl_gen \
    --input  examples/helloworld/HelloWorld.fidl \
    --output examples/helloworld/gen \
    --namespace "lap::com::examples" \
    --all
```

生成器输出：

| 文件 | 说明 |
|------|------|
| `HelloWorldTypes.hpp` | 所有类型定义 + ADL 序列化函数 |
| `HelloWorldProxy.hpp` | 客户端代理类 (含类型安全的方法/事件/字段访问) |
| `HelloWorldSkeleton.hpp` | 服务端骨架类 (含处理器注册接口) |
| `HelloWorld.idl` | DDS IDL (用于 DDS 绑定) |

### Step 3 — 实现服务端 (Server)

```cpp
#include "HelloWorldSkeleton.hpp"

// 创建 Skeleton
HelloWorldSkeleton skeleton( InstanceSpecifier( "HelloWorld/Provider" ) );

// 注册方法处理器
skeleton.sayHello.RegisterMethodHandler(
    []( String name ) -> Future< String > {
        return MakeReadyFuture< String >( "Hello, " + name + "!" );
    } );

// 注册字段 getter/setter
skeleton.visitorCount.RegisterGetHandler(
    []() -> Future< UInt32 > { return MakeReadyFuture< UInt32 >( 42 ); } );

// 提供服务
skeleton.OfferService();

// 发送事件
auto sample = skeleton.greeting.Allocate();
sample.Value()->message.text = "Hello from server";
skeleton.greeting.Send( std::move( sample ).Value() );

// 字段变更通知
skeleton.temperature.Update( 36.6 );
```

### Step 4 — 实现客户端 (Client)

```cpp
#include "HelloWorldProxy.hpp"

// 创建 Proxy
auto proxyResult = HelloWorldProxy::Create( handle );
auto& proxy = proxyResult.Value();

// 调用方法 (类型安全, 自动序列化)
auto r = proxy.sayHello( String( "Alice" ) );
// r.Value() == "Hello, Alice!"

// 订阅事件
proxy.greeting.Subscribe();
proxy.greeting.SetReceiveHandler( [&] {
    auto sample = proxy.greeting.GetNextSample();
    std::cout << sample.Value()->message.text << std::endl;
} );

// 读写字段
auto count = proxy.visitorCount.Get();    // 只读
proxy.serverName.Set( String( "New" ) );  // 写入
```

## 通信模式

### 方法 (Methods)

| 方法 | 类型 | 签名 |
|------|------|------|
| SayHello | 请求/响应 | `String → String` |
| Add | 请求/响应 | `(UInt32, UInt32) → UInt32` |
| NotifyLog | Fire-and-Forget | `String → void` |
| ComputeHash | 请求/响应 | `ByteBuffer → UInt64` |

### 事件 (Events)

| 事件 | 数据类型 | 说明 |
|------|----------|------|
| Greeting | `GreetingMessage` | 周期性文本广播 |
| StatusChanged | `ServerStatus` | 服务状态变更通知 |
| DataStream | `DataChunk` | 二进制数据流推送 |

### 字段 (Fields)

| 字段 | 类型 | 访问模式 |
|------|------|----------|
| VisitorCount | `UInt32` | 只读 (getter) |
| ServerName | `String` | 读写 (getter + setter) |
| Temperature | `Double` | 读写 + 通知 (getter + setter + notify) |

## Element ID 分配

| 类型 | 名称 | ID | 说明 |
|------|------|----|------|
| Event | Greeting | `0x0001` | |
| Event | StatusChanged | `0x0002` | |
| Event | DataStream | `0x0003` | |
| Method | SayHello | `0x0100` | |
| Method | Add | `0x0101` | |
| Method | NotifyLog | `0x0102` | fire-and-forget |
| Method | ComputeHash | `0x0103` | |
| Field | VisitorCount | `0x0200` | readonly |
| Field | ServerName | `0x0201` | |
| Field | Temperature | `0x0202` | +notify |

## 构建

```bash
cd /workspace/LightAP/modules/Com/build

# 配置 (首次)
cmake .. -DENABLE_BUILD_EXAMPLES=ON -DENABLE_BUILD_TESTS=ON

# 构建所有目标
cmake --build . -j$(nproc)

# 或单独构建
cmake --build . --target helloworld_server
cmake --build . --target helloworld_client
cmake --build . --target helloworld_test

# 重新生成框架代码 (修改 FIDL 后)
cmake --build . --target helloworld_generate
```

## 运行

### 单进程集成测试 (CI)

```bash
rm -f /dev/shm/lap_*
./helloworld_test
```

输出示例 (46 checks):
```
=== HelloWorld Integration Test ===
--- Infrastructure Setup ---
  PASS: dispatcher.Initialize()
  PASS: ServerBinding.Initialize()
  ...
--- Method Tests ---
  PASS: proxy.sayHello( "Alice" )
  PASS: SayHello response == "Hello, Alice!"
  ...
--- Event Tests ---
  PASS: Greeting event received
  ...
--- Field Tests ---
  PASS: VisitorCount == 42
  ...
========================================
Results: 46 / 46 passed
ALL PASSED
```

### 多进程模式

```bash
# 终端 1: 启动 Server
rm -f /dev/shm/lap_*
./helloworld_server

# 终端 2: 启动 Client
./helloworld_client

# 或使用测试脚本
./examples/helloworld/run_test.sh
```

### CTest

```bash
ctest -R HelloWorldIPCTest --output-on-failure
```

## 架构

```
┌─────────────────────────────────────────────────────┐
│  HelloWorld.fidl                                     │
│  (Service Definition)                                │
└────────────────────────┬────────────────────────────┘
                         │  lap_sidl_gen --all
                         ▼
┌─────────────┬──────────────────┬────────────────────┐
│ Types.hpp   │  Proxy.hpp       │  Skeleton.hpp       │
│ (types +    │  (client API)    │  (server API)       │
│  serialize) │                  │                     │
└──────┬──────┴────────┬─────────┴──────────┬─────────┘
       │               │                    │
       ▼               ▼                    ▼
┌──────────────────────────────────────────────────────┐
│  Com Runtime                                          │
│  ┌─────────────┐ ┌──────────────┐ ┌───────────────┐  │
│  │ ProxyEvent   │ │ ProxyMethod  │ │ ProxyField    │  │
│  │ SkeletonEvt  │ │ SkeletonMeth │ │ SkeletonField │  │
│  └──────┬──────┘ └──────┬───────┘ └──────┬────────┘  │
│         └───────────────┼────────────────┘            │
│                         ▼                             │
│  ┌──────────────────────────────────────────────────┐ │
│  │  BindingManager → ITransportBinding              │ │
│  │  ┌──────────────┐  ┌──────┐  ┌────────┐         │ │
│  │  │ CoreIPCBinding│  │ DDS  │  │ SOME/IP│  ...    │ │
│  │  └──────────────┘  └──────┘  └────────┘         │ │
│  └──────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────┘
```

## 旧版代码

`archive/` 目录保留了旧版使用原始 `CoreIPCBinding` API 直接操作的代码，
包含手写序列化等。仅供理解底层机制参考，**新开发应使用生成器流程**。
