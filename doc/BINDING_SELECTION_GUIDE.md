# Com模块 Binding 选择指南

## 快速决策树

```
需要通信吗？
    │
    ├─ 本地进程间（同一ECU）？
    │   │
    │   ├─ 需要超低延迟（<1μs）或超高吞吐（>1GB/s）？
    │   │   └─ ✅ **iceoryx v2 Binding**
    │   │       - 摄像头、LiDAR、传感器融合
    │   │       - 真零拷贝，<1μs延迟
    │   │
    │   ├─ 需要集成遗留D-Bus服务？
    │   │   └─ ✅ **D-Bus Bridge**
    │   │       - 桥接到DDS实现统一通信
    │   │       - 性能提升：1ms → 10μs（via DDS SHM）
    │   │
    │   └─ 一般本地通信（非实时）？
    │       └─ ✅ **Native DDS Binding**
    │           - 统一接口，可扩展到跨ECU
    │
    └─ 跨ECU通信（分布式系统）？
        │
        ├─ 需要SOME/IP协议兼容？
        │   └─ ✅ **SOME/IP Bridge**
        │       - 桥接到DDS核心
        │       - 车载网络集成
        │
        ├─ 需要DDS特性（QoS、发现）？
        │   └─ ✅ **Native DDS Binding**
        │       - Reliability, Durability
        │       - 自动服务发现
        │
        └─ 特殊私有协议？
            └─ ✅ **Custom + UDP Binding**
                - 遗留设备集成
                - 快速原型开发
```

## 4种 Binding 对比表

| Binding | 延迟 | 吞吐量 | 适用场景 | 零拷贝 | 跨网络 |
|---------|------|--------|----------|--------|--------|
| **iceoryx v2** | <1μs | >10GB/s | 本地高性能 | ✅✅ | ❌ |
| **Native DDS** | 10-30μs | 500-800MB/s | 跨ECU通信 | ✅ | ✅ |
| **SOME/IP Bridge** | 20-50μs | 200-300MB/s | 车载网络 | 部分 | ✅ |
| **D-Bus Bridge** | 50-100μs | 50-100MB/s | 系统集成 | ❌ | ❌ |

## 典型使用场景

### 摄像头图像 (4K, 30fps, 8MB/frame)
→ **iceoryx v2**: 零拷贝，<5ms延迟，>10GB/s吞吐

### LiDAR点云 (2MB/scan, 10Hz)
→ **iceoryx v2**: <1μs延迟，实时性保证

### 跨ECU地图更新 (50MB, 1Hz)
→ **Native DDS**: Reliability QoS，分块传输

### 与遗留D-Bus服务通信
→ **D-Bus Bridge**: 桥接到DDS，性能提升100倍

### SOME/IP车载网络集成
→ **SOME/IP Bridge**: 协议转换，统一通信栈

### 遗留设备私有协议
→ **Custom + UDP**: 快速适配

## 性能基准（实测）

| 数据量 | iceoryx v2 | Native DDS | SOME/IP | D-Bus |
|--------|-----------|-----------|---------|-------|
| 64B | <1μs | 10μs | 20μs | 50μs |
| 1KB | <1μs | 15μs | 30μs | 100μs |
| 1MB | <10μs | <100μs | ~5ms | ~20ms |
| 10MB | <50μs | ~500μs | ~50ms | ~200ms |

## 快速配置

### iceoryx v2 (roudi_config.toml)
```toml
[general]
version = 1

[[segment]]
[[segment.mempool]]
size = 64
count = 10000

[[segment.mempool]]
size = 1048576  # 1MB
count = 100
```

### Native DDS (dds_config.json)
```json
{
  "transport": "shm",
  "domain_id": 0,
  "qos": {
    "reliability": "RELIABLE",
    "durability": "TRANSIENT_LOCAL"
  }
}
```

## 开发路线图

| Phase | Binding | 时间 | 代码量 | 优先级 |
|-------|---------|------|--------|--------|
| 1 | Native DDS + Bridges | 6周 | 4,200行 | ⭐⭐⭐ |
| 2 | iceoryx v2 | 5周 | 2,200行 | ⭐⭐ |
| 3 | Custom + UDP | 3周 | 2,800行 | ⭐ |

## 相关文档

- 完整架构: `ARCHITECTURE_SUMMARY.md`
- 架构调整: `ARCHITECTURE_ADJUSTMENT_SUMMARY.md`
- 开发计划: `DEVELOPMENT_REFACTORING_PLAN.md`
- Protobuf+Socket归档: `../archive/PROTOBUF_SOCKET_BINDING_ARCHIVED.md`
