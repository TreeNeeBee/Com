# Planning Documents Archive

**归档日期**: 2025-11-24  
**归档原因**: 架构升级到 v3.1，保留最新实施计划

---

## 归档文档清单

### 1. NEW_ARCHITECTURE_IMPLEMENTATION_PLAN.md

**版本**: v1.0  
**日期**: 2025-11-20  
**归档原因**: 已被 `IMPLEMENTATION_PLAN_UPDATED.md` (v3.0) 替代  

**主要内容**:
- v3.0 零 Daemon 架构初版实施计划
- Phase 1-5 详细任务分解
- 基于 SERVICE_DISCOVERY_ARCHITECTURE.md v3.0

**替代文档**: `../IMPLEMENTATION_PLAN_UPDATED.md` (v3.0)

---

### 2. COM_DEVELOPMENT_ROADMAP.md

**版本**: 未标注  
**日期**: 2025-10-30  
**归档原因**: 早期开发路线图，已过时  

**主要内容**:
- Com 模块早期开发计划
- 传统架构设计（包含 Daemon）
- Phase 1-4 任务规划

**替代文档**: `../IMPLEMENTATION_PLAN_UPDATED.md` + `../IMPLEMENTATION_ROADMAP_DETAILED.md`

---

### 3. EPOCH_MIGRATION_PLAN.md

**版本**: 未标注  
**日期**: 2025-11-19  
**归档原因**: 特定迁移计划，已完成或不再适用  

**主要内容**:
- Epoch 架构迁移计划
- 从旧版本到新版本的迁移步骤
- 数据迁移和兼容性处理

**备注**: 如需参考迁移策略，可查阅此文档

---

## 当前有效文档

### 主要实施计划

1. **IMPLEMENTATION_PLAN_UPDATED.md** (v3.0)
   - 最新实施计划主文档
   - 基于 SERVICE_DISCOVERY_ARCHITECTURE.md v3.1
   - 包含零 Daemon + 双注册表 + 槽位 0 保护 + 广播互通 + 双层 IDL
   - 创建日期: 2025-11-24

2. **IMPLEMENTATION_ROADMAP_DETAILED.md** (v2.0)
   - 详细实施路线图
   - Phase 1-3 任务详细分解
   - 代码模板和最佳实践
   - 创建日期: 2025-11-24

3. **V3.1_ARCHITECTURE_UPDATE_SUMMARY.md**
   - v3.0 → v3.1 架构升级总结
   - 核心特性对比和迁移指南
   - 性能影响分析
   - 创建日期: 2025-11-24

---

## 架构演进历史

### v1.0 (2025-10-30)
- 传统架构，包含守护进程
- Hash 槽位映射
- 单注册表设计

### v2.0 (2025-11-19)
- 引入 Epoch 概念
- 迁移计划制定

### v3.0 (2025-11-20)
- 零 Daemon 架构
- 双注册表物理隔离
- 固定槽位映射（Hash）

### v3.1 (2025-11-24) ⭐ **当前版本**
- 零冲突槽位映射 (`slot = service_id & 1023`)
- 槽位 0 保护机制
- 广播槽位双向互通
- Instance ID 位域结构
- 双层 IDL 设计 (Franca → AUTOSAR + DDS)
- QM+AB Registry + ASIL-CD Registry 命名优化

---

## 文档查阅建议

### 如果你想了解...

**最新实施计划**:
→ 查看 `../IMPLEMENTATION_PLAN_UPDATED.md`

**详细开发步骤**:
→ 查看 `../IMPLEMENTATION_ROADMAP_DETAILED.md`

**v3.1 新特性**:
→ 查看 `../V3.1_ARCHITECTURE_UPDATE_SUMMARY.md`

**早期架构设计思路**:
→ 查看本目录归档文档

**完整架构设计**:
→ 查看 `../../architecture/SERVICE_DISCOVERY_ARCHITECTURE.md` (v3.1)

---

**维护者**: LightAP Team  
**AUTOSAR 标准**: R24-11 (November 2024)
