# Protobuf + Unix Domain Socket Binding - 归档文档

**归档日期**: 2025-11-19  
**归档原因**: 架构调整，统一使用 iceoryx v2 作为高性能本地IPC方案  
**替代方案**: iceoryx v2 Binding (零拷贝，<1μs延迟，>10GB/s吞吐量)

---

## 原设计概述

### 设计定位

**核心目标**: 提供**极致性能**的本地进程间通信（IPC）方案

**适用场景**:
- 🚀 **高吞吐量**: 传感器融合、视觉处理、大数据量传输
- ⚡ **低延迟**: 控制回路、实时决策、毫秒级响应
- 🔒 **本地安全**: 同一ECU内进程通信，无网络暴露

**性能目标**:
- 延迟: < 5μs (P99)
- 吞吐量: > 1 GB/s (单连接)
- 零拷贝: 支持共享内存传输
- CPU占用: < 2% (idle状态)

---

## 架构组成

| 组件 | 功能 | 文件 |
|------|------|------|
| `SocketConnectionManager` | Unix Socket 连接管理 | `SocketConnection.hpp` |
| `ProtobufSerializer` | Protobuf 序列化引擎 | `ProtobufSerializer.hpp` |
| `SocketMethodBinding` | 方法调用绑定 | `SocketMethodBinding.hpp` |
| `SocketEventBinding` | 事件推送绑定 | `SocketEventBinding.hpp` |
| `SocketFieldBinding` | Field 通知绑定 | `SocketFieldBinding.hpp` |
| `ZeroCopyBuffer` | 共享内存缓冲区 | `ZeroCopyBuffer.hpp` |

---

## 核心技术

### 1. Unix Domain Socket 优化

```cpp
// Socket选项优化
SO_SNDBUF = 4MB          // 大发送缓冲区
SO_RCVBUF = 4MB          // 大接收缓冲区
SO_REUSEADDR = true      // 快速重启
SO_PASSCRED = true       // 传递进程凭证
```

### 2. Protobuf 零拷贝序列化

```cpp
// 使用 Arena 分配器
google::protobuf::Arena arena;
auto* msg = google::protobuf::Arena::CreateMessage<MyMessage>(&arena);

// 使用 ZeroCopyOutputStream
google::protobuf::io::FileOutputStream output(socket_fd);
msg->SerializeToZeroCopyStream(&output);
```

### 3. 共享内存传输（大消息）

```cpp
// 超过阈值（默认 64KB）使用共享内存
if (payload_size > SHARED_MEMORY_THRESHOLD) {
    // 1. 创建共享内存
    int shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, payload_size);
    
    // 2. 通过 Socket 发送描述符
    send_fd_over_socket(socket_fd, shm_fd);
    
    // 3. 接收端直接映射，零拷贝
    void* addr = mmap(nullptr, size, PROT_READ, MAP_SHARED, shm_fd, 0);
}
```

### 4. 异步 I/O (epoll)

```cpp
// 事件驱动架构
int epoll_fd = epoll_create1(0);
epoll_event event{.events = EPOLLIN | EPOLLET};  // 边缘触发
epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, &event);

// 单线程事件循环
while (running) {
    int n = epoll_wait(epoll_fd, events, MAX_EVENTS, timeout);
    for (int i = 0; i < n; ++i) {
        handle_socket_event(events[i]);
    }
}
```

---

## 接口设计

### SocketConnectionManager.hpp

```cpp
class SocketConnectionManager {
public:
    Result<void> Initialize(const SocketConfig& config);
    
    // 服务端监听
    Result<void> Listen(const std::string& socket_path);
    
    // 客户端连接
    Result<ConnectionHandle> Connect(const std::string& socket_path);
    
    // 发送消息（自动选择传输方式）
    Result<void> Send(ConnectionHandle conn, const google::protobuf::Message& msg);
    
    // 接收消息（异步回调）
    void RegisterReceiveHandler(std::function<void(const Message&)> handler);
    
private:
    int epoll_fd_;
    std::unordered_map<int, Connection> connections_;
    google::protobuf::Arena arena_;  // 内存池
};
```

### ProtobufSerializer.hpp

```cpp
template <typename ProtoT>
class ProtobufSerializer {
public:
    // 序列化到缓冲区
    Result<ByteBuffer> Serialize(const ProtoT& message);
    
    // 从缓冲区反序列化
    Result<ProtoT> Deserialize(const ByteBuffer& buffer);
    
    // 零拷贝序列化（直接写入 Socket）
    Result<void> SerializeToSocket(int socket_fd, const ProtoT& message);
    
    // 零拷贝反序列化（从共享内存）
    Result<ProtoT> DeserializeFromSharedMemory(void* shm_addr, size_t size);
};
```

---

## AUTOSAR 集成

### 服务描述（IDL）

```protobuf
// Franca IDL 仍用于接口定义
interface MyService {
    method Calculate {
        in { Int32 a, Int32 b }
        out { Int32 result }
    }
    broadcast StatusChanged {
        out { String status }
    }
}

// 自动生成 Protobuf .proto
message CalculateRequest {
    int32 a = 1;
    int32 b = 2;
}
message CalculateResponse {
    int32 result = 1;
}
```

### 代码生成流程

```bash
# 1. Franca IDL -> Protobuf .proto
franca2proto MyService.fidl -o MyService.proto

# 2. Protobuf .proto -> C++ 代码
protoc --cpp_out=. MyService.proto

# 3. 生成 ara::com 绑定
generate_socket_binding MyService.fidl
```

---

## 性能基准

| 指标 | D-Bus | SOME/IP | DDS | Protobuf+Socket |
|------|-------|---------|-----|-----------------|
| 延迟 (小消息) | 50-100μs | 20-50μs | 10-30μs | **< 5μs** |
| 吞吐量 (MB/s) | 50-100 | 200-300 | 500-800 | **> 1000** |
| CPU 占用 | 3-5% | 2-4% | 4-6% | **< 2%** |
| 内存占用 | 中 | 中 | 高 | **低** |
| 零拷贝 | ❌ | 部分 | ✅ | **✅** |
| 跨网络 | ❌ | ✅ | ✅ | **❌** |

---

## 使用场景

| 场景 | 推荐传输 | 原因 |
|------|----------|------|
| 摄像头图像数据 (10MB/frame, 30fps) | Protobuf+Socket | 极致吞吐量 |
| LiDAR点云数据 (2MB/scan, 10Hz) | Protobuf+Socket | 低延迟 + 大数据 |
| 传感器融合结果 (< 1KB, 100Hz) | Protobuf+Socket / DDS | 看是否需要跨ECU |
| ECU内服务调用 (< 1KB, 低频) | D-Bus | 简单场景 |
| 车辆控制指令 (< 100B, < 10ms) | Protobuf+Socket | 超低延迟 |

---

## 配置示例

### socket_config.json

```json
{
  "bindings": [
    {
      "type": "socket",
      "socket_path": "/tmp/lightap_com.sock",
      "buffer_size": 4194304,
      "shared_memory_threshold": 65536,
      "zero_copy_enabled": true,
      "epoll_timeout_ms": 10,
      "max_connections": 64
    }
  ],
  "serialization": {
    "format": "protobuf",
    "arena_block_size": 262144,
    "compression": "none"
  }
}
```

---

## 实现路线图（原计划）

### Phase 1: 核心功能 (2周)
- Week 1: SocketConnectionManager + epoll 事件循环
- Week 2: ProtobufSerializer + 基础 Method/Event 绑定

### Phase 2: 性能优化 (2周)
- Week 3: 共享内存零拷贝传输
- Week 4: Arena 分配器 + 批量发送优化

### Phase 3: 完整集成 (1周)
- Week 5: Franca-to-Protobuf 代码生成 + 端到端测试

**预计工作量**: 5周，~3,500行代码

---

## 为什么归档？

### 架构调整原因

1. **iceoryx v2 更优秀**:
   - 延迟更低: <1μs vs <5μs
   - 吞吐量更高: >10GB/s vs >1GB/s
   - 真零拷贝: 无需序列化开销
   - 成熟稳定: Eclipse基金会支持，汽车行业广泛使用

2. **简化技术栈**:
   - 减少维护负担
   - 统一本地IPC方案
   - 避免方案碎片化

3. **更好的实时性**:
   - iceoryx Lock-free算法
   - 确定性延迟保证
   - POSIX兼容，适合RTOS

### 迁移建议

如需类似功能，请使用 **iceoryx v2 Binding**:

| Protobuf+Socket 特性 | iceoryx v2 替代 |
|---------------------|----------------|
| Unix Domain Socket | POSIX Shared Memory |
| Protobuf序列化 | 直接内存访问（无序列化） |
| 共享内存大消息 | MemPool零拷贝（所有消息） |
| epoll异步IO | Lock-free SPSC队列 |
| 低延迟(<5μs) | 超低延迟(<1μs) |
| 高吞吐(>1GB/s) | 极高吞吐(>10GB/s) |

---

## 参考资料

- **Protobuf官方文档**: https://protobuf.dev/
- **Unix Domain Socket编程**: `man 7 unix`
- **Zero-copy技术**: https://www.kernel.org/doc/html/latest/networking/msg_zerocopy.html
- **epoll高级用法**: `man 7 epoll`

---

**文档状态**: 🗂️ **已归档**  
**维护者**: LightAP Com模块开发团队  
**归档时间**: 2025-11-19
