# LightAP Communication Module (Com)

[English](README.md) | [中文](README_CN.md)

[![License](https://img.shields.io/badge/License-CC%20BY--NC%204.0-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![AUTOSAR](https://img.shields.io/badge/AUTOSAR-AP%20R24--11-green.svg)](https://www.autosar.org/)
[![Status](https://img.shields.io/badge/Status-Phase%204%20Active-yellow.svg)](#implementation-status)

> **AUTOSAR Adaptive Platform R24-11 Compliant Communication Middleware**  
> Zero-daemon service-oriented architecture with plugin-based transport bindings

**Version:** 3.1.0  
**Last Updated:** 2025-11-24  
**Architecture:** Zero-Daemon + Fixed Slot + Dual Registry + Plugin Bindings  
**Status:** Phase 3 Complete (iceoryx2), Phase 4 Active (DDS + AF_XDP 70%)

---

## 📋 Table of Contents

- [Overview](#overview)
- [Revolutionary Architecture](#revolutionary-architecture)
- [Transport Bindings](#transport-bindings)
- [Implementation Status](#implementation-status)
- [Quick Start](#quick-start)
- [Documentation](#documentation)
- [License](#license)

---

## Overview

LightAP Com is an **AUTOSAR Adaptive Platform R24-11** compliant communication module implementing a revolutionary **zero-daemon, plugin-based architecture**.

### Core Innovations

🚀 **Zero-Daemon Service Discovery (<500ns)**
- No RouDi, no systemd-resolved, no background processes
- Fixed slot mapping: `slot = service_id & 1023` (zero hash collisions)
- Dual registry: QM+AB Registry + ASIL-CD Registry (FuSa isolation)
- Slot 0 protected (error boundary), Slot 1023 broadcast (cross-ASIL)

🔌 **Plugin-Based Bindings (Runtime .so Loading)**
- ITransportBinding interface (18 methods, AUTOSAR standard)
- Priority-based selection (100 → 50 → 20 → 10)
- YAML configuration (zero code changes)

🏗️ **Dual-Layer IDL (Franca → AUTOSAR + DDS)**
- Franca IDL as Single Source of Truth
- PyFranca → `ara::com` API + DDS IDL auto-gen
- Schema Hash + TypeIdentifier enforcement

### Transport Bindings

| Binding | Priority | Latency | Throughput | Status | Use Case |
|---------|----------|---------|------------|--------|----------|
| **iceoryx2** | 100 | <1µs | >10GB/s | ✅ Complete | Camera, LiDAR |
| **DDS (AF_XDP)** | 50 | ~10µs | ~1GB/s | 🔄 70% | Cross-ECU RT |
| **Custom Protocol** | 20 | <10µs | ~500MB/s | 📋 Planned | Legacy IPC |
| **Legacy Gateway** | 10 | ~50µs | ~100MB/s | 📋 Planned | SOME/IP bridge |

---

## Revolutionary Architecture

### Zero-Daemon Fixed Slot Service Discovery

**Traditional (iceoryx v1):** App → RouDi Daemon → Registry → SHM (1-5µs, SPOF)  
**LightAP (iceoryx2):** App → Direct memfd → seqlock O(1) (<500ns, no SPOF)

**Core Mechanisms:**

1. **Fixed Slot Mapping** (Zero Collisions)
   ```cpp
   uint16_t slot = service_id & 0x03FF;  // Slots 0-1023
   
   // Slot 0: FORBIDDEN (error detection)
   // Slots 1-1022: Services
   // Slot 1023: Broadcast (service_id 0xFFFF)
   ```

2. **Dual Registry** (FuSa Physical Isolation)
   ```
   QM+AB Registry (/dev/shm/lap_com_registry_qm, 256KB):
     service_id 0x0001~0x03FE → slots 1~1022
     Permission: 0666 (all R/W)
     Safety: QM / ASIL-A / ASIL-B
   
   ASIL-CD Registry (/dev/shm/lap_com_registry_asil, 256KB):
     service_id 0xF001~0xF3FE → slots 1~1022
     Permission: 0640 (controlled write)
     Safety: ASIL-C / ASIL-D
   
   Broadcast: Both have slot 1023 for cross-ASIL events
   ```

3. **seqlock Lock-Free Concurrency**
   ```cpp
   struct ServiceSlot {  // 256 bytes (4× cache-line)
       atomic<uint64_t> seq_num;   // seqlock
       uint16_t service_id;
       uint32_t heartbeat_timestamp;
       // ...
   };
   
   // Read (<100ns):
   do {
       seq1 = slot->seq_num.load();
       data = *slot;
       seq2 = slot->seq_num.load();
   } while (seq1 != seq2 || seq1 & 1);
   
   // Write (atomic):
   slot->seq_num++;  // Odd (mark start)
   *slot = new_data;
   slot->seq_num++;  // Even (mark end)
   ```

4. **Heartbeat** (Service Liveness, 100ms interval)

### Plugin Architecture

```
Application (Pure AUTOSAR ara::com):
  auto proxy = MyService::Proxy::CreateProxy(handle);
  proxy->Method(...);  // No binding knowledge

Binding Manager (YAML-Driven):
  Read binding_config.yaml
  → dlopen("binding_iceoryx2.so") priority 100
  → dlopen("binding_dds.so") priority 50
  → Select best for service (local→iceoryx2, remote→DDS)
```

**ITransportBinding Interface:**
```cpp
class ITransportBinding {
public:
    virtual Result<void> Initialize(const YAML::Node&) = 0;
    virtual Result<ServiceHandleContainer> FindService(...) = 0;
    virtual Result<void> OfferService(...) = 0;
    virtual Result<ByteBuffer> CallMethod(...) = 0;
    virtual Result<void> SendEvent(...) = 0;
    virtual Result<void> SubscribeEvent(...) = 0;
    // + 12 more methods
};

extern "C" {
    ITransportBinding* CreateBindingInstance();
    void DestroyBindingInstance(ITransportBinding*);
}
```

---

## Transport Bindings

### 1. iceoryx2 Binding ✅ Complete (Phase 3)

**Performance:** <1µs latency, >10GB/s throughput  
**Status:** 100% (3/3 tests passed, 414ms total)

**Features:**
- Zero-daemon (no RouDi), self-managing
- True zero-copy (shared memory)
- Lock-free Rust queues
- 256-byte slot alignment

**Test Results (2025-11-23):**
```
✓ DirectBindingCreation: 2ms
✓ CompletePubSubFlow: 204ms (10 msgs verified)
✓ PerformanceMetrics: 207ms (20 msgs, <1µs)
```

**Config:**
```yaml
bindings:
  - type: iceoryx2
    library: /usr/lib/lap/com/binding_iceoryx2.so
    priority: 100
    enabled: true
    config:
      domain_name: lightap_com
      use_huge_pages: true
```

### 2. DDS Binding (AF_XDP) 🔄 70% (Phase 4)

**Performance:** ~10µs latency (target), ~1GB/s  
**Status:** Week 1/2 (FastDDS integration done, AF_XDP pending)

**Completed:**
- ✅ FastDDS 2.9.1 API wrapped
- ✅ IDL gen (LapComMessage.idl)
- ✅ Publisher/Subscriber
- ✅ Discovery (local + remote)

**Pending:**
- 🔄 Compilation verification
- ⏳ Cross-process tests
- ⏳ AF_XDP integration (Week 2)

**Config (AF_XDP planned):**
```yaml
bindings:
  - type: dds
    library: /usr/lib/lap/com/binding_dds.so
    priority: 50
    enabled: true
    config:
      domain: 0
      af_xdp_enabled: true
      af_xdp_interface: eth0
      zero_copy: true
```

### 3. Custom Protocol Binding 📋 (Phase 5)

**Design:** UDS + custom binary codec, <10µs, ~500MB/s

### 4. Legacy Gateway Binding 📋 (Phase 5)

**Design:** SOME/IP/D-Bus gateway, ~50µs, ~100MB/s

---

## Implementation Status

| Phase | Status | Done | Date | Doc |
|-------|--------|------|------|-----|
| **Phase 1** | ✅ | 100% | 2025-11-20 | [IMPLEMENTATION_STATUS.md](doc/reports/IMPLEMENTATION_STATUS.md) |
| **Phase 2** | ✅ | 100% | 2025-11-21 | [PHASE2_COMPLETE_FEATURES.md](doc/reports/PHASE2_COMPLETE_FEATURES.md) |
| **Phase 3** | ✅ | 100% | 2025-11-23 | [BINDING_STANDARDIZATION_STATUS.md](doc/reports/BINDING_STANDARDIZATION_STATUS.md) |
| **Phase 4** | 🔄 | 70% | 2025-11-24+ | [PHASE4_DDS_IMPLEMENTATION_STATUS.md](doc/reports/PHASE4_DDS_IMPLEMENTATION_STATUS.md) |
| **Phase 5** | 📋 | 0% | TBD | - |

**Tests:** 30/30 passed (100% for completed phases)

**Phase 1:** Fixed slot registry (memfd, seqlock, heartbeat)  
**Phase 2:** Binding Manager (ITransportBinding, dlopen, YAML)  
**Phase 3:** iceoryx2 (C FFI, zero-copy, <1µs verified)  
**Phase 4:** DDS + AF_XDP (FastDDS ✅, AF_XDP ⏳)  
**Phase 5:** Custom + Legacy bindings

---

## Quick Start

### Prerequisites

```bash
# Ubuntu 22.04+
sudo apt install build-essential cmake \
    libboost-all-dev nlohmann-json3-dev libyaml-cpp-dev

# iceoryx2
cargo install iceoryx2-cli

# FastDDS 2.9.1
sudo apt install libfastrtps-dev fastddsgen
```

### Build

```bash
git clone https://github.com/TreeNeeBee/LightAP.git
cd LightAP
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc) Com
ctest -R Com --verbose
```

### Example

```cpp
#include <lap/com/ara_com.hpp>
using namespace lap::com;

// Server
class MyServiceImpl : public MyServiceSkeleton {
public:
    MyServiceImpl() : MyServiceSkeleton(InstanceSpecifier("/services/MyService")) {}
    Int32 Add(Int32 a, Int32 b) override { return a + b; }
};

int main() {
    Runtime::Initialize();
    MyServiceImpl service;
    service.OfferService();  // Fixed slot registration
    // Event loop...
}

// Client
int main() {
    Runtime::Initialize();
    auto handles = FindService<MyService>(InstanceSpecifier("/services/MyService"));
    auto proxy = MyService::Proxy::CreateProxy(handles.Value()[0]);
    auto result = proxy->Add(10, 20);  // Binding auto-selected
    std::cout << result.Value() << std::endl;
}
```

**Config (binding_config.yaml):**
```yaml
bindings:
  - type: iceoryx2
    priority: 100
    enabled: true
  - type: dds
    priority: 50
    enabled: true
```

Application code **never changes** - pure configuration-driven.

---

## Documentation

### Architecture

- **[SERVICE_DISCOVERY_ARCHITECTURE.md](doc/architecture/SERVICE_DISCOVERY_ARCHITECTURE.md)** (7253 lines) ⭐  
  Zero-daemon, fixed slot, dual registry, seqlock, heartbeat

- **[ARCHITECTURE_SUMMARY.md](doc/architecture/ARCHITECTURE_SUMMARY.md)** (3547 lines)  
  Complete architecture, 4-binding design, AUTOSAR R24-11

- **[TRANSPORT_MATRIX.md](doc/architecture/TRANSPORT_MATRIX.md)**  
  Binding selection guide

### Planning

- **[IMPLEMENTATION_PLAN_UPDATED.md](doc/planning/IMPLEMENTATION_PLAN_UPDATED.md)** (911 lines)  
  Phase roadmap, AUTOSAR requirements

### Reports

- **[IMPLEMENTATION_STATUS.md](doc/reports/IMPLEMENTATION_STATUS.md)** - Phase 1
- **[PHASE4_DDS_IMPLEMENTATION_STATUS.md](doc/reports/PHASE4_DDS_IMPLEMENTATION_STATUS.md)** - Current
- **[BINDING_STANDARDIZATION_STATUS.md](doc/reports/BINDING_STANDARDIZATION_STATUS.md)** - Phase 3

### Guides

- **[BINDING_SELECTION_GUIDE.md](doc/guides/BINDING_SELECTION_GUIDE.md)**
- **[ICEORYX2_INTEGRATION_GUIDE.md](doc/guides/ICEORYX2_INTEGRATION_GUIDE.md)**
- **[DDS_INTEGRATION_GUIDE.md](doc/guides/DDS_INTEGRATION_GUIDE.md)**
- **[AUTOSAR_QUICK_REFERENCE.md](doc/guides/AUTOSAR_QUICK_REFERENCE.md)**

---

## License

**CC BY-NC 4.0** (Creative Commons Attribution-NonCommercial 4.0)

✅ Permitted: Education, personal projects, modification (with attribution)  
❌ Prohibited: Commercial use, production deployment

For commercial licensing: <https://github.com/TreeNeeBee/LightAP>

---

## Contact

**Project:** LightAP Communication Module  
**Repository:** <https://github.com/TreeNeeBee/LightAP>  
**Issues:** <https://github.com/TreeNeeBee/LightAP/issues>

---

## Acknowledgments

- AUTOSAR Consortium (AP R24-11)
- Eclipse iceoryx2 (zero-daemon IPC)
- eProsima FastDDS (DDS-RTPS)
- COVESA (vsomeip)

---

<p align="center">
  <strong>Zero-Daemon • Plugin-Based • AUTOSAR R24-11</strong><br>
  <sub>Built for the Adaptive Platform community • CC BY-NC 4.0</sub>
</p>
