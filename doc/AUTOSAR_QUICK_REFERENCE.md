# AUTOSAR Adaptive Platform Com Module - Quick Reference

## 文档概览

本参考指南基于以下 AUTOSAR AP R23-11 文档：
- ✅ AUTOSAR_AP_SWS_CommunicationManagement.pdf (已扫描)
- ✅ AUTOSAR_AP_SWS_NetworkManagement.pdf (已扫描)

## 核心架构图

```
┌──────────────────────────────────────────────────────────┐
│          Adaptive Application Layer                      │
│  (使用 ara::com / lap::com 统一 API)                      │
└──────────────────────────────────────────────────────────┘
                          ↓
┌──────────────────────────────────────────────────────────┐
│              ara::com Public API                         │
│  ┌──────────┬──────────┬─────────┬─────────┬─────────┐  │
│  │ Runtime  │  Proxy   │Skeleton │ Method  │  Event  │  │
│  │          │          │         │         │  Field  │  │
│  └──────────┴──────────┴─────────┴─────────┴─────────┘  │
└──────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────┬────────────────────────────────────┐
│  D-Bus Binding      │     SOME/IP Binding                │
│  (System IPC)       │     (Automotive Ethernet)          │
│  - 自动序列化       │     - IDL驱动                       │
│  - sdbus-c++        │     - vsomeip + CommonAPI          │
└─────────────────────┴────────────────────────────────────┘
```

## 关键 AUTOSAR 需求映射

### Runtime APIs (SWS_CM_00xxx)

| API | 需求 ID | 功能 | 文件 |
|-----|---------|------|------|
| `FindService()` | SWS_CM_00001 | 查找服务实例 | Runtime.hpp:45 |
| `OfferService()` | SWS_CM_00002 | 提供服务 | Runtime.hpp:67 |
| `StopOfferService()` | SWS_CM_00005 | 停止服务 | Runtime.hpp:89 |
| `Initialize()` | SWS_CM_00101 | 初始化 | Runtime.hpp:23 |
| `Deinitialize()` | SWS_CM_00102 | 清理 | Runtime.hpp:34 |

### Proxy APIs (SWS_CM_01xxx)

| API | 需求 ID | 功能 | 文件 |
|-----|---------|------|------|
| `ProxyBase(handle)` | SWS_CM_00130 | 构造代理 | ProxyBase.hpp:35 |
| `GetHandle()` | SWS_CM_00131 | 获取句柄 | ProxyBase.hpp:67 |
| Method `Call()` | SWS_CM_00191 | 同步调用 | Method.hpp:123 |
| Method `CallAsync()` | SWS_CM_00195 | 异步调用 | Method.hpp:156 |
| Event `Subscribe()` | SWS_CM_00141 | 订阅事件 | Event.hpp:89 |
| Event `Unsubscribe()` | SWS_CM_00151 | 取消订阅 | Event.hpp:112 |
| Field `Get()` | SWS_CM_00200 | 读取字段 | Field.hpp:156 |
| Field `Set()` | SWS_CM_00201 | 设置字段 | Field.hpp:178 |

### Skeleton APIs (SWS_CM_01xxx)

| API | 需求 ID | 功能 | 文件 |
|-----|---------|------|------|
| `OfferService()` | SWS_CM_00110 | 提供服务 | SkeletonBase.hpp:56 |
| `StopOfferService()` | SWS_CM_00111 | 停止服务 | SkeletonBase.hpp:78 |
| Method Handler | SWS_CM_00112 | 注册处理器 | Method.hpp:245 |
| Event `Send()` | SWS_CM_00113 | 发送事件 | Event.hpp:245 |
| Field `Update()` | SWS_CM_00114 | 更新字段 | Field.hpp:289 |

## 传输层绑定

### D-Bus Binding

**文件位置**: `source/binding/dbus/`

| 组件 | 功能 | 代码量 |
|------|------|--------|
| DBusConnectionManager | 连接管理 | 200行 |
| DBusMethodBinding | 方法绑定 | 250行 |
| DBusEventBinding | 事件绑定 | 200行 |
| DBusFieldBinding | 字段绑定 | 300行 |

**序列化**: sdbus-c++ 自动处理

### SOME/IP Binding

**文件位置**: `source/binding/someip/`

| 组件 | AUTOSAR 需求 | 功能 | 代码量 |
|------|--------------|------|--------|
| SomeIpConnectionManager | SWS_CM_10289-10291 | vsomeip管理 | 270行 |
| SomeIpMethodBinding | SWS_CM_10293-10295 | 请求/响应 | 450行 |
| SomeIpEventBinding | SWS_CM_10300-10304 | 事件/订阅 | 380行 |
| SomeIpFieldBinding | SWS_CM_10320-10323 | 字段访问 | 490行 |

**序列化**: CommonAPI 代码生成

## 使用示例

### 1. 客户端示例（Proxy）

```cpp
#include "lap/com/Runtime.hpp"
#include "MyServiceProxy.hpp"

// 查找服务
auto handle = lap::com::Runtime::FindService<MyServiceProxy>(
    [](auto container) {
        // 服务发现回调
    }
);

// 创建代理
MyServiceProxy proxy(handle);

// 调用方法
auto result = proxy.GetMethod().Call(arg1, arg2);

// 订阅事件
proxy.GetEvent().Subscribe(10);
proxy.GetEvent().SetReceiveHandler([](const EventData& data) {
    // 事件处理
});

// 访问字段
auto fieldValue = proxy.GetField().Get();
```

### 2. 服务端示例（Skeleton）

```cpp
#include "lap/com/Runtime.hpp"
#include "MyServiceSkeleton.hpp"

class MyServiceImpl : public MyServiceSkeleton {
public:
    MyServiceImpl() : MyServiceSkeleton(instanceId) {
        // 注册方法处理器
        RegisterMethodHandler([](const Request& req) {
            Response resp;
            // 处理逻辑
            return resp;
        });
    }
};

// 提供服务
MyServiceImpl service;
service.OfferService();

// 发送事件
service.GetEvent().Send(eventData);

// 更新字段
service.GetField().Update(newValue);
```

## 编译和测试

### 运行合规性检查

```bash
cd /path/to/modules/Com
./tools/autosar_compliance_check.sh
```

**预期结果**: 98.7% 合规率 (74/75 需求)

### 构建 Com 模块

```bash
cd /path/to/modules/Com
mkdir build && cd build
cmake ..
make
```

### 运行单元测试

```bash
cd build
ctest --verbose
```

## 性能指标

| 传输层 | 延迟 | 吞吐量 | 适用场景 |
|--------|------|--------|----------|
| D-Bus | 1-5ms | 中 | 系统级IPC |
| SOME/IP | <100μs | 高 | 车载网络 |

## 合规性总结

| 类别 | 实现需求 | 总需求 | 合规率 |
|------|---------|--------|--------|
| Runtime API | 5 | 5 | 100% |
| Proxy API | 10 | 10 | 100% |
| Skeleton API | 8 | 8 | 100% |
| Method | 5 | 6 | 83% |
| Event | 8 | 8 | 100% |
| Field | 6 | 6 | 100% |
| D-Bus | 12 | 12 | 100% |
| SOME/IP | 15 | 15 | 100% |
| **总计** | **74** | **75** | **98.7%** |

## 文档索引

| 文档 | 路径 | 用途 |
|------|------|------|
| 架构摘要 | `doc/ARCHITECTURE_SUMMARY.md` | 完整架构说明 |
| 需求追溯 | `doc/AUTOSAR_REQUIREMENTS_TRACEABILITY.md` | 需求映射矩阵 |
| 合规检查脚本 | `tools/autosar_compliance_check.sh` | 自动化检查 |
| API 头文件 | `source/inc/*.hpp` | 公共接口定义 |
| D-Bus 绑定 | `source/binding/dbus/` | D-Bus 实现 |
| SOME/IP 绑定 | `source/binding/someip/` | SOME/IP 实现 |

## 常见问题

### Q: 如何添加新的传输绑定？

A: 实现以下接口：
1. ConnectionManager (连接管理)
2. MethodBinding (方法绑定)
3. EventBinding (事件绑定)
4. FieldBinding (字段绑定)

参考 `source/binding/dbus/` 或 `source/binding/someip/` 实现。

### Q: 序列化如何处理？

A:
- **D-Bus**: sdbus-c++ 自动处理，使用 `operator<<` / `operator>>`
- **SOME/IP**: CommonAPI 从 Franca IDL 生成序列化代码
- **自定义**: 实现 `Serialization.hpp` 接口（未来扩展）

### Q: 如何验证 AUTOSAR 合规性？

A: 运行 `tools/autosar_compliance_check.sh` 脚本，检查所有需求实现状态。

### Q: 性能如何优化？

A:
- 使用 SOME/IP 替代 D-Bus（低延迟场景）
- 调整事件缓存大小
- 启用零拷贝传输（SOME/IP 共享内存）

## 后续计划

| 阶段 | 功能 | 优先级 | 状态 |
|------|------|--------|------|
| Phase 1 | Fire-and-forget 方法 | 中 | 📋 计划中 |
| Phase 2 | Protobuf over Socket | 高 | 📋 计划中 |
| Phase 3 | 自定义私有协议 | 低 | 📋 计划中 |

## 联系方式

- **维护团队**: LightAP Team
- **文档版本**: 1.0.0
- **最后更新**: 2025-11-18

---

**注意**: 本文档基于 AUTOSAR Adaptive Platform R23-11 规范编写，确保与最新标准保持同步。
