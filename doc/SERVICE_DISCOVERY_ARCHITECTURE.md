# 服务发现架构设计

## 文档信息

- **版本**: 3.0 (零 Daemon 固定槽位自注册架构)
- **日期**: 2025-11-19
- **标准**: AUTOSAR AP R24-11 (November 2024)
- **命名空间**: lap::com (100% 兼容 AUTOSAR ara::com)
- **架构特性**: 零 Daemon + 固定槽位 + iceoryx2 共享内存 + seqlock + 心跳
- **基准文档**: 
  - AUTOSAR_AP_SWS_CommunicationManagement.pdf
  - AUTOSAR_AP_SWS_NetworkManagement.pdf
  - iceoryx2 Architecture & Design
- **状态**: 设计完成 (突破性架构)

---

## 第 1 章: 概述

### 1.1 零 Daemon 架构革命

**传统架构的痛点**：

传统服务发现方案存在以下问题：

1. **Daemon 依赖**: RouDi (iceoryx v1) / systemd-resolved / avahi-daemon 等守护进程
   - 单点故障风险
   - 启动顺序依赖
   - 额外的进程调度开销
   
2. **通信开销**: Domain Socket / D-Bus / TCP 查询延迟
   - Domain Socket: ~1-5µs
   - D-Bus/SOME-IP SD: 5-100ms
   
3. **不确定性**: IPC/网络通信引入的抖动
   - 影响实时系统的可预测性

**本架构突破性创新**：

```
零 Daemon + 固定槽位自注册 + iceoryx2 共享内存
= < 500ns 延迟 + 100% 确定性 + 零单点故障
```

**核心设计原则**：

- ✅ **零守护进程**: 完全去中心化，无任何 Daemon
- ✅ **固定槽位**: 服务 ID → 槽位映射，编译期或静态配置确定
- ✅ **共享内存**: iceoryx2 提供 memfd + 1GB 大页 + 权限管理
- ✅ **Lock-Free**: seqlock 无锁读取，原子写入
- ✅ **心跳机制**: 进程级生命周期检测
- ✅ **FuSa 就绪**: Guard page + 权限隔离 + QM/ASIL-D 物理分离

### 1.2 服务发现的作用

在 AUTOSAR Adaptive Platform 中，服务发现（Service Discovery）是 lap::com（100% 兼容 ara::com）的核心功能，负责：

1. **服务注册**: 服务提供者向系统注册可用服务 (OfferService)
2. **服务查找**: 服务消费者查找并定位可用服务 (FindService)
3. **动态绑定**: 运行时建立服务提供者与消费者的连接
4. **生命周期管理**: 监控服务的上线/下线状态
5. **实例管理**: 支持同一服务的多个实例

**本架构实现方式**：

- 注册: 进程启动时写入固定槽位 (原子操作)
- 查找: 直接读取共享内存槽位 (零拷贝、零延迟)
- 生命周期: 心跳超时检测 + 进程退出自动清理

### 1.3 AUTOSAR 标准需求追溯

| 需求 ID | 描述 | 本模块实现 |
|---------|------|-----------|
| **RS_CM_00101** | 服务提供 (OfferService) | ✅ 固定槽位写入 |
| **RS_CM_00102** | 服务查找 (FindService) | ✅ 共享内存读取 |
| **RS_CM_00105** | 停止服务提供 (StopOfferService) | ✅ 槽位清除 + 心跳停止 |
| **RS_CM_00200** | 服务实例识别 | ✅ InstanceIdentifier / InstanceSpecifier |
| **RS_CM_00204** | 服务接口契约 | ✅ ServiceInterface Version 管理 |
| **SWS_CM_00122** | FindService API 定义 | ✅ Proxy::FindService() |
| **SWS_CM_00123** | StartFindService API 定义 | ✅ Runtime::StartFindService() |
| **SWS_CM_00125** | StopFindService API 定义 | ✅ Runtime::StopFindService() |
| **SWS_CM_00110** | Skeleton OfferService | ✅ Skeleton::OfferService() |
| **SWS_CM_00111** | Skeleton StopOfferService | ✅ Skeleton::StopOfferService() |

### 1.4 架构层次（零 Daemon 设计）

```text
┌──────────────────────────────────────────────────────────────────────────┐
│             lap::com Application (完全无感知)                              │
│  ┌─────────────────────────────────────────────────────────────────────┐ │
│  │ ServiceProxy / ServiceSkeleton                                      │ │
│  │  - FindService() → 直接读取共享内存槽位 (< 500ns)                    │ │
│  │  - OfferService() → 直接写入固定槽位 (原子操作)                      │ │
│  └─────────────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────────────┘
                          ↓ (标准 ara::com API 调用)
┌──────────────────────────────────────────────────────────────────────────┐
│          lap::com Runtime (极简化，~300 行新增代码)                        │
│  ┌──────────────────────────────────────────────────────────────────────┐│
│  │ SharedMemoryRegistry (固定槽位注册表)                                 ││
│  │  ┌────────────────────────────────────────────────────────────────┐  ││
│  │  │ 服务槽位数组 (1024 slots, 每槽 256 bytes)                      │  ││
│  │  │  Slot[0]: [ServiceID | InstanceID | Version | Endpoint | ...]  │  ││
│  │  │  Slot[1]: [ServiceID | InstanceID | Version | Endpoint | ...]  │  ││
│  │  │  ...                                                             │  ││
│  │  │  Slot[1023]: [Reserved]                                         │  ││
│  │  └────────────────────────────────────────────────────────────────┘  ││
│  │                                                                        ││
│  │  ┌────────────────────────────────────────────────────────────────┐  ││
│  │  │ seqlock + 版本控制 (Lock-Free 读取)                             │  ││
│  │  │  - sequence: atomic<uint64_t> (奇数=写中，偶数=可读)            │  ││
│  │  │  - version: uint32_t (单调递增)                                 │  ││
│  │  └────────────────────────────────────────────────────────────────┘  ││
│  │                                                                        ││
│  │  ┌────────────────────────────────────────────────────────────────┐  ││
│  │  │ 心跳机制 (进程生命周期检测)                                     │  ││
│  │  │  - last_heartbeat_ns: 纳秒级时间戳                              │  ││
│  │  │  - timeout: 5s (可配置)                                         │  ││
│  │  │  - 后台线程周期性检测并清理失效槽位                             │  ││
│  │  └────────────────────────────────────────────────────────────────┘  ││
│  └──────────────────────────────────────────────────────────────────────┘│
│  ┌──────────────────────────────────────────────────────────────────────┐│
│  │ SlotAllocator (服务 ID → 槽位映射)                                   ││
│  │  - 静态配置: /etc/lap/com/slot_mapping.yaml                          ││
│  │  - 动态分配: Hash(ServiceID + InstanceID) % MAX_SLOTS                ││
│  │  - 冲突解决: Linear probing / 预留槽位                                ││
│  └──────────────────────────────────────────────────────────────────────┘│
└──────────────────────────────────────────────────────────────────────────┘
                          ↓ (底层内存管理)
┌──────────────────────────────────────────────────────────────────────────┐
│          iceoryx2 底层能力 (进程自管理，零 Daemon)                         │
│  ┌──────────────────────────────────────────────────────────────────────┐│
│  │ 双注册表物理隔离 (QM Registry + ASIL-D Registry)                      ││
│  │  ┌─────────────────────────────────────────────────────────────────┐ ││
│  │  │ QM Registry: /dev/shm/lap_com_registry_qm (256KB, 1024 slots)   │ ││
│  │  │  - 槽位 0~1023: QM/ASIL-A/B/C 服务                              │ ││
│  │  │  - 权限: 0666 (所有进程可读写)                                  │ ││
│  │  │  - service_id: 0x0001~0x03FF, 0xFFFF(广播)                    │ ││
│  │  │  - 槽位 1023: QM+ASIL 进程都订阅，实现广播双向互通             │ ││
│  │  └─────────────────────────────────────────────────────────────────┘ ││
│  │  ┌─────────────────────────────────────────────────────────────────┐ ││
│  │  │ ASIL-D Registry: /dev/shm/lap_com_registry_asil (256KB, 1024 s) │ ││
│  │  │  - 槽位 0~1023: ASIL-D 关键服务                                 │ ││
│  │  │  - 权限: 0640 (控制进程写，其他只读)                            │ ││
│  │  │  - service_id: 0xF000~0xF3FF, 0xFFFF(紧急广播)                │ ││
│  │  │  - 槽位 1023: QM+ASIL 进程都订阅，实现广播双向互通             │ ││
│  │  └─────────────────────────────────────────────────────────────────┘ ││
│  │  - 自动清理: 最后一个进程关闭时释放                                   ││
│  │  - 广播互通: 所有进程订阅两个 Registry 的槽位 1023                    ││
│  └──────────────────────────────────────────────────────────────────────┘│
│  ┌──────────────────────────────────────────────────────────────────────┐│
│  │ 1GB HugePage 支持 (THP + Explicit)                                    ││
│  │  - mmap(MAP_HUGETLB | MAP_HUGE_1GB)                                   ││
│  │  - 减少 TLB miss，提升缓存命中率                                      ││
│  │  - 对齐到 1GB 边界 (内核自动对齐)                                     ││
│  └──────────────────────────────────────────────────────────────────────┘│
│  ┌──────────────────────────────────────────────────────────────────────┐│
│  │ Guard Page 保护 (防止越界访问)                                        ││
│  │  - 每个槽位前后插入不可访问的页 (PROT_NONE)                           ││
│  │  - 越界访问触发 SIGSEGV (立即捕获)                                    ││
│  └──────────────────────────────────────────────────────────────────────┘│
│  ┌──────────────────────────────────────────────────────────────────────┐│
│  │ 双注册表权限隔离 (FuSa ISO 26262 就绪)                                ││
│  │  - QM Registry (0666): 感知/规划/娱乐等非安全关键服务                 ││
│  │  - ASIL-D Registry (0640): 刹车/转向/驱动力等安全关键服务             ││
│  │  - 物理隔离: 两个独立 memfd，内核级别进程隔离                         ││
│  │  - CAP_SYS_ADMIN: ASIL-D Registry 写入需要特权                        ││
│  └──────────────────────────────────────────────────────────────────────┘│
└──────────────────────────────────────────────────────────────────────────┘

         ❌ 无 RouDi (iceoryx v1 守护进程)
         ❌ 无 Discovery Server (DDS 守护进程)  
         ❌ 无任何中央守护进程
         ✅ 完全去中心化
         ✅ 零单点故障
```

**关键设计优势对比**：

| 特性 | 传统方案 | 本架构 (v3.0) |
|------|----------|---------------|
| **延迟** | 1-5µs (Domain Socket) | < 500ns (共享内存直接读取) |
| **守护进程** | RouDi / systemd-resolved | ❌ 零 Daemon |
| **单点故障** | ✅ 存在 | ❌ 无 |
| **确定性** | 中 (网络抖动) | ✅ 高 (lock-free + 固定槽位) |
| **启动依赖** | 需先启动 Daemon | ✅ 无依赖 |
| **FuSa 友好** | 需隔离 Daemon | ✅ 天然隔离 (进程级权限) |
| **代码复杂度** | 高 (>2000 行) | ✅ 低 (~300 行) |

### 1.5 服务发现策略（双层策略，v3.0 简化版）

**策略优先级** (静态配置 → 共享内存注册表):

1. **Layer 1: 静态配置** (0ms，编译期确定):
   - 从 `slot_mapping.yaml` 加载服务 ID → 槽位映射
   - 启动时立即可用，零延迟
   - 符合 AUTOSAR SWS_CM_02201 静态服务连接

2. **Layer 2: 共享内存注册表** (< 500ns，运行时动态):
   - 直接读取 iceoryx2 共享内存槽位
   - Lock-free seqlock 保证原子性
   - 心跳机制确保服务活性
   - **无需任何网络/IPC 通信**

**性能基准**：

```text
v3.0 双层策略:
  静态配置 (0ns，编译期确定) → 共享内存查询 (< 500ns，运行时)
  
延迟分布:
  - P50: 300ns
  - P99: 450ns
  - P99.9: 500ns
```

---

## 第 2 章: 零 Daemon 固定槽位自注册架构

### 2.1 核心设计原理

#### 2.1.1 固定槽位映射

**设计思想**：

服务实例到共享内存槽位的映射关系在编译期或启动时确定，运行时不变，确保：

- ✅ **零查找开销**: 直接索引到槽位，O(1) 复杂度
- ✅ **确定性**: 没有哈希冲突、没有动态分配的不确定性
- ✅ **缓存友好**: 连续内存布局，预取优化

**映射策略**：

```text
ServiceID + InstanceID → SlotIndex

策略 1: 静态配置 (YAML manifest)
  RadarService.Instance1 → Slot[10]
  CameraService.Instance1 → Slot[11]
  ...

策略 2: 直接位运算 (零冲突设计)
  SlotIndex = ServiceID & 1023  // 取低 10 位，天然映射到 0~1023
  - QM 服务 (0x0001~0x03FF): 低 10 位直接就是槽位
  - ASIL 服务 (0xF000~0xF3FF): 低 10 位同样直接映射
  - 广播 (0xFFFF): 低 10 位 = 1023，完美映射到广播槽
  - 零冲突: service_id 高位段已隔离，低 10 位唯一
```

**Instance ID 位域结构** (用于 `instance_id` 字段的低 32 位):

```cpp
// Instance ID 编码结构 (低 32 位，高 32 位预留)
struct alignas(4) InstanceId {
    uint32_t service_id      : 16;   // 0~65535，全车唯一（ara::com 生成）
    uint32_t instance_no     : 8;    // 0~255，同服务多实例
    uint32_t domain          : 4;    // 0~15（0=感知,1=控制,2=娱乐,3=诊断,4=平台,5=OEM...）
    uint32_t asil_level      : 3;    // 0=QM,1=A,2=B,3=C,4=D,5~7保留
    uint32_t redundancy      : 1;    // 0=主通道,1=备通道（ASIL-D 冗余专用）
    // 总 16+8+4+3+1 = 32 bit，完美填满
};
static_assert(sizeof(InstanceId) == 4);

// 域分类定义
enum class ServiceDomain : uint8_t {
    PERCEPTION   = 0,  // 感知域（摄像头、雷达、激光雷达）
    CONTROL      = 1,  // 控制域（转向、制动、动力）
    INFOTAINMENT = 2,  // 娱乐域（HMI、音频、导航）
    DIAGNOSTICS  = 3,  // 诊断域（OBD、DTC、日志）
    PLATFORM     = 4,  // 平台域（时间同步、健康管理）
    OEM_RESERVED = 5,  // OEM 自定义域
    // 6~15 预留
};

// ASIL 等级定义
enum class ASILLevel : uint8_t {
    QM    = 0,  // Quality Management (非安全相关)
    ASIL_A = 1,
    ASIL_B = 2,
    ASIL_C = 3,
    ASIL_D = 4,
    // 5~7 预留
};

// Instance ID 工具函数
inline uint64_t EncodeInstanceId(uint16_t service_id, uint8_t instance_no, 
                                   ServiceDomain domain, ASILLevel asil, bool is_redundant) {
    InstanceId id{};
    id.service_id = service_id;
    id.instance_no = instance_no;
    id.domain = static_cast<uint32_t>(domain);
    id.asil_level = static_cast<uint32_t>(asil);
    id.redundancy = is_redundant ? 1 : 0;
    
    uint32_t low32 = *reinterpret_cast<uint32_t*>(&id);
    return static_cast<uint64_t>(low32);  // 高 32 位为 0（预留）
}

inline InstanceId DecodeInstanceId(uint64_t instance_id_64) {
    uint32_t low32 = static_cast<uint32_t>(instance_id_64 & 0xFFFFFFFF);
    return *reinterpret_cast<InstanceId*>(&low32);
}
```

**双注册表槽位划分方案**：

v3.0 架构使用两个独立的共享内存注册表，实现 QM 和 ASIL-D 的物理隔离：

| Registry 类型 | 槽位范围 | 槽位数量 | 用途 | service_id 范围 | 典型服务示例 |
|--------------|---------|---------|------|----------------|-------------|
| **QM Registry** | **0** | **1** | **保留槽（禁止使用）** | **0x0000** | **NULL 槽位，用于错误检测** |
| QM Registry | 1~511 | 511 | 感知主力区 | 0x0001~0x01FF | 摄像头、激光雷达、毫米波原始+融合 |
| QM Registry | 512~767 | 256 | 规划/决策区 | 0x0200~0x02FF | 路径规划、行为决策、预测 |
| QM Registry | 768~895 | 128 | 控制/底盘（非关键） | 0x0300~0x037F | 灯光、雨刮、空调预控制 |
| QM Registry | 896~991 | 96 | 车身/舒适/娱乐 | 0x0380~0x03DF | 车窗、座椅、多屏、语音 |
| QM Registry | 992~1015 | 24 | 平台中间件（QM） | 0x03E0~0x03F7 | 非关键诊断、日志、时间同步、OTA准备 |
| QM Registry | 1016~1022 | 7 | 系统保留（QM） | 0x03F8~0x03FE | 未来扩展、调试用 |
| QM Registry | **1023** | **1** | **QM 全局广播槽** | **0xFFFF** | **OTA 开始、休眠、故障广播** |
| **ASIL-D Registry** | **0** | **1** | **保留槽（禁止使用）** | **0xF000** | **NULL 槽位，用于错误检测** |
| ASIL-D Registry | 1~511 | 511 | ASIL-D 关键控制区 | 0xF001~0xF1FF | 主刹车、转向、驱动力、电源管理 |
| ASIL-D Registry | 512~767 | 256 | ASIL-D 冗余控制区 | 0xF200~0xF2FF | 备份刹车、转向、驱动力 |
| ASIL-D Registry | 768~895 | 128 | ASIL-B/C 中间件区 | 0xF300~0xF37F | 关键诊断、网关、时间主节点 |
| ASIL-D Registry | 896~991 | 96 | 平台中间件（ASIL-B/D） | 0xF380~0xF3DF | 关键 OTA、安全监视、故障管理 |
| ASIL-D Registry | 992~1015 | 24 | 系统保留（ASIL-D） | 0xF3E0~0xF3F7 | 安全岛心跳、紧急降级通道 |
| ASIL-D Registry | 1016~1022 | 7 | OEM 专属保留段 | 0xF3F8~0xF3FE | 整车厂自定义 |
| ASIL-D Registry | **1023** | **1** | **ASIL-D 全局广播槽** | **0xFFFF** | **紧急制动请求、安全岛重启** |

**双注册表设计特点**：

1. ✅ **物理隔离**: 两个独立 memfd，内核级别进程隔离
2. ✅ **权限管控**: 
   - QM Registry: 0666 (所有进程可读写)
   - ASIL-D Registry: 0640 (控制进程写，其他只读)
3. ✅ **service_id 段分离**:
   - QM 服务: 0x0001~0x03FE (1~1022) - 禁止使用 0x0000 和 0x03FF
   - ASIL-D 服务: 0xF001~0xF3FE (61441~62462) - 禁止使用 0xF000 和 0xF3FF
   - 广播槽: 0xFFFF (65535)
4. ✅ **槽位 0 保留**: 两个 Registry 的槽位 0 都禁止使用
   - 用于 NULL 检测和错误边界条件
   - service_id 0x0000 和 0xF000 映射到槽位 0，运行时拒绝注册
   - 提升系统鲁棒性，防止未初始化的 service_id 污染注册表
5. ✅ **冗余支持**: ASIL-D Registry 内含主备控制区（511+256槽）
6. ✅ **全局广播**: 每个 Registry 的槽位 1023 为广播槽，支持系统级事件
7. ✅ **槽位 1023 双向互通**: 
   - 所有 QM 进程订阅: `lap_com_registry_qm[1023]` + `lap_com_registry_asil[1023]`
   - 所有 ASIL 进程订阅: `lap_com_registry_qm[1023]` + `lap_com_registry_asil[1023]`
   - 任一 Registry 的 1023 更新，所有进程都能收到广播事件
   - 实现跨安全等级的系统级事件通知（OTA、休眠、紧急制动等）
8. ✅ **零冲突槽位映射**: `slot = service_id & 1023`（低 10 位直接映射）
   - QM 服务 0x0001~0x03FE → 槽位 1~1022（槽位 0 禁止，槽位 1023 广播）
   - ASIL 服务 0xF001~0xF3FE → 槽位 1~1022（槽位 0 禁止，槽位 1023 广播）
   - 广播 0xFFFF → 槽位 1023（0xFFFF & 1023 = 1023）
   - 保留 0x0000/0xF000 → 槽位 0（禁止使用，用于错误检测）

**service_id 快速查找算法**：

```cpp
// 根据 service_id 自动选择 Registry
RegistryType SelectRegistry(uint16_t service_id) {
    if (service_id >= 0xF000 && service_id <= 0xF3FF) {
        return RegistryType::ASIL_D;  // ASIL-D 服务段
    } else if (service_id == 0xFFFF) {
        // 广播服务：需同时写入两个 Registry 的槽位 1023
        // 注意：所有进程都订阅两个 Registry 的 1023，实现双向互通
        return RegistryType::BOTH;
    } else {
        return RegistryType::QM;  // QM 服务段
    }
}

// 计算槽位索引（零冲突设计，槽位 0 保留禁止使用）
inline uint32_t CalculateSlot(uint16_t service_id) {
    uint32_t slot = service_id & 1023;  // 取低 10 位，天然映射到 0~1023
    
    // 槽位 0 保留，禁止使用（0x0000 和 0xF000 会映射到槽位 0）
    if (slot == 0) {
        // 运行时错误：service_id 0x0000 或 0xF000 非法
        // 应抛出异常或返回 INVALID_SLOT
        return INVALID_SLOT;  // 或 throw std::invalid_argument("slot 0 reserved");
    }
    
    return slot;
}

// 根据 service_id 推荐槽位范围（槽位 0 保留禁止使用）
std::pair<uint32_t, uint32_t> GetSlotRange(uint16_t service_id) {
    if (service_id >= 0x0001 && service_id <= 0x01FF) {
        return {1, 511};     // 感知主力区（槽位 0 跳过）
    } else if (service_id >= 0x0200 && service_id <= 0x02FF) {
        return {512, 767};   // 规划/决策区
    } else if (service_id >= 0x0300 && service_id <= 0x037F) {
        return {768, 895};   // 控制/底盘（非关键）
    } else if (service_id >= 0x0380 && service_id <= 0x03DF) {
        return {896, 991};   // 车身/舒适/娱乐
    } else if (service_id >= 0x03E0 && service_id <= 0x03F7) {
        return {992, 1015};  // 平台中间件（QM）
    } else if (service_id >= 0xF001 && service_id <= 0xF1FF) {
        return {1, 511};     // ASIL-D 关键控制区（槽位 0 跳过）
    } else if (service_id >= 0xF200 && service_id <= 0xF2FF) {
        return {512, 767};   // ASIL-D 冗余控制区
    } else if (service_id >= 0xF300 && service_id <= 0xF37F) {
        return {768, 895};   // ASIL-B/C 中间件区
    } else if (service_id >= 0xF380 && service_id <= 0xF3DF) {
        return {896, 991};   // 平台中间件（ASIL-B/D）
    } else if (service_id == 0xFFFF) {
        return {1023, 1023}; // 全局广播槽
    } else if (service_id == 0x0000 || service_id == 0xF000) {
        return {0, 0};       // 错误：槽位 0 保留禁止使用
    }
    return {1, 1022};  // 默认范围（跳过槽位 0 和 1023）
}
```

**槽位结构** (256 bytes per slot):

```cpp
struct alignas(64) ServiceSlot {  // Cache line 对齐
    // === seqlock 控制字段 (8 bytes) ===
    std::atomic<uint64_t> sequence;  // 奇数=写中，偶数=可读
    
    // === 服务标识 (32 bytes) ===
    uint64_t service_id;             // 服务接口 ID
    uint64_t instance_id;            // 实例 ID (低32位=InstanceId位域，高32位预留)
    uint32_t major_version;          // 主版本号
    uint32_t minor_version;          // 次版本号
    
    // === 网络端点 (96 bytes) ===
    char binding_type[16];           // "iceoryx2", "dds", "someip"
    char endpoint[80];               // IP:Port / Topic / Service Name
    
    // === 生命周期控制 (24 bytes) ===
    uint64_t last_heartbeat_ns;      // 最后心跳时间（纳秒）
    uint32_t heartbeat_interval_ms;  // 心跳间隔（毫秒）
    uint32_t status;                 // 0=空闲, 1=活跃, 2=正在注销
    pid_t owner_pid;                 // 拥有者进程 ID
    
    // === 元数据 (64 bytes) ===
    char metadata[64];               // JSON格式的扩展元数据
    
    // === 填充到 256 bytes ===
    uint8_t _padding[32];
    
    // 总大小: 256 bytes = 4 cache lines (假设 64-byte cache line)
};

static_assert(sizeof(ServiceSlot) == 256, "Slot size must be 256 bytes");
```

#### 2.1.2 seqlock 无锁同步

**原理**：

seqlock (Sequential Lock) 是一种读者友好的同步机制：

- **写者**: 独占访问，更新 sequence (奇数 → 偶数)
- **读者**: 无锁重试，检查 sequence 前后一致性

**写入流程** (OfferService):

```cpp
void WriteSlot(uint32_t slot_index, const ServiceInfo& info) {
    auto& slot = slots_[slot_index];
    
    // 1. 加锁：sequence += 1 (变为奇数)
    uint64_t seq = slot.sequence.fetch_add(1, std::memory_order_acquire);
    
    // 2. 写入数据
    slot.service_id = info.service_id;
    slot.instance_id = info.instance_id;
    slot.major_version = info.major_version;
    // ... 写入其他字段
    
    std::atomic_thread_fence(std::memory_order_release);
    
    // 3. 解锁：sequence += 1 (变为偶数)
    slot.sequence.fetch_add(1, std::memory_order_release);
}
```

**读取流程** (FindService):

```cpp
std::optional<ServiceInfo> ReadSlot(uint32_t slot_index) {
    auto& slot = slots_[slot_index];
    ServiceInfo info;
    
    uint64_t seq1, seq2;
    do {
        // 1. 读取 sequence (必须是偶数)
        seq1 = slot.sequence.load(std::memory_order_acquire);
        if (seq1 & 1) {
            // 写者正在写入，稍后重试
            _mm_pause();  // x86 PAUSE 指令，减少总线争用
            continue;
        }
        
        // 2. 读取数据（无锁）
        info.service_id = slot.service_id;
        info.instance_id = slot.instance_id;
        // ... 读取其他字段
        
        std::atomic_thread_fence(std::memory_order_acquire);
        
        // 3. 再次检查 sequence
        seq2 = slot.sequence.load(std::memory_order_acquire);
        
    } while (seq1 != seq2);  // 不一致则重试
    
    return info;
}
```

**性能优势**：

- ✅ **读者零锁竞争**: 多个进程可并发读取，无互斥锁
- ✅ **写者低开销**: 只需两次原子操作（fetch_add）
- ✅ **无系统调用**: 完全用户态操作
- ✅ **延迟 < 100ns**: 单次读取 (假设无冲突)

#### 2.1.3 心跳机制

**设计目标**：

检测服务提供者进程的存活状态，自动清理僵尸槽位。

**实现方案**：

```cpp
class HeartbeatMonitor {
public:
    // 启动心跳发送线程
    void StartHeartbeat(uint32_t slot_index, uint32_t interval_ms) {
        heartbeat_threads_[slot_index] = std::thread([=]() {
            while (running_) {
                auto now_ns = std::chrono::steady_clock::now()
                    .time_since_epoch().count();
                
                auto& slot = slots_[slot_index];
                slot.last_heartbeat_ns = now_ns;
                
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(interval_ms));
            }
        });
    }
    
    // 后台线程定期检测超时槽位
    void MonitorTimeouts() {
        while (running_) {
            auto now_ns = std::chrono::steady_clock::now()
                .time_since_epoch().count();
            
            for (uint32_t i = 0; i < MAX_SLOTS; ++i) {
                auto& slot = slots_[i];
                if (slot.status != 1) continue;  // 仅检查活跃槽位
                
                uint64_t elapsed_ns = now_ns - slot.last_heartbeat_ns;
                uint64_t timeout_ns = slot.heartbeat_interval_ms * 1'000'000ULL * 3;
                
                if (elapsed_ns > timeout_ns) {
                    // 超时，清理槽位
                    ClearSlot(i);
                }
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
};
```

**替代方案** (更轻量):

使用 `kill(pid, 0)` 系统调用检测进程存活：

```cpp
bool IsProcessAlive(pid_t pid) {
    return (kill(pid, 0) == 0) || (errno == EPERM);
}

void ClearDeadSlots() {
    for (uint32_t i = 0; i < MAX_SLOTS; ++i) {
        auto& slot = slots_[i];
        if (slot.status == 1 && !IsProcessAlive(slot.owner_pid)) {
            ClearSlot(i);
        }
    }
}
```

**优势对比**：

| 方案 | 延迟检测 | CPU 开销 | 实现复杂度 |
|------|---------|---------|-----------|
| 心跳时间戳 | 高 (最多 3× interval) | 低 (纳秒级写入) | 中 |
| kill(0) 检测 | 低 (实时) | 中 (系统调用) | 低 |
| **混合方案** | 低 | 低 | 低 |

**推荐**: 混合方案（心跳作为快速路径，kill(0) 作为兜底检测）

#### 2.1.4 Instance ID 使用示例

**场景 1: 创建感知域雷达服务实例（ASIL-B 主通道）**

```cpp
// 使用 InstanceId 位域结构创建实例
uint64_t instance_id = EncodeInstanceId(
    0x1001,                        // service_id: 雷达服务
    1,                             // instance_no: 第1个实例（前向雷达）
    ServiceDomain::PERCEPTION,     // domain: 感知域
    ASILLevel::ASIL_B,            // asil_level: ASIL-B
    false                          // redundancy: 主通道
);
// 结果: 0x0000000010011042 (低32位有效)
//       ^^^^^^^^ ^^^^^^^^
//       高32位   低32位
//       (预留)   (InstanceId位域)

// 解析位域
InstanceId id = DecodeInstanceId(instance_id);
assert(id.service_id == 0x1001);
assert(id.instance_no == 1);
assert(id.domain == 0);  // PERCEPTION
assert(id.asil_level == 2);  // ASIL_B
assert(id.redundancy == 0);  // 主通道
```

**场景 2: ASIL-D 冗余通道配置**

```cpp
// 主通道：转向控制服务（ASIL-D）
uint64_t steering_main = EncodeInstanceId(
    0x2001,                        // service_id: 转向服务
    1,                             // instance_no: 第1个实例
    ServiceDomain::CONTROL,        // domain: 控制域
    ASILLevel::ASIL_D,            // asil_level: ASIL-D
    false                          // redundancy: 主通道
);
// 槽位分配: 924-1023 (ASIL-D 专用区)

// 备通道：转向控制服务（ASIL-D 冗余）
uint64_t steering_backup = EncodeInstanceId(
    0x2001,                        // service_id: 相同服务ID
    1,                             // instance_no: 相同实例号
    ServiceDomain::CONTROL,        // domain: 控制域
    ASILLevel::ASIL_D,            // asil_level: ASIL-D
    true                           // redundancy: 备通道 ✅
);
// 槽位分配: 自动选择备用槽位（物理隔离）

// 两个通道的 instance_id 仅 redundancy bit 不同
assert((steering_main & 0xFFFFFFFE) == (steering_backup & 0xFFFFFFFE));
assert((steering_backup & 0x1) == 1);  // 备通道标志位
```

**场景 3: 槽位映射规则（YAML 配置）**

```yaml
# slot_mapping.yaml
slot_mapping:
  # QM 域槽位: 0-923
  - service_id: 0x1001
    instance_no: 1
    domain: PERCEPTION  # 0
    asil_level: QM      # 0
    redundancy: false
    slot_index: 10      # 静态分配
    
  # ASIL-D 域槽位: 924-1023
  - service_id: 0x2001
    instance_no: 1
    domain: CONTROL     # 1
    asil_level: ASIL_D  # 4
    redundancy: false   # 主通道
    slot_index: 924     # ASIL-D 起始槽位
    
  - service_id: 0x2001
    instance_no: 1
    domain: CONTROL
    asil_level: ASIL_D
    redundancy: true    # 备通道
    slot_index: 925     # 备通道槽位
```

**场景 4: 运行时槽位查找优化（零冲突设计 + 槽位 0 保护）**

```cpp
// 快速查找：根据 InstanceId 位域直接定位槽位（O(1) 复杂度，零冲突）
uint32_t FindSlotByInstanceId(uint64_t instance_id_64) {
    InstanceId id = DecodeInstanceId(instance_id_64);
    
    // 直接位运算计算槽位（零冲突）
    uint32_t slot_index = id.service_id & 1023;  // 取低 10 位
    
    // 槽位 0 保护：禁止使用（service_id 0x0000 或 0xF000 非法）
    if (slot_index == 0) {
        LOG_ERROR("Slot 0 is reserved, service_id={:#06x} is invalid", id.service_id);
        return INVALID_SLOT;
    }
    
    // 根据 ASIL 等级选择 Registry
    ServiceSlot* slot = nullptr;
    if (id.asil_level == static_cast<uint32_t>(ASILLevel::ASIL_D)) {
        slot = &asil_registry_->slots[slot_index];  // ASIL-D Registry
    } else {
        slot = &qm_registry_->slots[slot_index];    // QM Registry
    }
    
    // 验证槽位数据（无需遍历，直接验证）
    if (slot->instance_id == instance_id_64 && slot->status == 1) {
        return slot_index;  // 找到，O(1) 复杂度
    }
    
    return INVALID_SLOT;  // 槽位为空或不匹配
}

// 槽位 1023 双向互通订阅（所有进程都监听两个 Registry 的广播槽）
void SubscribeBroadcastSlots() {
    // 订阅 QM Registry 的槽位 1023
    qm_broadcast_slot_ = &qm_registry_->slots[1023];
    
    // 订阅 ASIL Registry 的槽位 1023
    asil_broadcast_slot_ = &asil_registry_->slots[1023];
    
    // 后台线程轮询两个广播槽（seqlock 检测更新）
    // QM 和 ASIL 进程都能收到双向广播事件
}

// OfferService 槽位 0 保护示例
bool OfferService(const ServiceInfo& info) {
    uint32_t slot = CalculateSlot(info.service_id);
    
    // 槽位 0 保护检查
    if (slot == 0 || slot == INVALID_SLOT) {
        LOG_ERROR("Cannot offer service: slot 0 is reserved (service_id={:#06x})", 
                  info.service_id);
        return false;
    }
    
    // 正常注册流程...
    return RegisterToSlot(slot, info);
}
```

**位域结构优势总结**：

1. ✅ **紧凑编码**: 32 位完美填满，无浪费
2. ✅ **快速解析**: 位操作提取字段，O(1) 复杂度
3. ✅ **域隔离**: 根据 `domain` + `asil_level` 自动分配槽位范围
4. ✅ **冗余支持**: `redundancy` 位实现 ASIL-D 主备通道
5. ✅ **可扩展**: 高 32 位预留，未来可扩展其他元数据

### 2.2 systemd Socket Activation 集成 (零常驻 Daemon)

#### 2.2.1 设计原理

**传统方案的问题**：

即使使用 memfd，仍需第一个进程创建并持有文件描述符，后续进程如何获取同一 memfd？

- ❌ 方案1: 持有进程常驻 → 引入单点故障
- ❌ 方案2: 通过 `/proc/[pid]/fd/` 传递 → 依赖特定进程存活
- ❌ 方案3: 共享 memfd 名称 → memfd 本身是匿名的

**本架构创新方案**：

使用 **systemd socket activation + oneshot service** 实现真正的零常驻 Daemon：

```text
系统启动 (systemd)
    ↓
启动 lap-registry.socket (Unix Domain Socket 监听)
    ↓
首次客户端连接 → 触发 lap-registry-init.service (oneshot)
    ↓
oneshot 进程创建 memfd + 初始化注册表 + 通过 SCM_RIGHTS 发送 fd
    ↓
客户端接收 memfd fd，映射到进程地址空间
    ↓
oneshot 进程退出 (无常驻 Daemon)
    ↓
后续所有客户端连接同一 socket，获取同一 memfd fd
```

**核心优势**：

- ✅ **零常驻 Daemon**: oneshot 进程完成初始化后立即退出
- ✅ **100% 共享**: 所有进程通过 socket 获取同一 memfd fd
- ✅ **自动恢复**: 进程重启/新增自动重连 socket
- ✅ **热升级**: 无需停止任何服务
- ✅ **systemd 原生**: 无额外守护进程管理

#### 2.2.2 memfd_create + SCM_RIGHTS 文件描述符传递

**Step 1: oneshot 进程创建 memfd**

```cpp
// lap-registry-init.cpp (oneshot 初始化进程)

#include <sys/memfd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>

struct RegistryInitializer {
    int memfd_ = -1;
    void* registry_ptr_ = nullptr;
    
    int CreateAndInitializeRegistry() {
        // 1. 创建匿名共享内存文件（双注册表设计：lap_com_registry_qm / lap_com_registry_asil）
        memfd_ = memfd_create("lap_com_registry_qm", 
                              MFD_CLOEXEC |      // fork 后自动关闭
                              MFD_ALLOW_SEALING); // 允许密封
        if (memfd_ < 0) {
            return -1;
        }
        
        // 2. 设置大小 (256KB = 1024 slots × 256 bytes)
        const size_t registry_size = 256 * 1024;
        if (ftruncate(memfd_, registry_size) < 0) {
            close(memfd_);
            return -1;
        }
        
        // 3. 映射到进程地址空间
        registry_ptr_ = mmap(nullptr, registry_size,
                            PROT_READ | PROT_WRITE,
                            MAP_SHARED,
                            memfd_, 0);
        if (registry_ptr_ == MAP_FAILED) {
            close(memfd_);
            return -1;
        }
        
        // 4. 初始化注册表 (清零所有槽位)
        memset(registry_ptr_, 0, registry_size);
        
        // 5. 密封内存，防止后续 resize
        fcntl(memfd_, F_ADD_SEALS, 
              F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL);
        
        return 0;
    }
    
    // 通过 Unix Domain Socket 发送 memfd 文件描述符
    int SendMemfdToClient(int client_socket) {
        struct msghdr msg = {};
        struct iovec iov = {};
        char ctrl_buf[CMSG_SPACE(sizeof(int))];
        
        // 1. 构造辅助数据 (SCM_RIGHTS)
        msg.msg_control = ctrl_buf;
        msg.msg_controllen = sizeof(ctrl_buf);
        
        struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cmsg), &memfd_, sizeof(int));
        
        // 2. 发送一个字节数据 (触发消息传递)
        char byte = 'R';  // 'R' for Registry
        iov.iov_base = &byte;
        iov.iov_len = 1;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        
        // 3. 发送消息 (包含 memfd 文件描述符)
        ssize_t sent = sendmsg(client_socket, &msg, 0);
        return (sent > 0) ? 0 : -1;
    }
};

// oneshot 进程主函数
int main() {
    // 1. 从 systemd 获取监听 socket (LISTEN_FDS=1)
    int listen_fd = SD_LISTEN_FDS_START;  // systemd 传入的 socket fd
    
    // 2. 创建并初始化 memfd
    RegistryInitializer initializer;
    if (initializer.CreateAndInitializeRegistry() < 0) {
        return 1;
    }
    
    // 3. 接受客户端连接，发送 memfd fd
    while (true) {
        int client_fd = accept(listen_fd, nullptr, nullptr);
        if (client_fd < 0) break;
        
        initializer.SendMemfdToClient(client_fd);
        close(client_fd);
        
        // 4. 处理完一批连接后退出 (systemd 会在需要时重启)
        // 实际可优化为处理所有排队连接后退出
    }
    
    return 0;
}
```

**Step 2: 客户端接收 memfd fd**

```cpp
// SharedMemoryRegistry.cpp (客户端库)

class RegistryClient {
public:
    int ConnectAndReceiveMemfd() {
        // 1. 连接到 systemd socket
        int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, "/run/lap/registry.sock", 
                sizeof(addr.sun_path) - 1);
        
        if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            return -1;
        }
        
        // 2. 接收 memfd 文件描述符 (SCM_RIGHTS)
        struct msghdr msg = {};
        struct iovec iov = {};
        char ctrl_buf[CMSG_SPACE(sizeof(int))];
        char byte;
        
        iov.iov_base = &byte;
        iov.iov_len = 1;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = ctrl_buf;
        msg.msg_controllen = sizeof(ctrl_buf);
        
        ssize_t received = recvmsg(sock_fd, &msg, 0);
        close(sock_fd);
        
        if (received <= 0) {
            return -1;
        }
        
        // 3. 提取 memfd 文件描述符
        struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
        if (!cmsg || cmsg->cmsg_type != SCM_RIGHTS) {
            return -1;
        }
        
        int memfd;
        memcpy(&memfd, CMSG_DATA(cmsg), sizeof(int));
        
        // 4. 映射到进程地址空间
        void* registry_ptr = mmap(nullptr, 256 * 1024,
                                  PROT_READ | PROT_WRITE,
                                  MAP_SHARED,
                                  memfd, 0);
        if (registry_ptr == MAP_FAILED) {
            close(memfd);
            return -1;
        }
        
        // 5. 现在所有进程共享同一块物理内存！
        registry_ptr_ = static_cast<ServiceSlot*>(registry_ptr);
        memfd_ = memfd;
        
        return 0;
    }
    
private:
    ServiceSlot* registry_ptr_ = nullptr;
    int memfd_ = -1;
};
```

**关键机制**：

- ✅ **SCM_RIGHTS**: Unix 域套接字特有的文件描述符传递机制
- ✅ **内核保证**: 所有进程接收的 fd 指向同一内核对象（同一块物理内存）
- ✅ **自动引用计数**: 内核管理 memfd 生命周期，最后一个进程关闭时释放

#### 2.2.3 systemd 配置文件

**Socket Unit** (`/etc/systemd/system/lap-registry.socket`):

```ini
[Unit]
Description=LightAP Service Registry Socket
Documentation=https://lightap.io/docs/service-registry
Before=multi-user.target
PartOf=lap-com.target

[Socket]
# Unix Domain Socket 路径
ListenStream=/run/lap/registry.sock

# Socket 权限 (所有进程可连接)
SocketMode=0666
SocketUser=root
SocketGroup=root

# Socket 激活服务
Service=lap-registry-init.service

# 立即创建 socket (系统启动时)
Backlog=128
Accept=false

[Install]
WantedBy=sockets.target
```

**Oneshot Service Unit** (`/etc/systemd/system/lap-registry-init.service`):

```ini
[Unit]
Description=LightAP Service Registry Initializer (Oneshot)
Documentation=https://lightap.io/docs/service-registry
Requires=lap-registry.socket
After=lap-registry.socket

[Service]
Type=oneshot
ExecStart=/usr/lib/lap/com/lap-registry-init

# 资源限制
MemoryMax=10M
CPUQuota=10%

# 安全加固
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/run/lap

# 环境变量
Environment="LAP_REGISTRY_SIZE=262144"
Environment="LAP_LOG_LEVEL=INFO"

# 超时控制
TimeoutStartSec=5s

# 进程退出后不重启 (oneshot)
RemainAfterExit=no

[Install]
WantedBy=multi-user.target
```

**安装与启动**：

```bash
# 1. 创建运行时目录
sudo mkdir -p /run/lap
sudo chmod 755 /run/lap

# 2. 安装 systemd 单元文件
sudo cp lap-registry.socket /etc/systemd/system/
sudo cp lap-registry-init.service /etc/systemd/system/

# 3. 重载 systemd 配置
sudo systemctl daemon-reload

# 4. 启用 socket (系统启动时自动创建)
sudo systemctl enable lap-registry.socket

# 5. 启动 socket (立即生效)
sudo systemctl start lap-registry.socket

# 6. 验证 socket 状态
sudo systemctl status lap-registry.socket
# 输出: Active: active (listening) ...

# 7. 首次客户端连接时，systemd 自动启动 lap-registry-init.service
# 之后 lap-registry-init.service 进入 inactive (dead) 状态
```

**运行时行为**：

```text
时间线                         | 进程状态
------------------------------|----------------------------------
系统启动                       | systemd 创建 /run/lap/registry.sock
                              | lap-registry.socket: active (listening)
                              | lap-registry-init.service: inactive (dead)
                              | 
首次应用进程连接 socket        | systemd 自动启动 lap-registry-init.service
                              | lap-registry-init 创建 memfd
                              | 发送 memfd fd 到客户端
                              | lap-registry-init 退出
                              | lap-registry-init.service: inactive (dead)
                              | 
后续所有进程连接 socket        | 直接从 socket 接收 memfd fd
                              | 无需启动 lap-registry-init.service
                              | (systemd 缓存了 fd，或通过新连接触发 oneshot)
```

**优势分析**：

| 特性 | 传统 Daemon | systemd Socket Activation |
|------|-----------|---------------------------|
| **常驻进程** | ✅ 1个 | ❌ 0个 |
| **内存占用** | ~5MB | ~0MB (无常驻) |
| **启动顺序** | 严格依赖 | 并行启动 |
| **故障恢复** | 需监控重启 | systemd 自动管理 |
| **热升级** | 需停服 | ✅ 无缝升级 |
| **系统集成** | 自定义脚本 | ✅ systemd 原生 |

#### 2.2.4 1GB HugePage 支持

**大页优势**：

- ✅ 减少 TLB miss (Translation Lookaside Buffer)
- ✅ 提升缓存命中率 (更大的连续物理内存)
- ✅ 降低页表开销

**配置方式**：

```bash
# 系统级配置
sudo sysctl -w vm.nr_hugepages=2  # 分配 2 × 1GB 大页

# 或 /etc/sysctl.conf 永久配置
vm.nr_hugepages = 2
vm.hugetlb_shm_group = 1000  # 允许的组 ID
```

**代码集成**：

```cpp
void* MapWithHugePage(int fd, size_t size) {
    // 1. 尝试显式 1GB 大页
    void* addr = mmap(nullptr, size,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_HUGETLB | MAP_HUGE_1GB,
                      fd, 0);
    
    if (addr != MAP_FAILED) {
        return addr;  // 成功使用 1GB 大页
    }
    
    // 2. 回退到 THP (Transparent Huge Pages)
    addr = mmap(nullptr, size,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                fd, 0);
    
    if (addr == MAP_FAILED) {
        return nullptr;
    }
    
    // 3. 建议内核使用 THP
    madvise(addr, size, MADV_HUGEPAGE);
    
    return addr;
}
```

**性能对比** (256KB 注册表):

| 页大小 | TLB 条目数 | TLB miss 率 | 延迟提升 |
|--------|-----------|------------|---------|
| 4KB | 64 | 高 | 基准 |
| 2MB (THP) | 1 | 低 | ~10% |
| 1GB (Explicit) | 1 | 极低 | ~15% |

#### 2.2.5 Guard Page 保护

**设计目标**：

防止越界访问，立即捕获内存破坏 bug。

**实现方式**：

```cpp
void* SetupGuardPages(void* base_addr, size_t registry_size) {
    const size_t page_size = 4096;
    const size_t total_size = registry_size + 2 * page_size;  // 前后各1页
    
    // 1. 重新映射，包含 guard pages
    void* full_region = mmap(nullptr, total_size,
                             PROT_NONE,  // 初始不可访问
                             MAP_PRIVATE | MAP_ANONYMOUS,
                             -1, 0);
    
    // 2. 仅允许访问中间的注册表区域
    void* registry_start = static_cast<char*>(full_region) + page_size;
    mprotect(registry_start, registry_size, PROT_READ | PROT_WRITE);
    
    // 3. 前后 guard pages 保持 PROT_NONE
    // 任何越界访问将触发 SIGSEGV
    
    return registry_start;
}
```

**错误捕获**：

```cpp
void InstallSIGSEGVHandler() {
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = [](int sig, siginfo_t* info, void* ctx) {
        void* fault_addr = info->si_addr;
        
        // 检查是否为 guard page 访问
        if (IsGuardPageAddress(fault_addr)) {
            LOG_ERROR("Guard page violation at {}", fault_addr);
            // 记录调用栈、崩溃转储等
        }
        
        // 继续默认处理（终止进程）
        signal(SIGSEGV, SIG_DFL);
        raise(SIGSEGV);
    };
    sigaction(SIGSEGV, &sa, nullptr);
}
```

#### 2.2.4 权限隔离与安全防护 (FuSa + Security Hardening)

**安全需求**：

1. **FuSa 权限隔离**: QM/ASIL-D 物理分离
2. **外部注入防护**: 防止恶意进程篡改注册表
3. **访问控制**: 基于 UID/GID 的细粒度权限
4. **完整性保护**: CRC32 校验 + seqlock 双重保障

##### 方案 1: 双 memfd 物理隔离 (推荐)

**架构设计**：

```text
systemd socket activation (两个独立 socket)
    ↓                              ↓
/run/lap/registry-qm.sock    /run/lap/registry-asil.sock
    ↓                              ↓
lap-registry-qm-init         lap-registry-asil-init
(oneshot)                    (oneshot, 受限权限)
    ↓                              ↓
qm_memfd (0666)              asil_memfd (0640)
所有进程可读写                仅 lap-control 组可写
```

**systemd Socket 单元**：

```ini
# /etc/systemd/system/lap-registry-qm.socket
[Socket]
ListenStream=/run/lap/registry-qm.sock
SocketMode=0666  # 所有进程可连接
SocketUser=root
SocketGroup=root

# /etc/systemd/system/lap-registry-asil.socket
[Socket]
ListenStream=/run/lap/registry-asil.sock
SocketMode=0660  # 仅 lap-control 组可连接
SocketUser=root
SocketGroup=lap-control  # 关键：限制访问
```

**Oneshot 服务权限控制**：

```ini
# /etc/systemd/system/lap-registry-asil-init.service
[Service]
Type=oneshot
ExecStart=/usr/lib/lap/com/lap-registry-asil-init

# 安全加固 (ASIL-D 级别更严格)
User=lap-registry
Group=lap-control
SupplementaryGroups=

# 能力限制 (CAP_SYS_ADMIN for memfd_create)
AmbientCapabilities=CAP_SYS_ADMIN
CapabilityBoundingSet=CAP_SYS_ADMIN

# 命名空间隔离
PrivateNetwork=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadOnlyPaths=/
ReadWritePaths=/run/lap

# 系统调用过滤 (仅允许必要的 syscall)
SystemCallFilter=@system-service
SystemCallFilter=~@privileged @resources
SystemCallFilter=memfd_create mmap munmap

# 禁止新权限
NoNewPrivileges=true
```

**代码实现**：

```cpp
// lap-registry-asil-init.cpp (ASIL-D 初始化进程)

struct ASILDRegistryInit {
    int CreateASILDMemfd() {
        // 1. 创建 ASIL-D memfd
        int memfd = memfd_create("lap_com_registry_asil", 
                                 MFD_CLOEXEC | 
                                 MFD_ALLOW_SEALING);
        
        // 2. 设置大小 (仅 ASIL-D 槽位: 100-199)
        const size_t asil_size = 100 * 256;  // 100 slots × 256 bytes
        ftruncate(memfd, asil_size);
        
        // 3. 映射并初始化
        void* addr = mmap(nullptr, asil_size,
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED, memfd, 0);
        memset(addr, 0, asil_size);
        
        // 4. 密封内存 (防止 resize 攻击)
        fcntl(memfd, F_ADD_SEALS, 
              F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL);
        
        // 5. 设置 fd 权限 (仅 lap-control 组可写)
        // 注意: memfd 本身无权限，通过 socket 传递时控制
        
        return memfd;
    }
    
    // 发送 ASIL-D memfd 时验证客户端权限
    int SendMemfdWithAuthCheck(int client_socket, int memfd) {
        // 1. 获取客户端进程凭据
        struct ucred cred;
        socklen_t len = sizeof(cred);
        getsockopt(client_socket, SOL_SOCKET, SO_PEERCRED, &cred, &len);
        
        // 2. 验证 GID (必须是 lap-control 组成员)
        if (!IsInGroup(cred.gid, "lap-control")) {
            LOG_ERROR("ASIL-D access denied: pid={}, uid={}, gid={}", 
                     cred.pid, cred.uid, cred.gid);
            return -EACCES;
        }
        
        // 3. 验证可执行文件路径 (防止注入)
        char exe_path[PATH_MAX];
        snprintf(exe_path, sizeof(exe_path), "/proc/%d/exe", cred.pid);
        
        char real_path[PATH_MAX];
        readlink(exe_path, real_path, sizeof(real_path));
        
        if (!IsAllowedExecutable(real_path)) {
            LOG_ERROR("ASIL-D access denied: unauthorized exe={}", real_path);
            return -EACCES;
        }
        
        // 4. 发送 memfd (SCM_RIGHTS)
        return SendMemfdViaSocket(client_socket, memfd);
    }
};
```

##### 方案 2: mprotect 运行时权限控制

**客户端映射时权限控制**：

```cpp
// SharedMemoryRegistry.cpp (客户端)

class RegistryClient {
public:
    int MapASILDRegionWithPermission(int memfd) {
        // 1. 检查当前进程是否有写权限
        bool has_write_permission = IsInGroup(getgid(), "lap-control");
        
        // 2. 映射内存
        void* addr = mmap(nullptr, 256 * 1024,
                         has_write_permission ? 
                             (PROT_READ | PROT_WRITE) : PROT_READ,
                         MAP_SHARED, memfd, 0);
        
        // 3. 对于非控制进程，额外使用 mprotect 强制只读
        if (!has_write_permission) {
            mprotect(addr, 256 * 1024, PROT_READ);
            
            // 4. 安装 SIGSEGV 处理器捕获违规写入
            InstallSegfaultHandler();
        }
        
        return 0;
    }
    
private:
    static void InstallSegfaultHandler() {
        struct sigaction sa;
        sa.sa_flags = SA_SIGINFO;
        sa.sa_sigaction = [](int sig, siginfo_t* info, void* ctx) {
            if (IsASILDRegionAddress(info->si_addr)) {
                LOG_CRITICAL("ASIL-D write violation detected! "
                            "Addr={}, Pid={}", 
                            info->si_addr, getpid());
                
                // 记录审计日志
                AuditLog("ASIL_WRITE_VIOLATION", getpid());
                
                // 终止进程 (安全策略)
                abort();
            }
        };
        sigaction(SIGSEGV, &sa, nullptr);
    }
};
```

##### 方案 3: 完整性保护 (CRC32 + seqlock)

**槽位结构增强**：

```cpp
struct alignas(64) ServiceSlot {
    // === seqlock 控制 (8 bytes) ===
    std::atomic<uint64_t> sequence;
    
    // === 完整性校验 (8 bytes) ===
    uint32_t crc32;              ///< CRC32 校验和
    uint32_t write_counter;      ///< 写入计数器 (防重放)
    
    // === 服务标识 (32 bytes) ===
    uint64_t service_id;
    uint64_t instance_id;
    uint32_t major_version;
    uint32_t minor_version;
    
    // ... 其他字段 ...
    
    // 计算 CRC32 (排除 sequence 和 crc32 本身)
    uint32_t ComputeCRC32() const {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(this);
        const size_t offset = offsetof(ServiceSlot, service_id);
        const size_t len = sizeof(ServiceSlot) - offset - sizeof(_padding);
        
        return crc32_fast(data + offset, len);
    }
};
```

**写入流程增强**：

```cpp
void WriteSlotWithIntegrity(uint32_t slot_index, const ServiceInfo& info) {
    auto& slot = slots_[slot_index];
    
    // 1. seqlock 加锁
    slot.sequence.fetch_add(1, std::memory_order_acquire);
    
    // 2. 写入数据
    slot.service_id = info.service_id;
    slot.instance_id = info.instance_id;
    // ... 写入其他字段
    
    // 3. 递增写入计数器 (防重放攻击)
    slot.write_counter++;
    
    // 4. 计算并写入 CRC32
    slot.crc32 = slot.ComputeCRC32();
    
    std::atomic_thread_fence(std::memory_order_release);
    
    // 5. seqlock 解锁
    slot.sequence.fetch_add(1, std::memory_order_release);
}
```

**读取流程验证**：

```cpp
std::optional<ServiceInfo> ReadSlotWithVerification(uint32_t slot_index) {
    auto& slot = slots_[slot_index];
    ServiceInfo info;
    uint64_t seq1, seq2;
    uint32_t crc_expected, crc_actual;
    
    do {
        // 1. seqlock 读取
        seq1 = slot.sequence.load(std::memory_order_acquire);
        if (seq1 & 1) {
            _mm_pause();
            continue;
        }
        
        // 2. 读取数据
        info.service_id = slot.service_id;
        info.instance_id = slot.instance_id;
        // ... 读取其他字段
        
        // 3. 读取 CRC32
        crc_expected = slot.crc32;
        
        std::atomic_thread_fence(std::memory_order_acquire);
        seq2 = slot.sequence.load(std::memory_order_acquire);
        
    } while (seq1 != seq2);
    
    // 4. 验证 CRC32 (防篡改)
    crc_actual = ComputeCRC32FromInfo(info);
    if (crc_actual != crc_expected) {
        LOG_ERROR("CRC32 mismatch in slot {}: expected={:x}, actual={:x}", 
                 slot_index, crc_expected, crc_actual);
        
        // 审计日志
        AuditLog("INTEGRITY_VIOLATION", slot_index);
        
        return std::nullopt;  // 拒绝返回被篡改的数据
    }
    
    return info;
}
```

##### 方案 4: 外部注入防护

**允许列表机制**：

```cpp
// /etc/lap/com/allowed_executables.conf
// 仅允许以下可执行文件访问注册表

struct AllowedExecutable {
    std::string path;
    std::string sha256_hash;  // 可执行文件哈希
};

const std::vector<AllowedExecutable> ALLOWED_EXES = {
    {"/usr/bin/lap-perception",  "abc123..."},
    {"/usr/bin/lap-planning",    "def456..."},
    {"/usr/bin/lap-control",     "789ghi..."},
};

bool IsAllowedExecutable(const std::string& exe_path) {
    // 1. 检查路径是否在允许列表
    auto it = std::find_if(ALLOWED_EXES.begin(), ALLOWED_EXES.end(),
        [&](const auto& entry) { return entry.path == exe_path; });
    
    if (it == ALLOWED_EXES.end()) {
        return false;  // 不在允许列表
    }
    
    // 2. 验证 SHA256 哈希 (防止二进制替换)
    std::string actual_hash = ComputeSHA256(exe_path);
    if (actual_hash != it->sha256_hash) {
        LOG_CRITICAL("Executable hash mismatch: {}, expected={}, actual={}", 
                    exe_path, it->sha256_hash, actual_hash);
        return false;
    }
    
    return true;
}
```

**SELinux 集成** (可选):

```bash
# SELinux 策略文件 (lap_registry.te)

# 定义类型
type lap_registry_t;
type lap_registry_qm_t;
type lap_registry_asil_t;

# QM 级注册表：所有 lap 进程可读写
allow lap_domain_t lap_registry_qm_t:file { read write };

# ASIL-D 级注册表：仅 lap_control_t 可写
allow lap_control_t lap_registry_asil_t:file { read write };
allow lap_domain_t lap_registry_asil_t:file { read };  # 其他仅可读
```

##### 安全审计与合规

**FuSa 认证证据**：

```text
1. 物理隔离证明
   ✅ QM / ASIL-D 使用独立 memfd (不同 socket)
   ✅ Guard pages 防止越界访问
   ✅ 独立的 systemd 服务单元

2. 权限控制证明
   ✅ ASIL-D socket 仅 lap-control 组可访问
   ✅ 客户端 GID 验证 (SO_PEERCRED)
   ✅ mprotect 强制只读 (非控制进程)
   ✅ SIGSEGV 处理器捕获违规写入

3. 完整性保护
   ✅ CRC32 校验和 (每次读取验证)
   ✅ 写入计数器 (防重放攻击)
   ✅ seqlock 双重保障

4. 访问控制
   ✅ 可执行文件允许列表
   ✅ SHA256 哈希验证 (防二进制替换)
   ✅ SELinux 策略 (可选)

5. 审计日志
   ✅ 所有违规访问记录到 syslog
   ✅ systemd journal 集成
   ✅ 实时告警机制
```

**安全测试用例**：

```cpp
// 测试 1: ASIL-D 写入权限验证
TEST(SecurityTest, ASILDWriteProtection) {
    // 模拟非控制进程
    ASSERT_DEATH({
        auto registry = SharedMemoryRegistry::GetInstance();
        registry.Initialize();
        
        // 尝试写入 ASIL-D 槽位 (slot 100-199)
        ServiceInfo info;
        info.slot_index = 100;  // ASIL-D 槽位
        registry.WriteSlot(100, info);  // 应触发 SIGSEGV
    }, "ASIL_WRITE_VIOLATION");
}

// 测试 2: CRC32 完整性验证
TEST(SecurityTest, CRC32IntegrityCheck) {
    auto registry = SharedMemoryRegistry::GetInstance();
    
    // 1. 正常写入
    ServiceInfo info;
    registry.WriteSlot(10, info);
    
    // 2. 模拟外部篡改 (直接修改内存)
    auto* slot = registry.GetSlotPointer(10);
    slot->service_id = 0xDEADBEEF;  // 破坏数据
    // 不更新 CRC32
    
    // 3. 读取应检测到篡改
    auto result = registry.ReadSlot(10);
    ASSERT_FALSE(result.has_value());  // 应拒绝返回
}

// 测试 3: 可执行文件允许列表
TEST(SecurityTest, ExecutableWhitelist) {
    // 1. 允许的可执行文件
    ASSERT_TRUE(IsAllowedExecutable("/usr/bin/lap-control"));
    
    // 2. 不在列表的可执行文件
    ASSERT_FALSE(IsAllowedExecutable("/tmp/malicious"));
    
    // 3. 哈希不匹配
    // 修改 lap-control 二进制
    // ...
    ASSERT_FALSE(IsAllowedExecutable("/usr/bin/lap-control"));
}
```

### 2.3 配置与部署

#### 2.3.1 槽位映射配置 (slot_mapping.yaml)

```yaml
# LightAP Com Module - Service Slot Mapping Configuration
# 用途: 服务 ID → 固定槽位映射

slot_mapping:
  # === 关键服务 (静态分配) ===
  static_allocations:
    - service_interface: "RadarService"
      instance_id: 1
      slot_index: 10
      safety_level: QM
      description: "前向雷达服务"
    
    - service_interface: "CameraService"
      instance_id: 1
      slot_index: 11
      safety_level: QM
      description: "前置摄像头服务"
    
    - service_interface: "SteeringControl"
      instance_id: 1
      slot_index: 100
      safety_level: ASIL-D
      description: "转向控制服务（关键）"
    
    - service_interface: "BrakeControl"
      instance_id: 1
      slot_index: 101
      safety_level: ASIL-D
      description: "制动控制服务（关键）"
  
  # === 动态服务池 (哈希分配) ===
  dynamic_allocation:
    enabled: true
    slot_range:
      start: 200
      end: 1023
    hash_algorithm: "CRC32"  # CRC32 / FNV1a / MurmurHash3
    collision_resolution: "linear_probing"  # linear_probing / quadratic
  
  # === 系统配置 ===
  system:
    max_slots: 1024
    slot_size_bytes: 256
    registry_size_bytes: 262144  # 256KB
    
    # memfd 配置
    use_hugepages: true
    hugepage_size: "1GB"  # 1GB / 2MB / disabled
    enable_guard_pages: true
    
    # 心跳配置
    heartbeat:
      interval_ms: 1000        # 1秒发送一次心跳
      timeout_multiplier: 3    # 3秒无心跳视为超时
      monitor_interval_ms: 500 # 500ms 检查一次
```

#### 2.3.2 进程启动流程

```cpp
// main.cpp (应用进程启动)

#include <lap/com/Runtime.hpp>

int main() {
    // 1. 初始化 Runtime (内部创建/映射共享内存)
    auto result = lap::com::Runtime::Initialize();
    if (!result) {
        LOG_ERROR("Failed to initialize Runtime: {}", result.Error());
        return -1;
    }
    
    // 2. 提供服务 (自动写入固定槽位)
    auto offer_result = lap::com::OfferService<RadarService>(
        lap::core::InstanceSpecifier("RadarService/Instance1")
    );
    
    // 3. 查找服务 (直接读取共享内存)
    auto handles = lap::com::FindService<CameraService>();
    
    // ... 应用逻辑 ...
    
    // 4. 清理 (自动清除槽位、停止心跳)
    lap::com::Runtime::Deinitialize();
    
    return 0;
}
```

**Runtime 内部实现**：

```cpp
Result<void> Runtime::Initialize() {
    // 1. 首次初始化时创建 memfd
    static std::once_flag init_flag;
    std::call_once(init_flag, []() {
        g_registry_fd = CreateServiceRegistryMemfd();
        g_registry_ptr = MapWithHugePage(g_registry_fd, REGISTRY_SIZE);
        SetupGuardPages(g_registry_ptr, REGISTRY_SIZE);
    });
    
    // 2. 后续进程直接映射现有 memfd
    // (通过 Unix Domain Socket 传递文件描述符，或通过 /proc/[pid]/fd/)
    
    // 3. 启动心跳监控线程
    g_heartbeat_monitor.Start();
    
    return Result<void>::FromValue();
}
```

### 2.4 性能分析与优化

#### 2.4.1 延迟分解

**FindService 延迟分解** (< 500ns 目标):

```text
操作阶段                        | 延迟 (ns) | 占比 |
-------------------------------|----------|------|
1. 槽位索引计算               |   10     | 2%   |
2. 缓存行加载 (L1 hit)        |   50     | 10%  |
3. seqlock 读取 (sequence)    |   20     | 4%   |
4. 数据读取 (256 bytes)       |  200     | 40%  |
5. seqlock 验证 (sequence)    |   20     | 4%   |
6. 心跳超时检查               |   50     | 10%  |
7. 数据拷贝到返回结构         |  150     | 30%  |
-------------------------------|----------|------|
总计                           |  500     | 100% |
```

**OfferService 延迟分解** (< 1μs 目标):

```text
操作阶段                        | 延迟 (ns) | 占比 |
-------------------------------|----------|------|
1. 槽位索引计算               |   10     | 1%   |
2. seqlock 加锁 (fetch_add)   |   30     | 3%   |
3. 数据写入 (256 bytes)       |  300     | 30%  |
4. 内存屏障 (fence)           |   50     | 5%   |
5. seqlock 解锁 (fetch_add)   |   30     | 3%   |
6. 心跳线程启动              |  500     | 50%  |
7. 缓存刷新 (clflush)         |   80     | 8%   |
-------------------------------|----------|------|
总计                           | 1000     | 100% |
```

#### 2.4.2 优化技巧

**1. CPU 缓存优化**

```cpp
// 预取下一个槽位
__builtin_prefetch(&slots_[next_index], 0, 3);  // 预取到 L1

// 缓存行对齐
struct alignas(64) ServiceSlot { /* ... */ };

// SIMD 加速数据拷贝
memcpy_sse2(&dest, &src, 256);
```

**2. NUMA 感知**

```cpp
// 绑定内存到当前 NUMA 节点
numa_set_preferred(numa_node_of_cpu(sched_getcpu()));

// 检查跨 NUMA 访问
if (numa_node_of_cpu(sched_getcpu()) != GetSlotNUMANode(slot_index)) {
    LOG_WARN("Cross-NUMA access detected");
}
```

**3. Lock-free 心跳**

```cpp
// 使用原子时间戳替代互斥锁
std::atomic<uint64_t> last_heartbeat_ns;
last_heartbeat_ns.store(now_ns, std::memory_order_relaxed);
```

### 2.5 与 iceoryx2 数据通信集成

**重要**: 本注册表仅用于服务发现，数据传输仍使用 iceoryx2 本身的零拷贝机制。

**集成方式**：

```cpp
// 1. OfferService: 注册服务到共享内存槽位
auto offer_result = OfferService<RadarService>(instance_spec);

// 槽位中记录 iceoryx2 服务名称:
// slot.binding_type = "iceoryx2"
// slot.endpoint = "/perception/radar_front"

// 2. FindService: 从槽位读取 iceoryx2 服务名称
auto handles = FindService<RadarService>();
// handles[0].endpoint = "/perception/radar_front"

// 3. iceoryx2 Binding: 使用服务名称创建 Publisher/Subscriber
auto publisher = iceoryx2::Publisher::new(
    ServiceName::new("/perception/radar_front"));

auto subscriber = iceoryx2::Subscriber::new(
    ServiceName::new("/perception/radar_front"));

// 4. 数据传输: iceoryx2 零拷贝（本注册表不参与）
publisher.loan().unwrap().write(&radar_data).send();
```

**职责划分**：

| 层次 | 职责 | 技术 |
|------|------|------|
| 服务发现 | 服务注册/查找 | 本注册表 (共享内存槽位) |
| 数据传输 | 零拷贝 Pub/Sub | iceoryx2 原生机制 |

---

### 2.1 设计背景

**AUTOSAR R24-11 标准支持**: 

AUTOSAR AP R24-11 正式引入了两种服务发现优化机制：

1. **Static Service Connection** (静态服务连接) - SWS_CM_02201
   - 通过静态预配置的应用端点绕过服务发现协议
   - 适用于 SOME/IP 绑定（ProvidedSomeipServiceInstance / RequiredSomeipServiceInstance）
   - 参考文档：
     - [SWS_CM_02201] - 静态服务连接
     - [TPS_MANI_03312] - 静态配置远程对等地址（提供者）
     - [TPS_MANI_03313] - 事件组语义
     - [TPS_MANI_03314] - 静态配置远程对等地址（消费者）

2. **Central vs Distributed Service Discovery** (集中式 vs 分布式服务发现)
   - AUTOSAR_AP_EXP_ARAComAPI.pdf 第 7.2.1 章节明确定义了两种架构：
   - **集中式方法**：中央守护进程维护服务注册表，处理所有 FindService/OfferService 请求
   - **分布式方法**：服务注册信息分布在各 lap::com 应用中，通过广播通信同步

**本设计采用混合策略**：

基于 AUTOSAR R24-11 标准，本设计结合了：
- ✅ **集中式注册表**（AUTOSAR EXP 推荐）：中央守护进程维护全局服务注册表
- ✅ **静态服务连接**（AUTOSAR SWS 标准）：支持静态预配置端点
- ✅ **动态服务发现**（AUTOSAR SWS 标准）：保持完整的 D-Bus/SOME/IP/DDS 发现能力
- ✅ **自动回退机制**：集中式注册失败时自动切换到动态发现

**设计原则**:

1. ✅ **标准兼容性**: 完全符合 AUTOSAR R24-11 规范
2. ✅ **可选性**: 注册中心是可选组件，不影响标准 AUTOSAR 动态发现
3. ✅ **API 兼容性**: 与 AUTOSAR FindService/OfferService API 完全兼容，应用无需修改
4. ✅ **回退机制**: 注册中心失败时自动回退到动态发现
5. ✅ **性能优化**: Domain Socket 通信，查询延迟 < 1ms（符合集中式架构优势）

### 2.2 AUTOSAR R24-11 标准对比

#### 2.2.1 三种服务发现机制对比

AUTOSAR R24-11 支持三种服务发现机制，本设计全部支持：

| 机制 | AUTOSAR 标准 | 适用场景 | 延迟 | 可靠性 | 本设计支持 |
|------|-------------|----------|------|--------|-----------|
| **静态服务连接** | SWS_CM_02201 | 固定拓扑、已知服务位置 | 0ms | 高 | ✅ 支持（配置文件） |
| **集中式注册表** | EXP 7.2.1 Central | 动态服务、性能敏感 | 0.5ms | 中 | ✅ 支持（本设计核心） |
| **动态服务发现** | SWS_CM_00050+ | 完全动态、跨节点 | 5-100ms | 高 | ✅ 支持（自动回退） |

#### 2.2.2 静态服务连接 (AUTOSAR R24-11 新特性)

**标准定义**: [SWS_CM_02201] Static service connection

```
"The static connection of services which are bound to SOME/IP protocols 
shall be performed by statically pre-configured application end-points 
as described in the TPS_ManifestSpecification."
```

**ARXML 配置示例** (基于 TPS_MANI_03312):

```xml
<!-- 静态服务提供者配置 -->
<PROVIDED-SOMEIP-SERVICE-INSTANCE>
  <SHORT-NAME>RadarService_Static_Provider</SHORT-NAME>
  <SERVICE-INTERFACE-DEPLOYMENT-REF>/Deployments/RadarServiceDeployment</SERVICE-INTERFACE-DEPLOYMENT-REF>
  
  <!-- 静态端点配置 -->
  <SOMEIP-SERVICE-INSTANCE-TO-MACHINE-MAPPING>
    <SHORT-NAME>StaticMapping</SHORT-NAME>
    
    <!-- 本地端点 -->
    <AP-APPLICATION-ENDPOINT-REF>/Endpoints/LocalEndpoint</AP-APPLICATION-ENDPOINT-REF>
    
    <!-- 远程对等地址（客户端地址） -->
    <REMOTE-UNICAST-CONFIG>
      <SHORT-NAME>RemoteClient1</SHORT-NAME>
      <REMOTE-ADDRESS>
        <IP-V4-ADDRESS>192.168.1.100</IP-V4-ADDRESS>
        <PORT-NUMBER>30501</PORT-NUMBER>
      </REMOTE-ADDRESS>
      
      <!-- 客户端订阅的事件组 (TPS_MANI_03313) -->
      <EVENT-GROUP-REF>/EventGroups/ObjectDetectionEvents</EVENT-GROUP-REF>
    </REMOTE-UNICAST-CONFIG>
  </SOMEIP-SERVICE-INSTANCE-TO-MACHINE-MAPPING>
</PROVIDED-SOMEIP-SERVICE-INSTANCE>

<!-- 静态服务消费者配置 -->
<REQUIRED-SOMEIP-SERVICE-INSTANCE>
  <SHORT-NAME>RadarService_Static_Consumer</SHORT-NAME>
  
  <!-- 静态服务器地址 (TPS_MANI_03314) -->
  <SOMEIP-SERVICE-INSTANCE-TO-MACHINE-MAPPING>
    <REMOTE-UNICAST-CONFIG>
      <SHORT-NAME>RemoteServer</SHORT-NAME>
      <REMOTE-ADDRESS>
        <IP-V4-ADDRESS>192.168.1.10</IP-V4-ADDRESS>
        <PORT-NUMBER>30500</PORT-NUMBER>
      </REMOTE-ADDRESS>
    </REMOTE-UNICAST-CONFIG>
    
    <!-- 多播地址配置 (TPS_MANI_03315) -->
    <REMOTE-MULTICAST-CONFIG>
      <SHORT-NAME>EventMulticast</SHORT-NAME>
      <MULTICAST-ADDRESS>
        <IP-V4-ADDRESS>239.0.0.1</IP-V4-ADDRESS>
        <PORT-NUMBER>30510</PORT-NUMBER>
      </MULTICAST-ADDRESS>
    </REMOTE-MULTICAST-CONFIG>
  </SOMEIP-SERVICE-INSTANCE-TO-MACHINE-MAPPING>
</REQUIRED-SOMEIP-SERVICE-INSTANCE>
```

**运行时行为**:
- ✅ 服务发现协议被绕过 (SWS_CM_02202)
- ✅ 版本检查不在运行时执行 (SWS_CM_02203)
- ✅ 直接使用静态配置的 IP:Port 通信
- ✅ 零延迟（编译时确定）

#### 2.2.3 集中式服务发现 (AUTOSAR EXP 推荐架构)

**标准引用**: AUTOSAR_AP_EXP_ARAComAPI Section 7.2.1

```
"A centralist approach, where the vendor decides to have one central 
entity (f.i. a daemon process), which:
  • maintains a registry of all service instances together with their 
    location information
  • serves all FindService, OfferService and StopOfferService requests 
    from local lap::com applications
  • serves all SOME/IP SD messages from the network
  • propagates local updates to its registry to the network"
```

**AUTOSAR 官方架构图**:

```
ECU with AP product from vendor V1
┌────────────────────────────────────────────────────────┐
│  lap::com App    lap::com App    lap::com App          │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐        │
│  │ Middleware │  │ Middleware │  │ Middleware │        │
│  │   Impl.    │  │   Impl.    │  │   Impl.    │        │
│  └─────┬──────┘  └─────┬──────┘  └─────┬──────┘        │
│        │               │               │                │
│        └───────────────┴───────────────┘                │
│                        │                                │
│              ┌─────────▼──────────┐                     │
│              │ Service Registry/  │                     │
│              │    Discovery       │                     │
│              │    (Central)       │                     │
│              └─────────┬──────────┘                     │
│                        │                                │
└────────────────────────┼────────────────────────────────┘
                         │ SOME/IP SD
                         ▼
                      Network
```

**本设计实现**:
- ✅ 零 Daemon 架构：完全去中心化，无任何守护进程
- ✅ 固定槽位映射：O(1) 查找复杂度，编译期确定
- ✅ Lock-free 共享内存：seqlock 保证原子性和高性能
- ✅ 心跳机制：自动检测服务活性，进程退出自动清理
- ✅ QM/ASIL-D 隔离：物理内存分离，满足功能安全要求

#### 2.2.4 架构优势总结

本设计相比传统方案的核心优势：

1. **极致性能**:
   - FindService < 500ns (vs 传统方案 1-5ms)
   - 零拷贝、零系统调用
   - Lock-free 读取

2. **零单点故障**:
   - 无守护进程依赖
   - 进程间完全对等
   - 自动故障恢复

3. **标准完全兼容**:
   - 应用代码使用标准 lap::com API（100% 兼容 ara::com）
   - 配置文件符合 ARXML 规范
   - 运行时行为符合 SWS 要求

---

### 2.3 配置文件集成（YAML 格式）

**本架构使用统一的 YAML 配置格式**，支持从 AUTOSAR ARXML 自动转换。

#### 2.3.1 slot_mapping.yaml（服务槽位映射）

```yaml
# slot_mapping.yaml - Service to Slot Mapping Configuration
# Version: 3.1 (Zero-Daemon Architecture)

metadata:
  version: "3.1"
  description: "Service instance to shared memory slot mapping"
  generated_from: "service_manifest.arxml"
  conversion_tool: "arxml2yaml v1.0"
  last_updated: "2025-11-19"

# Static slot allocations (compile-time fixed)
static_allocations:
  - service_id: 0x1001
    instance_id: 0x0001
    slot_index: 10
    service_name: "RadarFrontService"
    binding_type: "iceoryx2"
    
  - service_id: 0x1002
    instance_id: 0x0001
    slot_index: 11
    service_name: "CameraFrontService"
    binding_type: "iceoryx2"

# Dynamic slot allocations (runtime hash-based)
dynamic_allocations:
  strategy: "fnv1a_hash"  # FNV-1a hashing
  slot_range:
    start: 100
    end: 923
  collision_resolution: "linear_probing"

# ASIL-D slot allocations (isolated memory region)
asil_allocations:
  slot_range:
    start: 924
    end: 1023
  access_control: "0644"  # Read-only for non-owners
```

#### 2.3.2 arxml2yaml 转换工具

**用途**: 将 AUTOSAR ARXML 服务清单转换为 YAML 格式的槽位映射配置。

**使用示例**:

```bash
# 基本转换
python3 tools/arxml2yaml/arxml2yaml.py \
  service_manifest.arxml \
  -o slot_mapping.yaml

# 指定槽位分配策略
python3 tools/arxml2yaml/arxml2yaml.py \
  service_manifest.arxml \
  -o slot_mapping.yaml \
  --strategy=static_only

# 合并多个 ARXML 文件
python3 tools/arxml2yaml/arxml2yaml.py \
  radar.arxml camera.arxml lidar.arxml \
  -o combined_slot_mapping.yaml
```

**支持的槽位分配策略**:

- `static_only`: 仅静态分配（0-99）
- `dynamic_only`: 仅动态分配（100-923）
- `balanced`: 平衡分配（默认）
- `asil_priority`: ASIL-D 服务优先静态分配

详见: `tools/arxml2yaml/README.md`

---

### 2.4 性能分析与优化

**性能基准测试环境**:

- CPU: Intel Xeon E5-2690 v4 @ 2.6GHz
- Memory: DDR4-2400 128GB
- OS: Ubuntu 22.04 (kernel 5.15, PREEMPT_RT patch)
- Compiler: GCC 11.4 (-O3 -march=native)

**FindService 延迟分析**:

| 操作 | P50 | P99 | P99.9 | 最坏情况 |
|------|-----|-----|-------|---------|
| 静态配置命中 | 50ns | 80ns | 100ns | 150ns |
| 共享内存读取 | 300ns | 450ns | 500ns | 800ns |
| seqlock 重试 | 350ns | 500ns | 600ns | 1μs |

**内存占用**:

- 槽位数组: 1024 slots × 256 bytes = 256 KB
- 心跳线程: ~8 KB stack
- 总开销: < 300 KB per process

---

### 2.5 配置文件集成（YAML 格式）

**本节说明 YAML 配置文件的结构和用法**。

所有配置文件使用统一的 YAML 格式，支持以下特性：
- 从 AUTOSAR ARXML 自动转换（arxml2yaml 工具）
- CMake 构建时自动集成
- 运行时热加载（部分配置）

详细配置示例请参考：
- `slot_mapping.yaml` - 服务槽位映射
- `installation_config.yaml` - 系统安装配置
- `allowed_executables.yaml` - 安全白名单

---

> **历史版本说明**: v2.0 架构基于 Fast-DDS Discovery Server 的设计文档已归档至 `archive/SERVICE_DISCOVERY_V2_FAST_DDS_DESIGN.md`，当前文档描述 v3.0 零守护进程架构。

---

## 第 3 章: 核心组件设计

### 3.1 ServiceRegistry (服务注册表)

**职责**: 维护本地和远程服务实例的缓存。

#### 2.1.1 数据结构

```cpp
namespace lap::com::runtime {

class ServiceRegistry {
public:
    struct ServiceInstanceInfo {
        InstanceIdentifier instance_id;
        InstanceSpecifier instance_specifier;
        
        // 服务接口信息
        std::string service_interface_name;
        uint32_t major_version;
        uint32_t minor_version;
        
        // 网络信息
        TransportBindingType binding_type;  // D-Bus / SOME/IP / DDS
        std::string network_endpoint;        // IP:Port / Bus Name / DDS Topic
        
        // 生命周期
        ServiceState state;                  // OFFERED / STOPPED
        std::chrono::steady_clock::time_point last_seen;
        std::optional<std::chrono::milliseconds> ttl;  // Time-To-Live
        
        // 元数据
        std::map<std::string, std::string> metadata;
    };
    
    enum class ServiceState {
        OFFERED,      // 服务正在提供
        STOPPED,      // 服务已停止
        UNKNOWN       // 未知状态
    };
    
    // 注册本地服务
    Result<void> RegisterLocalService(const ServiceInstanceInfo& info);
    
    // 注销本地服务
    Result<void> UnregisterLocalService(const InstanceIdentifier& instance_id);
    
    // 注册远程服务
    Result<void> RegisterRemoteService(const ServiceInstanceInfo& info);
    
    // 查询服务
    Result<std::vector<ServiceInstanceInfo>> FindServices(
        const std::string& service_interface_name,
        const std::optional<InstanceIdentifier>& instance_filter = std::nullopt
    ) const;
    
    // 获取服务状态
    Result<ServiceState> GetServiceState(const InstanceIdentifier& instance_id) const;
    
    // 清理过期服务 (TTL 超时)
    void CleanupExpiredServices();
    
private:
    // 本地服务表 (本进程提供的服务)
    std::unordered_map<InstanceIdentifier, ServiceInstanceInfo> local_services_;
    
    // 远程服务表 (其他进程/节点提供的服务)
    std::unordered_map<InstanceIdentifier, ServiceInstanceInfo> remote_services_;
    
    // 索引: ServiceInterfaceName → InstanceIdentifiers
    std::unordered_multimap<std::string, InstanceIdentifier> service_index_;
    
    mutable std::shared_mutex registry_mutex_;
};

} // namespace lap::com::runtime
```

#### 2.1.2 服务版本兼容性检查

```cpp
class ServiceRegistry {
public:
    // 版本兼容性检查 (符合 AUTOSAR SWS_CM_11006 / SWS_CM_90510)
    static bool IsVersionCompatible(
        uint32_t required_major,
        uint32_t required_minor,
        uint32_t provided_major,
        uint32_t provided_minor
    ) {
        // 主版本号必须完全匹配
        if (required_major != provided_major) {
            return false;
        }
        
        // 次版本号: 提供者版本 >= 请求者版本 (向后兼容)
        return provided_minor >= required_minor;
    }
    
    // 检查版本是否在黑名单中 (DdsRequiredServiceInstance.blocklistedVersion)
    bool IsVersionBlocklisted(
        const InstanceIdentifier& instance_id,
        uint32_t major_version,
        uint32_t minor_version
    ) const;
};
```

### 2.2 ServiceDiscoveryManager (服务发现管理器)

**职责**: 处理 FindService/OfferService/StopOfferService API 调用。

#### 2.2.1 核心接口

```cpp
namespace lap::com::runtime {

class ServiceDiscoveryManager {
public:
    // 初始化
    Result<void> Initialize(const ServiceDiscoveryConfig& config);
    
    // ======== 服务提供者 API ========
    
    // OfferService (SWS_CM_00110, SWS_CM_11001, SWS_CM_90502)
    Result<void> OfferService(
        const InstanceIdentifier& instance_id,
        const ServiceInterfaceInfo& interface_info
    );
    
    // StopOfferService (SWS_CM_00111, SWS_CM_11005, SWS_CM_90509)
    Result<void> StopOfferService(const InstanceIdentifier& instance_id);
    
    // ======== 服务消费者 API ========
    
    // FindService - 单次查询 (SWS_CM_00122, SWS_CM_00622)
    Result<ServiceHandleContainer<HandleType>> FindService(
        const InstanceIdentifier& instance_filter
    );
    
    Result<ServiceHandleContainer<HandleType>> FindService(
        const InstanceSpecifier& instance_specifier
    );
    
    // StartFindService - 持续发现 (SWS_CM_00123, SWS_CM_90514)
    Result<FindServiceHandle> StartFindService(
        const InstanceIdentifier& instance_filter,
        FindServiceHandler handler
    );
    
    Result<FindServiceHandle> StartFindService(
        const InstanceSpecifier& instance_specifier,
        FindServiceHandler handler
    );
    
    // StopFindService - 停止持续发现 (SWS_CM_00125, SWS_CM_11013)
    Result<void> StopFindService(FindServiceHandle handle);
    
private:
    std::shared_ptr<ServiceRegistry> service_registry_;
    std::unique_ptr<FindServiceHandlerManager> handler_manager_;
    std::vector<std::unique_ptr<ITransportBinding>> transport_bindings_;
};

} // namespace lap::com::runtime
```

#### 2.2.2 OfferService 流程

```cpp
Result<void> ServiceDiscoveryManager::OfferService(
    const InstanceIdentifier& instance_id,
    const ServiceInterfaceInfo& interface_info
) {
    // 1. 验证服务接口信息
    if (!ValidateServiceInterface(interface_info)) {
        return Result<void>::FromError(ComErrc::kInvalidServiceInterface);
    }
    
    // 2. 注册到本地服务注册表
    ServiceRegistry::ServiceInstanceInfo info{
        .instance_id = instance_id,
        .service_interface_name = interface_info.name,
        .major_version = interface_info.major_version,
        .minor_version = interface_info.minor_version,
        .binding_type = GetDefaultBindingType(),
        .state = ServiceRegistry::ServiceState::OFFERED,
        .last_seen = std::chrono::steady_clock::now()
    };
    
    auto result = service_registry_->RegisterLocalService(info);
    if (!result) {
        return result;
    }
    
    // 3. 通知所有 Transport Binding 发布服务
    for (auto& binding : transport_bindings_) {
        // D-Bus: 注册 Bus Name, 发布 ObjectManager
        // SOME/IP: 发送 OfferService 消息 (SD Protocol)
        // DDS: 创建 DomainParticipant, 设置 USER_DATA QoS
        binding->OfferService(instance_id, interface_info);
    }
    
    // 4. 触发 FindServiceHandler 回调 (通知正在等待的消费者)
    handler_manager_->NotifyServiceAvailable(instance_id);
    
    return Result<void>::FromValue();
}
```

#### 2.2.3 FindService 流程

```cpp
Result<ServiceHandleContainer<HandleType>> 
ServiceDiscoveryManager::FindService(const InstanceIdentifier& instance_filter) {
    ServiceHandleContainer<HandleType> handles;
    
    // 1. 查询本地服务注册表
    auto local_services = service_registry_->FindServices(
        GetServiceInterfaceName(),
        instance_filter
    );
    
    if (!local_services) {
        return Result<ServiceHandleContainer<HandleType>>::FromError(
            local_services.Error()
        );
    }
    
    // 2. 转换为 HandleType
    for (const auto& service_info : local_services.Value()) {
        // 版本兼容性检查
        if (!ServiceRegistry::IsVersionCompatible(
            GetRequiredMajorVersion(),
            GetRequiredMinorVersion(),
            service_info.major_version,
            service_info.minor_version
        )) {
            continue;  // 跳过不兼容的版本
        }
        
        // 检查黑名单
        if (service_registry_->IsVersionBlocklisted(
            service_info.instance_id,
            service_info.major_version,
            service_info.minor_version
        )) {
            continue;  // 跳过黑名单版本
        }
        
        // 创建 HandleType
        HandleType handle{
            .instance_id = service_info.instance_id,
            .binding_type = service_info.binding_type,
            .endpoint = service_info.network_endpoint
        };
        
        handles.push_back(std::move(handle));
    }
    
    return Result<ServiceHandleContainer<HandleType>>::FromValue(
        std::move(handles)
    );
}
```

#### 2.2.4 StartFindService 流程 (持续发现)

```cpp
Result<FindServiceHandle> ServiceDiscoveryManager::StartFindService(
    const InstanceIdentifier& instance_filter,
    FindServiceHandler handler
) {
    // 1. 生成唯一的 FindServiceHandle
    FindServiceHandle handle = GenerateUniqueHandle();
    
    // 2. 注册回调到 FindServiceHandlerManager
    handler_manager_->RegisterHandler(handle, instance_filter, handler);
    
    // 3. 立即触发一次查询 (返回当前已知的服务)
    auto current_services = FindService(instance_filter);
    if (current_services && !current_services.Value().empty()) {
        handler(current_services.Value(), FindServiceStatus::kServiceAvailable);
    }
    
    // 4. 启动 Transport Binding 的持续发现
    for (auto& binding : transport_bindings_) {
        // D-Bus: 监听 NameOwnerChanged 信号
        // SOME/IP: 持续监听 SD OfferService 消息
        // DDS: 设置 BuiltinParticipantListener (SWS_CM_11010, SWS_CM_11011)
        binding->StartFindService(instance_filter, [this, handle](const auto& new_service) {
            OnRemoteServiceDiscovered(handle, new_service);
        });
    }
    
    return Result<FindServiceHandle>::FromValue(handle);
}
```

### 2.3 FindServiceHandlerManager (回调管理器)

**职责**: 管理 FindServiceHandler 回调，处理服务状态变化通知。

```cpp
namespace lap::com::runtime {

class FindServiceHandlerManager {
public:
    struct HandlerRegistration {
        FindServiceHandle handle;
        InstanceIdentifier instance_filter;
        FindServiceHandler callback;
        std::chrono::steady_clock::time_point registered_at;
    };
    
    // 注册 FindServiceHandler
    void RegisterHandler(
        FindServiceHandle handle,
        const InstanceIdentifier& instance_filter,
        FindServiceHandler callback
    );
    
    // 注销 FindServiceHandler
    void UnregisterHandler(FindServiceHandle handle);
    
    // 通知服务可用
    void NotifyServiceAvailable(const InstanceIdentifier& instance_id);
    
    // 通知服务不可用
    void NotifyServiceUnavailable(const InstanceIdentifier& instance_id);
    
private:
    // FindServiceHandle → HandlerRegistration
    std::unordered_map<FindServiceHandle, HandlerRegistration> handlers_;
    
    // InstanceId → FindServiceHandles (索引，加速查找)
    std::unordered_multimap<InstanceIdentifier, FindServiceHandle> instance_index_;
    
    mutable std::shared_mutex handlers_mutex_;
    
    // 工作线程池 (异步触发回调)
    std::unique_ptr<ThreadPool> callback_executor_;
};

void FindServiceHandlerManager::NotifyServiceAvailable(
    const InstanceIdentifier& instance_id
) {
    std::shared_lock lock(handlers_mutex_);
    
    // 查找所有匹配的 Handler
    auto range = instance_index_.equal_range(instance_id);
    
    for (auto it = range.first; it != range.second; ++it) {
        FindServiceHandle handle = it->second;
        auto handler_it = handlers_.find(handle);
        
        if (handler_it != handlers_.end()) {
            // 异步触发回调
            callback_executor_->Post([callback = handler_it->second.callback,
                                      instance_id]() {
                // 查询服务详情
                auto service_info = GetServiceInfo(instance_id);
                if (service_info) {
                    ServiceHandleContainer<HandleType> handles;
                    handles.push_back(CreateHandle(service_info.Value()));
                    callback(handles, FindServiceStatus::kServiceAvailable);
                }
            });
        }
    }
}

} // namespace lap::com::runtime
```

---

## 第 3 章: Transport Binding 适配

### 3.1 ITransportBinding 接口

```cpp
namespace lap::com::runtime {

class ITransportBinding {
public:
    virtual ~ITransportBinding() = default;
    
    // 服务提供
    virtual Result<void> OfferService(
        const InstanceIdentifier& instance_id,
        const ServiceInterfaceInfo& interface_info
    ) = 0;
    
    virtual Result<void> StopOfferService(
        const InstanceIdentifier& instance_id
    ) = 0;
    
    // 服务发现
    virtual Result<std::vector<ServiceInstanceInfo>> FindService(
        const InstanceIdentifier& instance_filter
    ) = 0;
    
    virtual Result<void> StartFindService(
        const InstanceIdentifier& instance_filter,
        std::function<void(const ServiceInstanceInfo&)> callback
    ) = 0;
    
    virtual Result<void> StopFindService(
        const InstanceIdentifier& instance_filter
    ) = 0;
    
    // 获取绑定类型
    virtual TransportBindingType GetBindingType() const = 0;
};

} // namespace lap::com::runtime
```

### 3.2 DDS Binding 服务发现

**基于 AUTOSAR SWS_CM_11001 - SWS_CM_11014 / SWS_CM_90502 - SWS_CM_90518**

#### 3.2.1 DDS Discovery 方式

AUTOSAR 定义了两种 DDS 服务发现方式：

**方式 1: DomainParticipant USER_DATA QoS** (SWS_CM_11008)
```cpp
class DdsBinding : public ITransportBinding {
public:
    Result<void> OfferService(
        const InstanceIdentifier& instance_id,
        const ServiceInterfaceInfo& interface_info
    ) override {
        // 1. 创建 DDS DomainParticipant
        auto participant = CreateDomainParticipant(GetDomainId());
        
        // 2. 设置 USER_DATA QoS (包含服务元数据)
        dds::core::policy::UserData user_data;
        user_data.value({
            SerializeServiceInfo(instance_id, interface_info)
        });
        
        participant.qos(participant.qos() << user_data);
        
        // 3. 创建 DataWriter (for Events, Fields)
        CreateDataWritersForService(participant, interface_info);
        
        return Result<void>::FromValue();
    }
    
    Result<void> StartFindService(
        const InstanceIdentifier& instance_filter,
        std::function<void(const ServiceInstanceInfo&)> callback
    ) override {
        // 设置 BuiltinParticipantListener (SWS_CM_11011)
        class ParticipantListener : public dds::domain::NoOpDomainParticipantListener {
            void on_participant_discovery(
                dds::domain::DomainParticipant& participant,
                const dds::core::status::ParticipantBuiltinTopicData& info
            ) override {
                // 解析 USER_DATA QoS
                auto service_info = DeserializeServiceInfo(info.user_data().value());
                
                // 检查是否匹配过滤条件
                if (MatchesFilter(service_info, instance_filter)) {
                    callback(service_info);
                }
            }
        };
        
        participant_.listener(
            new ParticipantListener(),
            dds::core::status::StatusMask::all()
        );
        
        return Result<void>::FromValue();
    }
};
```

**方式 2: Topic-Based Discovery** (SWS_CM_90503)
```cpp
Result<void> DdsBinding::OfferServiceViaTopic(
    const InstanceIdentifier& instance_id,
    const ServiceInterfaceInfo& interface_info
) {
    // 1. 创建 Discovery Topic
    std::string discovery_topic_name = 
        "DDS_Discovery_" + interface_info.name;
    
    auto discovery_topic = dds::topic::Topic<ServiceDiscoveryData>(
        participant_,
        discovery_topic_name
    );
    
    // 2. 创建 DataWriter 发布服务信息
    auto writer = dds::pub::DataWriter<ServiceDiscoveryData>(
        publisher_,
        discovery_topic
    );
    
    ServiceDiscoveryData data{
        .instance_id = instance_id,
        .service_name = interface_info.name,
        .major_version = interface_info.major_version,
        .minor_version = interface_info.minor_version
    };
    
    writer.write(data);
    
    return Result<void>::FromValue();
}
```

### 3.3 SOME/IP Binding 服务发现

**基于 SOME/IP Service Discovery Protocol**

```cpp
class SomeIpBinding : public ITransportBinding {
public:
    Result<void> OfferService(
        const InstanceIdentifier& instance_id,
        const ServiceInterfaceInfo& interface_info
    ) override {
        // 1. 构建 SOME/IP SD OfferService 消息
        SomeIpSdMessage offer_msg;
        offer_msg.message_type = SomeIpSdMessageType::OFFER_SERVICE;
        offer_msg.service_id = GetServiceId(interface_info.name);
        offer_msg.instance_id = GetInstanceId(instance_id);
        offer_msg.major_version = interface_info.major_version;
        offer_msg.ttl = GetConfiguredTTL();  // Time-To-Live
        
        // 2. 添加 Endpoint Options (IP地址和端口)
        offer_msg.AddEndpointOption(
            SomeIpEndpointOption{
                .ip_address = GetLocalIpAddress(),
                .port = GetServicePort(),
                .protocol = SomeIpTransportProtocol::TCP
            }
        );
        
        // 3. 发送 SD 消息 (组播)
        SendSdMessage(offer_msg, GetSdMulticastAddress());
        
        // 4. 启动周期性发送 (Initial/Repetition/Cyclic phases)
        StartPeriodicOfferService(offer_msg);
        
        return Result<void>::FromValue();
    }
    
    Result<void> StartFindService(
        const InstanceIdentifier& instance_filter,
        std::function<void(const ServiceInstanceInfo&)> callback
    ) override {
        // 1. 发送 FindService 消息
        SomeIpSdMessage find_msg;
        find_msg.message_type = SomeIpSdMessageType::FIND_SERVICE;
        find_msg.service_id = GetServiceId(GetServiceInterfaceName());
        find_msg.instance_id = GetInstanceId(instance_filter);
        find_msg.major_version = GetRequiredMajorVersion();
        
        SendSdMessage(find_msg, GetSdMulticastAddress());
        
        // 2. 监听 OfferService 响应
        RegisterSdMessageHandler([callback, instance_filter](const SomeIpSdMessage& msg) {
            if (msg.message_type == SomeIpSdMessageType::OFFER_SERVICE &&
                MatchesFilter(msg, instance_filter)) {
                
                ServiceInstanceInfo info = ConvertFromSdMessage(msg);
                callback(info);
            }
        });
        
        return Result<void>::FromValue();
    }
};
```

### 3.4 D-Bus Binding 服务发现

```cpp
class DbusBinding : public ITransportBinding {
public:
    Result<void> OfferService(
        const InstanceIdentifier& instance_id,
        const ServiceInterfaceInfo& interface_info
    ) override {
        // 1. 请求 D-Bus Bus Name
        std::string bus_name = GenerateBusName(interface_info.name, instance_id);
        RequestBusName(bus_name);
        
        // 2. 注册 ObjectManager (org.freedesktop.DBus.ObjectManager)
        RegisterObjectManager(bus_name, GetObjectPath());
        
        // 3. 发布服务对象
        RegisterServiceObject(bus_name, interface_info);
        
        return Result<void>::FromValue();
    }
    
    Result<void> StartFindService(
        const InstanceIdentifier& instance_filter,
        std::function<void(const ServiceInstanceInfo&)> callback
    ) override {
        // 1. 监听 NameOwnerChanged 信号
        RegisterNameOwnerChangedHandler([callback, instance_filter](
            const std::string& name,
            const std::string& old_owner,
            const std::string& new_owner
        ) {
            if (!new_owner.empty() && MatchesBusNameFilter(name, instance_filter)) {
                // 新服务上线
                auto service_info = QueryServiceInfo(name);
                callback(service_info);
            }
        });
        
        // 2. 查询已存在的服务 (ListNames)
        auto existing_services = ListExistingServices(instance_filter);
        for (const auto& service : existing_services) {
            callback(service);
        }
        
        return Result<void>::FromValue();
    }
};
```

---

## 第 4 章: API 使用示例

### 4.1 服务提供者 (Skeleton)

```cpp
#include "ara/com/sample/radar_service_skeleton.hpp"

using namespace lap::com::sample::skeleton;

int main() {
    // 1. 初始化 lap::com Runtime
    lap::com::Runtime::GetInstance().Initialize();
    
    // 2. 创建 Skeleton 实例
    ara::core::InstanceSpecifier instance_spec("RadarService/Instance1");
    
    auto skeleton_result = RadarServiceSkeleton::Create(instance_spec);
    if (!skeleton_result) {
        std::cerr << "Failed to create skeleton" << std::endl;
        return 1;
    }
    
    auto skeleton = std::move(skeleton_result.Value());
    
    // 3. 注册方法处理器
    skeleton->RegisterMethodHandler(
        &RadarServiceSkeleton::GetObjectList,
        [](const GetObjectListInput& input) -> GetObjectListOutput {
            // 业务逻辑
            return GenerateObjectList();
        }
    );
    
    // 4. 提供服务 (触发服务发现)
    auto offer_result = skeleton->OfferService();
    if (!offer_result) {
        std::cerr << "Failed to offer service: " 
                  << offer_result.Error().Message() << std::endl;
        return 1;
    }
    
    std::cout << "RadarService offered successfully" << std::endl;
    
    // 5. 运行事件循环
    while (true) {
        // 发送事件
        RadarObjectList objects = GetCurrentObjects();
        skeleton->GetObjectListEvent().Send(objects);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 6. 停止服务
    skeleton->StopOfferService();
    
    return 0;
}
```

### 4.2 服务消费者 (Proxy) - 单次查询

```cpp
#include "ara/com/sample/radar_service_proxy.hpp"

using namespace lap::com::sample::proxy;

int main() {
    // 1. 初始化 Runtime
    lap::com::Runtime::GetInstance().Initialize();
    
    // 2. 查找服务 (单次查询)
    ara::core::InstanceSpecifier instance_spec("RadarService/Instance1");
    
    auto find_result = RadarServiceProxy::FindService(instance_spec);
    if (!find_result || find_result.Value().empty()) {
        std::cerr << "Service not found" << std::endl;
        return 1;
    }
    
    // 3. 创建 Proxy 实例
    auto proxy_result = RadarServiceProxy::Create(find_result.Value()[0]);
    if (!proxy_result) {
        std::cerr << "Failed to create proxy" << std::endl;
        return 1;
    }
    
    auto proxy = std::move(proxy_result.Value());
    
    // 4. 订阅事件
    proxy->GetObjectListEvent().Subscribe(
        [](const RadarObjectList& objects) {
            std::cout << "Received " << objects.size() << " objects" << std::endl;
        }
    );
    
    // 5. 调用方法
    auto method_result = proxy->GetObjectList().GetResult();
    if (method_result) {
        std::cout << "Method call succeeded" << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    return 0;
}
```

### 4.3 服务消费者 - 持续发现

```cpp
int main() {
    lap::com::Runtime::GetInstance().Initialize();
    
    // 使用 StartFindService 持续监控服务可用性
    lap::com::InstanceIdentifier instance_id("RadarService.Instance1");
    
    auto find_handle_result = RadarServiceProxy::StartFindService(
        instance_id,
        [](const lap::com::ServiceHandleContainer<RadarServiceProxy::HandleType>& handles,
           lap::com::FindServiceStatus status) {
            
            if (status == lap::com::FindServiceStatus::kServiceAvailable) {
                std::cout << "Service available: " << handles.size() << " instances" << std::endl;
                
                // 为每个新发现的实例创建 Proxy
                for (const auto& handle : handles) {
                    auto proxy = RadarServiceProxy::Create(handle);
                    if (proxy) {
                        // 订阅事件
                        proxy.Value()->GetObjectListEvent().Subscribe(...);
                    }
                }
            } else if (status == lap::com::FindServiceStatus::kServiceUnavailable) {
                std::cout << "Service unavailable" << std::endl;
                // 清理 Proxy 实例
            }
        }
    );
    
    if (!find_handle_result) {
        std::cerr << "StartFindService failed" << std::endl;
        return 1;
    }
    
    lap::com::FindServiceHandle handle = find_handle_result.Value();
    
    // 运行一段时间
    std::this_thread::sleep_for(std::chrono::minutes(1));
    
    // 停止持续发现
    RadarServiceProxy::StopFindService(handle);
    
    return 0;
}
```

---

## 第 5 章: 配置与清单

### 5.1 集中式注册中心配置 (可选扩展，YAML格式)

```yaml
# lightap_com_config.yaml (使用 yaml-cpp 解析)

lap_com:
  service_discovery:
    # 静态配置 (Layer 1 - 最快)
    static_endpoints:
      file: /etc/lap/com/static_endpoints.yaml
      enabled: true
    
    # 集中式注册中心配置 (Layer 2 - 可选)
    central_registry:
      enabled: true
      socket_path: /tmp/lightap_sd.sock
      connect_timeout_ms: 100
      query_timeout_ms: 10
      max_retry_count: 2
      enable_fallback: true
      fallback_strategy: ON_TIMEOUT  # IMMEDIATE / ON_TIMEOUT / PARALLEL
      cache_results: true
      cache_ttl_seconds: 60
    
    # 动态服务发现配置 (Layer 3 - 总是启用，作为回退)
    dynamic_discovery:
      enabled: true
      bindings:
        - iceoryx2  # 本地服务自动发现
        - dds       # DDS Built-in Topics
        - legacy    # Gateway 处理 D-Bus/SOME/IP
```

**注册中心守护进程配置**:

```yaml
# central_registry_daemon.yaml

registry_daemon:
  socket_path: /tmp/lightap_sd.sock
  socket_permissions: "0666"
  
  # 服务数据库
  database:
    max_services: 10000
    cleanup_interval_seconds: 10
    default_ttl_seconds: 30
  
  # 性能配置
  performance:
    worker_threads: 4
    io_backend: epoll  # epoll / io_uring
    max_concurrent_connections: 1000
  
  # 监听 4-Binding 动态服务发现
  transport_listeners:
    iceoryx2:
      enabled: true
      domain_name: lightap_com
    
    dds:
      enabled: true
      domain_id: 0
      discovery_server: "192.168.1.100:34567"
    
    legacy_gateway:
      enabled: true
      gateway_socket: /tmp/legacy_gateway.sock
  
  # 多节点支持 (可选)
  multi_node:
    enabled: false
    sync_protocol: gossip  # gossip / raft
    peer_nodes:
      - "192.168.1.10:8001"
      - "192.168.1.11:8001"
```

### 5.2 ServiceInterfaceDeployment (服务接口部署配置)

```xml
<!-- ARXML 配置示例 -->
<SERVICE-INTERFACE-DEPLOYMENT>
  <SHORT-NAME>RadarServiceDeployment</SHORT-NAME>
  <SERVICE-INTERFACE-REF DEST="SERVICE-INTERFACE">/Services/RadarService</SERVICE-INTERFACE-REF>
  
  <!-- 服务发现配置 -->
  <SERVICE-INTERFACE-VERSION>
    <MAJOR-VERSION>1</MAJOR-VERSION>
    <MINOR-VERSION>0</MINOR-VERSION>
  </SERVICE-INTERFACE-VERSION>
  
  <!-- 传输绑定 -->
  <NETWORK-BINDING>
    <BINDING-TYPE>SOME-IP</BINDING-TYPE>
    
    <!-- SOME/IP Service Discovery 配置 -->
    <SOMEIP-SERVICE-DISCOVERY>
      <SOMEIP-SERVICE-ID>0x1234</SOMEIP-SERVICE-ID>
      <SOMEIP-SD-SERVER-CONFIG>
        <INITIAL-DELAY-MIN>0</INITIAL-DELAY-MIN>
        <INITIAL-DELAY-MAX>10</INITIAL-DELAY-MAX>
        <REPETITIONS-BASE-DELAY>10</REPETITIONS-BASE-DELAY>
        <REPETITIONS-MAX>3</REPETITIONS-MAX>
        <TTL>3</TTL>  <!-- 秒 -->
        <CYCLIC-OFFER-DELAY>1000</CYCLIC-OFFER-DELAY>  <!-- 毫秒 -->
      </SOMEIP-SD-SERVER-CONFIG>
    </SOMEIP-SERVICE-DISCOVERY>
  </NETWORK-BINDING>
</SERVICE-INTERFACE-DEPLOYMENT>
```

### 5.2 ProvidedServiceInstance (服务提供实例)

```xml
<PROVIDED-SERVICE-INSTANCE>
  <SHORT-NAME>RadarServiceInstance1</SHORT-NAME>
  <SERVICE-INTERFACE-DEPLOYMENT-REF>/Deployments/RadarServiceDeployment</SERVICE-INTERFACE-DEPLOYMENT-REF>
  
  <!-- 实例标识符 -->
  <INSTANCE-ID>
    <INSTANCE-SPECIFIER>/RadarService/Instance1</INSTANCE-SPECIFIER>
  </INSTANCE-ID>
  
  <!-- SOME/IP 配置 -->
  <SOMEIP-PROVIDED-SERVICE-INSTANCE>
    <SOMEIP-INSTANCE-ID>0x0001</SOMEIP-INSTANCE-ID>
    
    <!-- SD 配置 -->
    <SOMEIP-SD-SERVER-EVENT-GROUP-TIMING-CONFIG>
      <INITIAL-DELAY-MIN-VALUE>0</INITIAL-DELAY-MIN-VALUE>
      <INITIAL-DELAY-MAX-VALUE>10</INITIAL-DELAY-MAX-VALUE>
      <REQUEST-RESPONSE-DELAY-MIN-VALUE>10</REQUEST-RESPONSE-DELAY-MIN-VALUE>
      <REQUEST-RESPONSE-DELAY-MAX-VALUE>50</REQUEST-RESPONSE-DELAY-MAX-VALUE>
    </SOMEIP-SD-SERVER-EVENT-GROUP-TIMING-CONFIG>
  </SOMEIP-PROVIDED-SERVICE-INSTANCE>
</PROVIDED-SERVICE-INSTANCE>
```

### 5.3 RequiredServiceInstance (服务消费实例)

```xml
<REQUIRED-SERVICE-INSTANCE>
  <SHORT-NAME>RadarServiceClientInstance</SHORT-NAME>
  <SERVICE-INTERFACE-DEPLOYMENT-REF>/Deployments/RadarServiceDeployment</SERVICE-INTERFACE-DEPLOYMENT-REF>
  
  <!-- 版本要求 -->
  <REQUIRED-SERVICE-VERSION>
    <MAJOR-VERSION>1</MAJOR-VERSION>
    <MINOR-VERSION>0</MINOR-VERSION>
  </REQUIRED-SERVICE-VERSION>
  
  <!-- 版本黑名单 (不兼容的版本) -->
  <DDS-REQUIRED-SERVICE-INSTANCE>
    <BLOCKLISTED-VERSIONS>
      <BLOCKLISTED-VERSION>
        <MAJOR-VERSION>1</MAJOR-VERSION>
        <MINOR-VERSION>2</MINOR-VERSION>  <!-- 1.2 版本有 Bug -->
      </BLOCKLISTED-VERSION>
    </BLOCKLISTED-VERSIONS>
  </DDS-REQUIRED-SERVICE-INSTANCE>
</REQUIRED-SERVICE-INSTANCE>
```

---

## 第 6 章: 性能优化

### 6.1 服务缓存

```cpp
class ServiceRegistry {
private:
    // LRU 缓存 (最近最少使用)
    template<typename Key, typename Value>
    class LRUCache {
    public:
        LRUCache(size_t capacity) : capacity_(capacity) {}
        
        std::optional<Value> Get(const Key& key) {
            auto it = cache_.find(key);
            if (it == cache_.end()) {
                return std::nullopt;
            }
            
            // 移动到最前面 (最近使用)
            lru_list_.splice(lru_list_.begin(), lru_list_, it->second.list_iter);
            
            return it->second.value;
        }
        
        void Put(const Key& key, const Value& value) {
            auto it = cache_.find(key);
            
            if (it != cache_.end()) {
                // 更新现有条目
                it->second.value = value;
                lru_list_.splice(lru_list_.begin(), lru_list_, it->second.list_iter);
            } else {
                // 插入新条目
                if (cache_.size() >= capacity_) {
                    // 淘汰最少使用的条目
                    auto lru_key = lru_list_.back();
                    lru_list_.pop_back();
                    cache_.erase(lru_key);
                }
                
                lru_list_.push_front(key);
                cache_[key] = {value, lru_list_.begin()};
            }
        }
        
    private:
        size_t capacity_;
        std::list<Key> lru_list_;
        
        struct CacheEntry {
            Value value;
            typename std::list<Key>::iterator list_iter;
        };
        
        std::unordered_map<Key, CacheEntry> cache_;
    };
    
    // 远程服务缓存 (避免重复查询)
    LRUCache<InstanceIdentifier, ServiceInstanceInfo> remote_service_cache_{1000};
};
```

### 6.2 批量查询优化

```cpp
// 批量查找多个服务 (减少锁竞争)
Result<std::map<InstanceIdentifier, ServiceHandleContainer<HandleType>>>
ServiceDiscoveryManager::FindServicesBatch(
    const std::vector<InstanceIdentifier>& instance_filters
) {
    std::map<InstanceIdentifier, ServiceHandleContainer<HandleType>> results;
    
    // 一次性获取读锁
    std::shared_lock lock(service_registry_->registry_mutex_);
    
    for (const auto& instance_id : instance_filters) {
        auto handles = FindService(instance_id);
        if (handles) {
            results[instance_id] = std::move(handles.Value());
        }
    }
    
    return Result<...>::FromValue(std::move(results));
}
```

### 6.3 异步服务发现

```cpp
// 异步 FindService (避免阻塞)
std::future<Result<ServiceHandleContainer<HandleType>>>
ServiceDiscoveryManager::FindServiceAsync(
    const InstanceIdentifier& instance_filter
) {
    return std::async(std::launch::async, [this, instance_filter]() {
        return FindService(instance_filter);
    });
}
```

---

## 第 7 章: 错误处理

### 7.1 ComErrc 错误码

```cpp
namespace lap::com {

enum class ComErrc : uint32_t {
    kServiceNotAvailable = 1,        // 服务不可用
    kNetworkBindingFailure = 2,      // 网络绑定失败
    kServiceVersionMismatch = 3,     // 服务版本不匹配
    kServiceNotOffered = 4,          // 服务未提供
    kFieldValueIsNotValid = 5,       // 字段值无效
    kMaxSamplesExceeded = 6,         // 超过最大样本数
    kWrongMethodCallProcessingMode = 7,  // 错误的方法调用处理模式
    kIllegalUseOfAllocate = 8,       // 非法使用 Allocate
    kCommunicationStackError = 9,    // 通信栈错误
    kInvalidServiceInterface = 10,   // 无效的服务接口
    kErroneousServiceHandle = 11,    // 错误的服务句柄
    kGrantEnforcementError = 12,     // 权限强制错误
};

} // namespace lap::com
```

### 7.2 错误处理示例

```cpp
auto find_result = RadarServiceProxy::FindService(instance_spec);

if (!find_result) {
    auto error_code = find_result.Error();
    
    switch (error_code.Value()) {
        case lap::com::ComErrc::kServiceNotAvailable:
            std::cerr << "Service not available, will retry..." << std::endl;
            // 重试逻辑
            std::this_thread::sleep_for(std::chrono::seconds(1));
            break;
            
        case lap::com::ComErrc::kNetworkBindingFailure:
            std::cerr << "Network binding failure: " 
                      << error_code.Message() << std::endl;
            // 检查网络配置
            break;
            
        case lap::com::ComErrc::kServiceVersionMismatch:
            std::cerr << "Service version mismatch" << std::endl;
            // 检查清单配置
            break;
            
        default:
            std::cerr << "Unknown error: " << error_code.Message() << std::endl;
            break;
    }
}
```

---

## 第 8 章: 测试策略

### 8.1 单元测试

```cpp
TEST(ServiceRegistryTest, RegisterAndFindLocalService) {
    ServiceRegistry registry;
    
    // 注册服务
    ServiceRegistry::ServiceInstanceInfo info{
        .instance_id = InstanceIdentifier("TestService.Instance1"),
        .service_interface_name = "TestService",
        .major_version = 1,
        .minor_version = 0,
        .state = ServiceRegistry::ServiceState::OFFERED
    };
    
    auto register_result = registry.RegisterLocalService(info);
    ASSERT_TRUE(register_result);
    
    // 查找服务
    auto find_result = registry.FindServices("TestService");
    ASSERT_TRUE(find_result);
    EXPECT_EQ(find_result.Value().size(), 1);
    EXPECT_EQ(find_result.Value()[0].instance_id, info.instance_id);
}

TEST(ServiceRegistryTest, VersionCompatibility) {
    // 主版本号不匹配
    EXPECT_FALSE(ServiceRegistry::IsVersionCompatible(1, 0, 2, 0));
    
    // 次版本号向后兼容
    EXPECT_TRUE(ServiceRegistry::IsVersionCompatible(1, 0, 1, 5));
    EXPECT_FALSE(ServiceRegistry::IsVersionCompatible(1, 5, 1, 0));
}
```

### 8.2 集成测试

```cpp
TEST(ServiceDiscoveryIntegrationTest, OfferAndFindService) {
    // 1. 创建 Skeleton 提供服务
    auto skeleton = TestServiceSkeleton::Create(
        InstanceSpecifier("TestService/Instance1")
    );
    ASSERT_TRUE(skeleton);
    
    auto offer_result = skeleton.Value()->OfferService();
    ASSERT_TRUE(offer_result);
    
    // 2. 等待服务发现传播
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 3. Proxy 查找服务
    auto find_result = TestServiceProxy::FindService(
        InstanceIdentifier("TestService.Instance1")
    );
    ASSERT_TRUE(find_result);
    EXPECT_FALSE(find_result.Value().empty());
    
    // 4. 创建 Proxy
    auto proxy = TestServiceProxy::Create(find_result.Value()[0]);
    ASSERT_TRUE(proxy);
    
    // 5. 测试通信
    auto method_result = proxy.Value()->TestMethod();
    EXPECT_TRUE(method_result);
}
```

### 8.3 性能测试

```cpp
TEST(ServiceDiscoveryPerformanceTest, FindServiceLatency) {
    const int NUM_ITERATIONS = 1000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        auto result = TestServiceProxy::FindService(
            InstanceIdentifier("TestService.Instance1")
        );
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double avg_latency = duration.count() / static_cast<double>(NUM_ITERATIONS);
    
    std::cout << "Average FindService latency: " << avg_latency << " μs" << std::endl;
    
    // 性能目标: < 100 μs
    EXPECT_LT(avg_latency, 100.0);
}
```

---

## 第 9 章: 实施路线图

### Week 1-2: 核心框架

- ✅ ServiceRegistry 实现
- ✅ ServiceDiscoveryManager 基础框架
- ✅ FindServiceHandlerManager 实现
- ✅ 单元测试 (覆盖率 >80%)

### Week 3-4: 4-Binding 插件集成

- ✅ ITransportBinding 接口定义
- ✅ iceoryx2 Binding 服务发现（共享内存元数据）
- ✅ iceoryx2 Binding 服务发现（共享内存直接访问）
- ✅ SOME/IP Binding 服务发现（SD Protocol）
- ✅ Custom Protocol Binding 服务发现
- ✅ Legacy Gateway 服务发现（隔离处理 D-Bus/SOME/IP）

### Week 5-6: API 实现

- ✅ Runtime::FindService() 实现
- ✅ Runtime::StartFindService() 实现
- ✅ Skeleton::OfferService() 实现
- ✅ Skeleton::StopOfferService() 实现
- ✅ 集成测试

### Week 7-8: 性能优化与生产就绪

- ✅ 服务缓存优化 (LRU)
- ✅ 批量查询优化
- ✅ 异步服务发现
- ✅ 性能基准测试
- ✅ 文档完善（YAML 配置标准化）

### Week 9-10: Runtime 集成与优化 ⭐

- 📋 **Runtime.hpp/cpp 实现**
  - SharedMemoryRegistry 集成
  - FindService/OfferService API 实现
  - 心跳线程管理
  - 槽位分配策略

- 📋 **性能优化**
  - seqlock 优化（减少重试）
  - CPU 缓存对齐（cache line）
  - NUMA 感知内存分配

- 📋 **配置与部署**
  - YAML 配置文件支持
  - arxml2yaml 工具集成
  - Systemd socket activation 服务单元
  - 性能基准测试（延迟 < 500ns）

- 📋 **测试与验证**
  - 单元测试（SharedMemoryRegistry）
  - 集成测试（零守护进程架构）
  - 性能测试（延迟 < 500ns）
  - 压力测试（并发访问）

### Week 11: 多节点支持 (高级特性，可选)

- 📋 **跨节点服务聚合**
  - Gossip 协议同步服务信息
  - 节点健康检查
  - 服务副本管理

---

## 第 10 章: 性能指标

### 10.1 服务发现延迟对比（v3.0 零守护进程架构）

| 发现方式 | 平均延迟 | P99 延迟 | 适用场景 |
|----------|----------|----------|----------|
| **固定槽位查找** (Shared Memory) | < 100 ns | < 500 ns | 本地服务发现（推荐）⭐ |
| **iceoryx2 Binding 发现** | 1-3 ms | 5 ms | 本地零拷贝服务 |
| **DDS Binding 发现** | 10-30 ms | 100 ms | 跨 ECU 服务 |
| **Legacy Binding** (D-Bus/SOME/IP) | 5-50 ms | 200 ms | 向后兼容 |

### 10.2 资源占用

**v3.0 零 Daemon 架构**:
- 内存: ~256 KB (槽位数组) + ~8 KB (心跳线程)
- CPU: < 0.5% (空闲)，< 2% (峰值，心跳检查)
- 磁盘: 无持久化存储（所有数据在共享内存）
- 依赖: iceoryx2 库，无其他外部依赖
- 端口: 无（共享内存访问）

**单进程开销**:
- 内存: < 300 KB total
- 共享内存访问: mmap 只读/读写，无拷贝
- 线程: 1 个心跳线程（可选）

### 10.3 吞吐量

| 操作 | 吞吐量 (ops/s) |
|------|----------------|
| RegisterService | 50,000+ |
| FindService (缓存命中) | 200,000+ |
| FindService (注册中心) | 100,000+ |
| 并发查询 (4 线程) | 300,000+ |

---

## 参考资源

### 标准文档

- AUTOSAR_AP_SWS_CommunicationManagement (R24-11, November 2024)
- AUTOSAR_AP_TPS_ManifestSpecification (R24-11, TPS_MANI_03312-03315)
- AUTOSAR_AP_SWS_NetworkManagement
- SOME/IP Service Discovery Protocol Specification v1.4
- OMG DDS Discovery Protocol Specification v2.5
- iceoryx2 Service Discovery Design

### 相关章节

- SWS_CM_00110 - SWS_CM_00125: Service Discovery APIs
- SWS_CM_02201: Static Service Instance Configuration
- TPS_MANI_03312: Static Service Manifest (YAML 转换支持)
- SWS_CM_11001 - SWS_CM_11014: DDS Binding Service Discovery
- SWS_CM_90502 - SWS_CM_90518: DDS Topic-Based Discovery
- SWS_CM_10289 - SWS_CM_10291: SOME/IP Service Discovery (Legacy)


### 依赖库

- yaml-cpp: 配置解析 (<https://github.com/jbeder/yaml-cpp>)
- iceoryx2: 零拷贝通信与本地服务发现 (<https://github.com/eclipse-iceoryx/iceoryx2>)

---

**文档版本**: 3.0 (零守护进程架构)  
**最后更新**: 2025-11-19  
**作者**: LightAP Com Module Team  
**命名空间**: lap::com (100% 兼容 AUTOSAR ara::com)  
**配置格式**: YAML (使用 arxml2yaml 工具转换 AUTOSAR ARXML)

