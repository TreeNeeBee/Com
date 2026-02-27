# LightAP Registry - systemd Integration

## 文件说明

| 文件 | 说明 |
|------|------|
| **Registry 注册表** | |
| `lap-registry-qm.socket` | QM 注册表 Socket 单元 (权限 0666，所有进程可连接) |
| `lap-registry-qm-init.service` | QM 注册表初始化服务 (由 socket 激活) |
| `lap-registry-asil.socket` | ASIL 注册表 Socket 单元 (权限 0660，受限访问) |
| `lap-registry-asil-init.service` | ASIL 注册表初始化服务 (安全加固) |
| **FastDDS Discovery Server** | |
| `lap-discovery-server.service` | Discovery Server systemd 服务单元 |
| `lap-discovery-monitor.service` | 健康监控 + PDP/EDP 退化 systemd 服务单元 |
| `fastdds_ds_server.xml` | Discovery Server 端 DDS 配置 (SERVER 协议) |
| `fastdds_ds_client.xml` | SD Proxy 端 DDS 配置 (CLIENT 协议) |
| `fastdds_ds_fallback.xml` | PDP/EDP 退化配置 (SIMPLE 协议, 多播发现) |
| `fastdds_ds_env.conf` | Discovery Server 环境变量配置 |
| `lap-discovery-monitor.sh` | 健康检查 + 自动 CLIENT↔SIMPLE 切换脚本 |
| **工具** | |
| `install.sh` | 安装/卸载/状态查看脚本 |
| `test_registry.sh` | 功能验证测试 (支持容器环境) |

## 架构

```
                    ┌────────────────────────────┐
                    │  FastDDS Discovery Server   │
                    │  (centralized, 0.0.0.0:11811)│
                    └──────────┬─────────────────┘
                               │ CLIENT protocol
                    ┌──────────┴─────────────────┐
                    │  SD Proxy (Slot 1 / 512)    │
                    │  Cross-ECU Service Discovery │
                    └──────────┬─────────────────┘
                               │
客户端连接 → UDS Socket → CRegistryServer → memfd (SCM_RIGHTS) → 客户端 mmap
                                   ↓
                        256KB 共享内存 (1024 × 256B slots)
```

### 发现模式

| 模式 | 协议 | 延迟 | 带宽 | 场景 |
|------|------|------|------|------|
| **CLIENT** | Discovery Server | < 5ms | 低 | 正常运行 (推荐) |
| **SIMPLE** | PDP/EDP 多播 | 10-50ms | 中 | Discovery Server 不可用时退化 |

### 退化链路 (§2.0.6)

```
Discovery Server 可用  →  CLIENT 模式 (集中式, 低延迟)
         │
    健康检查失败 × 3
         │
         ▼
Discovery Server 不可用  →  SIMPLE 模式 (PDP/EDP 多播, 去中心化)
         │
    Discovery Server 恢复
         │
         ▼
自动恢复  →  CLIENT 模式
```

## 安装 (systemd 环境)

```bash
sudo ./install.sh            # 安装 (Registry + Discovery Server)
sudo ./install.sh --status   # 查看状态
sudo ./install.sh --uninstall # 卸载所有
```

## 手动运行 (容器/开发环境)

```bash
# 创建运行目录
mkdir -p /run/lap

# 1. 启动 FastDDS Discovery Server
fastdds discovery -i 0 -l 127.0.0.1 -p 11811 &

# 2. 启动健康监控 (后台)
./lap-discovery-monitor.sh --daemon

# 3. 启动 QM 注册表
lap-registry-init --type=qm --socket=/run/lap/registry_qm.sock &

# 4. 启动 ASIL 注册表
lap-registry-init --type=asil --socket=/run/lap/registry_asil.sock &
```

## Discovery Server 管理

```bash
# 查看发现模式状态
./lap-discovery-monitor.sh --status

# 手动切换到 PDP/EDP 退化模式
./lap-discovery-monitor.sh --force-fallback

# 手动恢复到 CLIENT 模式
./lap-discovery-monitor.sh --force-client

# 查看当前活跃配置
cat /run/lap/discovery_state
readlink /run/lap/fastdds_active_profile.xml
```

## 配置说明

编辑 `fastdds_ds_env.conf` 调整 Discovery Server 参数:

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `DS_LISTEN_ADDR` | `0.0.0.0` | 监听地址 |
| `DS_PORT` | `11811` | UDP 端口 |
| `DS_SERVER_ID` | `0` | Server ID (HA 部署时需唯一) |
| `DS_HEALTH_INTERVAL` | `5` | 健康检查间隔 (秒) |
| `DS_MAX_FAILURES` | `3` | 最大连续失败次数 → 触发退化 |
| `DS_FALLBACK_ENABLED` | `true` | 是否启用自动 PDP/EDP 退化 |

## 功能测试

```bash
sudo ./test_registry.sh
```

测试内容:
- **Test 1-5**: QM/ASIL 守护进程启动、客户端 FD 接收、10 并发连接、共享内存读写、优雅关闭
- **Test 6**: FastDDS Discovery Server 启动 + 端口监听
- **Test 7**: Discovery Monitor CLIENT 模式设置
- **Test 8**: Discovery Server 停止 → PDP/EDP 退化
- **Test 9**: Discovery Server 恢复 → CLIENT 模式恢复
- **Test 10**: XML 配置文件格式验证

## 参考

- [SERVICE_DISCOVERY_ARCHITECTURE](../../doc/architecture/SERVICE_DISCOVERY_ARCHITECTURE.md) §5.2.3 (FastDDS Discovery Server)
- [SERVICE_DISCOVERY_ARCHITECTURE](../../doc/architecture/SERVICE_DISCOVERY_ARCHITECTURE.md) §2.0.6 (故障恢复)
- [SERVICE_DISCOVERY_ARCHITECTURE](../../doc/architecture/SERVICE_DISCOVERY_ARCHITECTURE.md) §5.4 (配置与部署)
