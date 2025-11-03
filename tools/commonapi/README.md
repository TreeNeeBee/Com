# CommonAPI Tools Integration

## Overview
This directory integrates CommonAPI C++ toolchain for D-Bus interface generation from Franca IDL.

## Prerequisites

### 1. CommonAPI Core Runtime (v3.2.x)
Runtime library for generated code:
```bash
# Install via package manager (if available)
sudo apt install libcommonapi-dev

# Or build from source
git clone https://github.com/COVESA/capicxx-core-runtime.git
cd capicxx-core-runtime
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j4 && sudo make install
```

### 2. CommonAPI D-Bus Runtime (v3.2.x)
D-Bus binding runtime:
```bash
# Install dependencies
sudo apt install libdbus-1-dev

# Build from source
git clone https://github.com/COVESA/capicxx-dbus-runtime.git
cd capicxx-dbus-runtime
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j4 && sudo make install
```

### 3. CommonAPI Generators
Download prebuilt generators from COVESA releases:
```bash
cd generators/
# Download commonapi-core-generator-linux-x86_64
wget https://github.com/COVESA/capicxx-core-tools/releases/download/3.2.0/commonapi-core-generator-linux-x86_64
chmod +x commonapi-core-generator-linux-x86_64

# Download commonapi-dbus-generator-linux-x86_64
wget https://github.com/COVESA/capicxx-dbus-tools/releases/download/3.2.0/commonapi-dbus-generator-linux-x86_64
chmod +x commonapi-dbus-generator-linux-x86_64
```

## Usage

### Generate Code from Franca IDL
```bash
./generate.sh <path-to-fidl-file> [output-dir]
```

Example:
```bash
./generate.sh ../fidl/examples/Calculator.fidl ../generated
```

### Generated Files
For `Calculator.fidl`:
- **Core (Proxy/Stub interfaces)**:
  - `v1/com/example/Calculator.hpp`
  - `v1/com/example/CalculatorProxy.hpp`
  - `v1/com/example/CalculatorStub.hpp`
  - `v1/com/example/CalculatorStubDefault.hpp`

- **D-Bus binding**:
  - `v1/com/example/CalculatorDBusProxy.hpp`
  - `v1/com/example/CalculatorDBusStub.hpp`
  - `v1/com/example/CalculatorDBusStubAdapter.hpp`

## Integration with LightAP

### Option 1: Direct CommonAPI Usage
Use generated Proxy/Stub with CommonAPI runtime directly.

### Option 2: LightAP Adapter (Recommended)
Wrap generated code with LightAP `Result<T>`, logging, and error handling:
```cpp
#include "v1/com/example/CalculatorProxy.hpp"
#include <core/CResult.hpp>

class CalculatorClient {
    std::shared_ptr<v1::com::example::CalculatorProxy<>> m_proxy;
public:
    lap::core::Result<float> Calculate(float a, float b, const std::string& op);
};
```

## Franca IDL Examples
See `../fidl/examples/` for sample interface definitions.

## Troubleshooting

### Generator Not Found
Ensure generators are in `generators/` directory and executable:
```bash
ls -lh generators/
chmod +x generators/commonapi-*-generator-linux-x86_64
```

### Runtime Library Not Found
Add to `LD_LIBRARY_PATH`:
```bash
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
```

### CMake Cannot Find CommonAPI
Set CMake prefix path:
```cmake
set(CMAKE_PREFIX_PATH /usr/local)
find_package(CommonAPI REQUIRED)
find_package(CommonAPI-DBus REQUIRED)
```

## References
- [CommonAPI C++ Documentation](https://covesa.github.io/capicxx-core-tools/)
- [Franca IDL Specification](https://franca.github.io/franca/)
- [AUTOSAR Adaptive Platform](https://www.autosar.org/standards/adaptive-platform/)
