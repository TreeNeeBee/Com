# LightAP Com Module - SOME/IP Integration

## 🎯 快速概览

LightAP Com模块现在支持**三种通信方式**：

| 通信方式 | 协议 | 范围 | 状态 |
|---------|------|------|------|
| **手写D-Bus** | IPC消息总线 | ECU内 | ✅ 生产就绪 |
| **CommonAPI-DBus** | IPC + 代码生成 | ECU内 | ✅ 生产就绪 |
| **SOME/IP** | 服务导向中间件 | ECU间 | ✅ 基础设施就绪 |

## 📚 文档导航

### 便利工具
- 🔍 **验证脚本**: `./verify_someip_integration.sh` - 检查集成状态（无需依赖）
- 📦 **安装助手**: `./install_someip_dependencies.sh` - 自动安装vsomeip和CommonAPI-SomeIP

### 入门指南
- 🚀 [下一步工作](tools/NEXT_STEPS.md) - CommonAPI入门
- 📊 [传输协议矩阵](TRANSPORT_MATRIX.md) - 如何选择传输方式
- 🏗️ [绑定架构](source/binding/README.md) - 整体设计

### SOME/IP专题
- 📘 [SOME/IP完整指南](tools/someip/README.md) - 安装、配置、使用
- 📝 [集成总结](SOMEIP_INTEGRATION_SUMMARY.md) - 已完成工作清单

### 代码示例
- 📂 [手写D-Bus示例](test/examples/dbus/) - 简单快速
- 📂 [CommonAPI-DBus示例](test/examples/commonapi/) - AUTOSAR标准
- 📂 [SOME/IP示例](test/examples/someip/) - 跨ECU通信

## 🔧 快速开始

### 选项1：手写D-Bus（推荐新手）
```cpp
#include <binding/dbus/DBusMethodBinding.hpp>

// 最简单的方式，立即上手
DBusMethodServer server(conn, "/calc", "com.example.Calc");
server.RegisterMethod<Request, Response>("Do", handler);
```

### 选项2：CommonAPI-DBus（推荐AUTOSAR项目）
```bash
# 1. 定义接口
vim tools/fidl/examples/MyService.fidl

# 2. 生成代码
cd tools/commonapi
./generate_new.sh ../fidl/examples/MyService.fidl dbus

# 3. 实现服务（使用适配器）
```

### 选项3：SOME/IP（推荐跨ECU通信）
```bash
# 1. 安装依赖（见tools/someip/README.md）
# 2. 定义接口 + 部署描述
vim tools/fidl/examples/MyService.fidl
vim tools/fidl/examples/MyService.fdepl

# 3. 生成代码
cd tools/commonapi
./generate_new.sh ../fidl/examples/MyService.fidl someip

# 4. 配置vsomeip
vim tools/someip/vsomeip_myservice.json

# 5. 实现服务（使用SomeIp适配器）
```

## 🆚 何时使用哪种方式？

```
需要ECU间通信？
├─ 是 → 使用 SOME/IP
│       ✅ Ethernet通信
│       ✅ 服务发现
│       ✅ 高带宽
│
└─ 否（ECU内）
   ├─ 需要AUTOSAR合规？
   │  ├─ 是 → 使用 CommonAPI-DBus
   │  │       ✅ 代码生成
   │  │       ✅ ARXML支持
   │  │       ✅ 标准化
   │  │
   │  └─ 否 → 使用手写D-Bus
   │          ✅ 简单直接
   │          ✅ 快速原型
   │          ✅ 完全控制
```

## 📦 新增文件（SOME/IP集成）

```
modules/Com/
├── source/binding/someip/
│   └── SomeIpConnectionManager.hpp       # vsomeip生命周期管理
├── source/binding/commonapi/
│   └── CommonAPISomeIpAdapter.hpp        # SOME/IP适配器
├── tools/
│   ├── commonapi/generate_new.sh         # 多协议代码生成
│   ├── fidl/examples/*.fdepl             # SOME/IP部署描述
│   └── someip/
│       ├── vsomeip_*.json                # vsomeip配置
│       └── README.md                     # 完整指南
├── test/examples/someip/
│   ├── calculator_server.cpp             # 服务端示例
│   └── calculator_client.cpp             # 客户端示例
├── TRANSPORT_MATRIX.md                   # 选型指南
└── SOMEIP_INTEGRATION_SUMMARY.md         # 集成总结
```

## ✅ 验证状态

- ✅ 所有现有代码编译通过
- ✅ 所有Com单元测试通过 (3/3)
- ✅ D-Bus binding正常工作
- ✅ CommonAPI基础设施就绪
- ✅ SOME/IP基础设施就绪（等待用户安装依赖）

## 🚀 后续工作

用户需要：
1. 安装vsomeip库
2. 安装CommonAPI-SomeIP Runtime
3. 下载CommonAPI-SomeIP Generator
4. 生成代码并测试

开发可选：
- [ ] 创建SOME/IP单元测试
- [ ] 添加CMake自动代码生成
- [ ] 性能基准测试
- [ ] 更多服务示例

## 📞 获取帮助

- **手写D-Bus**: 查看 `source/binding/dbus/`
- **CommonAPI**: 查看 `tools/NEXT_STEPS.md`
- **SOME/IP**: 查看 `tools/someip/README.md`
- **选型决策**: 查看 `TRANSPORT_MATRIX.md`

---

**项目状态**: SOME/IP集成完成，基础设施就绪 🎉
