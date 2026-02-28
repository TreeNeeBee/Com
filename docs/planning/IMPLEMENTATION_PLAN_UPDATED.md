# Com模块新架构实施优化方案（基于SERVICE_DISCOVERY_ARCHITECTURE.md）

**文档版本**: 3.0（采用零Daemon固定槽位架构 + 双层IDL设计）  
**创建日期**: 2025-11-24  
**基于标准**: AUTOSAR Adaptive Platform R24-11  
**参考设计**: `SERVICE_DISCOVERY_ARCHITECTURE.md` v3.1  
**架构特性**: 零Daemon + 固定槽位 + 双注册表物理隔离 + seqlock + 槽位0保护 + 广播互通 + 双层IDL  

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

**核心架构变更** (v3.1):
1. ✅ **服务发现**: 采用固定槽位映射 + 双注册表物理隔离（QM+AB Registry + ASIL-CD Registry）
2. ✅ **零冲突映射**: `slot = service_id & 1023` 直接位运算，天然映射无冲突
3. ✅ **槽位 0 保护**: 两个 Registry 的槽位 0 都禁止使用，用于错误检测
4. ✅ **广播互通**: 槽位 1023 实现跨安全等级广播事件
5. ✅ **零Daemon**: 完全去中心化，无RouDi/无守护进程
6. ✅ **超高性能**: < 500ns服务发现延迟（相比传统1-5µs）
7. ✅ **FuSa就绍**: ISO 26262 ASIL-D物理隔离
8. ✅ **双层IDL设计**: Franca IDL (SSOT) → AUTOSAR API + DDS IDL
   - PyFranca 生成 AUTOSAR ara::com API (应用层)
   - 自动转换为 DDS IDL，FastDDS-gen 生成 TypeSupport (传输层)
   - 强制版本一致性验证 (Schema Hash + TypeIdentifier)
   - QoS 独立配置 (YAML)，不污染 IDL
   - DDS 类型完全隔离，应用层零依赖

**实施优先级调整**:
- 🔴 P0: 固定槽位注册表 + Binding Manager（核心架构）
- 🟡 P1: CoreIPC + DDS Binding（高性能通信）
- 🟢 P2: 系统优化 + Custom Protocol（最后实施）

---

## 📊 最新实施状态 (2025-11-23)

### 已完成阶段

| 阶段 | 状态 | 完成日期 | 验证文档 |
|------|------|---------|---------|
| **Phase 1: 固定槽位服务注册表** | ✅ **完成** | 2025-11-20 | SERVICE_DISCOVERY_ARCHITECTURE.md |
| **Phase 2: Binding Manager** | ✅ **完成** | 2025-11-21 | PHASE2_COMPLETE_FEATURES.md |
| **Phase 3: CoreIPC Binding** | ✅ **完成** | 2025-11-23 | BINDING_STANDARDIZATION_STATUS.md |
| **Phase 4: DDS + AF_XDP** | 🔄 **进行中** | 2025-11-23 启动 | PHASE4_DDS_IMPLEMENTATION_STATUS.md |

### Phase 3 详细进度 ✅ 100% 完成

**当前完成度**: 100% ✅ **已完成所有任务**

| 子任务 | 状态 | 说明 |
|--------|------|------|
| ITransportBinding 接口定义 | ✅ 完成 | 18个方法，100% AUTOSAR 标准 |
| Iceoryx2Binding 框架实现 | ✅ 完成 | 完整的接口实现 |
| CoreIPC API 集成 | ✅ 完成 | 使用 v0.7.0 API |
| 配置参数化 | ✅ 完成 | Iceoryx2Config 结构体 |
| 多尺寸消息验证 | ✅ 完成 | 1B-1024B 测试通过 |
| Binding 标准化接口 | ✅ 完成 | 100% 符合 ITransportBinding |
| 性能指标集成 | ✅ 完成 | SendEvent/listenerThread 统计完整 |
| 线程安全验证 | ✅ 完成 | mutex + atomic 设计合理 |
| **集成测试** | ✅ **完成** | **3个测试全部通过（414ms）** |

**最近成果** (2025-11-23):
- ✅ 创建 CoreIPC 集成测试（test_coreipc_binding_manager_integration）
- ✅ 测试1：DirectBindingCreation - 通过 (2ms)
- ✅ 测试2：CompletePubSubFlow - 通过 (204ms, 10/10消息)
- ✅ 测试3：PerformanceMetrics - 通过 (207ms, 20/20消息)
- ✅ **Phase 3 任务 100% 完成**

**测试结果**:
```
[==========] Running 3 tests from 1 test suite.
[  PASSED  ] 3 tests. (414 ms total)
  - DirectBindingCreation: 2ms
  - CompletePubSubFlow: 204ms (10 messages, data verified)
  - PerformanceMetrics: 207ms (20 messages, metrics validated)
```

**详细报告**: 参见 `BINDING_STANDARDIZATION_STATUS.md`

### Phase 4 详细进度 🔄 70% 进行中

**当前完成度**: 70% （FastDDS核心实现完成，待编译验证和性能测试）

| 子任务 | 状态 | 完成度 | 说明 |
|--------|------|--------|------|
| DDS Binding 核心框架 | ✅ 完成 | 100% | DdsBinding.hpp/cpp 实现（1050+ lines）|
| CMake 构建配置 | ✅ 完成 | 100% | DdsBindingConfig.cmake（213 lines）|
| 基础单元测试 | ✅ 完成 | 100% | test_dds_binding.cpp（192 lines）|
| FastDDS API 适配 | ✅ 完成 | 100% | 完整DomainParticipant/Publisher/Subscriber |
| Publisher/Subscriber | ✅ 完成 | 100% | DataWriter/DataReader + QoS配置 |
| 服务发现 | ✅ 完成 | 100% | DdsDiscoveryListener（本地+远程）|
| 性能指标收集 | ✅ 完成 | 100% | TransportMetrics 延迟/吞吐量统计 |
| 编译验证 | 🔄 进行中 | 50% | 需要完整构建测试 |
| 集成测试 | ⏳ 待开始 | 0% | 跨进程pub/sub测试 |
| 性能基准测试 | ⏳ 待开始 | 0% | <15µs延迟验证 |
| AF_XDP 集成 | ⏳ 待开始 | 0% | Week 2 计划 |

**已创建文件** (2025-11-24 更新):
- ✅ `source/binding/dds/inc/DdsBinding.hpp` (282行) - 完整FastDDS API
- ✅ `source/binding/dds/src/DdsBinding.cpp` (839行) - 核心实现完成
- ~~`source/binding/dds/idl/LapComMessage.idl`~~ - **已移除** (2026/02/24)
- ~~`source/binding/dds/idl/LapComMessage.h`~~ - **已移除** (2026/02/24)
- ~~`source/binding/dds/idl/LapComMessagePubSubTypes.h`~~ - **已移除** (2026/02/24)
- ✅ `source/binding/dds/inc/CDdsPayload.hpp` - 代码定义的DDS wire type (替代IDL)
- ✅ `source/binding/dds/inc/IDdsTypeAdapter.hpp` - Per-service类型适配器接口
- ✅ `source/binding/dds/inc/CDdsTypeRegistry.hpp` - 运行时类型注册表
- ✅ `cmake/DdsBindingConfig.cmake` (213行) - 完整构建配置
- ✅ `test/binding/dds/test_dds_binding.cpp` (192行) - 单元测试框架
- ✅ `doc/reports/PHASE4_DDS_IMPLEMENTATION_STATUS.md` - 详细状态报告
- ✅ `doc/planning/CODE_IMPLEMENTATION_ASSESSMENT.md` - 代码审查报告
- ✅ `archive/deprecated/SlotAllocator.hpp.v2.x` - 已归档过时代码

**系统环境验证**:
- ✅ FastDDS 2.9.1 已安装并集成
- ✅ libfastrtps.so 库可用
- ✅ fastddsgen 工具已安装
- ✅ FastDDS C++ API 完整实现（DomainParticipant/Publisher/Subscriber）
- ✅ QoS配置完成（RELIABLE/BEST_EFFORT, TRANSIENT_LOCAL）
- ✅ DdsReaderListener异步回调完成
- ✅ DdsDiscoveryListener服务发现完成

**下一步任务** (Week 1 剩余):
1. ✅ FastDDS C++ API 适配 - 已完成
2. ✅ IDL 数据类型生成（fastddsgen）- 已完成
3. ✅ Publisher/Subscriber 实现完善 - 已完成
4. 🔄 编译验证 - 进行中
5. ⏳ 跨进程 pub/sub 测试 - 待开始
6. ⏳ 性能基准测试（<15µs延迟）- 待开始
7. ⏳ BindingManager集成验证 - 待开始

**详细报告**: 参见 `PHASE4_DDS_IMPLEMENTATION_STATUS.md`

---

## 目录

1. [架构设计依据](#1-架构设计依据)
2. [实施路线图](#2-实施路线图)
3. [Phase 1: 固定槽位服务注册表](#3-phase-1-固定槽位服务注册表)
4. [Phase 2: Binding Manager](#4-phase-2-binding-manager)
5. [Phase 3: CoreIPC Binding](#5-phase-3-coreipc-binding)
6. [Phase 4: DDS + AF_XDP](#6-phase-4-dds--af_xdp)
7. [Phase 5: 系统优化 + Custom Protocol](#7-phase-5-系统优化--custom-protocol)
8. [验收标准](#8-验收标准)

---

## 1. 架构设计依据

### 1.1 SERVICE_DISCOVERY_ARCHITECTURE.md核心设计

**文档位置**: `modules/Com/doc/SERVICE_DISCOVERY_ARCHITECTURE.md`

**核心设计原则** (v3.1):
- ✅ **零Daemon**: 无RouDi/无中央服务器/无守护进程
- ✅ **固定槽位**: 服务ID范围 → 槽位范围映射
- ✅ **零冲突映射**: `slot = service_id & 1023` 直接位运算，天然映射
- ✅ **槽位 0 保护**: 两个 Registry 的槽位 0 都禁止使用（service_id 0x0000 和 0xF000 非法）
- ✅ **双注册表**: QM+AB Registry (service_id 0x0001~0x03FE) + ASIL-CD Registry (service_id 0xF001~0xF3FE)
- ✅ **广播互通**: 槽位 1023 实现跨安全等级广播 (service_id 0xFFFF)
- ✅ **seqlock同步**: 无锁并发读取，原子写入
- ✅ **256字节槽位**: 4×cache-line对齐，包含完整服务元数据
- ✅ **Instance ID 位域**: service_id(16) | instance_no(8) | domain(4) | asil_level(3) | redundancy(1)
- ✅ **双层IDL**: Franca IDL (SSOT) → AUTOSAR API + DDS IDL，强制版本一致性

**架构对比**:

| 特性 | 传统方案 | 本架构 (v3.1) |
|------|----------|---------------|
| **延迟** | 1-5µs (Domain Socket) | < 500ns (共享内存直接读取) |
| **守护进程** | RouDi / systemd-resolved | ❌ 零 Daemon |
| **单点故障** | ✅ 存在 | ❌ 无 |
| **槽位映射** | Hash 冲突 | ✅ 零冲突 (slot = service_id & 1023) |
| **槽位 0** | 未明确 | ✅ 强制保护禁用 |
| **广播机制** | 单向 | ✅ 双向互通 (槽位 1023) |
| **FuSa隔离** | 需隔离Daemon | ✅ 双注册表物理隔离 |
| **IDL架构** | 单层 | ✅ 双层 (Franca → AUTOSAR + DDS) |
| **代码复杂度** | 高 (>2000行) | ✅ 低 (~800行) |

### 1.2 槽位分配策略（零冲突映射 + 双注册表）

**设计依据**: `SERVICE_DISCOVERY_ARCHITECTURE.md` §2.1.1

```cpp
// 零冲突槽位映射：直接位运算
inline uint32_t CalculateSlot(uint16_t service_id) {
    uint32_t slot = service_id & 1023;  // 取低 10 位，天然映射到 0~1023
    
    // 槽位 0 保护：禁止使用（service_id 0x0000 和 0xF000 非法）
    if (slot == 0) {
        // 运行时错误：service_id 0x0000 或 0xF000 非法
        return INVALID_SLOT;  // 或 throw std::invalid_argument("slot 0 reserved");
    }
    
    return slot;
}

// 根据 service_id 自动选择 Registry
RegistryType SelectRegistry(uint16_t service_id) {
    if (service_id >= 0xF001 && service_id <= 0xF3FE) {
        return RegistryType::ASIL_CD;  // ASIL-C/D 高安全等级服务段
    } else if (service_id == 0xFFFF) {
        // 广播服务：需同时写入两个 Registry 的槽位 1023
        return RegistryType::BOTH;
    } else if (service_id >= 0x0001 && service_id <= 0x03FE) {
        return RegistryType::QM_AB;  // QM/ASIL-A/B 低/中等安全等级服务段
    } else {
        // service_id 0x0000 或 0xF000 非法（映射到槽位 0）
        return RegistryType::INVALID;
    }
}

// 根据 service_id 推荐槽位范围（槽位 0 保留禁止使用）
std::pair<uint32_t, uint32_t> GetSlotRange(uint16_t service_id) {
    // QM+AB Registry
    if (service_id >= 0x0001 && service_id <= 0x01FF) {
        return {1, 511};     // 感知主力区 (QM/ASIL-A/B)（槽位 0 跳过）
    } else if (service_id >= 0x0200 && service_id <= 0x02FF) {
        return {512, 767};   // 规划/决策区 (QM/ASIL-B)
    } else if (service_id >= 0x0300 && service_id <= 0x035F) {
        return {768, 863};   // 控制/底盘 (ASIL-A/B)
    } else if (service_id >= 0x0360 && service_id <= 0x03BF) {
        return {864, 959};   // 车身/舒适/娱乐 (QM)
    } else if (service_id >= 0x03C0 && service_id <= 0x03FE) {
        return {960, 1022};  // 平台中间件 (QM/ASIL-A)
    }
    // ASIL-CD Registry
    else if (service_id >= 0xF001 && service_id <= 0xF1FF) {
        return {1, 511};     // ASIL-D 关键控制区（槽位 0 跳过）
    } else if (service_id >= 0xF200 && service_id <= 0xF2FF) {
        return {512, 767};   // ASIL-D 冗余控制区
    } else if (service_id >= 0xF300 && service_id <= 0xF37F) {
        return {768, 895};   // ASIL-C 中间件区
    } else if (service_id >= 0xF380 && service_id <= 0xF3DF) {
        return {896, 991};   // 平台中间件 (ASIL-C/D)
    } else if (service_id == 0xFFFF) {
        return {1023, 1023}; // 全局广播槽
    } else if (service_id == 0x0000 || service_id == 0xF000) {
        return {0, 0};       // 错误：槽位 0 保留禁止使用
    }
    return {1, 1022};  // 默认范围（跳过槽位 0 和 1023）
}
```

**零冲突设计优势**:
- ✅ **O(1) 查找**: 直接位运算，无需哈希表
- ✅ **零冲突**: service_id 高位段已隔离，低 10 位唯一
- ✅ **广播完美映射**: 0xFFFF & 1023 = 1023，天然映射到广播槽
- ✅ **槽位 0 保护**: 0x0000 和 0xF000 映射到槽位 0，运行时拒绝注册

### 1.3 双注册表物理隔离

**设计依据**: `SERVICE_DISCOVERY_ARCHITECTURE.md` §1.4 架构层次

```
QM+AB Registry:   /dev/shm/lap_com_registry_qm    (256KB, 1024 slots, 权限 0666)
  - 槽位 0:      保留禁止使用 (用于错误检测)
  - 槽位 1:      SD-Proxy 主实例 (service_id 0x0001)
  - 槽位 2~511:  感知主力区 (service_id 0x0002~0x01FF)
  - 槽位 512:    SD-Proxy 备份实例 (service_id 0x0200)
  - 槽位 513~1022: 其他 QM/ASIL-A/B 服务
  - 槽位 1023:   全局广播槽 (service_id 0xFFFF)

ASIL-CD Registry: /dev/shm/lap_com_registry_asil (256KB, 1024 slots, 权限 0640)
  - 槽位 0:      保留禁止使用 (用于错误检测)
  - 槽位 1~511:  ASIL-D 关键控制区 (service_id 0xF001~0xF1FF)
  - 槽位 512~767: ASIL-D 冗余控制区 (service_id 0xF200~0xF2FF)
  - 槽位 768~1022: ASIL-C 中间件区 (service_id 0xF300~0xF3FE)
  - 槽位 1023:   紧急广播槽 (service_id 0xFFFF)
```

**隔离策略**:
- **QM+AB Registry**: 所有进程可读写（感知/规划/娱乐等非安全关键服务）
- **ASIL-CD Registry**: 控制进程写入，其他进程只读（制动/转向等安全关键服务）
- **物理隔离**: 两个独立 memfd，内核级别进程隔离
- **槽位 0 保护**: 两个 Registry 的槽位 0 都禁止使用，用于 NULL 检测和错误边界保护
- **广播互通**: 所有进程订阅两个 Registry 的槽位 1023，实现跨安全等级系统级事件通知

---

## 🎯 当前任务: Phase 3 Week 10 - Binding 接口标准化

### 任务目标

完成 CoreIPC Binding 与标准 ITransportBinding 接口的完全对接，确保：
1. ✅ 符合 AUTOSAR R24-11 标准接口规范
2. ✅ 正确实现所有必需方法
3. ✅ 提供完整的能力查询接口
4. ✅ 支持 Binding Manager 动态加载

### 已完成基础

**现有实现**:
- ✅ `ITransportBinding.hpp` - 标准接口定义（375行）
- ✅ `Iceoryx2Binding.hpp/cpp` - 完整框架实现（600+行）
- ✅ CoreIPC API 集成
- ✅ Publisher/Subscriber 实现
- ✅ Event-Driven 架构（epoll + 监听线程）
- ✅ 配置参数化（Iceoryx2Config）
- ✅ 多尺寸消息验证（1B-1024B）

**待优化项**:
1. 确保所有 ITransportBinding 方法正确实现
2. 完善 GetCapabilities() 能力查询
3. 添加详细的性能指标收集
4. 确保线程安全和资源清理
5. 编写集成测试验证与 BindingManager 的对接

### 实施步骤

#### Step 1: 验证接口完整性 (1小时)

**检查清单**:
- [ ] 检查 Iceoryx2Binding 是否实现了 ITransportBinding 所有纯虚函数
- [ ] 验证方法签名是否完全匹配
- [ ] 确认返回值类型正确（Result<T>）
- [ ] 检查 noexcept 规范符合要求

**参考文档**:
- `ITransportBinding.hpp` 完整接口定义
- `AUTOSAR_AP_SWS_CommunicationManagement.pdf` §8.3

#### Step 2: 完善能力查询接口 (2小时)

**任务**:
- [ ] 实现 `GetCapabilities()` 返回详细能力描述
- [ ] 实现 `SupportsService()` 正确判断服务支持
- [ ] 完善 `GetMetrics()` 性能指标收集
- [ ] 添加 AUTOSAR 需求追溯注释

**代码示例**:
```cpp
Result<BindingCapabilities> Iceoryx2Binding::GetCapabilities() const noexcept {
    BindingCapabilities caps;
    caps.name = "coreipc";
    caps.priority = 100;
    caps.supports_zero_copy = true;
    caps.supports_event = true;
    caps.supports_method = false;  // CoreIPC 不支持 RPC
    caps.supports_field = false;
    caps.max_payload_size = config_.max_payload_size;
    caps.scope = TransportScope::LOCAL;  // 仅本地通信
    return Result<BindingCapabilities>::Success(caps);
}
```

#### Step 3: 线程安全和资源管理 (2小时)

**检查项**:
- [ ] 所有共享状态使用 std::mutex 保护
- [ ] Publisher/Subscriber 生命周期正确管理
- [ ] 监听线程安全启动和停止
- [ ] Shutdown() 确保清理所有资源
- [ ] 避免内存泄漏（使用 shared_ptr/unique_ptr）

**关键代码检查**:
```cpp
// 示例：确保线程安全的服务注册
Result<void> Iceoryx2Binding::OfferService(...) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 检查是否已存在
    auto key = makeServiceKey(service_id, instance_id);
    if (publishers_.find(key) != publishers_.end()) {
        return Result<void>::Error(ErrorCode::SERVICE_ALREADY_OFFERED);
    }
    
    // 创建 publisher
    // ...
    
    publishers_[key] = std::move(publisher);
    return Result<void>::Success();
}
```

#### Step 4: 性能指标完善 (1小时)

**任务**:
- [ ] 在 SendEvent() 中更新发送统计
- [ ] 在监听线程中更新接收统计
- [ ] 记录延迟数据（可选：使用环形缓冲区计算 P99）
- [ ] 实现 GetMetrics() 返回实时数据

**指标清单**:
```cpp
struct PerformanceMetrics {
    uint64_t messages_sent = 0;
    uint64_t messages_received = 0;
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
    uint64_t send_errors = 0;
    uint64_t receive_errors = 0;
    double avg_latency_ns = 0.0;
    double p99_latency_ns = 0.0;
};
```

#### Step 5: 集成测试 (2小时)

**测试内容**:
- [ ] 创建 `test_coreipc_binding_manager_integration.cpp`
- [ ] 测试 BindingManager 动态加载 CoreIPC 插件
- [ ] 测试优先级选择（CoreIPC | Core IPC (实际)应为本地通信首选）
- [ ] 测试完整的 Offer -> Subscribe -> Send -> Receive 流程
- [ ] 验证 Shutdown 清理正确

**测试代码框架**:
```cpp
TEST(Iceoryx2Integration, BindingManagerLoadAndSelect) {
    BindingManager manager;
    
    // 1. 加载 CoreIPC binding
    BindingConfig config;
    config.name = "coreipc";
    config.library_path = "./binding_coreipc.so";
    config.priority = 100;
    ASSERT_TRUE(manager.LoadBinding(config).IsSuccess());
    
    // 2. 选择 binding
    auto binding = manager.SelectBinding(SERVICE_LOCAL, INSTANCE_1);
    ASSERT_NE(binding, nullptr);
    ASSERT_EQ(binding->GetName(), "coreipc");
    
    // 3. 测试通信
    binding->OfferService(SERVICE_LOCAL, INSTANCE_1);
    binding->SubscribeEvent(SERVICE_LOCAL, INSTANCE_1, EVENT_1, callback);
    binding->SendEvent(SERVICE_LOCAL, INSTANCE_1, EVENT_1, data);
    
    // 验证回调被触发
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_TRUE(callback_triggered);
}
```

### 验收标准

| 检查项 | 标准 | 验证方法 |
|--------|------|---------|
| 接口完整性 | 100% ITransportBinding 方法实现 | 编译通过 + 代码审查 |
| 能力查询 | 返回正确的能力描述 | 单元测试 |
| 线程安全 | 无数据竞争/死锁 | ThreadSanitizer 检测 |
| 资源清理 | 无内存泄漏 | Valgrind 检测 |
| 性能指标 | GetMetrics() 返回实时数据 | 集成测试验证 |
| Binding Manager 集成 | 动态加载成功 | 集成测试通过 |
| 通信功能 | 完整的 pub/sub 流程 | 端到端测试 |

### 预计工时

| 任务 | 预计时间 | 优先级 |
|------|---------|--------|
| Step 1: 接口验证 | 1 小时 | 🔴 高 |
| Step 2: 能力查询 | 2 小时 | 🔴 高 |
| Step 3: 线程安全 | 2 小时 | 🟡 中 |
| Step 4: 性能指标 | 1 小时 | 🟡 中 |
| Step 5: 集成测试 | 2 小时 | 🔴 高 |
| **总计** | **8 小时** | - |

---

## 2. 实施路线图

### 2.1 总体时间线（20周 = 5个月）

```
阶段         Week 1-3    Week 4-5    Week 6-10   Week 11-14  Week 15-17  Week 18-20
           ┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
Phase 1    │███████████│          │          │          │          │          │ 固定槽位服务注册表
Phase 2    │          │██████████│          │          │          │          │ Binding Manager
Phase 3    │          │          │███████████████████████          │          │ CoreIPC Binding
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
| **M3** | Week 10 | CoreIPC Binding | IPC延迟 < 1µs |
| **M4** | Week 14 | DDS + AF_XDP | 跨ECU延迟 < 15µs |
| **M5** | Week 17 | 系统优化 | CPU占用 < 1% |
| **M6** | Week 20 | Custom Protocol + 完整测试 | 全功能验证 |

---

## 3. Phase 1: 固定槽位服务注册表

### 3.1 目标

**设计依据**: `SERVICE_DISCOVERY_ARCHITECTURE.md` §2.1 核心数据结构

实现完全去中心化的服务注册表，基于CoreIPC共享内存，达到：
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
    char binding_type[16];           // "coreipc", "dds", "someip"
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

## 5. Phase 3: CoreIPC Binding

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

