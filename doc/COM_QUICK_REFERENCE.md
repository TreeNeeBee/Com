# Com 模块开发快速参考

## 当前状态概览 (2025-10-30)

### ✅ 已完成的组件
```
API 定义层        [████████████████████] 100%
构建系统          [████████████████████] 100%
类型定义          [████████████████████] 100%
Runtime 框架      [████████████████░░░░]  75%
Proxy/Skeleton    [████████████████░░░░]  80%
Event API         [████████████████░░░░]  80%
Method API        [████████████████░░░░]  80%
Field API         [████████████████░░░░]  80%
E2E Protection    [████████████████░░░░]  80%
Serialization     [████████████████░░░░]  80%
```

### 🎯 下一步优先任务 (按顺序)

#### 1. D-Bus 绑定实现 (最高优先级)
**目标**: 让 Event 能通过 D-Bus 实际工作
**预计时间**: 2-3周
**文件**: `source/binding/dbus/`

```bash
# 创建 D-Bus 绑定目录
mkdir -p modules/Com/source/binding/dbus

# 关键文件
touch modules/Com/source/binding/dbus/DBusBinding.{hpp,cpp}
touch modules/Com/source/binding/dbus/DBusEventBinding.{hpp,cpp}
```

**开发步骤**:
1. 集成 sdbus-c++ 到 Event
2. 实现 Signal 发送和订阅
3. 创建简单的发布-订阅示例
4. 验证消息能通过 D-Bus 传输

---

#### 2. ServiceDiscovery 完整实现
**目标**: FindService/OfferService 真正工作
**预计时间**: 1-2周
**文件**: `source/src/ServiceDiscovery.cpp`

```cpp
// 需要实现的核心功能
class ServiceDiscovery {
    // 查找服务 (同步)
    ServiceHandleContainer FindService(InstanceIdentifier);
    
    // 开始持续查找
    FindServiceHandle StartFindService(Handler, InstanceIdentifier);
    
    // 停止查找
    void StopFindService(FindServiceHandle);
    
    // 注册服务
    Result<void> OfferService(InstanceSpecifier);
    
    // 注销服务
    void StopOfferService(InstanceSpecifier);
};
```

---

#### 3. Event 完整实现
**目标**: 让事件缓存、回调、多订阅者都工作
**预计时间**: 1周
**文件**: `source/inc/Event.hpp` (已有框架)

```cpp
// ProxyEvent 需要实现的方法
template<typename T>
class ProxyEvent {
    Result<void> Subscribe(UInt32 maxSampleCount);
    Result<void> SetReceiveHandler(EventReceiveHandler);
    Result<SamplePtr<T>> GetNextSample(timeout);
    // ... 其他方法
};

// SkeletonEvent 需要实现的方法
template<typename T>
class SkeletonEvent {
    Result<SamplePtr<T>> Allocate();
    Result<void> Send(SamplePtr<T>);
    UInt32 GetSubscriberCount();
    // ... 其他方法
};
```

---

## 开发环境设置

### 编译 Com 模块
```bash
cd /home/ddk/1_workspace/2_middleware/LightAP/build
cmake --build . --target com -j 8
```

### 运行测试 (将来)
```bash
cd /home/ddk/1_workspace/2_middleware/LightAP/build
ctest -R com_test -V
```

### 检查头文件
```bash
cd /home/ddk/1_workspace/2_middleware/LightAP/modules/Com/source/inc
ls -la *.hpp
```

---

## 代码示例

### 示例 1: 服务提供者 (Skeleton)
```cpp
#include <com/ara_com.hpp>

namespace radar {

// 1. 定义服务接口
struct RadarServiceInterface {
    using HandleType = lap::com::ServiceHandleType<RadarServiceInterface>;
};

// 2. 定义事件数据
struct ObjectData {
    float distance;
    float angle;
    uint32_t objectId;
};

// 3. Skeleton 实现
class RadarServiceSkeleton : public lap::com::SkeletonBase {
public:
    // 事件
    lap::com::SkeletonEvent<ObjectData> ObjectDetected;
    
    RadarServiceSkeleton(lap::core::InstanceSpecifier instance) 
        : SkeletonBase(instance) {}
    
    void DetectObject(float dist, float ang) {
        // 分配样本
        auto sample = ObjectDetected.Allocate().Value();
        sample->distance = dist;
        sample->angle = ang;
        sample->objectId = nextId++;
        
        // 发送事件
        ObjectDetected.Send(std::move(sample));
    }
    
private:
    uint32_t nextId = 0;
};

} // namespace radar

// 使用
int main() {
    // 初始化 Runtime
    lap::com::Runtime::Initialize();
    
    // 创建服务实例
    auto instance = lap::core::InstanceSpecifier::Create(
        "/radar/service/instance1").Value();
    radar::RadarServiceSkeleton service(instance);
    
    // 提供服务
    lap::com::Runtime::OfferService(instance);
    
    // 模拟对象检测
    while (running) {
        service.DetectObject(10.5f, 45.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    lap::com::Runtime::Deinitialize();
    return 0;
}
```

---

### 示例 2: 服务使用者 (Proxy)
```cpp
#include <com/ara_com.hpp>

namespace radar {

// 使用相同的接口定义
struct RadarServiceInterface {
    using HandleType = lap::com::ServiceHandleType<RadarServiceInterface>;
};

struct ObjectData {
    float distance;
    float angle;
    uint32_t objectId;
};

// Proxy 实现
class RadarServiceProxy : public lap::com::ProxyBase<RadarServiceInterface> {
public:
    // 事件
    lap::com::ProxyEvent<ObjectData> ObjectDetected;
    
    RadarServiceProxy(HandleType handle) 
        : ProxyBase(handle) {}
};

} // namespace radar

// 使用
int main() {
    lap::com::Runtime::Initialize();
    
    // 查找服务
    auto instance = lap::core::InstanceSpecifier::Create(
        "/radar/service/instance1").Value();
    
    auto handles = lap::com::Runtime::FindService<radar::RadarServiceInterface>(
        instance);
    
    if (handles.empty()) {
        std::cerr << "Service not found!" << std::endl;
        return 1;
    }
    
    // 创建 Proxy
    radar::RadarServiceProxy proxy(handles[0]);
    
    // 订阅事件
    proxy.ObjectDetected.Subscribe(10);
    
    // 设置接收回调
    proxy.ObjectDetected.SetReceiveHandler(
        [](lap::com::SamplePtr<radar::ObjectData> sample) {
            std::cout << "Object detected: "
                      << "distance=" << sample->distance
                      << ", angle=" << sample->angle
                      << ", id=" << sample->objectId << std::endl;
        });
    
    // 等待事件
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    lap::com::Runtime::Deinitialize();
    return 0;
}
```

---

## 项目结构

```
modules/Com/
├── CMakeLists.txt                    # 构建配置
├── COM_DEVELOPMENT_ROADMAP.md        # 开发路线图 (本文档的详细版)
├── README.md                         # 模块介绍
│
├── source/
│   ├── inc/                          # 公共头文件
│   │   ├── ara_com.hpp              # ✅ 主头文件
│   │   ├── ComTypes.hpp             # ✅ 类型定义
│   │   ├── Runtime.hpp              # ✅ Runtime API
│   │   ├── ServiceHandleType.hpp    # ✅ 服务句柄
│   │   ├── ProxyBase.hpp            # ✅ Proxy 基类
│   │   ├── SkeletonBase.hpp         # ✅ Skeleton 基类
│   │   ├── Event.hpp                # ✅ 事件通信
│   │   ├── Method.hpp               # ✅ 方法调用
│   │   ├── Field.hpp                # ✅ 字段访问
│   │   ├── E2EProtection.hpp        # ✅ 端到端保护
│   │   ├── Serialization.hpp        # ✅ 序列化
│   │   └── ServiceDiscovery.hpp     # ⏳ 服务发现 (待完善)
│   │
│   ├── comapi/                       # 实现源码
│   │   └── src/
│   │       └── Runtime.cpp          # ✅ Runtime 实现 (部分)
│   │
│   ├── binding/                      # ⏳ 传输层绑定 (待创建)
│   │   ├── dbus/
│   │   │   ├── DBusBinding.hpp
│   │   │   ├── DBusEventBinding.hpp
│   │   │   └── ...
│   │   └── someip/  (可选)
│   │
│   └── src/                          # ⏳ 其他实现 (待创建)
│       ├── ServiceDiscovery.cpp
│       ├── Event.cpp
│       ├── Method.cpp
│       └── Field.cpp
│
└── test/
    ├── unittest/                     # ⏳ 单元测试 (待创建)
    │   ├── test_runtime.cpp
    │   ├── test_event.cpp
    │   └── ...
    │
    ├── integration/                  # ⏳ 集成测试 (待创建)
    │   └── ...
    │
    └── examples/                     # ⏳ 示例程序
        ├── test_runtime_basic.cpp   # ✅ 基础示例 (已创建)
        ├── example_event_pubsub.cpp
        ├── example_method_call.cpp
        └── example_radar_service.cpp
```

---

## 常用命令

### 构建相关
```bash
# 完整重新构建
rm -rf build && mkdir build && cd build && cmake .. && make -j8

# 只构建 Com 模块
cmake --build build --target com -j8

# 清理 Com 模块
cmake --build build --target clean

# 检查编译错误
cmake --build build --target com 2>&1 | less
```

### 代码检查
```bash
# 查找 TODO
grep -rn "TODO" modules/Com/source/

# 查找 FIXME
grep -rn "FIXME" modules/Com/source/

# 统计代码行数
cloc modules/Com/source/inc/
```

### Git 操作
```bash
# 查看改动
git status
git diff

# 提交改动
git add modules/Com/
git commit -m "feat(com): implement D-Bus event binding"

# 创建分支
git checkout -b feature/dbus-binding
```

---

## 调试技巧

### 1. 使用 D-Bus 工具
```bash
# 查看 D-Bus 消息
dbus-monitor --session

# 查看服务
busctl --user list

# 查看对象
d-feet
```

### 2. GDB 调试
```bash
# 编译 debug 版本
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . --target com

# 调试
gdb ./test_program
(gdb) break lap::com::Runtime::Initialize
(gdb) run
```

### 3. 日志输出
```cpp
#include <log/CLog.hpp>

// 使用 lap::log
LOG_INFO("Service found: {}", handle.ToString());
LOG_ERROR("Failed to subscribe: {}", error.Message());
```

---

## 性能基准

### 目标指标
- Event 延迟: < 1ms (本地)
- Event 吞吐量: > 10,000 msg/s
- Method 调用延迟: < 5ms (本地)
- 内存开销: < 10MB (单服务实例)

### 测试方法
```cpp
// 延迟测试
auto start = std::chrono::high_resolution_clock::now();
event.Send(sample);
auto end = std::chrono::high_resolution_clock::now();
auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
    end - start).count();

// 吞吐量测试
const int N = 100000;
auto start = std::chrono::high_resolution_clock::now();
for (int i = 0; i < N; ++i) {
    event.Send(sample);
}
auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::seconds>(
    end - start).count();
std::cout << "Throughput: " << (N / duration) << " msg/s" << std::endl;
```

---

## 常见问题

### Q: 编译错误 "FindServiceHandle not defined"
A: ComTypes.hpp 未正确包含，检查 include 路径

### Q: 链接错误 "undefined reference to lap::com::Runtime::Initialize"
A: Runtime.cpp 未编译或未链接，检查 CMakeLists.txt

### Q: 运行时段错误
A: 检查是否调用了 Runtime::Initialize()

### Q: 服务发现失败
A: 检查 D-Bus 是否运行，服务名称是否正确

---

## 资源链接

### 内部文档
- [COM_DEVELOPMENT_ROADMAP.md](./COM_DEVELOPMENT_ROADMAP.md) - 详细开发路线图
- [IMPLEMENTATION_NOTES.md](./IMPLEMENTATION_NOTES.md) - 实现笔记

### AUTOSAR 规范
- SWS_CommunicationManagement - 通信管理规范
- SWS_Core - 核心类型规范
- RS_Main - 主需求规范

### 外部参考
- [sdbus-c++](https://github.com/Kistler-Group/sdbus-cpp) - D-Bus C++ 绑定
- [vsomeip](https://github.com/COVESA/vsomeip) - SOME/IP 实现

---

## 贡献指南

### 代码风格
- 遵循 AUTOSAR C++14 编码规范
- 使用 clang-format 格式化
- 头文件保护宏: `LAP_COM_XXX_HPP`
- 命名空间: `lap::com`

### 提交规范
```
feat(com): add D-Bus event binding
fix(com): correct event subscription logic
docs(com): update API documentation
test(com): add unit tests for Runtime
perf(com): optimize event serialization
```

### Code Review 检查点
- [ ] 代码编译通过
- [ ] 单元测试通过
- [ ] 无内存泄漏 (valgrind)
- [ ] 无未定义行为 (sanitizers)
- [ ] 文档更新
- [ ] 符合编码规范

---

**最后更新**: 2025-10-30
**维护者**: Com 模块开发团队
