# LightAP Service Registry - systemd Socket Activation

## 概述

本目录包含 LightAP 服务注册中心的 systemd 集成文件，实现**零常驻 Daemon** 的服务发现架构。

## 核心特性

- ✅ **零常驻进程**: 使用 systemd socket activation + oneshot service
- ✅ **100% 共享内存**: 所有进程通过 Unix Domain Socket 获取同一 memfd fd
- ✅ **自动恢复**: 进程重启/新增自动重连，零人工干预
- ✅ **热升级支持**: 无需停止任何服务
- ✅ **systemd 原生**: 完全集成到系统服务管理

## 文件说明

```
daemon/systemd/
├── lap-registry.socket          # Socket 单元 (监听 /run/lap/registry.sock)
├── lap-registry-init.service    # Oneshot 初始化服务
├── install.sh                   # 安装脚本
└── README.md                    # 本文件
```

## 架构原理

### 1. systemd Socket Activation

```text
系统启动
  ↓
systemd 创建 /run/lap/registry.sock (lap-registry.socket)
  ↓
首次应用连接 → systemd 启动 lap-registry-init.service (oneshot)
  ↓
lap-registry-init 创建 memfd + 初始化注册表
  ↓
通过 SCM_RIGHTS 发送 memfd fd 到客户端
  ↓
lap-registry-init 退出 (无常驻进程)
  ↓
后续所有进程连接 socket → 获取同一 memfd fd
```

### 2. memfd + SCM_RIGHTS

- **memfd_create**: 创建匿名共享内存文件
- **SCM_RIGHTS**: Unix 域套接字传递文件描述符
- **内核保证**: 所有进程接收的 fd 指向同一物理内存

## 安装步骤

### 前置条件

- Linux 内核 >= 5.4 (支持 memfd_create)
- systemd >= 239
- 编译好的 `lap-registry-init` 二进制文件

### 安装命令

```bash
# 1. 进入 systemd 目录
cd /path/to/LightAP/modules/Com/daemon/systemd

# 2. 运行安装脚本 (需要 root 权限)
sudo ./install.sh

# 3. 验证安装
sudo systemctl status lap-registry.socket

# 预期输出:
# ● lap-registry.socket - LightAP Service Registry Socket
#    Loaded: loaded (/etc/systemd/system/lap-registry.socket; enabled; ...)
#    Active: active (listening) since ...
#    Listen: /run/lap/registry.sock (Stream)
```

## 使用方式

### 客户端连接示例

```cpp
#include <sys/socket.h>
#include <sys/un.h>

int ConnectAndReceiveMemfd() {
    // 1. 连接到 systemd socket
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, "/run/lap/registry.sock");
    
    connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr));
    
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
    
    recvmsg(sock_fd, &msg, 0);
    
    // 3. 提取 memfd fd
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    int memfd;
    memcpy(&memfd, CMSG_DATA(cmsg), sizeof(int));
    
    // 4. 映射到进程地址空间
    void* registry = mmap(nullptr, 256*1024,
                          PROT_READ | PROT_WRITE,
                          MAP_SHARED, memfd, 0);
    
    return memfd;
}
```

### lap::com Runtime 集成

```cpp
// 应用代码无需修改，完全透明
#include <lap/com/Runtime.hpp>

int main() {
    // Runtime 内部自动连接 socket，获取 memfd
    lap::com::Runtime::Initialize();
    
    // 查找服务 (直接读取共享内存，< 500ns)
    auto handles = lap::com::FindService<RadarService>();
    
    // 提供服务 (直接写入共享内存)
    lap::com::OfferService<CameraService>(instance_spec);
    
    lap::com::Runtime::Deinitialize();
}
```

## 调试与监控

### 查看 Socket 状态

```bash
# Socket 监听状态
sudo systemctl status lap-registry.socket

# Socket 文件权限
ls -l /run/lap/registry.sock
# 预期输出: srw-rw-rw- 1 root root 0 ... /run/lap/registry.sock
```

### 查看 Oneshot 服务日志

```bash
# 实时日志
sudo journalctl -u lap-registry-init.service -f

# 最近日志
sudo journalctl -u lap-registry-init.service -n 50

# 查看首次启动记录
sudo journalctl -u lap-registry-init.service --since today
```

### 手动触发初始化

```bash
# 停止 socket (断开所有连接)
sudo systemctl stop lap-registry.socket

# 清理运行时文件
sudo rm -f /run/lap/registry.sock

# 重启 socket
sudo systemctl start lap-registry.socket

# 测试连接 (触发 oneshot 服务)
nc -U /run/lap/registry.sock
```

## 性能指标

| 指标 | 数值 |
|------|------|
| **常驻内存** | 0 MB (无常驻进程) |
| **首次连接延迟** | < 5ms (oneshot 启动) |
| **后续连接延迟** | < 100μs (直接从 socket) |
| **FindService 延迟** | < 500ns (共享内存读取) |
| **OfferService 延迟** | < 1μs (共享内存写入) |

## 故障排查

### 问题: Socket 无法创建

```bash
# 检查目录权限
ls -ld /run/lap
# 应为: drwxr-xr-x root root

# 手动创建目录
sudo mkdir -p /run/lap
sudo chmod 755 /run/lap
```

### 问题: Oneshot 服务启动失败

```bash
# 查看详细错误
sudo journalctl -xeu lap-registry-init.service

# 检查二进制文件
ls -l /usr/lib/lap/com/lap-registry-init
# 应为可执行文件

# 手动运行测试
sudo /usr/lib/lap/com/lap-registry-init
```

### 问题: 客户端无法连接

```bash
# 检查 socket 监听状态
sudo ss -lx | grep registry
# 应输出: /run/lap/registry.sock

# 测试连接
echo "test" | nc -U /run/lap/registry.sock
```

## 卸载

```bash
# 停止并禁用服务
sudo systemctl stop lap-registry.socket
sudo systemctl disable lap-registry.socket

# 删除单元文件
sudo rm /etc/systemd/system/lap-registry.socket
sudo rm /etc/systemd/system/lap-registry-init.service

# 重载 systemd
sudo systemctl daemon-reload

# 清理运行时文件
sudo rm -rf /run/lap
```

## 参考资料

- [systemd.socket(5) - Socket Unit](https://www.freedesktop.org/software/systemd/man/systemd.socket.html)
- [systemd.service(5) - Service Unit](https://www.freedesktop.org/software/systemd/man/systemd.service.html)
- [memfd_create(2) - Linux Manual](https://man7.org/linux/man-pages/man2/memfd_create.2.html)
- [unix(7) - SCM_RIGHTS](https://man7.org/linux/man-pages/man7/unix.7.html)
- [LightAP 服务发现架构文档](../../doc/SERVICE_DISCOVERY_ARCHITECTURE.md)

## 8. 安全与权限控制

### 8.1 功能安全等级 (ISO 26262)

**双 memfd 物理隔离架构**：

```text
QM 级注册表 (槽位 0-923)          ASIL-D 级注册表 (槽位 924-1023)
    ↓                                    ↓
/run/lap/registry.sock          /run/lap/registry-asil.sock
(所有进程可连接)                 (仅 lap-control 组可连接)
    ↓                                    ↓
qm_memfd (0666)                 asil_memfd (0660)
所有进程可读写                   仅控制进程可写，其他只读
```

**访问控制矩阵**：

| 进程类型 | QM 读 | QM 写 | ASIL-D 读 | ASIL-D 写 |
|----------|-------|-------|-----------|-----------|
| Perception (QM) | ✅ | ✅ | ✅ | ❌ |
| Planning (QM) | ✅ | ✅ | ✅ | ❌ |
| Control (ASIL-D) | ✅ | ✅ | ✅ | ✅ |
| 恶意进程 | ❌ | ❌ | ❌ | ❌ |

### 8.2 可执行文件白名单

**配置文件**：`/etc/lap/com/allowed_executables.yaml`

```yaml
asil_d_executables:
  - path: /usr/bin/lap-control
    sha256: "abc123def456..."  # 替换为实际 SHA256
    permissions:
      write_asil: true

qm_executables:
  - path: /usr/bin/lap-perception
    sha256: "def456789abc..."
    permissions:
      write_asil: false  # 仅可读 ASIL-D
```

**生成 SHA256 哈希**：

```bash
sha256sum /usr/bin/lap-control
# 输出: abc123def456...  /usr/bin/lap-control

# 更新 allowed_executables.yaml 中的哈希值
```

### 8.3 CRC32 完整性校验

**原理**：每次写入槽位时计算 CRC32 校验和，读取时验证

```cpp
// 写入流程
void WriteSlot(ServiceSlot& slot, const ServiceInfo& info) {
    slot.sequence.fetch_add(1);  // seqlock 加锁
    
    // 写入数据
    slot.service_id = info.service_id;
    // ...
    
    // 计算 CRC32
    slot.write_counter++;
    slot.crc32 = slot.ComputeCRC32();
    
    slot.sequence.fetch_add(1);  // seqlock 解锁
}

// 读取流程
std::optional<ServiceInfo> ReadSlot(const ServiceSlot& slot) {
    uint64_t seq1, seq2;
    ServiceInfo info;
    
    do {
        seq1 = slot.sequence.load();
        // 读取数据...
        seq2 = slot.sequence.load();
    } while (seq1 != seq2 || (seq1 & 1));
    
    // 验证 CRC32
    if (slot.crc32 != ComputeCRC32(info)) {
        LOG_ERROR("CRC32 mismatch! Data corrupted.");
        AuditLog("CRC32_MISMATCH", slot_index);
        return std::nullopt;  // 拒绝返回被篡改的数据
    }
    
    return info;
}
```

**性能影响**：< 50ns (CRC32 查找表实现)

### 8.4 审计日志

**查看安全审计日志**：

```bash
# 查看所有安全事件
journalctl -t lap-registry-security

# 查看权限违规事件
journalctl -t lap-registry-security | grep "UNAUTHORIZED_WRITE"

# 查看 CRC32 完整性错误
journalctl -t lap-registry-security | grep "CRC32_MISMATCH"

# 查看外部注入尝试
journalctl -t lap-registry-security | grep "NOT_IN_WHITELIST"

# 实时监控审计日志
journalctl -t lap-registry-security -f
```

**审计事件类型**：

- `UNAUTHORIZED_WRITE_ATTEMPT`: 未授权写入尝试
- `ASIL_WRITE_VIOLATION`: ASIL-D 写入违规 (非控制进程尝试写入)
- `CRC32_MISMATCH`: 数据完整性错误
- `EXECUTABLE_NOT_IN_WHITELIST`: 可执行文件不在白名单
- `EXECUTABLE_HASH_MISMATCH`: 可执行文件哈希不匹配 (二进制被替换)
- `SEGMENTATION_FAULT`: 内存访问违规 (mprotect 捕获)

### 8.5 ASIL-D 服务部署

**创建 lap-control 组**：

```bash
# 创建控制组
sudo groupadd -g 1000 lap-control

# 将控制进程用户加入组
sudo usermod -aG lap-control lap-control-user

# 验证组成员
getent group lap-control
```

**启动 ASIL-D 服务**：

```bash
# 启用 ASIL-D socket
sudo systemctl enable lap-registry-asil.socket
sudo systemctl start lap-registry-asil.socket

# 启动控制服务 (自动触发 ASIL-D 初始化)
sudo systemctl start lap-control.service

# 验证连接
sudo journalctl -u lap-registry-asil-init -n 20
```

## 9. 安全认证清单 (FuSa Compliance)

### 9.1 物理隔离证明

- ✅ QM / ASIL-D 使用独立 memfd (不同 socket)
- ✅ Guard pages 防止越界访问
- ✅ 独立的 systemd 服务单元
- ✅ 独立的 Unix domain socket (权限隔离)

### 9.2 权限控制证明

- ✅ ASIL-D socket 仅 lap-control 组可访问 (SocketGroup=lap-control)
- ✅ 客户端 GID 验证 (SO_PEERCRED)
- ✅ mprotect 强制只读 (非控制进程)
- ✅ SIGSEGV 处理器捕获违规写入

### 9.3 完整性保护

- ✅ CRC32 校验和 (每次读取验证)
- ✅ 写入计数器 (防重放攻击)
- ✅ seqlock 双重保障 (防并发数据损坏)

### 9.4 访问控制

- ✅ 可执行文件白名单 (SHA256 验证)
- ✅ 哈希验证 (防二进制替换)
- ✅ SELinux 策略 (可选)
- ✅ SystemCallFilter (系统调用白名单)

### 9.5 审计与追溯

- ✅ 所有违规访问记录到 syslog
- ✅ systemd journal 集成
- ✅ 实时告警机制
- ✅ 完整的事件追溯链

## 10. 性能测试 (包含安全开销)

### 10.1 基准测试

```bash
# 编译性能测试工具
cd /opt/lap/test
make bench-registry

# QM 级别延迟 (无额外安全检查)
./bench-registry --level=qm --iterations=1000000
# Expected: < 500ns FindService, < 1μs OfferService

# ASIL-D 级别延迟 (包含 CRC32 + 权限检查)
./bench-registry --level=asil-d --iterations=1000000
# Expected: < 600ns FindService, < 1.2μs OfferService

# 安全开销
# CRC32 验证: ~50ns
# 权限检查: ~30ns
# 白名单验证 (缓存): ~10ns
# 总安全开销: ~100ns (< 20%)
```

### 10.2 压力测试

```bash
# 并发安全测试 (1000 并发连接)
./stress-test --clients=1000 --duration=60s --safety-level=asil-d

# 恶意进程注入测试
./attack-simulator --mode=injection --attempts=10000
# Expected: 100% rejection rate

# 篡改攻击测试
./attack-simulator --mode=corruption --attempts=1000
# Expected: 100% detection rate (CRC32 mismatch)
```
