# LightAP Com 模块 Binding 选择指南

> **文档版本**: 2.0  
> **最后更新**: 2026-03-02  
> **基于**: `source/binding/` 实际实现状态

---

## 当前实现状态

| Binding | 状态 | 优先级 | 源码路径 |
|---------|------|--------|---------|
| **CoreIPC** | ✅ **已实现** | 100 | `source/binding/coreipc/` |
| **DDS** | ✅ **已实现** | 80 | `source/binding/dds/` |
| SOME/IP | ⚠️ 待实现 | 60 | `source/binding/someip/` (骨架) |
| Socket | ⚠️ 待实现 | 40 | `source/binding/socket/` (骨架) |
| D-Bus | ⚠️ 待实现 | 20 | `source/binding/dbus/` (骨架) |

---

## 快速决策树

```
需要跨 ECU 通信？
├─ YES → DDS Binding ✅
│        适用: 局域网/广域网、QoS、动态发现、FastDDS Discovery Server
│
└─ NO (同 ECU 进程间)
   └─ CoreIPC Binding ✅
      适用: 传感器数据、图像流、实时控制、任何高性能本地通信
```

**规则**: 同 ECU 优先 CoreIPC，跨 ECU 用 DDS。当两者都需要时，使用 `--binding coreipc,dds` 同时注册（BindingManager 自动按优先级选择最优路径）。

---

## 两种可用 Binding 详解

### CoreIPC — 本地零拷贝 IPC

**原则**: 同 ECU 通信的最优选择

| 指标 | 值 |
|------|-----|
| 延迟 (64B) | < 1µs |
| 延迟 (1MB) | < 10µs |
| 吞吐量 | > 10 GB/s |
| CPU 占用 | < 0.5% |
| 零拷贝 | ✅✅（共享内存直接访问） |
| 跨 ECU | ❌ |

**适用场景**:
- 摄像头图像 (4K, 30fps, 8MB/frame)
- LiDAR 点云 (2MB/scan, 10Hz)
- 传感器融合结果 (100KB, 100Hz)
- 实时控制指令 (64B, 1kHz)

```bash
# 生成 CoreIPC-only 代码
lap-sidl-gen -i MyService.fidl -o gen_server --server
lap-sidl-gen -i MyService.fidl -o gen_client --client
```

```cpp
// CMake 链接
target_link_libraries(my_server PRIVATE
    lap_com_binding_coreipc lap_com lap_core lap_log pthread)
```

---

### DDS — 跨网络分布式通信

**原则**: 跨 ECU 或需要网络 QoS 时使用

| 指标 | 值 |
|------|-----|
| 延迟 (SHM, 同 ECU) | < 10µs |
| 延迟 (UDP, 跨 ECU) | < 30µs |
| 吞吐量 (SHM) | > 1 GB/s |
| 吞吐量 (UDP) | ~900 MB/s |
| 零拷贝 | ✅ (SHM 模式) |
| 跨 ECU | ✅ |

**适用场景**:
- 跨 ECU 地图更新 (50MB, 1Hz)
- 车联网 V2X 数据
- 分布式传感器数据共享
- 需要 QoS (Reliability / Durability) 的场景
- 跨网络服务发现

```bash
# 生成 DDS-only 代码
lap-sidl-gen -i MyService.fidl -o gen_server --server --binding dds
lap-sidl-gen -i MyService.fidl -o gen_client --client --binding dds
```

```cpp
// CMake 链接 (DDS-only)
target_link_libraries(my_server PRIVATE
    lap_com_binding_dds lap_com lap_core lap_log ${DDS_LIBRARIES} pthread)
```

---

### 双 Binding 组合 (CoreIPC + DDS)

当服务既有本地消费者（同 ECU）也有远程消费者（跨 ECU）时，同时注册两个 binding：

```bash
# 生成双 binding 代码
lap-sidl-gen -i MyService.fidl -o gen_server --server --binding coreipc,dds
lap-sidl-gen -i MyService.fidl -o gen_client --client --binding coreipc,dds
```

BindingManager 会自动选择优先级最高的可用 binding（本地连接优先 CoreIPC，无本地实例则回落到 DDS）。

```cpp
// CMake 链接 (CoreIPC + DDS)
target_link_libraries(my_server PRIVATE
    lap_com_binding_coreipc lap_com_binding_dds
    lap_com lap_core lap_log ${DDS_LIBRARIES} pthread)
```

---

## 对比表

| 指标 | CoreIPC | DDS (SHM) | DDS (UDP) |
|------|---------|-----------|-----------|
| **延迟 (64B)** | < 1µs | < 10µs | < 30µs |
| **延迟 (1MB)** | < 10µs | < 100µs | < 5ms |
| **吞吐量** | > 10 GB/s | > 1 GB/s | ~900 MB/s |
| **CPU 占用** | < 0.5% | ~1% | ~4% |
| **零拷贝** | ✅✅ | ✅ | ❌ |
| **跨 ECU** | ❌ | 仅同机 SHM | ✅ |
| **服务发现** | 固定槽位 O(1) | FastDDS Discovery / DS | FastDDS Discovery / DS |
| **QoS 配置** | ❌ | 有限 | Reliability / Durability / Deadline |
| **生成代码** | `--binding coreipc` | `--binding dds` | `--binding dds` |

---

## 待实现 Binding（规划中）

以下 binding 在 `source/binding/` 下仅有骨架文件，核心逻辑**尚未实现**：

| Binding | 规划优先级 | 主要用途 |
|---------|-----------|---------|
| SOME/IP | P1 | AUTOSAR CP 互通、车身网络集成 |
| Socket | P2 | Unix Domain Socket 轻量连接、遗留系统集成 |
| D-Bus | P3 | systemd/系统服务集成 |

在这些 binding 可用之前，推荐的替代方案：
- **SOME/IP 场景** → DDS（Fast-DDS 支持跨网络，性能更优）
- **Socket 场景** → CoreIPC（同 ECU 更高性能）
- **D-Bus 场景** → CoreIPC（本地 IPC 同等功能且性能更优）

---

## DDS Discovery Server 配置

跨 ECU 通信推荐使用 Fast-DDS Discovery Server（避免 mDNS 广播）：

```yaml
# config/bindings.yaml
dds:
  discovery_server: "tcp://192.168.1.10:42100"
  ds_health_check_interval_ms: 5000
  ds_max_failures: 3
  ds_reconnect_interval_ms: 10000
  ds_enable_fallback: true   # DS 不可用时退化到 PDP
  ds_enable_reconnect: true  # 自动恢复
```

---

## 相关文档

| 文档 | 内容 |
|------|------|
| [DEVELOPMENT_GUIDE.md](DEVELOPMENT_GUIDE.md) | 完整开发流程（包含代码生成命令） |
| [DDS_INTEGRATION_GUIDE.md](DDS_INTEGRATION_GUIDE.md) | DDS 配置与 Security 详解 |
| [../architecture/TRANSPORT_MATRIX.md](../architecture/TRANSPORT_MATRIX.md) | 各 Binding 实现状态与技术细节 |
| [../architecture/BINDING_ARCHITECTURE.md](../architecture/BINDING_ARCHITECTURE.md) | ITransportBinding NVI 接口架构 |
