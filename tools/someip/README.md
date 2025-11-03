# SOME/IP Integration Guide

## Overview

LightAP Com module now supports **SOME/IP** transport in addition to D-Bus, using:
- **vsomeip** - Open-source SOME/IP implementation
- **CommonAPI-SomeIP** - Code generation from Franca IDL
- Full integration with LightAP conventions (Result<T>, logging, error handling)

---

## D-Bus vs SOME/IP Comparison

| Feature | D-Bus | SOME/IP | Use Case |
|---------|-------|---------|----------|
| **Protocol** | Binary message bus | Service-oriented middleware over IP | |
| **Transport** | Unix domain socket, TCP | UDP, TCP | |
| **Discovery** | Built-in (dbus-daemon) | Service Discovery (SD) | |
| **Performance** | Good for IPC | Optimized for Ethernet | |
| **Latency** | Low (local IPC) | Medium (network overhead) | |
| **Bandwidth** | Limited | High (Ethernet) | |
| **Serialization** | Custom | SOME/IP wire format | |
| **Message Size** | Limited (~128MB) | Configurable (fragments) | |
| **QoS** | No | Yes (UDP unreliable/TCP reliable) | |
| **Security** | PolicyKit, SELinux | TLS, payload encryption | |
| **Multicast** | No | Yes (UDP multicast) | |
| **Standards** | Desktop/embedded Linux | AUTOSAR | |
| **Ecosystem** | Desktop apps, systemd | Automotive ECUs | |
| **Best For** | Same-ECU communication | Inter-ECU over Ethernet | |

### When to Use D-Bus
✅ **Use D-Bus when:**
- Communication within a single ECU
- Integration with Linux desktop services (systemd, NetworkManager, etc.)
- Low-latency IPC required
- Simpler setup and debugging
- Resource-constrained embedded systems

### When to Use SOME/IP
✅ **Use SOME/IP when:**
- Communication between ECUs over Ethernet
- AUTOSAR Adaptive Platform compliance required
- Service-oriented architecture (SOA) needed
- Dynamic service discovery is critical
- High-bandwidth data transfer (sensor fusion, video, etc.)
- QoS and reliable/unreliable transmission needed

---

## Architecture

### SOME/IP Stack

```
Application Code
      ↓
SomeIpProxyAdapter / SomeIpStubAdapter (LightAP integration)
      ↓
CommonAPI-SomeIP Generated Code (Proxy/Stub)
      ↓
CommonAPI-SomeIP Runtime
      ↓
vsomeip Library (SOME/IP protocol implementation)
      ↓
Linux Network Stack (UDP/TCP over Ethernet)
```

### Components

1. **vsomeip** - Core SOME/IP implementation
   - Service discovery (SD)
   - Message serialization/deserialization
   - Routing between applications
   - Configuration management

2. **CommonAPI-SomeIP Runtime** - Binding layer
   - Maps CommonAPI interfaces to SOME/IP
   - Handles proxy/stub communication
   - Manages vsomeip lifecycle

3. **SomeIpConnectionManager** - LightAP wrapper
   - Singleton for vsomeip application management
   - Thread-safe initialization/shutdown
   - Integrated logging

4. **SomeIpProxyAdapter / SomeIpStubAdapter** - LightAP integration
   - Converts CommonAPI errors to `Result<T>`
   - Provides LAP_LOG integration
   - Simplifies service registration

---

## Installation

### 1. Install vsomeip

```bash
# Clone vsomeip repository
git clone https://github.com/COVESA/vsomeip.git
cd vsomeip

# Build and install
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j$(nproc)
sudo make install
sudo ldconfig
```

**Version tested:** vsomeip 3.3.x

### 2. Install CommonAPI-SomeIP Runtime

```bash
# Clone runtime repository
git clone https://github.com/COVESA/capicxx-someip-runtime.git
cd capicxx-someip-runtime

# Build and install
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j$(nproc)
sudo make install
sudo ldconfig
```

**Version tested:** CommonAPI-SomeIP 3.2.x

### 3. Download CommonAPI-SomeIP Generator

```bash
cd modules/Com/tools/commonapi/generators

# Download generator
wget https://github.com/COVESA/capicxx-someip-tools/releases/download/3.2.0.1/commonapi-someip-generator-linux-x86_64
chmod +x commonapi-someip-generator-linux-x86_64
```

### 4. Verify Installation

```bash
# Check vsomeip
ldconfig -p | grep vsomeip

# Check CommonAPI-SomeIP
ldconfig -p | grep CommonAPI-SomeIP

# Check generator
./commonapi-someip-generator-linux-x86_64 --version
```

---

## Usage

### Step 1: Define Service in Franca IDL

See existing examples in `tools/fidl/examples/`:
- `Calculator.fidl` - Service interface definition
- `Calculator.fdepl` - SOME/IP deployment specification

### Step 2: Generate Code

```bash
cd tools/commonapi

# Generate SOME/IP binding
./generate_new.sh ../fidl/examples/Calculator.fidl someip

# Or generate both D-Bus and SOME/IP
./generate_new.sh ../fidl/examples/Calculator.fidl both
```

**Output:** Generated files in `tools/commonapi/gen/`

### Step 3: Configure vsomeip

Create JSON configuration file (see `tools/someip/vsomeip_calculator.json`):

```json
{
    "unicast": "127.0.0.1",
    "applications": [
        {"name": "calculator_service", "id": "0x1111"},
        {"name": "calculator_client", "id": "0x2222"}
    ],
    "services": [
        {
            "service": "0x1234",
            "instance": "0x5678",
            "unreliable": "30509",
            "reliable": {"port": "30510"}
        }
    ],
    "routing": "calculator_service",
    "service-discovery": {
        "enable": "true",
        "multicast": "224.244.224.245",
        "port": "30490"
    }
}
```

**Important:** Service ID and instance ID in `.fdepl` must match JSON config.

### Step 4: Implement Service (Server)

```cpp
#include <binding/someip/SomeIpConnectionManager.hpp>
#include <binding/commonapi/CommonAPISomeIpAdapter.hpp>
#include <v1/com/lightap/example/CalculatorStubDefault.hpp>

class MyService : public v1::com::lightap::example::CalculatorStubDefault {
    // Implement virtual methods...
};

int main() {
    // Initialize vsomeip
    auto& connMgr = lap::com::someip::SomeIpConnectionManager::getInstance();
    connMgr.Initialize("calculator_service");

    // Start vsomeip in thread
    std::thread vsomeipThread([&]() { connMgr.Start(true); });

    // Register service
    auto service = std::make_shared<MyService>();
    lap::com::commonapi::SomeIpStubAdapter<MyService> adapter("local", "Calculator");
    adapter.Initialize(service);

    // Keep running...
}
```

See `test/examples/someip/calculator_server.cpp` for full example.

### Step 5: Create Client

```cpp
#include <binding/someip/SomeIpConnectionManager.hpp>
#include <binding/commonapi/CommonAPISomeIpAdapter.hpp>
#include <v1/com/lightap/example/CalculatorProxy.hpp>

int main() {
    // Initialize vsomeip
    auto& connMgr = lap::com::someip::SomeIpConnectionManager::getInstance();
    connMgr.Initialize("calculator_client");

    // Start vsomeip in thread
    std::thread vsomeipThread([&]() { connMgr.Start(true); });

    // Connect to service
    lap::com::commonapi::SomeIpProxyAdapter<CalculatorProxy> 
        adapter("local", "Calculator");
    adapter.Initialize(5000); // 5 second timeout

    auto proxy = adapter.GetProxy();
    
    // Call methods...
    float result;
    int32_t errorCode;
    CommonAPI::CallStatus status;
    proxy->calculate(10.0f, 5.0f, "+", status, result, errorCode);
}
```

See `test/examples/someip/calculator_client.cpp` for full example.

### Step 6: Run

```bash
# Terminal 1: Export config and run server
export VSOMEIP_CONFIGURATION=/path/to/vsomeip_calculator.json
./calculator_server

# Terminal 2: Export same config and run client
export VSOMEIP_CONFIGURATION=/path/to/vsomeip_calculator.json
./calculator_client
```

---

## Configuration Reference

### vsomeip JSON Structure

```json
{
    "unicast": "IP_ADDRESS",           // This ECU's IP
    "logging": { ... },                 // Log settings
    "applications": [ ... ],            // Application names and IDs
    "services": [ ... ],                // Service definitions
    "routing": "APP_NAME",              // Routing manager (usually server)
    "service-discovery": { ... }        // SD configuration
}
```

### Franca Deployment (.fdepl)

```fidl
define org.genivi.commonapi.someip.deployment for interface MyInterface {
    SomeIpServiceID = 0x1234           // Must be unique
    
    method myMethod {
        SomeIpMethodID = 1              // Unique within service
        SomeIpReliable = true           // TCP vs UDP
    }
    
    attribute myAttr {
        SomeIpGetterID = 10
        SomeIpSetterID = 11
        SomeIpNotifierID = 12
        SomeIpNotifierEventGroups = { 1 }
        SomeIpAttributeReliable = true
    }
    
    broadcast myEvent {
        SomeIpEventID = 20
        SomeIpEventGroups = { 1 }
        SomeIpEventReliable = false     // Unreliable for high-frequency
    }
}
```

---

## Performance Tuning

### Network Configuration

```bash
# Increase UDP buffer sizes
sudo sysctl -w net.core.rmem_max=26214400
sudo sysctl -w net.core.wmem_max=26214400
```

### vsomeip Configuration

```json
{
    "buffer-shrink-threshold": "5",
    "max_message_size_reliable": "10485760",  // 10 MB
    "threads": {
        "io": "2",
        "request-debouncing": "2"
    }
}
```

### Message Optimization

- Use **unreliable (UDP)** for high-frequency sensor data
- Use **reliable (TCP)** for critical commands and state
- Group events into **event groups** by update frequency
- Enable **serialization optimization** in .fdepl

---

## Debugging

### vsomeip Logging

```json
{
    "logging": {
        "level": "debug",      // trace, debug, info, warning, error, fatal
        "console": "true",
        "file": {
            "enable": "true",
            "path": "/tmp/vsomeip.log"
        }
    }
}
```

### Wireshark Dissector

vsomeip includes a Wireshark dissector for SOME/IP:
```bash
# Copy dissector
cp vsomeip/tools/wireshark/someip.lua ~/.local/lib/wireshark/plugins/

# Capture traffic
wireshark -i lo  # For local testing
```

### Common Issues

**Service not found:**
- Check service ID/instance ID match in .fdepl and JSON
- Verify routing manager is running (usually server)
- Check firewall allows UDP 30490 (service discovery)

**Timeout on method calls:**
- Increase timeout in `Initialize()`
- Check server is offering service
- Verify network connectivity

**Serialization errors:**
- Ensure .fdepl deployment matches interface
- Check struct length width settings
- Verify enum widths

---

## Migration from D-Bus

### Code Differences

| D-Bus | SOME/IP |
|-------|---------|
| `DBusConnectionManager` | `SomeIpConnectionManager` |
| `CommonAPIAdapter` | `SomeIpProxyAdapter` / `SomeIpStubAdapter` |
| No config file | Requires vsomeip JSON config |
| `systemctl status dbus` | Check network connectivity |

### Steps

1. Keep D-Bus code unchanged (both can coexist)
2. Add SOME/IP deployment (.fdepl) for services
3. Generate SOME/IP code with `generate_new.sh`
4. Create vsomeip configuration JSON
5. Use SOME/IP adapters in new code
6. Test both transports in parallel

---

## Best Practices

✅ **Do:**
- Use meaningful service/instance IDs (avoid conflicts)
- Configure separate event groups by update frequency
- Use UDP for high-frequency, TCP for critical data
- Test service discovery timeout scenarios
- Monitor network bandwidth usage

❌ **Don't:**
- Share vsomeip config between incompatible services
- Use same service ID for different services
- Forget to set VSOMEIP_CONFIGURATION environment variable
- Block in vsomeip callbacks
- Ignore serialization endianness

---

## References

- [vsomeip Documentation](https://github.com/COVESA/vsomeip/wiki)
- [CommonAPI-SomeIP](https://github.com/COVESA/capicxx-someip-runtime)
- [SOME/IP Specification](https://some-ip.com/)
- [AUTOSAR Adaptive Platform](https://www.autosar.org/standards/adaptive-platform/)
- LightAP D-Bus binding: `source/binding/dbus/`
- LightAP SOME/IP binding: `source/binding/someip/`

---

## Example Services

| Service | Transport | Configuration | Use Case |
|---------|-----------|---------------|----------|
| Calculator | D-Bus | None | Same-ECU RPC |
| Calculator | SOME/IP | vsomeip_calculator.json | Inter-ECU demo |
| VehicleSpeed | SOME/IP | vsomeip_vehicle.json | Sensor data streaming |
| Radar | SOME/IP | vsomeip_radar.json | High-frequency events |

See `test/examples/` for full implementations.
