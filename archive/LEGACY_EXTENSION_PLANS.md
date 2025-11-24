# 扩展方案详细设计 - Protobuf 和自定义协议

> **📦 历史规划文档**: 此文档包含早期规划的 Protobuf 和自定义协议扩展方案
> 
> **当前状态**: 已被更简洁的插件化架构替代（Custom Protocol + UDS Binding）
> 
> **归档日期**: 2025-11-19

---

## 扩展方案1: Protobuf over Unix Socket (历史规划)

### 目标

高性能本地进程间通信

### 核心组件

1. **SocketConnectionManager** - Unix Socket连接管理
2. **ProtobufSerializer** - Protobuf序列化器
3. **SocketMethodBinding** - 方法绑定
4. **SocketEventBinding** - 事件绑定
5. **SocketFieldBinding** - 字段绑定

### 目录结构

```
modules/Com/source/binding/socket/
├── SocketConnectionManager.hpp    # Socket管理
├── ProtobufSerializer.hpp         # Protobuf序列化
├── SocketMethodBinding.hpp        # 方法绑定
├── SocketEventBinding.hpp         # 事件绑定
└── SocketFieldBinding.hpp         # 字段绑定
```

### 使用流程

```cpp
// 1. 定义Protobuf消息
// calculator.proto
message CalculateRequest {
    double operand1 = 1;
    double operand2 = 2;
    string operation = 3;
}

message CalculateResponse {
    double result = 1;
}

// 2. 生成C++代码
$ protoc --cpp_out=. calculator.proto

// 3. 服务端实现
SocketMethodResponder<CalculateRequest, CalculateResponse> responder(
    endpoint,
    [](const CalculateRequest& req) -> CalculateResponse {
        CalculateResponse resp;
        if (req.operation() == "add") {
            resp.set_result(req.operand1() + req.operand2());
        }
        return resp;
    }
);
responder.start();

// 4. 客户端调用
SocketMethodCaller<CalculateRequest, CalculateResponse> caller(endpoint);
CalculateRequest request;
request.set_operand1(10);
request.set_operand2(5);
request.set_operation("add");

auto result = caller.call(request);
if (result.HasValue()) {
    std::cout << "Result: " << result.Value().result() << std::endl;
}
```

### 关键设计决策

| 方面 | 决策 | 理由 |
|------|------|------|
| 传输层 | Unix Domain Socket | 零网络开销，高性能 |
| 序列化 | Protobuf | 成熟、高效、跨语言 |
| 帧格式 | Length-Delimited | 4字节长度 + Protobuf payload |
| 连接模式 | SOCK_STREAM | 可靠、有序、面向连接 |
| 线程模型 | 每连接一线程 | 简化实现，后续可优化 |

### 性能目标

- 延迟: < 100μs (本地通信)
- 吞吐量: > 100k msg/s (小消息)
- 吞吐量: > 1GB/s (大消息)

---

## 扩展方案2: 自定义私有协议 (历史规划)

### 目标

极致性能，灵活定制

### 协议设计

```
LightAP Custom Protocol Frame Format:

┌─────────┬─────────┬─────────┬──────────┬──────────┬────────┐
│ Magic   │ Version │ Flags   │ Length   │ Payload  │  CRC   │
│ (4B)    │ (1B)    │ (1B)    │ (4B)     │ (N bytes)│ (4B)   │
└─────────┴─────────┴─────────┴──────────┴──────────┴────────┘

Magic:   0x4C415000 ('LAP\0')
Version: 0x01
Flags:   bit0: Compressed
         bit1: Encrypted
         bit2-3: Priority (0-3)
         bit4-5: Message Type (Request/Response/Event/Notification)
Length:  Payload length (big-endian)
CRC:     CRC32 checksum
```

### 核心组件

1. **CustomProtocol.hpp** - 协议定义
2. **CustomCodec.hpp** - 编解码器 (含CRC、压缩)
3. **CustomTransport.hpp** - 传输层抽象
4. **TcpTransport / UdpTransport / ShmTransport** - 具体传输实现
5. **CustomMethodBinding / CustomEventBinding / CustomFieldBinding**

### 目录结构

```
modules/Com/source/binding/custom/
├── CustomProtocol.hpp          # 协议定义
├── CustomCodec.hpp             # 编解码器
├── CustomTransport.hpp         # 传输抽象
├── TcpTransport.hpp            # TCP实现
├── UdpTransport.hpp            # UDP实现
├── ShmTransport.hpp            # 共享内存实现
├── CustomMethodBinding.hpp     # 方法绑定
├── CustomEventBinding.hpp      # 事件绑定
└── CustomFieldBinding.hpp      # 字段绑定
```

### 特色功能

1. **多传输支持**:
   - TCP: 可靠、跨网络
   - UDP: 低延迟、广播
   - 共享内存: 极致性能

2. **灵活序列化**:
   - 继承`Serialization.hpp`接口
   - 可选压缩 (LZ4/Zstd)
   - 可选加密 (AES)

3. **可靠性机制**:
   - CRC32校验
   - 可选重传
   - 超时处理

4. **性能优化**:
   - 零拷贝传输
   - 批量发送
   - 优先级队列

### 使用示例

```cpp
// 1. 选择传输层
auto transport = std::make_unique<TcpTransport>();  // 或 UdpTransport, ShmTransport

// 2. 创建方法响应器
CustomMethodResponder<MyRequest, MyResponse> responder(
    std::move(transport),
    "0.0.0.0:8080",
    [](const MyRequest& req) -> MyResponse {
        // 处理请求
        return MyResponse{};
    }
);
responder.start();

// 3. 客户端调用
auto clientTransport = std::make_unique<TcpTransport>();
CustomMethodCaller<MyRequest, MyResponse> caller(
    std::move(clientTransport),
    "127.0.0.1:8080"
);

MyRequest request;
auto result = caller.call(request);
```

### 性能目标

- 延迟: < 10μs (共享内存)
- 吞吐量: > 500k msg/s (小消息)
- 压缩率: > 50% (文本数据)

---

## 实施路线图 (历史规划)

### Phase 1: Protobuf over Socket (2-3周)

**Week 1: 核心基础**
- [ ] SocketConnectionManager实现 (3天)
- [ ] ProtobufSerializer实现 (2天)

**Week 2: 绑定层**
- [ ] SocketMethodBinding (3天)
- [ ] SocketEventBinding (2天)
- [ ] SocketFieldBinding (2天)

**Week 3: 测试与文档**
- [ ] 单元测试 (覆盖率 > 80%)
- [ ] 集成测试 (端到端)
- [ ] 性能基准测试
- [ ] API文档和示例

### Phase 2: 自定义协议 (3-4周)

**Week 1: 协议基础**
- [ ] 协议规范定义
- [ ] CustomCodec实现 (含CRC)
- [ ] 压缩/解压缩支持

**Week 2-3: 传输层**
- [ ] TCP Transport
- [ ] UDP Transport
- [ ] SharedMemory Transport
- [ ] 传输层工厂模式

**Week 4: 绑定与优化**
- [ ] Method/Event/Field Binding
- [ ] 性能优化 (零拷贝、批量处理)
- [ ] 完整测试和文档

---

## 为什么被替代

### 新架构优势

**当前 Custom Protocol + UDS Binding 方案**:
- ✅ 更简洁的设计（24字节帧头 vs 14字节）
- ✅ 统一的可扩展编解码器框架
- ✅ 与 iceoryx2 共享内存池集成
- ✅ 完全插件化（.so 动态加载）
- ✅ 配置驱动（binding_config.json）

**旧方案问题**:
- ❌ 协议帧过于复杂（14字节+CRC32）
- ❌ 多传输层实现增加维护成本
- ❌ 与主架构集成不够紧密
- ❌ 缺少与 iceoryx2 的零拷贝互操作

### 迁移到新方案

**新方案核心特点**:

1. **简化协议帧** (24字节):
   ```
   Magic(2B) + Version(1B) + Type(1B) + Flags(1B) + 
   MessageID(4B) + PayloadSize(4B) + CRC16(2B) + Timestamp(8B)
   ```

2. **可扩展编解码器**:
   - IProtocolCodec 接口
   - BinaryCodec / JsonCodec / CustomCodec 实现
   - 支持多种序列化格式

3. **UDS 专注**:
   - SOCK_STREAM / SOCK_DGRAM 模式
   - 专注本地 IPC，性能更好
   - 简化实现和维护

4. **零拷贝集成**:
   - 可选与 iceoryx2 UMEM 共享
   - 支持 sendmsg() 零拷贝 API
   - 性能 >500MB/s

---

**文档版本**: 1.0 (归档版本)  
**归档日期**: 2025-11-19  
**替代方案**: Custom Protocol + UDS Binding (参见 ARCHITECTURE_SUMMARY.md 第10章)
