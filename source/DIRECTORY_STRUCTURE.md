# Com模块目录结构说明 (AUTOSAR AP R24-11符合)

## 📁 新目录结构 (2025-11-20 重组)

基于AUTOSAR Adaptive Platform R24-11标准和开源项目最佳实践（vsomeip, Fast-DDS, iceoryx2）重新设计。

### 核心原则

1. **职责分离**: runtime / registry / binding / config 各司其职
2. **插件化**: 每个binding编译为独立.so，dlopen动态加载
3. **AUTOSAR兼容**: 符合SWS Communication Management规范
4. **开源对标**: 参考vsomeip/Fast-DDS目录组织

---

## 📂 目录详解

### 1. `api/` - 对外公共API (安装到 /usr/include/lap/com/)

**编译产物**: 头文件（不编译，仅安装）

```
api/
├── runtime.hpp          # Runtime主接口 (FindService/OfferService)
└── ComTypes.hpp         # 通用类型定义 (ServiceID, InstanceID, ErrorCode, etc.)
```

**用途**: 
- 应用程序include的唯一入口
- AUTOSAR ara::com标准接口
- 与liblap_com.so配套使用

**AUTOSAR标准**: 
- SWS_CM_00001 (FindService)
- SWS_CM_00002 (OfferService)
- SWS_CM_00122 (Runtime API)

---

### 2. `runtime/` - Runtime核心实现 (编译到 liblap_com.so)

**编译产物**: liblap_com.so（主库）

```
runtime/
├── inc/                          # Runtime内部头文件
│   ├── Runtime.hpp               # Runtime类定义
│   ├── BindingManager.hpp        # Binding管理器 (dlopen加载)
│   ├── ServiceInstance.hpp       # 服务实例管理
│   ├── ProxyBase.hpp             # 客户端代理基类
│   ├── SkeletonBase.hpp          # 服务端骨架基类
│   ├── Event.hpp                 # 事件通信原语
│   ├── Method.hpp                # 方法调用原语
│   ├── Field.hpp                 # 字段通知原语
│   └── ServiceDiscovery.hpp      # 服务发现接口
└── src/                          # Runtime实现
    ├── Runtime.cpp               # Runtime核心逻辑
    ├── BindingManager.cpp        # Binding插件加载
    └── ServiceInstance.cpp       # 服务实例生命周期
```

**职责**:
- 服务发现API入口 (FindService/OfferService)
- Binding插件管理 (按优先级加载.so)
- 服务实例生命周期管理
- ara::com标准接口实现

**AUTOSAR标准**:
- SWS_CM_00001-00005 (核心API)
- SWS_CM_00122-00125 (Runtime接口)

---

### 3. `registry/` - 服务发现注册表 (编译到 liblap_com.so)

**编译产物**: liblap_com.so（主库的一部分）

```
registry/
├── inc/
│   ├── SharedMemoryRegistry.hpp  # 双注册表架构 (QM+AB / ASIL-CD)
│   ├── ServiceSlot.hpp           # 256字节服务槽位
│   └── SeqLock.hpp               # 无锁并发控制
└── src/
    └── SharedMemoryRegistry.cpp  # 注册表实现
```

**职责**:
- 零守护进程服务发现 (< 500ns延迟)
- 固定槽位映射 (SlotIndex = ServiceID & 1023)
- 双注册表物理隔离 (QM+AB: slots 0-1022, ASIL-CD: 预留)
- seqlock无锁读取 (P99 < 150ns)

**设计亮点**:
- ✅ Week 1: seqlock实现 (P99读延迟27ns)
- ✅ Week 2: 双注册表实现 (14/14测试通过)
- 🎯 性能: FindService P99=149ns, RegisterService P99=1096ns

**架构文档**: `doc/SERVICE_DISCOVERY_ARCHITECTURE.md`

---

### 4. `binding/` - Transport Binding插件 (每个编译为独立.so)

**编译产物**: 独立动态库（.so），dlopen按需加载

```
binding/
├── common/                       # Binding公共接口
│   ├── ITransportBinding.hpp     # 插件接口定义 (纯虚基类)
│   └── BindingTypes.hpp          # Binding通用类型
│
├── iceoryx2/                     # iceoryx2 Binding (priority: 100)
│   ├── inc/
│   │   ├── Iceoryx2Binding.hpp
│   │   ├── Iceoryx2Publisher.hpp
│   │   └── Iceoryx2Subscriber.hpp
│   └── src/
│       ├── Iceoryx2Binding.cpp
│       ├── Iceoryx2Publisher.cpp
│       └── Iceoryx2Subscriber.cpp
│   # 编译为: binding_iceoryx2.so
│   # 特性: 零拷贝, 无守护进程, <1μs延迟, >10GB/s吞吐
│
├── dds/                          # DDS Binding (priority: 50)
│   ├── inc/
│   │   ├── DdsBinding.hpp
│   │   ├── DdsPublisher.hpp
│   │   └── DdsSubscriber.hpp
│   └── src/
│       ├── DdsBinding.cpp
│       ├── DdsPublisher.cpp
│       └── DdsSubscriber.cpp
│   # 编译为: binding_dds.so
│   # 特性: 跨ECU通信, Fast-DDS/CycloneDDS, DDS QoS
│
├── socket/                       # Socket Binding (priority: 30)
│   ├── inc/
│   │   ├── SocketBinding.hpp
│   │   ├── SocketServer.hpp
│   │   ├── SocketClient.hpp
│   │   ├── SocketEventBinding.hpp
│   │   ├── SocketMethodBinding.hpp
│   │   ├── SocketFieldBinding.hpp
│   │   ├── SocketConnectionManager.hpp
│   │   └── ProtobufSerializer.hpp
│   └── src/
│       └── (待实现)
│   # 编译为: binding_socket.so
│   # 特性: UDS本地通信, <10μs延迟, Protobuf序列化
│
├── dbus/                         # D-Bus Binding (priority: 20)
│   ├── inc/
│   │   ├── DbusBinding.hpp
│   │   ├── DBusEventBinding.hpp
│   │   ├── DBusMethodBinding.hpp
│   │   ├── DBusFieldBinding.hpp
│   │   └── DBusConnectionManager.hpp
│   └── src/
│       └── (待实现)
│   # 编译为: binding_dbus.so
│   # 特性: 遗留兼容, sdbus-c++, 诊断服务
│
└── someip/                       # SOME/IP Binding (priority: 10)
    ├── inc/
    │   ├── SomeipBinding.hpp
    │   ├── SomeIpEventBinding.hpp
    │   ├── SomeIpMethodBinding.hpp
    │   ├── SomeIpFieldBinding.hpp
    │   └── SomeIpConnectionManager.hpp
    └── src/
        └── (待实现)
    # 编译为: binding_someip.so (网关模式)
    # 特性: vsomeip网关, SOME/IP ↔ DDS翻译
```

**Binding优先级 (自动选择)**:
1. **iceoryx2** (100): 本地零拷贝，性能最优
2. **DDS** (50): 跨ECU通信
3. **Socket** (30): UDS通用本地IPC
4. **D-Bus** (20): 遗留兼容
5. **SOME/IP** (10): 网关模式（转发到独立进程）

**动态加载逻辑**: `runtime/src/BindingManager.cpp`

---

### 5. `config/` - 配置管理 (编译到 liblap_com.so)

**编译产物**: liblap_com.so（主库的一部分）

```
config/
├── inc/
│   ├── ManifestParser.hpp        # YAML配置解析
│   └── BindingConfig.hpp         # Binding配置管理
└── src/
    ├── ManifestParser.cpp
    └── BindingConfig.cpp
```

**职责**:
- YAML manifest解析 (yaml-cpp)
- 静态服务连接配置 (AUTOSAR R24-11新特性)
- Binding优先级配置
- 服务拓扑静态映射

**配置文件**: 
- `binding_config.yaml` (Binding Manager配置)
- `service_manifest.yaml` (静态服务连接)

**AUTOSAR标准**: 
- SWS_CM_02201 (静态服务连接)
- TPS_MANI_03312-03315 (YAML清单规范)

---

### 6. `util/` - 工具类 (编译到 liblap_com.so)

**编译产物**: liblap_com.so（主库的一部分）

```
util/
├── inc/
│   ├── Logger.hpp                # 日志工具 (集成lap_log)
│   └── Performance.hpp           # 性能监控
└── src/
    ├── Logger.cpp
    └── Performance.cpp
```

**职责**:
- 统一日志接口
- 性能监控埋点
- 调试工具

---

## 🔧 编译配置

### CMakeLists.txt 更新

```cmake
# 主库 (liblap_com.so)
set(MODULE_SOURCE_CXX_DIR 
    ${MODULE_SOURCE_DIR}/runtime/src
    ${MODULE_SOURCE_DIR}/registry/src
    ${MODULE_SOURCE_DIR}/config/src
    ${MODULE_SOURCE_DIR}/util/src
)

# 头文件搜索路径
set(MODULE_EXTERNAL_INCLUDE_DIR
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${MODULE_SOURCE_DIR}/runtime/inc
    ${MODULE_SOURCE_DIR}/registry/inc
    ${MODULE_SOURCE_DIR}/config/inc
    ${MODULE_SOURCE_DIR}/util/inc
)

# 公共API安装路径
install(DIRECTORY ${MODULE_SOURCE_DIR}/api/
        DESTINATION include/lap/com
        FILES_MATCHING PATTERN "*.hpp")

# Binding插件独立编译
add_subdirectory(binding/iceoryx2)
add_subdirectory(binding/dds)
add_subdirectory(binding/socket)
add_subdirectory(binding/dbus)
add_subdirectory(binding/someip)
```

---

## 🗑️ 旧目录清理 (待删除)

以下目录将在验证新结构后删除：

```
✗ comapi/          → 已合并到 runtime/
✗ inc/binding/     → 已移动到 binding/*/inc/
✗ src/binding/     → 已移动到 binding/*/src/
✗ inc/registry/    → 已移动到 registry/inc/
✗ src/registry/    → 已移动到 registry/src/
✗ binding/         → 旧的binding目录，已重组
```

**清理脚本**: `tools/cleanup_old_directories.sh`

---

## 📚 参考文档

1. **AUTOSAR标准**:
   - SWS Communication Management (R24-11)
   - TPS Manifest Specification (R24-11)
   - EXP ara::com API (R24-11)

2. **开源项目参考**:
   - [vsomeip](https://github.com/COVESA/vsomeip) - SOME/IP实现
   - [Fast-DDS](https://github.com/eProsima/Fast-DDS) - DDS实现
   - [iceoryx2](https://github.com/eclipse-iceoryx/iceoryx2) - 零拷贝IPC

3. **内部文档**:
   - `doc/ARCHITECTURE_SUMMARY.md` - 架构总览
   - `doc/SERVICE_DISCOVERY_ARCHITECTURE.md` - 服务发现设计
   - `doc/BINDING_DESIGN.md` - Binding插件设计

---

## ✅ 迁移检查清单

- [x] 创建新目录结构
- [x] 移动registry文件 (inc/ + src/)
- [x] 移动runtime文件 (Runtime.hpp/cpp)
- [x] 复制API公共头文件
- [x] 重组binding目录 (按类型分离)
- [ ] 更新CMakeLists.txt (主库 + binding插件)
- [ ] 更新include路径 (所有cpp/hpp)
- [ ] 编译验证 (liblap_com.so + binding_*.so)
- [ ] 测试验证 (test_registry通过)
- [ ] 删除旧目录
- [ ] 更新文档引用

---

**重组日期**: 2025-11-20  
**执行人**: AI + User  
**参考标准**: AUTOSAR AP R24-11 + vsomeip/Fast-DDS  
**下一步**: 更新CMakeLists.txt，修复include路径，编译验证
