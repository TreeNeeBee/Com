# LightAP 通信模块 (Com)

[English](README.md) | [中文](README_CN.md)

[![License](https://img.shields.io/badge/License-CC%20BY--NC%204.0-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![AUTOSAR](https://img.shields.io/badge/AUTOSAR-AP%20R25--11-green.svg)](https://www.autosar.org/)
[![Tests](https://img.shields.io/badge/CTest-26%20Suites%20Passing-brightgreen.svg)](#测试状态)

> **AUTOSAR Adaptive Platform R25-11 兼容通信中间件**  
> 零守护进程面向服务架构 + 插件化传输绑定

**版本：** 4.0.0  
**最后更新：** 2026-03-02  
**Binding 状态：** CoreIPC ✅ | DDS ✅ | SOME/IP ⚠️ | Socket ⚠️ | D-Bus ⚠️  
**测试：** 26 个 CTest 套件通过（211 个测试用例）

---

## 目录

- [概述](#概述)
- [架构](#架构)
- [传输绑定](#传输绑定)
- [快速开始](#快速开始)
- [代码生成器](#代码生成器)
- [示例](#示例)
- [测试状态](#测试状态)
- [文档](#文档)
- [许可证](#许可证)

---

## 概述

LightAP Com 是符合 **AUTOSAR AP R25-11** 标准的通信模块，采用**零守护进程、插件化架构**。

### 核心特性

- **零守护进程服务发现** — 无 RouDi、无后台进程。固定槽位映射 + seqlock O(1) 查找（<500ns）
- **插件化传输绑定** — `ITransportBinding` NVI 接口，优先级自动选择，YAML 配置驱动
- **Split Gen 代码生成** — `lap-sidl-gen` 从 Franca FIDL 生成隔离的 `gen_server/` 和 `gen_client/`
- **双层 IDL** — Franca IDL → ara::com API + DDS IDL（由 `fastddsgen` 自动生成）

### Binding 实现状态

| Binding | 优先级 | 延迟 | 吞吐量 | 状态 | 适用场景 |
|---------|--------|------|--------|------|---------|
| **CoreIPC** | 100 | < 1µs | > 10 GB/s | ✅ 已实现 | 同 ECU 零拷贝共享内存 |
| **DDS** | 80 | < 10µs SHM / < 30µs UDP | > 1 GB/s | ✅ 已实现 | 跨 ECU、QoS、动态发现 |
| SOME/IP | 60 | — | — | ⚠️ 待实现 | AUTOSAR CP 互通 |
| Socket | 40 | — | — | ⚠️ 待实现 | 轻量 IPC、遗留系统集成 |
| D-Bus | 20 | — | — | ⚠️ 待实现 | systemd 集成 |

---

## 架构

```
┌─────────────────────────────────────────────────┐
│           应用层                                 │
│   (ara::com API — Proxy / Skeleton / Runtime)   │
├─────────────────────────────────────────────────┤
│           Binding Manager                       │
│   (优先级选择, YAML 配置, dlopen)                │
├──────────────────┬──────────────────────────────┤
│  CoreIPC ✅      │  DDS (Fast-DDS 3.x) ✅      │
│  零拷贝共享内存   │  SHM + UDP, Discovery Server│
│  < 1µs, > 10GB/s │  < 10µs SHM / < 30µs UDP   │
├──────────────────┼──────────────────────────────┤
│  SOME/IP ⚠️     │  Socket ⚠️  │  D-Bus ⚠️    │
│  (骨架)          │  (骨架)      │  (骨架)       │
└──────────────────┴─────────────┴────────────────┘
```

### 核心机制

- **固定槽位映射**：`slot = service_id & 0x03FF` — 1024 个槽位，零哈希冲突
- **双注册表**：QM+AB (0666) + ASIL-CD (0640) — 功能安全物理隔离
- **seqlock 无锁并发**：读取 < 100ns，原子写入
- **心跳检测**：100ms 间隔服务活性检测

详细架构请参见 [docs/architecture/ARCHITECTURE_SUMMARY.md](docs/architecture/ARCHITECTURE_SUMMARY.md)。

---

## 传输绑定

### CoreIPC ✅

零守护进程、零拷贝共享内存 IPC，适用于同 ECU 通信。

- 延迟 < 1µs (64B)，< 10µs (1MB)
- 吞吐量 > 10 GB/s
- 无外部依赖

### DDS ✅

eProsima Fast-DDS 3.x 集成，适用于跨 ECU 分布式通信。

- 延迟 < 10µs SHM（同机），< 30µs UDP（跨机）
- 吞吐量 > 1 GB/s
- 支持 Discovery Server、QoS Reliability/Durability

### SOME/IP / Socket / D-Bus ⚠️ 待实现

`source/binding/{someip,socket,dbus}/` 目录下仅有骨架文件，核心逻辑尚未实现。

---

## 快速开始

### 前置要求

```bash
# Ubuntu 22.04+
sudo apt install build-essential cmake \
    libboost-all-dev nlohmann-json3-dev libyaml-cpp-dev

# DDS binding
sudo apt install libfastdds-dev fastddsgen
```

### 构建

```bash
git clone https://github.com/nicx-next/LightAP.git
cd LightAP
mkdir build && cd build
cmake .. -DENABLE_BUILD_TESTS=ON -DENABLE_BUILD_EXAMPLES=ON
cmake --build . -j$(nproc)
ctest --verbose
```

---

## 代码生成器

`lap-sidl-gen` 从 Franca FIDL 定义生成服务端和客户端代码，采用 **Split Gen 架构**（隔离的 `gen_server/` 和 `gen_client/` 目录）。

```bash
# 生成服务端代码
lap-sidl-gen -i MyService.fidl -o gen_server --server

# 生成客户端代码
lap-sidl-gen -i MyService.fidl -o gen_client --client

# 同时生成 + 指定 binding
lap-sidl-gen -i MyService.fidl -o gen_server --server --binding coreipc,dds
lap-sidl-gen -i MyService.fidl -o gen_client --client --binding coreipc,dds
```

生成器架构详见 [docs/architecture/GENERATOR.md](docs/architecture/GENERATOR.md)。

---

## 示例

三个递进复杂度的示例展示不同 binding 场景：

| 示例 | Binding | 说明 |
|------|---------|------|
| **helloworld** | CoreIPC | 基础 Method + Event + Field（46 个测试） |
| **helloworld2** | CoreIPC + DDS | 双 binding 自动选择（66 个测试） |
| **helloworld3** | DDS-only | 跨 ECU 通信模式（99 个测试） |

所有示例均使用 Split Gen 架构（`gen_server/` + `gen_client/`）。

```bash
# 运行指定示例测试
ctest -R helloworld_test --verbose
ctest -R helloworld2_test --verbose
ctest -R helloworld3_test --verbose
```

---

## 测试状态

**26 个 CTest 套件，211 个示例测试用例 — 全部通过。**

```
helloworld_test:   46/46  ✅  (CoreIPC)
helloworld2_test:  66/66  ✅  (CoreIPC + DDS)
helloworld3_test:  99/99  ✅  (DDS-only)
+ 23 个单元测试套件      ✅
```

---

## 文档

完整文档索引：[docs/README.md](docs/README.md)（[中文版](docs/README_CN.md)）

### 核心文档

| 文档 | 说明 |
|------|------|
| [**docs/guides/DEVELOPMENT_GUIDE.md**](docs/guides/DEVELOPMENT_GUIDE.md) | 完整开发流程 (v2.0) — FIDL → Split Gen → 构建 → 测试 |
| [docs/guides/BINDING_SELECTION_GUIDE.md](docs/guides/BINDING_SELECTION_GUIDE.md) | Binding 选择决策树 |
| [docs/guides/DDS_INTEGRATION_GUIDE.md](docs/guides/DDS_INTEGRATION_GUIDE.md) | DDS (Fast-DDS 3.x) 集成指南 |
| [docs/architecture/ARCHITECTURE_SUMMARY.md](docs/architecture/ARCHITECTURE_SUMMARY.md) | 模块架构总览 (v4.0) |
| [docs/architecture/GENERATOR.md](docs/architecture/GENERATOR.md) | lap-sidl-gen 代码生成器架构 |
| [docs/architecture/TRANSPORT_MATRIX.md](docs/architecture/TRANSPORT_MATRIX.md) | 传输协议状态矩阵 |
| [docs/guides/AUTOSAR_QUICK_REFERENCE.md](docs/guides/AUTOSAR_QUICK_REFERENCE.md) | AUTOSAR AP R25-11 API 快速参考 |

---

## 许可证

**CC BY-NC 4.0**（知识共享 署名-非商业性使用 4.0）

- ✅ 允许：教育、个人项目、修改（需署名）
- ❌ 禁止：商业使用、生产部署

商业授权：<https://github.com/nicx-next/LightAP>

---

## 致谢

- AUTOSAR Consortium（Adaptive Platform R25-11）
- eProsima Fast-DDS 3.x（DDS-RTPS）
- CoreIPC（零守护进程共享内存）

---

<p align="center">
  <strong>零守护进程 · 插件化 · AUTOSAR R25-11</strong><br>
  <sub>为自适应平台社区而生 · CC BY-NC 4.0</sub>
</p>
