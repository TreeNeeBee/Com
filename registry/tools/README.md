# LightAP Registry Tools

Registry 诊断与管理工具集，供系统分析和开发人员使用。

## 工具列表

### `lap-registry-ctl` (v2.0 — Core IPC 集成版)

注册表控制工具 —— 集成 Core IPC 模块和 LogAndTrace 模块，支持标准 IPC 流程
注册/注销服务。

**v2.0 变更：**
- ✅ 集成 `CRegistryProxy` → Core IPC MPSC 标准注册/注销流程
- ✅ 集成 `LAP_COM_LOG_*` 结构化日志（替代 printf/stderr）
- ✅ 智能降级：IPC 不可用时自动回退 UDS 直写 mmap
- ✅ 服务 ID 驱动的注册（daemon 分配 slot），取代旧版 slot-based 手动指定

#### 安装

```bash
# 方式 1: CMake 构建 (Com 模块启用时)
cd build && cmake .. && make lap-registry-ctl
sudo install -m 755 lap-registry-ctl /usr/local/bin/

# 方式 2: 手动编译 (需要 lap_core + lap_log 共享库)
cd modules/Com/registry/tools
g++ -std=c++17 -Wall -Wextra -O2 \
  -I../inc -I../../source/inc -I../../build/include \
  -I$(PROJECT_ROOT)/modules/Core/source/inc \
  -I$(PROJECT_ROOT)/modules/Core/source/inc/ipc \
  -I$(PROJECT_ROOT)/modules/LogAndTrace/source/inc \
  -o lap-registry-ctl \
  lap-registry-ctl.cpp ../src/CServiceRegistry.cpp ../src/CRegistryProxy.cpp \
  -L/usr/local/lib -llap_core -llap_log -lpthread -lrt
sudo install -m 755 lap-registry-ctl /usr/local/bin/
```

#### 命令概览

| 命令 | 说明 | 示例 |
|------|------|------|
| `list` | 列出所有活跃服务 | `lap-registry-ctl list` |
| `status` | 注册表摘要与统计 | `lap-registry-ctl status` |
| `register` | 注册服务 (Core IPC / 直写回退) | `lap-registry-ctl register 0x100A` |
| `unregister` | 注销服务 (Core IPC / 直写回退) | `lap-registry-ctl unregister 0x100A` |
| `inspect` | 槽位详情 + 原始 hex dump | `lap-registry-ctl inspect 10` |
| `watch` | 实时监控槽位变化 | `lap-registry-ctl watch` |

#### 全局选项

```
--socket=<path>   UDS 套接字路径 (默认: /run/lap/registry_qm.sock)
--type=<qm|asil>  注册表类型快捷方式
--timeout=<ms>    IPC 超时 (register/unregister, 默认: 5000)
--no-color        禁用彩色输出
--help, -h        显示帮助
```

#### 命令详解

##### `list [--all]`

列出所有活跃（ACTIVE）和注销中（UNREGISTERING）的服务槽位。

```bash
# QM 注册表 (默认)
lap-registry-ctl list

# ASIL 注册表
lap-registry-ctl --type=asil list

# 包含空闲槽位
lap-registry-ctl list --all
```

输出字段：

| 字段 | 说明 |
|------|------|
| SLOT | 槽位索引 (0~1023) |
| STATUS | IDLE / ACTIVE / UNREG |
| SVC\_ID | 服务 ID (64-bit hex) |
| INST\_ID | 实例 ID (64-bit hex, 编码 ASIL/域/实例号) |
| VER | 主版本.子版本 |
| ASIL | 安全等级 (QM/A/B/C/D) |
| BINDING | 绑定类型 (coreipc/dds/someip) |
| ENDPOINT | 网络端点地址 |
| PID | 拥有者进程 ID (绿色=存活, 红色=僵尸) |
| HEARTBEAT | 最后心跳时间 |

##### `status`

显示注册表全局状态，包括：
- 槽位利用率（活跃/注销中/空闲）
- QM vs ASIL 服务统计
- 健康状态（僵尸检测、心跳范围）
- Discovery Server 模式（CLIENT/SIMPLE）
- 保留槽位状态

```bash
lap-registry-ctl status
lap-registry-ctl --type=asil status
```

##### `register <svc_id> [options]`

注册服务。优先通过 Core IPC 标准流程（CRegistryProxy → MPSC → CRegistryDispatcher），
IPC 不可用时自动回退到 UDS 共享内存直写。

```bash
# 基本注册 (slot = svc_id & 1023)
lap-registry-ctl register 0x100A

# 完整参数
lap-registry-ctl register 0x100A \
    --instance=0x00010001 \
    --version=2.1 \
    --binding=dds \
    --endpoint="dds://239.255.0.1:7401/topic_nav" \
    --timeout=3000
```

选项：

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `--instance=<id>` | 0 | 实例 ID |
| `--version=<M.m>` | 1.0 | 版本号 |
| `--binding=<type>` | coreipc | 绑定类型 |
| `--endpoint=<ep>` | 空 | 端点地址 |
| `--timeout=<ms>` | 5000 | IPC 超时 (仅标准流程) |

**写入路径策略：**

```
CRegistryProxy::Initialize()
    ├─ 成功 → Core IPC MPSC → Daemon (标准流程，daemon 分配 slot)
    └─ 失败 → 回退 UDS 直写 mmap (slot = svc_id & 1023)
```

##### `unregister <svc_id>`

注销服务。与 `register` 相同的智能降级策略。

```bash
lap-registry-ctl unregister 0x100A
lap-registry-ctl --type=asil unregister 0x2001
```

##### `inspect <slot>`

深度查看单个槽位，包括：
- 完整字段解析
- 实例 ID 位域解码（service\_id / instance\_no / domain / asil\_level / redundancy）
- 进程存活状态
- 256 字节原始 hex dump

```bash
lap-registry-ctl inspect 10
lap-registry-ctl inspect 1023   # 广播槽
```

> **注意：** inspect 仅对 ACTIVE 状态的槽位有效。

##### `watch [--interval=<ms>]`

持续监控所有槽位的状态变化，检测 IDLE ↔ ACTIVE ↔ UNREG 转换事件。

```bash
# 默认 500ms 刷新
lap-registry-ctl watch

# 100ms 高频监控
lap-registry-ctl watch --interval=100

# Ctrl+C 停止
```

输出示例：
```
[14:23:01.456] Slot 10  : IDLE     → ACTIVE   svc=0x100a pid=12345 dds://...
[14:23:05.789] Slot 10  : ACTIVE   → UNREG
[14:23:05.790] Slot 10  : UNREG    → IDLE
```

#### 架构

```
┌─────────────────────────────────┐
│        lap-registry-ctl         │
│  (Core IPC + LogAndTrace)       │
│  链接: lap_core, lap_log        │
└───────┬──────────┬──────────────┘
        │ 写入路径  │ 读取路径
        ▼          ▼
┌──────────────┐  ┌──────────────────┐
│CRegistryProxy│  │ CServiceRegistry │
│ (Core IPC)   │  │ (read-only mmap) │
│              │  │                  │
│ Publisher:   │  │ InitializeFrom   │
│  /lap_registry│  │  Socket() →     │
│  _req (MPSC) │  │  SCM_RIGHTS →   │
│ Subscriber:  │  │  mmap(PROT_READ │
│  /lap_registry│  │   | PROT_WRITE) │
│  _resp (SPMC)│  │                  │
└──────┬───────┘  └────────┬─────────┘
       │                   │
       │ IPC 不可用时       │
       │ 回退到直写  ──────┘
       ▼
┌─────────────────────────────────┐
│   Shared Memory (256 KB)        │
│   1024 × ServiceSlot (256 B)   │
│   memfd + MAP_SHARED            │
└─────────────────────────────────┘
```

**关键设计决策：**
- **Core IPC 优先**：register/unregister 优先走 MPSC 标准流程，确保原子更新
- **智能降级**：IPC Dispatcher 未启动时自动回退 UDS 直写 mmap
- **结构化日志**：所有操作通过 `LAP_COM_LOG_*` 记录，支持 DLT 追踪
- **读写分离**：list/status/inspect/watch 使用 read-only mmap（零 IPC 开销）

#### 依赖

| 组件 | 用途 |
|------|------|
| `lap_core` (共享库) | Core IPC (Publisher/Subscriber), Result, String, TypeDef |
| `lap_log` (共享库) | LogAndTrace (CLog, LAP\_LOG 宏) |
| `CServiceRegistry` (源码编译) | 共享内存读写 |
| `CRegistryProxy` (源码编译) | Core IPC 客户端封装 |

#### 安全考虑

- QM socket (`0666`): 任何用户可连接 → 读写 QM 注册表
- ASIL socket (`0660`): 仅 root/group → 读写 ASIL 注册表
- 生产环境建议限制 `register` / `unregister` 仅 root 可执行

## 文件清单

```
tools/
├── README.md               # 本文档
└── lap-registry-ctl.cpp    # 注册表控制工具源码 (v2.0 Core IPC 集成)
```
