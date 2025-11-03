# LightAP Com Module - Transport Matrix

## Supported Transports

LightAP Com module provides three transport options:

| Transport | Protocol | Scope | Code Style | AUTOSAR | Status |
|-----------|----------|-------|------------|---------|--------|
| **D-Bus** | IPC message bus | Intra-ECU | Manual / Generated | Partial | ✅ Production |
| **SOME/IP** | Service-oriented over IP | Inter-ECU | Generated | Full | ✅ Ready |
| **Signal (TBD)** | Shared memory | Intra-ECU | Manual | No | ⏳ Future |

---

## Quick Decision Guide

```
Need inter-ECU communication? 
├─ YES → Use SOME/IP (vsomeip + CommonAPI)
│        Best for: Ethernet, AUTOSAR AP, SOA, dynamic discovery
│
└─ NO (same ECU only)
   ├─ Need AUTOSAR compliance? 
   │  ├─ YES → Use CommonAPI-DBus
   │  │        Best for: Tool-generated, standardized, ARXML integration
   │  │
   │  └─ NO → Use Manual D-Bus Binding
   │           Best for: Quick prototyping, simple interfaces, full control
   │
   └─ Need ultra-low latency?
      └─ Use Signal Binding (future)
              Best for: Real-time, zero-copy, shared memory
```

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Code                         │
└─────────────────────────────────────────────────────────────┘
                          │
         ┌────────────────┼────────────────┐
         │                │                │
    ┌────▼─────┐   ┌──────▼──────┐   ┌────▼─────┐
    │ Manual   │   │  CommonAPI  │   │  SOME/IP │
    │ Binding  │   │   Adapter   │   │  Binding │
    └────┬─────┘   └──────┬──────┘   └────┬─────┘
         │                │                │
    ┌────▼─────┐   ┌──────▼──────┐   ┌────▼─────┐
    │ sdbus++  │   │ CommonAPI   │   │ vsomeip  │
    │          │   │ Runtime     │   │          │
    └────┬─────┘   └──────┬──────┘   └────┬─────┘
         │                │                │
         │         ┌──────┴──────┐         │
         │         │             │         │
    ┌────▼─────┐  ┌▼──────┐ ┌───▼────┐    │
    │  D-Bus   │  │ D-Bus │ │ SOME/IP│    │
    │  Daemon  │  │       │ │ over   │◄───┘
    │          │  │       │ │ TCP/UDP│
    └──────────┘  └───────┘ └────────┘
```

---

## Feature Comparison

### Performance Characteristics

| Metric | Manual D-Bus | CommonAPI-DBus | SOME/IP (UDP) | SOME/IP (TCP) |
|--------|--------------|----------------|---------------|---------------|
| **Latency** | ~50-100 µs | ~100-200 µs | ~200-500 µs | ~500-1000 µs |
| **Throughput** | ~100 MB/s | ~80 MB/s | ~900 MB/s | ~800 MB/s |
| **Overhead** | Low | Medium | Low | Medium |
| **Setup Time** | Instant | 100ms | 1-3s (SD) | 1-3s (SD) |

*Note: Measurements are approximate and depend on hardware, network, and configuration.*

### Protocol Features

| Feature | D-Bus | SOME/IP |
|---------|-------|---------|
| Service Discovery | Static (dbus-daemon) | Dynamic (SD protocol) |
| QoS Support | No | Yes (UDP/TCP selection) |
| Multicast | No | Yes (UDP) |
| Fragmentation | No | Yes (configurable) |
| Compression | No | Optional |
| Encryption | PolicyKit | TLS, payload encryption |
| Max Message Size | ~128 MB | Configurable |
| Serialization | D-Bus wire format | SOME/IP wire format |

### Development Features

| Feature | Manual Binding | CommonAPI |
|---------|----------------|-----------|
| Code Generation | No | Yes (from .fidl) |
| Type Safety | Manual | Automatic |
| ARXML Support | No | Yes (via Franca) |
| Debugging | Direct API | Generated abstraction |
| Maintainability | Manual sync | Tool-assisted |
| Learning Curve | Easy | Medium |

---

## Directory Structure

```
modules/Com/
├── source/
│   ├── binding/
│   │   ├── dbus/                     # Manual D-Bus binding
│   │   │   ├── DBusEventBinding.hpp
│   │   │   ├── DBusMethodBinding.hpp
│   │   │   ├── DBusFieldBinding.hpp
│   │   │   └── DBusConnectionManager.hpp
│   │   ├── someip/                   # SOME/IP binding
│   │   │   └── SomeIpConnectionManager.hpp
│   │   ├── commonapi/                # CommonAPI adapters
│   │   │   ├── CommonAPIAdapter.hpp         (D-Bus)
│   │   │   └── CommonAPISomeIpAdapter.hpp   (SOME/IP)
│   │   └── README.md                 # This file
│   └── inc/
│       └── ComTypes.hpp              # Common error codes
├── tools/
│   ├── commonapi/                    # Code generation
│   │   ├── generate_new.sh           # Multi-transport generator
│   │   ├── generators/               # Generator binaries
│   │   └── README.md
│   ├── fidl/                         # Franca IDL definitions
│   │   └── examples/
│   │       ├── *.fidl                # Interface definitions
│   │       └── *.fdepl               # SOME/IP deployment
│   ├── someip/                       # vsomeip configuration
│   │   ├── vsomeip_*.json            # Service configs
│   │   └── README.md                 # SOME/IP guide
│   └── NEXT_STEPS.md                 # Getting started
└── test/
    └── examples/
        ├── dbus/                     # Manual D-Bus examples
        ├── commonapi/                # CommonAPI-DBus examples
        └── someip/                   # SOME/IP examples
```

---

## Example Mapping

Same `Calculator` service implemented in different ways:

| Example | Files | Transport | Lines of Code | Use Case |
|---------|-------|-----------|---------------|----------|
| **Manual D-Bus** | `method_server.cpp`<br>`method_client.cpp` | D-Bus | ~150 | Quick prototype |
| **CommonAPI-DBus** | `calculator_server.cpp`<br>`calculator_client.cpp`<br>+ generated | D-Bus | ~200 + gen | AUTOSAR desktop |
| **SOME/IP** | `calculator_server.cpp`<br>`calculator_client.cpp`<br>+ generated<br>+ vsomeip.json | UDP/TCP | ~250 + gen | AUTOSAR automotive |

All examples share the same interface defined in `Calculator.fidl`.

---

## Migration Paths

### From Manual to CommonAPI (Same Transport)

**Goal:** Keep D-Bus transport, add code generation

1. Extract interface to `.fidl` file
2. Run `generate_new.sh <file>.fidl dbus`
3. Implement generated `*StubDefault` class
4. Use `CommonAPIAdapter` in place of manual binding
5. Test both implementations in parallel

**Effort:** Medium (need to learn Franca IDL)
**Benefit:** Standardization, tool-assisted development

### From D-Bus to SOME/IP

**Goal:** Change from intra-ECU to inter-ECU

1. Add `.fdepl` deployment specification
2. Run `generate_new.sh <file>.fidl someip`
3. Create vsomeip JSON configuration
4. Use `SomeIpProxyAdapter`/`SomeIpStubAdapter`
5. Update network configuration

**Effort:** High (network setup, vsomeip configuration)
**Benefit:** Ethernet scalability, dynamic discovery, AUTOSAR AP

### Dual Transport

**Goal:** Support both D-Bus and SOME/IP simultaneously

1. Generate code for both: `generate_new.sh <file>.fidl both`
2. Conditional compilation or runtime selection
3. Different service instances for each transport

**Effort:** Medium
**Benefit:** Flexibility, testing, gradual migration

---

## Getting Started

### New to LightAP Com?

**Start here:** Manual D-Bus binding
- See: `test/examples/dbus/`
- Read: `source/binding/dbus/DBusMethodBinding.hpp`
- Simple API, immediate results

### Building AUTOSAR-compliant system?

**Start here:** CommonAPI with Franca IDL
- See: `tools/NEXT_STEPS.md`
- Define interface in `.fidl`
- Generate code for D-Bus or SOME/IP

### Need inter-ECU communication?

**Start here:** SOME/IP integration
- See: `tools/someip/README.md`
- Install vsomeip
- Create `.fdepl` deployment
- Configure service discovery

---

## Best Practices

### Transport Selection

✅ **Do:**
- Use D-Bus for same-ECU, low-latency IPC
- Use SOME/IP for inter-ECU over Ethernet
- Use CommonAPI for AUTOSAR compliance
- Profile performance before choosing

❌ **Don't:**
- Use SOME/IP for local IPC (D-Bus is better)
- Use D-Bus for inter-ECU (not designed for network)
- Mix manual and generated code for same service

### Code Organization

✅ **Do:**
- Keep `.fidl` files version-controlled
- Separate interface definition from implementation
- Use adapters for LightAP integration
- Document transport-specific configuration

❌ **Don't:**
- Hard-code service IDs or instance IDs
- Share vsomeip config between incompatible services
- Ignore error handling from Result<T>

### Testing Strategy

1. **Unit Tests:** Mock transport layer
2. **Integration Tests:** Real D-Bus/vsomeip
3. **Performance Tests:** Measure latency/throughput
4. **Network Tests:** Simulate packet loss, latency (SOME/IP only)

---

## Dependencies

| Component | D-Bus | SOME/IP | Required |
|-----------|-------|---------|----------|
| **sdbus-c++** | ✅ | ❌ | Manual D-Bus |
| **CommonAPI Runtime** | ✅ | ✅ | CommonAPI |
| **CommonAPI-DBus Runtime** | ✅ | ❌ | CommonAPI D-Bus |
| **vsomeip** | ❌ | ✅ | SOME/IP |
| **CommonAPI-SomeIP Runtime** | ❌ | ✅ | CommonAPI SOME/IP |
| **Generators** | Optional | Optional | Code generation |

See individual READMEs for installation instructions.

---

## References

### Documentation
- Manual Binding: `source/binding/dbus/`
- SOME/IP Guide: `tools/someip/README.md`
- CommonAPI Setup: `tools/commonapi/README.md`
- Getting Started: `tools/NEXT_STEPS.md`

### Standards
- [D-Bus Specification](https://dbus.freedesktop.org/doc/dbus-specification.html)
- [SOME/IP Protocol](https://some-ip.com/)
- [AUTOSAR Adaptive Platform](https://www.autosar.org/standards/adaptive-platform/)
- [Franca IDL](https://franca.github.io/franca/)

### Open Source Projects
- [sdbus-c++](https://github.com/Kistler-Group/sdbus-cpp)
- [vsomeip](https://github.com/COVESA/vsomeip)
- [CommonAPI](https://github.com/COVESA/capicxx-core-runtime)

---

## FAQ

**Q: Can I use both D-Bus and SOME/IP in the same application?**
A: Yes! Create separate service instances with different adapters.

**Q: Which is faster, D-Bus or SOME/IP?**
A: D-Bus is faster for local IPC. SOME/IP is faster for network communication.

**Q: Do I need CommonAPI for SOME/IP?**
A: No, you can use `SomeIpConnectionManager` directly. CommonAPI provides standardization.

**Q: Can I convert D-Bus service to SOME/IP?**
A: Yes, if using CommonAPI. Just regenerate with SOME/IP deployment and update adapters.

**Q: What about security?**
A: D-Bus uses PolicyKit. SOME/IP supports TLS and payload encryption (configure in vsomeip).

---

**Ready to start?** See `tools/NEXT_STEPS.md` for step-by-step guide!
