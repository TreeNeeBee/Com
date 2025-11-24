# 服务发现架构设计

## 文档信息

- **版本**: 3.1 (零 Daemon + 双层 IDL 架构)
- **日期**: 2025-11-24
- **标准**: AUTOSAR AP R24-11 (November 2024)
- **命名空间**: lap::com (100% 兼容 AUTOSAR ara::com)
- **架构特性**: 
  - 零 Daemon + 固定槽位 + iceoryx2 共享内存 + seqlock + 心跳
  - **双层 IDL 设计**: Franca IDL (SSOT) → AUTOSAR API + DDS IDL
  - 强制版本一致性验证 (Schema Hash + TypeIdentifier)
  - QoS 独立配置 (YAML)
  - DDS 类型隔离 (Proxy 层封装)
- **基准文档**:
  - AUTOSAR_AP_SWS_CommunicationManagement.pdf
  - AUTOSAR_AP_SWS_NetworkManagement.pdf
  - iceoryx2 Architecture & Design
  - Franca IDL Specification
- **状态**: 设计完成 (生产就绪)

---

## 目录

- [服务发现架构设计](#服务发现架构设计)
  - [文档信息](#文档信息)
  - [目录](#目录)
  - [第 1 章: 概述](#第-1-章-概述)
    - [1.1 零 Daemon 架构革命](#11-零-daemon-架构革命)
    - [1.2 服务发现的作用](#12-服务发现的作用)
    - [1.3 AUTOSAR 标准需求追溯](#13-autosar-标准需求追溯)
    - [1.4 架构层次（零 Daemon 设计）](#14-架构层次零-daemon-设计)
    - [1.5 服务发现策略（双层策略，v3.0 简化版）](#15-服务发现策略双层策略v30-简化版)
  - [第 2 章: 零 Daemon 固定槽位自注册架构](#第-2-章-零-daemon-固定槽位自注册架构)
    - [2.1 核心设计原理](#21-核心设计原理)
      - [2.1.1 固定槽位映射](#211-固定槽位映射)
      - [2.1.2 seqlock 无锁同步](#212-seqlock-无锁同步)
      - [2.1.3 心跳机制](#213-心跳机制)
      - [2.1.4 Instance ID 使用示例](#214-instance-id-使用示例)
        - [场景 1: 创建感知域雷达服务实例（ASIL-B 主通道）](#场景-1-创建感知域雷达服务实例asil-b-主通道)
        - [场景 2: ASIL 冗余通道配置（ASIL-D 级别服务）](#场景-2-asil-冗余通道配置asil-d-级别服务)
      - [场景 3: 槽位映射规则 (YAML 配置)](#场景-3-槽位映射规则-yaml-配置)
        - [场景 4: 运行时槽位查找优化（零冲突设计 + 槽位 0 保护）](#场景-4-运行时槽位查找优化零冲突设计--槽位-0-保护)
    - [2.2 systemd Socket Activation 集成 (零常驻 Daemon)](#22-systemd-socket-activation-集成-零常驻-daemon)
      - [2.2.1 设计原理](#221-设计原理)
      - [2.2.2 memfd\_create + SCM\_RIGHTS 文件描述符传递](#222-memfd_create--scm_rights-文件描述符传递)
        - [Step 1: oneshot 进程创建 memfd](#step-1-oneshot-进程创建-memfd)
        - [Step 2: 客户端接收 memfd fd](#step-2-客户端接收-memfd-fd)
      - [2.2.3 systemd 配置文件](#223-systemd-配置文件)
      - [2.2.4 1GB HugePage 支持](#224-1gb-hugepage-支持)
      - [2.2.5 Guard Page 保护](#225-guard-page-保护)
      - [2.2.6 权限隔离与安全防护 (FuSa + Security Hardening)](#226-权限隔离与安全防护-fusa--security-hardening)
        - [方案 1: 双 memfd 物理隔离 (推荐)](#方案-1-双-memfd-物理隔离-推荐)
        - [方案 2: mprotect 运行时权限控制](#方案-2-mprotect-运行时权限控制)
        - [方案 3: 完整性保护 (CRC32 + seqlock)](#方案-3-完整性保护-crc32--seqlock)
        - [方案 4: 外部注入防护](#方案-4-外部注入防护)
        - [安全审计与合规](#安全审计与合规)
    - [2.3 配置与部署](#23-配置与部署)
      - [2.3.1 槽位映射配置 (slot\_mapping.yaml)](#231-槽位映射配置-slot_mappingyaml)
      - [2.3.2 进程启动流程](#232-进程启动流程)
    - [2.4 性能分析与优化](#24-性能分析与优化)
      - [2.4.1 延迟分解](#241-延迟分解)
      - [2.4.2 优化技巧](#242-优化技巧)
        - [1. CPU 缓存优化](#1-cpu-缓存优化)
        - [2. NUMA 感知](#2-numa-感知)
        - [3. Lock-free 心跳](#3-lock-free-心跳)
    - [2.5 与 iceoryx2 数据通信集成](#25-与-iceoryx2-数据通信集成)
  - [第 3 章: 核心组件设计](#第-3-章-核心组件设计)
    - [3.1 ServiceRegistry (服务注册表)](#31-serviceregistry-服务注册表)
      - [3.1.1 数据结构](#311-数据结构)
      - [3.1.2 服务版本兼容性检查](#312-服务版本兼容性检查)
    - [3.2 ServiceDiscoveryManager (服务发现管理器)](#32-servicediscoverymanager-服务发现管理器)
      - [3.2.1 核心接口](#321-核心接口)
      - [3.2.2 OfferService 流程](#322-offerservice-流程)
      - [3.2.3 FindService 流程](#323-findservice-流程)
      - [3.2.4 StartFindService 流程 (持续发现)](#324-startfindservice-流程-持续发现)
    - [3.3 FindServiceHandlerManager (回调管理器)](#33-findservicehandlermanager-回调管理器)
  - [第 4 章: Transport Binding 适配](#第-4-章-transport-binding-适配)
    - [4.1 ITransportBinding 接口](#41-itransportbinding-接口)
    - [4.2 iceoryx2 Binding 服务发现（零拷贝优化）](#42-iceoryx2-binding-服务发现零拷贝优化)
      - [4.2.1 OfferService 实现](#421-offerservice-实现)
      - [4.2.2 FindService 实现（零拷贝访问）](#422-findservice-实现零拷贝访问)
    - [4.3 DDS Binding 服务发现](#43-dds-binding-服务发现)
      - [4.3.1 DDS Discovery 方式](#431-dds-discovery-方式)
      - [方式 1: DomainParticipant USER\_DATA QoS (SWS\_CM\_11008)](#方式-1-domainparticipant-user_data-qos-sws_cm_11008)
      - [方式 2: Topic-Based Discovery (SWS\_CM\_90503)](#方式-2-topic-based-discovery-sws_cm_90503)
    - [4.4 SOME/IP Service Discovery (SOME/IP-SD)](#44-someip-service-discovery-someip-sd)
      - [4.4.1 核心概念](#441-核心概念)
      - [4.4.2 OfferService 实现](#442-offerservice-实现)
      - [4.4.3 FindService 实现（被动监听）](#443-findservice-实现被动监听)
  - [第 5 章: 跨节点（跨 ECU）服务发现架构](#第-5-章-跨节点跨-ecu服务发现架构)
    - [5.1 架构概述与设计目标](#51-架构概述与设计目标)
      - [5.1.1 核心设计目标](#511-核心设计目标)
      - [5.1.2 ASIL 服务通过 QM SD-Proxy 的设计](#512-asil-服务通过-qm-sd-proxy-的设计)
      - [5.1.3 架构优势](#513-架构优势)
      - [5.1.4 SD-Proxy 内存使用分析（总计 ~200MB）](#514-sd-proxy-内存使用分析总计-200mb)
    - [5.2 SD Proxy 服务设计](#52-sd-proxy-服务设计)
      - [5.2.1 SD Proxy 槽位分配策略](#521-sd-proxy-槽位分配策略)
      - [5.2.2 SD Proxy 核心组件实现](#522-sd-proxy-核心组件实现)
      - [5.2.3 Fast DDS Discovery Server 集成](#523-fast-dds-discovery-server-集成)
      - [5.2.4 SD Proxy 主进程实现](#524-sd-proxy-主进程实现)
    - [5.3 典型查询流程（Application → Local-SD → SD-Proxy）](#53-典型查询流程application--local-sd--sd-proxy)
      - [5.3.1 完整查询流程](#531-完整查询流程)
      - [5.3.2 代码示例](#532-代码示例)
      - [5.3.3 配置示例](#533-配置示例)
      - [5.3.4 远程服务缓存（性能优化）](#534-远程服务缓存性能优化)
    - [5.4 配置与部署](#54-配置与部署)
      - [5.4.1 SD Proxy 配置文件](#541-sd-proxy-配置文件)
      - [5.4.2 Systemd 服务单元](#542-systemd-服务单元)
      - [5.4.3 安装与启动](#543-安装与启动)
    - [5.5 性能对比与监控](#55-性能对比与监控)
      - [5.5.1 SD Proxy vs 传统方案性能对比](#551-sd-proxy-vs-传统方案性能对比)
      - [5.5.2 SD Proxy 监控指标](#552-sd-proxy-监控指标)
    - [5.6 接口一致性保证机制](#56-接口一致性保证机制)
      - [5.6.1 接口一致性设计原则](#561-接口一致性设计原则)
        - [✅ (1) Franca IDL 作为单一真相源 (SSOT)](#-1-franca-idl-作为单一真相源-ssot)
        - [✅ (2) 强制版本一致性验证 (Schema Hash + TypeIdentifier)](#-2-强制版本一致性验证-schema-hash--typeidentifier)
        - [✅ (3) QoS 配置独立于 IDL (YAML 配置)](#-3-qos-配置独立于-idl-yaml-配置)
        - [✅ (4) DDS 类型不泄漏到应用层 (Proxy 层隔离)](#-4-dds-类型不泄漏到应用层-proxy-层隔离)
        - [✅ (5) Local-SD API 永不变化（对应用透明）](#-5-local-sd-api-永不变化对应用透明)
      - [5.6.2 接口一致性验证流程](#562-接口一致性验证流程)
      - [5.6.3 双层 IDL 设计优势总结](#563-双层-idl-设计优势总结)
  - [第 6 章: API 使用示例](#第-6-章-api-使用示例)
    - [6.1 服务提供者 (Skeleton)](#61-服务提供者-skeleton)
    - [6.2 服务消费者 (Proxy) - 单次查询](#62-服务消费者-proxy---单次查询)
    - [6.3 服务消费者 - 持续发现](#63-服务消费者---持续发现)
  - [第 7 章: 配置与清单](#第-7-章-配置与清单)
    - [7.1 ServiceInterfaceDeployment (服务接口部署配置)](#71-serviceinterfacedeployment-服务接口部署配置)
    - [7.2 ProvidedServiceInstance (服务提供实例)](#72-providedserviceinstance-服务提供实例)
    - [7.3 RequiredServiceInstance (服务消费实例)](#73-requiredserviceinstance-服务消费实例)
  - [第 8 章: 性能优化](#第-8-章-性能优化)
    - [8.1 服务缓存](#81-服务缓存)
    - [8.2 批量查询优化](#82-批量查询优化)
    - [8.3 异步服务发现](#83-异步服务发现)
  - [第 9 章: 错误处理](#第-9-章-错误处理)
    - [9.1 ComErrc 错误码](#91-comerrc-错误码)
    - [9.2 错误处理示例](#92-错误处理示例)
  - [第 10 章: 测试策略](#第-10-章-测试策略)
    - [10.1 单元测试](#101-单元测试)
    - [10.2 集成测试](#102-集成测试)
    - [10.3 性能测试](#103-性能测试)
  - [第 11 章: 实施路线图](#第-11-章-实施路线图)
    - [Week 1-2: 核心框架](#week-1-2-核心框架)
    - [Week 3-4: Transport Binding 插件集成](#week-3-4-transport-binding-插件集成)
    - [Week 5-6: API 实现](#week-5-6-api-实现)
    - [Week 7-8: 性能优化与生产就绪](#week-7-8-性能优化与生产就绪)
    - [Week 9-10: Runtime 集成与优化 ⭐](#week-9-10-runtime-集成与优化-)
    - [Week 11-12: 跨节点服务发现（可选特性）](#week-11-12-跨节点服务发现可选特性)
  - [第 12 章: 性能指标](#第-12-章-性能指标)
    - [12.1 服务发现延迟对比（v3.0 零守护进程架构）](#121-服务发现延迟对比v30-零守护进程架构)
    - [12.2 资源占用](#122-资源占用)
    - [12.3 吞吐量](#123-吞吐量)
  - [附录](#附录)
    - [附录 A: 设计背景](#附录-a-设计背景)
    - [附录 B: AUTOSAR R24-11 标准对比](#附录-b-autosar-r24-11-标准对比)
      - [B.1 三种服务发现机制对比](#b1-三种服务发现机制对比)
      - [B.2 架构优势总结](#b2-架构优势总结)
    - [附录 C: 配置文件集成（YAML 格式）](#附录-c-配置文件集成yaml-格式)
      - [C.1 slot\_mapping.yaml（服务槽位映射）](#c1-slot_mappingyaml服务槽位映射)
      - [C.2 Franca IDL 工具链](#c2-franca-idl-工具链)
    - [附录 D: 基准测试与性能优化](#附录-d-基准测试与性能优化)
  - [参考资源](#参考资源)
    - [标准文档](#标准文档)
    - [相关章节](#相关章节)
    - [依赖库](#依赖库)

---

## 第 1 章: 概述

### 1.1 零 Daemon 架构革命

**传统架构的痛点**：

传统服务发现方案存在以下问题：

1. **Daemon 依赖**: RouDi (iceoryx v1) / systemd-resolved / avahi-daemon 等守护进程
   - 单点故障风险
   - 启动顺序依赖
   - 额外的进程调度开销
   
2. **通信开销**: Domain Socket / D-Bus / TCP 查询延迟
   - Domain Socket: ~1-5µs
   - D-Bus/SOME-IP SD: 5-100ms
   
3. **不确定性**: IPC/网络通信引入的抖动
   - 影响实时系统的可预测性

**本架构突破性创新**：

```text
零 Daemon + 固定槽位自注册 + iceoryx2 共享内存
= < 500ns 延迟 + 100% 确定性 + 零单点故障
```

**核心设计原则**：

- ✅ **零守护进程**: 完全去中心化，无任何 Daemon
- ✅ **固定槽位**: 服务 ID → 槽位映射，编译期或静态配置确定
- ✅ **双层 IDL 架构**: Franca IDL (SSOT) 自动生成 AUTOSAR API + DDS Types
  - Franca IDL 作为统一接口模型
  - PyFranca 生成 AUTOSAR ara::com API (应用层)
  - 自动转换为 DDS IDL，FastDDS-gen 生成 TypeSupport (传输层)
  - 强制版本一致性验证 (Schema Hash + TypeIdentifier)
  - QoS 独立配置 (YAML)，不污染 IDL
  - DDS 类型完全隔离，应用层零依赖
- ✅ **共享内存**: iceoryx2 提供 memfd + 1GB 大页 + 权限管理
- ✅ **Lock-Free**: seqlock 无锁读取，原子写入
- ✅ **心跳机制**: 进程级生命周期检测
- ✅ **FuSa 就绪**: Guard page + 权限隔离 + QM/ASIL 物理分离

### 1.2 服务发现的作用

在 AUTOSAR Adaptive Platform 中，服务发现（Service Discovery）是 lap::com（100% 兼容 ara::com）的核心功能，负责：

1. **服务注册**: 服务提供者向系统注册可用服务 (OfferService)
2. **服务查找**: 服务消费者查找并定位可用服务 (FindService)
3. **动态绑定**: 运行时建立服务提供者与消费者的连接
4. **生命周期管理**: 监控服务的上线/下线状态
5. **实例管理**: 支持同一服务的多个实例

**本架构实现方式**：

- 注册: 进程启动时写入固定槽位 (原子操作)
- 查找: 直接读取共享内存槽位 (零拷贝、零延迟)
- 生命周期: 心跳超时检测 + 进程退出自动清理

### 1.3 AUTOSAR 标准需求追溯

| 需求 ID | 描述 | 本模块实现 |
|---------|------|-----------|
| **RS_CM_00101** | 服务提供 (OfferService) | ✅ 固定槽位写入 |
| **RS_CM_00102** | 服务查找 (FindService) | ✅ 共享内存读取 |
| **RS_CM_00105** | 停止服务提供 (StopOfferService) | ✅ 槽位清除 + 心跳停止 |
| **RS_CM_00200** | 服务实例识别 | ✅ InstanceIdentifier / InstanceSpecifier |
| **RS_CM_00204** | 服务接口契约 | ✅ ServiceInterface Version 管理 |
| **SWS_CM_00122** | FindService API 定义 | ✅ Proxy::FindService() |
| **SWS_CM_00123** | StartFindService API 定义 | ✅ Runtime::StartFindService() |
| **SWS_CM_00125** | StopFindService API 定义 | ✅ Runtime::StopFindService() |
| **SWS_CM_00110** | Skeleton OfferService | ✅ Skeleton::OfferService() |
| **SWS_CM_00111** | Skeleton StopOfferService | ✅ Skeleton::StopOfferService() |

### 1.4 架构层次（零 Daemon 设计）

```text
┌──────────────────────────────────────────────────────────────────────────┐
│             lap::com Application (完全无感知)                              │
│  ┌─────────────────────────────────────────────────────────────────────┐ │
│  │ ServiceProxy / ServiceSkeleton                                      │ │
│  │  - FindService() → 直接读取共享内存槽位 (< 500ns)                    │ │
│  │  - OfferService() → 直接写入固定槽位 (原子操作)                      │ │
│  └─────────────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────────────┘
                          ↓ (标准 ara::com API 调用)
┌──────────────────────────────────────────────────────────────────────────┐
│          lap::com Runtime (极简化，~300 行新增代码)                        │
│  ┌──────────────────────────────────────────────────────────────────────┐│
│  │ SharedMemoryRegistry (固定槽位注册表)                                 ││
│  │  ┌────────────────────────────────────────────────────────────────┐  ││
│  │  │ 服务槽位数组 (1024 slots, 每槽 256 bytes)                      │  ││
│  │  │  Slot[0]: [ServiceID | InstanceID | Version | Endpoint | ...]  │  ││
│  │  │  Slot[1]: [ServiceID | InstanceID | Version | Endpoint | ...]  │  ││
│  │  │  ...                                                             │  ││
│  │  │  Slot[1023]: [Reserved]                                         │  ││
│  │  └────────────────────────────────────────────────────────────────┘  ││
│  │                                                                        ││
│  │  ┌────────────────────────────────────────────────────────────────┐  ││
│  │  │ seqlock + 版本控制 (Lock-Free 读取)                             │  ││
│  │  │  - sequence: atomic<uint64_t> (奇数=写中，偶数=可读)            │  ││
│  │  │  - version: uint32_t (单调递增)                                 │  ││
│  │  └────────────────────────────────────────────────────────────────┘  ││
│  │                                                                        ││
│  │  ┌────────────────────────────────────────────────────────────────┐  ││
│  │  │ 心跳机制 (进程生命周期检测)                                     │  ││
│  │  │  - last_heartbeat_ns: 纳秒级时间戳                              │  ││
│  │  │  - timeout: 5s (可配置)                                         │  ││
│  │  │  - 后台线程周期性检测并清理失效槽位                             │  ││
│  │  └────────────────────────────────────────────────────────────────┘  ││
│  └──────────────────────────────────────────────────────────────────────┘│
│  ┌──────────────────────────────────────────────────────────────────────┐│
│  │ SlotAllocator (服务 ID → 槽位映射)                                   ││
│  │  - 静态配置: /etc/lap/com/slot_mapping.yaml                          ││
│  │  - 动态分配: Hash(ServiceID + InstanceID) % MAX_SLOTS                ││
│  │  - 冲突解决: Linear probing / 预留槽位                                ││
│  └──────────────────────────────────────────────────────────────────────┘│
└──────────────────────────────────────────────────────────────────────────┘
                          ↓ (底层内存管理)
┌──────────────────────────────────────────────────────────────────────────┐
│          iceoryx2 底层能力 (进程自管理，零 Daemon)                         │
│  ┌──────────────────────────────────────────────────────────────────────┐│
│  │ 双注册表物理隔离 (QM+AB Registry + ASIL-CD Registry)                 ││
│  │  ┌─────────────────────────────────────────────────────────────────┐ ││
│  │  │ QM+AB Registry: /dev/shm/lap_com_registry_qm (256KB, 1024 slots)│ ││
│  │  │  - 槽位 0~1023: QM + ASIL-A/B 服务                              │ ││
│  │  │  - 权限: 0666 (所有进程可读写)                                  │ ││
│  │  │  - service_id: 0x0001~0x0417, 0xFFFF(广播)                    │ ││
│  │  │  - 槽位 1023: 所有进程都订阅，实现广播双向互通                 │ ││
│  │  └─────────────────────────────────────────────────────────────────┘ ││
│  │  ┌─────────────────────────────────────────────────────────────────┐ ││
│  │  │ ASIL-CD Registry: /dev/shm/lap_com_registry_asil (256KB, 1024s) │ ││
│  │  │  - 槽位 0~1023: ASIL-C/D 高安全等级服务                         │ ││
│  │  │  - 权限: 0640 (控制进程写，其他只读)                            │ ││
│  │  │  - service_id: 0xF001~0xF3FE, 0xFFFF(紧急广播)                │ ││
│  │  │  - 槽位 1023: 所有进程都订阅，实现广播双向互通                 │ ││
│  │  └─────────────────────────────────────────────────────────────────┘ ││
│  │  - 自动清理: 最后一个进程关闭时释放                                   ││
│  │  - 广播互通: 所有进程订阅两个 Registry 的槽位 1023                    ││
│  └──────────────────────────────────────────────────────────────────────┘│
│  ┌──────────────────────────────────────────────────────────────────────┐│
│  │ 1GB HugePage 支持 (THP + Explicit)                                    ││
│  │  - mmap(MAP_HUGETLB | MAP_HUGE_1GB)                                   ││
│  │  - 减少 TLB miss，提升缓存命中率                                      ││
│  │  - 对齐到 1GB 边界 (内核自动对齐)                                     ││
│  └──────────────────────────────────────────────────────────────────────┘│
│  ┌──────────────────────────────────────────────────────────────────────┐│
│  │ Guard Page 保护 (防止越界访问)                                        ││
│  │  - 每个槽位前后插入不可访问的页 (PROT_NONE)                           ││
│  │  - 越界访问触发 SIGSEGV (立即捕获)                                    ││
│  └──────────────────────────────────────────────────────────────────────┘│
│  ┌──────────────────────────────────────────────────────────────────────┐│
│  │ 双注册表权限隔离 (FuSa ISO 26262 就绪)                                ││
│  │  - QM+AB Registry (0666): QM/ASIL-A/B 服务（感知/规划/娱乐等）        ││
│  │  - ASIL-CD Registry (0640): ASIL-C/D 服务（刹车/转向/驱动力等）       ││
│  │  - 物理隔离: 两个独立 memfd，内核级别进程隔离                         ││
│  │  - CAP_SYS_ADMIN: ASIL-CD Registry 写入需要特权                       ││
│  └──────────────────────────────────────────────────────────────────────┘│
└──────────────────────────────────────────────────────────────────────────┘

         ❌ 无 RouDi (iceoryx v1 守护进程)
         ❌ 无 Discovery Server (DDS 守护进程)  
         ❌ 无任何中央守护进程
         ✅ 完全去中心化
         ✅ 零单点故障
```

**关键设计优势对比**：

| 特性 | 传统方案 | 本架构 (v3.0) |
|------|----------|---------------|
| **延迟** | 1-5µs (Domain Socket) | < 500ns (共享内存直接读取) |
| **守护进程** | RouDi / systemd-resolved | ❌ 零 Daemon |
| **单点故障** | ✅ 存在 | ❌ 无 |
| **确定性** | 中 (网络抖动) | ✅ 高 (lock-free + 固定槽位) |
| **启动依赖** | 需先启动 Daemon | ✅ 无依赖 |
| **FuSa 友好** | 需隔离 Daemon | ✅ 天然隔离 (进程级权限) |
| **代码复杂度** | 高 (>2000 行) | ✅ 低 (~300 行) |

### 1.5 服务发现策略（双层策略，v3.0 简化版）

**策略优先级** (静态配置 → 共享内存注册表):

1. **Layer 1: 静态配置** (0ms，编译期确定):
   - 从 `slot_mapping.yaml` 加载服务 ID → 槽位映射
   - 启动时立即可用，零延迟
   - 符合 AUTOSAR SWS_CM_02201 静态服务连接

2. **Layer 2: 共享内存注册表** (< 500ns，运行时动态):
   - 直接读取 iceoryx2 共享内存槽位
   - Lock-free seqlock 保证原子性
   - 心跳机制确保服务活性
   - **无需任何网络/IPC 通信**

**性能基准**：

```text
v3.0 双层策略:
  静态配置 (0ns，编译期确定) → 共享内存查询 (< 500ns，运行时)
  
延迟分布:
  - P50: 300ns
  - P99: 450ns
  - P99.9: 500ns
```

---

## 第 2 章: 零 Daemon 固定槽位自注册架构

### 2.1 核心设计原理

#### 2.1.1 固定槽位映射

**设计思想**：

服务实例到共享内存槽位的映射关系在编译期或启动时确定，运行时不变，确保：

- ✅ **零查找开销**: 直接索引到槽位，O(1) 复杂度
- ✅ **确定性**: 没有哈希冲突、没有动态分配的不确定性
- ✅ **缓存友好**: 连续内存布局，预取优化

**映射策略**：

```text
ServiceID + InstanceID → SlotIndex

策略 1: 静态配置 (YAML manifest)
  RadarService.Instance1 → Slot[10]
  CameraService.Instance1 → Slot[11]
  ...

策略 2: 直接位运算 (零冲突设计)
  SlotIndex = ServiceID & 1023  // 取低 10 位，天然映射到 0~1023
  - QM 服务 (0x0001~0x03FF): 低 10 位直接就是槽位
  - ASIL 服务 (0xF000~0xF3FF): 低 10 位同样直接映射
  - 广播 (0xFFFF): 低 10 位 = 1023，完美映射到广播槽
  - 零冲突: service_id 高位段已隔离，低 10 位唯一
```

**Instance ID 位域结构** (用于 `instance_id` 字段的低 32 位):

```cpp
// Instance ID 编码结构 (低 32 位，高 32 位预留)
struct alignas(4) InstanceId {
    uint32_t service_id      : 16;   // 0~65535，全车唯一（ara::com 生成）
    uint32_t instance_no     : 8;    // 0~255，同服务多实例
    uint32_t domain          : 4;    // 0~15（0=感知,1=控制,2=娱乐,3=诊断,4=平台,5=OEM...）
    uint32_t asil_level      : 3;    // 0=QM,1=A,2=B,3=C,4=D,5~7保留
    uint32_t redundancy      : 1;    // 0=主通道,1=备通道（ASIL 冗余专用，主ASIL-D服务）
    // 总 16+8+4+3+1 = 32 bit，完美填满
};
static_assert(sizeof(InstanceId) == 4);

// 域分类定义
enum class ServiceDomain : uint8_t {
    PERCEPTION   = 0,  // 感知域（摄像头、雷达、激光雷达）
    CONTROL      = 1,  // 控制域（转向、制动、动力）
    INFOTAINMENT = 2,  // 娱乐域（HMI、音频、导航）
    DIAGNOSTICS  = 3,  // 诊断域（OBD、DTC、日志）
    PLATFORM     = 4,  // 平台域（时间同步、健康管理）
    OEM_RESERVED = 5,  // OEM 自定义域
    // 6~15 预留
};

// ASIL 等级定义
enum class ASILLevel : uint8_t {
    QM    = 0,  // Quality Management (非安全相关)
    ASIL_A = 1,
    ASIL_B = 2,
    ASIL_C = 3,
    ASIL_D = 4,
    // 5~7 预留
};

// Instance ID 工具函数
inline uint64_t EncodeInstanceId(uint16_t service_id, uint8_t instance_no, 
                                   ServiceDomain domain, ASILLevel asil, bool is_redundant) {
    InstanceId id{};
    id.service_id = service_id;
    id.instance_no = instance_no;
    id.domain = static_cast<uint32_t>(domain);
    id.asil_level = static_cast<uint32_t>(asil);
    id.redundancy = is_redundant ? 1 : 0;
    
    uint32_t low32 = *reinterpret_cast<uint32_t*>(&id);
    return static_cast<uint64_t>(low32);  // 高 32 位为 0（预留）
}

inline InstanceId DecodeInstanceId(uint64_t instance_id_64) {
    uint32_t low32 = static_cast<uint32_t>(instance_id_64 & 0xFFFFFFFF);
    return *reinterpret_cast<InstanceId*>(&low32);
}
```

**双注册表槽位划分方案**：

架构使用两个独立的共享内存注册表，实现安全等级分组隔离：

- **QM Registry**: QM + ASIL-A/B 服务（安全增强，共享注册表）
- **ASIL Registry**: ASIL-C/D 服务（物理隔离，高安全等级）

| Registry 类型 | 槽位范围 | 槽位数量 | 用途 | service_id 范围 | 典型服务示例 |
|--------------|---------|---------|------|----------------|-------------|
| **QM Registry** | **0** | **1** | **保留槽（禁止使用）** | **0x0000** | **NULL 槽位，用于错误检测** |
| QM Registry | **1** | **1** | **SD-Proxy 主实例** | **0x0001** | **跨 ECU 服务发现代理（主）** |
| QM Registry | 2~511 | 510 | 感知主力区（QM/ASIL-A/B） | 0x0002~0x01FF | 摄像头、激光雷达、毫米波原始+融合 |
| QM Registry | **512** | **1** | **SD-Proxy 备份实例** | **0x0200** | **跨 ECU 服务发现代理（备）** |
| QM Registry | 513~767 | 255 | 规划/决策区（QM/ASIL-B） | 0x0201~0x02FF | 路径规划、行为决策、预测 |
| QM Registry | 768~863 | 96 | 控制/底盘（ASIL-A/B） | 0x0300~0x035F | 灯光、雨刮、空调、辅助转向 |
| QM Registry | 864~959 | 96 | 车身/舒适/娱乐（QM） | 0x0360~0x03BF | 车窗、座椅、多屏、语音、娱乐 |
| QM Registry | 960~1022 | 63 | 平台中间件（QM/ASIL-A） | 0x03C0~0x03FE | 非关键诊断、日志、时间同步、OTA准备 |
| QM Registry | **1023** | **1** | **全局广播槽** | **0xFFFF** | **OTA 开始、休眠、故障广播** |
| **ASIL Registry** | **0** | **1** | **保留槽（禁止使用）** | **0xF000** | **NULL 槽位，用于错误检测** |
| ASIL Registry | 1~511 | 511 | ASIL-D 关键控制区 | 0xF001~0xF1FF | 主刹车、转向、驱动力、电源管理 |
| ASIL Registry | 512~767 | 256 | ASIL-D 冗余控制区 | 0xF200~0xF2FF | 备份刹车、转向、驱动力 |
| ASIL Registry | 768~895 | 128 | ASIL-C 中间件区 | 0xF300~0xF37F | 关键诊断、网关、时间主节点 |
| ASIL Registry | 896~991 | 96 | 平台中间件（ASIL-C/D） | 0xF380~0xF3DF | 关键 OTA、安全监视、故障管理 |
| ASIL Registry | 992~1015 | 24 | 系统保留（ASIL-C/D） | 0xF3E0~0xF3F7 | 安全岛心跳、紧急降级通道 |
| ASIL Registry | 1016~1022 | 7 | OEM 专属保留段 | 0xF3F8~0xF3FE | 整车厂自定义 |
| ASIL Registry | **1023** | **1** | **紧急广播槽** | **0xFFFF** | **紧急制动请求、安全岛重启** |

**双注册表设计特点**：

1. ✅ **安全等级分组隔离**: 两个独立 memfd，内核级别进程隔离
   - **QM Registry**: QM + ASIL-A/B 服务（安全增强，共享优化）
   - **ASIL Registry**: ASIL-C/D 服务（物理隔离，高安全等级）
2. ✅ **权限管控**:
   - QM Registry: 0666 (所有进程可读写)
   - ASIL Registry: 0640 (控制进程写，其他只读)
3. ✅ **service_id 段分离**:
   - QM 服务: 0x0001~0x03FE (1~1022) - 禁止使用 0x0000
     - 0x0001: SD-Proxy 主实例（Slot 1）
     - 0x0200: SD-Proxy 备份实例（Slot 512）
   - ASIL 服务: 0xF001~0xF3FE (61441~62462) - 禁止使用 0xF000 和 0xF3FF
   - 广播槽: 0xFFFF (65535)
4. ✅ **槽位 0 保留**: 两个 Registry 的槽位 0 都禁止使用
   - 用于 NULL 检测和错误边界条件
   - service_id 0x0000 和 0xF000 映射到槽位 0，运行时拒绝注册
   - 提升系统鲁棒性，防止未初始化的 service_id 污染注册表
5. ✅ **冗余支持**: ASIL Registry 内含主备控制区（511+256槽）
6. ✅ **全局广播**: 每个 Registry 的槽位 1023 为广播槽，支持系统级事件
7. ✅ **槽位 1023 双向互通**:
   - 所有进程订阅: `lap_com_registry_qm[1023]` + `lap_com_registry_asil[1023]`
   - 任一 Registry 的 1023 更新，所有进程都能收到广播事件
   - 实现跨安全等级的系统级事件通知（OTA、休眠、紧急制动等）
   - QM+AB ↔ ASIL-CD 双向广播互通
8. ✅ **零冲突槽位映射**: `slot = service_id & 1023`（低 10 位直接映射）
   - QM+AB 服务 0x0001~0x0417 → 槽位 1~1047（槽位 0 禁止，槽位 1023 广播）
   - ASIL-CD 服务 0xF001~0xF3FE → 槽位 1~1022（槽位 0 禁止，槽位 1023 广播）
   - 广播 0xFFFF → 槽位 1023（0xFFFF & 1023 = 1023）
   - 保留 0x0000/0xF000 → 槽位 0（禁止使用，用于错误检测）

**service_id 快速查找算法**：

```cpp
// 根据 service_id 自动选择 Registry
RegistryType SelectRegistry(uint16_t service_id) {
    if (service_id >= 0xF000 && service_id <= 0xF3FF) {
        return RegistryType::ASIL_CD;  // ASIL-C/D 高安全等级服务段
    } else if (service_id == 0xFFFF) {
        // 广播服务：需同时写入两个 Registry 的槽位 1023
        // 注意：所有进程都订阅两个 Registry 的 1023，实现双向互通
        return RegistryType::BOTH;
    } else {
        return RegistryType::QM_AB;  // QM/ASIL-A/B 低/中等安全等级服务段
    }
}

// 计算槽位索引（零冲突设计，槽位 0 保留禁止使用）
inline uint32_t CalculateSlot(uint16_t service_id) {
    uint32_t slot = service_id & 1023;  // 取低 10 位，天然映射到 0~1023
    
    // 槽位 0 保留，禁止使用（0x0000 和 0xF000 会映射到槽位 0）
    if (slot == 0) {
        // 运行时错误：service_id 0x0000 或 0xF000 非法
        // 应抛出异常或返回 INVALID_SLOT
        return INVALID_SLOT;  // 或 throw std::invalid_argument("slot 0 reserved");
    }
    
    return slot;
}

// 根据 service_id 推荐槽位范围（槽位 0 保留禁止使用）
std::pair<uint32_t, uint32_t> GetSlotRange(uint16_t service_id) {
    if (service_id >= 0x0001 && service_id <= 0x01FF) {
        return {1, 511};     // 感知主力区 (QM/ASIL-A/B)（槽位 0 跳过）
    } else if (service_id >= 0x0200 && service_id <= 0x02FF) {
        return {512, 767};   // 规划/决策区 (QM/ASIL-B)
    } else if (service_id >= 0x0300 && service_id <= 0x035F) {
        return {768, 863};   // 控制/底盘 (ASIL-A/B)
    } else if (service_id >= 0x0360 && service_id <= 0x03DF) {
        return {864, 991};   // 车身/舒适/娱乐 (QM)
    } else if (service_id >= 0x03E0 && service_id <= 0x0417) {
        return {992, 1047};  // 平台中间件 (QM/ASIL-A)
    } else if (service_id >= 0xF001 && service_id <= 0xF1FF) {
        return {1, 511};     // ASIL-D 关键控制区（槽位 0 跳过）
    } else if (service_id >= 0xF200 && service_id <= 0xF2FF) {
        return {512, 767};   // ASIL-D 冗余控制区
    } else if (service_id >= 0xF300 && service_id <= 0xF37F) {
        return {768, 895};   // ASIL-C 中间件区
    } else if (service_id >= 0xF380 && service_id <= 0xF3DF) {
        return {896, 991};   // 平台中间件 (ASIL-C/D)
    } else if (service_id == 0xFFFF) {
        return {1023, 1023}; // 全局广播槽
    } else if (service_id == 0x0000 || service_id == 0xF000) {
        return {0, 0};       // 错误：槽位 0 保留禁止使用
    }
    return {1, 1022};  // 默认范围（跳过槽位 0 和 1023）
}
```

**槽位结构定义** (256 bytes per slot):

```cpp
// ServiceSlot 结构 - 每个服务实例在注册表中占用一个槽位
struct alignas(64) ServiceSlot {  // 64字节对齐，匹配 CPU Cache Line
    
    // ========== 同步控制字段 (8 bytes) ==========
    std::atomic<uint64_t> sequence;  ///< [0-7] seqlock 序列号：奇数=写入中，偶数=可读
    
    // ========== 服务标识字段 (32 bytes) ==========
    uint64_t service_id;             ///< [8-15] 服务接口 ID (AUTOSAR Service Interface)
    uint64_t instance_id;            ///< [16-23] 实例 ID（低32位=InstanceId位域，高32位预留）
                                     ///< 低32位格式: service_id(16) | instance_no(8) | domain(4) | asil_level(3) | redundancy(1)
    uint32_t major_version;          ///< [24-27] 主版本号 (SWS_CM_00300)
    uint32_t minor_version;          ///< [28-31] 次版本号 (SWS_CM_00300)
    
    // ========== iceoryx2 零拷贝端点信息 (32 bytes) ==========
    uint64_t endpoint_offset;        ///< [32-39] iceoryx2 服务在共享内存的 chunk offset
                                     ///<   用途: 直接定位到 Publisher/Subscriber 的元数据区
                                     ///<   零拷贝！避免端点字符串拷贝和解析
    uint64_t mempool_base_ptr;       ///< [40-47] iceoryx2 mempool 基址（虚拟地址）
                                     ///<   用途: 跨进程验证，防止地址空间冲突
                                     ///<   注: 仅用于调试/验证，实际访问使用相对 offset
    uint32_t qos_profile;            ///< [48-51] QoS 配置标志位（iceoryx2 专用）
                                     ///<   bit[0-7]:   reliability (0=best_effort, 1=reliable)
                                     ///<   bit[8-15]:  history depth (0-255)
                                     ///<   bit[16-23]: max_subscribers (0=unlimited, 1-255=limit)
                                     ///<   bit[24-31]: 预留
    uint32_t subscriber_count;       ///< [52-55] 当前订阅者数量（实时统计）
                                     ///<   用途: 监控/调试，判断服务是否有消费者
                                     ///<   更新: Publisher 定期通过 iceoryx2 API 查询并更新
    
    // ========== 通用网络端点信息 (64 bytes) ==========
    char binding_type[16];           ///< [56-71] 绑定类型: "iceoryx2", "someip", "dds"
    char endpoint[48];               ///< [72-119] 备用端点地址（仅 SOME/IP/DDS 使用）
                                     ///<   - iceoryx2: 留空（使用 endpoint_offset）
                                     ///<   - SOME/IP: "192.168.1.100:30500"
                                     ///<   - DDS: "topic_name"
    
    // ========== 生命周期管理 (24 bytes) ==========
    uint64_t last_heartbeat_ns;      ///< [120-127] 最后心跳时间戳（纳秒，CLOCK_MONOTONIC）
    uint32_t heartbeat_interval_ms;  ///< [128-131] 心跳间隔（毫秒，默认1000ms）
    uint32_t status;                 ///< [132-135] 槽位状态：0=空闲, 1=活跃, 2=正在注销
    pid_t owner_pid;                 ///< [136-139] 拥有者进程 ID（用于 kill(pid, 0) 检测）
    uint32_t _reserved1;             ///< [140-143] 保留字段（对齐填充）
    
    // ========== 元数据扩展 (96 bytes) ==========
    char metadata[96];               ///< [144-239] JSON格式的扩展元数据
                                     ///< 示例: {"description":"Front Radar","sample_rate_hz":20}
    
    // ========== 安全增强字段 (8 bytes) ==========
    uint32_t crc32;                  ///< [240-243] CRC32 校验和（可选，ASIL-C/D服务启用）
    uint32_t write_counter;          ///< [244-247] 写入计数器（防重放攻击）
    
    // ========== 填充到 256 bytes ==========
    uint8_t _padding[8];             ///< [248-255] 填充到 256 字节 (4 个 Cache Lines)
    
    // 总大小: 256 bytes = 4 × 64-byte Cache Lines
    // 内存布局优化：
    //   - 热数据 (sequence, service_id, instance_id): 第1个 Cache Line (0-63)
    //   - iceoryx2 端点 (endpoint_offset, mempool_base_ptr, qos): 第2个 Cache Line (64-127)
    //   - 生命周期 (heartbeat, status, owner_pid): 第2个 Cache Line 尾部 (120-143)
    //   - 冷数据 (metadata, crc32, padding): 第3-4个 Cache Line (144-255)
};
static_assert(sizeof(ServiceSlot) == 256, "ServiceSlot must be 256 bytes");
static_assert(alignof(ServiceSlot) == 64, "ServiceSlot must be 64-byte aligned");
```

**iceoryx2 Binding 专用字段说明**：

1. ✅ **endpoint_offset** (零拷贝核心)
   - **作用**: 直接指向 iceoryx2 服务在共享内存的元数据位置
   - **优势**: 避免 endpoint 字符串解析，O(1) 直接访问
   - **示例**: `offset = 0x1A2B3C` → 直接 mmap 到该地址读取 Publisher 配置

2. ✅ **mempool_base_ptr** (安全验证)
   - **作用**: 记录 iceoryx2 mempool 的虚拟基址
   - **用途**: 跨进程验证地址空间一致性，防止错误访问
   - **注意**: 仅用于验证，实际访问使用 `base + endpoint_offset`

3. ✅ **qos_profile** (QoS 配置位图)
   - **bit[0-7]**: reliability
     - `0`: best_effort（无重传，低延迟）
     - `1`: reliable（保证送达，适合控制指令）
   - **bit[8-15]**: history_depth
     - 保留最近 N 个样本（0-255）
   - **bit[16-23]**: max_subscribers
     - 订阅者数量上限（0=不限制）
   - **bit[24-31]**: 预留扩展

4. ✅ **subscriber_count** (运行时监控)
   - **作用**: 实时统计当前订阅者数量
   - **更新**: Publisher 每次心跳时通过 iceoryx2 API 更新
   - **用途**:
     - 判断服务是否被消费（0 = 无订阅者，可能浪费资源）
     - 调试服务发现问题
     - 监控服务健康度

**结构设计亮点**：

1. ✅ **Cache Line 对齐**: 64字节对齐，避免 False Sharing
2. ✅ **固定大小**: 256字节，方便槽位地址计算 (`slot_addr = base + index * 256`)
3. ✅ **热数据前置**: 高频访问字段放在前面，提升 Cache 命中率
4. ✅ **iceoryx2 优化**: endpoint_offset 实现零拷贝服务发现
5. ✅ **seqlock 无锁**: 读者无锁并发访问，零竞争
6. ✅ **安全增强**: CRC32 + write_counter 支持 ASIL-C/D 级别安全需求
7. ✅ **灵活扩展**: metadata 字段支持 JSON 格式自定义元数据

#### 2.1.2 seqlock 无锁同步

**原理**：

seqlock (Sequential Lock) 是一种读者友好的同步机制：

- **写者**: 独占访问，更新 sequence (奇数 → 偶数)
- **读者**: 无锁重试，检查 sequence 前后一致性

**写入流程** (OfferService):

```cpp
void WriteSlot(uint32_t slot_index, const ServiceInfo& info) {
    auto& slot = slots_[slot_index];
    
    // 1. 加锁：sequence += 1 (变为奇数)
    uint64_t seq = slot.sequence.fetch_add(1, std::memory_order_acquire);
    
    // 2. 写入数据
    slot.service_id = info.service_id;
    slot.instance_id = info.instance_id;
    slot.major_version = info.major_version;
    // ... 写入其他字段
    
    std::atomic_thread_fence(std::memory_order_release);
    
    // 3. 解锁：sequence += 1 (变为偶数)
    slot.sequence.fetch_add(1, std::memory_order_release);
}
```

**读取流程** (FindService):

```cpp
std::optional<ServiceInfo> ReadSlot(uint32_t slot_index) {
    auto& slot = slots_[slot_index];
    ServiceInfo info;
    
    uint64_t seq1, seq2;
    do {
        // 1. 读取 sequence (必须是偶数)
        seq1 = slot.sequence.load(std::memory_order_acquire);
        if (seq1 & 1) {
            // 写者正在写入，稍后重试
            _mm_pause();  // x86 PAUSE 指令，减少总线争用
            continue;
        }
        
        // 2. 读取数据（无锁）
        info.service_id = slot.service_id;
        info.instance_id = slot.instance_id;
        // ... 读取其他字段
        
        std::atomic_thread_fence(std::memory_order_acquire);
        
        // 3. 再次检查 sequence
        seq2 = slot.sequence.load(std::memory_order_acquire);
        
    } while (seq1 != seq2);  // 不一致则重试
    
    return info;
}
```

**性能优势**：

- ✅ **读者零锁竞争**: 多个进程可并发读取，无互斥锁
- ✅ **写者低开销**: 只需两次原子操作（fetch_add）
- ✅ **无系统调用**: 完全用户态操作
- ✅ **延迟 < 100ns**: 单次读取 (假设无冲突)

#### 2.1.3 心跳机制

**设计目标**：

检测服务提供者进程的存活状态，自动清理僵尸槽位。

**实现方案**：

```cpp
class HeartbeatMonitor {
public:
    // 启动心跳发送线程
    void StartHeartbeat(uint32_t slot_index, uint32_t interval_ms) {
        heartbeat_threads_[slot_index] = std::thread([=]() {
            while (running_) {
                auto now_ns = std::chrono::steady_clock::now()
                    .time_since_epoch().count();
                
                auto& slot = slots_[slot_index];
                slot.last_heartbeat_ns = now_ns;
                
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(interval_ms));
            }
        });
    }
    
    // 后台线程定期检测超时槽位
    void MonitorTimeouts() {
        while (running_) {
            auto now_ns = std::chrono::steady_clock::now()
                .time_since_epoch().count();
            
            for (uint32_t i = 0; i < MAX_SLOTS; ++i) {
                auto& slot = slots_[i];
                if (slot.status != 1) continue;  // 仅检查活跃槽位
                
                uint64_t elapsed_ns = now_ns - slot.last_heartbeat_ns;
                uint64_t timeout_ns = slot.heartbeat_interval_ms * 1'000'000ULL * 3;
                
                if (elapsed_ns > timeout_ns) {
                    // 超时，清理槽位
                    ClearSlot(i);
                }
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
};
```

**替代方案** (更轻量):

使用 `kill(pid, 0)` 系统调用检测进程存活：

```cpp
bool IsProcessAlive(pid_t pid) {
    return (kill(pid, 0) == 0) || (errno == EPERM);
}

void ClearDeadSlots() {
    for (uint32_t i = 0; i < MAX_SLOTS; ++i) {
        auto& slot = slots_[i];
        if (slot.status == 1 && !IsProcessAlive(slot.owner_pid)) {
            ClearSlot(i);
        }
    }
}
```

**优势对比**：

| 方案 | 延迟检测 | CPU 开销 | 实现复杂度 |
|------|---------|---------|-----------|
| 心跳时间戳 | 高 (最多 3× interval) | 低 (纳秒级写入) | 中 |
| kill(0) 检测 | 低 (实时) | 中 (系统调用) | 低 |
| **混合方案** | 低 | 低 | 低 |

**推荐**: 混合方案（心跳作为快速路径，kill(0) 作为兜底检测）

#### 2.1.4 Instance ID 使用示例

##### 场景 1: 创建感知域雷达服务实例（ASIL-B 主通道）

```cpp
// 使用 InstanceId 位域结构创建实例
uint64_t instance_id = EncodeInstanceId(
    0x1001,                        // service_id: 雷达服务
    1,                             // instance_no: 第1个实例（前向雷达）
    ServiceDomain::PERCEPTION,     // domain: 感知域
    ASILLevel::ASIL_B,            // asil_level: ASIL-B
    false                          // redundancy: 主通道
);
// 结果: 0x0000000010011042 (低32位有效)
//       ^^^^^^^^ ^^^^^^^^
//       高32位   低32位
//       (预留)   (InstanceId位域)

// 解析位域
InstanceId id = DecodeInstanceId(instance_id);
assert(id.service_id == 0x1001);
assert(id.instance_no == 1);
assert(id.domain == 0);  // PERCEPTION
assert(id.asil_level == 2);  // ASIL_B
assert(id.redundancy == 0);  // 主通道
```

##### 场景 2: ASIL 冗余通道配置（ASIL-D 级别服务）

```cpp
// 主通道：转向控制服务（ASIL-D 级别）
uint64_t steering_main = EncodeInstanceId(
    0x2001,                        // service_id: 转向服务
    1,                             // instance_no: 第1个实例
    ServiceDomain::CONTROL,        // domain: 控制域
    ASILLevel::ASIL_D,            // asil_level: ASIL-D
    false                          // redundancy: 主通道
);

// 备通道：转向控制服务（ASIL-D 冗余）
uint64_t steering_backup = EncodeInstanceId(
    0x2001,                        // service_id: 相同服务ID
    1,                             // instance_no: 相同实例号
    ServiceDomain::CONTROL,        // domain: 控制域
    ASILLevel::ASIL_D,            // asil_level: ASIL-D
    true                           // redundancy: 备通道 ✅
);

// 两个通道的 instance_id 仅 redundancy bit 不同
assert((steering_main & 0xFFFFFFFE) == (steering_backup & 0xFFFFFFFE));
assert((steering_backup & 0x1) == 1);  // 备通道标志位

// 注：主备通道都注册到 ASIL-CD Registry，通过 redundancy bit 区分
```

#### 场景 3: 槽位映射规则 (YAML 配置)

```yaml
# slot_mapping.yaml
slot_mapping:
  # QM+AB Registry: QM/ASIL-A/B 服务
  - service_id: 0x1001
    instance_no: 1
    domain: PERCEPTION  # 0
    asil_level: ASIL_B  # 2
    redundancy: false
    slot_index: 10      # 静态分配
    registry: qm_ab     # 指定注册表
    
  # ASIL-CD Registry: ASIL-C/D 服务
  - service_id: 0xF001  # ASIL-CD 服务段
    instance_no: 1
    domain: CONTROL     # 1
    asil_level: ASIL_D  # 4
    redundancy: false   # 主通道
    slot_index: 100     # ASIL-CD Registry 槽位
    registry: asil_cd   # 指定注册表
    
  - service_id: 0x2001
    instance_no: 1
    domain: CONTROL
    asil_level: ASIL_D
    redundancy: true    # 备通道
    slot_index: 925     # 备通道槽位
```

##### 场景 4: 运行时槽位查找优化（零冲突设计 + 槽位 0 保护）

```cpp
// 快速查找：根据 InstanceId 位域直接定位槽位（O(1) 复杂度，零冲突）
uint32_t FindSlotByInstanceId(uint64_t instance_id_64) {
    InstanceId id = DecodeInstanceId(instance_id_64);
    
    // 直接位运算计算槽位（零冲突）
    uint32_t slot_index = id.service_id & 1023;  // 取低 10 位
    
    // 槽位 0 保护：禁止使用（service_id 0x0000 或 0xF000 非法）
    if (slot_index == 0) {
        LOG_ERROR("Slot 0 is reserved, service_id={:#06x} is invalid", id.service_id);
        return INVALID_SLOT;
    }
    
    // 根据 ASIL 等级选择 Registry
    ServiceSlot* slot = nullptr;
    if (id.asil_level == static_cast<uint32_t>(ASILLevel::ASIL_D)) {
        slot = &asil_registry_->slots[slot_index];  // ASIL-D Registry
    } else {
        slot = &qm_registry_->slots[slot_index];    // QM Registry
    }
    
    // 验证槽位数据（无需遍历，直接验证）
    if (slot->instance_id == instance_id_64 && slot->status == 1) {
        return slot_index;  // 找到，O(1) 复杂度
    }
    
    return INVALID_SLOT;  // 槽位为空或不匹配
}

// 槽位 1023 双向互通订阅（所有进程都监听两个 Registry 的广播槽）
void SubscribeBroadcastSlots() {
    // 订阅 QM Registry 的槽位 1023
    qm_broadcast_slot_ = &qm_registry_->slots[1023];
    
    // 订阅 ASIL Registry 的槽位 1023
    asil_broadcast_slot_ = &asil_registry_->slots[1023];
    
    // 后台线程轮询两个广播槽（seqlock 检测更新）
    // QM 和 ASIL 进程都能收到双向广播事件
}

// OfferService 槽位 0 保护示例
bool OfferService(const ServiceInfo& info) {
    uint32_t slot = CalculateSlot(info.service_id);
    
    // 槽位 0 保护检查
    if (slot == 0 || slot == INVALID_SLOT) {
        LOG_ERROR("Cannot offer service: slot 0 is reserved (service_id={:#06x})", 
                  info.service_id);
        return false;
    }
    
    // 正常注册流程...
    return RegisterToSlot(slot, info);
}
```

**位域结构优势总结**：

1. ✅ **紧凑编码**: 32 位完美填满，无浪费
2. ✅ **快速解析**: 位操作提取字段，O(1) 复杂度
3. ✅ **域隔离**: 根据 `domain` + `asil_level` 自动分配槽位范围
4. ✅ **冗余支持**: `redundancy` 位实现 ASIL 主备通道（主要用于ASIL-D服务）
5. ✅ **可扩展**: 高 32 位预留，未来可扩展其他元数据

### 2.2 systemd Socket Activation 集成 (零常驻 Daemon)

#### 2.2.1 设计原理

**传统方案的问题**：

即使使用 memfd，仍需第一个进程创建并持有文件描述符，后续进程如何获取同一 memfd？

- ❌ 方案1: 持有进程常驻 → 引入单点故障
- ❌ 方案2: 通过 `/proc/[pid]/fd/` 传递 → 依赖特定进程存活
- ❌ 方案3: 共享 memfd 名称 → memfd 本身是匿名的

**本架构创新方案**：

使用 **systemd socket activation + oneshot service** 实现真正的零常驻 Daemon：

```text
系统启动 (systemd)
    ↓
启动双 socket 监听:
  - /run/lap/registry_qm.sock (QM Registry 服务)
  - /run/lap/registry_asil.sock (ASIL Registry 服务)
    ↓
首次客户端连接 → 触发对应 oneshot service:
  - lap-registry-qm-init.service (QM 注册表初始化)
  - lap-registry-asil-init.service (ASIL 注册表初始化)
    ↓
oneshot 进程:
  1. memfd_create() 创建匿名共享内存文件
  2. 初始化注册表（256KB，1024 slots）
  3. 通过 Unix Domain Socket + SCM_RIGHTS 传递文件描述符
    ↓
客户端接收 memfd fd，mmap 映射到进程地址空间
    ↓
oneshot 进程退出 (无常驻 Daemon)
    ↓
后续所有客户端连接同一 socket，获取同一 memfd fd
```

**核心优势**：

- ✅ **零常驻 Daemon**: oneshot 进程完成初始化后立即退出
- ✅ **100% 共享**: 所有进程通过 socket 获取同一 memfd fd
- ✅ **自动恢复**: 进程重启/新增自动重连 socket
- ✅ **热升级**: 无需停止任何服务
- ✅ **systemd 原生**: 无额外守护进程管理

#### 2.2.2 memfd_create + SCM_RIGHTS 文件描述符传递

> **实现状态**: ✅ Phase 1 完成 (2025-11-20)  
> - memfd_create 匿名共享内存已实现  
> - 内存封闭 (sealing) 已启用  
> - 15/15 测试全部通过 (FindService P99=232ns)  
> - 无 /dev/shm 文件系统污染  
> - Phase 2 (UDS FD传递) 待实现  
> 详见: `IMPLEMENTATION_STATUS.md`

##### Step 1: oneshot 进程创建 memfd

```cpp
// lap-registry-init.cpp (oneshot 初始化进程)

#include <sys/memfd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>

struct RegistryInitializer {
    int memfd_ = -1;
    void* registry_ptr_ = nullptr;
    
    int CreateAndInitializeRegistry() {
        // 1. 创建匿名共享内存文件（双注册表设计：lap_com_registry_qm / lap_com_registry_asil）
        memfd_ = memfd_create("lap_com_registry_qm", 
                              MFD_CLOEXEC |      // fork 后自动关闭
                              MFD_ALLOW_SEALING); // 允许密封
        if (memfd_ < 0) {
            return -1;
        }
        
        // 2. 设置大小 (256KB = 1024 slots × 256 bytes)
        const size_t registry_size = 256 * 1024;
        if (ftruncate(memfd_, registry_size) < 0) {
            close(memfd_);
            return -1;
        }
        
        // 3. 映射到进程地址空间
        registry_ptr_ = mmap(nullptr, registry_size,
                            PROT_READ | PROT_WRITE,
                            MAP_SHARED,
                            memfd_, 0);
        if (registry_ptr_ == MAP_FAILED) {
            close(memfd_);
            return -1;
        }
        
        // 4. 初始化注册表 (清零所有槽位)
        memset(registry_ptr_, 0, registry_size);
        
        // 5. 密封内存，防止后续 resize
        fcntl(memfd_, F_ADD_SEALS, 
              F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL);
        
        return 0;
    }
    
    // 通过 Unix Domain Socket 发送 memfd 文件描述符
    int SendMemfdToClient(int client_socket) {
        struct msghdr msg = {};
        struct iovec iov = {};
        char ctrl_buf[CMSG_SPACE(sizeof(int))];
        
        // 1. 构造辅助数据 (SCM_RIGHTS)
        msg.msg_control = ctrl_buf;
        msg.msg_controllen = sizeof(ctrl_buf);
        
        struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cmsg), &memfd_, sizeof(int));
        
        // 2. 发送一个字节数据 (触发消息传递)
        char byte = 'R';  // 'R' for Registry
        iov.iov_base = &byte;
        iov.iov_len = 1;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        
        // 3. 发送消息 (包含 memfd 文件描述符)
        ssize_t sent = sendmsg(client_socket, &msg, 0);
        return (sent > 0) ? 0 : -1;
    }
};

// oneshot 进程主函数
int main() {
    // 1. 从 systemd 获取监听 socket (LISTEN_FDS=1)
    int listen_fd = SD_LISTEN_FDS_START;  // systemd 传入的 socket fd
    
    // 2. 创建并初始化 memfd
    RegistryInitializer initializer;
    if (initializer.CreateAndInitializeRegistry() < 0) {
        return 1;
    }
    
    // 3. 接受客户端连接，发送 memfd fd
    while (true) {
        int client_fd = accept(listen_fd, nullptr, nullptr);
        if (client_fd < 0) break;
        
        initializer.SendMemfdToClient(client_fd);
        close(client_fd);
        
        // 4. 处理完一批连接后退出 (systemd 会在需要时重启)
        // 实际可优化为处理所有排队连接后退出
    }
    
    return 0;
}
```

##### Step 2: 客户端接收 memfd fd

```cpp
// SharedMemoryRegistry.cpp (客户端库)

class RegistryClient {
public:
    int ConnectAndReceiveMemfd(bool use_asil_registry = false) {
        // 1. 连接到 systemd socket（根据需要选择 QM 或 ASIL）
        int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        const char* socket_path = use_asil_registry 
            ? "/run/lap/registry_asil.sock"  // ASIL Registry
            : "/run/lap/registry_qm.sock";    // QM Registry
        strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
        
        if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            return -1;
        }
        
        // 2. 接收 memfd 文件描述符 (SCM_RIGHTS)
        struct msghdr msg = {};
        struct iovec iov = {};
        char ctrl_buf[CMSG_SPACE(sizeof(int))];
        char byte;
        
        iov.iov_base = &byte;
        iov.iov_len = 1;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = ctrl_buf;
        msg.msg_controllen = sizeof(ctrl_buf);
        
        ssize_t received = recvmsg(sock_fd, &msg, 0);
        close(sock_fd);
        
        if (received <= 0) {
            return -1;
        }
        
        // 3. 提取 memfd 文件描述符
        struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
        if (!cmsg || cmsg->cmsg_type != SCM_RIGHTS) {
            return -1;
        }
        
        int memfd;
        memcpy(&memfd, CMSG_DATA(cmsg), sizeof(int));
        
        // 4. 映射到进程地址空间
        void* registry_ptr = mmap(nullptr, 256 * 1024,
                                  PROT_READ | PROT_WRITE,
                                  MAP_SHARED,
                                  memfd, 0);
        if (registry_ptr == MAP_FAILED) {
            close(memfd);
            return -1;
        }
        
        // 5. 现在所有进程共享同一块物理内存！
        registry_ptr_ = static_cast<ServiceSlot*>(registry_ptr);
        memfd_ = memfd;
        
        return 0;
    }
    
private:
    ServiceSlot* registry_ptr_ = nullptr;
    int memfd_ = -1;
};
```

**关键机制**：

- ✅ **SCM_RIGHTS**: Unix 域套接字特有的文件描述符传递机制
- ✅ **内核保证**: 所有进程接收的 fd 指向同一内核对象（同一块物理内存）
- ✅ **自动引用计数**: 内核管理 memfd 生命周期，最后一个进程关闭时释放

#### 2.2.3 systemd 配置文件

**QM Socket Unit** (`/etc/systemd/system/lap-registry-qm.socket`):

```ini
[Unit]
Description=LightAP QM Service Registry Socket
Documentation=https://lightap.io/docs/service-registry
Before=multi-user.target
PartOf=lap-com.target

[Socket]
# Unix Domain Socket 路径
ListenStream=/run/lap/registry_qm.sock

# Socket 权限 (所有进程可连接)
SocketMode=0666
SocketUser=root
SocketGroup=root

# Socket 激活服务
Service=lap-registry-qm-init.service

# 立即创建 socket (系统启动时)
Backlog=128
Accept=false

[Install]
WantedBy=sockets.target
WantedBy=lap-com.target
```

**ASIL Socket Unit** (`/etc/systemd/system/lap-registry-asil.socket`):

```ini
[Unit]
Description=LightAP ASIL Service Registry Socket
Documentation=https://lightap.io/docs/service-registry
Before=multi-user.target
PartOf=lap-com.target

[Socket]
# Unix Domain Socket 路径
ListenStream=/run/lap/registry_asil.sock

# Socket 权限 (ASIL 组成员可连接)
SocketMode=0660
SocketUser=root
SocketGroup=lap-asil

# Socket 激活服务
Service=lap-registry-asil-init.service

# 立即创建 socket (系统启动时)
Backlog=128
Accept=false

[Install]
WantedBy=sockets.target
```

**Oneshot Service Unit** (`/etc/systemd/system/lap-registry-init.service`):

```ini
[Unit]
Description=LightAP Service Registry Initializer (Oneshot)
Documentation=https://lightap.io/docs/service-registry
Requires=lap-registry.socket
After=lap-registry.socket

[Service]
Type=oneshot
ExecStart=/usr/lib/lap/com/lap-registry-init

# 资源限制
MemoryMax=10M
CPUQuota=10%

# 安全加固
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/run/lap

# 环境变量
Environment="LAP_REGISTRY_SIZE=262144"
Environment="LAP_LOG_LEVEL=INFO"

# 超时控制
TimeoutStartSec=5s

# 进程退出后不重启 (oneshot)
RemainAfterExit=no

[Install]
WantedBy=multi-user.target
```

**安装与启动**：

```bash
# 1. 创建运行时目录
sudo mkdir -p /run/lap
sudo chmod 755 /run/lap

# 2. 安装 systemd 单元文件
sudo cp lap-registry.socket /etc/systemd/system/
sudo cp lap-registry-init.service /etc/systemd/system/

# 3. 重载 systemd 配置
sudo systemctl daemon-reload

# 4. 启用 socket (系统启动时自动创建)
sudo systemctl enable lap-registry.socket

# 5. 启动 socket (立即生效)
sudo systemctl start lap-registry.socket

# 6. 验证 socket 状态
sudo systemctl status lap-registry.socket
# 输出: Active: active (listening) ...

# 7. 首次客户端连接时，systemd 自动启动 lap-registry-init.service
# 之后 lap-registry-init.service 进入 inactive (dead) 状态
```

**运行时行为**：

```text
时间线                         | 进程状态
------------------------------|----------------------------------
系统启动                       | systemd 创建 /run/lap/registry.sock
                              | lap-registry.socket: active (listening)
                              | lap-registry-init.service: inactive (dead)
                              | 
首次应用进程连接 socket        | systemd 自动启动 lap-registry-init.service
                              | lap-registry-init 创建 memfd
                              | 发送 memfd fd 到客户端
                              | lap-registry-init 退出
                              | lap-registry-init.service: inactive (dead)
                              | 
后续所有进程连接 socket        | 直接从 socket 接收 memfd fd
                              | 无需启动 lap-registry-init.service
                              | (systemd 缓存了 fd，或通过新连接触发 oneshot)
```

**优势分析**：

| 特性 | 传统 Daemon | systemd Socket Activation |
|------|-----------|---------------------------|
| **常驻进程** | ✅ 1个 | ❌ 0个 |
| **内存占用** | ~5MB | ~0MB (无常驻) |
| **启动顺序** | 严格依赖 | 并行启动 |
| **故障恢复** | 需监控重启 | systemd 自动管理 |
| **热升级** | 需停服 | ✅ 无缝升级 |
| **系统集成** | 自定义脚本 | ✅ systemd 原生 |

#### 2.2.4 1GB HugePage 支持

**大页优势**：

- ✅ 减少 TLB miss (Translation Lookaside Buffer)
- ✅ 提升缓存命中率 (更大的连续物理内存)
- ✅ 降低页表开销

**配置方式**：

```bash
# 系统级配置
sudo sysctl -w vm.nr_hugepages=2  # 分配 2 × 1GB 大页

# 或 /etc/sysctl.conf 永久配置
vm.nr_hugepages = 2
vm.hugetlb_shm_group = 1000  # 允许的组 ID
```

**代码集成**：

```cpp
void* MapWithHugePage(int fd, size_t size) {
    // 1. 尝试显式 1GB 大页
    void* addr = mmap(nullptr, size,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_HUGETLB | MAP_HUGE_1GB,
                      fd, 0);
    
    if (addr != MAP_FAILED) {
        return addr;  // 成功使用 1GB 大页
    }
    
    // 2. 回退到 THP (Transparent Huge Pages)
    addr = mmap(nullptr, size,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                fd, 0);
    
    if (addr == MAP_FAILED) {
        return nullptr;
    }
    
    // 3. 建议内核使用 THP
    madvise(addr, size, MADV_HUGEPAGE);
    
    return addr;
}
```

**性能对比** (256KB 注册表):

| 页大小 | TLB 条目数 | TLB miss 率 | 延迟提升 |
|--------|-----------|------------|---------|
| 4KB | 64 | 高 | 基准 |
| 2MB (THP) | 1 | 低 | ~10% |
| 1GB (Explicit) | 1 | 极低 | ~15% |

#### 2.2.5 Guard Page 保护

**设计目标**：

防止越界访问，立即捕获内存破坏 bug。

**实现方式**：

```cpp
void* SetupGuardPages(void* base_addr, size_t registry_size) {
    const size_t page_size = 4096;
    const size_t total_size = registry_size + 2 * page_size;  // 前后各1页
    
    // 1. 重新映射，包含 guard pages
    void* full_region = mmap(nullptr, total_size,
                             PROT_NONE,  // 初始不可访问
                             MAP_PRIVATE | MAP_ANONYMOUS,
                             -1, 0);
    
    // 2. 仅允许访问中间的注册表区域
    void* registry_start = static_cast<char*>(full_region) + page_size;
    mprotect(registry_start, registry_size, PROT_READ | PROT_WRITE);
    
    // 3. 前后 guard pages 保持 PROT_NONE
    // 任何越界访问将触发 SIGSEGV
    
    return registry_start;
}
```

**错误捕获**：

```cpp
void InstallSIGSEGVHandler() {
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = [](int sig, siginfo_t* info, void* ctx) {
        void* fault_addr = info->si_addr;
        
        // 检查是否为 guard page 访问
        if (IsGuardPageAddress(fault_addr)) {
            LOG_ERROR("Guard page violation at {}", fault_addr);
            // 记录调用栈、崩溃转储等
        }
        
        // 继续默认处理（终止进程）
        signal(SIGSEGV, SIG_DFL);
        raise(SIGSEGV);
    };
    sigaction(SIGSEGV, &sa, nullptr);
}
```

#### 2.2.6 权限隔离与安全防护 (FuSa + Security Hardening)

**安全需求**：

1. **FuSa 权限隔离**: QM/ASIL 物理分离
2. **外部注入防护**: 防止恶意进程篡改注册表
3. **访问控制**: 基于 UID/GID 的细粒度权限
4. **完整性保护**: CRC32 校验 + seqlock 双重保障

##### 方案 1: 双 memfd 物理隔离 (推荐)

**架构设计**：

```text
systemd socket activation (两个独立 socket)
    ↓                              ↓
/run/lap/registry-qm.sock    /run/lap/registry-asil.sock
    ↓                              ↓
lap-registry-qm-init         lap-registry-asil-init
(oneshot)                    (oneshot, 受限权限)
    ↓                              ↓
qm_memfd (0666)              asil_memfd (0640)
所有进程可读写                仅 lap-control 组可写
```

**systemd Socket 单元**：

```ini
# /etc/systemd/system/lap-registry-qm.socket
[Socket]
ListenStream=/run/lap/registry-qm.sock
SocketMode=0666  # 所有进程可连接
SocketUser=root
SocketGroup=root

# /etc/systemd/system/lap-registry-asil.socket
[Socket]
ListenStream=/run/lap/registry-asil.sock
SocketMode=0660  # 仅 lap-control 组可连接
SocketUser=root
SocketGroup=lap-control  # 关键：限制访问
```

**Oneshot 服务权限控制**：

```ini
# /etc/systemd/system/lap-registry-asil-init.service
[Service]
Type=oneshot
ExecStart=/usr/lib/lap/com/lap-registry-asil-init

# 安全加固 (ASIL Registry 级别更严格，适用ASIL-D服务)
User=lap-registry
Group=lap-control
SupplementaryGroups=

# 能力限制 (CAP_SYS_ADMIN for memfd_create)
AmbientCapabilities=CAP_SYS_ADMIN
CapabilityBoundingSet=CAP_SYS_ADMIN

# 命名空间隔离
PrivateNetwork=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadOnlyPaths=/
ReadWritePaths=/run/lap

# 系统调用过滤 (仅允许必要的 syscall)
SystemCallFilter=@system-service
SystemCallFilter=~@privileged @resources
SystemCallFilter=memfd_create mmap munmap

# 禁止新权限
NoNewPrivileges=true
```

**代码实现**：

```cpp
// lap-registry-asil-init.cpp (ASIL-D 初始化进程)

struct ASILDRegistryInit {
    int CreateASILDMemfd() {
        // 1. 创建 ASIL-D memfd
        int memfd = memfd_create("lap_com_registry_asil", 
                                 MFD_CLOEXEC | 
                                 MFD_ALLOW_SEALING);
        
        // 2. 设置大小 (仅 ASIL 槽位: 100-199，示例范围)
        const size_t asil_size = 100 * 256;  // 100 slots × 256 bytes
        ftruncate(memfd, asil_size);
        
        // 3. 映射并初始化
        void* addr = mmap(nullptr, asil_size,
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED, memfd, 0);
        memset(addr, 0, asil_size);
        
        // 4. 密封内存 (防止 resize 攻击)
        fcntl(memfd, F_ADD_SEALS, 
              F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL);
        
        // 5. 设置 fd 权限 (仅 lap-control 组可写)
        // 注意: memfd 本身无权限，通过 socket 传递时控制
        
        return memfd;
    }
    
    // 发送 ASIL memfd 时验证客户端权限
    int SendMemfdWithAuthCheck(int client_socket, int memfd) {
        // 1. 获取客户端进程凭据
        struct ucred cred;
        socklen_t len = sizeof(cred);
        getsockopt(client_socket, SOL_SOCKET, SO_PEERCRED, &cred, &len);
        
        // 2. 验证 GID (必须是 lap-control 组成员)
        if (!IsInGroup(cred.gid, "lap-control")) {
            LOG_ERROR("ASIL Registry access denied: pid={}, uid={}, gid={}", 
                     cred.pid, cred.uid, cred.gid);
            return -EACCES;
        }
        
        // 3. 验证可执行文件路径 (防止注入)
        char exe_path[PATH_MAX];
        snprintf(exe_path, sizeof(exe_path), "/proc/%d/exe", cred.pid);
        
        char real_path[PATH_MAX];
        readlink(exe_path, real_path, sizeof(real_path));
        
        if (!IsAllowedExecutable(real_path)) {
            LOG_ERROR("ASIL Registry access denied: unauthorized exe={}", real_path);
            return -EACCES;
        }
        
        // 4. 发送 memfd (SCM_RIGHTS)
        return SendMemfdViaSocket(client_socket, memfd);
    }
};
```

##### 方案 2: mprotect 运行时权限控制

**客户端映射时权限控制**：

```cpp
// SharedMemoryRegistry.cpp (客户端)

class RegistryClient {
public:
    int MapASILDRegionWithPermission(int memfd) {
        // 1. 检查当前进程是否有写权限
        bool has_write_permission = IsInGroup(getgid(), "lap-control");
        
        // 2. 映射内存
        void* addr = mmap(nullptr, 256 * 1024,
                         has_write_permission ? 
                             (PROT_READ | PROT_WRITE) : PROT_READ,
                         MAP_SHARED, memfd, 0);
        
        // 3. 对于非控制进程，额外使用 mprotect 强制只读
        if (!has_write_permission) {
            mprotect(addr, 256 * 1024, PROT_READ);
            
            // 4. 安装 SIGSEGV 处理器捕获违规写入
            InstallSegfaultHandler();
        }
        
        return 0;
    }
    
private:
    static void InstallSegfaultHandler() {
        struct sigaction sa;
        sa.sa_flags = SA_SIGINFO;
        sa.sa_sigaction = [](int sig, siginfo_t* info, void* ctx) {
            if (IsASILDRegionAddress(info->si_addr)) {
                LOG_CRITICAL("ASIL Registry write violation detected! "
                            "Addr={}, Pid={}", 
                            info->si_addr, getpid());
                
                // 记录审计日志
                AuditLog("ASIL_WRITE_VIOLATION", getpid());
                
                // 终止进程 (安全策略)
                abort();
            }
        };
        sigaction(SIGSEGV, &sa, nullptr);
    }
};
```

##### 方案 3: 完整性保护 (CRC32 + seqlock)

**完整性校验增强**：

在上述 ServiceSlot 结构中已包含 CRC32 和 write_counter 字段，用于 ASIL-C/D 级别服务的安全增强。

**CRC32 计算方法**：

```cpp
// 计算 ServiceSlot 的 CRC32 校验和（排除 sequence 和 crc32 本身）
uint32_t ServiceSlot::ComputeCRC32() const {
    const uint8_t* data = reinterpret_cast<const uint8_t*>(this);
    const size_t offset = offsetof(ServiceSlot, service_id);
    const size_t len = offsetof(ServiceSlot, crc32) - offset;
    
    return crc32_fast(data + offset, len);
}
```

**写入流程增强**：

```cpp
void WriteSlotWithIntegrity(uint32_t slot_index, const ServiceInfo& info) {
    auto& slot = slots_[slot_index];
    
    // 1. seqlock 加锁
    slot.sequence.fetch_add(1, std::memory_order_acquire);
    
    // 2. 写入数据
    slot.service_id = info.service_id;
    slot.instance_id = info.instance_id;
    // ... 写入其他字段
    
    // 3. 递增写入计数器 (防重放攻击)
    slot.write_counter++;
    
    // 4. 计算并写入 CRC32
    slot.crc32 = slot.ComputeCRC32();
    
    std::atomic_thread_fence(std::memory_order_release);
    
    // 5. seqlock 解锁
    slot.sequence.fetch_add(1, std::memory_order_release);
}
```

**读取流程验证**：

```cpp
std::optional<ServiceInfo> ReadSlotWithVerification(uint32_t slot_index) {
    auto& slot = slots_[slot_index];
    ServiceInfo info;
    uint64_t seq1, seq2;
    uint32_t crc_expected, crc_actual;
    
    do {
        // 1. seqlock 读取
        seq1 = slot.sequence.load(std::memory_order_acquire);
        if (seq1 & 1) {
            _mm_pause();
            continue;
        }
        
        // 2. 读取数据
        info.service_id = slot.service_id;
        info.instance_id = slot.instance_id;
        // ... 读取其他字段
        
        // 3. 读取 CRC32
        crc_expected = slot.crc32;
        
        std::atomic_thread_fence(std::memory_order_acquire);
        seq2 = slot.sequence.load(std::memory_order_acquire);
        
    } while (seq1 != seq2);
    
    // 4. 验证 CRC32 (防篡改)
    crc_actual = ComputeCRC32FromInfo(info);
    if (crc_actual != crc_expected) {
        LOG_ERROR("CRC32 mismatch in slot {}: expected={:x}, actual={:x}", 
                 slot_index, crc_expected, crc_actual);
        
        // 审计日志
        AuditLog("INTEGRITY_VIOLATION", slot_index);
        
        return std::nullopt;  // 拒绝返回被篡改的数据
    }
    
    return info;
}
```

##### 方案 4: 外部注入防护

**允许列表机制**：

```cpp
// /etc/lap/com/allowed_executables.conf
// 仅允许以下可执行文件访问注册表

struct AllowedExecutable {
    std::string path;
    std::string sha256_hash;  // 可执行文件哈希
};

const std::vector<AllowedExecutable> ALLOWED_EXES = {
    {"/usr/bin/lap-perception",  "abc123..."},
    {"/usr/bin/lap-planning",    "def456..."},
    {"/usr/bin/lap-control",     "789ghi..."},
};

bool IsAllowedExecutable(const std::string& exe_path) {
    // 1. 检查路径是否在允许列表
    auto it = std::find_if(ALLOWED_EXES.begin(), ALLOWED_EXES.end(),
        [&](const auto& entry) { return entry.path == exe_path; });
    
    if (it == ALLOWED_EXES.end()) {
        return false;  // 不在允许列表
    }
    
    // 2. 验证 SHA256 哈希 (防止二进制替换)
    std::string actual_hash = ComputeSHA256(exe_path);
    if (actual_hash != it->sha256_hash) {
        LOG_CRITICAL("Executable hash mismatch: {}, expected={}, actual={}", 
                    exe_path, it->sha256_hash, actual_hash);
        return false;
    }
    
    return true;
}
```

**SELinux 集成** (可选):

```bash
# SELinux 策略文件 (lap_registry.te)

# 定义类型
type lap_registry_t;
type lap_registry_qm_t;
type lap_registry_asil_t;

# QM 级注册表：所有 lap 进程可读写
allow lap_domain_t lap_registry_qm_t:file { read write };

# ASIL 级注册表：仅 lap_control_t 可写
allow lap_control_t lap_registry_asil_t:file { read write };
allow lap_domain_t lap_registry_asil_t:file { read };  # 其他仅可读
```

##### 安全审计与合规

**FuSa 认证证据**：

```text
1. 物理隔离证明
   ✅ QM / ASIL 使用独立 memfd (不同 socket)
   ✅ Guard pages 防止越界访问
   ✅ 独立的 systemd 服务单元

2. 权限控制证明
   ✅ ASIL socket 仅 lap-control 组可访问
   ✅ 客户端 GID 验证 (SO_PEERCRED)
   ✅ mprotect 强制只读 (非控制进程)
   ✅ SIGSEGV 处理器捕获违规写入

3. 完整性保护
   ✅ CRC32 校验和 (每次读取验证)
   ✅ 写入计数器 (防重放攻击)
   ✅ seqlock 双重保障

4. 访问控制
   ✅ 可执行文件允许列表
   ✅ SHA256 哈希验证 (防二进制替换)
   ✅ SELinux 策略 (可选)

5. 审计日志
   ✅ 所有违规访问记录到 syslog
   ✅ systemd journal 集成
   ✅ 实时告警机制
```

**安全测试用例**：

```cpp
// 测试 1: ASIL-D 写入权限验证
TEST(SecurityTest, ASILDWriteProtection) {
    // 模拟非控制进程
    ASSERT_DEATH({
        auto registry = SharedMemoryRegistry::GetInstance();
        registry.Initialize();
        
        // 尝试写入 ASIL-D 槽位 (slot 100-199)
        ServiceInfo info;
        info.slot_index = 100;  // ASIL-D 槽位
        registry.WriteSlot(100, info);  // 应触发 SIGSEGV
    }, "ASIL_WRITE_VIOLATION");
}

// 测试 2: CRC32 完整性验证
TEST(SecurityTest, CRC32IntegrityCheck) {
    auto registry = SharedMemoryRegistry::GetInstance();
    
    // 1. 正常写入
    ServiceInfo info;
    registry.WriteSlot(10, info);
    
    // 2. 模拟外部篡改 (直接修改内存)
    auto* slot = registry.GetSlotPointer(10);
    slot->service_id = 0xDEADBEEF;  // 破坏数据
    // 不更新 CRC32
    
    // 3. 读取应检测到篡改
    auto result = registry.ReadSlot(10);
    ASSERT_FALSE(result.has_value());  // 应拒绝返回
}

// 测试 3: 可执行文件允许列表
TEST(SecurityTest, ExecutableWhitelist) {
    // 1. 允许的可执行文件
    ASSERT_TRUE(IsAllowedExecutable("/usr/bin/lap-control"));
    
    // 2. 不在列表的可执行文件
    ASSERT_FALSE(IsAllowedExecutable("/tmp/malicious"));
    
    // 3. 哈希不匹配
    // 修改 lap-control 二进制
    // ...
    ASSERT_FALSE(IsAllowedExecutable("/usr/bin/lap-control"));
}
```

### 2.3 配置与部署

#### 2.3.1 槽位映射配置 (slot_mapping.yaml)

```yaml
# LightAP Com Module - Service Slot Mapping Configuration
# 用途: 服务 ID → 固定槽位映射

slot_mapping:
  # === 关键服务 (静态分配) ===
  static_allocations:
    - service_interface: "RadarService"
      instance_id: 1
      slot_index: 10
      safety_level: QM
      description: "前向雷达服务"
    
    - service_interface: "CameraService"
      instance_id: 1
      slot_index: 11
      safety_level: QM
      description: "前置摄像头服务"
    
    - service_interface: "SteeringControl"
      instance_id: 1
      slot_index: 100
      safety_level: ASIL-D
      description: "转向控制服务（关键）"
    
    - service_interface: "BrakeControl"
      instance_id: 1
      slot_index: 101
      safety_level: ASIL-D
      description: "制动控制服务（关键）"
  
  # === 动态服务池 (哈希分配) ===
  dynamic_allocation:
    enabled: true
    slot_range:
      start: 200
      end: 1023
    hash_algorithm: "CRC32"  # CRC32 / FNV1a / MurmurHash3
    collision_resolution: "linear_probing"  # linear_probing / quadratic
  
  # === 系统配置 ===
  system:
    max_slots: 1024
    slot_size_bytes: 256
    registry_size_bytes: 262144  # 256KB
    
    # memfd 配置
    use_hugepages: true
    hugepage_size: "1GB"  # 1GB / 2MB / disabled
    enable_guard_pages: true
    
    # 心跳配置
    heartbeat:
      interval_ms: 1000        # 1秒发送一次心跳
      timeout_multiplier: 3    # 3秒无心跳视为超时
      monitor_interval_ms: 500 # 500ms 检查一次
```

#### 2.3.2 进程启动流程

```cpp
// main.cpp (应用进程启动)

#include <lap/com/Runtime.hpp>

int main() {
    // 1. 初始化 Runtime (内部创建/映射共享内存)
    auto result = lap::com::Runtime::Initialize();
    if (!result) {
        LOG_ERROR("Failed to initialize Runtime: {}", result.Error());
        return -1;
    }
    
    // 2. 提供服务 (自动写入固定槽位)
    auto offer_result = lap::com::OfferService<RadarService>(
        lap::core::InstanceSpecifier("RadarService/Instance1")
    );
    
    // 3. 查找服务 (直接读取共享内存)
    auto handles = lap::com::FindService<CameraService>();
    
    // ... 应用逻辑 ...
    
    // 4. 清理 (自动清除槽位、停止心跳)
    lap::com::Runtime::Deinitialize();
    
    return 0;
}
```

**Runtime 内部实现**：

```cpp
Result<void> Runtime::Initialize() {
    // 1. 首次初始化时创建 memfd
    static std::once_flag init_flag;
    std::call_once(init_flag, []() {
        g_registry_fd = CreateServiceRegistryMemfd();
        g_registry_ptr = MapWithHugePage(g_registry_fd, REGISTRY_SIZE);
        SetupGuardPages(g_registry_ptr, REGISTRY_SIZE);
    });
    
    // 2. 后续进程直接映射现有 memfd
    // (通过 Unix Domain Socket 传递文件描述符，或通过 /proc/[pid]/fd/)
    
    // 3. 启动心跳监控线程
    g_heartbeat_monitor.Start();
    
    return Result<void>::FromValue();
}
```

### 2.4 性能分析与优化

#### 2.4.1 延迟分解

**FindService 延迟分解** (< 500ns 目标):

```text
操作阶段                        | 延迟 (ns) | 占比 |
-------------------------------|----------|------|
1. 槽位索引计算               |   10     | 2%   |
2. 缓存行加载 (L1 hit)        |   50     | 10%  |
3. seqlock 读取 (sequence)    |   20     | 4%   |
4. 数据读取 (256 bytes)       |  200     | 40%  |
5. seqlock 验证 (sequence)    |   20     | 4%   |
6. 心跳超时检查               |   50     | 10%  |
7. 数据拷贝到返回结构         |  150     | 30%  |
-------------------------------|----------|------|
总计                           |  500     | 100% |
```

**OfferService 延迟分解** (< 1μs 目标):

```text
操作阶段                        | 延迟 (ns) | 占比 |
-------------------------------|----------|------|
1. 槽位索引计算               |   10     | 1%   |
2. seqlock 加锁 (fetch_add)   |   30     | 3%   |
3. 数据写入 (256 bytes)       |  300     | 30%  |
4. 内存屏障 (fence)           |   50     | 5%   |
5. seqlock 解锁 (fetch_add)   |   30     | 3%   |
6. 心跳线程启动              |  500     | 50%  |
7. 缓存刷新 (clflush)         |   80     | 8%   |
-------------------------------|----------|------|
总计                           | 1000     | 100% |
```

#### 2.4.2 优化技巧

##### 1. CPU 缓存优化

```cpp
// 预取下一个槽位
__builtin_prefetch(&slots_[next_index], 0, 3);  // 预取到 L1

// 缓存行对齐
struct alignas(64) ServiceSlot { /* ... */ };

// SIMD 加速数据拷贝
memcpy_sse2(&dest, &src, 256);
```

##### 2. NUMA 感知

```cpp
// 绑定内存到当前 NUMA 节点
numa_set_preferred(numa_node_of_cpu(sched_getcpu()));

// 检查跨 NUMA 访问
if (numa_node_of_cpu(sched_getcpu()) != GetSlotNUMANode(slot_index)) {
    LOG_WARN("Cross-NUMA access detected");
}
```

##### 3. Lock-free 心跳

```cpp
// 使用原子时间戳替代互斥锁
std::atomic<uint64_t> last_heartbeat_ns;
last_heartbeat_ns.store(now_ns, std::memory_order_relaxed);
```

### 2.5 与 iceoryx2 数据通信集成

**重要**: 本注册表仅用于服务发现，数据传输仍使用 iceoryx2 本身的零拷贝机制。

**集成方式**：

```cpp
// 1. OfferService: 注册服务到共享内存槽位
auto offer_result = OfferService<RadarService>(instance_spec);

// 槽位中记录 iceoryx2 服务名称:
// slot.binding_type = "iceoryx2"
// slot.endpoint = "/perception/radar_front"

// 2. FindService: 从槽位读取 iceoryx2 服务名称
auto handles = FindService<RadarService>();
// handles[0].endpoint = "/perception/radar_front"

// 3. iceoryx2 Binding: 使用服务名称创建 Publisher/Subscriber
auto publisher = iceoryx2::Publisher::new(
    ServiceName::new("/perception/radar_front"));

auto subscriber = iceoryx2::Subscriber::new(
    ServiceName::new("/perception/radar_front"));

// 4. 数据传输: iceoryx2 零拷贝（本注册表不参与）
publisher.loan().unwrap().write(&radar_data).send();
```

**职责划分**：

| 层次 | 职责 | 技术 |
|------|------|------|
| 服务发现 | 服务注册/查找 | 本注册表 (共享内存槽位) |
| 数据传输 | 零拷贝 Pub/Sub | iceoryx2 原生机制 |

---

## 第 3 章: 核心组件设计

### 3.1 ServiceRegistry (服务注册表)

**职责**: 维护本地和远程服务实例的缓存。

#### 3.1.1 数据结构

```cpp
namespace lap::com::runtime {

class ServiceRegistry {
public:
    struct ServiceInstanceInfo {
        InstanceIdentifier instance_id;
        InstanceSpecifier instance_specifier;
        
        // 服务接口信息
        std::string service_interface_name;
        uint32_t major_version;
        uint32_t minor_version;
        
        // 网络信息
        TransportBindingType binding_type;  // D-Bus / SOME/IP / DDS
        std::string network_endpoint;        // IP:Port / Bus Name / DDS Topic
        
        // 生命周期
        ServiceState state;                  // OFFERED / STOPPED
        std::chrono::steady_clock::time_point last_seen;
        std::optional<std::chrono::milliseconds> ttl;  // Time-To-Live
        
        // 元数据
        std::map<std::string, std::string> metadata;
    };
    
    enum class ServiceState {
        OFFERED,      // 服务正在提供
        STOPPED,      // 服务已停止
        UNKNOWN       // 未知状态
    };
    
    // 注册本地服务
    Result<void> RegisterLocalService(const ServiceInstanceInfo& info);
    
    // 注销本地服务
    Result<void> UnregisterLocalService(const InstanceIdentifier& instance_id);
    
    // 注册远程服务
    Result<void> RegisterRemoteService(const ServiceInstanceInfo& info);
    
    // 查询服务
    Result<std::vector<ServiceInstanceInfo>> FindServices(
        const std::string& service_interface_name,
        const std::optional<InstanceIdentifier>& instance_filter = std::nullopt
    ) const;
    
    // 获取服务状态
    Result<ServiceState> GetServiceState(const InstanceIdentifier& instance_id) const;
    
    // 清理过期服务 (TTL 超时)
    void CleanupExpiredServices();
    
private:
    // 本地服务表 (本进程提供的服务)
    std::unordered_map<InstanceIdentifier, ServiceInstanceInfo> local_services_;
    
    // 远程服务表 (其他进程/节点提供的服务)
    std::unordered_map<InstanceIdentifier, ServiceInstanceInfo> remote_services_;
    
    // 索引: ServiceInterfaceName → InstanceIdentifiers
    std::unordered_multimap<std::string, InstanceIdentifier> service_index_;
    
    mutable std::shared_mutex registry_mutex_;
};

} // namespace lap::com::runtime
```

#### 3.1.2 服务版本兼容性检查

```cpp
class ServiceRegistry {
public:
    // 版本兼容性检查 (符合 AUTOSAR SWS_CM_11006 / SWS_CM_90510)
    static bool IsVersionCompatible(
        uint32_t required_major,
        uint32_t required_minor,
        uint32_t provided_major,
        uint32_t provided_minor
    ) {
        // 主版本号必须完全匹配
        if (required_major != provided_major) {
            return false;
        }
        
        // 次版本号: 提供者版本 >= 请求者版本 (向后兼容)
        return provided_minor >= required_minor;
    }
    
    // 检查版本是否在黑名单中 (DdsRequiredServiceInstance.blocklistedVersion)
    bool IsVersionBlocklisted(
        const InstanceIdentifier& instance_id,
        uint32_t major_version,
        uint32_t minor_version
    ) const;
};
```

### 3.2 ServiceDiscoveryManager (服务发现管理器)

**职责**: 处理 FindService/OfferService/StopOfferService API 调用。

#### 3.2.1 核心接口

```cpp
namespace lap::com::runtime {

class ServiceDiscoveryManager {
public:
    // 初始化
    Result<void> Initialize(const ServiceDiscoveryConfig& config);
    
    // ======== 服务提供者 API ========
    
    // OfferService (SWS_CM_00110, SWS_CM_11001, SWS_CM_90502)
    Result<void> OfferService(
        const InstanceIdentifier& instance_id,
        const ServiceInterfaceInfo& interface_info
    );
    
    // StopOfferService (SWS_CM_00111, SWS_CM_11005, SWS_CM_90509)
    Result<void> StopOfferService(const InstanceIdentifier& instance_id);
    
    // ======== 服务消费者 API ========
    
    // FindService - 单次查询 (SWS_CM_00122, SWS_CM_00622)
    Result<ServiceHandleContainer<HandleType>> FindService(
        const InstanceIdentifier& instance_filter
    );
    
    Result<ServiceHandleContainer<HandleType>> FindService(
        const InstanceSpecifier& instance_specifier
    );
    
    // StartFindService - 持续发现 (SWS_CM_00123, SWS_CM_90514)
    Result<FindServiceHandle> StartFindService(
        const InstanceIdentifier& instance_filter,
        FindServiceHandler handler
    );
    
    Result<FindServiceHandle> StartFindService(
        const InstanceSpecifier& instance_specifier,
        FindServiceHandler handler
    );
    
    // StopFindService - 停止持续发现 (SWS_CM_00125, SWS_CM_11013)
    Result<void> StopFindService(FindServiceHandle handle);
    
private:
    std::shared_ptr<ServiceRegistry> service_registry_;
    std::unique_ptr<FindServiceHandlerManager> handler_manager_;
    std::vector<std::unique_ptr<ITransportBinding>> transport_bindings_;
};

} // namespace lap::com::runtime
```

#### 3.2.2 OfferService 流程

```cpp
Result<void> ServiceDiscoveryManager::OfferService(
    const InstanceIdentifier& instance_id,
    const ServiceInterfaceInfo& interface_info
) {
    // 1. 验证服务接口信息
    if (!ValidateServiceInterface(interface_info)) {
        return Result<void>::FromError(ComErrc::kInvalidServiceInterface);
    }
    
    // 2. 注册到本地服务注册表
    ServiceRegistry::ServiceInstanceInfo info{
        .instance_id = instance_id,
        .service_interface_name = interface_info.name,
        .major_version = interface_info.major_version,
        .minor_version = interface_info.minor_version,
        .binding_type = GetDefaultBindingType(),
        .state = ServiceRegistry::ServiceState::OFFERED,
        .last_seen = std::chrono::steady_clock::now()
    };
    
    auto result = service_registry_->RegisterLocalService(info);
    if (!result) {
        return result;
    }
    
    // 3. 通知所有 Transport Binding 发布服务
    for (auto& binding : transport_bindings_) {
        // D-Bus: 注册 Bus Name, 发布 ObjectManager
        // SOME/IP: 发送 OfferService 消息 (SD Protocol)
        // DDS: 创建 DomainParticipant, 设置 USER_DATA QoS
        binding->OfferService(instance_id, interface_info);
    }
    
    // 4. 触发 FindServiceHandler 回调 (通知正在等待的消费者)
    handler_manager_->NotifyServiceAvailable(instance_id);
    
    return Result<void>::FromValue();
}
```

#### 3.2.3 FindService 流程

```cpp
Result<ServiceHandleContainer<HandleType>> 
ServiceDiscoveryManager::FindService(const InstanceIdentifier& instance_filter) {
    ServiceHandleContainer<HandleType> handles;
    
    // 1. 查询本地服务注册表
    auto local_services = service_registry_->FindServices(
        GetServiceInterfaceName(),
        instance_filter
    );
    
    if (!local_services) {
        return Result<ServiceHandleContainer<HandleType>>::FromError(
            local_services.Error()
        );
    }
    
    // 2. 转换为 HandleType
    for (const auto& service_info : local_services.Value()) {
        // 版本兼容性检查
        if (!ServiceRegistry::IsVersionCompatible(
            GetRequiredMajorVersion(),
            GetRequiredMinorVersion(),
            service_info.major_version,
            service_info.minor_version
        )) {
            continue;  // 跳过不兼容的版本
        }
        
        // 检查黑名单
        if (service_registry_->IsVersionBlocklisted(
            service_info.instance_id,
            service_info.major_version,
            service_info.minor_version
        )) {
            continue;  // 跳过黑名单版本
        }
        
        // 创建 HandleType
        HandleType handle{
            .instance_id = service_info.instance_id,
            .binding_type = service_info.binding_type,
            .endpoint = service_info.network_endpoint
        };
        
        handles.push_back(std::move(handle));
    }
    
    return Result<ServiceHandleContainer<HandleType>>::FromValue(
        std::move(handles)
    );
}
```

#### 3.2.4 StartFindService 流程 (持续发现)

```cpp
Result<FindServiceHandle> ServiceDiscoveryManager::StartFindService(
    const InstanceIdentifier& instance_filter,
    FindServiceHandler handler
) {
    // 1. 生成唯一的 FindServiceHandle
    FindServiceHandle handle = GenerateUniqueHandle();
    
    // 2. 注册回调到 FindServiceHandlerManager
    handler_manager_->RegisterHandler(handle, instance_filter, handler);
    
    // 3. 立即触发一次查询 (返回当前已知的服务)
    auto current_services = FindService(instance_filter);
    if (current_services && !current_services.Value().empty()) {
        handler(current_services.Value(), FindServiceStatus::kServiceAvailable);
    }
    
    // 4. 启动 Transport Binding 的持续发现
    for (auto& binding : transport_bindings_) {
        // D-Bus: 监听 NameOwnerChanged 信号
        // SOME/IP: 持续监听 SD OfferService 消息
        // DDS: 设置 BuiltinParticipantListener (SWS_CM_11010, SWS_CM_11011)
        binding->StartFindService(instance_filter, [this, handle](const auto& new_service) {
            OnRemoteServiceDiscovered(handle, new_service);
        });
    }
    
    return Result<FindServiceHandle>::FromValue(handle);
}
```

### 3.3 FindServiceHandlerManager (回调管理器)

**职责**: 管理 FindServiceHandler 回调，处理服务状态变化通知。

```cpp
namespace lap::com::runtime {

class FindServiceHandlerManager {
public:
    struct HandlerRegistration {
        FindServiceHandle handle;
        InstanceIdentifier instance_filter;
        FindServiceHandler callback;
        std::chrono::steady_clock::time_point registered_at;
    };
    
    // 注册 FindServiceHandler
    void RegisterHandler(
        FindServiceHandle handle,
        const InstanceIdentifier& instance_filter,
        FindServiceHandler callback
    );
    
    // 注销 FindServiceHandler
    void UnregisterHandler(FindServiceHandle handle);
    
    // 通知服务可用
    void NotifyServiceAvailable(const InstanceIdentifier& instance_id);
    
    // 通知服务不可用
    void NotifyServiceUnavailable(const InstanceIdentifier& instance_id);
    
private:
    // FindServiceHandle → HandlerRegistration
    std::unordered_map<FindServiceHandle, HandlerRegistration> handlers_;
    
    // InstanceId → FindServiceHandles (索引，加速查找)
    std::unordered_multimap<InstanceIdentifier, FindServiceHandle> instance_index_;
    
    mutable std::shared_mutex handlers_mutex_;
    
    // 工作线程池 (异步触发回调)
    std::unique_ptr<ThreadPool> callback_executor_;
};

void FindServiceHandlerManager::NotifyServiceAvailable(
    const InstanceIdentifier& instance_id
) {
    std::shared_lock lock(handlers_mutex_);
    
    // 查找所有匹配的 Handler
    auto range = instance_index_.equal_range(instance_id);
    
    for (auto it = range.first; it != range.second; ++it) {
        FindServiceHandle handle = it->second;
        auto handler_it = handlers_.find(handle);
        
        if (handler_it != handlers_.end()) {
            // 异步触发回调
            callback_executor_->Post([callback = handler_it->second.callback,
                                      instance_id]() {
                // 查询服务详情
                auto service_info = GetServiceInfo(instance_id);
                if (service_info) {
                    ServiceHandleContainer<HandleType> handles;
                    handles.push_back(CreateHandle(service_info.Value()));
                    callback(handles, FindServiceStatus::kServiceAvailable);
                }
            });
        }
    }
}

} // namespace lap::com::runtime
```

---

## 第 4 章: Transport Binding 适配

### 4.1 ITransportBinding 接口

```cpp
namespace lap::com::runtime {

class ITransportBinding {
public:
    virtual ~ITransportBinding() = default;
    
    // 服务提供
    virtual Result<void> OfferService(
        const InstanceIdentifier& instance_id,
        const ServiceInterfaceInfo& interface_info
    ) = 0;
    
    virtual Result<void> StopOfferService(
        const InstanceIdentifier& instance_id
    ) = 0;
    
    // 服务发现
    virtual Result<std::vector<ServiceInstanceInfo>> FindService(
        const InstanceIdentifier& instance_filter
    ) = 0;
    
    virtual Result<void> StartFindService(
        const InstanceIdentifier& instance_filter,
        std::function<void(const ServiceInstanceInfo&)> callback
    ) = 0;
    
    virtual Result<void> StopFindService(
        const InstanceIdentifier& instance_filter
    ) = 0;
    
    // 获取绑定类型
    virtual TransportBindingType GetBindingType() const = 0;
};

} // namespace lap::com::runtime
```

### 4.2 iceoryx2 Binding 服务发现（零拷贝优化）

**核心设计**: iceoryx2 服务通过固定槽位注册表直接管理，无需传统服务发现协议。

#### 4.2.1 OfferService 实现

```cpp
class Iceoryx2Binding : public ITransportBinding {
public:
    Result<void> OfferService(
        const InstanceIdentifier& instance_id,
        const ServiceInterfaceInfo& interface_info
    ) override {
        // 1. 创建 iceoryx2 Publisher
        auto publisher_builder = iox2::PublisherBuilder<SampleType>()
            .service_name(interface_info.name)
            .max_slice_len(1024);
        
        auto publisher = publisher_builder.create();
        if (!publisher) {
            return Result<void>::FromError(ErrorCode::kPublisherCreationFailed);
        }
        
        // 2. 获取 Publisher 在共享内存的 offset
        uint64_t endpoint_offset = GetPublisherOffset(publisher.value());
        uint64_t mempool_base = GetMempoolBaseAddress();
        
        // 3. 提取 QoS 配置
        uint32_t qos_profile = EncodeQoSProfile(
            publisher.value().reliability(),     // best_effort / reliable
            publisher.value().history_depth(),   // 历史深度
            publisher.value().max_subscribers()  // 最大订阅者数
        );
        
        // 4. 注册到固定槽位
        uint32_t slot_index = MapServiceIdToSlot(
            instance_id,
            GetRegistry(instance_id)  // QM+AB 或 ASIL-CD
        );
        
        ServiceSlot* slot = GetSlotPointer(slot_index);
        
        // 5. 使用 seqlock 写入槽位
        WriteSlotWithSeqlock(slot, [&]() {
            slot->service_id = GetServiceId(interface_info.name);
            slot->instance_id = EncodeInstanceId(instance_id);
            slot->major_version = interface_info.major_version;
            slot->minor_version = interface_info.minor_version;
            
            // iceoryx2 专用字段
            slot->endpoint_offset = endpoint_offset;      // 零拷贝关键！
            slot->mempool_base_ptr = mempool_base;
            slot->qos_profile = qos_profile;
            slot->subscriber_count = 0;  // 初始无订阅者
            
            // 通用字段
            std::strncpy(slot->binding_type, "iceoryx2", 16);
            slot->endpoint[0] = '\0';  // iceoryx2 不使用字符串端点
            
            slot->last_heartbeat_ns = GetMonotonicTimeNs();
            slot->heartbeat_interval_ms = 1000;
            slot->status = 1;  // 活跃
            slot->owner_pid = getpid();
            
            slot->crc32 = CalculateCRC32(slot);
            slot->write_counter++;
        });
        
        // 6. 启动心跳线程
        StartHeartbeatThread(slot_index, publisher.value());
        
        return Result<void>::FromValue();
    }
    
private:
    // 编码 QoS 配置到 32-bit 字段
    uint32_t EncodeQoSProfile(
        Reliability reliability,
        uint8_t history_depth,
        uint8_t max_subscribers
    ) {
        uint32_t qos = 0;
        qos |= (reliability == Reliability::Reliable ? 1 : 0);  // bit[0]
        qos |= (static_cast<uint32_t>(history_depth) << 8);      // bit[8-15]
        qos |= (static_cast<uint32_t>(max_subscribers) << 16);   // bit[16-23]
        return qos;
    }
    
    // 心跳线程：定期更新 subscriber_count
    void HeartbeatThreadFunc(uint32_t slot_index, iox2::Publisher<SampleType> publisher) {
        ServiceSlot* slot = GetSlotPointer(slot_index);
        
        while (slot->status == 1) {  // 服务活跃
            // 1. 更新心跳时间戳
            WriteSlotWithSeqlock(slot, [&]() {
                slot->last_heartbeat_ns = GetMonotonicTimeNs();
                
                // 2. 查询当前订阅者数量（iceoryx2 API）
                slot->subscriber_count = publisher.number_of_subscribers();
                
                // 3. 更新 CRC32
                slot->crc32 = CalculateCRC32(slot);
                slot->write_counter++;
            });
            
            // 休眠 heartbeat_interval_ms
            std::this_thread::sleep_for(
                std::chrono::milliseconds(slot->heartbeat_interval_ms)
            );
        }
    }
};
```

#### 4.2.2 FindService 实现（零拷贝访问）

```cpp
Result<std::vector<ServiceInstanceInfo>> Iceoryx2Binding::FindService(
    const InstanceIdentifier& instance_filter
) {
    std::vector<ServiceInstanceInfo> results;
    
    // 1. 计算槽位索引（O(1) 直接定位）
    uint32_t slot_index = MapServiceIdToSlot(
        instance_filter,
        GetRegistry(instance_filter)
    );
    
    ServiceSlot* slot = GetSlotPointer(slot_index);
    
    // 2. 使用 seqlock 无锁读取
    ServiceSlot snapshot;
    if (!ReadSlotWithSeqlock(slot, snapshot)) {
        return Result<void>::FromError(ErrorCode::kSeqlockReadFailed);
    }
    
    // 3. 验证槽位有效性
    if (snapshot.status != 1) {  // 非活跃
        return Result<void>::FromValue(results);  // 空结果
    }
    
    if (!VerifyCRC32(snapshot)) {  // CRC 校验失败
        return Result<void>::FromError(ErrorCode::kDataCorrupted);
    }
    
    // 4. 通过 endpoint_offset 零拷贝访问 iceoryx2 Publisher
    iox2::Publisher<SampleType>* publisher = 
        reinterpret_cast<iox2::Publisher<SampleType>*>(
            snapshot.mempool_base_ptr + snapshot.endpoint_offset
        );
    
    // 5. 解码 QoS 配置
    uint8_t reliability = snapshot.qos_profile & 0x1;
    uint8_t history_depth = (snapshot.qos_profile >> 8) & 0xFF;
    uint8_t max_subscribers = (snapshot.qos_profile >> 16) & 0xFF;
    
    // 6. 构建 ServiceInstanceInfo
    ServiceInstanceInfo info{
        .instance_id = DecodeInstanceId(snapshot.instance_id),
        .service_name = GetServiceName(snapshot.service_id),
        .major_version = snapshot.major_version,
        .minor_version = snapshot.minor_version,
        .binding_type = TransportBindingType::kIceoryx2,
        .endpoint_ptr = publisher,  // 零拷贝！直接返回指针
        .qos = {
            .reliability = (reliability == 1 ? Reliability::Reliable : Reliability::BestEffort),
            .history_depth = history_depth,
            .max_subscribers = max_subscribers
        },
        .subscriber_count = snapshot.subscriber_count,  // 实时统计
        .last_heartbeat = std::chrono::nanoseconds(snapshot.last_heartbeat_ns)
    };
    
    results.push_back(info);
    return Result<void>::FromValue(results);
}
```

**零拷贝优势**：

1. ✅ **无字符串解析**: endpoint_offset 直接定位，避免 "service_name" 字符串解析
2. ✅ **无内存拷贝**: 直接返回 iceoryx2 Publisher 指针，Proxy 可立即使用
3. ✅ **O(1) 性能**: 槽位索引 + offset 计算，延迟 < 100ns
4. ✅ **实时监控**: subscriber_count 字段提供服务健康度信息

### 4.3 DDS Binding 服务发现

> 基于 AUTOSAR SWS_CM_11001 - SWS_CM_11014 / SWS_CM_90502 - SWS_CM_90518

#### 4.3.1 DDS Discovery 方式

AUTOSAR 定义了两种 DDS 服务发现方式：

#### 方式 1: DomainParticipant USER_DATA QoS (SWS_CM_11008)

```cpp
class DdsBinding : public ITransportBinding {
public:
    Result<void> OfferService(
        const InstanceIdentifier& instance_id,
        const ServiceInterfaceInfo& interface_info
    ) override {
        // 1. 创建 DDS DomainParticipant
        auto participant = CreateDomainParticipant(GetDomainId());
        
        // 2. 设置 USER_DATA QoS (包含服务元数据)
        dds::core::policy::UserData user_data;
        user_data.value({
            SerializeServiceInfo(instance_id, interface_info)
        });
        
        participant.qos(participant.qos() << user_data);
        
        // 3. 创建 DataWriter (for Events, Fields)
        CreateDataWritersForService(participant, interface_info);
        
        return Result<void>::FromValue();
    }
    
    Result<void> StartFindService(
        const InstanceIdentifier& instance_filter,
        std::function<void(const ServiceInstanceInfo&)> callback
    ) override {
        // 设置 BuiltinParticipantListener (SWS_CM_11011)
        class ParticipantListener : public dds::domain::NoOpDomainParticipantListener {
            void on_participant_discovery(
                dds::domain::DomainParticipant& participant,
                const dds::core::status::ParticipantBuiltinTopicData& info
            ) override {
                // 解析 USER_DATA QoS
                auto service_info = DeserializeServiceInfo(info.user_data().value());
                
                // 检查是否匹配过滤条件
                if (MatchesFilter(service_info, instance_filter)) {
                    callback(service_info);
                }
            }
        };
        
        participant_.listener(
            new ParticipantListener(),
            dds::core::status::StatusMask::all()
        );
        
        return Result<void>::FromValue();
    }
};
```

#### 方式 2: Topic-Based Discovery (SWS_CM_90503)

```cpp
Result<void> DdsBinding::OfferServiceViaTopic(
    const InstanceIdentifier& instance_id,
    const ServiceInterfaceInfo& interface_info
) {
    // 1. 创建 Discovery Topic
    std::string discovery_topic_name = 
        "DDS_Discovery_" + interface_info.name;
    
    auto discovery_topic = dds::topic::Topic<ServiceDiscoveryData>(
        participant_,
        discovery_topic_name
    );
    
    // 2. 创建 DataWriter 发布服务信息
    auto writer = dds::pub::DataWriter<ServiceDiscoveryData>(
        publisher_,
        discovery_topic
    );
    
    ServiceDiscoveryData data{
        .instance_id = instance_id,
        .service_name = interface_info.name,
        .major_version = interface_info.major_version,
        .minor_version = interface_info.minor_version
    };
    
    writer.write(data);
    
    return Result<void>::FromValue();
}
```

### 4.4 SOME/IP Service Discovery (SOME/IP-SD)

> 基于 AUTOSAR SWS_CM_00403 / SOME/IP SD Protocol Specification v1.4

**SOME/IP-SD 协议**: 基于 UDP 多播的分布式服务发现协议，适用于车载以太网跨 ECU 通信。

#### 4.4.1 核心概念

**服务发现消息类型**:

- **OfferService**: 服务提供者广播服务可用消息
- **FindService**: 服务消费者请求查找服务
- **Subscribe**: 订阅事件组
- **SubscribeAck**: 订阅确认

**典型流程**:

```text
ECU-A (Provider)                    ECU-B (Consumer)
     |                                    |
     | OfferService (Multicast)          |
     |----------------------------------→ |
     |                                    |
     |          FindService (Multicast)   |
     | ←----------------------------------|
     |                                    |
     | OfferService (Unicast)            |
     |----------------------------------→ |
     |                                    |
     |             Subscribe              |
     | ←----------------------------------|
     |                                    |
     | SubscribeAck                      |
     |----------------------------------→ |
```

#### 4.4.2 OfferService 实现

```cpp
class SomeipBinding : public ITransportBinding {
public:
    Result<void> OfferService(
        const InstanceIdentifier& instance_id,
        const ServiceInterfaceInfo& interface_info
    ) override {
        // 1. 创建 SOME/IP Service Instance
        auto service = someip_runtime_->create_service(
            GetServiceId(interface_info.name),
            instance_id.instance_number,
            interface_info.major_version,
            interface_info.minor_version
        );
        
        // 2. Offer Service (触发 SD OfferService 消息)
        service->offer_service();
        
        // 3. 注册到本地槽位（可选，用于本地快速查找）
        RegisterToLocalSlot(instance_id, interface_info, {
            .binding_type = "someip",
            .endpoint = GetLocalEndpoint()  // "192.168.1.100:30500"
        });
        
        return Result<void>::FromValue();
    }
    
private:
    std::string GetLocalEndpoint() {
        // 从配置读取本地 IP:Port
        return config_.local_ip + ":" + std::to_string(config_.port);
    }
};
```

#### 4.4.3 FindService 实现（被动监听）

```cpp
Result<void> SomeipBinding::StartFindService(
    const InstanceIdentifier& instance_filter,
    std::function<void(const ServiceInstanceInfo&)> callback
) {
    // 1. 注册 SD 消息处理器
    someip_runtime_->register_sd_handler([=](
        someip::service_id_t service_id,
        someip::instance_id_t instance_id,
        someip::major_version_t major,
        someip::minor_version_t minor,
        const std::string& endpoint
    ) {
        // 2. 检查是否匹配过滤条件
        if (MatchesFilter(service_id, instance_id, instance_filter)) {
            ServiceInstanceInfo info{
                .instance_id = EncodeInstanceId(service_id, instance_id),
                .service_name = GetServiceName(service_id),
                .major_version = major,
                .minor_version = minor,
                .binding_type = TransportBindingType::kSomeip,
                .endpoint = endpoint
            };
            
            callback(info);
        }
    });
    
    // 3. 发送 FindService 请求（可选，主动查找）
    someip_runtime_->send_find_service_request(
        GetServiceId(instance_filter),
        instance_filter.instance_number
    );
    
    return Result<void>::FromValue();
}
```

**配置示例** (someip.json):

```json
{
    "unicast": "192.168.1.100",
    "multicast": "239.0.0.1",
    "port": 30500,
    "services": [
        {
            "service": "0x1234",
            "instance": "0x0001",
            "protocol": "udp",
            "port": 30501
        }
    ]
}
```

---

## 第 5 章: 跨节点（跨 ECU）服务发现架构

> 基于 SD Proxy 代理模式 + 接口一致性保证

### 5.1 架构概述与设计目标

#### 5.1.1 核心设计目标

**SD Proxy (Service Discovery Proxy)** 是一个本地代理层，提供以下核心能力：

1. ✅ **跨 ECU 服务发现能力**
   - 统一管理本地和远程服务
   - 支持 QM 和 ASIL 服务的跨 ECU 发现
   - 多协议适配（DDS / SOME/IP-SD / Discovery Server）

2. ✅ **对 Local-SD 做路由和结果同步**
   - 本地优先：本 ECU 服务通过共享内存槽位（< 500ns）
   - 远程回退：跨 ECU 服务通过 SD Proxy 查询（< 100ms）
   - 结果同步：远程服务缓存到本地（TTL 管理）

3. ✅ **屏蔽跨 ECU SD 复杂度（对应用透明）**
   - 应用使用标准 FindService/OfferService API
   - Local-SD API 永不变化
   - SD Proxy 自动处理协议转换

4. ✅ **降低网络广播/负载**
   - LRU Cache：1024 远程服务条目
   - TTL 机制：避免频繁跨 ECU 查询
   - Pull/Push 混合模式：按需查询 + 实时推送

5. ✅ **提供统一的注册、查询、缓存机制**
   - Cache Manager：LRU + TTL
   - Provider Registry：远程 ECU 注册表
   - Security Filter：白名单 + ACL

6. ✅ **支持未来切换/兼容多种协议**
   - 插件化架构：IDiscoveryProtocol 接口
   - 运行时切换：配置驱动
   - 支持协议：DDS / SOME/IP-SD / Discovery Server / 自定义

#### 5.1.2 ASIL 服务通过 QM SD-Proxy 的设计

**重要设计决策**: ASIL 服务的跨 ECU 发现通过 QM 等级的 SD-Proxy 实现

**设计原理**:

```text
┌─────────────────────────────────────────────────────────────────────┐
│              ASIL-CD Registry (ASIL-C/D 服务，本 ECU)                │
│  - Slot 0: 保留（禁止使用）                                          │
│  - Slot 1-1022: ASIL-C/D 本地服务                                   │
│  - Slot 1023: 紧急广播槽                                            │
│                                                                     │
│  注意：ASIL Registry 仅存储 **本 ECU** 的 ASIL 服务                 │
│        跨 ECU 的 ASIL 服务通过 QM SD-Proxy 发现                     │
└─────────────────────────────────────────────────────────────────────┘
                              ↓ (跨 ECU 查询)
┌─────────────────────────────────────────────────────────────────────┐
│                QM Registry (QM/ASIL-A/B 服务 + SD-Proxy)             │
│  - Slot 0: 保留（禁止使用）                                          │
│  - Slot 1: SD-Proxy 主实例 (service_id: 0x0001, QM 等级) ⭐         │
│  - Slot 512: SD-Proxy 备份实例 (service_id: 0x0200, QM 等级) ⭐     │
│  - Slot 2-511, 513-1022: 本地应用服务                               │
│  - Slot 1023: 全局广播槽                                            │
│                                                                     │
│  SD-Proxy 职责：                                                    │
│    1. 接收来自 ASIL/QM 应用的跨 ECU 查询请求                        │
│    2. 通过 DDS/SOME/IP-SD 查询远程 ECU                              │
│    3. 缓存远程服务（包括远程 ASIL 服务）                            │
│    4. 返回结果给应用（透明）                                        │
└─────────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────────┐
│                     跨 ECU 通信协议层                                │
│  - Fast DDS Discovery Server (推荐)                                │
│  - SOME/IP-SD (车载以太网标准)                                      │
│  - 自定义 Discovery Protocol                                        │
└─────────────────────────────────────────────────────────────────────┘
```

**为什么 ASIL 服务可以通过 QM SD-Proxy？**

1. **SD-Proxy 仅提供元数据查询**：
   - SD-Proxy 仅返回 ASIL 服务的**元数据**（service_id, endpoint, version）
   - 不涉及 ASIL 服务的**数据通路**（数据通过 iceoryx2/SOME/IP 直接传输）
   - 符合 ISO 26262 的安全隔离要求

2. **ASIL 服务的数据通路保持隔离**：
   - 元数据查询：QM SD-Proxy（非安全关键）
   - 数据传输：ASIL-CD Registry / ASIL 通信栈（安全关键）
   - 两者物理分离，互不影响

3. **降级策略**：
   - 如果 SD-Proxy 故障，ASIL 服务回退到静态配置
   - 静态配置通过 `slot_mapping.yaml` 预定义

**安全性验证**:

```text
ASIL 应用查询远程 ASIL 服务流程:

Application (ASIL-D)
    ↓ FindService(BrakeService)
Local-SD (查询 ASIL-CD Registry)
    ↓ 本地未找到
SD-Proxy (QM 等级，Slot 1)
    ↓ 查询远程 ECU
Fast DDS Discovery Server
    ↓ 返回远程 BrakeService 元数据
    ↓ endpoint: "remote_ecu_ip:port"
SD-Proxy 缓存 (QM 等级)
    ↓ 返回元数据给应用
Application (ASIL-D)
    ↓ 使用 endpoint 建立 ASIL-CD 数据通路
ASIL-CD 数据通路 (iceoryx2/SOME/IP)
    ✅ 刹车数据通过 ASIL-CD 通道传输
    ✅ 与 QM SD-Proxy 完全隔离
```

#### 5.1.3 架构优势

- ✅ **本地优先**: 本 ECU 服务通过共享内存槽位（< 500ns）
- ✅ **SD Proxy 透明代理**: 跨 ECU 服务通过 SD Proxy 统一管理
- ✅ **统一 API**: 应用无感知，自动路由到本地/远程
- ✅ **可选部署**: 仅需跨 ECU 通信时启用 SD Proxy
- ✅ **ASIL 兼容**: ASIL 服务元数据查询与数据通路分离

#### 5.1.4 SD-Proxy 内存使用分析（总计 ~200MB）

**内存预算分配**:

| 组件 | 内存占用 | 说明 |
|------|---------|------|
| **1. Cache Manager** | **80 MB** | LRU 缓存 1024 远程服务条目 |
| **2. Fast DDS Runtime** | **50 MB** | DDS 内部缓冲区、历史记录 |
| **3. Provider Registry** | **30 MB** | 256 远程 ECU 注册表 |
| **4. Security Filter** | **20 MB** | 白名单、ACL 规则缓存 |
| **5. TTL Manager** | **10 MB** | 时间戳、过期队列 |
| **6. 网络缓冲区** | **8 MB** | 发送/接收缓冲区 |
| **7. 其他（代码/栈）** | **2 MB** | 二进制代码、线程栈 |
| **总计** | **~200 MB** | |

**详细内存使用**:

##### 1. Cache Manager (80 MB)

```cpp
// 每个缓存条目的内存占用
struct CachedServiceEntry {
    // 基础元数据 (96 bytes)
    uint64_t service_id;              // 8 bytes
    uint64_t instance_id;             // 8 bytes
    uint32_t major_version;           // 4 bytes
    uint32_t minor_version;           // 4 bytes
    uint32_t ttl_seconds;             // 4 bytes
    std::chrono::steady_clock::time_point last_update;  // 8 bytes
    TransportBindingType binding_type; // 4 bytes
    uint32_t provider_ecu_id;         // 4 bytes
    uint32_t health_score;            // 4 bytes (0-100)
    
    // 端点信息 (256 bytes)
    std::string endpoint;  // 平均 128 bytes: "dds://domain_0/topic_name_..."
    std::string ip_address; // 45 bytes (IPv6: "2001:0db8:85a3::8a2e:0370:7334")
    uint16_t port;         // 2 bytes
    std::array<uint8_t, 80> reserved;  // 80 bytes 预留
    
    // DDS QoS 信息 (512 bytes)
    DdsQosPolicies qos;    // Reliability, Durability, History, Deadline...
    
    // 元数据 (64 KB)
    std::map<std::string, std::string> metadata;  // 平均 100 条键值对
    // 示例: {"manufacturer": "Bosch", "hw_version": "3.2", "sw_version": "1.5.3"}
    
    // IDL 类型信息 (16 KB)
    std::vector<uint8_t> type_object;  // Fast DDS TypeObject (XTypes)
    // 用于运行时类型兼容性检查
};  // 总计: ~80 KB per entry

// LRU 缓存配置
class CacheManager {
private:
    static constexpr size_t kMaxCachedServices = 1024;
    // 1024 entries × 80 KB = 80 MB
    std::unordered_map<ServiceKey, CachedServiceEntry> cache_;
    std::list<ServiceKey> lru_list_;  // LRU 淘汰链表
};
```

##### 2. Fast DDS Runtime (50 MB)

```cpp
// Fast DDS 内部内存占用
namespace fast_dds_memory {

// Discovery Database (20 MB)
struct DiscoveryDatabase {
    // 远程 Participant 数据库 (5 MB)
    std::vector<ParticipantProxyData> participants_;  // 最多 256 个 ECU
    // 每个 Participant: ~20 KB (GUID, QoS, User Data, Locators)
    
    // 远程 DataWriter 数据库 (10 MB)
    std::vector<WriterProxyData> writers_;  // 最多 512 个远程 Publisher
    // 每个 Writer: ~20 KB (GUID, Topic, Type, QoS)
    
    // 远程 DataReader 数据库 (5 MB)
    std::vector<ReaderProxyData> readers_;  // 最多 256 个远程 Subscriber
    // 每个 Reader: ~20 KB
};

// History Cache (25 MB)
struct HistoryCache {
    // Discovery 消息历史 (10 MB)
    std::deque<CacheChange> discovery_changes_;  
    // 最多 500 条 × 20 KB = 10 MB
    // 存储 DATA(p), DATA(w), DATA(r) 消息
    
    // User Data 历史 (15 MB)
    std::deque<CacheChange> user_data_changes_;
    // 最多 300 条 × 50 KB = 15 MB
    // 存储服务发现请求/响应
};

// Locator 缓存 (3 MB)
struct LocatorCache {
    std::vector<Locator_t> udp_locators_;    // 1 MB
    std::vector<Locator_t> tcp_locators_;    // 1 MB
    std::vector<Locator_t> shm_locators_;    // 1 MB
};

// 发送/接收缓冲区 (2 MB)
struct NetworkBuffers {
    std::array<uint8_t, 1024 * 1024> send_buffer_;   // 1 MB
    std::array<uint8_t, 1024 * 1024> recv_buffer_;   // 1 MB
};

}  // namespace fast_dds_memory
```

##### 3. Provider Registry (30 MB)

```cpp
struct RemoteECUInfo {
    // ECU 基础信息 (256 bytes)
    uint32_t ecu_id;
    std::string ecu_name;         // "ECU_ADAS_Front"
    std::string ip_address;       // "192.168.1.10"
    uint16_t discovery_port;
    
    // 健康状态 (128 bytes)
    struct HealthStatus {
        uint32_t rtt_ms;          // Round-Trip Time
        uint32_t packet_loss_rate; // 0-100%
        uint64_t last_heartbeat;  // Unix timestamp
        bool is_alive;
        std::chrono::steady_clock::time_point last_seen;
    } health;
    
    // 提供的服务列表 (100 KB)
    std::vector<ServiceInstanceInfo> provided_services;
    // 平均 500 个服务 × 200 bytes = 100 KB
    
    // Fast DDS Participant 信息 (20 KB)
    dds::domain::DomainParticipant participant_proxy;
    eprosima::fastrtps::rtps::GUID_t guid;
    std::vector<eprosima::fastrtps::rtps::Locator_t> locators;
};  // 总计: ~120 KB per ECU

class ProviderRegistry {
private:
    static constexpr size_t kMaxRemoteECUs = 256;
    // 256 ECUs × 120 KB = 30 MB
    std::unordered_map<uint32_t, RemoteECUInfo> registry_;
};
```

##### 4. Security Filter (20 MB)

```cpp
class SecurityFilter {
private:
    // 白名单 (10 MB)
    struct Whitelist {
        std::unordered_set<uint64_t> allowed_service_ids;  // 10000 条
        std::unordered_map<uint64_t, ServicePolicy> policies;  // 策略缓存
        // 10000 entries × 1 KB = 10 MB
    } whitelist_;
    
    // ACL 规则缓存 (8 MB)
    struct ACLCache {
        std::unordered_map<std::string, ACLEntry> ecu_acl_;  // ECU 级别
        std::unordered_map<std::string, ACLEntry> service_acl_;  // 服务级别
        // 8000 entries × 1 KB = 8 MB
    } acl_cache_;
    
    // 审计日志缓冲区 (2 MB)
    struct AuditLog {
        std::deque<AuditEntry> entries_;  // 环形缓冲区
        // 最多 4096 条 × 512 bytes = 2 MB
    } audit_log_;
};
```

##### 5. TTL Manager (10 MB)

```cpp
class TTLManager {
private:
    // 过期队列 (8 MB)
    struct ExpirationQueue {
        std::multimap<std::chrono::steady_clock::time_point, ServiceKey> queue_;
        // 时间戳 → 服务键映射
        // 最多 10000 条 × 800 bytes = 8 MB
    } expiration_queue_;
    
    // TTL 配置缓存 (2 MB)
    std::unordered_map<uint64_t, TTLPolicy> ttl_policies_;
    // 1000 服务类型 × 2 KB = 2 MB
};
```

##### 6. 网络缓冲区 (8 MB)

```cpp
class NetworkLayer {
private:
    // UDP 发送缓冲区 (2 MB)
    std::array<uint8_t, 2 * 1024 * 1024> udp_send_buffer_;
    
    // UDP 接收缓冲区 (2 MB)
    std::array<uint8_t, 2 * 1024 * 1024> udp_recv_buffer_;
    
    // TCP 连接池缓冲区 (4 MB)
    struct TCPConnectionPool {
        static constexpr size_t kMaxConnections = 64;
        std::array<TCPConnection, kMaxConnections> connections_;
        // 每个连接 64 KB × 64 = 4 MB
    } tcp_pool_;
};
```

##### 7. 其他（代码/栈）(2 MB)

```text
- 二进制代码段 (.text):        ~800 KB
- 只读数据段 (.rodata):        ~200 KB
- 全局变量 (.data + .bss):     ~500 KB
- 线程栈 (8 threads × 64 KB):  ~512 KB
  - Main thread
  - Discovery thread
  - Cache cleanup thread
  - TTL manager thread
  - Health checker thread
  - Network I/O threads (×3)
- 动态库依赖 (共享):           ~0 MB (已计入 Fast DDS)
```

**内存优化建议**:

1. **Cache Manager 优化**:
   - 减少 `metadata` 映射大小（当前 64 KB → 8 KB）
   - 使用定长数组代替 `std::map`
   - 预期节省: ~50 MB

2. **Fast DDS 配置优化**:
   ```xml
   <qos>
     <resource_limits>
       <max_samples>100</max_samples>  <!-- 默认 5000 -->
       <max_instances>50</max_instances>
     </resource_limits>
   </qos>
   ```
   - 预期节省: ~20 MB

3. **Provider Registry 优化**:
   - 减少最大 ECU 数量（256 → 64）
   - 预期节省: ~22 MB

**优化后内存占用: ~108 MB**

**SD Proxy 架构**:

```text
┌──────────────────────────────────────────────────────────────────────────┐
│                    Application Layer (ara::com / lap::com)               │
│                         FindService() / OfferService()                    │
└──────────────────────────────────────────────────────────────────────────┘
                                    ↓
┌──────────────────────────────────────────────────────────────────────────┐
│                    Local Service Discovery (本 ECU)                       │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │ QM+AB Registry (共享内存槽位)                                       │  │
│  │  - Slot 1: SD Proxy Service (固定槽位，主实例) ⭐                   │  │
│  │  - Slot 512: SD Proxy Service (备份实例，多点冗余) ⭐              │  │
│  │  - Slot 2-511: 本地应用服务                                        │  │
│  │  - Slot 513-1022: 本地应用服务                                     │  │
│  │  - Slot 1023: 广播槽                                               │  │
│  └────────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────────┘
                                    ↓
                       查询流程: Application → Local-SD → SD-Proxy
                       1. 先查询 Local Cache (本地槽位 2-1022)
                       2. 缓存命中 → 返回结果 (< 500ns)
                       3. 缓存未命中 → 通过 Slot 1/512 查询 SD Proxy
                       4. SD Proxy 跨 ECU 查询
                       5. 结果写入 SD Proxy Local Cache
                       6. 返回结果给应用
                                    ↓
┌──────────────────────────────────────────────────────────────────────────┐
│                    SD Proxy 服务 (lap_sd_proxy)                          │
│                    固定槽位: Slot 1 (主) + Slot 512 (备)                 │
│                    安全等级: QM (非安全关键服务)                          │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │ 1. Cache Manager (缓存管理器)                                      │  │
│  │    - LRU Cache: 远程服务元数据缓存                                 │  │
│  │    - 容量: 1024 个远程服务条目                                     │  │
│  │    - 查询延迟: < 1ms (内存查找)                                    │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │ 2. TTL Manager (生存时间管理器)                                    │  │
│  │    - 默认 TTL: 60s (可配置)                                        │  │
│  │    - 后台清理线程: 5s 周期检查过期条目                             │  │
│  │    - 主动刷新: 服务上线/下线事件触发                               │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │ 3. Cross-ECU Router (跨 ECU 路由器)                                │  │
│  │    - 本地优先: 优先返回本 ECU 服务                                 │  │
│  │    - 远程回退: 未找到则查询其他 ECU                                │  │
│  │    - 多 ECU 聚合: 合并多个 ECU 的服务实例                          │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │ 4. Provider Registry (服务提供者注册表)                            │  │
│  │    - 跟踪远程 ECU 的服务提供者                                     │  │
│  │    - 维护 ECU → Services 映射                                      │  │
│  │    - 心跳检测: 检测远程 ECU 存活状态                               │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │ 5. Pull/Push Agent (主动拉取/推送代理)                             │  │
│  │    - Pull Mode: 主动查询远程 ECU (按需)                            │  │
│  │    - Push Mode: 订阅远程 ECU 服务变化 (实时)                       │  │
│  │    - 混合模式: Pull (首次) + Push (持续更新)                       │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │ 6. Security Filter (安全过滤器)                                    │  │
│  │    - 白名单机制: 仅允许配置的 ECU 连接                             │  │
│  │    - 服务访问控制: 基于 service_id 的 ACL                          │  │
│  │    - 防注入攻击: 验证远程服务元数据合法性                          │  │
│  │    - 审计日志: 记录跨 ECU 服务发现事件                             │  │
│  └────────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────────┘
                                    ↓
┌──────────────────────────────────────────────────────────────────────────┐
│              底层通信接口 (DDS Discovery Server API)                      │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │ Fast DDS Discovery Server                                          │  │
│  │  - 集中式服务发现 (可选 HA 双主)                                   │  │
│  │  - IP: 192.168.1.10:11811 (主) + 192.168.1.11:11811 (备)           │  │
│  │  - DomainParticipant 发现协议                                      │  │
│  │  - 服务元数据同步 (USER_DATA QoS)                                  │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │ 替代方案: SOME/IP-SD (车载以太网标准)                              │  │
│  │  - UDP 多播: 239.0.0.1:30490                                       │  │
│  │  - OfferService / FindService 消息                                 │  │
│  └────────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────────┘
```

**性能目标**:

| 服务类型 | 发现延迟 | 查询延迟 | 适用场景 |
|----------|---------|---------|---------|
| **本地服务** | 0ns (编译期) | < 500ns | 同 ECU 高频通信 |
| **SD Proxy 缓存命中** | - | < 1ms | 跨 ECU 高频查询 |
| **SD Proxy 缓存未命中** | < 100ms | < 100ms | 首次跨 ECU 发现 |

### 5.2 SD Proxy 服务设计

> 固定槽位: Slot 1 (主实例) + Slot 512 (备份实例)

#### 5.2.1 SD Proxy 槽位分配策略

**槽位选择原则**:

- ✅ **Slot 1**: SD Proxy 主实例（QM Registry 第一个有效槽位，0 保留）
  - 固定位置，应用无需配置即可定位
  - service_id: `0x0001` (ServiceDiscoveryProxy)
  - instance_id: `0x00010001` (主实例)
  
- ✅ **Slot 512**: SD Proxy 备份实例（中间位置，冗余设计）
  - 多点冗余，主实例故障时自动切换
  - service_id: `0x0200` (映射到 Slot 512)
  - instance_id: `0x00010002` (备份实例)
  - 心跳同步: 主备实例通过共享内存心跳互检

**槽位注册结构** (Slot 1 示例):

```cpp
// SD Proxy 注册到 Slot 1
ServiceSlot slot_1 = {
    .sequence = 0,  // seqlock 序列号
    
    // 服务标识
    .service_id = 0x0001,  // ServiceDiscoveryProxy
    .instance_id = 0x00010001,  // 主实例
    .major_version = 1,
    .minor_version = 0,
    
    // iceoryx2 端点（SD Proxy 内部通信）
    .endpoint_offset = 0x1000,  // SD Proxy 共享内存偏移
    .mempool_base_ptr = 0x7f1234567000,
    .qos_profile = 0x00010100,  // reliable, history=1, max_subscribers=0
    .subscriber_count = 0,  // 被应用查询次数（统计用）
    
    // 绑定类型
    .binding_type = "sd_proxy",  // 特殊标识，区分普通服务
    .endpoint[0] = '\0',  // 不使用字符串端点
    
    // 生命周期
    .last_heartbeat_ns = GetMonotonicTimeNs(),
    .heartbeat_interval_ms = 1000,
    .status = 1,  // 活跃
    .owner_pid = getpid(),
    
    // 元数据
    .metadata = R"({"role":"sd_proxy","slot":"primary","version":"1.0"})",
    
    // 安全
    .crc32 = CalculateCRC32(&slot_1),
    .write_counter = 0
};
```

#### 5.2.2 SD Proxy 核心组件实现

##### 组件 1: Cache Manager (缓存管理器)

```cpp
namespace lap::com::sd_proxy {

class CacheManager {
public:
    struct CacheEntry {
        ServiceInstanceInfo service_info;
        std::chrono::steady_clock::time_point expiry;  // TTL 过期时间
        uint32_t hit_count;  // 缓存命中次数（统计）
        std::string source_ecu;  // 来源 ECU ID
    };
    
    // 查询缓存（< 1ms）
    std::optional<ServiceInstanceInfo> Find(
        const InstanceIdentifier& instance_filter
    ) {
        std::shared_lock lock(cache_mutex_);
        
        auto key = MakeCacheKey(instance_filter);
        auto it = cache_.find(key);
        
        if (it == cache_.end()) {
            stats_.cache_misses++;
            return std::nullopt;  // 缓存未命中
        }
        
        // 检查 TTL
        if (std::chrono::steady_clock::now() > it->second.expiry) {
            stats_.cache_expired++;
            return std::nullopt;  // 已过期
        }
        
        // 缓存命中
        it->second.hit_count++;
        stats_.cache_hits++;
        
        LOG_DEBUG("Cache hit: service_id={:#x}, source_ecu={}, hit_count={}",
                  it->second.service_info.service_id,
                  it->second.source_ecu,
                  it->second.hit_count);
        
        return it->second.service_info;
    }
    
    // 插入缓存
    void Insert(
        const ServiceInstanceInfo& info,
        std::chrono::seconds ttl,
        const std::string& source_ecu
    ) {
        std::unique_lock lock(cache_mutex_);
        
        auto key = MakeCacheKey(info.instance_id);
        
        CacheEntry entry{
            .service_info = info,
            .expiry = std::chrono::steady_clock::now() + ttl,
            .hit_count = 0,
            .source_ecu = source_ecu
        };
        
        cache_[key] = entry;
        
        // LRU 淘汰（保留最近 1024 个）
        if (cache_.size() > max_cache_size_) {
            EvictLRU();
        }
        
        LOG_INFO("Cache inserted: service_id={:#x}, source_ecu={}, ttl={}s",
                 info.service_id, source_ecu, ttl.count());
    }
    
    // 清除指定服务缓存（服务下线时调用）
    void Invalidate(const InstanceIdentifier& instance_id) {
        std::unique_lock lock(cache_mutex_);
        
        auto key = MakeCacheKey(instance_id);
        auto it = cache_.find(key);
        
        if (it != cache_.end()) {
            LOG_INFO("Cache invalidated: service_id={:#x}, source_ecu={}",
                     it->second.service_info.service_id,
                     it->second.source_ecu);
            cache_.erase(it);
            stats_.cache_invalidations++;
        }
    }
    
    // 获取缓存统计
    struct CacheStats {
        uint64_t cache_hits = 0;
        uint64_t cache_misses = 0;
        uint64_t cache_expired = 0;
        uint64_t cache_invalidations = 0;
        
        double HitRate() const {
            uint64_t total = cache_hits + cache_misses;
            return total > 0 ? static_cast<double>(cache_hits) / total : 0.0;
        }
    };
    
    CacheStats GetStats() const {
        return stats_;
    }
    
private:
    std::unordered_map<std::string, CacheEntry> cache_;
    mutable std::shared_mutex cache_mutex_;
    size_t max_cache_size_ = 1024;  // 最多缓存 1024 个远程服务
    CacheStats stats_;
    
    std::string MakeCacheKey(const InstanceIdentifier& id) const {
        return std::to_string(id.service_id) + ":" + std::to_string(id.instance_id);
    }
    
    void EvictLRU() {
        // 淘汰命中次数最少的条目
        auto it = std::min_element(cache_.begin(), cache_.end(),
            [](const auto& a, const auto& b) {
                return a.second.hit_count < b.second.hit_count;
            });
        
        if (it != cache_.end()) {
            LOG_DEBUG("LRU evicted: service_id={:#x}, hit_count={}",
                      it->second.service_info.service_id,
                      it->second.hit_count);
            cache_.erase(it);
        }
    }
};

} // namespace lap::com::sd_proxy
```

##### 组件 2: TTL Manager (生存时间管理器)

```cpp
class TTLManager {
public:
    void Start(std::shared_ptr<CacheManager> cache_manager) {
        cache_manager_ = cache_manager;
        running_ = true;
        
        // 启动后台清理线程
        cleanup_thread_ = std::thread([this]() {
            while (running_) {
                // 每 5 秒检查一次过期条目
                std::this_thread::sleep_for(std::chrono::seconds(5));
                
                CleanupExpiredEntries();
            }
        });
    }
    
    void Stop() {
        running_ = false;
        if (cleanup_thread_.joinable()) {
            cleanup_thread_.join();
        }
    }
    
    // 设置服务 TTL (可配置)
    void SetServiceTTL(uint64_t service_id, std::chrono::seconds ttl) {
        std::unique_lock lock(ttl_config_mutex_);
        ttl_config_[service_id] = ttl;
    }
    
    // 获取服务 TTL (默认 60s)
    std::chrono::seconds GetServiceTTL(uint64_t service_id) const {
        std::shared_lock lock(ttl_config_mutex_);
        
        auto it = ttl_config_.find(service_id);
        if (it != ttl_config_.end()) {
            return it->second;
        }
        
        return default_ttl_;  // 默认 60 秒
    }
    
private:
    void CleanupExpiredEntries() {
        // 缓存内部已处理过期检查（Find 时验证 TTL）
        // 这里可选：主动遍历清理过期条目以释放内存
        
        LOG_DEBUG("TTL cleanup cycle completed");
    }
    
    std::shared_ptr<CacheManager> cache_manager_;
    std::thread cleanup_thread_;
    std::atomic<bool> running_{false};
    
    std::unordered_map<uint64_t, std::chrono::seconds> ttl_config_;
    mutable std::shared_mutex ttl_config_mutex_;
    
    std::chrono::seconds default_ttl_{60};  // 默认 60 秒
};
```

##### 组件 3: Cross-ECU Router (跨 ECU 路由器)

```cpp
class CrossECURouter {
public:
    Result<std::vector<ServiceInstanceInfo>> FindService(
        const InstanceIdentifier& instance_filter
    ) {
        std::vector<ServiceInstanceInfo> results;
        
        // ========== Step 1: 本地优先查询 ==========
        auto local_results = local_registry_->FindService(instance_filter);
        if (local_results && !local_results.Value().empty()) {
            // 本地找到，直接返回（< 500ns）
            LOG_DEBUG("Local service found, skip remote query");
            return local_results;
        }
        
        // ========== Step 2: 查询 SD Proxy 缓存 ==========
        auto cached = cache_manager_->Find(instance_filter);
        if (cached) {
            // 缓存命中（< 1ms）
            LOG_DEBUG("SD Proxy cache hit");
            results.push_back(*cached);
            return Result<>::FromValue(results);
        }
        
        // ========== Step 3: 跨 ECU 查询 ==========
        LOG_INFO("Cache miss, querying remote ECUs");
        
        auto remote_results = QueryRemoteECUs(instance_filter);
        if (!remote_results.empty()) {
            // 写入缓存
            for (const auto& info : remote_results) {
                cache_manager_->Insert(
                    info,
                    ttl_manager_->GetServiceTTL(info.service_id),
                    info.source_ecu_id  // 记录来源 ECU
                );
                
                results.push_back(info);
            }
        }
        
        // ========== Step 4: 安全过滤 ==========
        results = security_filter_->FilterResults(results);
        
        return Result<>::FromValue(results);
    }
    
private:
    std::vector<ServiceInstanceInfo> QueryRemoteECUs(
        const InstanceIdentifier& instance_filter
    ) {
        std::vector<ServiceInstanceInfo> results;
        
        // 通过 Pull/Push Agent 查询所有远程 ECU
        for (const auto& ecu_id : provider_registry_->GetKnownECUs()) {
            auto ecu_results = pull_push_agent_->QueryECU(ecu_id, instance_filter);
            
            for (auto& info : ecu_results) {
                info.source_ecu_id = ecu_id;  // 标记来源
                info.is_remote = true;
                results.push_back(info);
            }
        }
        
        return results;
    }
    
    std::shared_ptr<CacheManager> cache_manager_;
    std::shared_ptr<TTLManager> ttl_manager_;
    std::shared_ptr<ProviderRegistry> provider_registry_;
    std::shared_ptr<PullPushAgent> pull_push_agent_;
    std::shared_ptr<SecurityFilter> security_filter_;
    std::shared_ptr<LocalRegistry> local_registry_;
};
```

##### 组件 4: Provider Registry (服务提供者注册表)

```cpp
class ProviderRegistry {
public:
    struct ECUInfo {
        std::string ecu_id;  // ECU 唯一标识
        std::string ip_address;
        uint16_t port;
        std::chrono::steady_clock::time_point last_heartbeat;
        std::vector<uint64_t> provided_services;  // 该 ECU 提供的服务列表
        bool is_alive;
    };
    
    // 注册远程 ECU
    void RegisterECU(const ECUInfo& ecu_info) {
        std::unique_lock lock(registry_mutex_);
        
        ecu_registry_[ecu_info.ecu_id] = ecu_info;
        
        LOG_INFO("ECU registered: id={}, ip={}:{}, services={}",
                 ecu_info.ecu_id,
                 ecu_info.ip_address,
                 ecu_info.port,
                 ecu_info.provided_services.size());
    }
    
    // 更新 ECU 心跳
    void UpdateHeartbeat(const std::string& ecu_id) {
        std::unique_lock lock(registry_mutex_);
        
        auto it = ecu_registry_.find(ecu_id);
        if (it != ecu_registry_.end()) {
            it->second.last_heartbeat = std::chrono::steady_clock::now();
            it->second.is_alive = true;
        }
    }
    
    // 检测 ECU 存活状态
    void CheckECUHealth() {
        std::unique_lock lock(registry_mutex_);
        
        auto now = std::chrono::steady_clock::now();
        constexpr auto timeout = std::chrono::seconds(10);
        
        for (auto& [ecu_id, ecu_info] : ecu_registry_) {
            if (now - ecu_info.last_heartbeat > timeout) {
                if (ecu_info.is_alive) {
                    LOG_WARN("ECU timeout: id={}, last_heartbeat={} seconds ago",
                             ecu_id,
                             std::chrono::duration_cast<std::chrono::seconds>(
                                 now - ecu_info.last_heartbeat).count());
                    
                    ecu_info.is_alive = false;
                    
                    // 清除该 ECU 的所有服务缓存
                    InvalidateECUServices(ecu_id);
                }
            }
        }
    }
    
    // 获取所有已知 ECU
    std::vector<std::string> GetKnownECUs() const {
        std::shared_lock lock(registry_mutex_);
        
        std::vector<std::string> ecu_ids;
        for (const auto& [ecu_id, ecu_info] : ecu_registry_) {
            if (ecu_info.is_alive) {
                ecu_ids.push_back(ecu_id);
            }
        }
        return ecu_ids;
    }
    
private:
    void InvalidateECUServices(const std::string& ecu_id) {
        auto it = ecu_registry_.find(ecu_id);
        if (it != ecu_registry_.end()) {
            for (uint64_t service_id : it->second.provided_services) {
                // 通知 Cache Manager 清除该 ECU 的服务
                // cache_manager_->InvalidateByECU(ecu_id);
            }
        }
    }
    
    std::unordered_map<std::string, ECUInfo> ecu_registry_;
    mutable std::shared_mutex registry_mutex_;
};
```

##### 组件 5: Pull/Push Agent (主动拉取/推送代理)

```cpp
class PullPushAgent {
public:
    // Pull Mode: 主动查询远程 ECU
    std::vector<ServiceInstanceInfo> QueryECU(
        const std::string& ecu_id,
        const InstanceIdentifier& instance_filter
    ) {
        // 通过 DDS Discovery Server API 查询
        auto participant = GetDomainParticipant(ecu_id);
        if (!participant) {
            LOG_ERROR("Failed to get DomainParticipant for ECU: {}", ecu_id);
            return {};
        }
        
        // 读取远程 ECU 的服务元数据（通过 DDS Topic）
        auto reader = GetServiceMetadataReader(ecu_id);
        std::vector<ServiceInstanceInfo> results;
        
        dds::sub::LoanedSamples<ServiceMetadata> samples = reader.take();
        for (const auto& sample : samples) {
            if (sample.info().valid() && 
                MatchesFilter(sample.data(), instance_filter)) {
                
                ServiceInstanceInfo info{
                    .instance_id = sample.data().instance_id,
                    .service_name = GetServiceName(sample.data().service_id),
                    .endpoint = sample.data().endpoint,
                    .major_version = sample.data().major_version,
                    .minor_version = sample.data().minor_version,
                    .is_remote = true,
                    .source_ecu_id = ecu_id
                };
                
                results.push_back(info);
            }
        }
        
        LOG_DEBUG("Pull query: ecu={}, found {} services", ecu_id, results.size());
        return results;
    }
    
    // Push Mode: 订阅远程 ECU 服务变化
    void SubscribeECUUpdates(const std::string& ecu_id) {
        // 设置 DDS DataReader Listener
        auto reader = GetServiceMetadataReader(ecu_id);
        
        reader.set_listener([this, ecu_id](auto& reader) {
            dds::sub::LoanedSamples<ServiceMetadata> samples = reader.take();
            
            for (const auto& sample : samples) {
                if (sample.info().valid()) {
                    OnServiceUpdate(ecu_id, sample.data());
                }
            }
        });
        
        LOG_INFO("Subscribed to ECU updates: {}", ecu_id);
    }
    
private:
    void OnServiceUpdate(const std::string& ecu_id, const ServiceMetadata& metadata) {
        // 服务更新，刷新缓存
        ServiceInstanceInfo info{
            .instance_id = metadata.instance_id,
            .service_name = GetServiceName(metadata.service_id),
            .endpoint = metadata.endpoint,
            .major_version = metadata.major_version,
            .minor_version = metadata.minor_version,
            .is_remote = true,
            .source_ecu_id = ecu_id
        };
        
        cache_manager_->Insert(
            info,
            ttl_manager_->GetServiceTTL(metadata.service_id),
            ecu_id
        );
        
        LOG_INFO("Service update pushed: service_id={:#x}, ecu={}", 
                 metadata.service_id, ecu_id);
    }
    
    std::shared_ptr<CacheManager> cache_manager_;
    std::shared_ptr<TTLManager> ttl_manager_;
};
```

##### 组件 6: Security Filter (安全过滤器)

```cpp
class SecurityFilter {
public:
    struct SecurityPolicy {
        std::set<std::string> allowed_ecus;  // ECU 白名单
        std::map<uint64_t, std::set<std::string>> service_acl;  // 服务访问控制
        bool enable_audit_log;  // 审计日志开关
    };
    
    void LoadPolicy(const SecurityPolicy& policy) {
        std::unique_lock lock(policy_mutex_);
        policy_ = policy;
        
        LOG_INFO("Security policy loaded: allowed_ecus={}, service_acl={}",
                 policy_.allowed_ecus.size(),
                 policy_.service_acl.size());
    }
    
    // 过滤查询结果
    std::vector<ServiceInstanceInfo> FilterResults(
        const std::vector<ServiceInstanceInfo>& results
    ) {
        std::shared_lock lock(policy_mutex_);
        
        std::vector<ServiceInstanceInfo> filtered;
        
        for (const auto& info : results) {
            // 1. 检查 ECU 白名单
            if (!policy_.allowed_ecus.empty()) {
                if (policy_.allowed_ecus.find(info.source_ecu_id) == 
                    policy_.allowed_ecus.end()) {
                    
                    LOG_WARN("ECU not in whitelist: {}", info.source_ecu_id);
                    AuditLog("ECU_BLOCKED", info);
                    continue;  // 拦截
                }
            }
            
            // 2. 检查服务 ACL
            auto acl_it = policy_.service_acl.find(info.service_id);
            if (acl_it != policy_.service_acl.end()) {
                if (acl_it->second.find(info.source_ecu_id) == acl_it->second.end()) {
                    LOG_WARN("Service access denied: service_id={:#x}, ecu={}",
                             info.service_id, info.source_ecu_id);
                    AuditLog("SERVICE_ACCESS_DENIED", info);
                    continue;  // 拦截
                }
            }
            
            // 3. 验证服务元数据合法性
            if (!ValidateServiceMetadata(info)) {
                LOG_ERROR("Invalid service metadata: service_id={:#x}", info.service_id);
                AuditLog("INVALID_METADATA", info);
                continue;  // 拦截
            }
            
            // 通过所有检查
            filtered.push_back(info);
            AuditLog("SERVICE_ALLOWED", info);
        }
        
        LOG_DEBUG("Security filter: {} -> {} services",
                  results.size(), filtered.size());
        
        return filtered;
    }
    
private:
    bool ValidateServiceMetadata(const ServiceInstanceInfo& info) {
        // 验证 service_id 合法性
        if (info.service_id == 0 || info.service_id == 0xFFFF) {
            return false;  // 非法 ID
        }
        
        // 验证版本号
        if (info.major_version == 0 && info.minor_version == 0) {
            return false;  // 非法版本
        }
        
        // 验证 endpoint 格式
        if (info.endpoint.empty() || info.endpoint.length() > 48) {
            return false;  // 非法端点
        }
        
        return true;
    }
    
    void AuditLog(const std::string& event, const ServiceInstanceInfo& info) {
        if (!policy_.enable_audit_log) {
            return;
        }
        
        LOG_INFO("[AUDIT] event={}, service_id={:#x}, ecu={}, endpoint={}",
                 event, info.service_id, info.source_ecu_id, info.endpoint);
        
        // 可选：写入审计日志文件
        // audit_logger_.Log(event, info);
    }
    
    SecurityPolicy policy_;
    mutable std::shared_mutex policy_mutex_;
};
```

#### 5.2.3 Fast DDS Discovery Server 集成

> 底层通信接口：DDS Discovery Server API

##### Discovery Server 部署模型

**拓扑选择**:

```text
集中式 Discovery Server (推荐用于车载以太网)
┌────────────────────────────────────────────────┐
│          Discovery Server (可选 HA 双主)        │
│          IP: 192.168.1.10:11811                │
└────────────────────────────────────────────────┘
         ↑          ↑          ↑          ↑
         │          │          │          │
    ┌────┴───┐  ┌──┴────┐  ┌──┴────┐  ┌──┴────┐
    │ ECU-A  │  │ ECU-B │  │ ECU-C │  │ ECU-D │
    │SD Proxy│  │SD Proxy│ │SD Proxy│ │SD Proxy│
    │Slot1/512│ │Slot1/512││Slot1/512││Slot1/512│
    └────────┘  └───────┘  └───────┘  └───────┘
```

##### SD Proxy 与 DDS 集成

```cpp
// SD Proxy 内部使用 DDS Discovery Server API
class SDProxyDDSClient {
public:
    void Initialize(const DDSConfig& config) {
        // 1. 创建 Client DomainParticipant
        eprosima::fastdds::dds::DomainParticipantQos pqos;
        pqos.wire_protocol().builtin.discovery_config.discoveryProtocol = 
            eprosima::fastrtps::rtps::DiscoveryProtocol::CLIENT;
        
        // 2. 配置连接到 Discovery Server
        eprosima::fastrtps::rtps::Locator_t server_locator;
        eprosima::fastrtps::rtps::IPLocator::setIPv4(
            server_locator, 
            config.server_ip  // "192.168.1.10"
        );
        server_locator.port = config.server_port;  // 11811
        
        pqos.wire_protocol().builtin.discovery_config.m_DiscoveryServers
            .push_back(server_locator);
        
        // 3. 创建 DomainParticipant
        participant_ = eprosima::fastdds::dds::DomainParticipantFactory::get_instance()
            ->create_participant(0, pqos);
        
        // 4. 设置发现监听器
        participant_->set_listener(&discovery_listener_);
        
        LOG_INFO("SD Proxy DDS client initialized");
    }
    
    // 查询远程 ECU 服务
    std::vector<ServiceInstanceInfo> QueryRemoteServices(
        const InstanceIdentifier& instance_filter
    ) {
        // 读取 DDS Topic 数据
        auto reader = GetServiceMetadataReader();
        std::vector<ServiceInstanceInfo> results;
        
        dds::sub::LoanedSamples<ServiceMetadata> samples = reader.take();
        for (const auto& sample : samples) {
            if (sample.info().valid() && 
                MatchesFilter(sample.data(), instance_filter)) {
                
                ServiceInstanceInfo info = ParseServiceMetadata(sample.data());
                results.push_back(info);
            }
        }
        
        return results;
    }
    
private:
    class DiscoveryListener : public eprosima::fastdds::dds::DomainParticipantListener {
        void on_participant_discovery(
            eprosima::fastdds::dds::DomainParticipant* participant,
            eprosima::fastrtps::rtps::ParticipantDiscoveryInfo&& info
        ) override {
            if (info.status == eprosima::fastrtps::rtps::ParticipantDiscoveryInfo::DISCOVERED_PARTICIPANT) {
                std::string ecu_id = ExtractECUId(info.info.m_guid);
                
                LOG_INFO("Discovered remote ECU: {}", ecu_id);
                
                // 更新 Provider Registry
                provider_registry_->RegisterECU({
                    .ecu_id = ecu_id,
                    .ip_address = ExtractIPAddress(info),
                    .port = ExtractPort(info),
                    .last_heartbeat = std::chrono::steady_clock::now(),
                    .is_alive = true
                });
                
                // 订阅该 ECU 的服务更新
                pull_push_agent_->SubscribeECUUpdates(ecu_id);
            }
        }
    };
    
    eprosima::fastdds::dds::DomainParticipant* participant_ = nullptr;
    DiscoveryListener discovery_listener_;
};
```

#### 5.2.4 SD Proxy 主进程实现

```cpp
// lap_sd_proxy 主进程
int main(int argc, char* argv[]) {
    // 1. 初始化配置
    SDProxyConfig config = LoadConfig("/etc/lap/sd_proxy.yaml");
    
    // 2. 创建核心组件
    auto cache_manager = std::make_shared<CacheManager>();
    auto ttl_manager = std::make_shared<TTLManager>();
    auto provider_registry = std::make_shared<ProviderRegistry>();
    auto pull_push_agent = std::make_shared<PullPushAgent>();
    auto security_filter = std::make_shared<SecurityFilter>();
    auto cross_ecu_router = std::make_shared<CrossECURouter>();
    
    // 3. 注册 SD Proxy 到固定槽位
    auto slot_manager = std::make_shared<SlotManager>();
    
    // 注册主实例到 Slot 1
    slot_manager->RegisterService({
        .slot_index = 1,
        .service_id = 0x0001,  // ServiceDiscoveryProxy
        .instance_id = 0x00010001,
        .binding_type = "sd_proxy",
        .metadata = R"({"role":"primary","version":"1.0"})"
    });
    
    // 注册备份实例到 Slot 512
    slot_manager->RegisterService({
        .slot_index = 512,
        .service_id = 0x0200,
        .instance_id = 0x00010002,
        .binding_type = "sd_proxy",
        .metadata = R"({"role":"backup","version":"1.0"})"
    });
    
    // 4. 启动 DDS 客户端（如果启用）
    if (config.enable_remote_discovery) {
        auto dds_client = std::make_shared<SDProxyDDSClient>();
        dds_client->Initialize(config.dds);
    }
    
    // 5. 启动 TTL 清理线程
    ttl_manager->Start(cache_manager);
    
    // 6. 启动健康检查线程
    std::thread health_check_thread([provider_registry]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            provider_registry->CheckECUHealth();
        }
    });
    
    // 7. 主循环：处理应用查询请求
    while (true) {
        // 通过 iceoryx2 接收应用的 FindService 请求
        // 调用 cross_ecu_router->FindService()
        // 返回结果给应用
        
        ProcessFindServiceRequests(cross_ecu_router);
    }
    
    return 0;
}
```

### 5.3 典型查询流程（Application → Local-SD → SD-Proxy）

#### 5.3.1 完整查询流程

```text
┌─────────────────────────────────────────────────────────────────────────┐
│                      Application (lap::com::Proxy)                       │
│                       FindService(RadarService)                          │
└─────────────────────────────────────────────────────────────────────────┘
                                    ↓
                    Step 1: 查询本地共享内存槽位 (< 500ns)
                                    ↓
┌─────────────────────────────────────────────────────────────────────────┐
│                 Local Service Discovery (QM Registry)                    │
│  遍历 Slot 2-1022 (跳过 Slot 1/512，它们是 SD Proxy 自己)               │
│  - seqlock 无锁读取                                                     │
│  - 匹配 service_id / instance_id                                        │
└─────────────────────────────────────────────────────────────────────────┘
                                    ↓
                          本地找到？
                    ┌──────┴──────┐
                   YES            NO
                    │              │
                    ↓              ↓
              返回结果      Step 2: 查询 SD Proxy
              (< 500ns)           (Slot 1 或 Slot 512)
                                    ↓
┌─────────────────────────────────────────────────────────────────────────┐
│                      SD Proxy Service (Slot 1/512)                       │
│  通过 iceoryx2 接收 FindService 请求                                     │
└─────────────────────────────────────────────────────────────────────────┘
                                    ↓
                    Step 3: 查询 SD Proxy Cache (< 1ms)
                                    ↓
┌─────────────────────────────────────────────────────────────────────────┐
│                         Cache Manager                                    │
│  LRU Cache: 1024 个远程服务条目                                          │
│  - 内存查找: std::unordered_map                                         │
│  - TTL 验证: 检查过期时间                                               │
└─────────────────────────────────────────────────────────────────────────┘
                                    ↓
                          缓存命中？
                    ┌──────┴──────┐
                   YES            NO
                    │              │
                    ↓              ↓
              返回缓存结果    Step 4: 跨 ECU 查询
              (< 1ms)               (< 100ms)
                                    ↓
┌─────────────────────────────────────────────────────────────────────────┐
│                         Cross-ECU Router                                 │
│  - Pull/Push Agent 查询远程 ECU                                          │
│  - 通过 DDS Discovery Server                                             │
│  - 聚合多个 ECU 结果                                                     │
└─────────────────────────────────────────────────────────────────────────┘
                                    ↓
┌─────────────────────────────────────────────────────────────────────────┐
│                      DDS Discovery Server                                │
│  - 读取远程 ECU 的 DomainParticipant USER_DATA                           │
│  - 查询 ServiceMetadata Topic                                            │
│  - 返回匹配的服务实例                                                    │
└─────────────────────────────────────────────────────────────────────────┘
                                    ↓
                    Step 5: Security Filter (安全过滤)
                                    ↓
┌─────────────────────────────────────────────────────────────────────────┐
│                         Security Filter                                  │
│  - ECU 白名单检查                                                        │
│  - 服务 ACL 验证                                                         │
│  - 元数据合法性验证                                                      │
│  - 审计日志记录                                                          │
└─────────────────────────────────────────────────────────────────────────┘
                                    ↓
                    Step 6: 写入 SD Proxy Cache
                                    ↓
┌─────────────────────────────────────────────────────────────────────────┐
│                         Cache Manager                                    │
│  - 插入远程服务到 LRU Cache                                              │
│  - 设置 TTL (默认 60s)                                                   │
│  - 记录 source_ecu_id                                                    │
└─────────────────────────────────────────────────────────────────────────┘
                                    ↓
                    Step 7: 返回结果给应用
                                    ↓
┌─────────────────────────────────────────────────────────────────────────┐
│                      Application (lap::com::Proxy)                       │
│  - 接收 ServiceHandleContainer                                           │
│  - 创建 Proxy 实例                                                       │
│  - 开始使用服务                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

#### 5.3.2 代码示例

**应用侧代码** (无感知本地/远程):

```cpp
// 应用代码完全无感知 SD Proxy 的存在
auto runtime = lap::com::Runtime::GetInstance();

// FindService 自动路由到本地或 SD Proxy
auto result = runtime->FindService<RadarServiceProxy>(
    lap::com::InstanceSpecifier("RadarService/Instance1")
);

if (result) {
    auto handles = result.Value();
    if (!handles.empty()) {
        // 创建 Proxy（本地或远程服务，透明）
        auto proxy = RadarServiceProxy::Create(handles[0]);
        
        // 使用服务
        proxy->GetObjects().Subscribe([](auto objects) {
            // 处理雷达目标数据
        });
    }
}
```

**Runtime 内部路由逻辑**:

```cpp
// Runtime::FindService 实现
template<typename ProxyType>
Result<ServiceHandleContainer<ProxyType::HandleType>> FindService(
    const InstanceSpecifier& instance_specifier
) {
    ServiceHandleContainer<ProxyType::HandleType> handles;
    
    // ========== Step 1: 查询本地槽位 (Slot 2-1022，跳过 1/512) ==========
    auto local_results = local_registry_->FindService(instance_specifier);
    if (local_results && !local_results.Value().empty()) {
        // 本地找到，直接返回
        for (const auto& info : local_results.Value()) {
            handles.push_back(CreateLocalHandle<ProxyType>(info));
        }
        
        LOG_DEBUG("FindService: {} local services found", handles.size());
        return Result<>::FromValue(handles);
    }
    
    // ========== Step 2: 查询 SD Proxy (如果启用远程发现) ==========
    if (config_.enable_remote_discovery) {
        // 通过 Slot 1 (主) 或 Slot 512 (备) 查询 SD Proxy
        auto sd_proxy_results = QuerySDProxy(instance_specifier);
        
        if (sd_proxy_results && !sd_proxy_results.Value().empty()) {
            for (const auto& info : sd_proxy_results.Value()) {
                handles.push_back(CreateRemoteHandle<ProxyType>(info));
            }
            
            LOG_DEBUG("FindService: {} remote services found via SD Proxy",
                      handles.size());
        }
    }
    
    // ========== Step 3: 返回合并结果 ==========
    if (handles.empty()) {
        return Result<>::FromError(ComErrc::kServiceNotFound);
    }
    
    return Result<>::FromValue(handles);
}

// 查询 SD Proxy 服务
Result<std::vector<ServiceInstanceInfo>> QuerySDProxy(
    const InstanceSpecifier& instance_specifier
) {
    // 1. 先尝试 Slot 1 (主实例)
    auto primary_slot = GetSlot(1);
    if (primary_slot && primary_slot->status == 1) {
        return QuerySDProxyViaSlot(1, instance_specifier);
    }
    
    // 2. 回退到 Slot 512 (备份实例)
    auto backup_slot = GetSlot(512);
    if (backup_slot && backup_slot->status == 1) {
        LOG_WARN("SD Proxy primary down, using backup (Slot 512)");
        return QuerySDProxyViaSlot(512, instance_specifier);
    }
    
    // 3. SD Proxy 不可用
    LOG_ERROR("SD Proxy unavailable (both Slot 1 and 512 down)");
    return Result<>::FromError(ComErrc::kSDProxyUnavailable);
}

Result<std::vector<ServiceInstanceInfo>> QuerySDProxyViaSlot(
    uint32_t slot_index,
    const InstanceSpecifier& instance_specifier
) {
    // 通过 iceoryx2 向 SD Proxy 发送查询请求
    auto sd_proxy_endpoint = GetSDProxyEndpoint(slot_index);
    
    // 发送 FindServiceRequest
    FindServiceRequest request{
        .instance_specifier = instance_specifier,
        .timeout_ms = 100  // 跨 ECU 查询超时 100ms
    };
    
    auto response = sd_proxy_endpoint->SendRequest(request);
    
    if (response) {
        return Result<>::FromValue(response->services);
    }
    
    return Result<>::FromError(ComErrc::kSDProxyQueryFailed);
}
```

---

#### 5.3.3 配置示例

**runtime_config.yaml**:

```yaml
service_discovery:
  # 本地服务发现（永远启用）
  local:
    enabled: true
    slot_mapping_file: /etc/lap/com/slot_mapping.yaml
    shared_memory_path: /dev/shm/lap_com_registry
  
  # 远程服务发现（可选）
  remote:
    enabled: true  # false = 仅本地模式
    protocol: fast_dds  # fast_dds / someip_sd / gossip
    
    # Fast DDS Discovery Server 配置
    fast_dds:
      discovery_servers:
        - ip: "192.168.1.10"
          port: 11811
        - ip: "192.168.1.11"  # 备用 Server
          port: 11811
      cache_ttl_seconds: 60
      discovery_timeout_ms: 5000
    
    # SOME/IP-SD 配置（可选）
    someip_sd:
      multicast_address: "239.0.0.1"
      multicast_port: 30490
      offer_interval_ms: 1000
```

#### 5.3.4 远程服务缓存（性能优化）

```cpp
// LRU Cache for remote services
class RemoteServiceCache {
public:
    struct CacheEntry {
        ServiceInstanceInfo info;
        std::chrono::steady_clock::time_point expiry;
    };
    
    std::optional<std::vector<ServiceInstanceInfo>> Find(
        const InstanceIdentifier& instance_filter
    ) {
        std::shared_lock lock(cache_mutex_);
        
        auto it = cache_.find(GetCacheKey(instance_filter));
        if (it == cache_.end()) {
            return std::nullopt;  // Cache miss
        }
        
        // 检查 TTL
        if (std::chrono::steady_clock::now() > it->second.expiry) {
            return std::nullopt;  // 已过期
        }
        
        return it->second.info;
    }
    
    void Insert(
        const ServiceInstanceInfo& info,
        std::chrono::seconds ttl
    ) {
        std::unique_lock lock(cache_mutex_);
        
        CacheEntry entry{
            .info = info,
            .expiry = std::chrono::steady_clock::now() + ttl
        };
        
        cache_[GetCacheKey(info.instance_id)] = entry;
        
        // LRU 淘汰（保留最近 100 个）
        if (cache_.size() > max_cache_size_) {
            EvictOldest();
        }
    }
    
private:
    std::unordered_map<std::string, CacheEntry> cache_;
    mutable std::shared_mutex cache_mutex_;
    size_t max_cache_size_ = 100;
};
```

### 5.4 配置与部署

#### 5.4.1 SD Proxy 配置文件

**sd_proxy.yaml**:

```yaml
sd_proxy:
  # 槽位配置
  slot_config:
    primary_slot: 1        # 主实例槽位
    backup_slot: 512       # 备份实例槽位
  
  # Cache Manager 配置
  cache:
    max_entries: 1024      # 最大缓存条目
    default_ttl: 60        # 默认 TTL (秒)
    lru_enabled: true      # 启用 LRU 淘汰
  
  # TTL Manager 配置
  ttl:
    cleanup_interval: 5    # 清理周期 (秒)
    min_ttl: 10            # 最小 TTL
    max_ttl: 300           # 最大 TTL
  
  # Provider Registry 配置
  provider_registry:
    max_ecus: 256          # 最大 ECU 数量
    health_check_interval: 10  # 健康检查间隔 (秒)
    heartbeat_timeout: 30  # 心跳超时 (秒)
  
  # Security Filter 配置
  security:
    whitelist_enabled: true
    allowed_services:
      - service_id: 0x1001
        ecus: ["ECU_ADAS_Front", "ECU_Gateway"]
      - service_id: 0x1002
        ecus: ["ECU_ADAS_Rear"]
    
    acl_rules:
      - ecu: "ECU_Gateway"
        permission: "allow_all"
      - ecu: "ECU_Cockpit"
        permission: "read_only"
  
  # Cross-ECU Router 配置
  router:
    local_priority: true   # 本地服务优先
    load_balance: "round_robin"  # 负载均衡策略
  
  # Fast DDS 配置
  dds:
    domain_id: 0
    discovery_server:
      ip: "192.168.1.10"
      port: 11811
    
    qos:
      reliability: "RELIABLE"
      durability: "TRANSIENT_LOCAL"
      history_depth: 100
```

#### 5.4.2 Systemd 服务单元

**lap_sd_proxy.service**:

```ini
[Unit]
Description=LightAP SD Proxy Service (跨 ECU 服务发现代理)
After=network.target
Requires=network.target

[Service]
Type=simple
ExecStart=/usr/bin/lap_sd_proxy --config /etc/lap/sd_proxy.yaml
Restart=always
RestartSec=5s

# 资源限制
MemoryMax=250M          # 200MB 实际使用 + 50MB 余量
MemoryHigh=220M         # 超过 220MB 触发警告
CPUQuota=15%            # 限制 CPU 使用

# 优先级设置
Nice=-5                 # 略高于默认优先级

# 安全加固
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true

# 日志配置
StandardOutput=journal
StandardError=journal
SyslogIdentifier=lap_sd_proxy

[Install]
WantedBy=multi-user.target
```

#### 5.4.3 安装与启动

**安装脚本** (install_sd_proxy.sh):

```bash
#!/bin/bash
set -e

# 1. 安装二进制文件
sudo install -m 755 lap_sd_proxy /usr/bin/

# 2. 安装配置文件
sudo mkdir -p /etc/lap
sudo install -m 644 sd_proxy.yaml /etc/lap/

# 3. 安装 systemd 服务单元
sudo install -m 644 lap_sd_proxy.service /etc/systemd/system/

# 4. 重新加载 systemd
sudo systemctl daemon-reload

# 5. 启用服务（开机自启）
sudo systemctl enable lap_sd_proxy.service

# 6. 启动服务
sudo systemctl start lap_sd_proxy.service

# 7. 检查状态
sudo systemctl status lap_sd_proxy.service

echo "✅ SD Proxy 安装完成"
```

**启动与管理**:

```bash
# 启动 SD Proxy
sudo systemctl start lap_sd_proxy

# 停止 SD Proxy
sudo systemctl stop lap_sd_proxy

# 重启 SD Proxy
sudo systemctl restart lap_sd_proxy

# 查看状态
sudo systemctl status lap_sd_proxy

# 查看日志
journalctl -u lap_sd_proxy -f

# 查看最近 100 行日志
journalctl -u lap_sd_proxy -n 100
```

### 5.5 性能对比与监控

#### 5.5.1 SD Proxy vs 传统方案性能对比

| 方案 | 首次发现延迟 | 缓存命中延迟 | 带宽占用 | 内存占用 | 适用场景 |
|------|------------|------------|---------|---------|---------|
| **本地共享内存** | 0ns | < 500ns | 0 | ~256 KB | 同 ECU ⭐ |
| **SD Proxy (缓存命中)** | - | < 1ms | 低 | +10 MB | 跨 ECU 高频 ⭐ |
| **SD Proxy (缓存未命中)** | < 100ms | - | 低 | +10 MB | 首次跨 ECU |
| **Fast DDS (无代理)** | 10-50ms | 10-50ms | 中 | ~50 MB | 跨 ECU 中频 |
| **SOME/IP-SD** | 20-50ms | 20-50ms | 中 | ~30 MB | 车载以太网标准 |
| **Gossip 协议** | 1-5s | < 10ms | 高 | ~20 MB | CAN-FD / FlexRay |

**推荐配置**:

- **仅本地通信**: 禁用远程发现（零开销）
- **以太网跨 ECU**: **SD Proxy + Fast DDS**（推荐）⭐
- **混合网络**: SD Proxy + SOME/IP-SD（主备模式）
- **低带宽网络**: Gossip 协议（CAN-FD）

#### 5.5.2 SD Proxy 监控指标

**通过 Slot 1 元数据字段暴露运行时统计**:

```cpp
// SD Proxy 定期更新 Slot 1 的 metadata 字段（JSON 格式）
json stats = {
    {"cache_hit_rate", cache_manager->GetStats().HitRate()},
    {"cache_entries", cache_manager->GetCacheSize()},
    {"active_ecus", provider_registry->GetKnownECUs().size()},
    {"query_total", query_stats.total},
    {"query_local", query_stats.local},
    {"query_remote", query_stats.remote},
    {"avg_latency_ms", query_stats.avg_latency_ms},
    {"uptime_seconds", GetUptimeSeconds()}
};

UpdateSlotMetadata(1, stats.dump());
```

**监控工具示例**:

```bash
# 查看 SD Proxy 实时统计
$ lap-sd-stat --slot 1

# 输出示例:
SD Proxy Statistics (Slot 1, Primary):
├─ Cache Hit Rate: 85.3%
├─ Cache Entries: 234/1024 (23% utilization)
├─ Active ECUs: 4
│  ├─ ECU_Gateway (192.168.1.10) ✓
│  ├─ ECU_ADAS_Front (192.168.1.20) ✓
│  ├─ ECU_ADAS_Rear (192.168.1.21) ✓
│  └─ ECU_Cockpit (192.168.1.30) ✓
├─ Query Statistics:
│  ├─ Total: 1234 queries
│  ├─ Local: 1050 (85.1%)
│  └─ Remote: 184 (14.9%)
├─ Avg Query Latency: 0.8ms (P99: 4.5ms)
└─ Uptime: 3h 25m 10s

Backup Instance (Slot 512): ✓ Healthy (Standby)
```

### 5.6 接口一致性保证机制

> 确保跨 ECU 服务发现的接口版本兼容性和语义一致性

#### 5.6.1 接口一致性设计原则

**双层 IDL 架构** (Franca IDL 作为 SSOT):

```text
┌─────────────────────────────────────────────────────────────────┐
│                    Franca IDL (SSOT)                            │
│               RadarService.fidl (v1.2.3)                        │
│  - 统一接口模型                                                  │
│  - 版本管理 (Major.Minor.Patch)                                 │
│  - Schema Hash 生成源                                           │
└────────────────────┬────────────────────────────────────────────┘
                     │
        ┌────────────┴────────────┬────────────────┐
        │                         │                │
        ▼                         ▼                ▼
┌──────────────────┐    ┌──────────────────┐  ┌──────────────┐
│  AUTOSAR API     │    │   DDS IDL v4.2   │  │  Proxy 绑定  │
│  (C++ 接口)      │    │  (RTPS Topics)   │  │  (Runtime)   │
│                  │    │                  │  │              │
│ PyFranca生成     │    │ FastDDS-gen生成  │  │ 版本适配层   │
│ - Skeleton API   │    │ - TypeSupport    │  │ - 类型转换   │
│ - Proxy API      │    │ - TypeObject     │  │ - QoS映射    │
│ - Event/Method   │    │ - Serialization  │  │              │
└──────────────────┘    └──────────────────┘  └──────────────┘
         │                       │                    │
         └───────────────────────┴────────────────────┘
                                 │
                    ┌────────────▼────────────┐
                    │  Runtime 统一接口层     │
                    │  (应用层API透明)        │
                    └─────────────────────────┘
```

**8 大一致性保证**:

##### ✅ (1) Franca IDL 作为单一真相源 (SSOT)

**所有服务接口通过 Franca IDL 定义**:

```fidl
// RadarService.fidl (Franca IDL)
package lap.com.radar

// 版本信息 (强制)
version { major 1 minor 2 patch 3 }

// 数据类型定义
struct Point3D {
    Float x
    Float y
    Float z
}

struct RadarObject {
    UInt32 objectId
    Point3D position
    Point3D velocity
    Float rcs  // Radar Cross Section
}

typedef RadarObjectList is RadarObject[]

// 服务接口
interface RadarService {
    version { major 1 minor 2 }
    
    // 事件 (Pub/Sub)
    broadcast ObjectsDetected {
        out {
            UInt64 timestamp
            RadarObjectList objects
        }
    }
    
    // 方法 (Request/Reply)
    method Calibrate {
        in {
            UInt32 calibrationType
        }
        out {
            Boolean success
            String errorMessage
        }
    }
    
    // 字段 (Field = Getter + Setter + Notifier)
    attribute UInt32 sensitivity
}
```

**自动生成 Schema Hash** (保证一致性):

```python
#!/usr/bin/env python3
# tools/generate_schema_hash.py
import hashlib
import json

def generate_franca_hash(fidl_file: str) -> str:
    """生成 Franca IDL 的 Schema Hash"""
    # 1. 解析 Franca IDL (使用 PyFranca)
    from pyfranca import Processor, ast
    
    processor = Processor()
    model = processor.import_file(fidl_file)
    
    # 2. 提取语义信息 (忽略注释、空格)
    schema = {
        "package": model.packages[0].name,
        "version": {
            "major": model.packages[0].version.major,
            "minor": model.packages[0].version.minor,
            "patch": model.packages[0].version.patch,
        },
        "interfaces": [],
        "types": []
    }
    
    for interface in model.packages[0].interfaces:
        iface_info = {
            "name": interface.name,
            "version": {
                "major": interface.version.major,
                "minor": interface.version.minor
            },
            "methods": [
                {"name": m.name, "in": m.in_args, "out": m.out_args}
                for m in interface.methods
            ],
            "broadcasts": [
                {"name": b.name, "out": b.out_args}
                for b in interface.broadcasts
            ],
            "attributes": [
                {"name": a.name, "type": a.type}
                for a in interface.attributes
            ]
        }
        schema["interfaces"].append(iface_info)
    
    # 3. 生成 SHA-256 Hash
    schema_json = json.dumps(schema, sort_keys=True)
    return hashlib.sha256(schema_json.encode()).hexdigest()[:16]

if __name__ == "__main__":
    fidl_hash = generate_franca_hash("RadarService.fidl")
    print(f"Franca Schema Hash: {fidl_hash}")
    # 输出: Franca Schema Hash: a3f7c9e2b5d14a8c
```

**代码生成工作流** (Franca → AUTOSAR + DDS):

```bash
#!/bin/bash
# tools/generate_all.sh

FIDL_FILE="services/radar/RadarService.fidl"
OUT_DIR="generated/radar"

# 1. 验证 Franca IDL 语法
echo "[1/5] Validating Franca IDL..."
pyfranca-validate "$FIDL_FILE" || exit 1

# 2. 生成 Schema Hash
echo "[2/5] Generating Schema Hash..."
FRANCA_HASH=$(python3 tools/generate_schema_hash.py "$FIDL_FILE")
echo "Franca Schema Hash: $FRANCA_HASH"

# 3. 生成 AUTOSAR C++ API (使用 PyFranca)
echo "[3/5] Generating AUTOSAR API (Skeleton/Proxy)..."
pyfranca-gen-cpp \
    --input "$FIDL_FILE" \
    --output "$OUT_DIR/autosar" \
    --namespace lap::com::radar \
    --schema-hash "$FRANCA_HASH"

# 生成的文件:
# - RadarServiceSkeleton.h/cpp  (Provider 侧)
# - RadarServiceProxy.h/cpp     (Consumer 侧)
# - RadarServiceTypes.h         (数据类型)

# 4. 转换为 DDS IDL v4.2
echo "[4/5] Converting to DDS IDL..."
python3 tools/franca_to_dds_idl.py \
    --input "$FIDL_FILE" \
    --output "$OUT_DIR/dds/RadarService.idl" \
    --schema-hash "$FRANCA_HASH" \
    --version "$FRANCA_VERSION"

# 5. 使用 FastDDS-gen 生成 DDS TypeSupport
echo "[5/5] Generating DDS TypeSupport..."
fastddsgen -replace -typeobject \
    -d "$OUT_DIR/dds" \
    "$OUT_DIR/dds/RadarService.idl"

echo "✅ Code generation complete!"
echo "   - AUTOSAR API: $OUT_DIR/autosar/"
echo "   - DDS Types:   $OUT_DIR/dds/"
echo "   - Schema Hash: $FRANCA_HASH"
```

**Franca 转 DDS IDL 工具** (自动注入版本和 Schema Hash):

```python
#!/usr/bin/env python3
# tools/franca_to_dds_idl.py
import argparse
from pyfranca import Processor

def franca_to_dds_idl(fidl_file: str, output_file: str, schema_hash: str, version: str):
    """将 Franca IDL 转换为 DDS IDL v4.2"""
    processor = Processor()
    model = processor.import_file(fidl_file)
    package = model.packages[0]
    
    dds_idl = []
    dds_idl.append(f"// Auto-generated from {fidl_file}")
    dds_idl.append(f"// Franca Schema Hash: {schema_hash}")
    dds_idl.append(f"// Version: {version}")
    dds_idl.append("")
    
    # 生成 module (package)
    namespace = package.name.replace('.', '::')
    modules = package.name.split('.')
    
    for module in modules:
        dds_idl.append(f"module {module} {{")
    
    # 注入版本信息
    dds_idl.append(f"    const string SCHEMA_HASH = \"{schema_hash}\";")
    dds_idl.append(f"    @version(\"{version}\")")
    dds_idl.append("")
    
    # 生成 struct 定义
    for typedef in package.typedefs:
        if typedef.is_array:
            dds_idl.append(f"    typedef sequence<{typedef.type}> {typedef.name};")
    
    for struct in package.structs:
        dds_idl.append(f"    struct {struct.name} {{")
        for field in struct.fields:
            field_type = map_franca_type_to_dds(field.type)
            dds_idl.append(f"        {field_type} {field.name};")
        dds_idl.append("    };");
        dds_idl.append("")
    
    # 生成 interface 的事件类型 (broadcast → struct)
    for interface in package.interfaces:
        for broadcast in interface.broadcasts:
            dds_idl.append(f"    struct {broadcast.name}Event {{")
            for arg in broadcast.out_args:
                arg_type = map_franca_type_to_dds(arg.type)
                dds_idl.append(f"        {arg_type} {arg.name};")
            dds_idl.append("    };");
            dds_idl.append("")
    
    # 关闭 module
    for module in reversed(modules):
        dds_idl.append("};");
    
    # 写入文件
    with open(output_file, 'w') as f:
        f.write('\n'.join(dds_idl))
    
    print(f"✅ Generated DDS IDL: {output_file}")

def map_franca_type_to_dds(franca_type: str) -> str:
    """Franca 类型 → DDS 类型映射"""
    type_map = {
        "UInt8": "octet",
        "UInt16": "unsigned short",
        "UInt32": "unsigned long",
        "UInt64": "unsigned long long",
        "Int8": "char",
        "Int16": "short",
        "Int32": "long",
        "Int64": "long long",
        "Float": "float",
        "Double": "double",
        "Boolean": "boolean",
        "String": "string",
    }
    return type_map.get(franca_type, franca_type)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--schema-hash", required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    
    franca_to_dds_idl(args.input, args.output, args.schema_hash, args.version)
```

**生成的 DDS IDL 示例** (自动包含版本和 Schema Hash):

```idl
// Auto-generated from RadarService.fidl
// Franca Schema Hash: a3f7c9e2b5d14a8c
// Version: 1.2.3

module lap {
module com {
module radar {
    const string SCHEMA_HASH = "a3f7c9e2b5d14a8c";
    @version("1.2.3")
    
    struct Point3D {
        float x;
        float y;
        float z;
    };
    
    struct RadarObject {
        unsigned long objectId;
        Point3D position;
        Point3D velocity;
        float rcs;
    };
    
    typedef sequence<RadarObject> RadarObjectList;
    
    // Broadcast → Event Topic Type
    struct ObjectsDetectedEvent {
        unsigned long long timestamp;
        RadarObjectList objects;
    };
    
}; // radar
}; // com
}; // lap
```

##### ✅ (2) 强制版本一致性验证 (Schema Hash + TypeIdentifier)

**SD-Proxy 启动时验证**:

```cpp
// SD-Proxy 启动时检查
class SDProxyValidator {
public:
    Result<void> ValidateServiceContract(
        const std::string& service_name,
        const std::string& expected_schema_hash
    ) {
        // 1. 读取 DDS TypeObject (FastDDS 内置)
        eprosima::fastdds::dds::DomainParticipant* participant = /*...*/;
        eprosima::fastrtps::types::TypeObject type_obj;
        
        if (!participant->get_type_object(
            "lap::com::radar::ObjectsDetectedEvent",
            type_obj
        )) {
            return Result<>::FromError(ComErrc::kTypeNotFound);
        }
        
        // 2. 计算 DDS TypeIdentifier Hash
        eprosima::fastrtps::types::TypeIdentifier type_id = 
            type_obj.complete().type_identifier();
        std::string dds_hash = CalculateTypeIdHash(type_id);
        
        // 3. 读取嵌入的 Franca Schema Hash
        std::string franca_hash = ReadSchemaHashFromIDL(service_name);
        
        // 4. 验证一致性
        if (franca_hash != expected_schema_hash) {
            LOG_ERROR("Schema Hash mismatch! Franca={}, Expected={}",
                      franca_hash, expected_schema_hash);
            return Result<>::FromError(ComErrc::kSchemaHashMismatch);
        }
        
        // 5. 验证 DDS TypeIdentifier (跨 ECU 一致性)
        if (!ValidateDDSTypeIdentifier(service_name, type_id)) {
            LOG_ERROR("DDS TypeIdentifier mismatch!");
            return Result<>::FromError(ComErrc::kTypeIdentifierMismatch);
        }
        
        LOG_INFO("✅ Service contract validated: {} (hash={})",
                 service_name, franca_hash);
        return Result<>::FromValue();
    }
    
private:
    std::string CalculateTypeIdHash(
        const eprosima::fastrtps::types::TypeIdentifier& type_id
    ) {
        // FastDDS TypeIdentifier 已经是 hash
        std::stringstream ss;
        ss << std::hex << type_id.equivalence_hash();
        return ss.str().substr(0, 16);
    }
};
```

**应用侧版本检查** (拒绝不兼容的服务):

```cpp
// Runtime::FindService 内部
Result<ServiceHandleContainer<HandleType>> Runtime::FindService(
    const InstanceIdentifier& instance_filter
) {
    auto results = QueryServices(instance_filter);
    
    ServiceHandleContainer<HandleType> handles;
    for (const auto& service_info : results) {
        // 验证 Schema Hash
        if (service_info.schema_hash != expected_schema_hash_) {
            LOG_WARN("Schema mismatch: service={}, expected={}, actual={}",
                     service_info.instance_id,
                     expected_schema_hash_,
                     service_info.schema_hash);
            continue;  // 跳过不兼容的服务
        }
        
        // 验证版本兼容性 (Major 必须相同)
        if (service_info.major_version != required_major_version_) {
            LOG_WARN("Major version mismatch: required={}, actual={}",
                     required_major_version_,
                     service_info.major_version);
            continue;
        }
        
        handles.push_back(CreateHandle(service_info));
    }
    
    return Result<>::FromValue(handles);
}
```

##### ✅ (3) QoS 配置独立于 IDL (YAML 配置)

**QoS 不写入 IDL，而是放在独立的 YAML 文件中**:

```yaml
# qos_config/radar_service.yaml
service:
  name: "RadarService"
  instance: "FrontRadar"
  schema_hash: "a3f7c9e2b5d14a8c"  # 必须匹配 Franca Hash

# DDS QoS 配置 (运行时加载)
dds_qos:
  topics:
    - name: "lap/radar/ObjectsDetected"
      reliability: "RELIABLE"  # RELIABLE / BEST_EFFORT
      durability: "TRANSIENT_LOCAL"  # VOLATILE / TRANSIENT_LOCAL / PERSISTENT
      history:
        kind: "KEEP_LAST"
        depth: 10
      deadline:
        period_ms: 100  # 100ms 内必须发布一次
      liveliness:
        kind: "AUTOMATIC"
        lease_duration_ms: 1000
      ownership: "SHARED"  # SHARED / EXCLUSIVE
  
  participants:
    domain_id: 0
    participant_name: "ECU_ADAS_Front"

# iceoryx2 QoS 配置 (本地通信)
iceoryx2_qos:
  max_subscribers: 8
  max_samples: 16
  node_name: "radar_provider"
```

**Runtime 加载 QoS 配置**:

```cpp
class QoSConfigLoader {
public:
    Result<DDSQoS> LoadDDSQoS(
        const std::string& service_name,
        const std::string& instance_id
    ) {
        // 1. 加载 YAML 配置
        std::string config_file = 
            fmt::format("/etc/lap/com/qos_config/{}.yaml", service_name);
        
        YAML::Node config = YAML::LoadFile(config_file);
        
        // 2. 验证 Schema Hash (必须匹配)
        std::string config_hash = config["service"]["schema_hash"].as<std::string>();
        if (config_hash != GetExpectedSchemaHash(service_name)) {
            return Result<>::FromError(ComErrc::kQoSConfigMismatch);
        }
        
        // 3. 构建 DDS QoS
        eprosima::fastdds::dds::DataWriterQos qos;
        
        auto topic_config = config["dds_qos"]["topics"][0];
        
        // Reliability
        if (topic_config["reliability"].as<std::string>() == "RELIABLE") {
            qos.reliability().kind = 
                eprosima::fastdds::dds::RELIABLE_RELIABILITY_QOS;
        } else {
            qos.reliability().kind = 
                eprosima::fastdds::dds::BEST_EFFORT_RELIABILITY_QOS;
        }
        
        // History
        qos.history().kind = eprosima::fastdds::dds::KEEP_LAST_HISTORY_QOS;
        qos.history().depth = topic_config["history"]["depth"].as<int>();
        
        // Deadline
        qos.deadline().period.nanosec = 
            topic_config["deadline"]["period_ms"].as<int>() * 1000000;
        
        return Result<>::FromValue(qos);
    }
};
```

**优势**:
- ✅ QoS 变化不需要重新生成 IDL
- ✅ 不同 ECU 可以使用不同的 QoS (如测试环境 vs 生产环境)
- ✅ QoS 可以在运行时动态调整 (通过配置文件)
- ✅ IDL 保持纯净，只关注数据结构

##### ✅ (4) DDS 类型不泄漏到应用层 (Proxy 层隔离)

**应用层永远不直接使用 DDS 类型**:

```cpp
// ❌ 错误：应用直接使用 DDS 类型 (泄漏)
#include "RadarServicePubSubTypes.h"  // DDS 生成的头文件

void BadExample() {
    lap::com::radar::ObjectsDetectedEvent dds_event;  // DDS 类型
    // ...
}

// ✅ 正确：应用使用 AUTOSAR API (从 Franca 生成)
#include "RadarServiceProxy.h"  // AUTOSAR API (PyFranca 生成)

void GoodExample() {
    using namespace lap::com::radar;
    
    // 1. 创建 Proxy (AUTOSAR API)
    auto runtime = Runtime::GetInstance();
    auto result = runtime->FindService<RadarServiceProxy>(
        InstanceSpecifier("RadarService/FrontRadar")
    );
    
    if (result && !result.Value().empty()) {
        auto proxy = RadarServiceProxy::Create(result.Value()[0]);
        
        // 2. 订阅事件 (AUTOSAR API，不涉及 DDS)
        proxy->ObjectsDetected.Subscribe(
            [](const ObjectsDetectedEventArgs& args) {
                // args.timestamp, args.objects 是 AUTOSAR 类型
                // 内部 Proxy 层自动转换 DDS ↔ AUTOSAR
                for (const auto& obj : args.objects) {
                    LOG_INFO("Object {}: pos=({}, {}, {})",
                             obj.objectId,
                             obj.position.x,
                             obj.position.y,
                             obj.position.z);
                }
            }
        );
    }
}
```

**Proxy 层负责类型转换** (DDS ↔ AUTOSAR):

```cpp
// RadarServiceProxy 内部实现 (自动生成)
class RadarServiceProxyImpl {
private:
    // DDS Reader (内部使用，不暴露给应用)
    eprosima::fastdds::dds::DataReader* dds_reader_;
    
    void OnDDSDataAvailable() {
        // 1. 读取 DDS 数据
        lap::com::radar::ObjectsDetectedEvent dds_event;  // DDS 类型
        eprosima::fastdds::dds::SampleInfo info;
        
        if (dds_reader_->take_next_sample(&dds_event, &info) == 
            eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK) {
            
            // 2. 转换为 AUTOSAR 类型
            ObjectsDetectedEventArgs autosar_event;  // AUTOSAR 类型
            autosar_event.timestamp = dds_event.timestamp();
            
            for (const auto& dds_obj : dds_event.objects()) {
                RadarObject autosar_obj;
                autosar_obj.objectId = dds_obj.objectId();
                autosar_obj.position.x = dds_obj.position().x();
                autosar_obj.position.y = dds_obj.position().y();
                autosar_obj.position.z = dds_obj.position().z();
                // ...
                autosar_event.objects.push_back(autosar_obj);
            }
            
            // 3. 触发 AUTOSAR 回调
            NotifySubscribers(autosar_event);
        }
    }
};
```

**优势**:
- ✅ 应用代码完全不依赖 DDS (可以换成其他 Transport)
- ✅ DDS 升级不影响应用 (Proxy 层吸收变化)
- ✅ 编译隔离 (应用不需要链接 FastDDS)
- ✅ AUTOSAR 标准合规 (ara::com API)

##### ✅ (5) Local-SD API 永不变化（对应用透明）

**应用层 API 稳定不变**:

```cpp
// AUTOSAR 标准 API（永不变化）
namespace lap::com {

// FindService API（R19-11 → R24-11 → 未来版本，保持兼容）
template<typename ProxyType>
ara::core::Result<ServiceHandleContainer<ProxyType::HandleType>> 
FindService(InstanceSpecifier instance_specifier);

// OfferService API（Skeleton 侧，永不变化）
ara::core::Result<void> OfferService();
ara::core::Result<void> StopOfferService();

} // namespace lap::com
```

**SD-Proxy 内部实现可变，但对应用透明**:

```cpp
// Runtime 内部实现（可演进）
class Runtime {
    // v1.0: 仅本地共享内存
    // v2.0: + Fast DDS Discovery
    // v3.0: + SOME/IP-SD
    // v4.0: + 自定义协议
    
    // 但应用代码无需任何修改
};
```

##### ✅ (3) Proxy 负责版本转换、格式转换、语义统一

**SD-Proxy 版本适配层**:

```cpp
class VersionAdapter {
public:
    // 将远程服务的版本转换为本地兼容版本
    Result<ServiceInstanceInfo> AdaptVersion(
        const RemoteServiceMetadata& remote_metadata
    ) {
        // 检查版本兼容性
        if (!IsVersionCompatible(
            local_major_, local_minor_,
            remote_metadata.major, remote_metadata.minor
        )) {
            LOG_WARN("Version incompatible: local={}.{}, remote={}.{}",
                     local_major_, local_minor_,
                     remote_metadata.major, remote_metadata.minor);
            
            return Result<>::FromError(ComErrc::kVersionIncompatible);
        }
        
        // 格式转换：DDS → 内部 ServiceEntry
        ServiceInstanceInfo adapted{
            .service_id = remote_metadata.service_id,
            .instance_id = remote_metadata.instance_id,
            .major_version = remote_metadata.major,
            .minor_version = remote_metadata.minor,
            .endpoint = ConvertDDSEndpoint(remote_metadata.dds_endpoint),
            .is_remote = true
        };
        
        return Result<>::FromValue(adapted);
    }
    
private:
    uint32_t local_major_;
    uint32_t local_minor_;
};
```

**语义统一**:

```cpp
// 内部统一数据模型 ServiceEntry
struct ServiceEntry {
    uint64_t service_id;
    uint64_t instance_id;
    uint32_t major_version;
    uint32_t minor_version;
    
    // 统一端点表示（屏蔽底层协议差异）
    std::string endpoint;  // "iceoryx2://..." / "someip://192.168.1.10:30500"
    
    // 统一绑定类型
    TransportBindingType binding_type;  // kIceoryx2 / kSomeip / kDDS
    
    // 统一元数据
    std::map<std::string, std::string> metadata;
};
```

##### ✅ (4) 统一版本策略（Major 强一致，Minor 弱一致）

**版本兼容性规则** (符合 AUTOSAR SWS_CM_11006):

```cpp
class VersionPolicy {
public:
    // Major 版本必须完全匹配（强一致性）
    static bool IsVersionCompatible(
        uint32_t required_major, uint32_t required_minor,
        uint32_t provided_major, uint32_t provided_minor
    ) {
        // 1. Major 版本必须完全匹配
        if (required_major != provided_major) {
            LOG_ERROR("Major version mismatch: required={}, provided={}",
                      required_major, provided_major);
            return false;
        }
        
        // 2. Minor 版本：提供者 >= 请求者（向后兼容）
        if (provided_minor < required_minor) {
            LOG_ERROR("Minor version too old: required={}, provided={}",
                      required_minor, provided_minor);
            return false;
        }
        
        // 3. 通过版本检查
        return true;
    }
};
```

**CI 阶段版本检查**:

```yaml
# .gitlab-ci.yml
version_check:
  stage: test
  script:
    # 检查所有 ECU 的服务版本一致性
    - python3 tools/version_checker.py \
        --ecu-a manifest/ecu_a_services.yaml \
        --ecu-b manifest/ecu_b_services.yaml \
        --ecu-c manifest/ecu_c_services.yaml
    
    # 失败示例:
    # ERROR: RadarService version mismatch
    #   ECU-A: 1.2
    #   ECU-B: 1.3  ✓ (compatible)
    #   ECU-C: 2.0  ✗ (major version different)
  only:
    - merge_requests
```

##### ✅ (5) 统一内部数据模型（ServiceEntry）

**所有协议适配到统一 ServiceEntry**:

```cpp
// 协议适配器接口
class IDiscoveryProtocolAdapter {
public:
    virtual ~IDiscoveryProtocolAdapter() = default;
    
    // 将协议特定的服务元数据转换为统一 ServiceEntry
    virtual Result<ServiceEntry> AdaptToServiceEntry(
        const ProtocolSpecificMetadata& metadata
    ) = 0;
    
    // 将统一 ServiceEntry 转换为协议特定格式
    virtual Result<ProtocolSpecificMetadata> AdaptFromServiceEntry(
        const ServiceEntry& entry
    ) = 0;
};

// DDS 适配器
class DDSAdapter : public IDiscoveryProtocolAdapter {
public:
    Result<ServiceEntry> AdaptToServiceEntry(
        const dds::core::policy::UserData& user_data
    ) override {
        // 解析 DDS USER_DATA QoS
        auto dds_metadata = DeserializeDDSMetadata(user_data);
        
        ServiceEntry entry{
            .service_id = dds_metadata.service_id,
            .instance_id = dds_metadata.instance_id,
            .major_version = dds_metadata.major,
            .minor_version = dds_metadata.minor,
            .endpoint = "dds://" + dds_metadata.topic_name,
            .binding_type = TransportBindingType::kDDS,
            .metadata = dds_metadata.custom_metadata
        };
        
        return Result<>::FromValue(entry);
    }
};

// SOME/IP-SD 适配器
class SomeipSDAdapter : public IDiscoveryProtocolAdapter {
public:
    Result<ServiceEntry> AdaptToServiceEntry(
        const someip::ServiceEntry& sd_entry
    ) override {
        ServiceEntry entry{
            .service_id = sd_entry.service_id,
            .instance_id = sd_entry.instance_id,
            .major_version = sd_entry.major_version,
            .minor_version = sd_entry.minor_version,
            .endpoint = "someip://" + sd_entry.ipv4_address + ":" + 
                        std::to_string(sd_entry.port),
            .binding_type = TransportBindingType::kSomeip,
            .metadata = {}
        };
        
        return Result<>::FromValue(entry);
    }
};
```

##### ✅ (6) CI 阶段进行服务版本检查

**版本检查工具** (`tools/version_checker.py`):

```python
#!/usr/bin/env python3
import yaml
import sys

def check_service_versions(ecu_manifests):
    """检查所有 ECU 的服务版本一致性"""
    
    # 收集所有服务的版本
    service_versions = {}  # {service_name: {ecu_id: (major, minor)}}
    
    for ecu_id, manifest_path in ecu_manifests.items():
        with open(manifest_path) as f:
            manifest = yaml.safe_load(f)
        
        for service in manifest['services']:
            name = service['name']
            major = service['major_version']
            minor = service['minor_version']
            
            if name not in service_versions:
                service_versions[name] = {}
            service_versions[name][ecu_id] = (major, minor)
    
    # 检查版本一致性
    errors = []
    for service_name, ecu_versions in service_versions.items():
        majors = set(v[0] for v in ecu_versions.values())
        
        if len(majors) > 1:
            # Major 版本不一致（严重错误）
            errors.append(f"ERROR: {service_name} major version mismatch:")
            for ecu_id, (major, minor) in ecu_versions.items():
                errors.append(f"  {ecu_id}: {major}.{minor}")
        else:
            # 检查 Minor 版本兼容性
            minor_versions = sorted(v[1] for v in ecu_versions.values())
            if minor_versions[-1] - minor_versions[0] > 5:
                # Minor 版本差异过大（警告）
                errors.append(f"WARN: {service_name} minor version gap too large:")
                for ecu_id, (major, minor) in ecu_versions.items():
                    errors.append(f"  {ecu_id}: {major}.{minor}")
    
    if errors:
        print("\n".join(errors))
        sys.exit(1)
    
    print("✅ All service versions are compatible")

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--ecu-a', required=True)
    parser.add_argument('--ecu-b', required=True)
    parser.add_argument('--ecu-c', required=True)
    args = parser.parse_args()
    
    check_service_versions({
        'ECU-A': args.ecu_a,
        'ECU-B': args.ecu_b,
        'ECU-C': args.ecu_c
    })
```

##### ✅ (7) 跨协议通过 Adapter 层统一格式

**协议适配器工厂**:

```cpp
class ProtocolAdapterFactory {
public:
    static std::unique_ptr<IDiscoveryProtocolAdapter> Create(
        const std::string& protocol
    ) {
        if (protocol == "fast_dds") {
            return std::make_unique<DDSAdapter>();
        } else if (protocol == "someip_sd") {
            return std::make_unique<SomeipSDAdapter>();
        } else if (protocol == "custom") {
            return std::make_unique<CustomProtocolAdapter>();
        } else {
            LOG_ERROR("Unknown protocol: {}", protocol);
            return nullptr;
        }
    }
};

// SD-Proxy 使用适配器
class SDProxy {
public:
    void Initialize(const SDProxyConfig& config) {
        // 根据配置创建协议适配器
        protocol_adapter_ = ProtocolAdapterFactory::Create(
            config.remote_discovery.protocol
        );
        
        if (!protocol_adapter_) {
            LOG_ERROR("Failed to create protocol adapter");
            return;
        }
        
        LOG_INFO("SD-Proxy initialized with protocol: {}",
                 config.remote_discovery.protocol);
    }
    
    Result<std::vector<ServiceEntry>> QueryRemoteServices(
        const InstanceIdentifier& filter
    ) {
        // 1. 通过协议查询远程服务
        auto remote_metadata = QueryProtocolSpecific(filter);
        
        // 2. 通过适配器转换为统一 ServiceEntry
        std::vector<ServiceEntry> entries;
        for (const auto& metadata : remote_metadata) {
            auto entry_result = protocol_adapter_->AdaptToServiceEntry(metadata);
            if (entry_result) {
                entries.push_back(entry_result.Value());
            }
        }
        
        return Result<>::FromValue(entries);
    }
    
private:
    std::unique_ptr<IDiscoveryProtocolAdapter> protocol_adapter_;
};
```

##### ✅ (8) 所有 ECU 依赖共同的 Service Contract Repository

**Service Contract Repository 设计**:

**Service Contract Repository 结构** (基于 Franca IDL):

```text
service-contracts/ (Git Repository)
├── README.md
├── schemas/
│   ├── franca_idl_style_guide.md   # Franca IDL 编码规范
│   └── version_policy.yaml         # 版本策略 (Major.Minor.Patch)
├── services/
│   ├── radar_service/
│   │   ├── RadarService.fidl       # Franca IDL (SSOT)
│   │   ├── RadarService_v1_0.fidl  # v1.0 历史版本
│   │   ├── RadarService_v1_2.fidl  # v1.2 历史版本
│   │   ├── CHANGELOG.md
│   │   └── README.md
│   ├── camera_service/
│   │   ├── CameraService.fidl      # Franca IDL
│   │   ├── CHANGELOG.md
│   │   └── README.md
│   └── brake_service/
│       ├── BrakeService.fidl       # ASIL-D 服务 (Franca IDL)
│       ├── CHANGELOG.md
│       └── README.md
├── common_types/
│   ├── Geometry.fidl               # 公共几何类型 (Franca)
│   ├── Timestamp.fidl              # 时间戳类型 (Franca)
│   └── ErrorCodes.fidl             # 错误码定义 (Franca)
├── manifests/
│   ├── ecu_a_services.yaml         # ECU-A 使用的服务列表
│   ├── ecu_b_services.yaml
│   └── ecu_c_services.yaml
├── generated/                      # 自动生成 (不提交到 Git)
│   ├── autosar/                    # PyFranca 生成的 AUTOSAR API
│   │   ├── RadarServiceSkeleton.h
│   │   ├── RadarServiceProxy.h
│   │   └── ...
│   └── dds/                        # 转换的 DDS IDL + FastDDS-gen 输出
│       ├── RadarService.idl        # 从 Franca 转换
│       ├── RadarService.h          # FastDDS-gen 生成
│       └── ...
└── tools/
    ├── generate_schema_hash.py     # 生成 Franca Schema Hash
    ├── franca_to_dds_idl.py        # Franca → DDS IDL 转换
    ├── version_checker.py          # 版本一致性检查
    ├── fidl_validator.py           # Franca IDL 语法验证
    └── generate_all.sh             # 批量生成脚本
```

**ECU 构建时依赖** (使用 Franca IDL):

```cmake
# ECU-A 的 CMakeLists.txt
include(FetchContent)

# 拉取 Service Contract Repository
FetchContent_Declare(
  service_contracts
  GIT_REPOSITORY https://github.com/company/service-contracts.git
  GIT_TAG        v1.2.3  # 所有 ECU 使用相同版本
)
FetchContent_MakeAvailable(service_contracts)

# 查找工具
find_program(PYFRANCA_GEN pyfranca-gen-cpp REQUIRED)
find_program(FRANCA_TO_DDS franca_to_dds_idl.py REQUIRED)
find_program(FASTDDSGEN fastddsgen REQUIRED)

# === Step 1: 生成 Schema Hash ===
set(RADAR_FIDL ${service_contracts_SOURCE_DIR}/services/radar_service/RadarService.fidl)
set(RADAR_GEN_DIR ${CMAKE_BINARY_DIR}/generated/radar)

execute_process(
  COMMAND python3 ${service_contracts_SOURCE_DIR}/tools/generate_schema_hash.py ${RADAR_FIDL}
  OUTPUT_VARIABLE RADAR_SCHEMA_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "RadarService Schema Hash: ${RADAR_SCHEMA_HASH}")

# === Step 2: 生成 AUTOSAR API (使用 PyFranca) ===
add_custom_command(
  OUTPUT 
    ${RADAR_GEN_DIR}/autosar/RadarServiceSkeleton.h
    ${RADAR_GEN_DIR}/autosar/RadarServiceSkeleton.cpp
    ${RADAR_GEN_DIR}/autosar/RadarServiceProxy.h
    ${RADAR_GEN_DIR}/autosar/RadarServiceProxy.cpp
  COMMAND ${CMAKE_COMMAND} -E make_directory ${RADAR_GEN_DIR}/autosar
  COMMAND ${PYFRANCA_GEN}
    --input ${RADAR_FIDL}
    --output ${RADAR_GEN_DIR}/autosar
    --namespace lap::com::radar
    --schema-hash ${RADAR_SCHEMA_HASH}
  DEPENDS ${RADAR_FIDL}
  COMMENT "Generating AUTOSAR API from RadarService.fidl (PyFranca)"
)

# === Step 3: 转换为 DDS IDL ===
add_custom_command(
  OUTPUT ${RADAR_GEN_DIR}/dds/RadarService.idl
  COMMAND ${CMAKE_COMMAND} -E make_directory ${RADAR_GEN_DIR}/dds
  COMMAND python3 ${FRANCA_TO_DDS}
    --input ${RADAR_FIDL}
    --output ${RADAR_GEN_DIR}/dds/RadarService.idl
    --schema-hash ${RADAR_SCHEMA_HASH}
    --version "1.2.3"
  DEPENDS ${RADAR_FIDL}
  COMMENT "Converting Franca IDL to DDS IDL"
)

# === Step 4: 生成 DDS TypeSupport (使用 FastDDS-gen) ===
add_custom_command(
  OUTPUT 
    ${RADAR_GEN_DIR}/dds/RadarService.h
    ${RADAR_GEN_DIR}/dds/RadarService.cxx
    ${RADAR_GEN_DIR}/dds/RadarServicePubSubTypes.h
    ${RADAR_GEN_DIR}/dds/RadarServicePubSubTypes.cxx
  COMMAND ${FASTDDSGEN}
    -replace
    -d ${RADAR_GEN_DIR}/dds
    -typeobject
    ${RADAR_GEN_DIR}/dds/RadarService.idl
  DEPENDS ${RADAR_GEN_DIR}/dds/RadarService.idl
  COMMENT "Generating DDS TypeSupport from DDS IDL (FastDDS-gen)"
)

# === Step 5: 创建库 ===

# AUTOSAR API 库 (应用层使用)
add_library(radar_service_api
  ${RADAR_GEN_DIR}/autosar/RadarServiceSkeleton.cpp
  ${RADAR_GEN_DIR}/autosar/RadarServiceProxy.cpp
)
target_include_directories(radar_service_api PUBLIC ${RADAR_GEN_DIR}/autosar)
target_link_libraries(radar_service_api PUBLIC lap_com_runtime)

# DDS TypeSupport 库 (Proxy 层内部使用，不暴露给应用)
add_library(radar_service_dds PRIVATE
  ${RADAR_GEN_DIR}/dds/RadarService.cxx
  ${RADAR_GEN_DIR}/dds/RadarServicePubSubTypes.cxx
)
target_include_directories(radar_service_dds PRIVATE ${RADAR_GEN_DIR}/dds)
target_link_libraries(radar_service_dds PRIVATE fastrtps fastcdr)

# 应用程序只链接 AUTOSAR API
add_executable(radar_app main.cpp)
target_link_libraries(radar_app PRIVATE radar_service_api)  # ✅ 不依赖 DDS
```

**版本锁定策略** (基于 Franca Schema Hash):

```yaml
# manifests/ecu_a_services.yaml
ecu_id: "ECU_ADAS_Front"
service_contract_version: "v1.2.3"  # 锁定 Contract Repository 版本

services:
  - name: "RadarService"
    fidl_path: "services/radar_service/RadarService.fidl"  # Franca IDL
    schema_hash: "a3f7c9e2b5d14a8c"  # 强制验证
    version:
      major: 1
      minor: 2
      patch: 3
    role: provider  # ECU-A 提供雷达服务
    bindings:
      - type: iceoryx2
        topic: "lap/radar/objects"  # 本地共享内存
      - type: dds
        topic: "lap/radar/ObjectsDetectedEvent"  # 跨 ECU (自动从 Franca 生成)
    qos_config: "qos_config/radar_service.yaml"  # 独立 QoS 配置
  
  - name: "BrakeService"
    fidl_path: "services/brake_service/BrakeService.fidl"
    schema_hash: "d7b4e1f8a9c23456"
    version:
      major: 1
      minor: 0
      patch: 0
    role: consumer  # ECU-A 消费刹车服务（来自 ECU-Chassis）
    bindings:
      - type: dds
        topic: "lap/brake/CommandEvent"
    qos_config: "qos_config/brake_service.yaml"
```

#### 5.6.2 接口一致性验证流程

**CI/CD Pipeline** (双层 IDL 验证):

```yaml
# .gitlab-ci.yml
stages:
  - validate
  - generate
  - build
  - test
  - deploy

# Stage 1: 验证 Franca IDL
validate_franca_idl:
  stage: validate
  script:
    # 1. 验证 Franca IDL 语法
    - echo "Validating Franca IDL syntax..."
    - for fidl in services/**/*.fidl common_types/**/*.fidl; do
        pyfranca-validate "$fidl" || exit 1;
      done
    
    # 2. 生成 Schema Hash 并验证唯一性
    - echo "Generating Schema Hashes..."
    - python3 tools/validate_schema_hashes.py services/**/*.fidl
    
    # 3. 检查版本策略合规性
    - python3 tools/version_policy_checker.py
    
    # 4. 验证跨 ECU 服务版本一致性
    - python3 tools/version_checker.py \
        --ecu-a manifests/ecu_a_services.yaml \
        --ecu-b manifests/ecu_b_services.yaml \
        --ecu-c manifests/ecu_c_services.yaml
  
  only:
    - merge_requests
    - main

# Stage 2: 生成代码
generate_code:
  stage: generate
  script:
    # 1. 生成 AUTOSAR API (PyFranca)
    - echo "Generating AUTOSAR API..."
    - bash tools/generate_all.sh
    
    # 2. 验证生成的 DDS IDL 语法
    - echo "Validating generated DDS IDL..."
    - for dds_idl in generated/*/dds/*.idl; do
        fastddsgen -replace -typeobject "$dds_idl" -d /tmp/test_gen || exit 1;
      done
    
    # 3. 验证 Schema Hash 一致性
    - python3 tools/validate_hash_consistency.py \
        --franca services/**/*.fidl \
        --dds generated/*/dds/*.idl
  
  artifacts:
    paths:
      - generated/
    expire_in: 1 week
  
  dependencies:
    - validate_franca_idl

# Stage 3: 构建所有 ECU
build_all_ecus:
  stage: build
  parallel:
    matrix:
      - ECU: [ecu_a, ecu_b, ecu_c]
  script:
    - cd $ECU
    - cmake -B build -DCMAKE_BUILD_TYPE=Release
    - cmake --build build -j$(nproc)
    
    # 验证生成的二进制文件中嵌入了正确的 Schema Hash
    - python3 ../tools/verify_embedded_hash.py \
        --binary build/radar_app \
        --expected-hash $(cat ../manifests/${ECU}_services.yaml | \
          yq '.services[] | select(.name == "RadarService") | .schema_hash')
  
  dependencies:
    - generate_code

# Stage 4: 运行时验证
runtime_validation:
  stage: test
  script:
    # 模拟跨 ECU 通信，验证 Schema Hash 检查
    - docker-compose up -d ecu_a ecu_b ecu_c
    - sleep 5
    
    # 验证 SD-Proxy 启动时的 Schema 验证
    - docker exec ecu_a journalctl -u lap_sd_proxy | \
        grep "Service contract validated.*RadarService.*hash=a3f7c9e2b5d14a8c"
    
    # 验证不兼容的服务被拒绝
    - docker exec ecu_b ./inject_incompatible_service.sh || true
    - docker exec ecu_a journalctl -u lap_sd_proxy | \
        grep "Schema Hash mismatch"
  
  dependencies:
    - build_all_ecus
```

**Schema Hash 一致性验证工具**:

```python
#!/usr/bin/env python3
# tools/validate_hash_consistency.py
import argparse
import sys
from pathlib import Path
from generate_schema_hash import generate_franca_hash

def validate_hash_consistency(franca_files: list, dds_files: list):
    """验证 Franca IDL 和生成的 DDS IDL 的 Schema Hash 一致性"""
    errors = []
    
    for fidl_path in franca_files:
        # 1. 生成 Franca Hash
        franca_hash = generate_franca_hash(fidl_path)
        
        # 2. 查找对应的 DDS IDL
        service_name = Path(fidl_path).stem
        dds_idl_path = None
        for dds_path in dds_files:
            if service_name in Path(dds_path).stem:
                dds_idl_path = dds_path
                break
        
        if not dds_idl_path:
            errors.append(f"❌ {fidl_path}: No corresponding DDS IDL found")
            continue
        
        # 3. 读取 DDS IDL 中嵌入的 Schema Hash
        with open(dds_idl_path, 'r') as f:
            dds_content = f.read()
        
        if f'// Franca Schema Hash: {franca_hash}' not in dds_content:
            errors.append(
                f"❌ {fidl_path}: Schema Hash mismatch!\n"
                f"   Franca Hash: {franca_hash}\n"
                f"   DDS IDL does not contain expected hash"
            )
            continue
        
        # 4. 验证 const string SCHEMA_HASH
        if f'const string SCHEMA_HASH = "{franca_hash}";' not in dds_content:
            errors.append(
                f"❌ {fidl_path}: Schema Hash constant not embedded in DDS IDL"
            )
            continue
        
        print(f"✅ {fidl_path}: Schema Hash consistent ({franca_hash})")
    
    if errors:
        print("\n" + "\n".join(errors))
        sys.exit(1)
    
    print(f"\n✅ All {len(franca_files)} services have consistent Schema Hashes")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--franca", nargs='+', required=True)
    parser.add_argument("--dds", nargs='+', required=True)
    args = parser.parse_args()
    
    validate_hash_consistency(args.franca, args.dds)
```

**版本策略检查工具**:

```python
#!/usr/bin/env python3
# tools/version_policy_checker.py
from pyfranca import Processor
from pathlib import Path
import sys

def check_version_policy(fidl_file: str):
    """检查 Franca IDL 版本策略合规性"""
    processor = Processor()
    model = processor.import_file(fidl_file)
    package = model.packages[0]
    
    errors = []
    
    # 规则 1: Package 必须有版本
    if not package.version:
        errors.append(f"❌ {fidl_file}: Package missing version")
    else:
        # 规则 2: 版本必须是 Major.Minor.Patch 格式
        if not hasattr(package.version, 'patch'):
            errors.append(
                f"❌ {fidl_file}: Version must be Major.Minor.Patch "
                f"(got {package.version.major}.{package.version.minor})"
            )
    
    # 规则 3: Interface 版本必须 <= Package 版本
    for interface in package.interfaces:
        if not interface.version:
            errors.append(f"❌ {fidl_file}: Interface {interface.name} missing version")
        elif interface.version.major > package.version.major:
            errors.append(
                f"❌ {fidl_file}: Interface {interface.name} version "
                f"({interface.version.major}.{interface.version.minor}) "
                f"cannot exceed package version "
                f"({package.version.major}.{package.version.minor}.{package.version.patch})"
            )
    
    # 规则 4: 文件名必须匹配 <ServiceName>.fidl
    expected_name = Path(fidl_file).stem
    if package.interfaces and package.interfaces[0].name != expected_name:
        errors.append(
            f"❌ {fidl_file}: File name mismatch "
            f"(expected {package.interfaces[0].name}.fidl)"
        )
    
    return errors

if __name__ == "__main__":
    all_errors = []
    fidl_files = list(Path("services").rglob("*.fidl"))
    fidl_files.extend(Path("common_types").rglob("*.fidl"))
    
    for fidl_file in fidl_files:
        errors = check_version_policy(str(fidl_file))
        if errors:
            all_errors.extend(errors)
        else:
            print(f"✅ {fidl_file}: Version policy compliant")
    
    if all_errors:
        print("\n" + "\n".join(all_errors))
        sys.exit(1)
    
    print(f"\n✅ All {len(fidl_files)} files comply with version policy")
```

**运行时 Schema 验证** (应用启动时):

```cpp
// Application 启动时验证
class ServiceContractValidator {
public:
    static Result<void> ValidateBeforeConnect(
        const std::string& service_name,
        const std::string& instance_id
    ) {
        // 1. 从配置读取期望的 Schema Hash
        auto expected_hash = LoadExpectedHash(service_name);
        if (!expected_hash) {
            return Result<>::FromError(ComErrc::kConfigNotFound);
        }
        
        // 2. 查询服务实例
        auto runtime = Runtime::GetInstance();
        auto result = runtime->FindService(
            InstanceSpecifier(fmt::format("{}/{}", service_name, instance_id))
        );
        
        if (!result || result.Value().empty()) {
            return Result<>::FromError(ComErrc::kServiceNotFound);
        }
        
        auto service_info = result.Value()[0];
        
        // 3. 验证 Schema Hash
        if (service_info.schema_hash != expected_hash.Value()) {
            LOG_ERROR(
                "Schema Hash mismatch for {}:\n"
                "  Expected: {}\n"
                "  Actual:   {}\n"
                "  ⚠️  Service contract incompatible, refusing connection!",
                service_name,
                expected_hash.Value(),
                service_info.schema_hash
            );
            return Result<>::FromError(ComErrc::kSchemaHashMismatch);
        }
        
        LOG_INFO("✅ Service contract validated: {} (hash={})",
                 service_name, service_info.schema_hash);
        return Result<>::FromValue();
    }
};

// 应用代码
int main() {
    // 验证服务契约
    auto validation_result = ServiceContractValidator::ValidateBeforeConnect(
        "RadarService", "FrontRadar"
    );
    
    if (!validation_result) {
        LOG_FATAL("Service contract validation failed!");
        return 1;
    }
    
    // 继续创建 Proxy
    auto proxy = RadarServiceProxy::Create(/*...*/);
    // ...
}
```

---

#### 5.6.3 双层 IDL 设计优势总结

**✅ 架构优势**:

| **方面** | **传统单层 DDS IDL** | **双层 IDL (Franca + DDS)** |
|---------|---------------------|---------------------------|
| **接口定义** | DDS IDL v4.2 (耦合 DDS) | Franca IDL (平台无关) |
| **代码生成** | 仅 FastDDS-gen | PyFranca (AUTOSAR API) + FastDDS-gen (DDS Types) |
| **应用依赖** | ❌ 必须依赖 FastDDS 库 | ✅ 仅依赖 AUTOSAR API (lap::com) |
| **Transport 抽象** | ❌ DDS 类型泄漏到应用 | ✅ Proxy 层完全隔离 DDS |
| **QoS 配置** | ❌ 写在 IDL 或代码中 | ✅ 独立 YAML 配置，运行时加载 |
| **版本管理** | ❌ 手动维护版本号 | ✅ Franca 强制 Major.Minor.Patch + Schema Hash |
| **一致性验证** | ❌ 仅 DDS TypeIdentifier | ✅ Franca Hash + DDS TypeID 双重验证 |
| **多 Transport 支持** | ❌ 仅支持 DDS | ✅ 同一 Franca IDL 生成 iceoryx2/DDS/SOME-IP |
| **标准合规** | ❌ DDS 特定 | ✅ AUTOSAR ara::com 标准 API |
| **编译隔离** | ❌ 应用必须链接 FastDDS | ✅ 应用无需链接 DDS (仅 Runtime) |
| **升级影响** | ❌ DDS 升级影响所有应用 | ✅ DDS 升级仅影响 Proxy 层 |

**🔥 核心价值**:

1. **单一真相源 (SSOT)**:
   - Franca IDL 是唯一的服务契约定义
   - 所有 ECU 从同一 Git Repository 拉取
   - Schema Hash 自动生成，保证一致性

2. **平台无关性**:
   - 应用代码完全不依赖 DDS
   - 可以无缝切换到其他 Transport (SOME/IP, Vsomeip, Signal)
   - AUTOSAR 标准 API (ara::com) 永不变化

3. **强制一致性验证**:
   - **CI/CD 阶段**: Franca IDL 语法验证 + 版本策略检查
   - **构建阶段**: Schema Hash 一致性验证
   - **运行时**: SD-Proxy 拒绝不兼容的服务

4. **QoS 灵活配置**:
   - QoS 不污染 IDL 定义
   - 不同 ECU/环境可使用不同 QoS
   - QoS 修改不需要重新生成代码

5. **编译隔离**:
   ```bash
   # 应用编译依赖 (非常轻量)
   Radar App → lap_com_runtime → AUTOSAR API (Header-only)
   
   # Runtime 编译依赖 (内部隔离)
   lap_com_runtime → DDS Proxy Layer → FastDDS (仅此处链接)
   ```

**📊 开发流程对比**:

```text
┌─────────────────────────────────────────────────────────────────────┐
│ 传统单层 DDS IDL 流程                                                │
├─────────────────────────────────────────────────────────────────────┤
│ 1. 手写 DDS IDL (RadarService.idl)                                  │
│ 2. FastDDS-gen 生成 DDS Types                                       │
│ 3. ❌ 应用直接使用 DDS API (DataWriter/DataReader)                  │
│ 4. ❌ QoS 写在代码或 XML 中                                         │
│ 5. ❌ 每个 ECU 可能有不同的 IDL 版本                                │
│ 6. ❌ 版本不兼容只能运行时发现 (崩溃/数据错乱)                      │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│ 双层 IDL (Franca + DDS) 流程                                         │
├─────────────────────────────────────────────────────────────────────┤
│ 1. 定义 Franca IDL (RadarService.fidl)                              │
│    ├─ 强制版本: version { major 1 minor 2 patch 3 }                │
│    └─ 自动生成 Schema Hash: a3f7c9e2b5d14a8c                        │
│                                                                      │
│ 2. PyFranca 生成 AUTOSAR API                                         │
│    ├─ RadarServiceSkeleton.h/cpp (Provider)                         │
│    ├─ RadarServiceProxy.h/cpp (Consumer)                            │
│    └─ Schema Hash 嵌入到 API 中                                     │
│                                                                      │
│ 3. Franca → DDS IDL 转换                                             │
│    ├─ 自动注入 Schema Hash                                          │
│    ├─ 自动注入 @version("1.2.3")                                    │
│    └─ 生成 DDS TypeSupport (FastDDS-gen)                            │
│                                                                      │
│ 4. ✅ 应用仅使用 AUTOSAR API (完全不知道 DDS)                       │
│ 5. ✅ QoS 从 YAML 加载 (qos_config/radar_service.yaml)              │
│ 6. ✅ CI/CD 验证所有 ECU 使用相同 Schema Hash                       │
│ 7. ✅ Runtime 启动时强制验证，拒绝不兼容服务                        │
└─────────────────────────────────────────────────────────────────────┘
```

**🎯 实际效果**:

- **编译时安全**: CI/CD 阻止不兼容的 Schema Hash 合并
- **运行时安全**: SD-Proxy 拒绝连接不兼容的服务
- **开发效率**: 应用开发者无需关心 DDS/SOME-IP 细节
- **维护成本**: IDL 变化时，仅重新生成代码即可
- **升级路径**: DDS 版本升级不影响应用代码

---

## 第 6 章: API 使用示例
        // 检查本地服务版本是否与 Contract Repository 一致
        auto contract_version = LoadContractVersion(service.name);
        
        if (service.major != contract_version.major ||
            service.minor < contract_version.minor) {
            
            LOG_ERROR("Service version mismatch: {} (local: {}.{}, contract: {}.{})",
                      service.name,
                      service.major, service.minor,
                      contract_version.major, contract_version.minor);
            
            // ASIL 服务版本不一致：拒绝启动
            if (service.asil_level >= ASILLevel::ASIL_C) {
                throw std::runtime_error("ASIL service version mismatch");
            }
        }
    }
}
```

---

## 第 6 章: API 使用示例

### 6.1 服务提供者 (Skeleton)

```cpp
#include "ara/com/sample/radar_service_skeleton.hpp"

using namespace lap::com::sample::skeleton;

int main() {
    // 1. 初始化 lap::com Runtime
    lap::com::Runtime::GetInstance().Initialize();
    
    // 2. 创建 Skeleton 实例
    ara::core::InstanceSpecifier instance_spec("RadarService/Instance1");
    
    auto skeleton_result = RadarServiceSkeleton::Create(instance_spec);
    if (!skeleton_result) {
        std::cerr << "Failed to create skeleton" << std::endl;
        return 1;
    }
    
    auto skeleton = std::move(skeleton_result.Value());
    
    // 3. 注册方法处理器
    skeleton->RegisterMethodHandler(
        &RadarServiceSkeleton::GetObjectList,
        [](const GetObjectListInput& input) -> GetObjectListOutput {
            // 业务逻辑
            return GenerateObjectList();
        }
    );
    
    // 4. 提供服务 (触发服务发现)
    auto offer_result = skeleton->OfferService();
    if (!offer_result) {
        std::cerr << "Failed to offer service: " 
                  << offer_result.Error().Message() << std::endl;
        return 1;
    }
    
    std::cout << "RadarService offered successfully" << std::endl;
    
    // 5. 运行事件循环
    while (true) {
        // 发送事件
        RadarObjectList objects = GetCurrentObjects();
        skeleton->GetObjectListEvent().Send(objects);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 6. 停止服务
    skeleton->StopOfferService();
    
    return 0;
}
```

### 6.2 服务消费者 (Proxy) - 单次查询

```cpp
#include "ara/com/sample/radar_service_proxy.hpp"

using namespace lap::com::sample::proxy;

int main() {
    // 1. 初始化 Runtime
    lap::com::Runtime::GetInstance().Initialize();
    
    // 2. 查找服务 (单次查询)
    ara::core::InstanceSpecifier instance_spec("RadarService/Instance1");
    
    auto find_result = RadarServiceProxy::FindService(instance_spec);
    if (!find_result || find_result.Value().empty()) {
        std::cerr << "Service not found" << std::endl;
        return 1;
    }
    
    // 3. 创建 Proxy 实例
    auto proxy_result = RadarServiceProxy::Create(find_result.Value()[0]);
    if (!proxy_result) {
        std::cerr << "Failed to create proxy" << std::endl;
        return 1;
    }
    
    auto proxy = std::move(proxy_result.Value());
    
    // 4. 订阅事件
    proxy->GetObjectListEvent().Subscribe(
        [](const RadarObjectList& objects) {
            std::cout << "Received " << objects.size() << " objects" << std::endl;
        }
    );
    
    // 5. 调用方法
    auto method_result = proxy->GetObjectList().GetResult();
    if (method_result) {
        std::cout << "Method call succeeded" << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    return 0;
}
```

### 6.3 服务消费者 - 持续发现

```cpp
int main() {
    lap::com::Runtime::GetInstance().Initialize();
    
    // 使用 StartFindService 持续监控服务可用性
    lap::com::InstanceIdentifier instance_id("RadarService.Instance1");
    
    auto find_handle_result = RadarServiceProxy::StartFindService(
        instance_id,
        [](const lap::com::ServiceHandleContainer<RadarServiceProxy::HandleType>& handles,
           lap::com::FindServiceStatus status) {
            
            if (status == lap::com::FindServiceStatus::kServiceAvailable) {
                std::cout << "Service available: " << handles.size() << " instances" << std::endl;
                
                // 为每个新发现的实例创建 Proxy
                for (const auto& handle : handles) {
                    auto proxy = RadarServiceProxy::Create(handle);
                    if (proxy) {
                        // 订阅事件
                        proxy.Value()->GetObjectListEvent().Subscribe(...);
                    }
                }
            } else if (status == lap::com::FindServiceStatus::kServiceUnavailable) {
                std::cout << "Service unavailable" << std::endl;
                // 清理 Proxy 实例
            }
        }
    );
    
    if (!find_handle_result) {
        std::cerr << "StartFindService failed" << std::endl;
        return 1;
    }
    
    lap::com::FindServiceHandle handle = find_handle_result.Value();
    
    // 运行一段时间
    std::this_thread::sleep_for(std::chrono::minutes(1));
    
    // 停止持续发现
    RadarServiceProxy::StopFindService(handle);
    
    return 0;
}
```

---

## 第 7 章: 配置与清单

### 7.1 ServiceInterfaceDeployment (服务接口部署配置)

```xml
<!-- ARXML 配置示例 -->
<SERVICE-INTERFACE-DEPLOYMENT>
  <SHORT-NAME>RadarServiceDeployment</SHORT-NAME>
  <SERVICE-INTERFACE-REF DEST="SERVICE-INTERFACE">/Services/RadarService</SERVICE-INTERFACE-REF>
  
  <!-- 服务发现配置 -->
  <SERVICE-INTERFACE-VERSION>
    <MAJOR-VERSION>1</MAJOR-VERSION>
    <MINOR-VERSION>0</MINOR-VERSION>
  </SERVICE-INTERFACE-VERSION>
  
  <!-- 传输绑定 -->
  <NETWORK-BINDING>
    <BINDING-TYPE>SOME-IP</BINDING-TYPE>
    
    <!-- SOME/IP Service Discovery 配置 -->
    <SOMEIP-SERVICE-DISCOVERY>
      <SOMEIP-SERVICE-ID>0x1234</SOMEIP-SERVICE-ID>
      <SOMEIP-SD-SERVER-CONFIG>
        <INITIAL-DELAY-MIN>0</INITIAL-DELAY-MIN>
        <INITIAL-DELAY-MAX>10</INITIAL-DELAY-MAX>
        <REPETITIONS-BASE-DELAY>10</REPETITIONS-BASE-DELAY>
        <REPETITIONS-MAX>3</REPETITIONS-MAX>
        <TTL>3</TTL>  <!-- 秒 -->
        <CYCLIC-OFFER-DELAY>1000</CYCLIC-OFFER-DELAY>  <!-- 毫秒 -->
      </SOMEIP-SD-SERVER-CONFIG>
    </SOMEIP-SERVICE-DISCOVERY>
  </NETWORK-BINDING>
</SERVICE-INTERFACE-DEPLOYMENT>
```

### 7.2 ProvidedServiceInstance (服务提供实例)

```xml
<PROVIDED-SERVICE-INSTANCE>
  <SHORT-NAME>RadarServiceInstance1</SHORT-NAME>
  <SERVICE-INTERFACE-DEPLOYMENT-REF>/Deployments/RadarServiceDeployment</SERVICE-INTERFACE-DEPLOYMENT-REF>
  
  <!-- 实例标识符 -->
  <INSTANCE-ID>
    <INSTANCE-SPECIFIER>/RadarService/Instance1</INSTANCE-SPECIFIER>
  </INSTANCE-ID>
  
  <!-- SOME/IP 配置 -->
  <SOMEIP-PROVIDED-SERVICE-INSTANCE>
    <SOMEIP-INSTANCE-ID>0x0001</SOMEIP-INSTANCE-ID>
    
    <!-- SD 配置 -->
    <SOMEIP-SD-SERVER-EVENT-GROUP-TIMING-CONFIG>
      <INITIAL-DELAY-MIN-VALUE>0</INITIAL-DELAY-MIN-VALUE>
      <INITIAL-DELAY-MAX-VALUE>10</INITIAL-DELAY-MAX-VALUE>
      <REQUEST-RESPONSE-DELAY-MIN-VALUE>10</REQUEST-RESPONSE-DELAY-MIN-VALUE>
      <REQUEST-RESPONSE-DELAY-MAX-VALUE>50</REQUEST-RESPONSE-DELAY-MAX-VALUE>
    </SOMEIP-SD-SERVER-EVENT-GROUP-TIMING-CONFIG>
  </SOMEIP-PROVIDED-SERVICE-INSTANCE>
</PROVIDED-SERVICE-INSTANCE>
```

### 7.3 RequiredServiceInstance (服务消费实例)

```xml
<REQUIRED-SERVICE-INSTANCE>
  <SHORT-NAME>RadarServiceClientInstance</SHORT-NAME>
  <SERVICE-INTERFACE-DEPLOYMENT-REF>/Deployments/RadarServiceDeployment</SERVICE-INTERFACE-DEPLOYMENT-REF>
  
  <!-- 版本要求 -->
  <REQUIRED-SERVICE-VERSION>
    <MAJOR-VERSION>1</MAJOR-VERSION>
    <MINOR-VERSION>0</MINOR-VERSION>
  </REQUIRED-SERVICE-VERSION>
  
  <!-- 版本黑名单 (不兼容的版本) -->
  <DDS-REQUIRED-SERVICE-INSTANCE>
    <BLOCKLISTED-VERSIONS>
      <BLOCKLISTED-VERSION>
        <MAJOR-VERSION>1</MAJOR-VERSION>
        <MINOR-VERSION>2</MINOR-VERSION>  <!-- 1.2 版本有 Bug -->
      </BLOCKLISTED-VERSION>
    </BLOCKLISTED-VERSIONS>
  </DDS-REQUIRED-SERVICE-INSTANCE>
</REQUIRED-SERVICE-INSTANCE>
```

---

## 第 8 章: 性能优化

### 8.1 服务缓存

```cpp
class ServiceRegistry {
private:
    // LRU 缓存 (最近最少使用)
    template<typename Key, typename Value>
    class LRUCache {
    public:
        LRUCache(size_t capacity) : capacity_(capacity) {}
        
        std::optional<Value> Get(const Key& key) {
            auto it = cache_.find(key);
            if (it == cache_.end()) {
                return std::nullopt;
            }
            
            // 移动到最前面 (最近使用)
            lru_list_.splice(lru_list_.begin(), lru_list_, it->second.list_iter);
            
            return it->second.value;
        }
        
        void Put(const Key& key, const Value& value) {
            auto it = cache_.find(key);
            
            if (it != cache_.end()) {
                // 更新现有条目
                it->second.value = value;
                lru_list_.splice(lru_list_.begin(), lru_list_, it->second.list_iter);
            } else {
                // 插入新条目
                if (cache_.size() >= capacity_) {
                    // 淘汰最少使用的条目
                    auto lru_key = lru_list_.back();
                    lru_list_.pop_back();
                    cache_.erase(lru_key);
                }
                
                lru_list_.push_front(key);
                cache_[key] = {value, lru_list_.begin()};
            }
        }
        
    private:
        size_t capacity_;
        std::list<Key> lru_list_;
        
        struct CacheEntry {
            Value value;
            typename std::list<Key>::iterator list_iter;
        };
        
        std::unordered_map<Key, CacheEntry> cache_;
    };
    
    // 远程服务缓存 (避免重复查询)
    LRUCache<InstanceIdentifier, ServiceInstanceInfo> remote_service_cache_{1000};
};
```

### 8.2 批量查询优化

```cpp
// 批量查找多个服务 (减少锁竞争)
Result<std::map<InstanceIdentifier, ServiceHandleContainer<HandleType>>>
ServiceDiscoveryManager::FindServicesBatch(
    const std::vector<InstanceIdentifier>& instance_filters
) {
    std::map<InstanceIdentifier, ServiceHandleContainer<HandleType>> results;
    
    // 一次性获取读锁
    std::shared_lock lock(service_registry_->registry_mutex_);
    
    for (const auto& instance_id : instance_filters) {
        auto handles = FindService(instance_id);
        if (handles) {
            results[instance_id] = std::move(handles.Value());
        }
    }
    
    return Result<...>::FromValue(std::move(results));
}
```

### 8.3 异步服务发现

```cpp
// 异步 FindService (避免阻塞)
std::future<Result<ServiceHandleContainer<HandleType>>>
ServiceDiscoveryManager::FindServiceAsync(
    const InstanceIdentifier& instance_filter
) {
    return std::async(std::launch::async, [this, instance_filter]() {
        return FindService(instance_filter);
    });
}
```

---

## 第 9 章: 错误处理

### 9.1 ComErrc 错误码

```cpp
namespace lap::com {

enum class ComErrc : uint32_t {
    kServiceNotAvailable = 1,        // 服务不可用
    kNetworkBindingFailure = 2,      // 网络绑定失败
    kServiceVersionMismatch = 3,     // 服务版本不匹配
    kServiceNotOffered = 4,          // 服务未提供
    kFieldValueIsNotValid = 5,       // 字段值无效
    kMaxSamplesExceeded = 6,         // 超过最大样本数
    kWrongMethodCallProcessingMode = 7,  // 错误的方法调用处理模式
    kIllegalUseOfAllocate = 8,       // 非法使用 Allocate
    kCommunicationStackError = 9,    // 通信栈错误
    kInvalidServiceInterface = 10,   // 无效的服务接口
    kErroneousServiceHandle = 11,    // 错误的服务句柄
    kGrantEnforcementError = 12,     // 权限强制错误
};

} // namespace lap::com
```

### 9.2 错误处理示例

```cpp
auto find_result = RadarServiceProxy::FindService(instance_spec);

if (!find_result) {
    auto error_code = find_result.Error();
    
    switch (error_code.Value()) {
        case lap::com::ComErrc::kServiceNotAvailable:
            std::cerr << "Service not available, will retry..." << std::endl;
            // 重试逻辑
            std::this_thread::sleep_for(std::chrono::seconds(1));
            break;
            
        case lap::com::ComErrc::kNetworkBindingFailure:
            std::cerr << "Network binding failure: " 
                      << error_code.Message() << std::endl;
            // 检查网络配置
            break;
            
        case lap::com::ComErrc::kServiceVersionMismatch:
            std::cerr << "Service version mismatch" << std::endl;
            // 检查清单配置
            break;
            
        default:
            std::cerr << "Unknown error: " << error_code.Message() << std::endl;
            break;
    }
}
```

---

## 第 10 章: 测试策略

### 10.1 单元测试

```cpp
TEST(ServiceRegistryTest, RegisterAndFindLocalService) {
    ServiceRegistry registry;
    
    // 注册服务
    ServiceRegistry::ServiceInstanceInfo info{
        .instance_id = InstanceIdentifier("TestService.Instance1"),
        .service_interface_name = "TestService",
        .major_version = 1,
        .minor_version = 0,
        .state = ServiceRegistry::ServiceState::OFFERED
    };
    
    auto register_result = registry.RegisterLocalService(info);
    ASSERT_TRUE(register_result);
    
    // 查找服务
    auto find_result = registry.FindServices("TestService");
    ASSERT_TRUE(find_result);
    EXPECT_EQ(find_result.Value().size(), 1);
    EXPECT_EQ(find_result.Value()[0].instance_id, info.instance_id);
}

TEST(ServiceRegistryTest, VersionCompatibility) {
    // 主版本号不匹配
    EXPECT_FALSE(ServiceRegistry::IsVersionCompatible(1, 0, 2, 0));
    
    // 次版本号向后兼容
    EXPECT_TRUE(ServiceRegistry::IsVersionCompatible(1, 0, 1, 5));
    EXPECT_FALSE(ServiceRegistry::IsVersionCompatible(1, 5, 1, 0));
}
```

### 10.2 集成测试

```cpp
TEST(ServiceDiscoveryIntegrationTest, OfferAndFindService) {
    // 1. 创建 Skeleton 提供服务
    auto skeleton = TestServiceSkeleton::Create(
        InstanceSpecifier("TestService/Instance1")
    );
    ASSERT_TRUE(skeleton);
    
    auto offer_result = skeleton.Value()->OfferService();
    ASSERT_TRUE(offer_result);
    
    // 2. 等待服务发现传播
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 3. Proxy 查找服务
    auto find_result = TestServiceProxy::FindService(
        InstanceIdentifier("TestService.Instance1")
    );
    ASSERT_TRUE(find_result);
    EXPECT_FALSE(find_result.Value().empty());
    
    // 4. 创建 Proxy
    auto proxy = TestServiceProxy::Create(find_result.Value()[0]);
    ASSERT_TRUE(proxy);
    
    // 5. 测试通信
    auto method_result = proxy.Value()->TestMethod();
    EXPECT_TRUE(method_result);
}
```

### 10.3 性能测试

```cpp
TEST(ServiceDiscoveryPerformanceTest, FindServiceLatency) {
    const int NUM_ITERATIONS = 1000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        auto result = TestServiceProxy::FindService(
            InstanceIdentifier("TestService.Instance1")
        );
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double avg_latency = duration.count() / static_cast<double>(NUM_ITERATIONS);
    
    std::cout << "Average FindService latency: " << avg_latency << " μs" << std::endl;
    
    // 性能目标: < 100 μs
    EXPECT_LT(avg_latency, 100.0);
}
```

---

## 第 11 章: 实施路线图

### Week 1-2: 核心框架 + 双层 IDL 工具链

- ✅ ServiceRegistry 实现
- ✅ ServiceDiscoveryManager 基础框架
- ✅ FindServiceHandlerManager 实现
- ✅ 单元测试 (覆盖率 >80%)
- 📋 **Franca IDL 工具链搭建**
  - PyFranca 环境配置
  - generate_schema_hash.py 实现
  - franca_to_dds_idl.py 转换工具
  - 示例服务 Franca IDL 定义 (RadarService.fidl)

### Week 3-4: Transport Binding 插件集成 + 代码生成

- ✅ ITransportBinding 接口定义
- ✅ iceoryx2 Binding 服务发现（共享内存元数据）
- ✅ iceoryx2 Binding 服务发现（共享内存直接访问）
- ✅ DDS Binding 服务发现（Topic-Based Discovery）
- ✅ Custom Protocol Binding 服务发现
- 📋 **双层 IDL 代码生成集成**
  - PyFranca 生成 AUTOSAR API (Skeleton/Proxy)
  - FastDDS-gen 生成 DDS TypeSupport
  - CMake 构建系统集成
  - Schema Hash 嵌入和验证

### Week 5-6: API 实现 + 版本一致性验证

- ✅ Runtime::FindService() 实现
- ✅ Runtime::StartFindService() 实现
- ✅ Skeleton::OfferService() 实现
- ✅ Skeleton::StopOfferService() 实现
- ✅ 集成测试
- 📋 **版本一致性验证**
  - Schema Hash 验证逻辑
  - DDS TypeIdentifier 验证
  - Runtime 启动时契约验证
  - 不兼容服务拒绝机制

### Week 7-8: 性能优化与生产就绪 + QoS 独立配置

- ✅ 服务缓存优化 (LRU)
- ✅ 批量查询优化
- ✅ 异步服务发现
- ✅ 性能基准测试
- ✅ 文档完善（YAML 配置标准化）
- 📋 **QoS 独立配置**
  - QoS YAML 配置文件设计
  - Runtime 加载 QoS 配置
  - DDS QoS 映射实现
  - 多环境 QoS 配置支持

### Week 9-10: Runtime 集成与优化 ⭐

- 📋 **Runtime.hpp/cpp 实现**
  - SharedMemoryRegistry 集成
  - FindService/OfferService API 实现
  - 心跳线程管理
  - 槽位分配策略

- 📋 **性能优化**
  - seqlock 优化（减少重试）
  - CPU 缓存对齐（cache line）
  - NUMA 感知内存分配

- 📋 **配置与部署**
  - YAML 配置文件支持
  - Franca IDL 工具链集成
  - Systemd socket activation 服务单元
  - 性能基准测试（延迟 < 500ns）

- 📋 **测试与验证**
  - 单元测试（SharedMemoryRegistry）
  - 集成测试（零守护进程架构）
  - 性能测试（延迟 < 500ns）
  - 压力测试（并发访问）
  - Schema Hash 一致性测试

### Week 11-12: 跨节点服务发现 + CI/CD 集成

- 📋 **Fast DDS Discovery Server 集成**
  - Discovery Server 部署（systemd service）
  - Discovery Client 集成到 Runtime
  - 远程服务缓存（LRU Cache + TTL）
  - 性能测试（首次发现 < 100ms，缓存命中 < 10ms）

- 📋 **SOME/IP-SD 支持**
  - OfferService / FindService 消息处理
  - UDP 多播配置
  - 与本地槽位集成

- 📋 **CI/CD Pipeline**
  - Franca IDL 验证 (pyfranca-validate)
  - Schema Hash 一致性检查
  - 版本策略合规性验证
  - 自动代码生成和测试
  - 跨 ECU 兼容性测试

- 📋 **Service Contract Repository**
  - Git Repository 搭建
  - 版本锁定策略
  - 多 ECU 协作流程
  - 自动化发布流程

- 📋 **统一路由逻辑**
  - 本地优先查找（< 500ns）
  - 远程透明回退（< 100ms）
  - 配置驱动启用/禁用

---

## 第 12 章: 性能指标

### 12.1 服务发现延迟对比（v3.0 零守护进程架构）

| 发现方式 | 平均延迟 | P99 延迟 | 适用场景 |
|----------|----------|----------|----------|
| **固定槽位查找** (Shared Memory) | < 100 ns | < 500 ns | 本地服务发现（推荐）⭐ |
| **iceoryx2 Binding 发现** | 1-3 ms | 5 ms | 本地零拷贝服务 |
| **DDS Binding 发现** | 10-30 ms | 100 ms | 跨 ECU 服务 |

### 12.2 资源占用

**v3.0 零 Daemon 架构**:

- 内存: ~256 KB (槽位数组) + ~8 KB (心跳线程)
- CPU: < 0.5% (空闲)，< 2% (峰值，心跳检查)
- 磁盘: 无持久化存储（所有数据在共享内存）
- 依赖: iceoryx2 库，无其他外部依赖
- 端口: 无（共享内存访问）

**单进程开销**:

- 内存: < 300 KB total
- 共享内存访问: mmap 只读/读写，无拷贝
- 线程: 1 个心跳线程（可选）

### 12.3 吞吐量

| 操作 | 吞吐量 (ops/s) |
|------|----------------|
| RegisterService | 50,000+ |
| FindService (缓存命中) | 200,000+ |
| FindService (注册中心) | 100,000+ |
| 并发查询 (4 线程) | 300,000+ |

---

## 附录

### 附录 A: 设计背景

**AUTOSAR R24-11 标准支持**:

AUTOSAR AP R24-11 正式引入了两种服务发现优化机制：

1. **Static Service Connection** (静态服务连接) - SWS_CM_02201
   - 通过静态预配置的应用端点绕过服务发现协议
   - 适用于 SOME/IP 绑定（ProvidedSomeipServiceInstance / RequiredSomeipServiceInstance）
   - 参考文档：
     - [SWS_CM_02201] - 静态服务连接
     - [TPS_MANI_03312] - 静态配置远程对等地址（提供者）
     - [TPS_MANI_03313] - 事件组语义
     - [TPS_MANI_03314] - 静态配置远程对等地址（消费者）

2. **Central vs Distributed Service Discovery** (集中式 vs 分布式服务发现)
   - AUTOSAR_AP_EXP_ARAComAPI.pdf 第 7.2.1 章节明确定义了两种架构：
   - **集中式方法**：中央守护进程维护服务注册表，处理所有 FindService/OfferService 请求
   - **分布式方法**：服务注册信息分布在各 lap::com 应用中，通过广播通信同步

**本设计采用零 Daemon 创新架构**：

基于 AUTOSAR R24-11 标准，本设计突破传统限制：

- ✅ **零守护进程**：完全去中心化，无任何 Daemon
- ✅ **固定槽位映射**：编译期或静态配置确定
- ✅ **共享内存直接访问**：iceoryx2 + memfd，< 500ns 延迟
- ✅ **Lock-Free 同步**：seqlock 机制，100% 确定性
- ✅ **自动生命周期管理**：心跳机制 + 进程退出自动清理

**设计原则**:

1. ✅ **标准兼容性**: 完全符合 AUTOSAR R24-11 规范
2. ✅ **API 兼容性**: 与 AUTOSAR FindService/OfferService API 完全兼容
3. ✅ **性能突破**: < 500ns 延迟，远超传统方案
4. ✅ **零单点故障**: 无守护进程依赖
5. ✅ **FuSa 就绪**: QM/ASIL 物理隔离

### 附录 B: AUTOSAR R24-11 标准对比

#### B.1 三种服务发现机制对比

AUTOSAR R24-11 支持三种服务发现机制：

| 机制 | AUTOSAR 标准 | 适用场景 | 延迟 | 可靠性 | 本设计支持 |
|------|-------------|----------|------|--------|-----------||
| **静态服务连接** | SWS_CM_02201 | 固定拓扑、已知服务位置 | 0ms | 高 | ✅ 支持（YAML配置） |
| **零 Daemon 注册表** | 创新设计 | 本地高性能服务 | < 500ns | 高 | ✅ 本设计核心 |
| **动态服务发现** | SWS_CM_00050+ | 完全动态、跨节点 | 5-100ms | 高 | ✅ 支持（可选） |

#### B.2 架构优势总结

本设计相比传统方案的核心优势：

1. **极致性能**:
   - FindService < 500ns (vs 传统方案 1-5ms)
   - 零拷贝、零系统调用
   - Lock-free 读取

2. **零单点故障**:
   - 无守护进程依赖
   - 进程间完全对等
   - 自动故障恢复

3. **标准完全兼容**:
   - 应用代码使用标准 lap::com API（100% 兼容 ara::com）
   - 配置文件符合 ARXML 规范
   - 运行时行为符合 SWS 要求

### 附录 C: 配置文件集成（YAML 格式）

**本架构使用统一的 YAML 配置格式**，基于 Franca IDL 的双层设计。

#### C.1 slot_mapping.yaml（服务槽位映射）

配置文件示例见第 2 章相关章节。

#### C.2 Franca IDL 工具链

**双层 IDL 代码生成工具**:

```bash
# === 完整工作流 ===

# 1. 验证 Franca IDL
pyfranca-validate services/radar/RadarService.fidl

# 2. 生成 Schema Hash
SCHEMA_HASH=$(python3 tools/generate_schema_hash.py \
  services/radar/RadarService.fidl)
echo "Schema Hash: $SCHEMA_HASH"

# 3. 生成 AUTOSAR API (PyFranca)
pyfranca-gen-cpp \
  --input services/radar/RadarService.fidl \
  --output generated/radar/autosar \
  --namespace lap::com::radar \
  --schema-hash "$SCHEMA_HASH"

# 4. 转换为 DDS IDL
python3 tools/franca_to_dds_idl.py \
  --input services/radar/RadarService.fidl \
  --output generated/radar/dds/RadarService.idl \
  --schema-hash "$SCHEMA_HASH" \
  --version "1.2.3"

# 5. 生成 DDS TypeSupport (FastDDS-gen)
fastddsgen -replace -typeobject \
  -d generated/radar/dds \
  generated/radar/dds/RadarService.idl

# === 批量生成所有服务 ===
bash tools/generate_all.sh
```

**支持的工具**:

- `pyfranca-validate`: Franca IDL 语法验证
- `generate_schema_hash.py`: 生成 Franca Schema Hash (SHA-256)
- `pyfranca-gen-cpp`: 生成 AUTOSAR C++ API (Skeleton/Proxy)
- `franca_to_dds_idl.py`: Franca IDL → DDS IDL v4.2 转换
- `validate_hash_consistency.py`: Schema Hash 一致性验证
- `version_policy_checker.py`: 版本策略合规性检查
- `generate_all.sh`: 批量生成所有服务代码

**QoS 配置管理**:

QoS 配置独立于 IDL，放在单独的 YAML 文件中：

```bash
# QoS 配置文件位置
qos_config/
├── radar_service.yaml
├── camera_service.yaml
└── brake_service.yaml
```

详见: 
- `tools/franca_idl_toolchain/README.md`
- 第 5.6 节 "接口一致性保证机制"

### 附录 D: 基准测试与性能优化

**性能基准测试环境**:

- CPU: Intel Xeon E5-2690 v4 @ 2.6GHz
- Memory: DDR4-2400 128GB
- OS: Ubuntu 22.04 (kernel 5.15, PREEMPT_RT patch)
- Compiler: GCC 11.4 (-O3 -march=native)

**FindService 延迟分析**:

| 操作 | P50 | P99 | P99.9 | 最坏情况 |
|------|-----|-----|-------|---------||
| 静态配置命中 | 50ns | 80ns | 100ns | 150ns |
| 共享内存读取 | 300ns | 450ns | 500ns | 800ns |
| seqlock 重试 | 350ns | 500ns | 600ns | 1μs |

**内存占用**:

- 槽位数组: 1024 slots × 256 bytes = 256 KB
- 心跳线程: ~8 KB stack
- 总开销: < 300 KB per process

---

> **历史版本说明**: v2.0 架构基于 Fast-DDS Discovery Server 的设计文档已归档至 `archive/SERVICE_DISCOVERY_V2_FAST_DDS_DESIGN.md`，当前文档描述 v3.0 零守护进程架构。

---

## 参考资源

### 标准文档

- AUTOSAR_AP_SWS_CommunicationManagement (R24-11, November 2024)
- AUTOSAR_AP_TPS_ManifestSpecification (R24-11, TPS_MANI_03312-03315)
- AUTOSAR_AP_SWS_NetworkManagement
- **Franca IDL Specification** (Common API C++ IDL)
- SOME/IP Service Discovery Protocol Specification v1.4
- OMG DDS Discovery Protocol Specification v2.5
- Fast DDS IDL v4.2 Specification
- iceoryx2 Service Discovery Design

### 相关章节

- SWS_CM_00110 - SWS_CM_00125: Service Discovery APIs
- SWS_CM_02201: Static Service Instance Configuration
- TPS_MANI_03312: Static Service Manifest (YAML 转换支持)
- SWS_CM_11001 - SWS_CM_11014: DDS Binding Service Discovery
- SWS_CM_90502 - SWS_CM_90518: DDS Topic-Based Discovery

### 依赖库

- **PyFranca**: Franca IDL 解析和代码生成 (<https://github.com/zayfod/pyfranca>)
- **FastDDS**: eProsima Fast DDS (RTPS 实现) (<https://github.com/eProsima/Fast-DDS>)
- **fastddsgen**: Fast DDS IDL 代码生成器
- yaml-cpp: 配置解析 (<https://github.com/jbeder/yaml-cpp>)
- iceoryx2: 零拷贝通信与本地服务发现 (<https://github.com/eclipse-iceoryx/iceoryx2>)

### 工具链

- **Franca IDL 工具链**: `tools/franca_idl_toolchain/`
  - `generate_schema_hash.py`: Schema Hash 生成
  - `franca_to_dds_idl.py`: Franca → DDS IDL 转换
  - `validate_hash_consistency.py`: 一致性验证
  - `version_policy_checker.py`: 版本策略检查
  - `generate_all.sh`: 批量代码生成脚本

---

**文档版本**: 3.0 (零守护进程架构)  
**最后更新**: 2025-11-19  
**作者**: LightAP Com Module Team  
**命名空间**: lap::com (100% 兼容 AUTOSAR ara::com)  
**配置格式**: YAML (使用 arxml2yaml 工具转换 AUTOSAR ARXML)

