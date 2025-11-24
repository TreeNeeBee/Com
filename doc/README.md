# Com 模块文档索引

本目录包含 LightAP Com 模块的所有技术文档。

> **快速开始**: 请先阅读模块根目录的 [README.md](../README.md) 或 [README_CN.md](../README_CN.md) 了解模块概览、快速开始和 API 参考。
>
> 本文档目录包含更详细的架构设计、集成指南和开发计划。

## 📁 文档组织结构

### 📐 [architecture/](architecture/) - 架构设计文档
核心架构设计和技术规范文档
- `SERVICE_DISCOVERY_ARCHITECTURE.md` - 服务发现架构（v3.1，Dual-layer IDL 设计）⭐
- `COM_ARCHITECTURE.md` - Com 模块总体架构
- `SECURITY_ARCHITECTURE_SUMMARY.md` - 安全架构总结
- `TRANSPORT_MATRIX.md` - 传输层技术选型矩阵
- `YAML_CONFIGURATION_SUMMARY.md` - YAML 配置设计总结
- `ARCHITECTURE_SUMMARY.md` - 架构总结

### 📋 [planning/](planning/) - 规划文档
开发计划、路线图和迁移计划
- `COM_DEVELOPMENT_ROADMAP.md` - Com 模块开发路线图
- `IMPLEMENTATION_ROADMAP_DETAILED.md` - 详细实施路线图
- `NEW_ARCHITECTURE_IMPLEMENTATION_PLAN.md` - 新架构实施计划
- `IMPLEMENTATION_PLAN_UPDATED.md` - 实施计划（更新版）
- `EPOCH_MIGRATION_PLAN.md` - Epoch 迁移计划

### 📊 [reports/](reports/) - 报告文档
实施状态、合规性检查和阶段总结报告
- 实施状态报告（PHASE2/3/4）
- 绑定层合规性报告
- 集成完成报告（DBUS, ICEORYX, DDS）
- AUTOSAR 扫描报告

### ✅ [checklist/](checklist/) - 检查清单
集成和验证检查清单
- `INTEGRATION_CHECKLIST.md` - 集成检查清单

### 📖 [guides/](guides/) - 集成指南
各种绑定层和协议的集成指南
- DDS 集成指南
- DBUS 集成指南
- SOMEIP 集成指南
- ProtoBuf+Socket 集成指南
- 自定义 UDP 协议集成指南
- 桥接集成指南（DBUS-DDS, SOMEIP-DDS）
- AUTOSAR 快速参考
- Com 快速参考
- 绑定层选择指南
- 扩展指南

### 🗄️ [archive/](archive/) - 归档文档
已废弃的设计和临时文档
- `old_designs/` - 旧版本设计文档
- `old_docs/` - 过时的文档
- `summaries/` - 临时总结文档

---

## 🚀 快速导航

### 新用户入门
1. 阅读 [`architecture/SERVICE_DISCOVERY_ARCHITECTURE.md`](architecture/SERVICE_DISCOVERY_ARCHITECTURE.md) - 了解核心服务发现架构
2. 阅读 [`guides/COM_QUICK_REFERENCE.md`](guides/COM_QUICK_REFERENCE.md) - Com 模块快速参考
3. 查看 [`planning/COM_DEVELOPMENT_ROADMAP.md`](planning/COM_DEVELOPMENT_ROADMAP.md) - 了解开发计划

### 集成开发
1. 选择绑定层: [`guides/BINDING_SELECTION_GUIDE.md`](guides/BINDING_SELECTION_GUIDE.md)
2. 查看集成指南: [`guides/`](guides/) 目录下对应的集成文档
3. 参考检查清单: [`checklist/INTEGRATION_CHECKLIST.md`](checklist/INTEGRATION_CHECKLIST.md)

### AUTOSAR 合规性
1. [`guides/AUTOSAR_QUICK_REFERENCE.md`](guides/AUTOSAR_QUICK_REFERENCE.md) - AUTOSAR 快速参考
2. [`reports/AUTOSAR_R24-11_SCAN_REPORT.md`](reports/AUTOSAR_R24-11_SCAN_REPORT.md) - R24-11 扫描报告
3. [`reports/BINDING_ARCHITECTURE_COMPLIANCE_REPORT.md`](reports/BINDING_ARCHITECTURE_COMPLIANCE_REPORT.md) - 合规性报告

---

## 📝 文档状态

| 类别 | 文档数量 | 状态 |
|------|---------|------|
| 架构设计 | 6 | ✅ 活跃维护 |
| 规划文档 | 5 | ✅ 定期更新 |
| 报告文档 | ~15 | 📊 持续跟踪 |
| 集成指南 | ~12 | 📖 完善中 |
| 检查清单 | 1 | ✅ 可用 |
| 归档文档 | ~8 | 🗄️ 仅供参考 |

---

## 🔄 最近更新

- **2025-11-24**: 服务发现架构升级至 v3.1（双层 IDL 设计，Franca + DDS）
- **2025-11-24**: 文档结构重组（按类型分类）
- 查看各文档内的版本历史获取详细更新记录

---

## 📞 联系方式

如有文档问题或建议，请联系 Com 模块维护团队。

