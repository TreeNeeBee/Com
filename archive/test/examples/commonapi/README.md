# CommonAPI Examples

This directory contains examples demonstrating the use of CommonAPI-generated code with LightAP.

## Prerequisites

1. **CommonAPI Runtime installed** (see `tools/commonapi/README.md`)
2. **Generated code from Franca IDL**:
   ```bash
   cd ../../tools/commonapi
   ./generate.sh ../fidl/examples/Calculator.fidl
   ```

## Directory Structure

```
commonapi/
├── README.md                    # This file
├── calculator_server.cpp        # Service implementation using StubAdapter
├── calculator_client.cpp        # Client using ProxyAdapter
└── CMakeLists.txt              # Build configuration (TBD)
```

## Build Instructions

### Option 1: Manual Build
```bash
# Ensure generated code is in include path
export GEN_CODE_PATH=/home/ddk/1_workspace/2_middleware/LightAP/modules/Com/tools/commonapi/gen

g++ -std=c++14 \
    -I../../../source/inc \
    -I../../../source/binding/commonapi \
    -I${GEN_CODE_PATH} \
    -I/usr/local/include/CommonAPI-3.2 \
    calculator_server.cpp \
    -L../../../build -llap_core -llap_log -llap_com \
    -lCommonAPI -lCommonAPI-DBus \
    -o calculator_server

g++ -std=c++14 \
    -I../../../source/inc \
    -I../../../source/binding/commonapi \
    -I${GEN_CODE_PATH} \
    -I/usr/local/include/CommonAPI-3.2 \
    calculator_client.cpp \
    -L../../../build -llap_core -llap_log -llap_com \
    -lCommonAPI -lCommonAPI-DBus \
    -o calculator_client
```

### Option 2: CMake Integration (Future)
```bash
mkdir build && cd build
cmake ..
make
```

## Usage

### Terminal 1: Run Server
```bash
./calculator_server
# Output:
# [INFO] CalculatorService: Initializing...
# [INFO] StubAdapter: Service registered at com.lightap.example.Calculator
# [INFO] CalculatorService: Ready for requests
```

### Terminal 2: Run Client
```bash
./calculator_client
# Output:
# [INFO] Connecting to Calculator service...
# [INFO] Service available!
# [INFO] Calculate: 10 + 5 = 15
# [INFO] Calculate: 20 - 8 = 12
# [INFO] Calculate: 6 * 7 = 42
# [INFO] Calculate: 100 / 4 = 25
# [ERROR] Calculate: 10 / 0 = Error: Division by zero
```

## Code Overview

### Server (`calculator_server.cpp`)
- Implements `CalculatorStubDefault` (generated from .fidl)
- Uses `StubAdapter<CalculatorService>` to register with D-Bus
- Implements business logic in overridden virtual methods
- Uses LightAP logging (`LAP_LOG_*`)

### Client (`calculator_client.cpp`)
- Uses `ProxyAdapter<CalculatorProxy>` to connect to service
- Wraps synchronous method calls with `WrapCallStatus()`
- Converts CommonAPI errors to `lap::core::Result<T>`
- Demonstrates error handling for invalid operations

## Key Differences vs Manual Binding

| Aspect | Manual Binding | CommonAPI Binding |
|--------|----------------|-------------------|
| **Interface Definition** | C++ structs | Franca IDL |
| **Code Generation** | No | Yes |
| **Type Safety** | Manual marshalling | Automatic |
| **Service Setup** | `RegisterMethod()` | Override virtual methods |
| **Client Calls** | `CallMethod<>()` | `proxy->methodName()` |
| **Error Handling** | `Result<Response>` | `CallStatus` + `WrapCallStatus()` |

## Comparison with Manual Examples

See `../dbus/method_server.cpp` for equivalent manual binding implementation.

**Manual approach** (direct API):
```cpp
server.RegisterMethod<CalcReq, CalcResp>("Do", [](const CalcReq& req, CalcResp& resp) {
    // Handler logic
});
```

**CommonAPI approach** (generated code):
```cpp
class CalculatorService : public CalculatorStubDefault {
    void calculate(float a, float b, std::string op, float& result, int32_t& error) override {
        // Handler logic
    }
};
```

## Troubleshooting

### "Service not available"
- Ensure server is running first
- Check D-Bus daemon is running: `systemctl status dbus`
- Verify service name matches in both server and client

### "Undefined reference to CommonAPI symbols"
- Install CommonAPI runtime (see `tools/commonapi/README.md`)
- Add `-lCommonAPI -lCommonAPI-DBus` to linker flags

### "Cannot find generated headers"
- Run `tools/commonapi/generate.sh` first
- Add generated code directory to include path

### Crashes during shutdown
- CommonAPI runtime cleanup is automatic
- Don't call `Runtime::get()->unregisterService()` manually if using StubAdapter
- StubAdapter destructor handles cleanup

## Next Steps

1. **Experiment with attributes**: Uncomment attribute code in examples
2. **Add broadcasts**: Implement event notifications
3. **Async methods**: Try fire-and-forget methods
4. **Multiple interfaces**: Implement multiple services in one process

For ARXML integration, see `tools/commonapi/README.md`.
