# Com模块新架构实施优化方案（基于SERVICE_DISCOVERY_ARCHITECTURE.md）

**文档版本**: 2.0（采用零Daemon固定槽位架构）  
**创建日期**: 2025-11-20  
**基于标准**: AUTOSAR Adaptive Platform R24-11  
**参考设计**: `SERVICE_DISCOVERY_ARCHITECTURE.md` v3.0  
**架构特性**: 零Daemon + 固定槽位 + 双注册表物理隔离 + seqlock  

---

## ⚠️ 前置条件与强制要求

### 📜 接口规范（严格遵循AUTOSAR R24-11标准）

所有接口设计**必须**参考以下AUTOSAR标准文档，有不明确的地方**直接查阅对应文档**：

1. **AUTOSAR_AP_EXP_ARAComAPI** (141页)
   - 路径: `LightAP/doc/R24-11/AUTOSAR_AP_EXP_ARAComAPI.pdf`
   - 用途: ara::com API完整规范与示例
   - 关键章节: API Reference, Usage Examples

2. **AUTOSAR_AP_RS_CommunicationManagement** 
   - 路径: `LightAP/doc/R24-11/AUTOSAR_AP_RS_CommunicationManagement.pdf`
   - 用途: 通信管理需求规范
   - 关键章节: Requirements Traceability

3. **AUTOSAR_AP_SWS_CommunicationManagement** (672页)
   - 路径: `LightAP/doc/R24-11/AUTOSAR_AP_SWS_CommunicationManagement.pdf`
   - 用途: 通信管理软件规范
   - 关键章节: Service Discovery, Binding Specification

4. **AUTOSAR_AP_SWS_NetworkManagement**
   - 路径: `LightAP/doc/R24-11/AUTOSAR_AP_SWS_NetworkManagement.pdf`
   - 用途: 网络管理规范
   - 关键章节: Network Bindings, Transport Protocols

**强制规则**:
- ✅ **100%符合**: 所有API签名、命名空间、类型定义必须与AUTOSAR标准一致
- ✅ **需求追溯**: 每个实现必须标注对应的AUTOSAR需求ID（如SWS_CM_00001）
- ⛔ **禁止偏离**: 不得自行定义不符合标准的接口或行为

### 🏗️ 架构/设计规范（严格按照设计文档）

所有架构设计**必须**遵循以下设计文档，有不明确的地方**直接查阅对应文档**：

1. **ARCHITECTURE_SUMMARY.md** (3,380行)
   - 路径: `modules/Com/doc/ARCHITECTURE_SUMMARY.md`
   - 用途: 完整架构设计总览
   - 关键章节: 4-Binding架构, 性能优化方案

2. **SERVICE_DISCOVERY_ARCHITECTURE.md** (3,553行) 🎯
   - 路径: `modules/Com/doc/SERVICE_DISCOVERY_ARCHITECTURE.md`
   - 用途: **服务发现核心设计**（零Daemon架构）
   - 关键章节: §1.4 架构层次, §2.1 核心数据结构, §2.1.1 槽位分配策略

**强制规则**:
- ✅ **严格遵循**: 必须采用零Daemon + 固定槽位 + 双注册表架构
- ✅ **256字节槽位**: ServiceSlot结构必须为256字节（4×cache-line对齐）
- ✅ **seqlock同步**: 必须使用seqlock无锁并发机制
- ⛔ **禁止替代方案**: 不得使用哈希表、中央服务器等传统方案

### 🧩 模块依赖规范

**优先使用现有模块**: 基础功能必须优先使用Core/Log/Persistency已有功能，避免重复实现。

| 功能 | 使用模块 | 文件路径 |
|------|---------|---------|
| **错误处理** | Core | `modules/Core/source/inc/CResult.hpp` |
| **可选类型** | Core | `modules/Core/source/inc/COptional.hpp` |
| **字符串** | Core | `modules/Core/source/inc/CString.hpp` |
| **日志** | LogAndTrace | `modules/LogAndTrace/source/inc/CLogger.hpp` |
| **配置管理** | Persistency | `modules/Persistency/...` |

**缺失功能处理流程**:
1. 检查Core/Log/Persistency是否已有类似功能
2. 如确实缺失，补充到对应模块（而非Com模块）
3. 提交补充需求，包含：
   - 功能描述
   - 接口设计
   - 实现细节
   - 单元测试

**示例**:
```cpp
// ✅ 正确：使用Core模块
#include <core/CResult.hpp>
#include <core/COptional.hpp>
using lap::core::Result;
using lap::core::Optional;

// ❌ 错误：在Com模块自己实现Result
namespace lap::com {
    template<typename T> class Result { ... };  // 禁止！
}
```

### 🔄 不向后兼容（破坏性升级）

**强制规则**:
- ✅ **直接实施最新方案**: 不需要考虑向后兼容性
- ✅ **删除旧代码**: 与新架构冲突的旧代码直接删除
- ✅ **替换机制**: 
  - 旧的服务发现代码 → 删除，使用固定槽位注册表
  - 旧的Binding硬编码 → 删除，使用Binding Manager
  - 96字节槽位 → 替换为256字节槽位

**删除清单**（需确认后执行）:
- [ ] 旧的服务发现实现（如有动态发现代码）
- [ ] 硬编码的Binding选择逻辑
- [ ] 不符合AUTOSAR R24-11的API

### 🛑 问题上报机制

**遇到以下情况必须停止开发并上报**:

1. **标准不明确**: AUTOSAR文档有歧义或缺失
2. **设计冲突**: 设计文档之间存在矛盾
3. **依赖缺失**: Core/Log/Persistency缺少必要功能且无法快速补充
4. **技术障碍**: 性能目标无法达成（如< 500ns延迟）
5. **资源不足**: 硬件/软件环境不满足要求

**上报格式**:
```markdown
## 问题上报

**问题类型**: [标准不明确 / 设计冲突 / 依赖缺失 / 技术障碍 / 资源不足]
**影响阶段**: Phase X
**具体描述**: ...
**相关文档**: 
- AUTOSAR_xxx.pdf, Page xx
- SERVICE_DISCOVERY_ARCHITECTURE.md, §x.x
**建议方案**: ...
```

---

## 执行摘要

本方案严格遵循 `SERVICE_DISCOVERY_ARCHITECTURE.md` 中的零Daemon固定槽位自注册架构，实施Com模块的完整优化升级。

**核心架构变更**:
1. ✅ **服务发现**: 采用固定槽位映射 + 双注册表物理隔离（QM + ASIL-D）
2. ✅ **零Daemon**: 完全去中心化，无RouDi/无守护进程
3. ✅ **超高性能**: < 500ns服务发现延迟（相比传统1-5µs）
4. ✅ **FuSa就绪**: ISO 26262 ASIL-D物理隔离

**实施优先级调整**:
- 🔴 P0: 固定槽位注册表 + Binding Manager（核心架构）
- 🟡 P1: iceoryx2 + DDS Binding（高性能通信）
- 🟢 P2: 系统优化 + Custom Protocol（最后实施）

---

## 目录

1. [架构设计依据](#1-架构设计依据)
2. [实施路线图](#2-实施路线图)
3. [Phase 1: 固定槽位服务注册表](#3-phase-1-固定槽位服务注册表)
4. [Phase 2: Binding Manager](#4-phase-2-binding-manager)
5. [Phase 3: iceoryx2 Binding](#5-phase-3-iceoryx2-binding)
6. [Phase 4: DDS + AF_XDP](#6-phase-4-dds--af_xdp)
7. [Phase 5: 系统优化 + Custom Protocol](#7-phase-5-系统优化--custom-protocol)
8. [验收标准](#8-验收标准)

---

## 1. 架构设计依据

### 1.1 SERVICE_DISCOVERY_ARCHITECTURE.md核心设计

**文档位置**: `modules/Com/doc/SERVICE_DISCOVERY_ARCHITECTURE.md`

**核心设计原则** (v3.0):
- ✅ **零Daemon**: 无RouDi/无中央服务器/无守护进程
- ✅ **固定槽位**: 服务ID范围 → 槽位范围映射
- ✅ **双注册表**: QM Registry (0x0001~0x03FF) + ASIL-D Registry (0xF000~0xF3FF)
- ✅ **seqlock同步**: 无锁并发读取，原子写入
- ✅ **256字节槽位**: 4×cache-line对齐，包含完整服务元数据

**架构对比**:

| 特性 | 传统方案 | 本架构 (v3.0) |
|------|----------|---------------|
| **延迟** | 1-5µs (Domain Socket) | < 500ns (共享内存直接读取) |
| **守护进程** | RouDi / systemd-resolved | ❌ 零 Daemon |
| **单点故障** | ✅ 存在 | ❌ 无 |
| **FuSa隔离** | 需隔离Daemon | ✅ 双注册表物理隔离 |
| **代码复杂度** | 高 (>2000行) | ✅ 低 (~800行) |

### 1.2 槽位分配策略（固定映射）

**设计依据**: `SERVICE_DISCOVERY_ARCHITECTURE.md` §2.1.1

```cpp
// 服务ID范围 → 槽位范围映射
std::pair<uint16_t, uint16_t> GetSlotRange(uint16_t service_id) {
    // QM级别服务 (0x0001~0x03FF)
    if (service_id >= 0x0001 && service_id <= 0x00FF) {
        return {1, 255};     // 感知服务（摄像头、激光雷达等）
    } else if (service_id >= 0x0100 && service_id <= 0x01FF) {
        return {256, 511};   // 规划决策服务
    } else if (service_id >= 0x0200 && service_id <= 0x02FF) {
        return {512, 767};   // 娱乐信息服务
    } else if (service_id >= 0x0300 && service_id <= 0x03FF) {
        return {768, 1022};  // 诊断日志服务
    }
    // ASIL-D级别服务 (0xF000~0xF3FF)
    else if (service_id >= 0xF000 && service_id <= 0xF0FF) {
        return {1, 255};     // ASIL-D 核心控制区
    }
    // 广播槽位
    else if (service_id == 0xFFFF) {
        return {1023, 1023}; // 全局广播
    }
    return {1, 1022};  // 默认范围
}
```

### 1.3 双注册表物理隔离

**设计依据**: `SERVICE_DISCOVERY_ARCHITECTURE.md` §1.4 架构层次

```
QM Registry:   /dev/shm/lap_com_registry_qm    (256KB, 1024 slots, 权限 0666)
ASIL-D Registry: /dev/shm/lap_com_registry_asil (256KB, 1024 slots, 权限 0640)
```

**隔离策略**:
- QM Registry: 所有进程可读写（感知/规划/娱乐等非安全关键服务）
- ASIL-D Registry: 控制进程写入，其他进程只读（刹车/转向等安全关键服务）
- 物理隔离: 两个独立memfd，内核级别进程隔离

---

## 2. 实施路线图

### 2.1 总体时间线（20周 = 5个月）

```
阶段         Week 1-3    Week 4-5    Week 6-10   Week 11-14  Week 15-17  Week 18-20
           ┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
Phase 1    │███████████│          │          │          │          │          │ 固定槽位服务注册表
Phase 2    │          │██████████│          │          │          │          │ Binding Manager
Phase 3    │          │          │███████████████████████          │          │ iceoryx2 Binding
Phase 4    │          │          │          │████████████████████████          │ DDS + AF_XDP
Phase 5    │          │          │          │          │          │████████████ 系统优化+Custom
测试验证   │      ████████      ████████      ████████      ████████      ████████ 持续测试
           └──────────┴──────────┴──────────┴──────────┴──────────┴──────────┘
```

### 2.2 关键里程碑

| 里程碑 | 时间 | 交付物 | 验收标准 |
|--------|------|--------|---------|
| **M1** | Week 3 | 双注册表 + seqlock | 发现延迟 < 500ns |
| **M2** | Week 5 | Binding Manager | 4个Binding动态加载 |
| **M3** | Week 10 | iceoryx2 Binding | IPC延迟 < 1µs |
| **M4** | Week 14 | DDS + AF_XDP | 跨ECU延迟 < 15µs |
| **M5** | Week 17 | 系统优化 | CPU占用 < 1% |
| **M6** | Week 20 | Custom Protocol + 完整测试 | 全功能验证 |

---

## 3. Phase 1: 固定槽位服务注册表

### 3.1 目标

**设计依据**: `SERVICE_DISCOVERY_ARCHITECTURE.md` §2.1 核心数据结构

实现完全去中心化的服务注册表，基于iceoryx2共享内存，达到：
- ✅ 服务发现延迟 < 500ns (P99)
- ✅ 零Daemon架构（无RouDi/无守护进程）
- ✅ O(1) 查找复杂度（固定槽位映射）
- ✅ 双注册表物理隔离（QM + ASIL-D）
- ✅ seqlock无锁并发访问
- ✅ 心跳机制 + 自动清理

### 3.2 核心组件设计

#### 3.2.1 ServiceSlot结构（256字节）

**设计依据**: `SERVICE_DISCOVERY_ARCHITECTURE.md` §2.1 槽位结构  
**AUTOSAR需求**: SWS_CM_00302 (Service Instance Identification)  
**标准参考**: `AUTOSAR_AP_SWS_CommunicationManagement.pdf` §7.2.1

```cpp
struct alignas(64) ServiceSlot {  // Cache line对齐
    // === seqlock控制字段 (8 bytes) ===
    std::atomic<uint64_t> sequence;  // 奇数=写中，偶数=可读
    
    // === 服务标识 (32 bytes) ===
    uint64_t service_id;             // 服务接口 ID
    uint64_t instance_id;            // 实例 ID
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
    
    // seqlock写入（原子操作）
    void BeginWrite() {
        sequence.fetch_add(1, std::memory_order_acquire);  // 奇数=写入中
    }
    
    void EndWrite() {
        std::atomic_thread_fence(std::memory_order_release);
        sequence.fetch_add(1, std::memory_order_release);  // 偶数=可读
    }
    
    // seqlock读取（无锁，< 100ns）
    template<typename Func>
    bool Read(Func&& reader) const {
        uint64_t seq1, seq2;
        do {
            seq1 = sequence.load(std::memory_order_acquire);
            if (seq1 & 1) {  // 写入中
                _mm_pause();  // x86 PAUSE指令，减少总线争用
                continue;
            }
            
            reader(*this);  // 读取数据
            std::atomic_thread_fence(std::memory_order_acquire);
            
            seq2 = sequence.load(std::memory_order_acquire);
        } while (seq1 != seq2);  // 版本不一致，重试
        return true;
    }
};
static_assert(sizeof(ServiceSlot) == 256, "ServiceSlot must be 256 bytes");
```

#### 3.2.2 SharedMemoryRegistry（双注册表）

**设计依据**: `SERVICE_DISCOVERY_ARCHITECTURE.md` §1.4 双注册表物理隔离  
**AUTOSAR需求**: SWS_CM_00001 (FindService), SWS_CM_00002 (OfferService)  
**标准参考**: `AUTOSAR_AP_SWS_CommunicationManagement.pdf` §7.1

```cpp
// 使用Core模块的Result类型
#include <core/CResult.hpp>
#include <core/CString.hpp>

using lap::core::Result;
using lap::core::String;

class SharedMemoryRegistry {
public:
    // 初始化双注册表（QM + ASIL-D物理隔离）
    Result<void> Initialize(
        const std::string& qm_shm_name = "/dev/shm/lap_com_registry_qm",
        const std::string& asil_shm_name = "/dev/shm/lap_com_registry_asil"
    );
    
    // 注册服务（写入固定槽位）
    Result<void> RegisterService(
        uint16_t service_id,
        uint16_t instance_id,
        const std::string& binding_type,
        const std::string& endpoint
    );
    
    // 查找服务（O(1)，< 500ns）
    Result<ServiceSlot> FindService(uint16_t service_id, uint16_t instance_id);
    
    // 心跳更新
    Result<void> UpdateHeartbeat(uint16_t service_id, uint16_t instance_id);
    
    // 清理僵尸服务
    void CleanupStaleServices(uint64_t timeout_ns = 5000000000);  // 默认5秒超时
    
private:
    // 双注册表文件描述符
    int qm_shm_fd_ = -1;      // QM Registry
    int asil_shm_fd_ = -1;    // ASIL-D Registry
    
    // 双槽位数组指针（各1024个槽位）
    ServiceSlot* qm_slots_ = nullptr;      // QM服务槽位
    ServiceSlot* asil_slots_ = nullptr;    // ASIL-D服务槽位
    
    // 心跳守护线程
    std::thread heartbeat_thread_;
    std::atomic<bool> running_{false};
    
    // 根据服务ID选择注册表
    ServiceSlot* getRegistry(uint16_t service_id);
};
```

### 3.3 实施计划（3周）

#### Week 1: seqlock + 基础槽位结构

**AUTOSAR需求追溯**: SWS_CM_00302, SWS_CM_00303

**任务清单**:
- [ ] **查阅标准**: 
  - `AUTOSAR_AP_SWS_CommunicationManagement.pdf` §7.2 (Service Instance)
  - `SERVICE_DISCOVERY_ARCHITECTURE.md` §2.1 (槽位结构)
- [ ] `ServiceSlot`结构体实现（256字节对齐）
- [ ] seqlock读写机制实现（严格遵循设计文档）
- [ ] **使用Core模块**: 所有基础类型使用`lap::core`
- [ ] 单元测试（并发读写验证）

**交付物**:
- `ServiceSlot.hpp` (100行) + AUTOSAR需求注释
- `test_seqlock.cpp` (200行)
- seqlock性能测试（< 100ns读取验证）

**强制检查点**:
- ✅ 槽位大小严格256字节
- ✅ seqlock实现与设计文档一致
- ✅ 所有类型使用Core模块（Result, Optional, String）
- ⛔ 如有疑问立即上报，禁止自行修改设计

#### Week 2: 双注册表实现

**AUTOSAR需求追溯**: SWS_CM_00001, SWS_CM_00002, SWS_CM_00110, SWS_CM_00111

**任务清单**:
- [ ] **查阅标准**:
  - `AUTOSAR_AP_SWS_CommunicationManagement.pdf` §7.1 (Service Discovery)
  - `SERVICE_DISCOVERY_ARCHITECTURE.md` §1.4 (双注册表架构)
  - `SERVICE_DISCOVERY_ARCHITECTURE.md` §2.1.1 (槽位分配策略)
- [ ] QM Registry创建（/dev/shm/lap_com_registry_qm）
- [ ] ASIL-D Registry创建（/dev/shm/lap_com_registry_asil）
- [ ] 权限管理（QM 0666 / ASIL-D 0640）
- [ ] 固定槽位映射逻辑（严格按§2.1.1实现）
- [ ] **使用Log模块**: 所有日志使用`lap::log::Logger`

**交付物**:
- `SharedMemoryRegistry.hpp` (300行) + AUTOSAR需求注释
- `SharedMemoryRegistry.cpp` (500行)
- 双注册表集成测试

**强制检查点**:
- ✅ 槽位映射算法与设计文档完全一致
- ✅ QM/ASIL-D物理隔离（独立memfd）
- ✅ 权限设置正确（0666 / 0640）
- ⛔ 禁止使用哈希表或其他映射方式

#### Week 3: 服务注册/发现 + 心跳

**AUTOSAR需求追溯**: SWS_CM_00122, SWS_CM_00123, SWS_CM_00125

**任务清单**:
- [ ] **查阅标准**:
  - `AUTOSAR_AP_EXP_ARAComAPI.pdf` (完整API参考)
  - `AUTOSAR_AP_SWS_CommunicationManagement.pdf` §8.1 (FindService API)
  - `SERVICE_DISCOVERY_ARCHITECTURE.md` §2.1.3 (心跳机制)
- [ ] `RegisterService()` 实现（OfferService接口）
- [ ] `FindService()` 实现（O(1)查找，严格遵循AUTOSAR API）
- [ ] 心跳守护线程实现（混合方案：心跳+kill(0)）
- [ ] Runtime API集成
- [ ] **Core模块依赖检查**: 确认所需功能是否完备

**交付物**:
- 服务注册/发现功能 (400行) + AUTOSAR API符合性注释
- 心跳守护线程 (150行)
- 性能基准测试（< 500ns验证）
- API符合性测试报告

**强制检查点**:
- ✅ FindService API签名100%符合AUTOSAR标准
- ✅ OfferService行为符合SWS_CM_00002
- ✅ 性能达标（< 500ns，P99）
- ✅ 心跳机制与设计文档一致
- ⛔ 如API不明确，立即查阅`AUTOSAR_AP_EXP_ARAComAPI.pdf`

### 3.4 验收标准

| 指标 | 目标 | 验证方法 |
|------|------|---------|
| 服务发现延迟 | < 500ns (P99) | 性能基准测试 (100万次调用) |
| 并发读取性能 | > 1000万次/秒 | 多线程压测 |
| 内存占用 | 512KB (双注册表) | 内存分析 |
| 心跳开销 | < 0.1% CPU | 系统监控 |
| 僵尸清理 | < 5秒 | 进程崩溃模拟 |

---

## 4. Phase 2: Binding Manager

（与原方案保持一致，略）

---

## 5. Phase 3: iceoryx2 Binding

（与原方案保持一致，略）

---

## 6. Phase 4: DDS + AF_XDP

（与原方案保持一致，略）

---

## 7. Phase 5: 系统优化 + Custom Protocol

### 7.1 系统优化（Week 15-16）

**优化清单**:
- 1GB大页内存
- CPU亲和性绑核
- io_uring SQPOLL

### 7.2 Custom Protocol Binding（Week 17，最后实施）

**优先级调整原因**:
- 🟢 P2: 非核心功能，可选实现
- 适用场景: 遗留系统集成、快速原型验证
- 依赖前置: 需Binding Manager框架完成

**实施内容**:
- UDS传输层
- 自定义编解码器
- 简化实现（600行代码）

---

## 8. 验收标准

### 8.1 最终性能指标

| 指标 | 当前 | 目标 | 提升幅度 |
|------|------|------|---------|
| 服务发现 | 1-100ms | **< 500ns** | 200倍 |
| 本地IPC | 50-100µs | **< 1µs** | 100倍 |
| 跨ECU | 100-200µs | **< 15µs** | 13倍 |
| 吞吐量 | < 500MB/s | **> 10GB/s** | 20倍 |
| CPU占用 | 3-5% | **< 1%** | 5倍 |

### 8.2 架构符合性

- ✅ 100% 遵循 `SERVICE_DISCOVERY_ARCHITECTURE.md` v3.0
- ✅ 零Daemon架构验证
- ✅ 双注册表物理隔离验证
- ✅ seqlock无锁并发验证
- ✅ FuSa-Ready（ISO 26262 ASIL-D支持）

---

**文档维护**: LightAP Team  
**最后更新**: 2025-11-20  
**参考设计**: SERVICE_DISCOVERY_ARCHITECTURE.md v3.0

