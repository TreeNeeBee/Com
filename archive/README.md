# 归档文档说明

**创建日期**: 2025-11-19  
**最后更新**: 2025-11-24  
**目的**: 保存历史文档，避免信息丢失

---

## 📂 目录结构

- `old_designs/` - 旧版本架构设计（已被新设计替代）
- `old_docs/` - 过时的集成文档和实现笔记
- `summaries/` - 临时性总结文档

---

## 🗄️ 2025-11-24 归档内容

### 重构和清理计划（已完成）
- `ARCHITECTURE_V3_REFACTORING_SUMMARY.md` - V3 架构重构总结
- `CLEANUP_EXECUTION_REPORT.md` - 清理执行报告
- `CLEANUP_SUMMARY.md` - 清理总结
- `DEVELOPMENT_REFACTORING_PLAN.md` - 开发重构计划
- `DOCUMENTATION_CLEANUP_PLAN.md` - 文档清理计划
- `PHASE2_IMPLEMENTATION.md` - 第二阶段实施文档

### 旧版本设计（old_designs/）
- `LEGACY_DYNAMIC_DISCOVERY_DESIGN.md` - 旧版动态发现设计
- `SERVICE_DISCOVERY_V2_FAST_DDS_DESIGN.md` - V2 Fast DDS 设计（已被 V3.1 双层 IDL 设计取代）

---

## 📂 summaries/

早期的总结文档 (`.txt` 文件)，现已有更完整的 Markdown 版本。

| 归档文件 | 对应最新文档 | 位置 |
|----------|--------------|------|
| `CUSTOM_PROTOCOL_UDP_SUMMARY.txt` | `CUSTOM_PROTOCOL_UDP_INTEGRATION_GUIDE.md` | `doc/` (9758行) |
| `DDS_INTEGRATION_SUMMARY.txt` | `DDS_INTEGRATION_GUIDE.md` | `doc/` (675行) |
| `PROTOBUF_SOCKET_SUMMARY.txt` | `PROTOBUF_SOCKET_INTEGRATION_GUIDE.md` | `doc/` (1102行) |
| `SOMEIP_DDS_BRIDGE_SUMMARY.txt` | `SOMEIP_DDS_BRIDGE_INTEGRATION_GUIDE.md` | `doc/` |
| `UNIFIED_DDS_BRIDGE_SUMMARY.txt` | - | - |
| `CENTRAL_REGISTRY_SUMMARY.txt` | - | 历史设计文档 |
| `REFACTOR_SUMMARY.txt` | - | 历史重构记录 |
| `QUICK_REFERENCE.txt` | `COM_QUICK_REFERENCE.md` | `doc/` (496行) |

---

## 📂 old_docs/

已被更新或合并的旧版文档。

| 归档文件 | 对应最新文档 | 说明 |
|----------|--------------|------|
| `DBUS_EVENT_BINDING_SUMMARY.md` | `DBUS_BINDING_COMPLETE.md` | 已合并到完整版 (567行) |

---

## 📂 根目录归档 (架构演进历史)

从主架构设计文档中分离的已废弃实现细节。

| 归档文件 | 内容描述 | 归档原因 | 归档日期 |
|----------|----------|----------|----------|
| `LEGACY_COMMONAPI_IMPLEMENTATION.md` | CommonAPI适配器、Franca IDL工作流、SOME/IP序列化 | 当前架构不使用CommonAPI，改用插件化Binding | 2025-11-19 |
| `LEGACY_EXTENSION_PLANS.md` | Protobuf over Socket设计、旧自定义协议格式 | 已被Custom Protocol + UDS Binding替代 | 2025-11-19 |

**对应最新文档**: `doc/ARCHITECTURE_SUMMARY.md` (当前4-Binding插件化架构)

---

## 🔍 查阅最新文档

请参考主索引文档:
- **完整索引**: `/modules/Com/DOCUMENTATION_INDEX.md`
- **简化索引**: `/modules/Com/doc/INDEX.md`

---

## ⚠️ 注意事项

1. 归档文档**不再维护更新**
2. 仅供历史参考，可能包含过时信息
3. 如需最新内容，请查阅对应的 `.md` 文件
4. 这些文件可通过 Git 历史记录追溯

---

**维护者**: LightAP Com 模块开发团队

