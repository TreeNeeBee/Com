# Com模块详细实施路线图

**文档版本**: 2.0 (v3.1 架构)  
**创建日期**: 2025-11-24  
**配套文档**: IMPLEMENTATION_PLAN_UPDATED.md  
**架构版本**: SERVICE_DISCOVERY_ARCHITECTURE.md v3.1  

---

## 目录

1. [Phase 1详细分解: 零守护进程服务发现](#phase-1详细分解)
2. [Phase 2详细分解: Binding Manager](#phase-2详细分解)
3. [Phase 3详细分解: iceoryx2 Binding](#phase-3详细分解)
4. [代码模板与最佳实践](#代码模板与最佳实践)
5. [调试与故障排查指南](#调试与故障排查指南)

---

## Phase 1详细分解

### Week 1: Day 1-2 - SlotAllocator基础实现

#### 任务目标
实现固定槽位映射机制，支持编译期哈希和运行时YAML配置。

#### 详细任务清单

**Day 1上午**: 头文件设计
```bash
# 创建文件
$ touch modules/Com/source/inc/SlotAllocator.hpp
$ touch modules/Com/test/unit/test_slot_allocator.cpp
```

**SlotAllocator.hpp 核心实现**:
```cpp
#ifndef LAP_COM_SLOT_ALLOCATOR_HPP
#define LAP_COM_SLOT_ALLOCATOR_HPP

#include <core/CResult.hpp>
#include <unordered_map>
#include <cstdint>

namespace lap::com {

class SlotAllocator {
public:
    // 编译期槽位计算（constexpr，零开销）
    static constexpr uint16_t ComputeSlot(uint16_t service_id, uint16_t instance_id) {
        // FNV-1a哈希变体（避免冲突）
        uint32_t hash = 2166136261u;
        hash ^= service_id;
        hash *= 16777619u;
        hash ^= instance_id;
        hash *= 16777619u;
        return static_cast<uint16_t>(hash % kTotalSlots);
    }
    
    // 运行时槽位查询（从YAML静态配置加载）
    Result<uint16_t> GetSlot(uint16_t service_id, uint16_t instance_id) const;
    
    // 从YAML加载静态映射
    Result<void> LoadStaticMapping(const std::string& yaml_file);
    
    // 槽位范围检查
    static constexpr bool IsQmSlot(uint16_t slot_id) {
        return slot_id < kQmSlotEnd;
    }
    
    static constexpr bool IsAsilDSlot(uint16_t slot_id) {
        return slot_id >= kAsilDSlotStart && slot_id < kTotalSlots;
    }
    
    // 槽位预留（防止冲突）
    Result<void> ReserveSlot(uint16_t slot_id, uint16_t service_id, uint16_t instance_id);
    
    // 检查槽位是否可用
    bool IsSlotAvailable(uint16_t slot_id) const;
    
private:
    static constexpr uint16_t kQmSlotEnd = 924;
    static constexpr uint16_t kAsilDSlotStart = 924;
    static constexpr uint16_t kTotalSlots = 1024;
    
    // 静态映射表（服务ID+实例ID → 槽位ID）
    std::unordered_map<uint32_t, uint16_t> static_mapping_;
    
    // 反向映射（槽位ID → 服务信息）
    std::unordered_map<uint16_t, std::pair<uint16_t, uint16_t>> slot_owners_;
    
    // 组合键生成
    static constexpr uint32_t MakeKey(uint16_t service_id, uint16_t instance_id) {
        return (static_cast<uint32_t>(service_id) << 16) | instance_id;
    }
};

} // namespace lap::com

#endif
```

**Day 1下午**: 单元测试设计
```cpp
// test_slot_allocator.cpp
#include <gtest/gtest.h>
#include <inc/SlotAllocator.hpp>

using namespace lap::com;

// 测试1: 编译期哈希计算
TEST(SlotAllocatorTest, CompileTimeHashing) {
    // constexpr验证（编译期计算）
    constexpr uint16_t slot1 = SlotAllocator::ComputeSlot(0x1234, 0x0001);
    constexpr uint16_t slot2 = SlotAllocator::ComputeSlot(0x5678, 0x0002);
    
    // 确保在有效范围内
    EXPECT_LT(slot1, 1024);
    EXPECT_LT(slot2, 1024);
    
    // 不同服务应该映射到不同槽位（大概率）
    EXPECT_NE(slot1, slot2);
}

// 测试2: QM/ASIL-D槽位范围
TEST(SlotAllocatorTest, SafetyLevelRanges) {
    EXPECT_TRUE(SlotAllocator::IsQmSlot(0));
    EXPECT_TRUE(SlotAllocator::IsQmSlot(923));
    EXPECT_FALSE(SlotAllocator::IsQmSlot(924));
    
    EXPECT_TRUE(SlotAllocator::IsAsilDSlot(924));
    EXPECT_TRUE(SlotAllocator::IsAsilDSlot(1023));
    EXPECT_FALSE(SlotAllocator::IsAsilDSlot(0));
}

// 测试3: 静态映射加载
TEST(SlotAllocatorTest, StaticMappingFromYAML) {
    SlotAllocator allocator;
    
    // 加载测试配置
    auto result = allocator.LoadStaticMapping("test_data/slot_mapping.yaml");
    ASSERT_TRUE(result.HasValue());
    
    // 验证映射
    auto slot = allocator.GetSlot(0x1234, 0x0001);
    ASSERT_TRUE(slot.HasValue());
    EXPECT_EQ(slot.Value(), 42);  // 假设YAML中配置为42
}

// 测试4: 槽位冲突检测
TEST(SlotAllocatorTest, SlotConflictDetection) {
    SlotAllocator allocator;
    
    // 预留槽位100
    auto reserve1 = allocator.ReserveSlot(100, 0x1234, 0x0001);
    EXPECT_TRUE(reserve1.HasValue());
    
    // 尝试重复预留（应该失败）
    auto reserve2 = allocator.ReserveSlot(100, 0x5678, 0x0002);
    EXPECT_FALSE(reserve2.HasValue());
    EXPECT_EQ(reserve2.Error().Value(), static_cast<int>(ComErrc::kSlotAlreadyReserved));
}
```

**Day 2**: 实现与集成
- [ ] 实现`SlotAllocator.cpp`（YAML解析逻辑）
- [ ] 集成yaml-cpp库
- [ ] 运行单元测试，覆盖率 > 90%

**交付物检查清单**:
- [x] `SlotAllocator.hpp` (150行)
- [x] `SlotAllocator.cpp` (200行)
- [x] `test_slot_allocator.cpp` (150行)
- [ ] 测试通过率 100%
- [ ] 代码审查通过

---

### Week 1: Day 3-5 - SharedMemoryRegistry实现

#### 任务目标
实现共享内存服务注册表，支持seqlock无锁并发访问。

#### 详细任务清单

**Day 3**: seqlock机制实现
```cpp
// ServiceSlotEntry中的seqlock使用示例
struct ServiceSlotEntry {
    // ... 其他字段 ...
    std::atomic<uint64_t> seqlock{0};
    
    // 写入操作（服务注册/更新）
    void WriteData(uint16_t sid, uint16_t iid, uint8_t binding) {
        BeginWrite();  // seqlock++（变为奇数）
        
        service_id = sid;
        instance_id = iid;
        binding_type = binding;
        timestamp_us = GetCurrentTimeMicros();
        
        EndWrite();  // seqlock++（变为偶数）
    }
    
    // 读取操作（服务查找）
    bool ReadData(uint16_t& sid, uint16_t& iid) const {
        uint64_t seq1, seq2;
        do {
            seq1 = seqlock.load(std::memory_order_acquire);
            if (seq1 & 1) {
                // 正在写入，让出CPU
                std::this_thread::yield();
                continue;
            }
            
            // 读取数据
            std::atomic_thread_fence(std::memory_order_acquire);
            sid = service_id;
            iid = instance_id;
            std::atomic_thread_fence(std::memory_order_acquire);
            
            seq2 = seqlock.load(std::memory_order_acquire);
        } while (seq1 != seq2);  // 版本不一致则重试
        
        return state.load() == static_cast<uint32_t>(State::kActive);
    }
};
```

**Day 4**: 共享内存创建与映射
```cpp
// SharedMemoryRegistry.cpp核心实现
Result<void> SharedMemoryRegistry::Initialize(const std::string& shm_name) {
    // 1. 创建或打开共享内存
    shm_fd_ = shm_open(shm_name.c_str(), 
                       O_CREAT | O_RDWR, 
                       S_IRUSR | S_IWUSR);
    if (shm_fd_ == -1) {
        return MakeError(ComErrc::kSharedMemoryCreationFailed);
    }
    
    // 2. 设置大小（1024槽位 × 96字节）
    constexpr size_t total_size = 1024 * sizeof(ServiceSlotEntry);
    if (ftruncate(shm_fd_, total_size) == -1) {
        close(shm_fd_);
        return MakeError(ComErrc::kSharedMemoryResizeFailed);
    }
    
    // 3. 映射到进程地址空间
    slots_ = static_cast<ServiceSlotEntry*>(
        mmap(nullptr, total_size, 
             PROT_READ | PROT_WRITE, 
             MAP_SHARED, 
             shm_fd_, 0)
    );
    
    if (slots_ == MAP_FAILED) {
        close(shm_fd_);
        return MakeError(ComErrc::kSharedMemoryMappingFailed);
    }
    
    // 4. 初始化所有槽位（仅第一次创建时）
    for (size_t i = 0; i < 1024; ++i) {
        slots_[i].state.store(static_cast<uint32_t>(ServiceSlotEntry::State::kInactive),
                              std::memory_order_release);
    }
    
    // 5. 启动心跳守护线程
    running_.store(true);
    heartbeat_thread_ = std::thread(&SharedMemoryRegistry::heartbeatDaemon, this);
    
    return Result<void>::Ok();
}
```

**Day 5**: 服务注册/查找实现
```cpp
Result<void> SharedMemoryRegistry::RegisterService(
    uint16_t service_id,
    uint16_t instance_id,
    uint8_t binding_type,
    uint64_t binding_handle) 
{
    // 1. 计算槽位ID
    uint16_t slot_id = slot_allocator_.ComputeSlot(service_id, instance_id);
    
    // 2. 检查槽位是否已占用
    auto& slot = slots_[slot_id];
    auto current_state = slot.state.load(std::memory_order_acquire);
    
    if (current_state == static_cast<uint32_t>(ServiceSlotEntry::State::kActive)) {
        // 槽位冲突检测
        if (slot.service_id != service_id || slot.instance_id != instance_id) {
            return MakeError(ComErrc::kSlotConflict);
        }
    }
    
    // 3. 写入槽位（使用seqlock）
    slot.BeginWrite();
    slot.service_id = service_id;
    slot.instance_id = instance_id;
    slot.process_id = getpid();
    slot.binding_type = binding_type;
    slot.binding_handle = binding_handle;
    slot.timestamp_us = GetCurrentTimeMicros();
    slot.heartbeat_us = slot.timestamp_us;
    slot.state.store(static_cast<uint32_t>(ServiceSlotEntry::State::kActive),
                     std::memory_order_release);
    slot.EndWrite();
    
    return Result<void>::Ok();
}

Result<ServiceSlotEntry> SharedMemoryRegistry::FindService(
    uint16_t service_id, 
    uint16_t instance_id) 
{
    // 1. O(1)槽位计算
    uint16_t slot_id = slot_allocator_.ComputeSlot(service_id, instance_id);
    
    // 2. seqlock无锁读取（< 100ns）
    const auto& slot = slots_[slot_id];
    
    ServiceSlotEntry result;
    bool valid = slot.Read([&](const ServiceSlotEntry& entry) {
        result = entry;  // 复制数据
    });
    
    // 3. 验证数据
    if (!valid || result.state.load() != static_cast<uint32_t>(ServiceSlotEntry::State::kActive)) {
        return MakeError(ComErrc::kServiceNotAvailable);
    }
    
    if (result.service_id != service_id || result.instance_id != instance_id) {
        return MakeError(ComErrc::kSlotConflict);
    }
    
    return result;
}
```

**交付物检查清单**:
- [ ] `SharedMemoryRegistry.hpp` (300行)
- [ ] `SharedMemoryRegistry.cpp` (500行)
- [ ] `test_shared_memory_registry.cpp` (400行)
- [ ] 并发测试（10线程读/写）通过
- [ ] 性能测试（FindService < 500ns）通过

---

### Week 2: 服务注册与心跳机制

#### 心跳守护线程实现

```cpp
void SharedMemoryRegistry::heartbeatDaemon() {
    while (running_.load(std::memory_order_acquire)) {
        // 1. 遍历所有槽位
        for (size_t i = 0; i < 1024; ++i) {
            auto& slot = slots_[i];
            
            auto state = slot.state.load(std::memory_order_acquire);
            if (state != static_cast<uint32_t>(ServiceSlotEntry::State::kActive)) {
                continue;  // 跳过非活跃槽位
            }
            
            // 2. 检查心跳超时（5秒）
            uint64_t now = GetCurrentTimeMicros();
            uint64_t last_heartbeat = slot.heartbeat_us.load(std::memory_order_acquire);
            
            if (now - last_heartbeat > 5000000) {  // 5秒超时
                // 3. 标记为僵尸服务
                slot.BeginWrite();
                slot.state.store(static_cast<uint32_t>(ServiceSlotEntry::State::kInactive),
                                 std::memory_order_release);
                slot.EndWrite();
                
                // 记录日志
                LOG_WARN("Service timeout: sid={}, iid={}, pid={}",
                         slot.service_id, slot.instance_id, slot.process_id);
            }
        }
        
        // 4. 每秒检查一次
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// 服务提供者调用（每秒一次）
Result<void> SharedMemoryRegistry::UpdateHeartbeat(
    uint16_t service_id, 
    uint16_t instance_id) 
{
    uint16_t slot_id = slot_allocator_.ComputeSlot(service_id, instance_id);
    auto& slot = slots_[slot_id];
    
    // 无锁更新心跳时间戳
    slot.heartbeat_us.store(GetCurrentTimeMicros(), std::memory_order_release);
    
    return Result<void>::Ok();
}
```

---

## Phase 2详细分解

### Week 1: Binding Manager核心框架

#### BindingManager动态加载实现

```cpp
Result<void> BindingManager::LoadBinding(const BindingConfig& config) {
    // 1. dlopen加载动态库
    void* handle = dlopen(config.library_path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        const char* error = dlerror();
        LOG_ERROR("Failed to load binding: {}, error: {}", config.type, error);
        return MakeError(ComErrc::kBindingLoadFailed);
    }
    
    // 2. 获取工厂函数
    auto create_func = reinterpret_cast<CreateBindingFunc>(
        dlsym(handle, "CreateBindingInstance"));
    
    if (!create_func) {
        dlclose(handle);
        return MakeError(ComErrc::kBindingSymbolNotFound);
    }
    
    // 3. 创建Binding实例
    ITransportBinding* binding = create_func();
    if (!binding) {
        dlclose(handle);
        return MakeError(ComErrc::kBindingCreationFailed);
    }
    
    // 4. 初始化Binding
    auto init_result = binding->Initialize(config.config);
    if (!init_result.HasValue()) {
        auto destroy_func = reinterpret_cast<DestroyBindingFunc>(
            dlsym(handle, "DestroyBindingInstance"));
        if (destroy_func) destroy_func(binding);
        dlclose(handle);
        return init_result.Error();
    }
    
    // 5. 注册到管理器
    bindings_.emplace(config.priority, binding);
    binding_by_name_[config.type] = binding;
    library_handles_[config.type] = handle;
    
    LOG_INFO("Binding loaded: name={}, priority={}", config.type, config.priority);
    return Result<void>::Ok();
}

// 优先级选择算法
ITransportBinding* BindingManager::SelectBinding(
    uint16_t service_id, 
    uint16_t instance_id) 
{
    // 1. 检查静态配置
    auto static_binding = getStaticBinding(service_id, instance_id);
    if (static_binding) {
        return static_binding;
    }
    
    // 2. 按优先级选择（multimap已排序，从高到低）
    for (auto& [priority, binding] : bindings_) {
        // 检查Binding是否支持该服务
        // （这里可以添加更复杂的选择逻辑）
        return binding;
    }
    
    return nullptr;
}
```

---

## 代码模板与最佳实践

### 1. Binding插件模板

**文件**: `source/binding/template/TemplateBinding.cpp`

```cpp
#include "TemplateBinding.hpp"
#include <log/CLogger.hpp>

namespace lap::com::binding {

// ===== 插件生命周期 =====
Result<void> TemplateBinding::Initialize(const YAML::Node& config) {
    LOG_INFO("Initializing TemplateBinding");
    
    // 解析配置
    if (config["custom_param"]) {
        custom_param_ = config["custom_param"].as<std::string>();
    }
    
    // 初始化底层传输
    // ...
    
    return Result<void>::Ok();
}

Result<void> TemplateBinding::Shutdown() {
    LOG_INFO("Shutting down TemplateBinding");
    // 清理资源
    return Result<void>::Ok();
}

// ===== 服务管理 =====
Result<void> TemplateBinding::OfferService(
    uint16_t service_id, 
    uint16_t instance_id) 
{
    // 实现服务提供逻辑
    LOG_DEBUG("OfferService: sid={:#x}, iid={:#x}", service_id, instance_id);
    return Result<void>::Ok();
}

// ===== 通信原语 =====
Result<void> TemplateBinding::SendEvent(
    uint16_t service_id,
    uint16_t instance_id,
    uint16_t event_id,
    const ByteBuffer& data) 
{
    // 发送事件
    LOG_TRACE("SendEvent: size={} bytes", data.size());
    return Result<void>::Ok();
}

// ===== C导出符号 =====
extern "C" {
    ITransportBinding* CreateBindingInstance() {
        return new TemplateBinding();
    }
    
    void DestroyBindingInstance(ITransportBinding* instance) {
        delete instance;
    }
    
    const char* GetBindingName() {
        return "template";
    }
    
    uint32_t GetBindingVersion() {
        return 0x00010000;  // 1.0.0
    }
}

} // namespace lap::com::binding
```

### 2. CMakeLists.txt插件编译模板

```cmake
# binding_template.so
add_library(binding_template SHARED
    TemplateBinding.cpp
    TemplateBinding.hpp
)

target_include_directories(binding_template PRIVATE
    ${MODULE_SOURCE_DIR}/inc
)

target_link_libraries(binding_template PRIVATE
    lap_core
    lap_log
    yaml-cpp
)

# 安装到标准路径
install(TARGETS binding_template
    LIBRARY DESTINATION /usr/lib/lap/com
)
```

---

## 调试与故障排查指南

### 1. 共享内存调试

#### 查看共享内存状态
```bash
# 列出所有共享内存段
$ ipcs -m

# 查看特定共享内存
$ ls -lh /dev/shm/lightap_com_registry

# 删除僵尸共享内存
$ rm /dev/shm/lightap_com_registry
```

#### GDB调试共享内存
```gdb
# 打印槽位数组
(gdb) p slots_[0]
(gdb) p slots_[0].service_id
(gdb) p slots_[0].seqlock

# 遍历所有活跃槽位
(gdb) p/x slots_[0]@1024 | grep state=1
```

### 2. seqlock并发问题调试

#### ThreadSanitizer检测数据竞争
```bash
# 编译时启用TSan
$ cmake -DCMAKE_CXX_FLAGS="-fsanitize=thread" ..
$ make
$ ./test_shared_memory_registry
```

#### 手动验证seqlock正确性
```cpp
// 测试代码
void TestSeqlockConcurrency() {
    ServiceSlotEntry slot;
    
    // 线程1: 持续写入
    std::thread writer([&]() {
        for (int i = 0; i < 100000; ++i) {
            slot.WriteData(0x1234, 0x0001, 1);
        }
    });
    
    // 线程2-10: 持续读取
    std::vector<std::thread> readers;
    for (int i = 0; i < 9; ++i) {
        readers.emplace_back([&]() {
            uint16_t sid, iid;
            for (int j = 0; j < 100000; ++j) {
                bool ok = slot.ReadData(sid, iid);
                EXPECT_TRUE(ok);
                EXPECT_EQ(sid, 0x1234);
                EXPECT_EQ(iid, 0x0001);
            }
        });
    }
    
    writer.join();
    for (auto& r : readers) r.join();
}
```

### 3. Binding加载失败排查

#### dlopen错误诊断
```cpp
void* handle = dlopen("binding_iceoryx2.so", RTLD_LAZY);
if (!handle) {
    // 打印详细错误
    fprintf(stderr, "dlopen failed: %s\n", dlerror());
    
    // 检查库依赖
    system("ldd binding_iceoryx2.so");
    
    // 检查符号导出
    system("nm -D binding_iceoryx2.so | grep CreateBindingInstance");
}
```

#### 常见问题解决

| 错误 | 原因 | 解决方案 |
|------|------|---------|
| `undefined symbol` | 缺少依赖库 | 检查链接选项 `-l` |
| `cannot open shared object` | LD_LIBRARY_PATH未设置 | `export LD_LIBRARY_PATH=/usr/lib/lap/com` |
| `symbol not found` | 忘记`extern "C"` | 添加C导出 |

---

**文档维护**: LightAP Team  
**最后更新**: 2025-11-20

