# CoreIPCBinding TODO List

## Current Status (2025-01-XX)

### ✅ Completed
1. **Event Communication** (Priority: P0)
   - [x] SendEvent - 基于Core IPC Publisher
   - [x] SubscribeEvent - 基于Core IPC Subscriber
   - [x] Event listener thread with callback dispatch
   - [x] Zero-copy event delivery via Sample<T>
   - [x] Publisher/Subscriber lifecycle management

2. **ServiceRegistry Integration** (Priority: P0)
   - [x] Initialize/Shutdown with ServiceRegistry
   - [x] OfferService - RegisterService with shm_path as endpoint
   - [x] StopOfferService - UnregisterService
   - [x] FindService - Query registry to discover services
   - [x] Auto-routing based on service_id (QM/ASIL)

3. **Build System**
   - [x] CMakeLists.txt with ServiceRegistry dependencies
   - [x] Successful compilation with all dependencies resolved
   - [x] Export functions for dynamic loading (CreateBindingInstance/DestroyBindingInstance)

4. **Metrics & Monitoring**
   - [x] TransportMetrics tracking (events_sent, events_received, errors)
   - [x] Error handling with Result<T> pattern
   - [x] Comprehensive logging throughout

---

## 📋 TODO Items

### 1. Method Communication (Priority: P0)

**Why Blocked**: Core IPC 目前只提供 Publisher/Subscriber（单向Event通信），缺少 Request/Response 机制。

**Required Core IPC Features**:
- [ ] `RequestChannel` class - 发送Method请求，等待响应
- [ ] `ResponseChannel` class - 接收Method请求，发送响应
- [ ] Correlation ID mechanism - 匹配请求和响应
- [ ] Timeout handling - 支持同步/异步超时
- [ ] Error handling - 传递Method调用错误

**Implementation Steps**:
1. **Design Request/Response Protocol**:
   ```cpp
   struct MethodRequest {
       uint64_t correlation_id;    // 唯一请求ID
       uint64_t service_id;
       uint64_t instance_id;
       uint32_t method_id;
       uint32_t payload_size;
       uint8_t payload[];          // Variable-length data
   };

   struct MethodResponse {
       uint64_t correlation_id;    // 对应的请求ID
       uint8_t status;             // 0=success, 1=error
       uint32_t payload_size;
       uint8_t payload[];          // Response data or error info
   };
   ```

2. **Add Core IPC Request/Response Channels**:
   - In `modules/Core/source/inc/ipc/`:
     - `RequestChannel.hpp` - 客户端发送请求，等待响应
     - `ResponseChannel.hpp` - 服务端接收请求，发送响应
   - Based on dual Publisher/Subscriber pairs:
     - Client: Request Publisher + Response Subscriber
     - Server: Request Subscriber + Response Publisher

3. **Implement CoreIPCBinding Method Support**:
   ```cpp
   Result<void> CoreIPCBinding::RegisterMethod(
       uint64_t service_id, uint64_t instance_id,
       uint32_t method_id, MethodCallback callback) noexcept
   {
       // 1. Create ResponseChannel for this method
       // 2. Store method handler (callback)
       // 3. Start response listener thread
       // 4. Register in methods_ map
   }

   Result<ByteBuffer> CoreIPCBinding::InvokeMethod(
       uint64_t service_id, uint64_t instance_id,
       uint32_t method_id, const ByteBuffer& request_data) noexcept
   {
       // 1. Find service in ServiceRegistry
       // 2. Create RequestChannel to target service
       // 3. Generate correlation_id
       // 4. Send MethodRequest
       // 5. Wait for MethodResponse (with timeout)
       // 6. Return response data or error
   }
   ```

4. **Update ServiceRegistry Integration**:
   - Method endpoints需要两个shm_path:
     - `request_path` - 用于请求队列
     - `response_path` - 用于响应队列
   - 扩展ServiceSlot.endpoint字段格式:
     ```
     Format: "event:/path/to/event;method:/path/to/request,/path/to/response"
     ```

**Testing Requirements**:
- [ ] Unit tests for Request/Response channels
- [ ] Integration tests: Fire-and-forget, synchronous, asynchronous calls
- [ ] Timeout handling tests
- [ ] Error propagation tests
- [ ] Performance tests (latency, throughput)

---

### 2. Field Communication (Priority: P1)

**Why Blocked**: 需要实现Field的Getter/Setter/Notifier机制。

**Required Features**:
- [ ] Field value storage (server-side)
- [ ] Get/Set operations (基于Method Request/Response)
- [ ] Field change notification (基于Event)
- [ ] Field access control (read-only/read-write)

**Implementation Approach**:
```cpp
Result<ByteBuffer> CoreIPCBinding::GetField(
    uint64_t service_id, uint64_t instance_id, uint32_t field_id) noexcept
{
    // Implemented as Method call with special method_id
    // method_id = 0x80000000 | field_id (MSB=1表示Field Get)
    return InvokeMethod(service_id, instance_id, 
                        0x80000000 | field_id, ByteBuffer{});
}

Result<void> CoreIPCBinding::SetField(
    uint64_t service_id, uint64_t instance_id,
    uint32_t field_id, const ByteBuffer& data) noexcept
{
    // Implemented as Method call with special method_id
    // method_id = 0xC0000000 | field_id (MSB=11表示Field Set)
    auto result = InvokeMethod(service_id, instance_id, 
                               0xC0000000 | field_id, data);
    return result ? Result<void>::FromValue() 
                  : Result<void>::FromError(result.Error());
}
```

**Field Change Notification**:
- Field值变化时，通过Event机制广播
- Event ID = `0x40000000 | field_id` (MSB=01表示Field Update)
- Subscribers可以订阅Field变化通知

**Testing Requirements**:
- [ ] Get/Set field value tests
- [ ] Field change notification tests
- [ ] Concurrent access tests (thread safety)
- [ ] Field access control tests

---

### 3. EventID Protocol (Priority: P1)

**Why**: 标准化Event ID编码，支持Field/Method/Event区分。

**Current Format**: 
```
event_id = raw uint32_t (no structure)
```

**Proposed Format**:
```
Bits 31-30: Type (00=Event, 01=Field Update, 10=Method Request, 11=Method Response)
Bits 29-16: Reserved (14 bits)
Bits 15-0:  ID (16 bits)

Examples:
0x00001234 - Event ID 0x1234
0x40001234 - Field 0x1234 Update Notification
0x80001234 - Method 0x1234 Request
0xC0001234 - Method 0x1234 Response
```

**Implementation**:
- [ ] Define `EventIDProtocol.hpp` with encoding/decoding helpers
- [ ] Update SendEvent/SubscribeEvent to use structured IDs
- [ ] Update Method implementation to use structured request/response IDs

---

### 4. FuSa Memory Pool (Priority: P2)

**Why**: 安全关键应用需要QM/ASIL隔离的内存分配。

**Required Features**:
- [ ] Dual memory pools (QM Pool, ASIL Pool)
- [ ] Auto-select pool based on service_id (0x0001-0x0417→QM, 0xF001-0xF3FE→ASIL)
- [ ] Memory isolation enforcement
- [ ] Runtime integrity checks for ASIL pool

**Implementation**:
- [ ] Extend ChunkPoolAllocator with safety level parameter
- [ ] Add pool selection logic in CoreIPCBinding::OfferService()
- [ ] Integrate with ServiceRegistry routing

---

## Testing Strategy

### Unit Tests
- [ ] ServiceRegistry integration tests
- [ ] Publisher/Subscriber lifecycle tests
- [ ] Error handling tests
- [ ] Metrics tracking tests

### Integration Tests
- [ ] Multi-service communication tests
- [ ] Service discovery tests (FindService)
- [ ] Concurrent access tests
- [ ] Failure recovery tests

### Performance Tests
- [ ] Event throughput (events/sec)
- [ ] Event latency (microseconds)
- [ ] Memory usage (per service)
- [ ] CPU usage (under load)

---

## Known Issues

### 1. Listener Thread Lifecycle
**Issue**: Listener thread可能在Shutdown后仍尝试访问已销毁的对象。
**Solution**: 
- [ ] Add proper thread synchronization in Shutdown()
- [ ] Use condition variable to gracefully stop listener
- [ ] Ensure all callbacks complete before destroying subscribers

### 2. Error Propagation
**Issue**: 某些错误路径返回的ErrorCode可能不够精确。
**Solution**:
- [ ] Define CoreIPCBinding-specific error codes
- [ ] Map Core IPC errors to Com module errors
- [ ] Add error context information

### 3. Configuration Validation
**Issue**: BindingConfig参数未充分验证。
**Solution**:
- [ ] Add BindingConfig validation in Initialize()
- [ ] Document valid parameter ranges
- [ ] Add runtime parameter adjustment

---

## Dependencies

### From Core Module
- [x] Publisher/Subscriber (Event communication)
- [ ] RequestChannel/ResponseChannel (Method communication) - **MISSING**
- [x] SharedMemoryManager (SHM lifecycle)
- [x] ChunkPoolAllocator (Memory allocation)

### From Com Module
- [x] ServiceRegistry (Service discovery)
- [x] RegistryInitializer (Registry startup)
- [x] ITransportBinding interface
- [x] Result<T> error handling

### External Dependencies
- [x] lap_log (Logging)
- [x] pthread (Threading)
- [x] rt (POSIX shared memory)

---

## Documentation

- [ ] Update [CORE_IPC_INTERFACE_REQUIREMENTS.md](../../../CORE_IPC_INTERFACE_REQUIREMENTS.md)
- [ ] Create Method communication design document
- [ ] Create Field communication design document
- [ ] Add API usage examples
- [ ] Update [ARCHITECTURE_SUMMARY.md](../../../doc/architecture/ARCHITECTURE_SUMMARY.md)

---

## References

- **Core IPC Module**: `/workspace/LightAP/modules/Core/source/inc/ipc/`
- **ServiceRegistry**: `/workspace/LightAP/modules/Com/source/registry/`
- **Architecture Doc**: `/workspace/LightAP/modules/Com/doc/architecture/ARCHITECTURE_SUMMARY.md`
- **Requirements**: `/workspace/LightAP/modules/Com/CORE_IPC_INTERFACE_REQUIREMENTS.md`

---

**Last Updated**: 2025-01-XX
**Author**: GitHub Copilot
**Status**: Event communication完成，Method/Field待Core IPC支持
