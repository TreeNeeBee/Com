# AUTOSAR Adaptive Platform Communication Management 开发路线图

## 项目概述
基于 AUTOSAR R22-11 SWS_CommunicationManagement 规范，完整实现 ara::com (现为 lap::com) 模块。

## 当前状态 (2025-10-30)

### ✅ 已完成
1. **基础架构搭建**
   - [x] 模块目录结构创建
   - [x] CMake 构建系统配置
   - [x] 符号链接优化 (参考 Persistency)
   - [x] 命名空间迁移 (ara → lap)
   - [x] 头文件保护宏规范化 (LAP_COM_XXX)

2. **核心类型定义 (Section 8)**
   - [x] ComTypes.hpp - 错误码、类型别名、枚举
   - [x] ServiceHandleType.hpp - 服务实例句柄
   - [x] ComErrorDomain - 错误域实现
   - [x] 17种 ComErrc 错误代码
   - [x] ServiceVersionType 结构体
   - [x] FindServiceHandle 类型定义

3. **Runtime API (Section 8.2)**
   - [x] Runtime.hpp - 生命周期和服务发现接口
   - [x] Runtime::Initialize() / Deinitialize()
   - [x] Runtime 基础实现 (Runtime.cpp)
   - [x] ServiceDiscovery 集成框架

4. **Proxy/Skeleton 基础类 (Section 9)**
   - [x] ProxyBase.hpp - 客户端通信基类
   - [x] SkeletonBase.hpp - 服务端通信基类
   - [x] ServiceProxy 模板类
   - [x] ServiceSkeleton 模板类

5. **事件通信 (Section 9.3)**
   - [x] Event.hpp - 发布/订阅事件
   - [x] ProxyEvent - 客户端事件接收
   - [x] SkeletonEvent - 服务端事件发送
   - [x] SamplePtr 智能指针
   - [x] 事件缓存管理接口

6. **方法调用 (Section 9.4)**
   - [x] Method.hpp - RPC 方法调用
   - [x] ProxyMethod - 同步/异步调用
   - [x] SkeletonMethod - 方法处理器
   - [x] FireAndForget 变体

7. **字段通信 (Section 9.5)**
   - [x] Field.hpp - 远程数据访问
   - [x] ProxyField - Getter/Setter/Notifier
   - [x] SkeletonField - 值管理和更新通知

8. **端到端保护 (Section 10)**
   - [x] E2EProtection.hpp - Profile 1 实现
   - [x] CRC-8 SAE J1850 校验
   - [x] 计数器和数据ID机制

9. **序列化支持**
   - [x] Serialization.hpp - 二进制序列化器
   - [x] 基本类型编解码
   - [x] 容器类型支持

10. **主头文件**
    - [x] ara_com.hpp - 统一包含文件
    - [x] 版本信息结构体

### 🔄 进行中
- 无

### ⏳ 待开发
- [ ] SOME/IP 绑定 (Phase 2)
- [ ] 完善 Runtime 实现细节
- [ ] ServiceDiscovery 后端绑定
- [ ] E2E 保护集成

---

## Phase 1: 传输层绑定 ✅ **已完成 2024-10-30**

### 1.1 D-Bus 绑定实现 (SWS_CM Section 11)
**优先级**: 🔴 最高
**预计时间**: 3周
**状态**: ✅ **完成** (所有绑定已实现并测试)

#### 任务清单:
- [x] **DBusConnectionManager** - **已完成** ✅
  - [x] 单例模式连接管理
  - [x] Session/System bus 支持
  - [x] 服务名注册/释放
  - [x] 线程安全连接池
  - **文件**: `DBusConnectionManager.hpp` (251 lines)

- [x] **事件传输** - **已完成** ✅
  - [x] D-Bus Signal 发送机制
  - [x] Signal 订阅和过滤
  - [x] POD 数据序列化 (memcpy)
  - [x] 复杂类型序列化 (std::string, std::vector<T>)
  - [x] 回调函数机制
  - [x] 示例程序 (publisher/subscriber)
  - **文件**: `DBusEventBinding_v2.hpp` (268 lines)
  - **测试**: `simple_dbus_publisher`, `simple_dbus_subscriber`
  - **状态**: 完全工作，已验证

- [x] **方法调用传输 (RPC)** - **已完成** ✅
  - [x] D-Bus Method 调用实现
  - [x] 同步调用 (method_call + reply)
  - [x] 异步调用 (带 std::future)
  - [x] 错误返回映射
  - [x] 超时处理
  - [x] void 返回类型支持
  - **文件**: `DBusMethodBinding.hpp` (251 lines)
  - **测试**: `dbus_method_server`, `dbus_method_client`
  - **状态**: 完全工作，已验证（包括异步调用）

- [x] **字段传输 (Properties)** - **已完成** ✅
  - [x] Getter (D-Bus Property Read)
  - [x] Setter (D-Bus Property Write)
  - [x] Notifier (D-Bus PropertiesChanged Signal)
  - [x] 属性缓存策略
  - [x] 订阅属性变化通知
  - **文件**: `DBusFieldBinding.hpp` (348 lines)
  - **测试**: `dbus_field_server`, `dbus_field_client`
  - **状态**: 完全工作，已验证

- [ ] **服务发现**
  - [ ] 集成 sdbus-c++ 的名称监听
  - [ ] NameOwnerChanged 信号处理
  - [ ] 服务可用性状态机
  - [ ] 动态服务注册/注销

#### 文件列表:
```
source/binding/dbus/
├── DBusBinding.hpp          # D-Bus 绑定主接口
├── DBusBinding.cpp
├── DBusProxyAdapter.hpp     # Proxy 适配器
├── DBusProxyAdapter.cpp
├── DBusSkeletonAdapter.hpp  # Skeleton 适配器
├── DBusSkeletonAdapter.cpp
├── DBusSerializer.hpp       # D-Bus 类型序列化
├── DBusSerializer.cpp
├── DBusEventBinding.hpp     # 事件绑定
├── DBusEventBinding.cpp
├── DBusMethodBinding.hpp    # 方法绑定
├── DBusMethodBinding.cpp
└── DBusFieldBinding.hpp     # 字段绑定
    DBusFieldBinding.cpp
```

#### 验收标准:
- [ ] 能够通过 D-Bus 发送/接收事件
- [ ] 支持同步和异步方法调用
- [ ] 字段 Get/Set/Notify 正常工作
- [ ] 服务发现能够检测到 D-Bus 服务
- [ ] 通过 d-feet/busctl 验证 D-Bus 消息

---

### 1.2 SOME/IP 绑定实现 (可选，后期)
**优先级**: 🟡 中等
**预计时间**: 4周

#### 任务清单:
- [ ] SOME/IP 协议栈集成
- [ ] 服务发现协议 (SD)
- [ ] 事件组管理
- [ ] TCP/UDP 传输层
- [ ] 序列化/反序列化

---

## Phase 2: 服务发现完善 (2-3周)

### 2.1 ServiceDiscovery 完整实现
**优先级**: 🔴 最高
**预计时间**: 2周

#### 任务清单:
- [ ] **FindService 实现**
  - [ ] 同步查找接口
  - [ ] 异步查找 (FindServiceHandler)
  - [ ] InstanceIdentifier 过滤
  - [ ] StartFindService 持续监听
  - [ ] StopFindService 取消监听

- [ ] **OfferService 实现**
  - [ ] 服务注册到传输层
  - [ ] InstanceSpecifier 解析
  - [ ] StopOfferService 服务注销
  - [ ] 多实例支持

- [ ] **服务可用性通知**
  - [ ] ServiceAvailabilityHandler
  - [ ] 状态变化事件
  - [ ] kAvailable/kNotAvailable 转换

- [ ] **服务注册表**
  - [ ] 本地服务缓存
  - [ ] ServiceHandle 生成和管理
  - [ ] 服务版本兼容性检查

#### 文件更新:
```
source/inc/Runtime.hpp           # 完善接口
source/comapi/src/Runtime.cpp    # 完整实现
source/inc/ServiceDiscovery.hpp  # 发现接口
source/src/ServiceDiscovery.cpp  # 发现实现
```

#### 验收标准:
- [ ] FindService 能找到本地和远程服务
- [ ] OfferService 成功注册服务
- [ ] 服务上下线触发可用性回调
- [ ] 多实例服务正确管理

---

### 2.2 InstanceIdentifier 配置解析
**优先级**: 🟠 高
**预计时间**: 1周

#### 任务清单:
- [ ] 集成 Core 模块的 InstanceSpecifier
- [ ] JSON 配置文件解析
- [ ] ServiceInterface 到 InstanceId 映射
- [ ] 配置文件示例和文档

---

## Phase 3: 通信模式实现 (3-4周)

### 3.1 Event 通信完整实现
**优先级**: 🔴 最高
**预计时间**: 2周

#### 任务清单:
- [ ] **SkeletonEvent 实现**
  - [ ] Allocate() 内存管理
  - [ ] Send() 发送到传输层
  - [ ] GetSubscriberCount() 实现
  - [ ] 多订阅者广播

- [ ] **ProxyEvent 实现**
  - [ ] Subscribe()/Unsubscribe()
  - [ ] SetReceiveHandler() 回调注册
  - [ ] GetNewSamples() 查询
  - [ ] GetNextSample() 获取数据
  - [ ] 循环缓冲区管理

- [ ] **事件缓存策略**
  - [ ] maxSampleCount 限制
  - [ ] 覆盖策略 (FIFO)
  - [ ] 零拷贝优化

#### 验收标准:
- [ ] 能够发送和接收事件
- [ ] 缓存机制正常工作
- [ ] 支持多订阅者
- [ ] 性能测试通过 (>1000 msg/s)

---

### 3.2 Method 通信完整实现
**优先级**: 🔴 最高
**预计时间**: 1.5周

#### 任务清单:
- [ ] **ProxyMethod 实现**
  - [ ] 同步调用 (阻塞等待)
  - [ ] 异步调用 (Future/Promise)
  - [ ] 超时处理
  - [ ] 错误返回

- [ ] **SkeletonMethod 实现**
  - [ ] RegisterHandler() 注册处理函数
  - [ ] 参数解包
  - [ ] 返回值封装
  - [ ] 异常处理

- [ ] **FireAndForget 变体**
  - [ ] 单向调用 (无返回)
  - [ ] 优化传输

#### 验收标准:
- [ ] 同步方法调用正常
- [ ] 异步方法返回 Future
- [ ] 错误能正确传播
- [ ] FireAndForget 不阻塞

---

### 3.3 Field 通信完整实现
**优先级**: 🟠 高
**预计时间**: 1周

#### 任务清单:
- [ ] **ProxyField 实现**
  - [ ] Get()/GetAsync() 获取值
  - [ ] Set()/SetAsync() 设置值
  - [ ] Subscribe() 订阅更新
  - [ ] GetNextSample() 获取通知

- [ ] **SkeletonField 实现**
  - [ ] RegisterGetHandler()
  - [ ] RegisterSetHandler()
  - [ ] Update() 发送通知
  - [ ] 值缓存

#### 验收标准:
- [ ] Getter/Setter 正常工作
- [ ] 字段更新能触发通知
- [ ] 订阅机制与 Event 一致

---

## Phase 4: 高级特性 (3-4周)

### 4.1 端到端保护完善
**优先级**: 🟡 中等
**预计时间**: 1.5周

#### 任务清单:
- [ ] **E2E Profile 1 实现**
  - [ ] 完善 CRC 计算
  - [ ] Counter 增量和验证
  - [ ] DataID 配置
  - [ ] 检查状态报告

- [ ] **E2E 集成**
  - [ ] Event 自动保护
  - [ ] 配置文件支持
  - [ ] 性能优化

- [ ] **其他 Profile (可选)**
  - [ ] Profile 2, 4, 7 等

#### 验收标准:
- [ ] CRC 校验正确
- [ ] 能检测到数据丢失/重复/乱序
- [ ] E2ECheckStatus 正确返回

---

### 4.2 服务实例管理
**优先级**: 🟡 中等
**预计时间**: 1周

#### 任务清单:
- [ ] InstanceIdentifier 完整支持
- [ ] 实例版本协商
- [ ] 服务迁移支持
- [ ] 多实例负载均衡

---

### 4.3 QoS 策略
**优先级**: 🟡 中等
**预计时间**: 1.5周

#### 任务清单:
- [ ] 事件可靠性配置
- [ ] 优先级管理
- [ ] 带宽限制
- [ ] 延迟优化

---

## Phase 5: 辅助功能 (2-3周)

### 5.1 日志和诊断
**优先级**: 🟠 高
**预计时间**: 1周

#### 任务清单:
- [ ] 集成 lap::log 模块
- [ ] 通信事件日志
- [ ] 错误诊断信息
- [ ] DLT 支持

---

### 5.2 配置管理
**优先级**: 🟠 高
**预计时间**: 1周

#### 任务清单:
- [ ] JSON 配置文件格式
- [ ] 服务接口配置
- [ ] 传输层配置
- [ ] E2E 配置
- [ ] 配置验证工具

配置文件示例:
```json
{
  "services": [
    {
      "service_interface": "radar::RadarService",
      "instance_id": 1,
      "binding": "dbus",
      "dbus": {
        "service_name": "com.example.RadarService",
        "object_path": "/com/example/RadarService/Instance1"
      },
      "events": [
        {
          "name": "ObjectDetected",
          "e2e_profile": "Profile1",
          "e2e_dataid": 100
        }
      ]
    }
  ]
}
```

---

### 5.3 性能监控
**优先级**: 🟡 中等
**预计时间**: 1周

#### 任务清单:
- [ ] 消息统计 (发送/接收计数)
- [ ] 延迟测量
- [ ] 吞吐量监控
- [ ] 性能报告工具

---

## Phase 6: 测试和验证 (持续)

### 6.1 单元测试
**优先级**: 🔴 最高
**进度**: 持续进行

#### 任务清单:
- [ ] **Runtime 测试**
  - [ ] Initialize/Deinitialize
  - [ ] FindService 各种场景
  - [ ] OfferService 各种场景

- [ ] **Event 测试**
  - [ ] 发送/接收
  - [ ] 缓存机制
  - [ ] 多订阅者

- [ ] **Method 测试**
  - [ ] 同步/异步调用
  - [ ] 错误处理
  - [ ] 超时

- [ ] **Field 测试**
  - [ ] Get/Set
  - [ ] 通知机制

- [ ] **E2E 测试**
  - [ ] CRC 计算
  - [ ] Counter 验证
  - [ ] 错误检测

#### 测试文件:
```
test/unittest/
├── test_runtime.cpp
├── test_event.cpp
├── test_method.cpp
├── test_field.cpp
├── test_e2e.cpp
├── test_serialization.cpp
└── test_service_discovery.cpp
```

#### 目标覆盖率: >80%

---

### 6.2 集成测试
**优先级**: 🔴 最高

#### 任务清单:
- [ ] 端到端通信测试
- [ ] 跨进程通信
- [ ] 服务发现测试
- [ ] 压力测试
- [ ] 长时间稳定性测试

#### 测试场景:
```
test/integration/
├── scenario_basic_comm.cpp        # 基础通信
├── scenario_service_discovery.cpp # 服务发现
├── scenario_multi_instance.cpp    # 多实例
├── scenario_stress_test.cpp       # 压力测试
└── scenario_error_handling.cpp    # 错误处理
```

---

### 6.3 示例程序
**优先级**: 🟠 高

#### 任务清单:
- [ ] **基础示例**
  ```
  test/examples/
  ├── example_event_pubsub.cpp      # 事件发布订阅
  ├── example_method_call.cpp       # 方法调用
  ├── example_field_access.cpp      # 字段访问
  └── example_full_service.cpp      # 完整服务示例
  ```

- [ ] **实际应用示例**
  - [ ] Radar 服务 (事件密集型)
  - [ ] Camera 服务 (大数据传输)
  - [ ] Actuator 控制 (低延迟)

---

## Phase 7: 文档和工具 (2周)

### 7.1 开发者文档
**优先级**: 🟠 高
**预计时间**: 1周

#### 任务清单:
- [ ] API 参考文档 (Doxygen)
- [ ] 用户指南
- [ ] 快速入门教程
- [ ] 最佳实践
- [ ] 常见问题 FAQ

---

### 7.2 代码生成工具
**优先级**: 🟡 中等
**预计时间**: 1周

#### 任务清单:
- [ ] 从 ARXML 生成 Proxy/Skeleton
- [ ] 服务接口代码生成器
- [ ] 配置文件生成器

---

## Phase 8: 性能优化 (持续)

### 8.1 零拷贝优化
**优先级**: 🟡 中等

#### 任务清单:
- [ ] 共享内存传输
- [ ] SamplePtr 优化
- [ ] 序列化优化

---

### 8.2 多线程优化
**优先级**: 🟡 中等

#### 任务清单:
- [ ] 无锁队列
- [ ] 线程池
- [ ] 异步 I/O

---

## 关键里程碑

### Milestone 1: 基础可用 (Week 6)
- ✅ 核心 API 定义完成
- ✅ 基础架构搭建
- [ ] D-Bus 绑定基本可用
- [ ] 简单的 Event 通信工作
- [ ] 单元测试框架就绪

### Milestone 2: 功能完整 (Week 12)
- [ ] 所有通信模式实现
- [ ] 服务发现完善
- [ ] E2E 保护工作
- [ ] 集成测试通过

### Milestone 3: 生产就绪 (Week 18)
- [ ] 性能达标
- [ ] 稳定性验证
- [ ] 文档完善
- [ ] 工具链完善

---

## 技术债务和风险

### 高优先级
1. **Future/Promise 实现**: Core 模块尚未提供，需要实现或使用 std::future
2. **ServiceDiscovery 后端**: 当前仅框架，需完整实现
3. **序列化性能**: 需要优化二进制序列化效率

### 中优先级
1. **配置文件解析**: 需要 JSON 解析器支持
2. **多传输层支持**: 目前只规划 D-Bus
3. **线程安全**: 需要全面的并发测试

### 低优先级
1. **代码生成工具**: 手写代码足够，后期优化
2. **SOME/IP 绑定**: 可选特性

---

## 依赖项

### 内部依赖
- ✅ lap::core - 基础类型、Result、Optional
- ✅ lap::log - 日志支持
- [ ] lap::core::Future/Promise - 需要实现

### 外部依赖
- ✅ sdbus-c++ - D-Bus 绑定
- ✅ nlohmann_json - JSON 解析
- [ ] vsomeip (可选) - SOME/IP 支持
- [ ] boost::asio (可选) - 异步 I/O

---

## 团队建议

### 开发角色分工
- **核心开发 (2人)**: Runtime、ServiceDiscovery、绑定层
- **通信模式 (1人)**: Event、Method、Field 实现
- **测试 (1人)**: 单元测试、集成测试、性能测试
- **工具和文档 (1人)**: 代码生成、文档、示例

### 开发优先级建议
1. 🔴 **先实现 D-Bus 绑定** - 这是验证整个架构的关键
2. 🔴 **完善 Event 通信** - 最常用的模式
3. 🟠 **完善服务发现** - 核心功能
4. 🟠 **Method 和 Field** - 补充通信模式
5. 🟡 **高级特性** - E2E、QoS 等

---

## 持续改进

### 定期评审 (每2周)
- [ ] 进度检查
- [ ] 技术债务评估
- [ ] 优先级调整
- [ ] 风险识别

### 质量指标
- [ ] 代码覆盖率 >80%
- [ ] 静态分析无严重问题
- [ ] 性能基准达标
- [ ] 文档完整性 >90%

---

## 总结

**预计总工期**: 16-20 周 (4-5个月)
**关键路径**: D-Bus 绑定 → 服务发现 → 事件通信 → 方法/字段
**当前阶段**: Phase 1 准备阶段
**下一步**: 开始 D-Bus 绑定实现

---

**最后更新**: 2025-10-30
**文档维护**: 随开发进度持续更新
