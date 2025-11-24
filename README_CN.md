# LightAP 通信模块 (Com)

[English](README.md) | [中文](README_CN.md)

[![License](https://img.shields.io/badge/License-CC%20BY--NC%204.0-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![AUTOSAR](https://img.shields.io/badge/AUTOSAR-AP%20R24--11-green.svg)](https://www.autosar.org/)
[![Status](https://img.shields.io/badge/Status-阶段4进行中-yellow.svg)](#实现状态)

> **AUTOSAR 自适应平台 R24-11 兼容通信中间件**  
> 零守护进程面向服务架构 + 插件化传输绑定

**版本：** 3.1.0  
**最后更新：** 2025-11-24  
**架构：** 零守护进程 + 固定槽位 + 双注册表 + 插件绑定  
**状态：** 阶段3完成（iceoryx2），阶段4进行中（DDS + AF_XDP 70%）

---

## 📋 目录

- [概述](#概述)
- [革命性架构](#革命性架构)
- [传输绑定](#传输绑定)
- [实现状态](#实现状态)
- [快速开始](#快速开始)
- [文档](#文档)
- [许可证](#许可证)

---

## 概述

LightAP Com 是符合 **AUTOSAR 自适应平台 R24-11** 标准的通信模块，实现了革命性的**零守护进程、插件化架构**。

### 核心创新

🚀 **零守护进程服务发现（<500ns）**
- 无 RouDi、无 systemd-resolved、无后台进程
- 固定槽位映射：`slot = service_id & 1023`（零哈希冲突）
- 双注册表：QM+AB 注册表 + ASIL-CD 注册表（功能安全隔离）
- 槽位0保护（错误边界），槽位1023广播（跨ASIL）

🔌 **插件化绑定（运行时 .so 加载）**
- ITransportBinding 接口（18个方法，AUTOSAR标准）
- 基于优先级选择（100 → 50 → 20 → 10）
- YAML 配置（零代码更改）

🏗️ **双层 IDL（Franca → AUTOSAR + DDS）**
- Franca IDL 作为单一事实来源
- PyFranca → `ara::com` API + DDS IDL 自动生成
- Schema Hash + TypeIdentifier 强制校验

### 传输绑定

| 绑定 | 优先级 | 延迟 | 吞吐量 | 状态 | 使用场景 |
|------|--------|------|--------|------|----------|
| **iceoryx2** | 100 | <1µs | >10GB/s | ✅ 完成 | 相机、激光雷达 |
| **DDS (AF_XDP)** | 50 | ~10µs | ~1GB/s | 🔄 70% | 跨ECU实时 |
| **自定义协议** | 20 | <10µs | ~500MB/s | 📋 计划中 | 遗留IPC |
| **遗留网关** | 10 | ~50µs | ~100MB/s | 📋 计划中 | SOME/IP桥接 |

---

## 革命性架构

### 零守护进程固定槽位服务发现

**传统方式（iceoryx v1）：** App → RouDi守护进程 → 注册表 → SHM（1-5µs，单点故障）  
**LightAP（iceoryx2）：** App → 直接memfd → seqlock O(1)（<500ns，无单点故障）

**核心机制：**

1. **固定槽位映射**（零冲突）
   ```cpp
   uint16_t slot = service_id & 0x03FF;  // 槽位 0-1023
   
   // 槽位 0：禁止使用（错误检测）
   // 槽位 1-1022：服务
   // 槽位 1023：广播（service_id 0xFFFF）
   ```

2. **双注册表**（功能安全物理隔离）
   ```
   QM+AB 注册表 (/dev/shm/lap_com_registry_qm, 256KB)：
     service_id 0x0001~0x03FE → 槽位 1~1022
     权限：0666（所有进程可读写）
     安全级别：QM / ASIL-A / ASIL-B
   
   ASIL-CD 注册表 (/dev/shm/lap_com_registry_asil, 256KB)：
     service_id 0xF001~0xF3FE → 槽位 1~1022
     权限：0640（受控写入）
     安全级别：ASIL-C / ASIL-D
   
   广播：两个注册表均有槽位 1023 用于跨ASIL事件
   ```

3. **seqlock 无锁并发**
   ```cpp
   struct ServiceSlot {  // 256字节（4×缓存行）
       atomic<uint64_t> seq_num;   // seqlock
       uint16_t service_id;
       uint32_t heartbeat_timestamp;
       // ...
   };
   
   // 读取（<100ns）：
   do {
       seq1 = slot->seq_num.load();
       data = *slot;
       seq2 = slot->seq_num.load();
   } while (seq1 != seq2 || seq1 & 1);
   
   // 写入（原子）：
   slot->seq_num++;  // 奇数（标记开始）
   *slot = new_data;
   slot->seq_num++;  // 偶数（标记结束）
   ```

4. **心跳**（服务活性检测，100ms间隔）

### 插件架构

```
应用程序（纯 AUTOSAR ara::com）：
  auto proxy = MyService::Proxy::CreateProxy(handle);
  proxy->Method(...);  // 无需知道绑定类型

绑定管理器（YAML驱动）：
  读取 binding_config.yaml
  → dlopen("binding_iceoryx2.so") 优先级 100
  → dlopen("binding_dds.so") 优先级 50
  → 为服务选择最佳绑定（本地→iceoryx2，远程→DDS）
```

**ITransportBinding 接口：**
```cpp
class ITransportBinding {
public:
    virtual Result<void> Initialize(const YAML::Node&) = 0;
    virtual Result<ServiceHandleContainer> FindService(...) = 0;
    virtual Result<void> OfferService(...) = 0;
    virtual Result<ByteBuffer> CallMethod(...) = 0;
    virtual Result<void> SendEvent(...) = 0;
    virtual Result<void> SubscribeEvent(...) = 0;
    // + 其他12个方法
};

extern "C" {
    ITransportBinding* CreateBindingInstance();
    void DestroyBindingInstance(ITransportBinding*);
}
```

---

## 传输绑定

### 1. iceoryx2 绑定 ✅ 完成（阶段3）

**性能：** <1µs 延迟，>10GB/s 吞吐量  
**状态：** 100%（3/3测试通过，总计414ms）

**特性：**
- 零守护进程（无RouDi），自我管理
- 真正的零拷贝（共享内存）
- 无锁 Rust 队列
- 256字节槽位对齐

**测试结果（2025-11-23）：**
```
✓ DirectBindingCreation：2ms
✓ CompletePubSubFlow：204ms（10条消息已验证）
✓ PerformanceMetrics：207ms（20条消息，<1µs）
```

**配置：**
```yaml
bindings:
  - type: iceoryx2
    library: /usr/lib/lap/com/binding_iceoryx2.so
    priority: 100
    enabled: true
    config:
      domain_name: lightap_com
      use_huge_pages: true
```

### 2. DDS 绑定（AF_XDP）🔄 70%（阶段4）

**性能：** ~10µs 延迟（目标），~1GB/s  
**状态：** 第1/2周（FastDDS集成完成，AF_XDP待完成）

**已完成：**
- ✅ FastDDS 2.9.1 API 封装
- ✅ IDL 生成（LapComMessage.idl）
- ✅ Publisher/Subscriber
- ✅ 发现（本地 + 远程）

**待完成：**
- 🔄 编译验证
- ⏳ 跨进程测试
- ⏳ AF_XDP 集成（第2周）

**配置（AF_XDP计划）：**
```yaml
bindings:
  - type: dds
    library: /usr/lib/lap/com/binding_dds.so
    priority: 50
    enabled: true
    config:
      domain: 0
      af_xdp_enabled: true
      af_xdp_interface: eth0
      zero_copy: true
```

### 3. 自定义协议绑定 📋（阶段5）

**设计：** UDS + 自定义二进制编解码器，<10µs，~500MB/s

### 4. 遗留网关绑定 📋（阶段5）

**设计：** SOME/IP/D-Bus 网关，~50µs，~100MB/s

---

## 实现状态

| 阶段 | 状态 | 完成度 | 日期 | 文档 |
|------|------|--------|------|------|
| **阶段1** | ✅ | 100% | 2025-11-20 | [IMPLEMENTATION_STATUS.md](doc/reports/IMPLEMENTATION_STATUS.md) |
| **阶段2** | ✅ | 100% | 2025-11-21 | [PHASE2_COMPLETE_FEATURES.md](doc/reports/PHASE2_COMPLETE_FEATURES.md) |
| **阶段3** | ✅ | 100% | 2025-11-23 | [BINDING_STANDARDIZATION_STATUS.md](doc/reports/BINDING_STANDARDIZATION_STATUS.md) |
| **阶段4** | 🔄 | 70% | 2025-11-24+ | [PHASE4_DDS_IMPLEMENTATION_STATUS.md](doc/reports/PHASE4_DDS_IMPLEMENTATION_STATUS.md) |
| **阶段5** | 📋 | 0% | 待定 | - |

**测试：** 30/30通过（已完成阶段100%）

**阶段1：** 固定槽位注册表（memfd、seqlock、心跳）  
**阶段2：** 绑定管理器（ITransportBinding、dlopen、YAML）  
**阶段3：** iceoryx2（C FFI、零拷贝、<1µs已验证）  
**阶段4：** DDS + AF_XDP（FastDDS ✅，AF_XDP ⏳）  
**阶段5：** 自定义 + 遗留绑定

---

## 快速开始

### 前置要求

```bash
# Ubuntu 22.04+
sudo apt install build-essential cmake \
    libboost-all-dev nlohmann-json3-dev libyaml-cpp-dev

# iceoryx2
cargo install iceoryx2-cli

# FastDDS 2.9.1
sudo apt install libfastrtps-dev fastddsgen
```

### 构建

```bash
git clone https://github.com/TreeNeeBee/LightAP.git
cd LightAP
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc) Com
ctest -R Com --verbose
```

### 示例

```cpp
#include <lap/com/ara_com.hpp>
using namespace lap::com;

// 服务端
class MyServiceImpl : public MyServiceSkeleton {
public:
    MyServiceImpl() : MyServiceSkeleton(InstanceSpecifier("/services/MyService")) {}
    Int32 Add(Int32 a, Int32 b) override { return a + b; }
};

int main() {
    Runtime::Initialize();
    MyServiceImpl service;
    service.OfferService();  // 固定槽位注册
    // 事件循环...
}

// 客户端
int main() {
    Runtime::Initialize();
    auto handles = FindService<MyService>(InstanceSpecifier("/services/MyService"));
    auto proxy = MyService::Proxy::CreateProxy(handles.Value()[0]);
    auto result = proxy->Add(10, 20);  // 绑定自动选择
    std::cout << result.Value() << std::endl;
}
```

**配置（binding_config.yaml）：**
```yaml
bindings:
  - type: iceoryx2
    priority: 100
    enabled: true
  - type: dds
    priority: 50
    enabled: true
```

应用程序代码**永不更改** - 纯配置驱动。

---

## 文档

### 架构

- **[SERVICE_DISCOVERY_ARCHITECTURE.md](doc/architecture/SERVICE_DISCOVERY_ARCHITECTURE.md)**（7253行）⭐  
  零守护进程、固定槽位、双注册表、seqlock、心跳

- **[ARCHITECTURE_SUMMARY.md](doc/architecture/ARCHITECTURE_SUMMARY.md)**（3547行）  
  完整架构、4绑定设计、AUTOSAR R24-11

- **[TRANSPORT_MATRIX.md](doc/architecture/TRANSPORT_MATRIX.md)**  
  绑定选择指南

### 规划

- **[IMPLEMENTATION_PLAN_UPDATED.md](doc/planning/IMPLEMENTATION_PLAN_UPDATED.md)**（911行）  
  阶段路线图、AUTOSAR需求

### 报告

- **[IMPLEMENTATION_STATUS.md](doc/reports/IMPLEMENTATION_STATUS.md)** - 阶段1
- **[PHASE4_DDS_IMPLEMENTATION_STATUS.md](doc/reports/PHASE4_DDS_IMPLEMENTATION_STATUS.md)** - 当前
- **[BINDING_STANDARDIZATION_STATUS.md](doc/reports/BINDING_STANDARDIZATION_STATUS.md)** - 阶段3

### 指南

- **[BINDING_SELECTION_GUIDE.md](doc/guides/BINDING_SELECTION_GUIDE.md)**
- **[ICEORYX2_INTEGRATION_GUIDE.md](doc/guides/ICEORYX2_INTEGRATION_GUIDE.md)**
- **[DDS_INTEGRATION_GUIDE.md](doc/guides/DDS_INTEGRATION_GUIDE.md)**
- **[AUTOSAR_QUICK_REFERENCE.md](doc/guides/AUTOSAR_QUICK_REFERENCE.md)**

---

## 许可证

**CC BY-NC 4.0**（知识共享 署名-非商业性使用 4.0）

✅ 允许：教育、个人项目、修改（需署名）  
❌ 禁止：商业使用、生产部署

商业授权联系：<https://github.com/TreeNeeBee/LightAP>

---

## 联系方式

**项目：** LightAP 通信模块  
**仓库：** <https://github.com/TreeNeeBee/LightAP>  
**问题：** <https://github.com/TreeNeeBee/LightAP/issues>

---

## 致谢

- AUTOSAR Consortium（AP R24-11）
- Eclipse iceoryx2（零守护进程 IPC）
- eProsima FastDDS（DDS-RTPS）
- COVESA（vsomeip）

---

<p align="center">
  <strong>零守护进程 • 插件化 • AUTOSAR R24-11</strong><br>
  <sub>为自适应平台社区而生 • CC BY-NC 4.0</sub>
</p>
