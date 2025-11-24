# Epoch机制迁移方案

## 1. 架构变更总结

### 1.1 帧头格式变更

**原设计**（8-24字节可变）:
```
Byte 0-3:    MagicID + Version + Type + Flags + Reserved + PLen + HdrLen
Byte 4-(3+PLen): Payload Length (0-4字节可变)
Byte (4+PLen)-(7+PLen): Sequence Number (32-bit)
Byte (8+PLen)-(11+PLen): CRC32 (可选)
Byte (CRC_END+1)-(CRC_END+8): Timestamp (可选)
Byte (TS_END+1)-(TS_END+8): Extended Header (可选)
```

**新设计**（12-28字节可变）:
```
Byte 0-3:    MagicID + Version + Type + Flags + Reserved + PLen + HdrLen
Byte 4-(3+PLen): Payload Length (0-4字节可变)
Byte (4+PLen)-(7+PLen): Epoch (32-bit) ✨ 新增
Byte (8+PLen)-(11+PLen): Sequence Number (32-bit)
Byte (12+PLen)-(15+PLen): CRC32 (可选)
Byte (CRC_END+1)-(CRC_END+8): Timestamp (可选，仅用于时钟同步)
Byte (TS_END+1)-(TS_END+8): Extended Header (可选)
```

### 1.2 核心概念变更

| 方面 | 原设计 | 新设计 (Epoch) |
|------|--------|----------------|
| **序列号空间** | 32-bit单一空间 | 64-bit (Epoch+Seq) |
| **回绕处理** | RFC 1982窗口机制 | Epoch续约，Seq清零 |
| **全局标识** | 仅Seq | (Epoch, Seq)元组 |
| **比较算法** | 复杂窗口比较 | 简单元组比较 |
| **续约机制** | 连接续约+新连接 | Epoch+1，Seq清零 |
| **开销** | +0字节（使用窗口） | +4字节（Epoch字段） |
| **生命周期** | ~14小时@1Gbps | 实际无限（2^64包） |

### 1.3 Timestamp字段使用场景说明

**✅ 推荐使用场景**:
1. **时钟同步**：TSN/PTP环境下的时间同步
2. **事件时间戳**：记录传感器采样的精确时间
3. **时延测量**：配合F.S标志，计算端到端延迟（需双向同步）

**❌ 不推荐场景**:
1. **RTT计算**：应使用单调时钟，避免时钟跳变影响
2. **RTO超时检测**：应使用本地单调时钟
3. **乱序检测**：应使用Epoch+SeqNum

**最佳实践**:
- 仅在确实需要绝对时间时才启用F.T标志
- 性能敏感场景优先使用Epoch+SeqNum排序
- 时钟同步场景建议配合F.S标志（表示本地时钟已同步）

## 2. Epoch机制核心设计

### 2.1 EpochSequenceManager

```cpp
class EpochSequenceManager {
public:
    struct GlobalSequence {
        uint32_t epoch;   // 连接周期号
        uint32_t seq;     // 序列号
        
        bool operator<(const GlobalSequence& other) const {
            if (epoch != other.epoch) return epoch < other.epoch;
            return seq < other.seq;
        }
        
        uint64_t ToUint64() const {
            return (static_cast<uint64_t>(epoch) << 32) | seq;
        }
    };
    
    static constexpr uint32_t RENEWAL_THRESHOLD = 0xFFFFF000;   // 剩余16M时触发
    static constexpr uint32_t CRITICAL_THRESHOLD = 0xFFFFFF00;  // 剩余256个时临界
    
private:
    uint32_t current_epoch_ = 0;
    uint32_t current_seq_ = 0;
    
public:
    GlobalSequence GetNext() {
        GlobalSequence result{current_epoch_, current_seq_};
        current_seq_++;
        return result;
    }
    
    bool ShouldRenew() const {
        return current_seq_ >= RENEWAL_THRESHOLD;
    }
    
    void RenewEpoch() {
        current_epoch_++;
        current_seq_ = 0;
        LogInfo("Epoch renewed: " + std::to_string(current_epoch_));
    }
};
```

### 2.2 续约流程对比

**原设计（连接续约）**:
```
1. 检测 seq >= 0xFFFFF000
2. 建立新UDP连接（新端口）
3. 双通道并行传输500ms（旧seq持续递增）
4. 迁移到新连接
5. seq重置为0
```

**新设计（Epoch续约）**:
```
1. 检测 seq >= 0xFFFFF000
2. 通信双方协商：发送 EPOCH_RENEWAL 控制消息
3. 双方同步执行：epoch++, seq=0
4. 继续使用同一UDP连接
5. 无需建立新连接
```

**优势对比**:

| 特性 | 连接续约 | Epoch续约 |
|------|---------|----------|
| 复杂度 | 高（需新连接+双通道） | 低（仅更新计数器） |
| 延迟 | ~100ms（建立连接） | <10ms（更新epoch） |
| 带宽开销 | 2倍（双通道500ms） | 无额外开销 |
| 失败处理 | 复杂（需回退） | 简单（重试即可） |
| 数据丢失风险 | 低（双通道保护） | 无（同一连接） |
| 帧头开销 | 0字节 | +4字节/帧 |

## 3. 修改清单

### 3.1 帧头结构修改

文件：`CUSTOM_PROTOCOL_UDP_INTEGRATION_GUIDE.md`

**位置1**: 第160-320行 - 帧头格式图示
- [x] 添加32-bit Epoch字段在Sequence Number前
- [x] 更新字节偏移说明
- [x] 增加Epoch字段详细说明
- [x] 更新Timestamp字段使用场景说明

**位置2**: 第320-500行 - 字段详细说明
- [ ] 需要更新：字段说明表格
- [ ] 需要更新：帧头大小计算示例
- [ ] 需要更新：编码/解码示例代码

### 3.2 序列号管理章节重构

文件：`CUSTOM_PROTOCOL_UDP_INTEGRATION_GUIDE.md`

**第4.1.10节** (约2400-3500行):

当前章节：`4.1.10 序列号回绕处理 (Sequence Number Wraparound Management)`

需改为：`4.1.10 Epoch机制与序列号管理 (Epoch-Based Sequence Management)`

**子节调整**:

| 原章节 | 新章节 | 状态 |
|--------|--------|------|
| 4.1.10.1 问题背景 | 4.1.10.1 Epoch机制的必要性 | 待修改 |
| 4.1.10.2 RFC 1982序列号比较 | 4.1.10.2 Epoch管理器 | 待修改 |
| 4.1.10.3 连接续约流程 | 4.1.10.3 Epoch续约流程 | 待修改 |
| 4.1.10.4 对端响应处理 | 4.1.10.4 双方同步机制 | 待修改 |
| 4.1.10.5 性能与可靠性保证 | 4.1.10.5 性能分析 | 待修改 |
| 4.1.10.6 监控与告警 | 4.1.10.6 监控与告警 | 待修改 |
| 4.1.10.7 使用示例 | 4.1.10.7 使用示例 | 待修改 |

### 3.3 相关章节联动更新

**需要更新的其他章节**:

1. **4.1.11 拥塞控制** (~3500-4100行)
   - CongestionController中的RTT计算相关代码需适配Epoch
   - PacketLossDetector中的序列号比较逻辑需更新

2. **4.1.12 RTO动态计算** (~4100-4700行)
   - RetransmissionManager中的PacketRecord需添加epoch字段
   - Karn's算法需基于(epoch, seq)判断是否为重传包

3. **编码/解码示例** (多处)
   - CompactFrameHeader结构需添加epoch字段
   - EncodeFrame/DecodeFrame函数需处理epoch

### 3.4 代码示例更新

所有包含`uint32_t sequence`的代码示例需改为：
```cpp
struct GlobalSequence {
    uint32_t epoch;
    uint32_t seq;
};
```

## 4. 实施计划

### 阶段1：核心数据结构更新（已完成）
- [x] 更新帧头格式图示
- [x] 添加Epoch字段说明
- [x] 更新Timestamp使用场景说明

### 阶段2：4.1.10章节重构（待进行）
- [ ] 重写4.1.10.1 - Epoch机制必要性
- [ ] 重写4.1.10.2 - EpochSequenceManager实现
- [ ] 重写4.1.10.3 - Epoch续约流程（简化版）
- [ ] 重写4.1.10.4 - 双方同步机制
- [ ] 更新4.1.10.5 - 性能分析（对比连接续约）
- [ ] 更新4.1.10.6 - 监控指标（添加epoch相关）
- [ ] 更新4.1.10.7 - 使用示例

### 阶段3：联动章节更新（待进行）
- [ ] 更新拥塞控制中的序列号处理
- [ ] 更新RTO计算中的PacketRecord结构
- [ ] 更新所有编码/解码示例
- [ ] 更新测试用例

### 阶段4：验证与测试（待进行）
- [ ] 回归测试所有代码示例
- [ ] 验证帧头大小计算
- [ ] 性能基准测试
- [ ] 文档一致性检查

## 5. 风险评估

### 5.1 向后兼容性

**问题**：帧头增加4字节，旧版本无法解析

**解决方案**：
1. 使用Version字段区分：
   - Version 0x1: 无Epoch（原设计）
   - Version 0x2: 有Epoch（新设计）

2. 协商机制：
   - 连接建立时协商版本
   - 降级到最低公共版本

### 5.2 性能影响

**额外开销**：
- 帧头：+4字节/帧
- @ 1Gbps, 1500B包：+4/(1500+4) ≈ 0.27%带宽
- @ 10Gbps, 500B包：+4/(500+4) ≈ 0.8%带宽

**结论**：开销可接受（<1%）

### 5.3 实现复杂度

| 方面 | 连接续约 | Epoch续约 |
|------|---------|----------|
| 代码行数 | ~800行 | ~300行 |
| 状态机 | 5个状态 | 2个状态 |
| 线程数 | 3个（监控+双通道） | 1个（监控） |
| 测试用例 | ~20个 | ~8个 |

**结论**：Epoch方案大幅简化实现

## 6. 下一步行动

**立即执行**:
1. ✅ 确认Epoch机制设计方案
2. ✅ 更新帧头格式文档
3. ⏳ 重构4.1.10章节（约2000行代码需更新）
4. ⏳ 更新相关章节的序列号处理逻辑

**用户确认点**:
- [ ] 是否接受+4字节/帧的开销？
- [ ] 是否需要保留连接续约作为备选方案？
- [ ] 版本兼容性策略是否认可？

**预计工作量**:
- 文档更新：~4小时
- 代码示例验证：~2小时
- 测试用例更新：~2小时
- **总计：~8小时**

