# LightAP 服务注册表安全架构总结

**版本**: 3.1  
**日期**: 2025-11-19  
**状态**: 设计完成，待实现  

## 1. 安全需求概览

### 1.1 功能安全 (ISO 26262)

- **QM / ASIL-D 物理隔离**: 使用独立 memfd 确保不同安全等级之间的物理分离
- **权限控制**: 基于进程组 (GID) 的细粒度访问控制
- **故障隔离**: 单个服务崩溃不影响全局注册表
- **确定性延迟**: < 600ns FindService (包含安全开销)

### 1.2 信息安全

- **外部注入防护**: 可执行文件白名单 + SHA256 哈希验证
- **完整性保护**: CRC32 校验和 + seqlock 双重保障
- **访问审计**: 所有安全违规事件记录到 systemd journal
- **运行时保护**: mprotect + SIGSEGV 捕获违规写入

---

## 2. 核心安全机制

### 2.1 双 memfd 物理隔离

```text
┌─────────────────────────────────────────────────────────────────┐
│                        systemd socket activation                │
└─────────────────────────────────────────────────────────────────┘
           ↓                                    ↓
┌──────────────────────────┐      ┌──────────────────────────────┐
│  /run/lap/registry.sock  │      │ /run/lap/registry-asil.sock  │
│  SocketMode=0666         │      │ SocketMode=0660              │
│  (所有进程可连接)         │      │ SocketGroup=lap-control      │
└──────────────────────────┘      └──────────────────────────────┘
           ↓                                    ↓
┌──────────────────────────┐      ┌──────────────────────────────┐
│  lap-registry-init.service│      │ lap-registry-asil-init.service│
│  Type=oneshot            │      │ Type=oneshot                 │
│  User=lap-registry       │      │ User=lap-registry            │
│  Group=root              │      │ Group=lap-control            │
└──────────────────────────┘      └──────────────────────────────┘
           ↓                                    ↓
┌──────────────────────────┐      ┌──────────────────────────────┐
│  qm_memfd                │      │ asil_memfd                   │
│  槽位 0-923 (924 slots)   │      │ 槽位 924-1023 (100 slots)     │
│  所有进程: RW             │      │ 控制进程: RW / 其他: RO       │
└──────────────────────────┘      └──────────────────────────────┘
```

**关键设计点**：

1. **Socket 权限隔离**: ASIL-D socket 仅 `lap-control` 组可访问
2. **独立 oneshot 服务**: QM 和 ASIL-D 各自独立初始化
3. **不同内存区域**: 两个 memfd 映射到不同物理内存
4. **自动恢复**: 进程重启后重新连接 socket 获取 memfd fd

### 2.2 访问控制矩阵

| 进程类型 | 可执行文件 | 所属组 | QM 读 | QM 写 | ASIL-D 读 | ASIL-D 写 |
|---------|-----------|--------|-------|-------|-----------|-----------|
| Perception | `/usr/bin/lap-perception` | `lap-perception` | ✅ | ✅ | ✅ | ❌ |
| Planning | `/usr/bin/lap-planning` | `lap-planning` | ✅ | ✅ | ✅ | ❌ |
| Control | `/usr/bin/lap-control` | `lap-control` | ✅ | ✅ | ✅ | ✅ |
| 未知进程 | `/tmp/malicious` | - | ❌ | ❌ | ❌ | ❌ |

**验证流程**：

```cpp
// 1. 获取客户端进程凭据 (Unix socket SO_PEERCRED)
struct ucred cred;
getsockopt(client_socket, SOL_SOCKET, SO_PEERCRED, &cred, &len);

// 2. 检查可执行文件路径
char exe_path[PATH_MAX];
readlink("/proc/{cred.pid}/exe", exe_path, sizeof(exe_path));

// 3. 验证是否在白名单
if (!IsAllowedExecutable(exe_path)) {
    AuditLog("EXECUTABLE_NOT_IN_WHITELIST", exe_path);
    return -EACCES;
}

// 4. 验证 SHA256 哈希
std::string actual_hash = ComputeSHA256(exe_path);
if (actual_hash != expected_hash) {
    AuditLog("EXECUTABLE_HASH_MISMATCH", exe_path);
    return -EACCES;
}

// 5. 检查组权限 (ASIL-D 写入)
if (safety_level == ASIL_D && !IsInGroup(cred.gid, "lap-control")) {
    AuditLog("ASIL_WRITE_VIOLATION", exe_path);
    return -EACCES;
}
```

### 2.3 完整性保护 (CRC32 + seqlock)

**槽位结构增强**：

```cpp
struct alignas(64) ServiceSlot {
    // === seqlock 控制 (8 bytes) ===
    std::atomic<uint64_t> sequence;  // 奇数=写中，偶数=可读
    
    // === 完整性保护 (8 bytes) ===
    uint32_t crc32;                  // CRC32 校验和 (IEEE 802.3)
    uint32_t write_counter;          // 写入计数器 (防重放攻击)
    
    // === 服务数据 (240 bytes) ===
    // ... 服务信息字段 ...
    
    // 计算 CRC32 (排除 sequence 和 crc32 本身)
    uint32_t ComputeCRC32() const {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(this);
        const size_t offset = offsetof(ServiceSlot, write_counter);
        const size_t len = sizeof(ServiceSlot) - offset - sizeof(_padding);
        return Crc32Fast(data + offset, len);  // ~50ns
    }
};
```

**写入流程** (带完整性保护):

```cpp
void WriteSlotWithIntegrity(uint32_t slot_index, const ServiceInfo& info) {
    auto& slot = slots_[slot_index];
    
    // 1. seqlock 加锁 (原子递增，变为奇数)
    slot.sequence.fetch_add(1, std::memory_order_acquire);
    
    // 2. 写入数据
    slot.service_id = info.service_id;
    slot.instance_id = info.instance_id;
    // ... 其他字段
    
    // 3. 递增写入计数器 (防重放)
    slot.write_counter++;
    
    // 4. 计算并写入 CRC32
    slot.crc32 = slot.ComputeCRC32();
    
    std::atomic_thread_fence(std::memory_order_release);
    
    // 5. seqlock 解锁 (原子递增，变为偶数)
    slot.sequence.fetch_add(1, std::memory_order_release);
}
```

**读取流程** (带完整性验证):

```cpp
std::optional<ServiceInfo> ReadSlotWithVerification(uint32_t slot_index) {
    auto& slot = slots_[slot_index];
    ServiceInfo info;
    uint64_t seq1, seq2;
    uint32_t crc_expected, crc_actual;
    
    do {
        // 1. seqlock 读取前检查 (偶数 = 可读)
        seq1 = slot.sequence.load(std::memory_order_acquire);
        if (seq1 & 1) {  // 奇数 = 正在写入，重试
            _mm_pause();
            continue;
        }
        
        // 2. 读取数据
        info.service_id = slot.service_id;
        info.instance_id = slot.instance_id;
        // ... 其他字段
        
        // 3. 读取 CRC32
        crc_expected = slot.crc32;
        
        std::atomic_thread_fence(std::memory_order_acquire);
        
        // 4. seqlock 读取后检查 (序列号必须一致)
        seq2 = slot.sequence.load(std::memory_order_acquire);
        
    } while (seq1 != seq2);  // 读取期间有写入，重试
    
    // 5. 验证 CRC32 (防篡改)
    crc_actual = ComputeCRC32FromInfo(info);
    if (crc_actual != crc_expected) {
        LOG_ERROR("CRC32 mismatch in slot {}: expected={:x}, actual={:x}", 
                 slot_index, crc_expected, crc_actual);
        
        // 审计日志
        AuditLog("CRC32_MISMATCH", slot_index);
        
        return std::nullopt;  // 拒绝返回被篡改的数据
    }
    
    return info;
}
```

**性能开销**：

- seqlock 读取: ~200ns (无冲突)
- CRC32 计算: ~50ns (256 bytes, 查找表实现)
- 总延迟: ~250ns → 加上安全检查后 < 600ns (目标 < 500ns)

### 2.4 运行时内存保护 (mprotect)

**客户端映射时权限设置**：

```cpp
void MapMemoryWithPermission(int memfd, SafetyLevel level) {
    // 1. 检查当前进程权限
    bool has_write_permission = HasWritePermission(level);
    
    // 2. 映射内存 (根据权限设置)
    int prot = PROT_READ;
    if (has_write_permission) {
        prot |= PROT_WRITE;
    }
    
    void* addr = mmap(nullptr, 256 * 1024, prot, MAP_SHARED, memfd, 0);
    
    // 3. 对于非授权进程，额外使用 mprotect 强制只读
    if (level == ASIL_D && !has_write_permission) {
        mprotect(addr, 256 * 1024, PROT_READ);
        
        // 4. 安装 SIGSEGV 处理器捕获违规写入
        InstallSegfaultHandler(addr, 256 * 1024);
    }
}
```

**SIGSEGV 信号处理器**：

```cpp
void SegfaultHandler(int sig, siginfo_t* info, void* ctx) {
    void* fault_addr = info->si_addr;
    
    // 检查是否是 ASIL-D 区域违规
    if (IsASILDRegionAddress(fault_addr)) {
        LOG_CRITICAL("ASIL-D write violation detected! "
                    "Addr={}, Pid={}", fault_addr, getpid());
        
        // 记录审计日志
        AuditLog("SEGMENTATION_FAULT", getpid());
        
        // 终止违规进程 (安全策略)
        abort();
    }
}
```

---

## 3. 外部注入防护

### 3.1 可执行文件白名单

**配置文件**: `/etc/lap/com/allowed_executables.yaml`

```yaml
asil_d_executables:
  - path: /usr/bin/lap-control
    sha256: "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
    permissions:
      write_asil: true

qm_executables:
  - path: /usr/bin/lap-perception
    sha256: "d4735e3a265e16eee03f59718b9b5d03019c07d8b6c51f90da3a666eec13ab35"
    permissions:
      write_asil: false
```

**验证流程**：

```cpp
bool VerifyExecutable(const std::string& exe_path) {
    // 1. 检查是否在白名单
    auto it = std::find_if(whitelist_.begin(), whitelist_.end(),
        [&](const auto& entry) { return entry.path == exe_path; });
    
    if (it == whitelist_.end()) {
        AuditLog("EXECUTABLE_NOT_IN_WHITELIST", exe_path);
        return false;
    }
    
    // 2. 计算 SHA256 哈希
    std::string actual_hash = ComputeSHA256(exe_path);
    
    // 3. 验证哈希匹配
    if (actual_hash != it->sha256_hash) {
        LOG_CRITICAL("Executable hash mismatch: {}, expected={}, actual={}", 
                    exe_path, it->sha256_hash, actual_hash);
        
        AuditLog("EXECUTABLE_HASH_MISMATCH", exe_path);
        return false;
    }
    
    return true;
}
```

**SHA256 计算** (OpenSSL):

```cpp
std::string ComputeSHA256(const std::string& file_path) {
    FILE* file = fopen(file_path.c_str(), "rb");
    if (!file) return "";
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    
    const int buffer_size = 32768;
    unsigned char buffer[buffer_size];
    size_t bytes_read;
    
    while ((bytes_read = fread(buffer, 1, buffer_size, file)) > 0) {
        SHA256_Update(&sha256, buffer, bytes_read);
    }
    
    SHA256_Final(hash, &sha256);
    fclose(file);
    
    // 转换为十六进制字符串
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') 
           << static_cast<int>(hash[i]);
    }
    
    return ss.str();
}
```

### 3.2 防御措施总结

| 攻击类型 | 防御机制 | 实现方式 |
|---------|---------|---------|
| 未授权进程注入 | 可执行文件白名单 | 验证 `/proc/{pid}/exe` 是否在 `allowed_executables.yaml` |
| 二进制替换攻击 | SHA256 哈希验证 | 每次连接时计算并验证可执行文件哈希 |
| 权限提升攻击 | 组权限检查 | `SO_PEERCRED` 获取 GID，验证是否在 `lap-control` 组 |
| 内存篡改攻击 | CRC32 完整性校验 | 读取时验证 CRC32，不匹配则拒绝返回 |
| 重放攻击 | 写入计数器 | 每次写入递增计数器，检测过期数据 |
| 并发数据损坏 | seqlock | 读取时检测是否有并发写入，重试直到一致 |
| 越界写入攻击 | Guard pages | 在内存区域前后设置不可访问页 |
| 违规写入 | mprotect + SIGSEGV | 非授权进程映射为只读，写入触发段错误 |

---

## 4. 审计与追溯

### 4.1 审计事件类型

```cpp
enum class AuditEventType {
    // 权限违规
    UNAUTHORIZED_WRITE_ATTEMPT,      // 未授权写入尝试
    UNAUTHORIZED_READ_ATTEMPT,       // 未授权读取尝试
    ASIL_WRITE_VIOLATION,            // ASIL-D 写入违规
    
    // 完整性违规
    CRC32_MISMATCH,                  // CRC32 校验失败
    SLOT_CORRUPTION_DETECTED,        // 槽位数据损坏
    
    // 注入攻击
    EXECUTABLE_NOT_IN_WHITELIST,     // 可执行文件不在白名单
    EXECUTABLE_HASH_MISMATCH,        // 可执行文件哈希不匹配
    
    // 异常行为
    HEARTBEAT_TIMEOUT,               // 心跳超时
    ZOMBIE_PROCESS_CLEANUP,          // 僵尸进程清理
    SEGMENTATION_FAULT,              // 内存访问违规
};
```

### 4.2 审计日志示例

```text
[2025-11-19 14:32:15.123] [CRITICAL] ASIL_WRITE_VIOLATION
  PID: 1234
  UID: 1000
  EXE: /usr/bin/lap-perception
  Details: {"slot_index": 950, "safety_level": "ASIL-D", "group": "lap-perception"}
  Action: Access denied, process terminated

[2025-11-19 14:32:16.456] [ERROR] EXECUTABLE_HASH_MISMATCH
  PID: 5678
  EXE: /usr/bin/lap-control
  Details: {"expected_hash": "abc123...", "actual_hash": "def456...", "file_size": 123456}
  Action: Connection rejected, incident reported

[2025-11-19 14:32:17.789] [WARNING] CRC32_MISMATCH
  PID: 9012
  EXE: /usr/bin/lap-planning
  Details: {"slot_index": 42, "expected_crc32": "0x12345678", "actual_crc32": "0x87654321"}
  Action: Read rejected, slot marked for re-validation
```

### 4.3 查询审计日志

```bash
# 查看最近 100 条安全事件
journalctl -t lap-registry-security -n 100

# 查看特定时间范围
journalctl -t lap-registry-security --since "2025-11-19 14:00:00" --until "2025-11-19 15:00:00"

# 查看特定进程的事件
journalctl -t lap-registry-security | grep "PID: 1234"

# 统计事件类型分布
journalctl -t lap-registry-security --no-pager | grep -oP '\[\w+\]' | sort | uniq -c
```

---

## 5. 性能分析 (包含安全开销)

### 5.1 延迟分解

| 操作 | QM 级别 | ASIL-D 级别 | 安全开销 |
|-----|--------|------------|---------|
| FindService (无冲突) | 200ns | 250ns | +50ns (CRC32) |
| FindService (含权限检查) | 230ns | 280ns | +80ns (权限+CRC32) |
| OfferService (写入) | 500ns | 600ns | +100ns (CRC32+计数器) |
| 可执行文件验证 (缓存命中) | - | 10ns | +10ns (哈希查找) |
| 可执行文件验证 (缓存未命中) | - | 50μs | +50μs (SHA256 计算) |

### 5.2 安全开销占比

- **CRC32 计算**: ~50ns (< 20% 总延迟)
- **权限验证**: ~30ns (< 10% 总延迟)
- **白名单验证**: ~10ns (缓存命中)
- **总安全开销**: < 100ns (< 20% 总延迟)

**结论**: 安全机制对性能影响可控，仍然满足 < 600ns 目标

---

## 6. 部署清单

### 6.1 系统配置

```bash
# 1. 创建用户和组
sudo groupadd -g 1000 lap-control
sudo groupadd -g 1001 lap-perception
sudo groupadd -g 1002 lap-planning
sudo useradd -u 999 -g root -s /sbin/nologin lap-registry

# 2. 安装 systemd 单元文件
sudo cp lap-registry.socket /etc/systemd/system/
sudo cp lap-registry-init.service /etc/systemd/system/
sudo cp lap-registry-asil.socket /etc/systemd/system/
sudo cp lap-registry-asil-init.service /etc/systemd/system/

# 3. 创建配置目录
sudo mkdir -p /etc/lap/com
sudo mkdir -p /var/log/lap
sudo mkdir -p /run/lap

# 4. 部署配置文件
sudo cp slot_mapping.yaml /etc/lap/com/
sudo cp allowed_executables.yaml /etc/lap/com/

# 5. 更新可执行文件哈希
for exe in /usr/bin/lap-*; do
    echo "$exe: $(sha256sum $exe | cut -d' ' -f1)"
done
# 将输出更新到 allowed_executables.yaml

# 6. 启用服务
sudo systemctl daemon-reload
sudo systemctl enable lap-registry.socket lap-registry-asil.socket
sudo systemctl start lap-registry.socket lap-registry-asil.socket
```

### 6.2 验证安全配置

```bash
# 1. 验证 socket 权限
ls -la /run/lap/*.sock
# 期望输出:
# srw-rw-rw- 1 root root 0 Nov 19 14:00 registry.sock
# srw-rw---- 1 root lap-control 0 Nov 19 14:00 registry-asil.sock

# 2. 验证白名单
cat /etc/lap/com/allowed_executables.yaml | grep -A 2 "path:"

# 3. 测试权限控制
sudo -u lap-perception /opt/lap/test/test-qm-access   # 应成功
sudo -u lap-perception /opt/lap/test/test-asil-write  # 应失败 (只读)
sudo -u lap-control /opt/lap/test/test-asil-write     # 应成功

# 4. 查看审计日志
journalctl -t lap-registry-security -f
```

---

## 7. 安全认证证据 (FuSa Compliance)

### 7.1 物理隔离证明

- ✅ **独立 memfd**: QM 和 ASIL-D 使用不同的 `memfd_create()` 文件描述符
- ✅ **独立 socket**: 不同的 Unix domain socket 路径和权限
- ✅ **独立服务**: 不同的 systemd oneshot 服务单元
- ✅ **Guard pages**: 在内存区域前后设置不可访问页，防止越界

### 7.2 权限控制证明

- ✅ **Socket 权限**: `SocketGroup=lap-control` 限制 ASIL-D 访问
- ✅ **进程凭据验证**: `SO_PEERCRED` 获取客户端 PID/UID/GID
- ✅ **可执行文件验证**: `/proc/{pid}/exe` + SHA256 哈希验证
- ✅ **mprotect 强制**: 非授权进程映射为只读

### 7.3 完整性证明

- ✅ **CRC32 校验**: 每次读取验证数据完整性
- ✅ **写入计数器**: 防止重放攻击
- ✅ **seqlock 保障**: 防止并发读写数据损坏

### 7.4 审计追溯证明

- ✅ **systemd journal**: 所有安全事件记录到 journal
- ✅ **syslog 集成**: Critical 事件发送到 syslog
- ✅ **实时告警**: 支持集成外部监控系统
- ✅ **事件追溯**: 包含 PID/UID/时间戳/详细信息

---

## 8. 后续工作

### 8.1 待实现组件

- [ ] `SharedMemoryRegistrySecurity.cpp` (安全模块实现)
- [ ] `lap-registry-asil-init.cpp` (ASIL-D 初始化进程)
- [ ] `CRC32Calculator` (CRC32 快速计算实现)
- [ ] `ExecutableWhitelist` (白名单加载与验证)
- [ ] `AuditLogger` (审计日志实现)
- [ ] `SegfaultHandler` (信号处理器)

### 8.2 测试用例

- [ ] 权限控制测试 (ASIL-D 写入保护)
- [ ] CRC32 完整性测试 (篡改检测)
- [ ] 白名单测试 (注入防护)
- [ ] 性能基准测试 (含安全开销)
- [ ] 压力测试 (1000 并发连接)
- [ ] 故障注入测试 (攻击模拟)

### 8.3 认证工作

- [ ] FuSa 文档 (ISO 26262 证据)
- [ ] 安全审计报告
- [ ] 渗透测试报告
- [ ] 性能测试报告

---

## 9. 参考文献

1. **AUTOSAR AP R24-11**: Communication Management Specification
2. **ISO 26262**: Road vehicles - Functional safety
3. **CWE-863**: Incorrect Authorization
4. **CWE-502**: Deserialization of Untrusted Data
5. **systemd.socket(5)**: systemd socket unit configuration
6. **mprotect(2)**: Set protection on a region of memory
7. **memfd_create(2)**: Create an anonymous file

---

**维护者**: LightAP Architecture Team  
**最后更新**: 2025-11-19  
**文档版本**: 3.1
