# lap-sidl-gen — Franca IDL 代码生成器架构文档

> **工具版本**: v1.0.0
> **位置**: `modules/Com/generator/`
> **构建依赖**: 零外部依赖，仅需 C++17 标准库
> **生成日期**: 2026-02-23（基于源码自动梳理）
> **注意**: 本文件为 `GENERATOR.md` 的备份副本（因文件系统别名约束，GENERATOR.md 与 test_fidl_parser.cpp 共享内容）

---

## 目录

| §  | 章节 | 描述 |
|----|------|------|
| §1  | 概述           | 工具定位与架构设计原则 |
| §2  | 目录结构       | 源码目录组织 |
| §3  | 构建与安装     | 编译与安装命令 |
| §4  | CLI 使用       | 完整命令行参数说明 |
| §5  | 处理流水线     | 从 .fidl 到生成文件的数据流 |
| §6  | Franca IDL AST | 抽象语法树节点定义 |
| §7  | 词法分析器     | CFidlLexer — Token 类型与扫描规则 |
| §8  | 语法分析器     | CFidlParser — BNF 文法与递归下降 |
| §9  | Schema Hash    | CSchemaHash — 确定性 ServiceID 生成 |
| §10 | 生成器详细设计 | 4 个生成器的职责与类型映射 |
| §11 | 双层 IDL 设计  | YAML 配置文件与 QoS 三级解析 |
| §12 | Franca IDL 示例 | Calculator.fidl 完整内容 |
| §13 | 测试覆盖       | 单元测试 + CLI 集成测试 |
| §14 | AUTOSAR 标准追溯 | SWS_CM 需求覆盖率矩阵 |
| §15 | R25-11 差距分析 | 缺失特性清单与优先级规划 |
| §16 | 相关文档       | 外部参考与关联架构文档 |

---

## §1 概述

`lap-sidl-gen` 是 LightAP Communication Management 模块的 **Franca IDL 代码生成工具**。
它以一个 `.fidl` 文件为唯一真值源（SSOT），分两层生成通信中间件所需的全部代码和配置：

```
Franca IDL (.fidl)          <- 唯一真值源
        |
        v
+-------------------------------------------------------------------+
|  Layer 1: C++17 Ara::Com API（类型安全的应用层接口）              |
|    <Interface>_types.hpp   — 枚举、结构体、typedef                |
|    <Interface>_proxy.hpp   — Proxy 类（客户端）                   |
|    <Interface>_skeleton.hpp — Skeleton 类（服务端）               |
+-------------------------------------------------------------------+
|  Layer 2: OMG IDL + DDS QoS XML（传输层绑定）                    |
|    <Interface>.idl         — OMG IDL 3.5 主题类型定义             |
|    <Interface>_qos.xml     — DDS QoS Profile (XML)                |
+-------------------------------------------------------------------+
```

**设计原则**：

1. 零外部依赖 — 所有文件解析（FIDL 词法 + YAML 子集）均使用 C++17 标准库实现
2. 单二进制 — 发布为独立可执行文件 `lap-sidl-gen`，便于 CI 集成
3. 确定性 — 相同的 `.fidl` 输入始终产生相同的 SchemaHash 和 ServiceID
4. 无运行时依赖 — 生成代码仅依赖 `ara::core` 和 `ara::com` 头文件

---

## §2 目录结构

```
modules/Com/generator/
├── CMakeLists.txt              # 构建定义
├── inc/                        # 公共头文件
│   ├── CFidlAst.hpp            # AST 节点定义（所有生成器共用）
│   ├── CFidlLexer.hpp          # 词法分析器
│   ├── CFidlParser.hpp         # 语法分析器
│   ├── CSchemaHash.hpp         # Schema Hash + ServiceID 生成
│   ├── IGenerator.hpp          # 生成器抽象接口 + CCodeWriter + 工具函数
│   ├── CTypesGenerator.hpp     # Layer1: C++ 类型头文件生成器
│   ├── CProxyGenerator.hpp     # Layer1: Proxy 类生成器
│   ├── CSkeletonGenerator.hpp  # Layer1: Skeleton 类生成器
│   ├── CDdsIdlGenerator.hpp    # Layer2: OMG IDL + DDS QoS XML 生成器
│   └── CQosLoader.hpp          # YAML QoS 配置加载器
├── src/                        # 实现文件
│   ├── CFidlLexer.cpp
│   ├── CFidlParser.cpp
│   ├── CSchemaHash.cpp
│   ├── CTypesGenerator.cpp
│   ├── CProxyGenerator.cpp
│   ├── CSkeletonGenerator.cpp
│   ├── CDdsIdlGenerator.cpp
│   ├── CQosLoader.cpp
│   └── main.cpp
└── test/
    ├── Calculator.fidl         # 测试用 Franca IDL 示例
    ├── test_fidl_parser.cpp    # 4 个库级单元测试
    └── test_cli_modes.cpp      # 20 个 CLI 集成测试
```

---

## §3 构建与安装

```bash
cd modules/Com/generator
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
cmake --install build --prefix /usr/local  # 可选
```

构建产物：

| 产物 | 说明 |
|------|------|
| `build/lap-sidl-gen` | 可执行二进制 |
| `build/lib_generator.a` | 静态库（供单元测试链接） |

---

## §4 CLI 使用

```
lap-sidl-gen [OPTIONS]

必需:
  -i, --input  <file>         输入的 .fidl 文件路径

可选 - 输出控制:
  -o, --output <dir>          输出目录（默认: 当前目录）
  --proxy                     生成 <Interface>Proxy.hpp
  --skeleton                  生成 <Interface>Skeleton.hpp
  --types                     生成 <Interface>Types.hpp
  --dds-idl                   生成 <Interface>.idl + <Interface>_qos.xml
  --all                       等价于 --proxy --skeleton --types --dds-idl

可选 - 元数据:
  -n, --namespace <prefix>    命名空间前缀（默认: "lap::com"）
  --author <name>             生成文件的 @author（默认: "Aii"）
  --version-string <ver>      覆盖 OMG IDL 中的版本注释

可选 - Schema Hash:
  --hash-only                 仅打印 SchemaHash，不生成文件
  --schema-hash <hex>         注入外部 SchemaHash（跳过自动计算）
  --service-id <id>           覆盖 ServiceID

可选 - QoS 配置:
  --com-config    <file>      com_config.yaml 路径
  --service-deploy <file>     service_deploy.yaml 路径
  --slot-mapping  <file>      slot_mapping.yaml 路径（可选）
  --instance-id   <id>        覆盖 instanceId

可选 - 诊断:
  --validate                  仅解析语法，不生成文件（CI lint）
  -h, --help                  显示帮助并退出
  -v, --version               显示版本并退出
```

常用示例：

```bash
# 验证语法
lap-sidl-gen -i Calculator.fidl --validate

# 生成全部文件
lap-sidl-gen -i Calculator.fidl -o gen/ --all

# 带 QoS 配置
lap-sidl-gen -i Calculator.fidl -o gen/ --all \
    --com-config config/com_config.yaml \
    --service-deploy config/service_deploy.yaml
```

---

## §5 处理流水线

```
.fidl 文件
    |
    v  CFidlLexer::Tokenize()
    |  输出: vector<Token>
    v  CFidlParser::Parse()
    |  输出: FidlModel (AST)
    v  CSchemaHash::Compute()
    |  输出: schemaHash (16 hex), serviceId (1..1022)
    |
    +-- CTypesGenerator    --> <Interface>Types.hpp
    +-- CProxyGenerator    --> <Interface>Proxy.hpp
    +-- CSkeletonGenerator --> <Interface>Skeleton.hpp
    +-- CDdsIdlGenerator   --> <Interface>.idl + <Interface>_qos.xml
```

`CQosLoader` 在 `CDdsIdlGenerator` 运行前解析 YAML QoS 参数。

---

## §6 Franca IDL AST

### 6.1 类型别名（自包含，不依赖 lap::core）

```cpp
using Bool       = bool;
using Char       = char;
using Int32      = ::std::int32_t;
using UInt8      = ::std::uint8_t;
using UInt16     = ::std::uint16_t;
using UInt32     = ::std::uint32_t;
using UInt64     = ::std::uint64_t;
using String     = ::std::string;
using StringView = ::std::string_view;
template< typename T > using UniquePtr = ::std::unique_ptr< T >;
template< typename T > using SharedPtr = ::std::shared_ptr< T >;
```

### 6.2 AST 主要节点

| 节点类型 | 描述 |
|----------|------|
| `Version` | major.minor.patch 版本声明 |
| `TypeRef` | 类型引用（支持点分限定名，支持数组） |
| `Field` | 结构体成员 / 方法参数 |
| `Enumerator` | 枚举值（名称 + 数值） |
| `EnumDecl` | Franca `enumeration` 声明 |
| `StructDecl` | Franca `struct` 声明 |
| `TypedefDecl` | Franca `typedef` 声明 |
| `MethodArg` | 方法的 in/out/error 参数 |
| `MethodDecl` | Franca `method`（含 `isFireAndForget` 标记） |
| `BroadcastDecl` | Franca `broadcast`（事件） |
| `AttributeDecl` | Franca `attribute`（含 `isReadonly` 标记） |
| `TypeCollection` | Franca `typeCollection` |
| `InterfaceDecl` | Franca `interface` |
| `FidlModel` | 顶层模型（packageName + typeCollections + interfaces） |

### 6.3 GeneratorConfig

```cpp
struct GeneratorConfig {
    String outputDir          = "./generated";
    String namespacePrefix    = "lap::com";
    String author             = "Aii";
    String schemaHashOverride;      // 非空时覆盖自动计算值
    String versionOverride;         // 非空时注入 OMG IDL 版本注释
    String comConfigPath;
    String serviceDeployPath;
    String slotMappingPath;
    Bool   generateProxy    = true;
    Bool   generateSkeleton = true;
    Bool   generateTypes    = true;
    Bool   generateDdsIdl   = false;
    Bool   headerOnly       = true;
    UInt16 serviceIdOverride  = 0;   // 0 = 自动从 hash 生成
    UInt16 instanceIdOverride = 0;   // 0 = 自动（默认 1）
};
```

---

## §7 词法分析器

### TokenType 枚举

```cpp
enum class TokenType : UInt8 {
    kPackage, kTypeCollection, kInterface, kVersion,
    kMethod, kBroadcast, kAttribute,
    kIn, kOut, kError, kFireAndForget, kReadonly,
    kEnumeration, kStruct, kTypedef, kArray, kOf, kIs,
    kMajor, kMinor,
    kIdentifier, kNumber, kString,
    kLBrace, kRBrace, kDot, kEqual,
    kEof
};
```

### CFidlLexer API

```cpp
// 构造: 传入源文本 + 文件名（用于错误报告）
explicit CFidlLexer( const String& source, const String& filename = "" ) noexcept;

// 词法化: 返回 Token 列表，最后一个为 kEof
::std::vector< Token > Tokenize();
```

---

## §8 语法分析器

### BNF 文法摘要

```
Model         ::= PackageDecl (TypeCollection | Interface)*
TypeCollection::= 'typeCollection' Identifier '{' VersionDecl TypeDef* '}'
Interface     ::= 'interface' Identifier '{' VersionDecl Member* '}'
EnumDecl      ::= 'enumeration' Identifier '{' Enumerator+ '}'
StructDecl    ::= 'struct' Identifier '{' Field+ '}'
TypedefDecl   ::= 'typedef' Identifier 'is' 'array' 'of' TypeRef
MethodDecl    ::= 'method' Identifier ('fireAndForget')? '{' InBlock? OutBlock? ErrorBlock? '}'
BroadcastDecl ::= 'broadcast' Identifier '{' OutBlock '}'
AttributeDecl ::= 'attribute' TypeRef Identifier ('readonly')?
```

### CFidlParser API

```cpp
// 构造: 传入 Token 列表 + 文件名
explicit CFidlParser( const ::std::vector< Token >& tokens,
                      const String& filename = "" ) noexcept;

// 解析: 递归下降，返回 FidlModel；出错抛 ParserError
FidlModel Parse();
```

---

## §9 Schema Hash

### CSchemaHash API

```cpp
// 计算接口的 SHA-256 schema hash (返回 16 字符 hex 字符串)
static String Compute( const FidlModel& model ) noexcept;

// 从完全限定服务名（如 "org.lap.examples.Calculator"）生成 ServiceID
// 输出: [1, 1022]（规避 DDS 保留 ID）
static UInt16 GenerateServiceId( const String& qualifiedName ) noexcept;
```

### 算法

```
1. 将接口名、版本、方法/事件/属性签名序列化为规范字符串
2. SHA-256(规范字符串) -> 取前 8 字节 -> 16 字符 hex (kSchemaHash)
ServiceID = (FNV-1a32(qualifiedName) % 1022) + 1
```

---

## §10 生成器详细设计

### IGenerator 接口

```cpp
class IGenerator {
public:
    virtual Bool Generate( const FidlModel& model,
                           const GeneratorConfig& config ) = 0;
};
```

### CCodeWriter

| 方法 | 说明 |
|------|------|
| `Line(text)` | 写出一行（自动加缩进和换行） |
| `Indent()` / `Dedent()` | 缩进级别 +1/-1（4 空格） |
| `GetOutput()` | 返回累积字符串 |
| `WriteToFile(path)` | 写入文件，返回 bool |

### Franca -> C++ 类型映射

| Franca | C++ |
|--------|-----|
| UInt8..UInt64 | UInt8..UInt64 |
| Int8..Int64 | Int8..Int64 |
| Float/Float32 | Float |
| Float64/Double | Double |
| Boolean | Bool |
| String | String |
| ByteBuffer | ::std::vector< UInt8 > |

### Franca -> OMG IDL 类型映射

| Franca | OMG IDL |
|--------|---------|
| UInt8 | octet |
| UInt16 | unsigned short |
| UInt32 | unsigned long |
| UInt64 | unsigned long long |
| Int8 | char |
| Int16 | short |
| Int32 | long |
| Int64 | long long |
| Float/Float32 | float |
| Float64/Double | double |
| Boolean | boolean |
| String | string |
| ByteBuffer | sequence\<octet\> |

### 输出文件命名约定

| 生成器 | 输出文件 |
|--------|---------|
| CTypesGenerator | `<Interface>Types.hpp` |
| CProxyGenerator | `<Interface>Proxy.hpp` |
| CSkeletonGenerator | `<Interface>Skeleton.hpp` |
| CDdsIdlGenerator | `<Interface>.idl` + `<Interface>_qos.xml` |

---

## §11 双层 IDL 设计

### YAML 配置文件体系

```
com_config.yaml       <- 命名 QoS Profile 库
service_deploy.yaml   <- 每个接口的 instanceId + 元素绑定
slot_mapping.yaml     <- 可选: SOME/IP Slot 映射
```

### QoS 内置默认值

| ElementKind | reliability | durability | history | depth | timeout_ms |
|-------------|-------------|------------|---------|-------|-----------|
| kEvent | BEST_EFFORT | VOLATILE | KEEP_LAST | 1 | 0 |
| kMethod | RELIABLE | VOLATILE | KEEP_LAST | 10 | 5000 |
| kFireAndForget | BEST_EFFORT | VOLATILE | KEEP_LAST | 1 | 0 |
| kField | RELIABLE | TRANSIENT_LOCAL | KEEP_LAST | 1 | 0 |

### QoS 三级解析优先级

```
优先级 1 (最高): service_deploy.yaml per-element 直接覆盖字段
优先级 2:         service_deploy.yaml 中引用的命名 Profile (来自 com_config.yaml)
优先级 3 (最低): 内置默认值（见上表）
```

### com_config.yaml 格式

```yaml
qos_profiles:
  - name: reliable_event
    reliability: RELIABLE
    durability: TRANSIENT_LOCAL
    history: KEEP_LAST
    history_depth: 10
  - name: method_call
    reliability: RELIABLE
    durability: VOLATILE
    history_depth: 10
    timeout_ms: 5000
    retry_count: 0
```

### service_deploy.yaml 格式

```yaml
services:
  - interface: Calculator
    instance_id: 1
    elements:
      - name: add
        kind: method
        profile: method_call
      - name: resultReady
        kind: event
        reliability: BEST_EFFORT
        history_depth: 1
      - name: lastResult
        kind: field
        reliability: RELIABLE
        durability: TRANSIENT_LOCAL
```

---

## §12 Franca IDL 示例 (Calculator.fidl)

```fidl
package org.lap.examples

typeCollection CalculatorTypes
{
    version { major 1 minor 0 }

    enumeration ErrorCode
    {
        OK = 0
        DIVIDE_BY_ZERO = 1
        OVERFLOW = 2
        INVALID_INPUT = 3
    }

    struct Operand
    {
        Float64 value
        String  label
    }

    struct OperationResult
    {
        Float64 result
        ErrorCode status
    }

    typedef OperandList is array of Operand
}

interface Calculator
{
    version { major 1 minor 0 }

    method add
    {
        in  { Float64 a   Float64 b  }
        out { Float64 sum            }
    }

    method subtract
    {
        in  { Float64 a   Float64 b           }
        out { Float64 difference               }
    }

    method divide
    {
        in  { Float64 dividend   Float64 divisor }
        out { Float64 quotient   CalculatorTypes.ErrorCode status }
        error CalculatorTypes.ErrorCode
    }

    method reset fireAndForget { }

    broadcast resultReady
    {
        out
        {
            CalculatorTypes.OperationResult result
            UInt64 timestamp
        }
    }

    attribute Float64 lastResult readonly
}
```

---

## §13 测试覆盖

### 测试 1: FidlParserTest (test_fidl_parser.cpp) — 4 个子测试

| # | 函数 | 覆盖范围 |
|---|------|---------|
| 1 | TestLexer | CFidlLexer::Tokenize — Token 数>10，kEof，kPackage/kInterface/kMethod |
| 2 | TestParser | CFidlParser::Parse — packageName, typeCollections, interfaces, ErrorCode (4值), structs, typedef, 4方法, broadcast (2 outArgs), attribute (readonly) |
| 3 | TestSchemaHash | CSchemaHash::Compute (确定性，16 hex chars) + GenerateServiceId ([1,1022]) |
| 4 | TestGenerators | 4 个生成器运行，验证文件存在及内容 (enum class ErrorCode, kSchemaHash, skeleton, SCHEMA_HASH) |

### 测试 2: CliModesTest (test_cli_modes.cpp) — 20 个子测试

| 分组 | 数量 |
|------|------|
| 基本模式 (--help/--version/--validate/--hash-only) | 4 |
| 单生成器 (--types/--proxy/--skeleton/--dds-idl) | 4 |
| 组合模式 (--all, 组合) | 3 |
| QoS 配置 (--com-config, --service-deploy, 组合) | 3 |
| 错误处理 (缺少参数, 不存在文件, 未知选项) | 3 |
| 元数据 (--author, --schema-hash, --version-string) | 3 |

---

## §14 AUTOSAR 标准追溯（基于 R25-11）

| 需求 ID | 描述 | 状态 |
|---------|------|------|
| [SWS_CM_00002] | Skeleton 类生成 | ✅ |
| [SWS_CM_00003] | Proxy 类生成 | ✅ |
| [SWS_CM_00004] | 方法调用（request-response） | ✅ |
| [SWS_CM_00005] | FireAndForget 方法 | ✅ |
| [SWS_CM_00006] | Broadcast 事件 | ✅ |
| [SWS_CM_00007] | Attribute（字段） | ✅ |
| [SWS_CM_00721] | Trigger::Send（**R22-11 新增**） | ❌ §15 G1 |
| [SWS_CM_01073] | GetServiceState()（**R23-11 新增**） | ❌ §15 G2 |
| [SWS_CM_11506] | serviceIdentifier: ServiceIdentifierType | ⚠️ 使用 UInt16 |
| [SWS_CM_11507] | serviceVersion: ServiceVersionType | ❌ §15.3 |
| [SWS_CM_11508] | serviceContractVersionMajor 命名 | ⚠️ 命名差异 |
| [SWS_CM_11509] | serviceContractVersionMinor 命名 | ⚠️ 命名差异 |
| [SWS_LBAP_00065] | Bitfield 类型（**R25-11 新增**） | ❌ §15 G3 |

---

## §15 R25-11 差距分析

### 特性缺失清单

| ID | 特性 | 首次引入版本 | 优先级 |
|----|------|-------------|--------|
| G1 | `Trigger::Send` — Skeleton 侧主动推送 | **R22-11** | P0 |
| G2 | `GetServiceState` / `SetServiceState` / `UnsetServiceState` | **R23-11** | P1 |
| G3 | Bitfield 类型（`bitfield` 关键字） | **R25-11** | P1 |
| G4 | InstanceSpecifier 完整支持 | R24-11 | P1 |
| G5 | `kCommunicationFailure` 错误码 | **R25-11** | P1 |
| G6 | E2E 保护端到端集成 | R24-11 | P2 |
| G7 | SOME/IP 传输适配 | R24-11 | P2 |

### SWS_CM_11506/11507 类型差异

| SWS 要求 | 当前实现 | 差异 |
|----------|---------|------|
| `serviceIdentifier`: `ServiceIdentifierType` | `UInt16 serviceId` | 缺强类型别名 |
| `serviceVersion`: `ServiceVersionType` | 未生成 | 缺失 |
| `serviceContractVersionMajor` | `major` / `minor` | 命名不符 |

### 订阅状态（R24-11 新增）

`kSubscriptionPending` 在 **R24-11** 中新增：`GetSubscriptionState()` 返回类型需更新。

---

## §16 相关文档

| 文档 | 路径 |
|------|------|
| Com 模块整体架构 | `modules/Com/docs/architecture/ARCHITECTURE_SUMMARY.md` |
| 服务发现架构 | `modules/Com/docs/architecture/SERVICE_DISCOVERY_ARCHITECTURE.md` |
| R25-11 差距分析（详细） | `modules/Com/docs/GENERATOR_R25_11_GAP_ANALYSIS.md` |
| AI 代码规范 | `docs/AI/code_rules.md` |
| AI 工作规则 | `docs/AI/rules.md` |
| R25-11 规范哈希 | `docs/R25-11/AUTOSAR_AP_TR_SpecificationHashes.sha256` |

---

*最后更新: 2026-02-23 | 版本: v1.0.0 | 标准基线: AUTOSAR AP R25-11*
