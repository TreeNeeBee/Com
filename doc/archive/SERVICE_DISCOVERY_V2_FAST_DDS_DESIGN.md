# 服务发现架构 v2.0 - Fast-DDS Discovery Server 设计（已废弃）

## 归档说明

**归档日期**: 2025-11-19  
**原版本**: v2.0  
**废弃原因**: 已升级到 v3.0 零 Daemon 固定槽位自注册架构  
**替代方案**: 基于 systemd socket activation + memfd_create 的零守护进程架构  

本文档保存了 v2.0 版本中基于 Fast-DDS Discovery Server 的集中式服务发现设计，供历史参考和技术对比使用。

---

## v2.0 架构概述

### 传统架构的特点

v2.0 采用 Fast-DDS Discovery Server 作为集中式服务注册中心：

1. **Daemon 依赖**: RouDi (iceoryx v1) / Discovery Server (DDS) 等守护进程
   - 单点故障风险
   - 启动顺序依赖
   - 额外的进程调度开销
   
2. **通信开销**: Domain Socket / TCP 查询延迟
   - Discovery Server: ~0.5ms
   - D-Bus/SOME-IP SD: 5-100ms
   
3. **不确定性**: 网络/IPC 通信引入的抖动
   - 影响实时系统的可预测性

### v2.0 三层服务发现策略

**策略优先级**:

```text
Layer 1: 静态配置 (0ms，编译期确定)
    ↓
Layer 2: Fast-DDS Discovery Server (< 0.5ms，集中式注册中心)
    ↓
Layer 3: Binding 动态发现 (5-100ms，D-Bus/SOME-IP/DDS)
```

**详细说明**:

1. **Layer 1: 静态配置** (0ms，编译期确定):
   - 从配置文件加载预定义的服务端点
   - 启动时立即可用，零延迟
   - 符合 TPS_MANI_03312-03315 规范
   - 使用 ARXML 或 YAML 配置

2. **Layer 2: Fast-DDS Discovery Server** (< 0.5ms，集中式注册中心):
   - 使用 **Fast-DDS Discovery Server** 作为中央服务注册中心
   - 符合 **AUTOSAR EXP 7.2.1** 集中式服务发现架构
   - 支持 **Discovery Server v2.0** 协议（DDS-RTPS）
   - 优先查询 Discovery Server（TCP/UDP 通信）
   - 适用于高频服务查询、跨 ECU 服务聚合
   - 支持多 Discovery Server 凗余（Server 集群）
   - 自动同步所有 Participant 服务信息

3. **回退机制** (Fast-DDS Discovery Server 故障自动切换):
   - Discovery Server 连接失败 → 自动切换到 Binding 动态发现
   - Discovery Server 查询超时 (10ms) → 并行执行动态发现
   - Discovery Server 无结果 → 补充动态发现
   - 支持多 Server 凗余（自动切换备用 Server）

4. **Layer 3: 动态发现** (传统 AUTOSAR 模式):
   - D-Bus: NameOwnerChanged 信号
   - SOME/IP: SD Protocol
   - DDS: Participant Discovery

### 性能对比 (v2.0 vs v3.0)

```text
v2.0 三层策略:
  静态 (0ms) → Discovery Server (0.5ms) → 动态 (5-100ms)
  平均延迟: ~0.5ms (假设 80% 命中 Layer 2)

v3.0 双层策略:
  静态 (0ms) → 共享内存 (< 500ns)
  平均延迟: < 500ns (100% 命中 Layer 2)

性能提升: 1000× (0.5ms → 0.5μs)
```

---

## Fast-DDS Discovery Server 集成方案（AUTOSAR 兼容）

### 架构定位（符合 AUTOSAR EXP 7.2.1）

Fast-DDS Discovery Server 是 eProsima 实现的 **DDS Discovery Server v2.0 协议**，作为集中式服务发现中心，完全符合 AUTOSAR Adaptive Platform 的集中式架构推荐。

```
┌─────────────────────────────────────────────────────────────────────┐
│  Fast-DDS Discovery Server (独立进程或容器)                         │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │ Discovery Server Core (eProsima Fast-DDS)                   │    │
│  │  - RTPS Discovery Protocol v2.0                             │    │
│  │  - Server GUID: 44.53.00.5f.45.50.52.4f.53.49.4d.41         │    │
│  │  - Participant Database (所有 DomainParticipant 信息)       │    │
│  │  - DataWriter/DataReader Endpoint 数据库                    │    │
│  └─────────────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │ Network Endpoints (多传输层支持)                            │    │
│  │  - TCP: 192.168.1.100:11811 (默认)                          │    │
│  │  - UDP: 192.168.1.100:11811                                 │    │
│  │  - SHM: 共享内存传输（本地优化）                            │    │
│  └─────────────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │ Server 集群支持 (可选冗余)                                  │    │
│  │  - Primary Server: 192.168.1.100:11811                      │    │
│  │  - Backup Server 1: 192.168.1.101:11811                     │    │
│  │  - Backup Server 2: 192.168.1.102:11811                     │    │
│  │  - 自动故障转移 (Client 自动重连)                           │    │
│  └─────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────┘
         ▲                                      ▲
         │ RTPS Discovery Protocol              │ Service Registration
         │                                      │
    ┌────┴─────────┐                    ┌──────┴──────┐
    │ DDS Binding  │                    │ DDS Binding │
    │ (ECU 1)      │                    │ (ECU 2)     │
    │ - CLIENT     │                    │ - CLIENT    │
    └──────────────┘                    └─────────────┘
```

### Discovery Server 特性与 AUTOSAR 对应

| Fast-DDS 特性 | AUTOSAR 标准对应 | 说明 |
|--------------|-----------------|------|
| **Discovery Server v2.0** | EXP 7.2.1 Centralist Approach | 集中式服务注册表 |
| **SERVER-CLIENT 模式** | SWS_CM_02201 Static Configuration | Server 地址可静态配置 |
| **Participant Discovery** | RS_CM_00102 FindService | 发现 DomainParticipant |
| **Endpoint Discovery** | RS_CM_00101 OfferService | 发现 DataWriter/Reader |
| **Liveliness Protocol** | SWS_CM Service Lifecycle | 服务生命周期管理 |
| **Server 集群** | AUTOSAR Redundancy | 故障容错与高可用 |
| **TCP/UDP/SHM** | Multi-Transport | 多传输层支持 |

### Discovery Server 配置（YAML 格式）

```yaml
# discovery_server.yaml (Fast-DDS Discovery Server 配置)

discovery_server:
  # Server 标识
  server_id: 0  # Server GUID 生成基础
  
  # 网络配置
  listening_addresses:
    - transport: TCP
      address: 0.0.0.0
      port: 11811
      
    - transport: UDP
      address: 0.0.0.0
      port: 11811
  
  # Server 集群配置（冗余支持）
  servers:
    - id: 0
      address: 192.168.1.100:11811
      role: PRIMARY
      
    - id: 1
      address: 192.168.1.101:11811
      role: BACKUP
      
    - id: 2
      address: 192.168.1.102:11811
      role: BACKUP
  
  # 性能配置
  performance:
    max_participants: 10000
    max_endpoints: 100000
    announcement_period: 3000ms  # 3秒周期性通告
    liveliness_lease_duration: 10000ms
  
  # 持久化配置（可选）
  persistence:
    enabled: false
    database_file: /var/lib/fastdds/discovery_db.sqlite
  
  # 日志配置
  logging:
    verbosity: WARNING  # ERROR / WARNING / INFO / DEBUG
    file: /var/log/fastdds/discovery_server.log
```

### DDS Binding 集成（CLIENT 模式）

**binding_config.yaml 配置**:

```yaml
bindings:
  - type: dds
    library: /usr/lib/lap/com/binding_dds.so
    priority: 50
    enabled: true
    config:
      domain: 0
      
      # Discovery Server 客户端配置
      discovery_protocol: SERVER  # SIMPLE / SERVER / SUPER_CLIENT
      discovery_servers:
        # 主 Server
        - address: "192.168.1.100"
          port: 11811
          transport: TCP
          guid_prefix: "44.53.00.5f.45.50.52.4f.53.49.4d.41.00"
          
        # 备用 Server（自动故障转移）
        - address: "192.168.1.101"
          port: 11811
          transport: TCP
          guid_prefix: "44.53.00.5f.45.50.52.4f.53.49.4d.41.01"
      
      # 传输层配置
      transport:
        use_builtin_transports: false
        custom_transports:
          - type: TCPv4
            send_buffer_size: 65536
            receive_buffer_size: 65536
          
          - type: UDPv4  # 作为备用
            send_buffer_size: 65536
            receive_buffer_size: 65536
          
          - type: SHM  # 本地优化
            segment_size: 4194304  # 4MB
      
      # 服务发现配置
      discovery:
        use_static_edp: false  # 不使用静态端点发现
        ignore_participant_flags: FILTER_SAME_PROCESS
        lease_duration: 10000ms
        announcement_period: 3000ms
```

### Discovery Server 启动与管理

**Systemd 服务单元** (`/etc/systemd/system/fastdds-discovery-server.service`):

```ini
[Unit]
Description=Fast-DDS Discovery Server (AUTOSAR Service Discovery)
Documentation=https://fast-dds.docs.eprosima.com/en/latest/fastddscli/cli/cli.html
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=fastdds
Group=fastdds

# Discovery Server 启动命令
ExecStart=/usr/bin/fastdds discovery \
    --server-id 0 \
    --ip-address 0.0.0.0 \
    --port 11811 \
    --transport tcp \
    --backup \
    --backup-file /var/lib/fastdds/server_backup.db

# 资源限制
MemoryLimit=1G
CPUQuota=200%

# 重启策略
Restart=always
RestartSec=5s

# 日志
StandardOutput=journal
StandardError=journal
SyslogIdentifier=fastdds-discovery

[Install]
WantedBy=multi-user.target
```

**启动命令**:

```bash
# 启动 Discovery Server (PRIMARY)
$ fastdds discovery \
    --server-id 0 \
    --ip-address 192.168.1.100 \
    --port 11811 \
    --transport tcp \
    --backup \
    --backup-file /var/lib/fastdds/server_0.db

# 启动 Backup Server 1
$ fastdds discovery \
    --server-id 1 \
    --ip-address 192.168.1.101 \
    --port 11811 \
    --transport tcp \
    --backup

# 启动 Backup Server 2
$ fastdds discovery \
    --server-id 2 \
    --ip-address 192.168.1.102 \
    --port 11811 \
    --transport tcp \
    --backup
```

### Discovery Server 协议原理

**RTPS Discovery Protocol v2.0**:

```
Client (DomainParticipant)          Discovery Server
        │                                   │
        │  1. PDP DATA (Participant Info)   │
        ├──────────────────────────────────>│
        │                                   │ 存储到 Participant DB
        │                                   │
        │  2. PDP DATA_FRAG (确认)          │
        │<──────────────────────────────────┤
        │                                   │
        │  3. EDP DATA (Endpoint Info)      │
        ├──────────────────────────────────>│
        │                                   │ 更新 Endpoint DB
        │                                   │
        │  4. EDP DATA (匹配的 Endpoints)   │
        │<──────────────────────────────────┤
        │                                   │
        │  5. 定期 Liveliness 消息          │
        ├<─────────────────────────────────>│
        │     (保持活跃状态)                │
```

**关键协议消息**:

1. **PDP (Participant Discovery Protocol)**:
   - `DATA(SPDPdiscoveredParticipantData)`: Participant 注册
   - 包含: GUID, QoS, 网络定位器 (IP:Port)

2. **EDP (Endpoint Discovery Protocol)**:
   - `DATA(DiscoveredWriterData)`: DataWriter 注册
   - `DATA(DiscoveredReaderData)`: DataReader 注册
   - 包含: Topic 名称, QoS 策略, GUID

3. **Liveliness Protocol**:
   - 周期性心跳消息（默认 3 秒）
   - Server 检测 Client 超时（默认 10 秒）

### AUTOSAR lap::com 集成

**DDS Binding 实现 Discovery Server Client**:

```cpp
// modules/Com/source/binding/dds/DdsDiscoveryClient.hpp

namespace lap::com::binding::dds {

class DdsDiscoveryClient {
public:
    struct Config {
        // Discovery Server 地址列表（支持多 Server 冗余）
        std::vector<ServerEndpoint> servers;
        
        struct ServerEndpoint {
            std::string address;
            uint16_t port;
            TransportType transport;  // TCP / UDP
            std::string guid_prefix;
        };
        
        // 连接配置
        std::chrono::milliseconds connect_timeout{1000};
        std::chrono::milliseconds query_timeout{10};
        uint32_t max_retry_count = 3;
        bool enable_fallback = true;  // 回退到 Simple Discovery
    };
    
    // 初始化 Discovery Client
    Result<void> Initialize(const Config& config);
    
    // 注册服务到 Discovery Server（通过 DDS Participant）
    Result<void> RegisterService(
        const ServiceInstanceInfo& service_info,
        eprosima::fastdds::dds::DomainParticipant* participant
    );
    
    // 从 Discovery Server 查找服务
    Result<std::vector<ServiceInstanceInfo>> FindService(
        const std::string& service_interface_name,
        const InstanceIdentifier& instance_id
    );
    
    // 订阅服务变化（利用 DDS Built-in Topics）
    Result<void> SubscribeServiceChanges(
        FindServiceHandler handler
    );

private:
    // Fast-DDS DomainParticipant (SERVER 模式)
    eprosima::fastdds::dds::DomainParticipant* participant_;
    
    // Discovery Server 配置
    eprosima::fastrtps::rtps::RemoteServerList_t remote_servers_;
    
    // Built-in Topics 监听
    std::unique_ptr<BuiltinTopicsListener> builtin_listener_;
};

} // namespace lap::com::binding::dds
```

**DomainParticipant 配置（Discovery Server 模式）**:

```cpp
// 创建 DomainParticipant with Discovery Server

#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/rtps/transport/TCPv4TransportDescriptor.h>

using namespace eprosima::fastdds::dds;
using namespace eprosima::fastrtps::rtps;

void DdsDiscoveryClient::Initialize(const Config& config) {
    // 1. 配置 DomainParticipantQos
    DomainParticipantQos pqos = PARTICIPANT_QOS_DEFAULT;
    
    // 2. 设置 Discovery Protocol 为 SERVER
    pqos.wire_protocol().builtin.discovery_config.discoveryProtocol = 
        DiscoveryProtocol_t::SERVER;
    
    // 3. 配置 Discovery Server 列表
    for (const auto& server : config.servers) {
        Locator_t locator;
        locator.kind = (server.transport == TransportType::TCP) ? 
            LOCATOR_KIND_TCPv4 : LOCATOR_KIND_UDPv4;
        IPLocator::setIPv4(locator, server.address);
        locator.port = server.port;
        
        // 添加 Remote Server
        RemoteServerAttributes server_attr;
        server_attr.ReadguidPrefix(server.guid_prefix.c_str());
        server_attr.metatrafficUnicastLocatorList.push_back(locator);
        
        pqos.wire_protocol().builtin.discovery_config.m_DiscoveryServers
            .push_back(server_attr);
    }
    
    // 4. 禁用 Simple Discovery（避免网络广播）
    pqos.wire_protocol().builtin.discovery_config.use_SIMPLE_EndpointDiscoveryProtocol = false;
    pqos.wire_protocol().builtin.discovery_config.use_STATIC_EndpointDiscoveryProtocol = false;
    
    // 5. 配置传输层（TCP 优先）
    pqos.transport().use_builtin_transports = false;
    
    auto tcp_transport = std::make_shared<TCPv4TransportDescriptor>();
    tcp_transport->sendBufferSize = 65536;
    tcp_transport->receiveBufferSize = 65536;
    pqos.transport().user_transports.push_back(tcp_transport);
    
    // 6. 创建 DomainParticipant
    participant_ = DomainParticipantFactory::get_instance()
        ->create_participant(0, pqos);
    
    if (!participant_) {
        return Error{ErrorCode::kDiscoveryServerConnectionFailed};
    }
    
    // 7. 订阅 Built-in Topics（监听服务发现事件）
    builtin_listener_ = std::make_unique<BuiltinTopicsListener>();
    participant_->get_builtin_subscriber()
        ->get_datareader(fastdds::dds::rtps::participant_topic_name)
        ->set_listener(builtin_listener_.get());
    
    return {};
}
```

**服务映射（lap::com ↔ DDS）**:

```cpp
// lap::com 服务 → DDS Topic/DataWriter

struct ServiceToDdsMapping {
    // lap::com 服务实例
    std::string service_interface;      // "RadarService"
    InstanceIdentifier instance_id;     // 0x1234
    
    // DDS 映射
    std::string topic_name;             // "RadarService_0x1234"
    std::string type_name;              // "RadarData"
    DomainId_t domain_id;               // 0
    DataWriter* writer;
    DataReader* reader;
    
    // Discovery Server 自动处理
    // - Participant 注册
    // - Endpoint (DataWriter/DataReader) 注册
    // - Topic 信息传播
};
```

**服务查找流程（集成 Discovery Server）**:

```cpp
Result<std::vector<ServiceInstanceInfo>> 
DdsDiscoveryClient::FindService(
    const std::string& service_interface_name,
    const InstanceIdentifier& instance_id
) {
    // 1. 查询 Built-in Topic: DCPSParticipant
    auto participant_reader = participant_->get_builtin_subscriber()
        ->lookup_datareader(participant_topic_name);
    
    ParticipantBuiltinTopicData participant_data;
    SampleInfo info;
    
    std::vector<ServiceInstanceInfo> found_services;
    
    // 2. 遍历所有已发现的 Participant
    while (participant_reader->take_next_sample(&participant_data, &info) 
           == ReturnCode_t::RETCODE_OK) {
        
        if (info.valid_data) {
            // 3. 检查 Participant User Data (包含 lap::com 服务信息)
            // 我们在 OfferService 时将服务元数据编码到 USER_DATA QoS
            auto service_info = DecodeServiceInfoFromUserData(
                participant_data.user_data.value());
            
            if (service_info.service_interface == service_interface_name &&
                (instance_id.IsAny() || service_info.instance_id == instance_id)) {
                found_services.push_back(service_info);
            }
        }
    }
    
    // 4. Discovery Server 已自动同步所有 Participant 信息
    // 无需手动查询网络，延迟 < 1ms
    
    return found_services;
}
```

### 性能优势对比

| 发现方式 | 延迟 | 网络开销 | 适用场景 | AUTOSAR 合规性 |
|---------|------|---------|---------|---------------|
| **Fast-DDS Discovery Server** | 0.3-0.5ms | 极低（仅初始连接） | 多 ECU、大规模部署 | ✅ EXP 7.2.1 |
| **DDS Simple Discovery** | 10-50ms | 高（广播/组播） | 小规模、局域网 | ✅ SWS_CM_11001 |
| **Static Endpoints (YAML)** | 0ms | 零 | 已知拓扑 | ✅ TPS_MANI_03312 |
| **Legacy (D-Bus/SOME/IP)** | 5-100ms | 中 | 向后兼容 | ✅ SWS_CM_10289 |

**Discovery Server 核心优势**:

1. ✅ **集中式管理**：符合 AUTOSAR EXP 7.2.1 推荐架构
2. ✅ **低延迟**：0.3-0.5ms（vs Simple Discovery 10-50ms）
3. ✅ **低网络开销**：避免 Simple Discovery 的广播/组播风暴
4. ✅ **跨 ECU 支持**：天然支持多节点服务发现
5. ✅ **高可用性**：Server 集群冗余（自动故障转移）
6. ✅ **标准协议**：DDS-RTPS 标准，跨厂商互操作
7. ✅ **QoS 感知**：完整支持 DDS QoS 策略（Reliability、Durability 等）

---

## 实施路线图（v2.0）

### Week 9-10: 集中式注册中心 (Fast-DDS Discovery Server 部署)

- **Fast-DDS Discovery Server 部署**
  - Discovery Server 二进制部署 (fast-discovery-server)
  - PRIMARY + BACKUP 服务器配置（集群冗余）
  - Systemd 服务单元配置
  - 网络拓扑规划（TCP/UDP/SHM 传输层选择）

- **DDS Binding Discovery Server CLIENT 集成**
  - DomainParticipant QoS 配置（SERVER 模式）
  - RemoteServerList 配置（多 Server 地址）
  - Built-in Topics Listener 实现
  - 故障转移逻辑（自动切换 BACKUP Server）

- **三层混合服务发现策略（完整实现）**
  - Layer 1: 静态配置（static_endpoints.yaml，0ms）
  - Layer 2: Discovery Server 快速查询（< 0.5ms，RTPS Discovery）
  - Layer 3: Binding 动态发现（自动回退，5-100ms）
  - 并行查询模式（PARALLEL 策略）
  - 结果缓存（TTL 管理）
  - AUTOSAR EXP 7.2.1 合规性验证

- **配置与部署**
  - YAML 配置文件支持（yaml-cpp）
  - AUTOSAR ARXML 转换工具
  - Systemd 服务单元（registry daemon）
  - 性能基准测试（延迟 < 0.5ms）

- **测试与验证**
  - 单元测试（Client/Daemon）
  - 集成测试（三层混合发现）
  - 故障恢复测试（回退机制）
  - 性能测试（吞吐量 + 延迟）

---

## 性能指标 (v2.0)

### 服务发现延迟对比

| 发现方式 | 平均延迟 | P99 延迟 | 适用场景 |
|----------|----------|----------|----------|
| **Layer 1: 静态配置** (YAML) | 0 ms | 0 ms | 启动时立即可用 |
| **Layer 2: Discovery Server** (RTPS) | 0.3 ms | 0.5 ms | 高频查询、跨 ECU |
| **Layer 3: iceoryx2 发现** | 1-3 ms | 5 ms | 本地零拷贝服务 |
| **Layer 3: DDS 发现** | 10-30 ms | 100 ms | 跨 ECU 服务（Simple Discovery）|
| **Layer 3: Legacy Gateway** | 5-50 ms | 200 ms | D-Bus/SOME/IP 兼容 |
| **三层混合模式** | 0.3-10 ms | 50 ms | 生产环境推荐 |

### 资源占用

**Fast-DDS Discovery Server**:
- 内存: ~15 MB (1000 个 DomainParticipant)
- CPU: < 2% (空闲)，< 8% (峰值，RTPS 处理)
- 磁盘: 可选持久化存储（SQLite 数据库）
- 依赖: Fast-DDS 库 (libfastrtps)
- 端口: TCP 11811 (默认) / UDP 7400 (可配置)

**DDS Binding Discovery Client**:
- 内存: ~200 KB per DomainParticipant
- 网络连接: 1 个 TCP 连接到 Discovery Server per Participant
- Built-in Topics 监听: 自动处理（Fast-DDS 内部）

---

## v2.0 vs v3.0 对比总结

| 特性 | v2.0 (Fast-DDS Discovery Server) | v3.0 (零 Daemon 固定槽位) |
|------|--------------------------------|--------------------------|
| **架构** | 集中式守护进程 | 完全去中心化 |
| **延迟** | 0.3-0.5ms | < 500ns |
| **守护进程** | 需要 Discovery Server | ❌ 零 Daemon |
| **单点故障** | 存在（需集群冗余） | ❌ 无 |
| **网络开销** | 低（TCP/UDP连接） | ❌ 零 |
| **代码复杂度** | 高（>2000行） | 低（~300行） |
| **AUTOSAR合规** | ✅ EXP 7.2.1 | ✅ R24-11 SWS |
| **功能安全** | 需隔离Daemon | ✅ 天然隔离 |

---

## 参考资料

- Fast-DDS 官方文档: <https://fast-dds.docs.eprosima.com/>
- AUTOSAR AP R24-11 规范
- DDS-RTPS 标准: <https://www.omg.org/spec/DDSI-RTPS/>
- eProsima Fast-DDS GitHub: <https://github.com/eProsima/Fast-DDS>

---

**结束语**

本文档归档了 v2.0 基于 Fast-DDS Discovery Server 的服务发现架构设计。该设计已被 v3.0 的零 Daemon 固定槽位自注册架构取代，但仍具有历史参考价值和技术对比意义。

如需了解最新的 v3.0 架构设计，请参阅 `SERVICE_DISCOVERY_ARCHITECTURE.md` 主文档。
