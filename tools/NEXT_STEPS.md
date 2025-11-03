# CommonAPI Integration - Next Steps

## Current Status ✅

The CommonAPI integration framework has been successfully created:

### Completed Infrastructure
1. **Tools Directory Structure**
   - `tools/commonapi/` - Generator integration
   - `tools/fidl/examples/` - Franca IDL examples
   - `tools/commonapi/generate.sh` - Code generation script

2. **Adapter Layer**
   - `source/binding/commonapi/CommonAPIAdapter.hpp` 
   - `ProxyAdapter<T>` - Client-side adapter
   - `StubAdapter<T>` - Server-side adapter
   - Integrated with LightAP `Result<T>`, logging, and error handling

3. **Documentation**
   - `tools/README.md` - CommonAPI workflow overview
   - `tools/commonapi/README.md` - Detailed setup guide
   - `source/binding/README.md` - Architecture comparison (manual vs CommonAPI)
   - `test/examples/commonapi/README.md` - Example usage guide

4. **Example Code (Ready for Generated Code)**
   - `test/examples/commonapi/calculator_server.cpp` - Stub implementation example
   - `test/examples/commonapi/calculator_client.cpp` - Proxy usage example
   - Code is commented out with `#if 0` blocks, ready to be enabled after code generation

5. **Franca IDL Examples**
   - `Calculator.fidl` - Methods, attributes, broadcasts, type collections
   - `VehicleSpeed.fidl` - Vehicle service with properties and notifications
   - `Radar.fidl` - Sensor data streaming with object detection

### Verification ✅
- All existing Com unit tests pass (3/3 - 100%)
- All code compiles successfully
- Manual binding examples updated with LAP_LOG macros
- Deprecated code removed (DBusEventBinding_v2.hpp)

---

## Next Steps 🚀

### Step 1: Install CommonAPI Toolchain

**Required Components:**
- CommonAPI Core Runtime (libCommonAPI.so)
- CommonAPI D-Bus Runtime (libCommonAPI-DBus.so)
- commonapi-core-generator (executable)
- commonapi-dbus-generator (executable)

**Installation:**
```bash
# See tools/commonapi/README.md for detailed instructions

# Quick start (Ubuntu/Debian):
cd tools/commonapi

# Download runtimes (example URLs - check COVESA GitHub for latest)
wget https://github.com/COVESA/capicxx-core-runtime/releases/download/3.2.3/commonapi-3.2.3-Linux-x86_64.tar.gz
wget https://github.com/COVESA/capicxx-dbus-runtime/releases/download/3.2.3/commonapi-dbus-3.2.3-Linux-x86_64.tar.gz

# Extract and install
sudo tar -xzf commonapi-3.2.3-Linux-x86_64.tar.gz -C /usr/local
sudo tar -xzf commonapi-dbus-3.2.3-Linux-x86_64.tar.gz -C /usr/local

# Download generators
mkdir -p generators
cd generators
wget https://github.com/COVESA/capicxx-core-tools/releases/download/3.2.0.1/commonapi-core-generator-linux-x86_64
wget https://github.com/COVESA/capicxx-dbus-tools/releases/download/3.2.0.1/commonapi-dbus-generator-linux-x86_64
chmod +x commonapi-*-generator-linux-x86_64

# Update library cache
sudo ldconfig
```

### Step 2: Generate Code from Franca IDL

```bash
cd tools/commonapi
./generate.sh ../fidl/examples/Calculator.fidl
```

**Expected Output:**
```
Generated code will be in: gen/
├── v1/com/lightap/example/
│   ├── Calculator.hpp                      # Interface definition
│   ├── CalculatorProxy.hpp                 # Client proxy
│   ├── CalculatorProxyBase.hpp
│   ├── CalculatorStub.hpp                  # Server stub
│   ├── CalculatorStubDefault.hpp           # Default stub implementation
│   ├── CalculatorTypes.hpp                 # Type definitions
│   └── CalculatorDBusProxy.hpp             # D-Bus binding
│       CalculatorDBusStubAdapter.hpp
```

### Step 3: Enable and Build Examples

```bash
cd test/examples/commonapi

# Edit calculator_server.cpp and calculator_client.cpp
# Change: #if 0 → #if 1 (to enable the code)

# Build manually (or integrate into CMake later)
g++ -std=c++14 \
    -I../../../source/inc \
    -I../../../source/binding/commonapi \
    -I../../../tools/commonapi/gen \
    -I/usr/local/include/CommonAPI-3.2 \
    calculator_server.cpp \
    -L../../../build -llap_core -llap_log -llap_com \
    -lCommonAPI -lCommonAPI-DBus \
    -o calculator_server

g++ -std=c++14 \
    -I../../../source/inc \
    -I../../../source/binding/commonapi \
    -I../../../tools/commonapi/gen \
    -I/usr/local/include/CommonAPI-3.2 \
    calculator_client.cpp \
    -L../../../build -llap_core -llap_log -llap_com \
    -lCommonAPI -lCommonAPI-DBus \
    -o calculator_client
```

### Step 4: Test Generated Code

**Terminal 1 (Server):**
```bash
cd test/examples/commonapi
./calculator_server
```

**Terminal 2 (Client):**
```bash
cd test/examples/commonapi
./calculator_client
```

**Expected Client Output:**
```
[INFO] Connecting to Calculator service...
[INFO] Service available!
[INFO] Calculate: 10 + 5 = 15
[INFO] Calculate: 20 - 8 = 12
[INFO] Calculate: 6 * 7 = 42
[INFO] Calculate: 100 / 4 = 25
[ERROR] Calculate: 10 / 0 = Error: Division by zero
```

### Step 5: CMake Integration (Optional)

Create automatic code generation from `.fidl` files:

```cmake
# In modules/Com/CMakeLists.txt

# Find CommonAPI generators
find_program(COMMONAPI_CORE_GEN commonapi-core-generator
    PATHS ${CMAKE_SOURCE_DIR}/modules/Com/tools/commonapi/generators)
find_program(COMMONAPI_DBUS_GEN commonapi-dbus-generator
    PATHS ${CMAKE_SOURCE_DIR}/modules/Com/tools/commonapi/generators)

# Function to generate code from .fidl
function(commonapi_generate_code FIDL_FILE)
    get_filename_component(FIDL_NAME ${FIDL_FILE} NAME_WE)
    set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)
    
    add_custom_command(
        OUTPUT ${GEN_DIR}/generated_${FIDL_NAME}.stamp
        COMMAND ${CMAKE_SOURCE_DIR}/modules/Com/tools/commonapi/generate.sh ${FIDL_FILE}
        COMMAND ${CMAKE_COMMAND} -E touch ${GEN_DIR}/generated_${FIDL_NAME}.stamp
        DEPENDS ${FIDL_FILE}
        COMMENT "Generating CommonAPI code from ${FIDL_NAME}.fidl"
    )
    
    add_custom_target(generate_${FIDL_NAME}
        DEPENDS ${GEN_DIR}/generated_${FIDL_NAME}.stamp)
endfunction()

# Example usage:
# commonapi_generate_code(${CMAKE_SOURCE_DIR}/modules/Com/tools/fidl/examples/Calculator.fidl)
```

### Step 6: Create Unit Tests for CommonAPI Binding

Create `test/unittest/com_commonapi_test.cpp`:
- Test ProxyAdapter initialization
- Test StubAdapter registration
- Test method calls through generated Proxy/Stub
- Test error handling and Result<T> conversion
- Test attribute access
- Test broadcasts

---

## Migration Strategy

### For New Services
Use CommonAPI + Franca IDL from the start:
1. Define interface in `.fidl` file
2. Run `generate.sh` to create Proxy/Stub
3. Implement service using `StubAdapter<T>`
4. Create client using `ProxyAdapter<T>`

### For Existing Services
Keep using manual bindings (fully supported):
- No migration required
- Both approaches can coexist in the same application
- Gradually migrate complex interfaces to CommonAPI as needed

### Hybrid Approach (Recommended)
- **Internal/private interfaces**: Manual binding (faster iteration)
- **Public/external APIs**: CommonAPI (standardized, ARXML-compatible)
- **Prototypes**: Manual binding (quick experiments)
- **Production**: CommonAPI (tool-generated, validated)

---

## Troubleshooting

### Generator Not Found
- Download generators from COVESA GitHub (see Step 1)
- Place in `tools/commonapi/generators/`
- Make executable with `chmod +x`

### Runtime Library Not Found
- Install CommonAPI runtime libraries (see Step 1)
- Run `sudo ldconfig` after installation
- Check with: `ldconfig -p | grep CommonAPI`

### Generated Code Compilation Errors
- Ensure generated code directory is in include path
- Check CommonAPI version compatibility (use 3.2.x)
- Verify all template instantiations are correct

### Service Not Available
- Ensure D-Bus daemon is running: `systemctl status dbus`
- Check service name matches in server and client
- Verify domain and instance names are correct

---

## References

### Documentation
- [CommonAPI C++ Documentation](https://covesa.github.io/capicxx-core-tools/)
- [Franca IDL User Guide](https://franca.github.io/franca/)
- [AUTOSAR Adaptive Platform](https://www.autosar.org/)

### Source Code
- [COVESA CommonAPI GitHub](https://github.com/COVESA/)
- LightAP Manual Binding: `source/binding/dbus/`
- LightAP CommonAPI Adapter: `source/binding/commonapi/`

### LightAP Modules
- Core: Memory management, error handling (`lap::core::Result<T>`)
- Log: Logging infrastructure (`LAP_LOG_*` macros)
- Com: D-Bus communication (manual + CommonAPI bindings)

---

## Success Criteria

Integration is complete when:
- ✅ CommonAPI runtime and generators installed
- ✅ Calculator example builds and runs successfully
- ✅ Both manual and CommonAPI examples coexist and work
- ✅ Unit tests pass for both binding types
- ✅ CMake automatic code generation works (optional)
- ✅ Documentation covers both approaches

---

## Contact

For questions about:
- **Manual Binding**: See `test/examples/dbus/` and `test/unittest/com_dbus_*_test.cpp`
- **CommonAPI Integration**: See `tools/commonapi/README.md` and `source/binding/README.md`
- **ARXML Workflow**: See `tools/README.md` (requires Franca ARXML connector)

---

**Current Status**: Infrastructure ready, awaiting generator binaries and first code generation test.
