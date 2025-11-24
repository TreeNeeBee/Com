# Phase 2: Unix Domain Socket FD Passing Implementation

**Status**: ✅ Complete  
**Date**: 2025-11-20  
**Architecture**: SERVICE_DISCOVERY_ARCHITECTURE.md §2.2.2

---

## 概述

Phase 2实现了真正的跨进程共享内存架构，通过Unix Domain Socket + SCM_RIGHTS传递memfd文件描述符，使所有客户端进程共享同一物理内存。

### 核心优势

| 特性 | Phase 1 (当前) | Phase 2 (新增) |
|------|---------------|---------------|
| **内存模型** | 每进程独立memfd | 所有进程共享单一memfd |
| **内存占用** | N进程 × 256KB | 256KB (固定) |
| **初始化** | 每进程创建 | 服务端创建，客户端接收 |
| **一致性** | 依赖内核 | 内核保证物理内存共享 |
| **跨进程延迟** | 未优化 | 最优 (同一物理页) |

---

## 文件清单

### 新增核心文件

```
modules/Com/
├── source/registry/inc/
│   └── RegistryInitializer.hpp          # 服务端初始化器 (138行)
├── source/registry/src/
│   └── RegistryInitializer.cpp          # 服务端实现 (376行)
├── daemon/
│   └── lap-registry-init.cpp            # 独立守护进程 (154行)
├── systemd/
│   ├── lap-registry-qm.socket           # QM注册表socket单元
│   ├── lap-registry-qm-init.service     # QM注册表服务单元
│   ├── lap-registry-asil.socket         # ASIL注册表socket单元
│   └── lap-registry-asil-init.service   # ASIL注册表服务单元
└── test/unittest/
    └── test_multiprocess_registry.cpp   # 多进程集成测试 (274行)
```

### 修改文件

- `source/inc/ComTypes.hpp` - 新增15个注册表错误码 (kMemfdCreateFailed等)
- `source/registry/inc/SharedMemoryRegistry.hpp` - 新增InitializeFromSocket()方法
- `source/registry/src/SharedMemoryRegistry.cpp` - 实现客户端FD接收逻辑
- `CMakeLists.txt` - 新增daemon构建、systemd单元安装、多进程测试

---

## 架构流程

### 系统启动 (systemd)

```
1. systemd启动socket单元
   └─ /run/lap/registry_qm.sock (创建但不监听)

2. 首次客户端连接触发服务激活
   └─ systemd启动 lap-registry-qm-init.service

3. RegistryInitializer运行
   ├─ 创建memfd ("lap_com_registry_qm")
   ├─ ftruncate (256KB)
   ├─ mmap (MAP_SHARED)
   ├─ 初始化1024个槽位
   ├─ fcntl(F_ADD_SEALS) - 密封内存
   └─ 监听UDS socket

4. 客户端连接
   ├─ connect(/run/lap/registry_qm.sock)
   ├─ recvmsg() 接收memfd FD (SCM_RIGHTS)
   ├─ mmap(memfd) 映射到自己地址空间
   └─ 所有进程共享同一物理内存
```

### 客户端初始化 (两种模式)

```cpp
// 模式1: 独立模式 (Phase 1) - 每进程独立创建memfd
SingleRegistry registry(RegistryType::QM);
registry.Initialize();  // 创建独立memfd

// 模式2: 共享模式 (Phase 2) - 从服务端接收memfd
SingleRegistry registry(RegistryType::QM);
registry.InitializeFromSocket("/run/lap/registry_qm.sock");  // 接收共享memfd
```

---

## 编译与安装

### 编译

```bash
cd /home/ddk/1_workspace/2_middleware/LightAP/build
cmake --build . --target lap-registry-init -j8
cmake --build . --target test_multiprocess_registry -j8
```

### 安装

```bash
# 安装daemon到 /usr/local/bin
sudo cmake --install . --component lap-registry-init

# 安装systemd单元到 /etc/systemd/system
sudo cp modules/Com/systemd/*.socket /etc/systemd/system/
sudo cp modules/Com/systemd/*.service /etc/systemd/system/

# 创建运行时目录
sudo mkdir -p /run/lap
sudo chmod 775 /run/lap

# 重新加载systemd配置
sudo systemctl daemon-reload
```

### 启用服务

```bash
# 启用socket激活 (推荐)
sudo systemctl enable lap-registry-qm.socket
sudo systemctl start lap-registry-qm.socket

# 或手动启动服务 (调试用)
/usr/local/bin/lap-registry-init --type=qm --socket=/run/lap/registry_qm.sock
```

---

## 测试

### 单元测试

```bash
cd /home/ddk/1_workspace/2_middleware/LightAP/build
./test_multiprocess_registry
```

**测试覆盖**:
- ✅ 服务端创建memfd并监听UDS
- ✅ 客户端连接并接收FD
- ✅ 多客户端共享内存验证
- ✅ 跨进程服务注册/发现

### 手动测试

**终端1 (服务端)**:
```bash
/usr/local/bin/lap-registry-init --type=qm --socket=/tmp/test.sock
# 输出: Listening on Unix domain socket: /tmp/test.sock
```

**终端2 (客户端1 - 写入)**:
```cpp
#include "SharedMemoryRegistry.hpp"
int main() {
    lap::com::registry::SingleRegistry reg(lap::com::registry::RegistryType::QM);
    reg.InitializeFromSocket("/tmp/test.sock");
    
    // 注册服务到槽位10
    reg.RegisterService(10, 0x1234, 1, 1, 0, "test", "endpoint");
    sleep(10);  // 保持运行
    return 0;
}
```

**终端3 (客户端2 - 读取)**:
```cpp
#include "SharedMemoryRegistry.hpp"
int main() {
    lap::com::registry::SingleRegistry reg(lap::com::registry::RegistryType::QM);
    reg.InitializeFromSocket("/tmp/test.sock");
    
    // 读取槽位10 (应该看到客户端1的服务)
    auto slot = reg.ReadSlot(10);
    if (slot.has_value() && slot.value().IsActive()) {
        printf("Found service: 0x%lx\n", slot.value().service_id);
    }
    return 0;
}
```

---

## 错误码参考

Phase 2新增错误码 (ComErrc):

| 错误码 | 值 | 说明 |
|--------|---|------|
| kMemfdCreateFailed | 32 | memfd_create系统调用失败 |
| kMemfdSealingFailed | 33 | 内存密封失败 |
| kSocketCreationFailed | 34 | 创建Unix domain socket失败 |
| kSocketBindFailed | 35 | socket绑定失败 |
| kSocketConnectFailed | 36 | socket连接失败 |
| kSocketListenFailed | 37 | socket监听失败 |
| kFdPassingFailed | 38 | SCM_RIGHTS FD传递失败 |
| kFdReceiveFailed | 39 | FD接收失败 |

---

## 性能基准

### 内存占用对比

```
场景: 10个客户端进程

Phase 1 (独立memfd):
  进程1: 256KB
  进程2: 256KB
  ...
  进程10: 256KB
  总计: 2.56MB

Phase 2 (共享memfd):
  服务端: 256KB
  客户端1-10: 0KB (共享同一物理内存)
  总计: 256KB
  节省: 90%
```

### 延迟对比 (预估)

| 操作 | Phase 1 | Phase 2 | 改进 |
|------|---------|---------|------|
| FindService | 232ns | 232ns | - |
| 跨进程更新可见性 | ~1µs | ~100ns | 10x |
| 初始化延迟 | 228µs | ~50µs | 4.5x |

---

## 已知限制

1. **systemd socket激活**
   - 当前实现使用手动socket创建
   - TODO: 支持SD_LISTEN_FDS_START

2. **ASIL注册表权限**
   - 需要创建'asil'用户组
   - `sudo groupadd asil`
   - `sudo usermod -aG asil <username>`

3. **运行时目录**
   - /run/lap需要手动创建
   - TODO: 添加tmpfiles.d配置

---

## 下一步 (Phase 3)

- [ ] iceoryx2 Binding集成
- [ ] 零拷贝事件通信
- [ ] ProxyEvent/SkeletonEvent实现
- [ ] 性能目标: IPC延迟 <1µs

---

## 参考文档

- **SERVICE_DISCOVERY_ARCHITECTURE.md** §2.2.2: UDS FD Passing设计
- **IMPLEMENTATION_PLAN_UPDATED.md**: Phase 2详细计划
- **IMPLEMENTATION_ROADMAP_DETAILED.md**: Week 1-3实施路线
- **unix(7)**: Unix domain socket手册
- **cmsg(3)**: 控制消息 (SCM_RIGHTS)
- **systemd.socket(5)**: systemd socket激活

---

**状态**: ✅ Phase 2 Complete - 准备进入Phase 3
