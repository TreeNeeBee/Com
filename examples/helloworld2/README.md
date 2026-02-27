# HelloWorld2 Example

## Overview

HelloWorld2 demonstrates the **standard AUTOSAR AP development flow** using the
`lap-gen-pipeline` code generator.  It covers every communication pattern
supported by the Communication Management API:

| Pattern       | Elements                                          |
|---------------|---------------------------------------------------|
| Methods       | `SayHello` (sync), `Add` (sync), `NotifyLog` (F&F), `ComputeHash` (async) |
| Events        | `Greeting` (text), `StatusChanged` (enum), `DataStream` (binary) |
| Fields        | `VisitorCount` (readonly getter), `ServerName` (rw), `Temperature` (rw) |

---

## Standard Development Flow

```
Step 1  Define interface               HelloWorld2.fidl
        ──────────────────────────────────────────────────────────────
Step 2  Run generator pipeline         gen_pipeline.py
           coreipc → Skeleton/Proxy + coreipc_config.yaml
           dds     → OMG IDL + QoS XML (+ fastddsgen if installed)
        ──────────────────────────────────────────────────────────────
Step 3  Implement server               helloworld2_server.cpp
           - #include "gen/HelloWorld2ServiceSkeleton.hpp"
           - RegisterMethodHandler / RegisterGetHandler / RegisterSetHandler
           - OfferService(), event loop, Send()
        ──────────────────────────────────────────────────────────────
Step 4  Implement client               helloworld2_client.cpp
           - #include "gen/HelloWorld2ServiceProxy.hpp"
           - FindService(), Proxy::Create()
           - Subscribe() + SetReceiveHandler()
           - Method calls, field Get()/Set()
```

---

## Directory Structure

```
helloworld2/
├── HelloWorld2.fidl               # Service interface definition (source of truth)
│
├── gen/                           # AUTO-GENERATED — do not edit manually
│   ├── HelloWorld2ServiceTypes.hpp
│   ├── HelloWorld2ServiceProxy.hpp
│   ├── HelloWorld2ServiceSkeleton.hpp
│   ├── HelloWorld2Service.idl          # OMG IDL 4.2 for DDS transport
│   ├── HelloWorld2Service_qos.xml      # eProsima Fast DDS QoS profiles
│   └── HelloWorld2Service_coreipc_config.yaml
│
├── helloworld2_server.cpp         # Server (uses generated Skeleton)
├── helloworld2_client.cpp         # Client (uses generated Proxy)
├── helloworld2_test.cpp           # Integration test (uses generated Proxy)
├── run_helloworld2_test.sh        # Test orchestration script
└── README.md                      # This file
```

---

## Regenerating the Code

Run from the repository root:

```bash
python3 modules/Com/tools/gen_pipeline.py \
    -i modules/Com/examples/helloworld2/HelloWorld2.fidl \
    -o modules/Com/examples/helloworld2/gen \
    --backend all \
    --sidl-gen modules/Com/generator/build/lap_sidl_gen
```

`--backend all` runs both the **coreipc** and **dds** pipelines in sequence.
The DDS step also invokes `fastddsgen` automatically if it is present in PATH.

---

## How the Skeleton is Used (server-side)

```cpp
#include "gen/HelloWorld2ServiceSkeleton.hpp"
using namespace helloworld2::skeleton;

// 1. Create skeleton
HelloWorld2ServiceSkeleton skeleton(
    lap::core::InstanceSpecifier("HelloWorld2/Provider"));

// 2. Register typed method handlers
skeleton.sayHello.RegisterMethodHandler(
    [](String visitorName) -> lap::core::Future<String> {
        return MakeReadyFuture<String>("Hello, " + visitorName + "!");
    });

skeleton.notifyLog.RegisterMethodHandler(
    [](String msg) {                          // fire-and-forget: no return
        std::cout << "LOG: " << msg << "\n";
    });

// 3. Register field handlers
skeleton.visitorCount.RegisterGetHandler(
    []() -> lap::core::Future<UInt32> { return MakeReadyFuture<UInt32>(42u); });

// 4. Offer service
skeleton.OfferService();

// 5. Send events
auto sample = skeleton.greeting.Allocate();
sample.Value()->text = "Hello!";
skeleton.greeting.Send(std::move(sample).Value());
```

---

## How the Proxy is Used (client-side)

```cpp
#include "gen/HelloWorld2ServiceProxy.hpp"
using namespace helloworld2::proxy;

// Service discovery + proxy construction
auto proxy = HelloWorld2ServiceProxy::Create(handle).Value();

// Subscribe to events
proxy.greeting.Subscribe();
proxy.greeting.SetReceiveHandler([&] {
    auto s = proxy.greeting.GetNextSample();
    if (s.HasValue() && s.Value())
        std::cout << s.Value()->text << "\n";
});

// Call methods (typed — no manual serialization)
auto r = proxy.sayHello(String("Alice"));    // -> Result<String>
auto r2 = proxy.add(UInt32(42), UInt32(8)); // -> Result<UInt32>
proxy.notifyLog(String("hi"));              // fire-and-forget

// Field access
auto count = proxy.visitorCount.Get();      // -> Result<UInt32>
proxy.serverName.Set(String("New Name"));   // -> Result<void>
```

---

## Key Differences from HelloWorld1

| Aspect           | HelloWorld1 (helloworld/)          | HelloWorld2 (helloworld2/)                       |
|------------------|------------------------------------|--------------------------------------------------|
| Transport        | CoreIPC (shared memory)            | CoreIPC + DDS IDL generated                      |
| Event payload    | `GreetingEvent { GreetingMessage }` | Direct `GreetingEvent { String text }`          |
| Type collection  | `HelloWorldTypes` (struct wrapping) | `HelloWorld2Types` (flat enum + struct)          |
| Field notify     | Temperature has `.Update()` notify  | Temperature has no notification (FIDL note)     |
| Generator cmd    | `lap_sidl_gen --all`               | `gen_pipeline.py --backend all`                  |

---

## Running the Example

```bash
# Terminal 1 — start server
./helloworld2_server

# Terminal 2 — start client (after server is ready)
./helloworld2_client

# Or run the full test suite
./run_helloworld2_test.sh
```
