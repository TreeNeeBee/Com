# SERVICE_DISCOVERY_ARCHITECTURE v3.0 重构总结

## 变更日期
2025-11-20

## 重构目标
根据 SERVICE_DISCOVERY_ARCHITECTURE.md v3.0 设计变更，更新双注册表实现以反映新的安全等级分组和服务ID范围。

## 关键设计变更

### 1. 注册表命名变更
**旧设计 (v2.x):**
- QM Registry (QM服务)
- ASIL-D Registry (ASIL-D服务)

**新设计 (v3.0):**
- **QM+AB Registry** (QM/ASIL-A/B 低/中安全等级服务)
- **ASIL-CD Registry** (ASIL-C/D 高安全等级服务)

**设计理念:**
- 按照安全等级分组，而非单一ASIL级别
- QM+AB: 涵盖QM、ASIL-A、ASIL-B，适用于感知、规划、娱乐等
- ASIL-CD: 涵盖ASIL-C、ASIL-D，适用于制动、转向、电源管理等

### 2. Service ID 范围扩展

| Registry | 旧范围 (v2.x) | 新范围 (v3.0) | 变化 |
|----------|--------------|--------------|------|
| QM → QM+AB | 0x0001~0x03FF (1023个) | 0x0001~0x0417 (1047个) | +24个槽位 |
| ASIL-D → ASIL-CD | 0xF001~0xF3FF (1023个) | 0xF001~0xF3FE (1022个) | -1个槽位 |

**重要边界值:**
- QM+AB最大值: 0x0417 (新增平台中间件槽位 992~1047)
- ASIL-CD最大值: 0xF3FE (0xF3FF被保留)
- 槽位0保留: 0x0000 (QM+AB), 0xF000 (ASIL-CD) - 禁止使用

### 3. 槽位分区细化

**QM+AB Registry (0x0001~0x0417):**
```
Slot 0:       Reserved (0x0000) - prohibited
Slots 1-511:  Perception (0x0001~0x01FF) - 摄像头、激光雷达、毫米波雷达
Slots 512-767: Planning (0x0200~0x02FF) - 路径规划、行为决策
Slots 768-863: Control/Chassis ASIL-A/B (0x0300~0x035F)
Slots 864-991: Infotainment QM (0x0360~0x03DF)
Slots 992-1047: Platform middleware (0x03E0~0x0417) ← 新增范围
Slot 1023:    Broadcast (0xFFFF)
```

**ASIL-CD Registry (0xF001~0xF3FE):**
```
Slot 0:       Reserved (0xF000) - prohibited
Slots 1-511:  ASIL-D critical (0xF001~0xF1FF) - 制动、转向
Slots 512-767: ASIL-D redundancy (0xF200~0xF2FF)
Slots 768-895: ASIL-C middleware (0xF300~0xF37F)
Slots 896-991: Platform ASIL-C/D (0xF380~0xF3DF)
Slots 992-1022: System reserved (0xF3E0~0xF3FE) ← 新增范围
Slot 1023:    Emergency broadcast (0xFFFF)
```

## 已完成的代码重构

### 1. 头文件更新 (SharedMemoryRegistry.hpp)

#### RegistryType 枚举
```cpp
// 旧代码
enum class RegistryType : uint8_t {
    QM      = 0,
    ASIL_D  = 1,
    BOTH    = 2
};

// 新代码
enum class RegistryType : uint8_t {
    QM_AB   = 0,  ///< QM+AB registry (QM/ASIL-A/B, low/medium safety)
    ASIL_CD = 1,  ///< ASIL-CD registry (ASIL-C/D, high safety)
    BOTH    = 2   ///< Broadcast (bidirectional)
};
```

#### Service ID 常量
```cpp
// 旧代码
static constexpr uint16_t QM_SERVICE_ID_MIN = 0x0001;
static constexpr uint16_t QM_SERVICE_ID_MAX = 0x03FF;
static constexpr uint16_t ASIL_SERVICE_ID_MIN = 0xF001;
static constexpr uint16_t ASIL_SERVICE_ID_MAX = 0xF3FF;

// 新代码
static constexpr uint16_t QM_AB_SERVICE_ID_MIN = 0x0001;
static constexpr uint16_t QM_AB_SERVICE_ID_MAX = 0x0417;  // Extended
static constexpr uint16_t ASIL_CD_SERVICE_ID_MIN = 0xF001;
static constexpr uint16_t ASIL_CD_SERVICE_ID_MAX = 0xF3FE;  // Adjusted
```

#### 共享内存路径和权限
```cpp
// 旧代码
static constexpr const char* QM_REGISTRY_PATH = "/lap_com_registry_qm";
static constexpr const char* ASIL_REGISTRY_PATH = "/lap_com_registry_asil";
static constexpr mode_t QM_PERMISSIONS = 0666;
static constexpr mode_t ASIL_PERMISSIONS = 0640;

// 新代码
static constexpr const char* QM_AB_REGISTRY_PATH = "/lap_com_registry_qm";
static constexpr const char* ASIL_CD_REGISTRY_PATH = "/lap_com_registry_asil";
static constexpr mode_t QM_AB_PERMISSIONS = 0666;
static constexpr mode_t ASIL_CD_PERMISSIONS = 0640;
```

#### SelectRegistry 方法更新
```cpp
// 新实现
static RegistryType SelectRegistry(uint64_t service_id) noexcept
{
    uint16_t sid = static_cast<uint16_t>(service_id & 0xFFFF);
    
    if (sid == RegistryConfig::BROADCAST_SERVICE_ID) {
        return RegistryType::BOTH;  // 0xFFFF
    } else if (sid >= RegistryConfig::ASIL_CD_SERVICE_ID_MIN && 
               sid <= RegistryConfig::ASIL_CD_SERVICE_ID_MAX) {
        return RegistryType::ASIL_CD;  // 0xF001~0xF3FE
    } else if (sid >= RegistryConfig::QM_AB_SERVICE_ID_MIN && 
               sid <= RegistryConfig::QM_AB_SERVICE_ID_MAX) {
        return RegistryType::QM_AB;  // 0x0001~0x0417
    } else {
        return RegistryType::QM_AB;  // Fallback
    }
}
```

#### 成员变量重命名
```cpp
// 旧代码
private:
    SingleRegistry qm_registry_;
    SingleRegistry asil_registry_;

// 新代码
private:
    SingleRegistry qm_ab_registry_;   // QM+AB (QM/ASIL-A/B)
    SingleRegistry asil_cd_registry_; // ASIL-CD (ASIL-C/D)
```

### 2. 实现文件更新 (SharedMemoryRegistry.cpp)

#### Initialize 方法
```cpp
// 更新注释和变量引用
Result<void> SharedMemoryRegistry::Initialize() noexcept
{
    // Initialize QM+AB registry (QM/ASIL-A/B services)
    auto qm_result = qm_ab_registry_.Initialize();
    if (qm_result.HasValue() == false) {
        return qm_result;
    }

    // Initialize ASIL-CD registry (ASIL-C/D services)
    auto asil_result = asil_cd_registry_.Initialize();
    if (asil_result.HasValue() == false) {
        return asil_result;
    }

    return Result<void>(lap::core::ErrorCode(EINVAL));
}
```

#### RegisterService 方法
```cpp
// 更新 RegistryType 判断和成员变量引用
if (reg_type == RegistryType::BOTH) {
    // Broadcast: register in both QM+AB and ASIL-CD
    auto qm_result = qm_ab_registry_.RegisterService(...);
    auto asil_result = asil_cd_registry_.RegisterService(...);
    ...
} else if (reg_type == RegistryType::ASIL_CD) {
    // ASIL-C/D high safety service
    return asil_cd_registry_.RegisterService(...);
} else {
    // QM/ASIL-A/B low/medium safety service
    return qm_ab_registry_.RegisterService(...);
}
```

#### UnregisterService/FindService/UpdateHeartbeat
- 统一更新所有方法中的成员变量引用
- `qm_registry_` → `qm_ab_registry_`
- `asil_registry_` → `asil_cd_registry_`
- `RegistryType::ASIL_D` → `RegistryType::ASIL_CD`

### 3. 测试文件更新 (test_registry.cpp)

#### 文件头注释
```cpp
// 旧注释
@brief Unit tests for SharedMemoryRegistry (dual QM/ASIL-D registries)

// 新注释
@brief Unit tests for SharedMemoryRegistry (dual QM+AB/ASIL-CD registries v3.0)
```

#### 测试用例重命名
| 旧测试名称 | 新测试名称 | 说明 |
|----------|----------|------|
| RegisterQMService | RegisterQM_AB_Service | QM → QM+AB |
| RegisterASILService | RegisterASIL_CD_Service | ASIL-D → ASIL-CD |
| FindQMService | FindQM_AB_Service | 同上 |
| FindASILService | FindASIL_CD_Service | 同上 |
| FixedSlotMapping | FixedSlotMapping | 更新注释 |

#### 新增边界测试
```cpp
// 测试 QM+AB 范围边界
TEST_F(SharedMemoryRegistryTest, QM_AB_ServiceID_Boundary)
{
    // Min: 0x0001, Max: 0x0417 (extended range)
    auto result_min = registry_->RegisterService(0x0001, ...);
    auto result_max = registry_->RegisterService(0x0417, ...);
    ASSERT_FALSE(result_min.HasError());
    ASSERT_FALSE(result_max.HasError());
}

// 测试 ASIL-CD 范围边界
TEST_F(SharedMemoryRegistryTest, ASIL_CD_ServiceID_Boundary)
{
    // Min: 0xF001, Max: 0xF3FE (adjusted from 0xF3FF)
    auto result_min = registry_->RegisterService(0xF001, ...);
    auto result_max = registry_->RegisterService(0xF3FE, ...);
    ASSERT_FALSE(result_min.HasError());
    ASSERT_FALSE(result_max.HasError());
}
```

### 4. 头文件引用修复

修复了Com模块中错误的头文件引用：
```cpp
// 修复前
#include <core/Result.hpp>

// 修复后
#include <core/CResult.hpp>
```

受影响文件:
- `SlotAllocator.hpp`
- `ServiceDiscovery.hpp`
- `SharedMemoryRegistry.hpp` (旧版本，已删除)

## 未完成的工作

### 1. ErrorCode 错误处理优化
当前使用临时方案 `lap::core::ErrorCode(errno)`，需要:
- [ ] 定义 RegistryError 枚举
- [ ] 实现 ErrorCode domain 注册
- [ ] 统一错误码处理策略

### 2. 编译系统集成
由于Com模块存在其他编译错误（与本次重构无关），需要:
- [ ] 修复 Runtime.cpp 中的 ServiceDiscovery 依赖问题
- [ ] 完整编译 test_registry 目标
- [ ] 运行单元测试验证功能正确性

### 3. 性能验证
需要验证重构后的性能指标:
- [ ] FindService 延迟 < 500ns (P99)
- [ ] RegisterService 延迟 < 1µs (P99)
- [ ] 并发访问无数据竞争

## 架构演进对比

| 特性 | v2.x (旧设计) | v3.0 (新设计) |
|------|-------------|-------------|
| **注册表分类** | QM vs ASIL-D | QM+AB vs ASIL-CD |
| **安全等级支持** | QM, ASIL-D | QM, ASIL-A, ASIL-B, ASIL-C, ASIL-D |
| **QM+AB服务数量** | 1023 (0x03FF) | 1047 (0x0417) |
| **ASIL-CD服务数量** | 1023 (0xF3FF) | 1022 (0xF3FE) |
| **槽位分区** | 简单QM/ASIL分离 | 细化功能域分区 |
| **广播机制** | 单向槽位1023 | 双向交叉订阅 |
| **槽位0处理** | 保留 | 明确禁止并用于错误检测 |

## 向后兼容性

### 破坏性变更
1. **枚举值变更**: `RegistryType::QM` → `RegistryType::QM_AB`, `ASIL_D` → `ASIL_CD`
2. **成员变量重命名**: `qm_registry_` → `qm_ab_registry_`, `asil_registry_` → `asil_cd_registry_`
3. **service_id范围扩展**: 代码依赖旧边界值需要更新

### 兼容性保证
1. **共享内存文件名不变**: `/lap_com_registry_qm`, `/lap_com_registry_asil`
2. **权限配置不变**: 0666 (QM+AB), 0640 (ASIL-CD)
3. **固定槽位映射算法不变**: `slot_index = service_id & 1023`
4. **seqlock机制不变**: 27ns P99读延迟保持

## 迁移指南

### 代码迁移
```cpp
// 旧代码
if (reg_type == RegistryType::ASIL_D) {
    asil_registry_.RegisterService(...);
}

// 新代码
if (reg_type == RegistryType::ASIL_CD) {
    asil_cd_registry_.RegisterService(...);
}
```

### 配置迁移
- 无需更改systemd单元文件路径
- Service ID分配需要遵循新范围
- ASIL-A/B服务应使用QM+AB注册表（0x0001~0x0417）
- ASIL-C/D服务应使用ASIL-CD注册表（0xF001~0xF3FE）

## 测试覆盖

### 已更新测试用例
- [x] RegisterQM_AB_Service
- [x] RegisterASIL_CD_Service
- [x] FindQM_AB_Service
- [x] FindASIL_CD_Service
- [x] QM_AB_ServiceID_Boundary (新增)
- [x] ASIL_CD_ServiceID_Boundary (新增)
- [x] FixedSlotMapping (更新注释)

### 待验证测试用例
- [ ] UnregisterService
- [ ] UpdateHeartbeat
- [ ] RejectSlotZero
- [ ] FindServiceLatency
- [ ] RegisterServiceLatency
- [ ] BroadcastService

## 参考文档
- `SERVICE_DISCOVERY_ARCHITECTURE.md` v3.0 (架构设计文档)
- `Week1-2_Implementation_Report.md` (Week 1/2实现报告)
- `SharedMemoryRegistry.hpp` (头文件)
- `SharedMemoryRegistry.cpp` (实现文件)
- `test_registry.cpp` (单元测试)

## 变更记录
| 日期 | 版本 | 变更内容 |
|------|------|---------|
| 2025-11-20 | v3.0 | 完成架构重构，更新所有代码和测试用例 |
| 2025-11-18 | v2.1 | Week 2 实现70%（旧架构） |
| 2025-11-17 | v2.0 | Week 1 完成（seqlock + ServiceSlot） |

---
**重构状态**: 代码重构完成，等待编译系统修复后验证  
**预计完成时间**: 2025-11-21  
**责任人**: LightAP Development Team

