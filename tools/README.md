# Com Module Tools

## Overview
This directory contains tools for D-Bus interface code generation using CommonAPI and Franca IDL.

## Directory Structure
```
tools/
├── README.md                    # This file
├── commonapi/                   # CommonAPI tools integration
│   ├── generators/              # CommonAPI generator binaries (not in repo)
│   ├── generate.sh              # Code generation script
│   └── README.md                # CommonAPI setup guide
├── fidl/                        # Franca IDL interface definitions
│   └── examples/                # Example .fidl files
└── generated/                   # Generated CommonAPI C++ code (auto-generated)
```

## CommonAPI Toolchain

### Prerequisites
1. **CommonAPI Core Generator** (v3.x)
   - Generates core C++ stub/proxy from Franca IDL
2. **CommonAPI D-Bus Generator** (v3.x)
   - Generates D-Bus binding code
3. **Franca IDL** (.fidl files)
   - Interface definitions compatible with ARXML

### Installation
See `commonapi/README.md` for detailed setup instructions.

## Workflow

### 1. Define Interface (Franca IDL)
Create `.fidl` file in `fidl/` directory:
```fidl
package com.example

interface Calculator {
    version { major 1 minor 0 }
    
    method calculate {
        in {
            Float operand1
            Float operand2
            String operation
        }
        out {
            Float result
            Int32 errorCode
        }
    }
}
```

### 2. Generate C++ Code
```bash
cd tools/commonapi
./generate.sh ../fidl/Calculator.fidl
```

This generates:
- `v1/com/example/Calculator*.hpp` (Proxy/Stub interfaces)
- `v1/com/example/DBusCalculator*.hpp` (D-Bus bindings)

### 3. Integrate with LightAP Com
Generated code works with existing LightAP binding infrastructure through adapter pattern.

### 4. Build and Deploy
CMake automatically discovers and compiles generated code.

## ARXML Compatibility
Franca IDL is AUTOSAR-compliant and can be converted to/from ARXML:
- Use AUTOSAR tools to export `.arxml` → `.fidl`
- Or use Franca ARXML connector plugins

## Manual Binding vs Generated Code
The Com module supports both approaches:
- **Manual binding**: Direct use of `DBusEventBinding`, `DBusMethodBinding`, `DBusFieldBinding`
- **Generated binding**: CommonAPI-generated Proxy/Stub + LightAP adapter

Choose based on:
- Manual: Quick prototyping, simple interfaces
- Generated: Large-scale deployment, ARXML integration, tool-assisted development
