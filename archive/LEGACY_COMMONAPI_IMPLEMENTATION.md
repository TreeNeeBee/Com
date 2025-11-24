# 遗留实现文档 - CommonAPI 相关内容

> **⚠️ 已废弃**: 此文档包含已废弃的 CommonAPI 实现细节，仅作为历史参考保留。
> 
> **当前架构**: 已迁移到插件化架构 (iceoryx2 + DDS + Custom Protocol + Legacy Gateway)
> 
> **归档日期**: 2025-11-19

---

## CommonAPI Adapters (已废弃)

### 组件列表

| 组件 | 功能 | 代码量 |
|------|------|--------|
| `CommonAPIDBusAdapter.hpp` | D-Bus适配器 | ~350行 |
| `CommonAPISomeIpAdapter.hpp` | SOME/IP适配器 | ~420行 |

**总计**: ~770行，桥接CommonAPI生成代码与LightAP API

### 旧架构代码组织

```
modules/Com/
├── source/
│   ├── inc/                      # 公共API (8个头文件, ~3,140行)
│   └── binding/                  # 传输绑定
│       ├── dbus/                 # D-Bus (4个文件, ~950行)
│       ├── someip/               # SOME/IP (使用 CommonAPI)
│       └── commonapi/            # CommonAPI 适配器 (已废弃)
│           ├── CommonAPIDBusAdapter.hpp
│           └── CommonAPISomeIpAdapter.hpp
└── tools/
    ├── fidl/                     # Franca IDL定义
    └── someip/                   # SOME/IP 工具链
        ├── commonapi_generator_wrapper.sh
        └── franca_to_autosar.py

旧总计: 41个文件, ~10,790行代码（包含 CommonAPI ~770行）
```

---

## SOME/IP 序列化 (CommonAPI 方式 - 已废弃)

### 处理方式

由 CommonAPI 代码生成器**自动生成**序列化代码

```cpp
// 1. 定义Franca IDL
// VehicleSpeed.fidl
struct VehicleSpeed {
    Float current
    Float average
    UInt32 timestamp
}

// 2. 运行代码生成器
$ commonapi-someip-generator -sk VehicleSpeed.fidl

// 3. 生成的代码 (自动，无需手写)
namespace CommonAPI {
namespace SomeIP {
    // 自动生成的序列化器
    template<>
    struct Serializer<VehicleSpeed> {
        static void serialize(OutputStream& output, const VehicleSpeed& value) {
            output << value.current << value.average << value.timestamp;
        }
        
        static void deserialize(InputStream& input, VehicleSpeed& value) {
            input >> value.current >> value.average >> value.timestamp;
        }
    };
}
}
```

### 应用层使用示例

```cpp
// 发送端 - 无需手动序列化
VehicleSpeed speed{100.5f, 95.3f, 123456};
myProxy->getSpeedAttribute().setValue(speed);  // 自动序列化

// 接收端 - 自动反序列化
myProxy->getSpeedAttribute().getChangedEvent().subscribe([](const VehicleSpeed& speed) {
    // speed已经是反序列化后的对象
    std::cout << "Speed: " << speed.current << std::endl;
});
```

### 特点

- ✅ IDL驱动: 从Franca IDL生成一切
- ✅ 零手动代码: 序列化逻辑完全自动生成
- ✅ 协议兼容: `.fdepl`文件定义SOME/IP协议映射
- ✅ 高性能: 生成的代码经过优化

### 底层机制

```
Franca IDL → CommonAPI Generator → C++ Serializer/Deserializer
                                          ↓
                                    SOME/IP Wire Format
```

---

## 旧序列化流程对比

| 传输层 | 序列化实现 | 开发者工作量 | 性能 |
|--------|-----------|-------------|------|
| **D-Bus** | sdbus-c++自动 | 0 (只需定义struct) | 高 |
| **SOME/IP** (旧) | CommonAPI生成 | 0 (只需写.fidl) | 极高 |
| **SOME/IP** (新) | 独立网关进程 | 0 (网关内部处理) | 中 |

---

## 迁移到新架构

### 旧架构 (CommonAPI)

```
ara::com API
    ↓
CommonAPI Runtime
    ↓
CommonAPI-SomeIP Binding
    ↓
vsomeip3
    ↓
SOME/IP Protocol
```

### 新架构 (Legacy Gateway)

```
ara::com API
    ↓
binding_legacy.so (转发接口)
    ↓
Unix Domain Socket
    ↓
SomeIpGateway (独立进程)
    ├→ vsomeip3 协议栈
    └→ DDS 双向转换
```

### 迁移优势

1. **完全隔离**: 网关进程崩溃不影响主应用
2. **简化依赖**: 主应用无需链接 CommonAPI/vsomeip
3. **灵活部署**: 网关可选部署，不需要时不启动
4. **协议无感知**: 应用只看到标准 ara::com API

---

## CommonAPI 工具链 (历史参考)

### 代码生成流程

```bash
# 1. Franca IDL → CommonAPI C++ 代码
commonapi-core-generator -sk VehicleSpeed.fidl

# 2. Franca IDL + FDEPL → SOME/IP C++ 代码  
commonapi-someip-generator -sk VehicleSpeed.fidl VehicleSpeed.fdepl

# 3. 编译生成的代码
g++ -std=c++17 \
    -I/usr/include/CommonAPI-3.2 \
    -I/usr/include/CommonAPI-SomeIP-3.2 \
    -lCommonAPI -lCommonAPI-SomeIP -lvsomeip3 \
    generated/*.cpp app.cpp -o app
```

### FDEPL 配置示例

```fidl
// VehicleSpeed.fdepl
import "VehicleSpeed.fidl"

define org.example.someip for typeCollection org.example.VehicleSpeed {
    struct VehicleSpeed {
        Float current {
            SomeIpByteOrder: BigEndian
        }
        Float average {
            SomeIpByteOrder: BigEndian
        }
        UInt32 timestamp {
            SomeIpByteOrder: BigEndian
        }
    }
}

define org.example.someip for interface org.example.SpeedService {
    SomeIpServiceID: 0x1234
    SomeIpInstanceID: 0x5678
    
    method getSpeed {
        SomeIpMethodID: 0x0001
        SomeIpReliable: true
    }
    
    attribute speed {
        SomeIpGetterID: 0x0002
        SomeIpSetterID: 0x0003
        SomeIpNotifierID: 0x8002
    }
}
```

---

## 废弃原因总结

1. **复杂性**: CommonAPI 引入了 Franca IDL + FDEPL 双层配置
2. **工具链**: 需要维护多个代码生成器版本兼容性
3. **耦合度**: 应用与 CommonAPI 运行时紧密耦合
4. **故障隔离**: SOME/IP 问题会影响整个应用
5. **部署灵活性**: 无法可选部署 SOME/IP 支持

### 新架构优势

- ✅ 插件化: 运行时动态加载 .so
- ✅ 配置驱动: JSON 配置控制启用哪些协议
- ✅ 故障隔离: 独立网关进程
- ✅ 应用透明: 100% 标准 ara::com API
- ✅ 可选部署: 不需要遗留协议时不启动网关

---

**文档版本**: 1.0 (归档版本)  
**归档日期**: 2025-11-19  
**替代文档**: `ARCHITECTURE_SUMMARY.md` (当前架构设计)
