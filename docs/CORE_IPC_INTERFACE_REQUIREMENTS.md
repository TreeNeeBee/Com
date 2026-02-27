# Core IPC接口需求文档

## 文档信息
- **创建日期**: 2026-01-19
- **版本**: 1.0.0
- **目的**: 定义Com模块使用Core IPC替代iceoryx2所需的额外接口

## 背景

Com模块当前使用iceoryx2作为零拷贝IPC传输绑定实现。为了减少外部依赖并统一LightAP架构，计划将iceoryx2替换为Core模块中已实现的IPC功能。

**Core模块现有IPC能力**:
- ✅ Publisher/Subscriber API (零拷贝)
- ✅ Sample/Loan机制 (RAII样本管理)
- ✅ 无锁环形缓冲区 (RingBufferBlock)
- ✅ 共享内存管理 (SharedMemoryManager)
- ✅ Chunk池分配器 (ChunkPoolAllocator)
- ✅ 订阅者注册表 (ChannelRegistry)
- ✅ 三种IPC模式 (SHRINK/NORMAL/EXTEND)

**Com模块ITransportBinding接口需求**:
- Service管理: OfferService / StopOfferService / FindService
- Event通信: SendEvent / SubscribeEvent / UnsubscribeEvent
- Method通信: CallMethod / RegisterMethod (可选)
- Field通信: GetField / SetField (可选)

---

## 需求分析

### 1. 服务发现机制

#### 当前状态
Core IPC基于**SHM路径**进行通信:
```cpp
// 创建Publisher/Subscriber需要明确的SHM路径
Publisher::Create("/dev/shm/my_service", config);
Subscriber::Create("/dev/shm/my_service", config);
```

#### Com模块需求
Com模块需要**服务ID + 实例ID**映射:
```cpp
// ITransportBinding接口
OfferService(service_id=0x1234, instance_id=0x5678);
FindService(service_id=0x1234) -> {instance_id: 0x5678, 0x5679}
```

#### 接口缺口 ❌
**缺少**: 服务注册表和发现机制

**解决方案**:
在Core IPC中新增**ServiceRegistry**组件，管理 (service_id, instance_id) → shm_path 映射。

---

### 2. Service到SHM路径映射

#### 需求
将AUTOSAR服务标识符映射为SHM路径:
```cpp
// 映射规则 (建议)
shm_path = "/dev/shm/lap_ipc_" + hex(service_id) + "_" + hex(instance_id)

// 示例
(0x1234, 0x5678) → "/dev/shm/lap_ipc_1234_5678"
```

#### 接口缺口 ❌
**缺少**: 标准化路径生成函数

**解决方案**:
```cpp
namespace lap::core::ipc {
    String GenerateServicePath(UInt64 service_id, UInt64 instance_id);
}
```

---

### 3. 多实例订阅支持

#### 当前状态
Core IPC每个Subscriber对应一个SHM路径 (1:1)

#### Com模块需求
Com需要支持:
- **同一服务的多个实例订阅** (1:N)
- **事件ID过滤** (同一服务不同事件)

```cpp
// 场景1: 订阅服务0x1234的所有实例
FindService(0x1234) -> {0x5678, 0x5679}
for instance_id in instances:
    SubscribeEvent(0x1234, instance_id, event_id=1, callback)
```

#### 接口缺口 ⚠️
**部分支持**: 可创建多个Subscriber，但需Com层管理

**改进建议** (可选):
```cpp
// MultiSubscriber: 自动聚合多个实例的事件
class MultiSubscriber {
    Result<void> SubscribeService(UInt64 service_id, EventCallback cb);
    // 内部管理多个Subscriber实例
};
```

**结论**: 当前Core IPC已足够，Com层实现聚合逻辑。

---

### 4. 服务生命周期管理

#### Com模块需求
- **OfferService**: 标记服务为"可用"
- **StopOfferService**: 标记服务为"停止"
- **FindService**: 仅返回"可用"的服务

#### 接口缺口 ❌
**缺少**: 服务状态管理 (Available/Stopped)

**解决方案**:
ServiceRegistry需支持状态字段:
```cpp
struct ServiceRegistryEntry {
    UInt64 service_id;
    UInt64 instance_id;
    String shm_path;
    ServiceState state;  // Available / Stopped
};
```

---

### 5. 服务可用性通知 (可选)

#### Com模块需求 (可选)
ITransportBinding支持服务可用性回调:
```cpp
// 当服务上线/下线时通知
SetServiceAvailabilityHandler(service_id, 
    [](instance_id, available) { ... });
```

#### 接口缺口 ⚠️ (低优先级)
**缺少**: 事件通知机制

**解决方案** (Phase 2):
- 使用inotify监控SHM创建/删除
- 或使用专门的控制通道

**结论**: 初版可采用**轮询**方式 (FindService定期查询)

---

### 6. Event ID支持

#### 当前状态
Core IPC的Publisher/Subscriber传输**原始字节流**，无内置Event ID概念。

#### Com模块需求
Com需要区分同一服务的不同事件:
```cpp
SendEvent(service_id, instance_id, event_id=1, data);  // 速度事件
SendEvent(service_id, instance_id, event_id=2, data);  // 位置事件
```

#### 接口缺口 ⚠️
**部分支持**: 可在Payload前添加Event ID头

**解决方案** (Com层实现):
```cpp
// 发送端
ByteBuffer payload;
payload.append(event_id, 4 bytes);  // 前4字节: event_id
payload.append(user_data);
publisher.SendCopy(payload.data(), payload.size());

// 接收端
auto sample = subscriber.Receive();
uint32_t event_id = *reinterpret_cast<uint32_t*>(sample.RawData());
ByteBuffer user_data(sample.RawData() + 4, sample.RawDataSize() - 4);
callback(service_id, instance_id, event_id, user_data);
```

**结论**: Core IPC无需修改，Com层自行封装。

---

### 7. Method调用支持 (请求-响应)

#### 当前状态
Core IPC仅支持**单向发布订阅**，无内置RPC机制。

#### Com模块需求
```cpp
// 客户端
ByteBuffer response = CallMethod(service_id, instance_id, method_id, request);

// 服务端
RegisterMethod(service_id, instance_id, method_id, 
    [](request) -> response { ... });
```

#### 接口缺口 ❌
**缺少**: 请求-响应模式

**解决方案**:
在Core IPC中新增**MethodChannel**组件:
```cpp
class MethodChannel {
    // 客户端: 同步调用
    Result<ByteBuffer> Call(const ByteBuffer& request, UInt64 timeout_ms);
    
    // 服务端: 注册处理器
    Result<void> RegisterHandler(MethodHandler handler);
};
```

**实现方案**:
- 方案A (推荐): 基于两对Publisher/Subscriber (请求通道 + 响应通道)
- 方案B: 使用UDS Socket (高延迟，不推荐)

**结论**: **必须实现** (Method是ara::com核心功能)

---

### 8. Field支持 (Getter/Setter + Notifier)

#### Com模块需求
```cpp
// Getter
ByteBuffer value = GetField(service_id, instance_id, field_id);

// Setter
SetField(service_id, instance_id, field_id, value);

// Notifier (自动通知订阅者)
```

#### 接口缺口 ❌
**缺少**: Field语义

**解决方案**:
Field = Event (Notifier) + Method (Getter/Setter):
```cpp
class FieldChannel {
    // Getter = Method调用 (method_id = field_id | 0x10000)
    Result<ByteBuffer> Get();
    
    // Setter = Method调用 (method_id = field_id | 0x20000)
    Result<void> Set(const ByteBuffer& value);
    
    // Notifier = Event订阅 (event_id = field_id)
    Result<void> SubscribeNotifier(EventCallback callback);
};
```

**结论**: 基于MethodChannel + Event实现 (Com层封装)

---

## 接口需求总结

### 🔴 必须新增 (P0 - 阻塞)

1. **ServiceRegistry 组件**
   ```cpp
   namespace lap::core::ipc {
       class ServiceRegistry {
       public:
           // 注册服务 (OfferService)
           Result<void> RegisterService(UInt64 service_id, UInt64 instance_id, 
                                        const String& shm_path);
           
           // 注销服务 (StopOfferService)
           Result<void> UnregisterService(UInt64 service_id, UInt64 instance_id);
           
           // 查找服务 (FindService)
           Result<Vector<UInt64>> FindInstances(UInt64 service_id);
           
           // 获取SHM路径
           Result<String> GetServicePath(UInt64 service_id, UInt64 instance_id);
       };
   }
   ```

2. **SHM路径生成函数**
   ```cpp
   namespace lap::core::ipc {
       String GenerateServicePath(UInt64 service_id, UInt64 instance_id);
   }
   ```

3. **MethodChannel 组件** (请求-响应)
   ```cpp
   namespace lap::core::ipc {
       class MethodChannel {
       public:
           // 客户端
           static Result<MethodChannel> CreateClient(const String& shm_path);
           Result<ByteBuffer> Call(const ByteBuffer& request, UInt64 timeout_ms);
           
           // 服务端
           static Result<MethodChannel> CreateServer(const String& shm_path);
           Result<void> RegisterHandler(MethodHandler handler);
           Result<void> StartServing();  // 启动请求处理循环
       };
       
       using MethodHandler = std::function<ByteBuffer(const ByteBuffer&)>;
   }
   ```

### 🟡 建议新增 (P1 - 优化)

4. **服务状态管理**
   ```cpp
   enum class ServiceState : UInt8 {
       kAvailable = 0,
       kStopped   = 1
   };
   
   // 在ServiceRegistry中添加状态查询
   Result<ServiceState> GetServiceState(UInt64 service_id, UInt64 instance_id);
   ```

5. **MultiSubscriber 辅助类** (可选)
   ```cpp
   class MultiSubscriber {
       Result<void> SubscribeService(UInt64 service_id, EventCallback callback);
       // 自动订阅所有实例
   };
   ```

### 🟢 可延后 (P2 - 增强)

6. **服务可用性通知**
   - 使用inotify监控/dev/shm
   - 或实现专用控制通道

7. **WaitSet机制** (多路复用)
   ```cpp
   class WaitSet {
       Result<void> Attach(Subscriber& sub);
       Result<Vector<Subscriber*>> Wait(UInt64 timeout_ms);
   };
   ```

---

## 实现优先级

### Phase 1: 核心功能 (本周完成)
- [x] ServiceRegistry (服务注册表)
- [x] GenerateServicePath (路径生成)
- [x] MethodChannel (方法调用)

### Phase 2: 优化改进 (下周)
- [ ] 服务状态管理
- [ ] MultiSubscriber辅助类

### Phase 3: 高级特性 (按需)
- [ ] 服务可用性通知
- [ ] WaitSet多路复用

---

## 设计建议

### ServiceRegistry实现方案

**选项A: 基于SHM的注册表** (推荐)
- 使用共享内存存储服务映射表
- 支持跨进程查询
- 无需守护进程

**选项B: 基于文件的注册表**
- 使用JSON文件存储
- 简单但性能较低

**推荐**: **选项A** (与Com模块现有设计一致)

### MethodChannel实现方案

**架构**:
```
Client                          Server
  │                               │
  ├─► request_pub  ──────────►  request_sub
  │                               │
  │                            [处理请求]
  │                               │
  │◄── response_sub ◄──────────  response_pub
  │                               │
```

**SHM路径命名**:
```cpp
// 请求通道
"/dev/shm/lap_ipc_method_req_{service_id}_{instance_id}"

// 响应通道
"/dev/shm/lap_ipc_method_resp_{service_id}_{instance_id}"
```

**超时处理**:
- 客户端使用Subscriber::Receive()的timeout参数
- 服务端异步处理，无需超时

---

## API使用示例

### 示例1: Event通信 (当前Core IPC已支持)

```cpp
// 服务端
auto path = GenerateServicePath(0x1234, 0x5678);
ServiceRegistry::GetInstance().RegisterService(0x1234, 0x5678, path);

PublisherConfig config;
auto pub = Publisher::Create(path, config).Value();

// 发送Event (Com层封装: 添加event_id头)
uint32_t event_id = 1;
ByteBuffer payload;
payload.append(&event_id, 4);
payload.append(user_data.data(), user_data.size());
pub.SendCopy(payload.data(), payload.size());

// 客户端
auto instances = ServiceRegistry::GetInstance().FindInstances(0x1234).Value();
for (auto inst_id : instances) {
    auto path = ServiceRegistry::GetInstance().GetServicePath(0x1234, inst_id).Value();
    auto sub = Subscriber::Create(path).Value();
    
    auto sample = sub.Receive().Value();
    uint32_t event_id = *reinterpret_cast<uint32_t*>(sample.RawData());
    // 处理事件...
}
```

### 示例2: Method调用 (需新增MethodChannel)

```cpp
// 服务端
auto path = GenerateServicePath(0x1234, 0x5678);
auto method_channel = MethodChannel::CreateServer(path).Value();
method_channel.RegisterHandler([](const ByteBuffer& req) {
    // 处理请求
    return ByteBuffer{response_data};
});
method_channel.StartServing();  // 阻塞处理请求

// 客户端
auto path = ServiceRegistry::GetInstance().GetServicePath(0x1234, 0x5678).Value();
auto method_channel = MethodChannel::CreateClient(path).Value();
auto response = method_channel.Call(request, 1000 /* ms */).Value();
```

---

## 兼容性说明

### Core IPC已有功能 ✅
无需修改，Com层直接使用:
- Publisher/Subscriber API
- Sample RAII机制
- SendCopy/SendEmplace
- Receive API
- 三种IPC模式配置

### Core IPC新增功能 ❌
需在Core模块实现:
- ServiceRegistry
- GenerateServicePath
- MethodChannel

### Com层封装 ⚙️
Com ITransportBinding实现CoreIPCBinding时需:
- Event ID协议封装 (4字节头)
- Field = Method + Event组合
- 多实例订阅管理

---

## 性能预期

| 指标 | iceoryx2 | Core IPC (预期) |
|------|----------|-----------------|
| Event延迟 | <1µs | <5µs |
| Method延迟 | N/A | <100µs |
| 吞吐量 | >10GB/s | >10GB/s |
| 零拷贝 | ✅ | ✅ |
| 无守护进程 | ✅ | ✅ |

**说明**:
- Event性能与iceoryx2相当 (Core IPC基于相同设计)
- Method调用增加额外往返延迟 (双向Pub/Sub)

---

## 后续行动

1. ✅ **本文档**: 定义接口需求
2. ⏳ **Core模块开发**: 实现ServiceRegistry + MethodChannel
3. ⏳ **Com模块开发**: 实现CoreIPCBinding
4. ⏳ **测试验证**: 单元测试 + 集成测试
5. ⏳ **文档更新**: 更新README和架构文档

---

## 参考资料

- [Core模块IPC实现](../../Core/source/inc/ipc/)
- [Com模块ITransportBinding接口](../source/binding/common/ITransportBinding.hpp)
- [iceoryx2设计文档](https://eclipse-iceoryx.github.io/iceoryx2/)
- [AUTOSAR SWS Communication Management](https://www.autosar.org/)
