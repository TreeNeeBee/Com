# Com 模块实施计划文档

**最后更新**: 2025-11-24  
**当前架构**: v3.1 (零 Daemon + 双注册表 + 槽位 0 保护 + 广播互通 + 双层 IDL)  
**AUTOSAR 标准**: R24-11 (November 2024)

---

## 📋 当前有效文档

### 1. IMPLEMENTATION_PLAN_UPDATED.md ⭐

**主要实施计划文档**

- **版本**: v3.0
- **创建**: 2025-11-24
- **用途**: Com 模块完整实施计划和验收标准
- **内容**:
  - 前置条件与强制要求
  - 核心架构变更 (v3.1)
  - Phase 1-5 实施路线图
  - 当前实施状态
  - 验收标准

**适用人群**: 项目经理、架构师、开发团队

---

### 2. IMPLEMENTATION_ROADMAP_DETAILED.md

**详细实施路线图**

- **版本**: v2.0
- **创建**: 2025-11-24
- **用途**: 开发任务详细分解和代码模板
- **内容**:
  - Phase 1-3 详细任务清单
  - Day-by-Day 开发计划
  - 代码模板与最佳实践
  - 调试与故障排查指南

**适用人群**: 开发工程师、测试工程师

---

### 3. V3.1_ARCHITECTURE_UPDATE_SUMMARY.md

**架构升级总结**

- **创建**: 2025-11-24
- **用途**: v3.0 → v3.1 架构变更说明
- **内容**:
  - 核心架构更新（6 大特性）
  - 性能影响分析
  - 配置文件更新
  - 迁移指南
  - 验收标准

**适用人群**: 架构师、技术负责人、Code Review 人员

---

## 🏗️ v3.1 核心架构特性

### 1. 零冲突槽位映射
```cpp
uint16_t slot = service_id & 1023;  // 直接位运算，零冲突
```

### 2. 槽位 0 保护
```cpp
if (slot == 0) return INVALID_SLOT;  // 禁止 0x0000 和 0xF000
```

### 3. 双注册表物理隔离
- **QM+AB Registry**: service_id 0x0001~0x03FE (权限 0666)
- **ASIL-CD Registry**: service_id 0xF001~0xF3FE (权限 0640)

### 4. 广播槽位双向互通
- 槽位 1023 实现跨安全等级广播
- service_id 0xFFFF 同时写入两个 Registry

### 5. Instance ID 位域结构
```cpp
service_id(16) | instance_no(8) | domain(4) | asil_level(3) | redundancy(1)
```

### 6. 双层 IDL 设计
```
Franca IDL → AUTOSAR API + DDS IDL
```

---

## 📊 实施进度

### Phase 1: 固定槽位服务注册表
- **状态**: ✅ 完成
- **日期**: 2025-11-20

### Phase 2: Binding Manager
- **状态**: ✅ 完成
- **日期**: 2025-11-21

### Phase 3: iceoryx2 Binding
- **状态**: ✅ 完成
- **日期**: 2025-11-23
- **测试**: 3/3 通过 (414ms)

### Phase 4: DDS + AF_XDP
- **状态**: 🔄 进行中 (40%)
- **启动**: 2025-11-23

### Phase 5: 系统优化 + Custom Protocol
- **状态**: ⏳ 待开始

---

## 🗂️ 归档文档

历史版本和早期计划已移至 `archive/` 目录：

- `NEW_ARCHITECTURE_IMPLEMENTATION_PLAN.md` (v1.0, 2025-11-20)
- `COM_DEVELOPMENT_ROADMAP.md` (2025-10-30)
- `EPOCH_MIGRATION_PLAN.md` (2025-11-19)

详见 [`archive/ARCHIVE_README.md`](archive/ARCHIVE_README.md)

---

## 📚 相关文档

### 架构设计

- **SERVICE_DISCOVERY_ARCHITECTURE.md** (v3.1)
  - 路径: `../architecture/SERVICE_DISCOVERY_ARCHITECTURE.md`
  - 零 Daemon 固定槽位自注册架构详细设计

- **ARCHITECTURE_SUMMARY.md** (v3.1)
  - 路径: `../architecture/ARCHITECTURE_SUMMARY.md`
  - Com 模块完整架构总览

### AUTOSAR 标准

- **AUTOSAR_AP_EXP_ARAComAPI.pdf** (R24-11)
  - 路径: `../../../doc/R24-11/AUTOSAR_AP_EXP_ARAComAPI.pdf`
  - ara::com API 完整规范

- **AUTOSAR_AP_SWS_CommunicationManagement.pdf** (R24-11)
  - 路径: `../../../doc/R24-11/AUTOSAR_AP_SWS_CommunicationManagement.pdf`
  - 通信管理软件规范

### 实施报告

- **PHASE4_DDS_IMPLEMENTATION_STATUS.md**
  - 路径: `../reports/PHASE4_DDS_IMPLEMENTATION_STATUS.md`
  - DDS Binding 实施状态

- **BINDING_STANDARDIZATION_STATUS.md**
  - 路径: `../reports/BINDING_STANDARDIZATION_STATUS.md`
  - Binding 标准化状态

---

## 🚀 快速开始

### 如果你是新加入的开发者

1. **了解架构**: 阅读 `V3.1_ARCHITECTURE_UPDATE_SUMMARY.md`
2. **查看计划**: 阅读 `IMPLEMENTATION_PLAN_UPDATED.md`
3. **开始开发**: 参考 `IMPLEMENTATION_ROADMAP_DETAILED.md`

### 如果你是项目管理者

1. **检查进度**: 查看 `IMPLEMENTATION_PLAN_UPDATED.md` §实施状态
2. **评估风险**: 查看各 Phase 的验收标准
3. **跟踪报告**: 查看 `../reports/` 目录

### 如果你是架构审查者

1. **架构对比**: 阅读 `V3.1_ARCHITECTURE_UPDATE_SUMMARY.md` §核心架构更新
2. **标准符合**: 对照 AUTOSAR R24-11 标准文档
3. **设计细节**: 查看 `../architecture/SERVICE_DISCOVERY_ARCHITECTURE.md`

---

## 📞 联系方式

**问题上报**: 参考 `IMPLEMENTATION_PLAN_UPDATED.md` §问题上报机制

**文档维护**: LightAP Team

---

**最后更新**: 2025-11-24
