# Com 模块文档索引

> **最后更新**: 2026-03-02  
> **AUTOSAR 标准**: AP R25-11

本目录包含 LightAP Com 模块的所有技术文档。

> **快速开始**: 请先阅读 [**开发指南 DEVELOPMENT_GUIDE.md**](guides/DEVELOPMENT_GUIDE.md) — 覆盖从 FIDL 定义到代码生成、构建、测试的完整流程。

---

## 🚀 快速导航

### 新手入门 (推荐阅读顺序)

| 步骤 | 文档 | 说明 |
|------|------|------|
| 1 | [**guides/DEVELOPMENT_GUIDE.md**](guides/DEVELOPMENT_GUIDE.md) | **开发指南** — FIDL → Split Gen → Server/Client → CMake → 测试 |
| 2 | [architecture/ARCHITECTURE_SUMMARY.md](architecture/ARCHITECTURE_SUMMARY.md) | Com 模块架构总览 (v4.0) |
| 3 | [guides/BINDING_SELECTION_GUIDE.md](guides/BINDING_SELECTION_GUIDE.md) | Binding 选择决策 |
| 4 | [architecture/GENERATOR.md](architecture/GENERATOR.md) | lap-sidl-gen 生成器详细架构 |

### 按场景查阅

| 场景 | 文档 |
|------|------|
| 开发新服务 | [guides/DEVELOPMENT_GUIDE.md](guides/DEVELOPMENT_GUIDE.md) |
| 选择传输协议 | [architecture/TRANSPORT_MATRIX.md](architecture/TRANSPORT_MATRIX.md) |
| DDS 集成 | [guides/DDS_INTEGRATION_GUIDE.md](guides/DDS_INTEGRATION_GUIDE.md) |
| SOME/IP 集成 | [guides/README_SOMEIP.md](guides/README_SOMEIP.md) |
| AUTOSAR 合规 | [guides/AUTOSAR_QUICK_REFERENCE.md](guides/AUTOSAR_QUICK_REFERENCE.md) |
| 扩展新 Binding | [guides/EXTENSION_GUIDE.md](guides/EXTENSION_GUIDE.md) |

---

## 📁 文档组织结构

### 📐 [architecture/](architecture/) — 架构设计文档

| 文档 | 说明 |
|------|------|
| `ARCHITECTURE_SUMMARY.md` | Com 模块架构总览 (v4.0, CoreIPC + DDS) |
| `BINDING_ARCHITECTURE.md` | Binding 层架构设计 (CoreIPC ✓, DDS ✓, SOME/IP ⚠️待实现) |
| `GENERATOR.md` | lap-sidl-gen 生成器架构 (v1.0, 1600+ 行) |
| `SERVICE_DISCOVERY_ARCHITECTURE.md` | 服务发现架构 (v3.1, Dual-layer IDL) |
| `SECURITY_ARCHITECTURE_SUMMARY.md` | 安全架构总结 |
| `TRANSPORT_MATRIX.md` | 传输协议状态矩阵 (CoreIPC/DDS ✓, SOME/IP+Socket+D-Bus ⚠️待实现) |
| `ARCHITECTURE_SUMMARY.md` | 架构总结 |
| `YAML_CONFIGURATION_SUMMARY.md` | YAML 配置设计 |

### 📖 [guides/](guides/) — 开发指南与集成文档

| 文档 | 说明 | 优先级 |
|------|------|--------|
| **`DEVELOPMENT_GUIDE.md`** | **完整开发流程 (v2.0)** — Split Gen, App Framework, 3 个示例 | ⭐⭐⭐ |
| `AUTOSAR_QUICK_REFERENCE.md` | AUTOSAR AP COM API 快速参考 | ⭐⭐⭐ |
| `AUTOSAR_R24-11_SERVICE_DISCOVERY_REFERENCE.md` | SD 标准参考 | ⭐⭐ |
| `AUTOSAR_REQUIREMENTS_TRACEABILITY.md` | 需求追溯矩阵 (98.7% 合规) | ⭐⭐ |
| `BINDING_SELECTION_GUIDE.md` | Binding 选择决策树 | ⭐⭐⭐ |
| `COM_QUICK_REFERENCE.md` | Com 模块 API 快速参考 | ⭐⭐ |
| `DDS_INTEGRATION_GUIDE.md` | DDS (Fast-DDS) 集成指南 | ⭐⭐ |
| `EXTENSION_GUIDE.md` | 新 Binding 扩展指南 | ⭐⭐ |
| `README_SOMEIP.md` | SOME/IP binding 指南 ⚠️ **待实现** | ⚠️ |
| `CUSTOM_PROTOCOL_UDP_INTEGRATION_GUIDE.md` | 自定义 UDP 协议集成 ⚠️ **待实现** | ⚠️ |
| `PROTOBUF_SOCKET_INTEGRATION_GUIDE.md` | Protobuf + Socket 集成 ⚠️ **待实现** | ⚠️ |
| `DBUS_DDS_BRIDGE_INTEGRATION_GUIDE.md` | D-Bus ↔ DDS 桥接 | ⭐ |
| `SOMEIP_DDS_BRIDGE_INTEGRATION_GUIDE.md` | SOME/IP ↔ DDS 桥接 | ⭐ |

### 📊 [reports/](reports/) — 实施报告

实施状态、合规性检查、阶段总结报告。

### 📋 [planning/](planning/) — 规划文档

开发路线图、实施计划、架构更新总结。

### ✅ [checklist/](checklist/) — 检查清单

- `INTEGRATION_CHECKLIST.md` — 集成验证检查清单

### 🗄️ [archive/](archive/) — 归档文档

已废弃的设计和过时文档，仅供历史参考。

---

## 📝 文档状态

| 类别 | 文档数量 | 状态 |
|------|---------|------|
| 架构设计 | 8 | ✅ 活跃维护 |
| 开发指南 | 13 | ✅ **DEVELOPMENT_GUIDE v2.0 更新** |
| 报告文档 | ~21 | 📊 持续跟踪 |
| 规划文档 | 6 | 📋 定期更新 |
| 检查清单 | 1 | ✅ 可用 |
| 归档文档 | ~8 | 🗄️ 仅供参考 |

---

## 🔄 最近更新

- **2026-03-02**: DEVELOPMENT_GUIDE v2.0 — Split Gen 隔离架构, App Framework, HelloWorld3 DDS-only 示例
- **2026-03-02**: 文档索引重组 — 快速导航、按场景查阅
- **2026-03-01**: 三个 HelloWorld 示例全部迁移至 split gen_server/gen_client 架构
- **2025-11-24**: 服务发现架构升级至 v3.1（双层 IDL 设计，Franca + DDS）

---

如有文档问题或建议，请联系 Com 模块维护团队。

