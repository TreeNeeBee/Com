# LightAP Registry + FastDDS Discovery Server 部署指南

> 容器/开发环境部署说明 (无 systemd PID 1)  
> 参考: SERVICE_DISCOVERY_ARCHITECTURE §5.2.3, §5.4, §2.0.6

---

## 1. 部署架构

```
┌─────────────────────────────────────────────────────────────┐
│  Container / Dev Host                                       │
│                                                             │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  FastDDS Discovery Server                             │  │
│  │  PID: fast-discovery-server                           │  │
│  │  Endpoint: 127.0.0.1:11811 (UDP)                      │  │
│  │  Protocol: SERVER (centralized discovery)             │  │
│  └──────────────────────┬────────────────────────────────┘  │
│                         │ CLIENT protocol                    │
│  ┌──────────────────────┴────────────────────────────────┐  │
│  │  Discovery Health Monitor (lap-discovery-monitor.sh)  │  │
│  │  CLIENT ↔ SIMPLE (PDP/EDP) 自动切换                    │  │
│  └───────────────────────────────────────────────────────┘  │
│                                                             │
│  ┌─────────────────────┐  ┌─────────────────────────────┐  │
│  │  QM Registry         │  │  ASIL Registry              │  │
│  │  lap-registry-init   │  │  lap-registry-init          │  │
│  │  Socket: 0666        │  │  Socket: 0660               │  │
│  │  /run/lap/            │  │  /run/lap/                  │  │
│  │  registry_qm.sock    │  │  registry_asil.sock         │  │
│  └──────────┬──────────┘  └──────────┬──────────────────┘  │
│             │                         │                      │
│             └─────────┬───────────────┘                      │
│                       ↓                                      │
│            256KB 共享内存 (memfd)                              │
│            1024 × 256B ServiceSlot                           │
│            客户端 mmap(PROT_READ)                             │
└─────────────────────────────────────────────────────────────┘
```

### 运行进程清单

| 进程 | 二进制 | 功能 |
|------|--------|------|
| FastDDS Discovery Server | `fast-discovery-server` | 集中式 DDS 服务发现 (跨 ECU) |
| Health Monitor | `lap-discovery-monitor.sh` | DS 健康检查 + PDP/EDP 退化 |
| QM Registry | `lap-registry-init --type=qm` | QM 级服务注册表守护进程 |
| ASIL Registry | `lap-registry-init --type=asil` | ASIL 级服务注册表守护进程 |

---

## 2. 前置条件

```bash
# 检查 registry 二进制
ls -la /workspace/LightAP/modules/Com/build/registry/lap-registry-init

# 检查 fastdds 可用性
which fastdds
fastdds discovery --help | head -5

# 如果二进制不存在, 先编译
cd /workspace/LightAP/modules/Com/build
cmake .. && make -j$(nproc)
```

---

## 3. 一键部署

### 3.1 安装

```bash
cd /workspace/LightAP/modules/Com/registry/systemd

# 方式 1: 使用 install.sh (推荐)
sudo ./install.sh

# 方式 2: 手动安装
# 安装二进制
cp /workspace/LightAP/modules/Com/build/registry/lap-registry-init /usr/local/bin/
chmod 755 /usr/local/bin/lap-registry-init

# 安装 Discovery Server 配置
mkdir -p /etc/lap/discovery
cp fastdds_ds_server.xml fastdds_ds_client.xml fastdds_ds_fallback.xml \
   fastdds_ds_env.conf /etc/lap/discovery/
cp lap-discovery-monitor.sh /etc/lap/discovery/
chmod 755 /etc/lap/discovery/lap-discovery-monitor.sh

# 初始化运行目录
mkdir -p /run/lap
ln -sf /etc/lap/discovery/fastdds_ds_client.xml /run/lap/fastdds_active_profile.xml
echo "CLIENT" > /run/lap/discovery_state
```

### 3.2 启动全部服务

```bash
# 按依赖顺序启动:
# 1) Discovery Server  →  2) Health Monitor  →  3) QM Registry  →  4) ASIL Registry

# Step 1: FastDDS Discovery Server
fastdds discovery -i 0 -l 127.0.0.1 -p 11811 &
sleep 2

# Step 2: Health Monitor (后台守护)
export DS_LISTEN_ADDR=127.0.0.1
export DS_PORT=11811
export DS_CONFIG_DIR=/etc/lap/discovery
/etc/lap/discovery/lap-discovery-monitor.sh --daemon

# Step 3: QM Registry
lap-registry-init --type=qm --socket=/run/lap/registry_qm.sock &

# Step 4: ASIL Registry
lap-registry-init --type=asil --socket=/run/lap/registry_asil.sock &

sleep 1
echo "部署完成, 检查进程:"
ps aux | grep -E "fast-discovery|lap-registry|lap-discovery-monitor" | grep -v grep
```

### 3.3 验证部署

```bash
# 检查所有进程
ps aux | grep -E "fast-discovery|lap-registry|lap-discovery-monitor" | grep -v grep

# 检查运行时文件
ls -la /run/lap/

# 预期输出:
#   discovery_monitor.pid
#   discovery_state          → 内容 "CLIENT"
#   fastdds_active_profile.xml → /etc/lap/discovery/fastdds_ds_client.xml
#   registry_asil.sock       (S= 类型)
#   registry_qm.sock         (S= 类型)

# 检查 Discovery Server 状态
/etc/lap/discovery/lap-discovery-monitor.sh --status

# 验证 QM 客户端连接
python3 -c "
import socket, struct, array, os
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.settimeout(5.0)
sock.connect('/run/lap/registry_qm.sock')
msg, ancdata, _, _ = sock.recvmsg(1, socket.CMSG_SPACE(struct.calcsize('i')))
for level, typ, data in ancdata:
    if level == socket.SOL_SOCKET and typ == socket.SCM_RIGHTS:
        fds = array.array('i'); fds.frombytes(data)
        size = os.fstat(fds[0]).st_size
        print(f'✓ QM: FD={fds[0]}, size={size} ({size//1024}KB)')
        os.close(fds[0])
sock.close()
"
```

---

## 4. 发现模式与退化机制

### 4.1 双模式设计

| 模式 | DDS 协议 | 配置文件 | 延迟 | 触发条件 |
|------|----------|----------|------|----------|
| **CLIENT** | `DiscoveryProtocol::CLIENT` | `fastdds_ds_client.xml` | < 5ms | DS 可用 (默认) |
| **SIMPLE** | `DiscoveryProtocol::SIMPLE` | `fastdds_ds_fallback.xml` | 10-50ms | DS 不可用 × 3 次 |

### 4.2 退化与恢复流程

```
                  ┌──────────────┐
                  │  Health Check │ ← 每 5 秒执行
                  │  (monitor.sh) │
                  └───┬──────┬───┘
              DS 可用 │      │ DS 不可用
                  ┌───┘      └───┐
                  ↓              ↓
          ┌──────────────┐ ┌──────────────┐
          │ CLIENT 模式   │ │ 失败计数 +1  │
          │ (集中式发现)   │ │              │
          └──────────────┘ └──────┬───────┘
                                  │ ≥ 3 次
                                  ↓
                          ┌──────────────┐
                          │ SIMPLE 模式   │
                          │ (PDP/EDP 多播) │
                          └──────────────┘
                                  │
                          DS 恢复 │
                                  ↓
                          ┌──────────────┐
                          │ CLIENT 模式   │ ← 自动恢复
                          └──────────────┘
```

### 4.3 手动控制

```bash
# 查看当前模式
cat /run/lap/discovery_state

# 强制切换到 PDP/EDP 退化模式
export DS_LISTEN_ADDR=127.0.0.1 DS_PORT=11811 DS_CONFIG_DIR=/etc/lap/discovery
/etc/lap/discovery/lap-discovery-monitor.sh --force-fallback

# 强制恢复到 CLIENT 模式
/etc/lap/discovery/lap-discovery-monitor.sh --force-client

# 查看活跃配置
readlink /run/lap/fastdds_active_profile.xml
```

### 4.4 退化验证

```bash
# 1. 确认当前 CLIENT 模式
cat /run/lap/discovery_state   # → CLIENT

# 2. 杀掉 Discovery Server
pkill -f "fast-discovery-server"
sleep 2

# 3. 触发退化
/etc/lap/discovery/lap-discovery-monitor.sh --force-fallback
cat /run/lap/discovery_state   # → SIMPLE

# 4. 重启 Discovery Server
fastdds discovery -i 0 -l 127.0.0.1 -p 11811 &
sleep 2

# 5. 恢复 CLIENT 模式
/etc/lap/discovery/lap-discovery-monitor.sh --force-client
cat /run/lap/discovery_state   # → CLIENT
```

---

## 5. 配置参考

### 5.1 环境变量 (`/etc/lap/discovery/fastdds_ds_env.conf`)

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `DS_LISTEN_ADDR` | `0.0.0.0` | Discovery Server 监听地址 |
| `DS_PORT` | `11811` | Discovery Server UDP 端口 |
| `DS_SERVER_ID` | `0` | Server ID (HA 时需唯一, 范围 0-255) |
| `DS_HEALTH_INTERVAL` | `5` | 健康检查周期 (秒) |
| `DS_MAX_FAILURES` | `3` | 最大连续失败 → 触发退化 |
| `DS_FALLBACK_ENABLED` | `true` | 是否启用自动 PDP/EDP 退化 |
| `DS_CONFIG_DIR` | `/etc/lap/discovery` | 配置文件目录 |

### 5.2 XML 配置文件

| 文件 | DDS 协议 | 用途 |
|------|----------|------|
| `fastdds_ds_server.xml` | `SERVER` | Discovery Server 端配置 (监听端口/传输层) |
| `fastdds_ds_client.xml` | `CLIENT` | SD Proxy / DDS 参与者连接 DS 的配置 |
| `fastdds_ds_fallback.xml` | `SIMPLE` | PDP/EDP 多播退化配置 |

### 5.3 关键路径

| 路径 | 说明 |
|------|------|
| `/usr/local/bin/lap-registry-init` | Registry 守护进程二进制 |
| `/etc/lap/discovery/` | Discovery Server 配置目录 |
| `/run/lap/registry_qm.sock` | QM 注册表 Unix Domain Socket |
| `/run/lap/registry_asil.sock` | ASIL 注册表 Unix Domain Socket |
| `/run/lap/discovery_state` | 当前发现模式 (`CLIENT` / `SIMPLE`) |
| `/run/lap/fastdds_active_profile.xml` | 活跃 DDS 配置符号链接 |
| `/run/lap/discovery_monitor.pid` | 健康监控进程 PID 文件 |

---

## 6. 停止与卸载

### 停止所有服务

```bash
# 按反向依赖顺序停止
pkill -f "lap-registry-init"
pkill -f "lap-discovery-monitor"
pkill -f "fast-discovery-server"

# 清理运行时
rm -f /run/lap/registry_qm.sock /run/lap/registry_asil.sock
rm -f /run/lap/discovery_state /run/lap/fastdds_active_profile.xml
rm -f /run/lap/discovery_monitor.pid
```

### 完整卸载

```bash
cd /workspace/LightAP/modules/Com/registry/systemd
sudo ./install.sh --uninstall
```

---

## 7. 功能测试

```bash
cd /workspace/LightAP/modules/Com/registry/systemd
sudo ./test_registry.sh
```

### 测试用例 (26 项)

| # | 测试 | 验证内容 |
|---|------|----------|
| 1 | QM Start + Connect | QM socket 创建, 客户端 memfd 接收 (256KB) |
| 2 | ASIL Start + Connect | ASIL socket 创建, 客户端 memfd 接收 |
| 3 | 10 Concurrent Clients | 10 个并发连接同时获取 memfd |
| 4 | Shared Memory R/W | mmap 读写 + 回读验证 |
| 5 | Graceful Shutdown | SIGTERM 优雅退出 (exit code 0/143) |
| 6 | DS Start | Discovery Server 启动 + 端口监听 |
| 7 | CLIENT Mode | Monitor 设置 CLIENT 模式 + profile 符号链接 |
| 8 | PDP/EDP Fallback | DS 停止 → SIMPLE 模式 + fallback XML |
| 9 | DS Recovery | DS 重启 → CLIENT 模式恢复 |
| 10 | XML Validation | 3 个 XML 配置文件格式校验 + env 文件检查 |

---

## 8. 故障排查

### Discovery Server 启动失败

```bash
# 检查端口占用
ss -uln | grep 11811
# 如果端口被占用, 杀掉旧进程
pkill -f "fast-discovery-server"
sleep 2
fastdds discovery -i 0 -l 127.0.0.1 -p 11811 &
```

### Registry 连接被拒

```bash
# 检查 socket 文件
ls -la /run/lap/*.sock
# 检查 daemon 进程
pgrep -la "lap-registry-init"
# 重启 daemon
pkill -f "lap-registry-init"
lap-registry-init --type=qm --socket=/run/lap/registry_qm.sock &
```

### 退化模式不切换

```bash
# 手动检查 DS 可达性
pgrep -la "fast-discovery-server"
# 重置状态
echo "CLIENT" > /run/lap/discovery_state
ln -sf /etc/lap/discovery/fastdds_ds_client.xml /run/lap/fastdds_active_profile.xml
```

---

## 9. 参考文档

| 文档 | 章节 |
|------|------|
| [SERVICE_DISCOVERY_ARCHITECTURE](../architecture/SERVICE_DISCOVERY_ARCHITECTURE.md) | §5.2.3 FastDDS Discovery Server 集成 |
| [SERVICE_DISCOVERY_ARCHITECTURE](../architecture/SERVICE_DISCOVERY_ARCHITECTURE.md) | §5.4 配置与部署 |
| [SERVICE_DISCOVERY_ARCHITECTURE](../architecture/SERVICE_DISCOVERY_ARCHITECTURE.md) | §2.0.6 故障恢复机制 |
| [REGISTRY_V2_IMPLEMENTATION_SUMMARY](REGISTRY_V2_IMPLEMENTATION_SUMMARY.md) | Registry v2 实现总结 |
| [systemd/README](../systemd/README.md) | systemd 集成说明 |
