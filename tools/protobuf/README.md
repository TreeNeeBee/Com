# Protobuf Code Generation for LightAP Com Module

## Overview

This directory contains Protocol Buffers (protobuf) message definitions and code generation tools for the Socket transport layer.

## Requirements

- **Protocol Buffers Compiler** (`protoc`) version 3.0 or higher
- **libprotobuf** development libraries

### Installation

#### Ubuntu/Debian
```bash
sudo apt-get install protobuf-compiler libprotobuf-dev
```

#### Fedora/CentOS
```bash
sudo dnf install protobuf protobuf-devel
```

#### macOS
```bash
brew install protobuf
```

#### From Source
```bash
# Download from https://github.com/protocolbuffers/protobuf/releases
wget https://github.com/protocolbuffers/protobuf/releases/download/v21.12/protobuf-cpp-3.21.12.tar.gz
tar -xzf protobuf-cpp-3.21.12.tar.gz
cd protobuf-3.21.12
./configure
make -j$(nproc)
sudo make install
sudo ldconfig
```

## Usage

### 1. Generate C++ Code from .proto Files

```bash
cd modules/Com/tools/protobuf
./generate_protobuf.sh
```

This will:
- Parse all `.proto` files in the current directory
- Generate C++ header (`.pb.h`) and implementation (`.pb.cc`) files
- Place generated files in `generated/` subdirectory

### 2. Use Generated Code in Your Application

```cpp
#include "tools/protobuf/generated/calculator.pb.h"

// Create a request message
lap::com::example::CalculateRequest request;
request.set_operand1(10.5);
request.set_operand2(3.2);
request.set_operation("add");

// Create a response message
lap::com::example::CalculateResponse response;
response.set_result(13.7);
response.set_error_code(0);
```

## Message Definitions

### calculator.proto

Defines messages for a calculator service:

- **CalculateRequest**: Contains two operands and an operation
- **CalculateResponse**: Contains the result and error information
- **EchoRequest**: Simple echo service request
- **EchoResponse**: Echo service response with repeated messages

### Adding New Messages

1. Create a new `.proto` file (using proto3 syntax):

```protobuf
syntax = "proto3";

package lap.com.example;

message MyRequest {
    string data = 1;
    int32 value = 2;
}

message MyResponse {
    string result = 1;
}
```

2. Run the generation script:

```bash
./generate_protobuf.sh
```

3. Include the generated header in your code:

```cpp
#include "tools/protobuf/generated/myfile.pb.h"
```

## Protobuf Style Guide

### Naming Conventions

- **Messages**: PascalCase (e.g., `CalculateRequest`)
- **Fields**: snake_case (e.g., `error_message`)
- **Enums**: PascalCase for types, SCREAMING_SNAKE_CASE for values
- **Packages**: lowercase with dots (e.g., `lap.com.example`)

### Field Numbering

- Use 1-15 for frequently used fields (more efficient encoding)
- Use 16+ for less common fields
- Never reuse field numbers
- Reserve deleted field numbers:

```protobuf
message MyMessage {
    reserved 2, 15, 9 to 11;
    reserved "foo", "bar";
    
    string name = 1;
    int32 id = 3;
}
```

### Best Practices

1. **Use proto3 syntax**: Modern features, simpler semantics
2. **Add comments**: Document your messages and fields
3. **Version your messages**: Add version field if needed
4. **Use optional for optional fields**: Make intent clear
5. **Avoid nested messages**: Keep structure flat when possible
6. **Use enums for fixed sets**: Better type safety

## Integration with Socket Transport

### Server Example

```cpp
#include "binding/socket/SocketMethodBinding.hpp"
#include "tools/protobuf/generated/calculator.pb.h"

using namespace lap::com::binding::socket;
using namespace lap::com::example;

// Define handler
CalculateResponse handleCalculate(const CalculateRequest& request) {
    CalculateResponse response;
    
    if (request.operation() == "add") {
        response.set_result(request.operand1() + request.operand2());
    } else if (request.operation() == "subtract") {
        response.set_result(request.operand1() - request.operand2());
    } else {
        response.set_error_message("Unknown operation");
        response.set_error_code(-1);
    }
    
    return response;
}

// Create server
SocketEndpoint endpoint{
    .socketPath = "/tmp/calculator.sock",
    .mode = SocketTransportMode::kStream,
    .maxMessageSize = 65536
};

SocketMethodResponder<CalculateRequest, CalculateResponse> server(
    endpoint, handleCalculate);

server.start();
```

### Client Example

```cpp
#include "binding/socket/SocketMethodBinding.hpp"
#include "tools/protobuf/generated/calculator.pb.h"

using namespace lap::com::binding::socket;
using namespace lap::com::example;

SocketEndpoint endpoint{
    .socketPath = "/tmp/calculator.sock",
    .mode = SocketTransportMode::kStream,
    .maxMessageSize = 65536
};

SocketMethodCaller<CalculateRequest, CalculateResponse> client(endpoint);

// Prepare request
CalculateRequest request;
request.set_operand1(10.0);
request.set_operand2(5.0);
request.set_operation("add");

// Call method
auto result = client.call(request, 5000);
if (result.HasValue()) {
    const auto& response = result.Value();
    if (response.error_code() == 0) {
        std::cout << "Result: " << response.result() << std::endl;
    } else {
        std::cerr << "Error: " << response.error_message() << std::endl;
    }
}
```

## Performance Tips

### 1. Reuse Message Objects

```cpp
// Good: Reuse message
CalculateRequest request;
for (int i = 0; i < 1000; ++i) {
    request.Clear();  // Clear instead of creating new
    request.set_operand1(i);
    // ...
}

// Bad: Create new message each time
for (int i = 0; i < 1000; ++i) {
    CalculateRequest request;  // Expensive!
    // ...
}
```

### 2. Use Move Semantics

```cpp
// Move instead of copy
CalculateResponse response = std::move(createResponse());
```

### 3. Reserve Repeated Fields

```cpp
EchoResponse response;
response.mutable_messages()->Reserve(100);  // Pre-allocate
```

### 4. Use Arena Allocation (Advanced)

```cpp
#include <google/protobuf/arena.h>

google::protobuf::Arena arena;
auto* request = google::protobuf::Arena::CreateMessage<CalculateRequest>(&arena);
// All allocations are from arena, faster cleanup
```

## Troubleshooting

### Error: protoc: command not found

Install Protocol Buffers compiler (see Requirements section).

### Error: protobuf version mismatch

Ensure runtime library matches compiler version:

```bash
protoc --version
pkg-config --modversion protobuf
```

### Error: cannot find -lprotobuf

Install development libraries:

```bash
sudo apt-get install libprotobuf-dev  # Ubuntu/Debian
```

### Generated files not found

Check the `generated/` directory:

```bash
ls -la modules/Com/tools/protobuf/generated/
```

Re-run generation script if files are missing.

## References

- [Protocol Buffers Documentation](https://protobuf.dev/)
- [Proto3 Language Guide](https://protobuf.dev/programming-guides/proto3/)
- [C++ Tutorial](https://protobuf.dev/getting-started/cpptutorial/)
- [Style Guide](https://protobuf.dev/programming-guides/style/)

## Files

```
tools/protobuf/
├── README.md                    # This file
├── generate_protobuf.sh         # Code generation script
├── calculator.proto             # Calculator service definition
└── generated/                   # Generated C++ code (auto-generated)
    ├── calculator.pb.h
    └── calculator.pb.cc
```

## License

Copyright (c) 2025 LightAP Team
