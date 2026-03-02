# Com 模块文档索引

> **[English](README.md)**

> **最后更新**: 2026-03-02  
> **AUTOSAR 标准**: Adaptive Platform R25-11

LightAP Com（通信管理）模块的技术文档。

> **快速开始**: 请先阅读 [**guides/DEVELOPMENT_GUIDE.md**](guides/DEVELOPMENT_GUIDE.md) — 覆盖从 FIDL 定义到代码生成、构建、测试的完整流程。

---

## 快速导航

### 新手入门（推荐阅读顺序）

| 步骤 | 文档 | 说明 |
|------|------|------|
| 1 | [**guides/DEVELOPMENT_GUIDE.md**](guides/DEVELOPMENT_GUIDE.md) | **开发指南** — FIDL → Split Gen → Server/Client → CMake → 测试 |
| 2 | [architecture/ARCHITECTURE_SUMMARY.md](architecture/ARCHITECTURE_SUMMARY.md) | Com 模块架构总览 (v4.0) |
| 3 | [guides/BINDING_SELECTION_GUIDE.md](guides/BINDING_SELECTION_GUIDE.md) | Binding 选择决策树 |
| 4 | [architecture/GENERATOR.md](architecture/GENERATOR.md) | lap-sidl-gen 代码生成器架构 |

### 按场景查阅

| 场景 | 文档 |
|------|------|
| 开发新服务 | [guides/DEVELOPMENT_GUIDE.md](guides/DEVELOPMENT_GUIDE.md) |
| 选择传输协议 | [guides/BINDING_SELECTION_GUIDE.md](guides/BINDING_SELECTION_GUIDE.md) |
| DDS 集成 | [guides/DDS_INTEGRATION_GUIDE.md](guides/DDS_INTEGRATION_GUIDE.md) |
| 传输协议对比 | [architecture/TRANSPORT_MATRIX.md](architecture/TRANSPORT_MATRIX.md) |
| AUTOSAR 合规 | [guides/AUTOSAR_QUICK_REFERENCE.md](guides/AUTOSAR_QUICK_REFERENCE.md) |
| 扩展新 Binding | [architecture/BINDING_ARCHITECTURE.md](architecture/BINDING_ARCHITECTURE.md) |

---

## Binding 实现状态

| Binding | 状态 | 延迟 | 适用场景 |
|---------|------|------|---------|
| **CoreIPC** | ✅ 已实现 | < 1µs (64B) | 同 ECU 零拷贝共享内存 |
| **DDS** | ✅ 已实现 | < 10µs SHM / < 30µs UDP | 跨 ECU、QoS、动态发现 |
| SOME/IP | ⚠️ 待实现 | — | AUTOSAR CP 互通 |
| Socket | ⚠️ 待实现 | — | 轻量 IPC、遗留系统集成 |
| D-Bus | ⚠️ 待实现 | — | systemd 集成 |

---

## 目录结构

### [architecture/](architecture/) — 架构设计文档（7 篇）

| 文档 | 说明 |
|------|------|
| `ARCHITECTURE_SUMMARY.md` | Com 模块架构总览 (v4.0, CoreIPC + DDS) |
| `BINDING_ARCHITECTURE.md` | Binding 层架构设计（ITransportBinding NVI 接口） |
| `GENERATOR.md` | lap-sidl-gen 生成器架构 (v1.0) |
| `SERVICE_DISCOVERY_ARCHITECTURE.md` | 服务发现架构 (v3.1, Dual-layer IDL) |
| `SECURITY_ARCHITECTURE_SUMMARY.md` | 安全架构总结 |
| `TRANSPORT_MATRIX.md` | 传输协议状态矩阵 |
| `YAML_CONFIGURATION_SUMMARY.md` | YAML 配置设计 |

### [guides/](guides/) — 开发指南（6 篇）

| 文档 | 说明 | 优先级 |
|------|------|--------|
| **`DEVELOPMENT_GUIDE.md`** | **完整开发流程 (v2.0)** — Split Gen, App Framework, 3 个示例 | ⭐⭐⭐ |
| `BINDING_SELECTION_GUIDE.md` | Binding 选择决策树 (CoreIPC ✅ / DDS ✅) | ⭐⭐⭐ |
| `AUTOSAR_QUICK_REFERENCE.md` | AUTOSAR AP COM API 快速参考 (R25-11) | ⭐⭐⭐ |
| `DDS_INTEGRATION_GUIDE.md` | DDS (Fast-DDS 3.x) 集成指南 | ⭐⭐ |
| `AUTOSAR_REQUIREMENTS_TRACEABILITY.md` | 需求追溯矩阵 (R25-11) | ⭐⭐ |
| `AUTOSAR_R24-11_SERVICE_DISCOVERY_REFERENCE.md` | 服务发现标准参考 | ⭐⭐ |

### [reports/](reports/) — 实施报告（21 篇）

实施状态、合规性检查、阶段总结报告。

### [planning/](planning/) — 规划文档（7 篇）

开发路线图、实施计划、架构更新总结。

### [archive/](archive/) — 归档文档（12 篇）

已废弃的设计和过时文档，仅供历史参考。

---

## 最近更新

- **2026-03-02**: guides/ 清理 — 7 个过时规划文档归档，BINDING_SELECTION_GUIDE v2.0 重写，R23-11 → R25-11
- **2026-03-02**: DEVELOPMENT_GUIDE v2.0 — Split Gen 隔离架构, App Framework, HelloWorld3 DDS-only 示例
- **2026-03-01**: 三个 HelloWorld 示例全部迁移至 split gen_server/gen_client 架构
- **2025-11-24**: 服务发现架构升级至 v3.1（双层 IDL 设计，Franca + DDS）
