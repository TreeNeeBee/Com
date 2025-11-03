# LightAP Com Binding Architecture

## Overview
The Com module supports **multiple approaches** for inter-process and inter-ECU communication:

### 1. Manual Binding (Direct API)
Low-level, flexible binding API for quick prototyping and simple use cases.

**Location**: `source/binding/dbus/`
- `DBusEventBinding.hpp` - Publish/subscribe events
- `DBusMethodBinding.hpp` - RPC methods
- `DBusFieldBinding.hpp` - Property access with notifications
- `DBusConnectionManager.hpp` - Connection lifecycle

**Transport**: D-Bus (intra-ECU)

**Use when**:
- Rapid prototyping
- Simple interfaces with few methods
- Full control over serialization
- No ARXML/tool-chain integration needed
- Same-ECU communication

### 2. SOME/IP Binding (vsomeip)
Direct vsomeip integration for SOME/IP protocol.

**Location**: `source/binding/someip/`
- `SomeIpConnectionManager.hpp` - vsomeip lifecycle management

**Transport**: SOME/IP over UDP/TCP (inter-ECU)

**Use when**:
- Inter-ECU communication over Ethernet
- Manual control over vsomeip application
- Custom SOME/IP protocol handling
- No code generation needed

### 3. CommonAPI Generated Binding (Tool-based)
AUTOSAR-compliant, tool-generated Proxy/Stub from Franca IDL.

**Location**: `source/binding/commonapi/`
- `CommonAPIDBusAdapter.hpp` - D-Bus adapter layer
- `CommonAPISomeIpAdapter.hpp` - SOME/IP adapter layer

**Transport**: D-Bus (via CommonAPI-DBus) or SOME/IP (via CommonAPI-SomeIP)

**Use when**:
- Large-scale deployment with many interfaces
- ARXML integration required
- Tool-assisted development workflow
- Need to switch between D-Bus and SOME/IP without code changes
- Need standardized interface definitions

---

## Architecture Comparison

### Manual Binding Architecture
```
Application Code
      ↓
DBusEventBinding / DBusMethodBinding / DBusFieldBinding
      ↓
DBusConnectionManager (sdbus-c++)
      ↓
D-Bus Daemon
```

**Pros**:
- Simple, direct API
- No external tools required
- Minimal dependencies
- Full control over wire protocol

**Cons**:
- Manual interface definition in C++
- No automatic synchronization with specifications
- Harder to maintain for large interfaces

### CommonAPI Architecture
```
Franca IDL (.fidl)
      ↓
CommonAPI Generators
      ↓
Generated Proxy/Stub C++
      ↓
CommonAPIDBusAdapter / CommonAPISomeIpAdapter (LightAP integration)
      ↓
CommonAPI Runtime (D-Bus or SOME/IP binding)
      ↓
D-Bus Daemon / vsomeip
```

**Pros**:
- AUTOSAR-compliant
- ARXML compatible (via Franca tools)
- Type-safe generated code
- Automatic marshalling
- Standardized across projects

**Cons**:
- Requires external tool-chain
- Additional runtime dependency (CommonAPI)
- More complex setup

---

## Migration Path

### From Manual to CommonAPI

**Step 1**: Extract interface definition
```cpp
// Manual: method_server.cpp
struct CalculateRequest { float a, b; char op; };
struct CalculateResponse { float r; int ec; };
server.RegisterMethod<CalculateRequest, CalculateResponse>("Calculate", handler);
```

**Step 2**: Create Franca IDL
```fidl
// Generated: Calculator.fidl
package com.lightap.example
interface Calculator {
    method calculate {
        in { Float a, Float b, String op }
        out { Float r, Int32 ec }
    }
}
```

**Step 3**: Generate CommonAPI code
```bash
cd tools/commonapi
./generate.sh ../fidl/Calculator.fidl
```

**Step 4**: Implement service with adapter
```cpp
#include "v1/com/lightap/example/CalculatorStubDefault.hpp"
#include <binding/commonapi/CommonAPIDBusAdapter.hpp>

class CalculatorService : public v1::com::lightap::example::CalculatorStubDefault {
    void calculate(float a, float b, std::string op, float& r, int32_t& ec) override {
        // Implementation
    }
};

// In main:
lap::com::commonapi::StubAdapter<CalculatorService> adapter("local", "Calculator");
auto service = std::make_shared<CalculatorService>();
adapter.Initialize(service);
```

### Hybrid Approach (Recommended for Migration)
You can use **both** approaches in the same application:
- **Manual binding** for experimental features
- **CommonAPI** for stable, documented interfaces

They coexist without conflict - just use different service names.

---

## Binding Layer Details

### Manual Binding API (Existing)
**Features**:
- POD serialization via `memcpy`
- String/Vector support
- Result<T> error handling
- LAP_LOG integration

**Example**:
```cpp
#include <binding/dbus/DBusMethodBinding.hpp>

DBusMethodServer server(conn, "/calc", "com.example.Calc");
server.RegisterMethod<Request, Response>("Do", handler);
server.FinishRegistration();
```

### CommonAPI Adapter API (New)
**Features**:
- Wraps CommonAPI Proxy/Stub
- Converts CommonAPI errors to lap::core::Result<T>
- Integrates LAP_LOG
- Handles service availability

**Example**:
```cpp
#include <binding/commonapi/CommonAPIAdapter.hpp>

lap::com::commonapi::ProxyAdapter<CalculatorProxy> adapter("local", "Calculator");
adapter.Initialize();
auto proxy = adapter.GetProxy();
// Use generated proxy methods
```

---

## Directory Structure

```
modules/Com/
├── source/
│   └── binding/
│       ├── dbus/                    # Manual binding (sdbus-c++)
│       │   ├── DBusEventBinding.hpp
│       │   ├── DBusMethodBinding.hpp
│       │   ├── DBusFieldBinding.hpp
│       │   └── DBusConnectionManager.hpp
│       └── commonapi/               # CommonAPI adapter
│           └── CommonAPIAdapter.hpp
├── tools/
│   ├── commonapi/                   # CommonAPI tool integration
│   │   ├── generate.sh
│   │   ├── generators/              # (External binaries)
│   │   └── README.md
│   └── fidl/                        # Franca IDL definitions
│       └── examples/
│           ├── Calculator.fidl
│           ├── VehicleSpeed.fidl
│           └── Radar.fidl
└── test/
    ├── examples/
    │   └── dbus/                    # Manual binding examples
    │       ├── simple_publisher.cpp
    │       ├── method_server.cpp
    │       └── field_server.cpp
    └── unittest/
        └── com_dbus_*_test.cpp      # Unit tests (manual binding)
```

---

## Choosing an Approach

| Criteria | Manual Binding | CommonAPI |
|----------|----------------|-----------|
| **Setup Complexity** | Low | Medium |
| **Learning Curve** | Easy | Moderate |
| **ARXML Support** | No | Yes |
| **Type Safety** | Manual | Generated |
| **Performance** | Excellent | Good |
| **Tooling** | None needed | Generators required |
| **Maintenance** | Manual sync | Automated |
| **AUTOSAR Compliance** | Partial | Full |

**Recommendation**:
- **Prototypes/PoCs**: Use manual binding
- **Production/AUTOSAR projects**: Use CommonAPI + Franca IDL
- **Large teams**: CommonAPI ensures consistency
- **Small embedded**: Manual binding for minimal footprint

---

## Next Steps

### For Manual Binding Users
Continue using the existing API - it's fully supported and tested.

See: `test/examples/dbus/` for working examples.

### For CommonAPI Users
1. Install CommonAPI runtime (see `tools/commonapi/README.md`)
2. Download generators
3. Create `.fidl` files (see `tools/fidl/examples/`)
4. Run `tools/commonapi/generate.sh`
5. Integrate generated code with `CommonAPIAdapter.hpp`

### For Hybrid Users
Use manual binding for internal/private interfaces, CommonAPI for public/external APIs.

---

## References
- [CommonAPI C++ Documentation](https://covesa.github.io/capicxx-core-tools/)
- [Franca IDL User Guide](https://franca.github.io/franca/)
- [AUTOSAR Adaptive Platform Specification](https://www.autosar.org/)
- LightAP Manual Binding API: See headers in `source/binding/dbus/`
