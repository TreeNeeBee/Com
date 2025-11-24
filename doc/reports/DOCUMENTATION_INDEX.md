# Com 模块文档索引 (完整分类版)

**最后更新**: 2025-11-18  
**文档总数**: 39 个  
**总行数**: ~15,000+ 行

---

## 📖 使用指南

### 新手入门路径
1. 阅读 [AUTOSAR_QUICK_REFERENCE.md](doc/AUTOSAR_QUICK_REFERENCE.md) - 了解核心概念
2. 查看 [COM_ARCHITECTURE.md](doc/COM_ARCHITECTURE.md) - 理解架构分层
3. 参考 [COM_QUICK_REFERENCE.md](COM_QUICK_REFERENCE.md) - 快速开发指导

### 集成开发路径
根据传输协议选择对应的集成指南:
- **本地IPC (低延迟)**: [PROTOBUF_SOCKET_INTEGRATION_GUIDE.md](doc/PROTOBUF_SOCKET_INTEGRATION_GUIDE.md)
- **本地IPC (标准)**: [DBUS_BINDING_COMPLETE.md](DBUS_BINDING_COMPLETE.md)
- **跨ECU (标准)**: [README_SOMEIP.md](README_SOMEIP.md)
- **跨ECU (高级)**: [DDS_INTEGRATION_GUIDE.md](doc/DDS_INTEGRATION_GUIDE.md)
- **遗留设备/自定义**: [CUSTOM_PROTOCOL_UDP_INTEGRATION_GUIDE.md](doc/CUSTOM_PROTOCOL_UDP_INTEGRATION_GUIDE.md)

---

## 📂 1. 架构与设计 (Architecture & Design)

核心架构文档和分层设计说明。

| 文档 | 位置 | 行数 | 说明 | 优先级 |
|------|------|------|------|--------|
| [COM_ARCHITECTURE.md](doc/COM_ARCHITECTURE.md) | doc/ | 953 | **核心架构文档** - 分层设计、组件交互、依赖关系 | ⭐⭐⭐ |
| [ARCHITECTURE_SUMMARY.md](doc/ARCHITECTURE_SUMMARY.md) | doc/ | ~900 | AUTOSAR 标准架构总结 | ⭐⭐⭐ |
| [SERVICE_DISCOVERY_ARCHITECTURE.md](doc/SERVICE_DISCOVERY_ARCHITECTURE.md) | doc/ | - | 服务发现机制架构设计 | ⭐⭐ |
| [TRANSPORT_MATRIX.md](doc/TRANSPORT_MATRIX.md) | doc/ | 321 | **传输协议选型矩阵** (D-Bus/SOME/IP/Signal) | ⭐⭐⭐ |

**适用场景**: 架构设计、技术选型、系统理解

---

## 📂 2. AUTOSAR 合规性 (AUTOSAR Compliance)

AUTOSAR 标准符合性文档和需求追溯。

| 文档 | 位置 | 行数 | 说明 | 状态 |
|------|------|------|------|------|
| [AUTOSAR_REQUIREMENTS_TRACEABILITY.md](doc/AUTOSAR_REQUIREMENTS_TRACEABILITY.md) | doc/ | - | **需求追溯矩阵** (74/75 需求, 98.7% 合规) | ✅ 完成 |
| [AUTOSAR_QUICK_REFERENCE.md](doc/AUTOSAR_QUICK_REFERENCE.md) | doc/ | 262 | **快速参考** - API 映射表、使用示例 | ✅ 完成 |
| [AUTOSAR_R24-11_SCAN_REPORT.md](doc/AUTOSAR_R24-11_SCAN_REPORT.md) | doc/ | - | R24-11 标准扫描报告 (65,554 行文本提取) | ✅ 完成 |
| [AUTOSAR_R24-11_SERVICE_DISCOVERY_REFERENCE.md](doc/AUTOSAR_R24-11_SERVICE_DISCOVERY_REFERENCE.md) | doc/ | - | 服务发现标准参考 | ✅ 完成 |

**适用场景**: AUTOSAR 合规性验证、需求审查、标准学习

---

## 📂 3. 集成指南 (Integration Guides)

各种传输绑定的完整集成指南 - **最实用的开发文档**。

| 文档 | 位置 | 行数 | 协议 | 场景 | 状态 |
|------|------|------|------|------|------|
| [CUSTOM_PROTOCOL_UDP_INTEGRATION_GUIDE.md](doc/CUSTOM_PROTOCOL_UDP_INTEGRATION_GUIDE.md) | doc/ | **9758** | UDP + 自定义协议 | 遗留设备对接、快速原型 | 📋 规划 |
| [PROTOBUF_SOCKET_INTEGRATION_GUIDE.md](doc/PROTOBUF_SOCKET_INTEGRATION_GUIDE.md) | doc/ | 1102 | Protobuf + UDS | 极致性能IPC (<5μs) | 📋 规划 |
| [DDS_INTEGRATION_GUIDE.md](doc/DDS_INTEGRATION_GUIDE.md) | doc/ | 675 | DDS (Fast-DDS) | 分布式系统、V2X | 📋 规划 |
| [DBUS_DDS_BRIDGE_INTEGRATION_GUIDE.md](doc/DBUS_DDS_BRIDGE_INTEGRATION_GUIDE.md) | doc/ | - | D-Bus ↔ DDS | 混合传输桥接 | 📋 规划 |
| [SOMEIP_DDS_BRIDGE_INTEGRATION_GUIDE.md](doc/SOMEIP_DDS_BRIDGE_INTEGRATION_GUIDE.md) | doc/ | - | SOME/IP ↔ DDS | 跨域桥接 | 📋 规划 |

**特别说明**:
- **Custom Protocol UDP**: 包含详细可观测性章节 (Prometheus + Grafana + OpenTelemetry)
- **Protobuf Socket**: 包含零拷贝技术、性能基准测试
- **DDS**: 包含 DDS Security 集成

---

## 📂 4. Binding 实现文档 (Binding Implementation)

已完成的传输绑定实现总结。

| 文档 | 位置 | 行数 | 说明 | 状态 |
|------|------|------|------|------|
| [DBUS_BINDING_COMPLETE.md](doc/DBUS_BINDING_COMPLETE.md) | doc/ | 567 | **D-Bus Binding Phase 1** 完成报告 (Event/Method/Field) | ✅ 完成 |
| [SOMEIP_INTEGRATION_SUMMARY.md](doc/SOMEIP_INTEGRATION_SUMMARY.md) | doc/ | - | SOME/IP 集成总结 (vsomeip + CommonAPI) | ✅ 完成 |
| [README_SOMEIP.md](doc/README_SOMEIP.md) | doc/ | - | SOME/IP 说明文档 | ✅ 完成 |
| [SOCKET_INTEGRATION.md](doc/SOCKET_INTEGRATION.md) | doc/ | - | Socket 传输集成文档 | ✅ 完成 |

**适用场景**: 了解已实现功能、参考实现代码

---

## 📂 5. 开发路线图与规划 (Roadmap & Planning)

项目开发计划、迁移方案、技术演进路线。

| 文档 | 位置 | 行数 | 说明 | 状态 |
|------|------|------|------|------|
| [COM_DEVELOPMENT_ROADMAP.md](doc/COM_DEVELOPMENT_ROADMAP.md) | doc/ | 669 | **主开发路线图** - 各阶段任务、已完成清单 | 🔄 持续更新 |
| [EPOCH_MIGRATION_PLAN.md](doc/EPOCH_MIGRATION_PLAN.md) | doc/ | - | Epoch 时间同步迁移计划 (PTP/gPTP 集成) | 📋 规划 |
| [EPOCH_SECTION_4.1.10_COMPLETE.md](doc/EPOCH_SECTION_4.1.10_COMPLETE.md) | doc/ | - | Epoch 4.1.10 节完整实现文档 | ✅ 完成 |

**适用场景**: 项目规划、进度跟踪、里程碑检查

---

## 📂 6. 快速参考 (Quick References)

快速查找常用 API、开发步骤、集成清单。

| 文档 | 位置 | 行数 | 说明 | 优先级 |
|------|------|------|------|--------|
| [COM_QUICK_REFERENCE.md](doc/COM_QUICK_REFERENCE.md) | doc/ | 496 | **模块开发快速参考** (当前状态、下一步任务) | ⭐⭐⭐ |
| [INTEGRATION_CHECKLIST.md](doc/INTEGRATION_CHECKLIST.md) | doc/ | - | 集成检查清单 (验证步骤) | ⭐⭐ |

**适用场景**: 日常开发、快速查阅、集成验证

---

## 📂 7. 实现笔记与总结 (Implementation Notes)

实现细节、技术决策、扩展开发指南。

| 文档 | 位置 | 行数 | 说明 | 类型 |
|------|------|------|------|------|
| [IMPLEMENTATION_NOTES.md](doc/IMPLEMENTATION_NOTES.md) | doc/ | 524 | **实现技术笔记** (代码结构、关键实现) | 技术笔记 |
| [EXTENSION_GUIDE.md](doc/EXTENSION_GUIDE.md) | doc/ | - | 扩展开发指南 (如何添加新 Binding) | 开发指南 |

**适用场景**: 深入理解实现、扩展开发、代码重构参考

---

## 📂 8. 归档文档 (Archived Documents)

这些文件已归档到 `archive/` 目录，现有更完整的 Markdown 版本。

| 归档文件 | 对应 Markdown | 归档位置 | 说明 |
|----------|---------------|----------|------|
| `CUSTOM_PROTOCOL_UDP_SUMMARY.txt` | [CUSTOM_PROTOCOL_UDP_INTEGRATION_GUIDE.md](doc/CUSTOM_PROTOCOL_UDP_INTEGRATION_GUIDE.md) (9758行) | archive/summaries/ | ✅ 已归档 |
| `DDS_INTEGRATION_SUMMARY.txt` | [DDS_INTEGRATION_GUIDE.md](doc/DDS_INTEGRATION_GUIDE.md) (675行) | archive/summaries/ | ✅ 已归档 |
| `PROTOBUF_SOCKET_SUMMARY.txt` | [PROTOBUF_SOCKET_INTEGRATION_GUIDE.md](doc/PROTOBUF_SOCKET_INTEGRATION_GUIDE.md) (1102行) | archive/summaries/ | ✅ 已归档 |
| `SOMEIP_DDS_BRIDGE_SUMMARY.txt` | [SOMEIP_DDS_BRIDGE_INTEGRATION_GUIDE.md](doc/SOMEIP_DDS_BRIDGE_INTEGRATION_GUIDE.md) | archive/summaries/ | ✅ 已归档 |
| `UNIFIED_DDS_BRIDGE_SUMMARY.txt` | - | archive/summaries/ | ✅ 已归档 |
| `DBUS_EVENT_BINDING_SUMMARY.md` | [DBUS_BINDING_COMPLETE.md](doc/DBUS_BINDING_COMPLETE.md) (更完整) | archive/old_docs/ | ✅ 已归档 |
| `CENTRAL_REGISTRY_SUMMARY.txt` | - (历史设计) | archive/summaries/ | ✅ 已归档 |
| `REFACTOR_SUMMARY.txt` | - (历史重构) | archive/summaries/ | ✅ 已归档 |
| `QUICK_REFERENCE.txt` | [COM_QUICK_REFERENCE.md](doc/COM_QUICK_REFERENCE.md) (496行) | archive/summaries/ | ✅ 已归档 |

**归档说明**: 参考 [archive/README.md](archive/README.md)

---

## 📂 9. 工具与脚本文档 (Tools Documentation)

开发工具、自动化脚本的使用说明。

| 文档 | 位置 | 说明 | 类型 |
|------|------|------|------|
| [tools/README.md](tools/README.md) | tools/ | 工具总览 (合规检查、绑定测试) | 工具说明 |
| [tools/NEXT_STEPS.md](tools/NEXT_STEPS.md) | tools/ | 工具链下一步开发计划 | 规划文档 |
| [tools/commonapi/README.md](tools/commonapi/README.md) | tools/commonapi/ | CommonAPI 代码生成工具 | 工具说明 |
| [tools/protobuf/README.md](tools/protobuf/README.md) | tools/protobuf/ | Protobuf 编译工具 | 工具说明 |
| [tools/someip/README.md](tools/someip/README.md) | tools/someip/ | SOME/IP 工具链 | 工具说明 |

**适用场景**: 自动化构建、代码生成、合规性检查

---

## 📂 10. 示例代码文档 (Examples Documentation)

示例代码的使用说明和参考文档。

| 文档 | 位置 | 说明 | 类型 |
|------|------|------|------|
| [source/binding/README.md](source/binding/README.md) | source/binding/ | Binding 层代码结构说明 | 代码说明 |
| [test/examples/commonapi/README.md](test/examples/commonapi/README.md) | test/examples/commonapi/ | CommonAPI 使用示例 | 示例说明 |

**适用场景**: 学习示例代码、参考实现

---

## 📂 11. 顶层索引 (Master Index)

| 文档 | 位置 | 行数 | 说明 | 状态 |
|------|------|------|------|------|
| [INDEX.md](doc/INDEX.md) | doc/ | 168 | **主索引文档** (基础版) | ✅ 已存在 |
| **DOCUMENTATION_INDEX.md** (本文档) | 根目录 | - | **完整分类索引** (增强版) | ✅ 新建 |

---

## 🔍 快速查找表

### 按使用场景查找

| 场景 | 推荐文档 |
|------|----------|
| **新手入门** | [AUTOSAR_QUICK_REFERENCE.md](doc/AUTOSAR_QUICK_REFERENCE.md) → [COM_QUICK_REFERENCE.md](doc/COM_QUICK_REFERENCE.md) |
| **架构理解** | [COM_ARCHITECTURE.md](doc/COM_ARCHITECTURE.md) → [ARCHITECTURE_SUMMARY.md](doc/ARCHITECTURE_SUMMARY.md) |
| **D-Bus 开发** | [DBUS_BINDING_COMPLETE.md](doc/DBUS_BINDING_COMPLETE.md) |
| **SOME/IP 开发** | [README_SOMEIP.md](doc/README_SOMEIP.md) → [SOMEIP_INTEGRATION_SUMMARY.md](doc/SOMEIP_INTEGRATION_SUMMARY.md) |
| **DDS 开发** | [DDS_INTEGRATION_GUIDE.md](doc/DDS_INTEGRATION_GUIDE.md) |
| **自定义协议** | [CUSTOM_PROTOCOL_UDP_INTEGRATION_GUIDE.md](doc/CUSTOM_PROTOCOL_UDP_INTEGRATION_GUIDE.md) ⭐ **9758行完整指南** |
| **极致性能 IPC** | [PROTOBUF_SOCKET_INTEGRATION_GUIDE.md](doc/PROTOBUF_SOCKET_INTEGRATION_GUIDE.md) |
| **传输协议选型** | [TRANSPORT_MATRIX.md](doc/TRANSPORT_MATRIX.md) |
| **AUTOSAR 合规** | [AUTOSAR_REQUIREMENTS_TRACEABILITY.md](doc/AUTOSAR_REQUIREMENTS_TRACEABILITY.md) |
| **可观测性设计** | [CUSTOM_PROTOCOL_UDP_INTEGRATION_GUIDE.md](doc/CUSTOM_PROTOCOL_UDP_INTEGRATION_GUIDE.md) 第5章 (Prometheus+Grafana) |
| **项目规划** | [COM_DEVELOPMENT_ROADMAP.md](doc/COM_DEVELOPMENT_ROADMAP.md) |
| **代码扩展** | [EXTENSION_GUIDE.md](doc/EXTENSION_GUIDE.md) |

### 按文档大小查找 (Top 5)

1. **CUSTOM_PROTOCOL_UDP_INTEGRATION_GUIDE.md**: 9758 行 (最详细的集成指南)
2. **PROTOBUF_SOCKET_INTEGRATION_GUIDE.md**: 1102 行 (性能优化重点)
3. **COM_ARCHITECTURE.md**: 953 行 (架构核心)
4. **ARCHITECTURE_SUMMARY.md**: ~900 行 (AUTOSAR 标准架构)
5. **DDS_INTEGRATION_GUIDE.md**: 675 行 (DDS 完整集成)

### 按开发阶段查找

| 阶段 | 文档 |
|------|------|
| **Phase 1 (已完成)** | [DBUS_BINDING_COMPLETE.md](doc/DBUS_BINDING_COMPLETE.md) - D-Bus 基础绑定 |
| **Phase 2 (规划中)** | [COM_DEVELOPMENT_ROADMAP.md](doc/COM_DEVELOPMENT_ROADMAP.md) - Fire-and-forget、触发器 |
| **Phase 3 (未来)** | [EPOCH_MIGRATION_PLAN.md](doc/EPOCH_MIGRATION_PLAN.md) - PTP 时间同步 |

---

## 🛠️ 建议改进措施

### 1. 文档位置标准化

**建议将根目录的技术文档移至 `doc/` 子目录**:

```bash
cd /home/ddk/1_workspace/2_middleware/LightAP/modules/Com

# 移动开发文档
mv COM_DEVELOPMENT_ROADMAP.md doc/
mv COM_QUICK_REFERENCE.md doc/
mv DBUS_BINDING_COMPLETE.md doc/
mv SOMEIP_INTEGRATION_SUMMARY.md doc/
mv README_SOMEIP.md doc/
mv SOCKET_INTEGRATION.md doc/
mv IMPLEMENTATION_NOTES.md doc/
mv INTEGRATION_CHECKLIST.md doc/
mv TRANSPORT_MATRIX.md doc/
mv DBUS_EVENT_BINDING_SUMMARY.md doc/

# 根目录只保留:
# - CMakeLists.txt (构建)
# - DOCUMENTATION_INDEX.md (索引)
# - install_someip_dependencies.sh (脚本)
# - test_*.sh (测试脚本)
# - verify_*.sh (验证脚本)
```

### 2. 清理重复文件

**归档已有完整 Markdown 版本的 .txt 文件**:

```bash
mkdir -p archive/summaries
mv *_SUMMARY.txt archive/summaries/
mv REFACTOR_SUMMARY.txt archive/summaries/
mv CENTRAL_REGISTRY_SUMMARY.txt archive/summaries/
mv QUICK_REFERENCE.txt archive/summaries/
```

### 3. 更新主索引 `doc/INDEX.md`

建议替换 `doc/INDEX.md` 为简化版本，并链接到本文档 (DOCUMENTATION_INDEX.md) 作为完整索引。

### 4. 文档编号规范

建议在每个文档开头添加统一的元数据:

```markdown
---
文档编号: COM-DOC-XXX
版本: 1.0.0
最后更新: 2025-11-18
状态: ✅ 完成 / 📋 规划 / 🔄 更新中
分类: 架构设计 / 集成指南 / API参考 / ...
---
```

---

## 📊 统计信息

| 类别 | 文档数量 | 总行数 (估算) |
|------|----------|---------------|
| 架构与设计 | 4 | ~2,200 |
| AUTOSAR 合规性 | 4 | ~500 |
| 集成指南 | 5 | **~12,500** |
| Binding 实现 | 4 | ~1,000 |
| 开发路线图 | 3 | ~700 |
| 快速参考 | 2 | ~500 |
| 实现笔记 | 2 | ~600 |
| 归档文档 | 9 | - |
| 工具文档 | 5 | ~300 |
| 示例文档 | 2 | ~100 |
| **总计** | **40** | **~18,400** |

**重点文档**: `CUSTOM_PROTOCOL_UDP_INTEGRATION_GUIDE.md` 占总行数的 **53%** (9758/18400)

---

## 📝 维护建议

1. **定期更新**: 每次功能完成后更新对应的集成指南和路线图
2. **版本管理**: 使用 Git 标签标记重要文档版本
3. **交叉引用**: 在相关文档间添加更多链接
4. **自动化检查**: 使用脚本检测文档链接失效、格式错误
5. **评审流程**: 新文档或重大更新需要 Code Review

---

**文档维护者**: LightAP Com 模块开发团队  
**反馈邮件**: [待补充]  
**最后审核**: 2025-11-18

