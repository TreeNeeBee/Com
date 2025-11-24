# 遗留设计归档: 动态服务发现与中央注册守护进程

**归档日期**: 2025-11-20  
**归档原因**: v3.0 架构采用零 Daemon + 固定槽位设计，以下内容不再适用  
**原文档**: SERVICE_DISCOVERY_ARCHITECTURE.md (遗留章节)

---

## 1. 中央注册守护进程配置 (已废弃)

### 1.1 集中式注册中心配置 (YAML格式)

```yaml
# lightap_com_config.yaml (使用 yaml-cpp 解析)

lap_com:
  service_discovery:
    # 静态配置 (Layer 1 - 最快)
    static_endpoints:
      file: /etc/lap/com/static_endpoints.yaml
      enabled: true
    
    # 集中式注册中心配置 (Layer 2 - 可选)
    central_registry:
      enabled: true
      socket_path: /tmp/lightap_sd.sock
      connect_timeout_ms: 100
      query_timeout_ms: 10
      max_retry_count: 2
      enable_fallback: true
      fallback_strategy: ON_TIMEOUT  # IMMEDIATE / ON_TIMEOUT / PARALLEL
      cache_results: true
      cache_ttl_seconds: 60
    
    # 动态服务发现配置 (Layer 3 - 总是启用，作为回退)
    dynamic_discovery:
      enabled: true
      bindings:
        - iceoryx2  # 本地服务自动发现
        - dds       # DDS Built-in Topics
        - legacy    # Gateway 处理 D-Bus/SOME/IP
```

### 1.2 注册中心守护进程配置

```yaml
# central_registry_daemon.yaml

registry_daemon:
  socket_path: /tmp/lightap_sd.sock
  socket_permissions: "0666"
  
  # 服务数据库
  database:
    max_services: 10000
    cleanup_interval_seconds: 10
    default_ttl_seconds: 30
  
  # 性能配置
  performance:
    worker_threads: 4
    io_backend: epoll  # epoll / io_uring
    max_concurrent_connections: 1000
  
  # 监听 4-Binding 动态服务发现
  transport_listeners:
    iceoryx2:
      enabled: true
      domain_name: lightap_com
    
    dds:
      enabled: true
      domain_id: 0
      discovery_server: "192.168.1.100:34567"
    
    legacy_gateway:
      enabled: true
      gateway_socket: /tmp/legacy_gateway.sock
  
  # 多节点支持 (可选)
  multi_node:
    enabled: false
    sync_protocol: gossip  # gossip / raft
    peer_nodes:
      - "192.168.1.10:8001"
      - "192.168.1.11:8001"
```

**废弃原因**: v3.0 架构完全取消守护进程，使用 memfd + systemd socket activation 实现零 Daemon 启动。

---

## 2. SOME/IP Binding 动态服务发现 (已废弃)

### 2.1 SOME/IP SD Protocol 实现

```cpp
class SomeIpBinding : public ITransportBinding {
public:
    Result<void> OfferService(
        const InstanceIdentifier& instance_id,
        const ServiceInterfaceInfo& interface_info
    ) override {
        // 1. 构建 SOME/IP SD OfferService 消息
        SomeIpSdMessage offer_msg;
        offer_msg.message_type = SomeIpSdMessageType::OFFER_SERVICE;
        offer_msg.service_id = GetServiceId(interface_info.name);
        offer_msg.instance_id = GetInstanceId(instance_id);
        offer_msg.major_version = interface_info.major_version;
        offer_msg.ttl = GetConfiguredTTL();  // Time-To-Live
        
        // 2. 添加 Endpoint Options (IP地址和端口)
        offer_msg.AddEndpointOption(
            SomeIpEndpointOption{
                .ip_address = GetLocalIpAddress(),
                .port = GetServicePort(),
                .protocol = SomeIpTransportProtocol::TCP
            }
        );
        
        // 3. 发送 SD 消息 (组播)
        SendSdMessage(offer_msg, GetSdMulticastAddress());
        
        // 4. 启动周期性发送 (Initial/Repetition/Cyclic phases)
        StartPeriodicOfferService(offer_msg);
        
        return Result<void>::FromValue();
    }
    
    Result<void> StartFindService(
        const InstanceIdentifier& instance_filter,
        std::function<void(const ServiceInstanceInfo&)> callback
    ) override {
        // 1. 发送 FindService 消息
        SomeIpSdMessage find_msg;
        find_msg.message_type = SomeIpSdMessageType::FIND_SERVICE;
        find_msg.service_id = GetServiceId(GetServiceInterfaceName());
        find_msg.instance_id = GetInstanceId(instance_filter);
        find_msg.major_version = GetRequiredMajorVersion();
        
        SendSdMessage(find_msg, GetSdMulticastAddress());
        
        // 2. 监听 OfferService 响应
        RegisterSdMessageHandler([callback, instance_filter](const SomeIpSdMessage& msg) {
            if (msg.message_type == SomeIpSdMessageType::OFFER_SERVICE &&
                MatchesFilter(msg, instance_filter)) {
                
                ServiceInstanceInfo info = ConvertFromSdMessage(msg);
                callback(info);
            }
        });
        
        return Result<void>::FromValue();
    }
};
```

**废弃原因**: v3.0 使用固定槽位映射，SOME/IP 服务通过静态配置注册，无需动态 SD 协议。

---

## 3. D-Bus Binding 动态服务发现 (已废弃)

### 3.1 D-Bus Service Discovery 实现

```cpp
class DbusBinding : public ITransportBinding {
public:
    Result<void> OfferService(
        const InstanceIdentifier& instance_id,
        const ServiceInterfaceInfo& interface_info
    ) override {
        // 1. 请求 D-Bus Bus Name
        std::string bus_name = GenerateBusName(interface_info.name, instance_id);
        RequestBusName(bus_name);
        
        // 2. 注册 Object Path
        std::string object_path = GenerateObjectPath(instance_id);
        RegisterObjectPath(object_path);
        
        // 3. 发布到 D-Bus Introspection
        PublishToIntrospection(bus_name, object_path, interface_info);
        
        return Result<void>::FromValue();
    }
    
    Result<void> StartFindService(
        const InstanceIdentifier& instance_filter,
        std::function<void(const ServiceInstanceInfo&)> callback
    ) override {
        // 1. 监听 D-Bus NameOwnerChanged 信号
        WatchBusNameOwnerChanged([callback, instance_filter](
            const std::string& name,
            const std::string& old_owner,
            const std::string& new_owner) {
            
            if (!new_owner.empty() && MatchesServicePattern(name, instance_filter)) {
                // 服务可用
                ServiceInstanceInfo info = QueryServiceInfo(name);
                callback(info);
            }
        });
        
        // 2. 查询现有服务
        auto existing_services = ListDBusNames();
        for (const auto& name : existing_services) {
            if (MatchesServicePattern(name, instance_filter)) {
                ServiceInstanceInfo info = QueryServiceInfo(name);
                callback(info);
            }
        }
        
        return Result<void>::FromValue();
    }
};
```

**废弃原因**: D-Bus 服务通过 Legacy Gateway 隔离处理，不再直接集成到核心服务发现。

---

## 4. Legacy Gateway 架构 (已废弃)

### 4.1 Legacy Gateway 设计思路

Legacy Gateway 设计用于隔离处理传统 D-Bus/SOME/IP 服务发现，避免污染零延迟的 iceoryx2 共享内存路径。

```
┌─────────────────────────────────────────┐
│  lap::com 核心 (iceoryx2 Zero-Copy)     │
│  - 固定槽位注册表                       │
│  - memfd + SCM_RIGHTS                  │
│  - seqlock 无锁读                       │
└──────────────┬──────────────────────────┘
               │
               │ Unix Domain Socket
               ↓
┌─────────────────────────────────────────┐
│  Legacy Gateway (独立进程)              │
│  - D-Bus Service Discovery             │
│  - SOME/IP SD Protocol                 │
│  - 结果转发到注册表                     │
└─────────────────────────────────────────┘
```

**配置示例**:

```yaml
legacy_gateway:
  enabled: true
  gateway_socket: /tmp/legacy_gateway.sock
  
  dbus:
    enabled: true
    bus_type: system  # system / session
  
  someip:
    enabled: true
    multicast_address: "239.0.0.1:30490"
    ttl: 5
```

**废弃原因**: v3.0 架构彻底移除守护进程，Legacy 协议通过外部转换工具离线处理，不在运行时动态发现。

---

## 5. 三层服务发现架构 (已废弃)

### 5.1 分层设计

```
Layer 1: Static Endpoints (静态配置，编译期确定)
  ├─ 优先级: 最高
  ├─ 延迟: 0 ns (编译期内联)
  └─ 用途: 关键 ASIL-D 服务

Layer 2: Central Registry (集中式注册表，运行时查询)
  ├─ 优先级: 中
  ├─ 延迟: ~10 μs (Unix Socket IPC)
  └─ 用途: 动态服务注册/发现

Layer 3: Dynamic Discovery (动态协议，自动发现)
  ├─ 优先级: 最低
  ├─ 延迟: 5-50 ms (网络延迟)
  └─ 用途: 跨节点服务发现
```

**废弃原因**: v3.0 简化为单层固定槽位架构，通过 YAML 配置在编译/部署期确定所有服务映射。

---

## 6. 性能对比表 (遗留数据)

| 发现方式 | 首次延迟 | 持续监控延迟 | 适用场景 |
|---------|---------|-------------|---------|
| **Static Endpoints** | 0 ns | N/A | ASIL-D 关键服务 |
| **Central Registry** | ~10 μs | ~5 μs | 本地动态服务 |
| **iceoryx2 Shared Memory** | ~1 μs | ~500 ns | 零拷贝数据通道 |
| **DDS Discovery** | 10-100 ms | 50 ms | 跨节点服务 |
| **Legacy Binding** (D-Bus/SOME/IP) | 5-50 ms | 200 ms | 向后兼容 |

**说明**: v3.0 架构完全移除 Central Registry 和 Legacy Binding 动态发现，专注于固定槽位 + seqlock 的极致性能优化。

---

## 7. 实施路线图 (遗留计划)

### Week 3-4: 4-Binding 插件集成 (已废弃)

- ❌ SOME/IP Binding 服务发现（SD Protocol）
- ❌ D-Bus Binding 服务发现（NameOwnerChanged）
- ❌ Legacy Gateway 服务发现（隔离处理）

**新架构**: v3.0 只保留 iceoryx2 Binding 的固定槽位注册，其他协议通过离线工具转换。

---

## 总结

v3.0 架构的核心理念是"零守护进程 + 固定槽位 + 编译期确定"，彻底摒弃了以下遗留设计：

1. ❌ 中央注册守护进程 (Central Registry Daemon)
2. ❌ 动态服务发现协议 (SOME/IP SD, D-Bus Discovery)
3. ❌ Legacy Gateway 隔离架构
4. ❌ 三层服务发现分层设计
5. ❌ 运行时服务 TTL 和心跳管理（改为编译期 YAML 配置）

v3.0 保留的核心能力：

- ✅ 固定槽位注册表 (QM+AB Registry / ASIL-CD Registry)
- ✅ memfd + SCM_RIGHTS 零拷贝文件描述符传递
- ✅ seqlock 无锁原子读写
- ✅ systemd socket activation 自动启动
- ✅ YAML 配置驱动的静态服务映射

**参考文档**: 
- 当前架构: `SERVICE_DISCOVERY_ARCHITECTURE.md` (v3.0)
- 历史版本: `archive/SERVICE_DISCOVERY_V2_FAST_DDS_DESIGN.md` (v2.0)

