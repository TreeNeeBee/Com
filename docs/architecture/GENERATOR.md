# lap-sidl-gen — Franca IDL 代码生成器架构文档

> **工具版本**: v1.0.0  
> **位置**: `modules/Com/generator/`  
> **构建依赖**: 零外部依赖，仅需 C++17 标准库  
> **生成日期**: 2026-02-21（基于源码自动梳理）

---

## 目录

| 章节 | 内容 | 行数 |
|------|------|------|
| [§1 概述](#1-概述) | 双层 IDL 数据流、定位 | ~30 |
| [§2 目录结构](#2-目录结构) | generator/ 文件布局 | ~30 |
| [§3 构建与安装](#3-构建与安装) | CMake 构建命令 | ~15 |
| [§4 CLI 使用](#4-cli-使用) | 运行模式、选项、示例、生产工作流 | ~100 |
| [§5 处理流水线](#5-处理流水线) | Lexer → Parser → Hash → Generators | ~15 |
| [§6 Franca IDL AST](#6-franca-idl-ast-cfidlasthpp) | AST 节点、类型别名、GeneratorConfig | ~60 |
| [§7 词法分析器](#7-词法分析器-cfidllexer) | Token 类型、词法规则 | ~40 |
| [§8 语法分析器](#8-语法分析器-cfidlparser) | BNF 语法、递归下降特性 | ~50 |
| [§9 Schema Hash](#9-schema-hash-cschemahash) | SHA-256 计算、Service ID、覆盖机制 | ~30 |
| [§10 生成器详细设计](#10-生成器详细设计) | 4 个生成器、类型映射、OMG IDL 示例 | ~120 |
| [§11 双层 IDL 设计](#11-双层-idl-设计) | 架构对比、YAML 配置体系、默认配置、QoS 附加生成 | ~385 |
| [§12 Franca IDL 示例](#12-franca-idl-示例) | Calculator.fidl 完整示例 | ~60 |
| [§13 测试覆盖](#13-测试覆盖) | 4 + 16 个测试用例 | ~50 |
| [§14 AUTOSAR 标准追溯](#14-autosar-标准追溯) | SWS_CM 需求映射表 (含 R25-11 合规状态) | ~35 |
| [§15 R25-11 差距分析](#15-r25-11-合规差距分析与重构规划) | 7 项新特性差距、命名合规、重构路线图 | ~150 |
| [§16 相关文档](#16-相关文档) | 交叉引用链接 | ~10 |

---

## 1. 概述

`lap-sidl-gen` 是一个自包含的 C++ 代码生成器，取代原有的 Python PyFranca 工具链，实现 **Franca IDL → AUTOSAR AP C++ 代码** 的完整流水线。该工具是 Com 模块双层 IDL 架构的核心组件：

- **Franca IDL** 作为 SSOT (Single Source of Truth)
- 生成 **AUTOSAR ara::com API** 头文件（应用层）
- 生成 **OMG IDL v4.2**（传输层标准格式，可由 FastDDS-gen / OpenDDS / CycloneDDS 等任意 OMG IDL 编译器处理）
- 内置 **Schema Hash** 强制版本一致性验证

### 双层 IDL 数据流

```
  ┌──────────────┐
  │  .fidl 文件   │  Franca IDL (SSOT)
  └──────┬───────┘
         │
    lap-sidl-gen
         │
  ┌──────┴──────────────────────────┐
  │              │                  │
  ▼              ▼                  ▼
Types.hpp    Proxy.hpp /        <Interface>.idl
             Skeleton.hpp       (OMG IDL v4.2)
  │              │                  │
  ▼              ▼                  ▼
应用层 API    ara::com 服务      OMG IDL 编译器
(类型+序列化)  (Proxy/Skeleton)   (FastDDS-gen 等)
                                → TypeSupport.hpp
```

---

## 2. 目录结构

```
modules/Com/generator/
├── CMakeLists.txt              # 构建脚本 (独立构建)
├── inc/
│   ├── CFidlAst.hpp            # AST 节点定义
│   ├── CFidlLexer.hpp          # 词法分析器
│   ├── CFidlParser.hpp         # 语法分析器 (递归下降)
│   ├── CSchemaHash.hpp         # Schema Hash (SHA-256 + FNV-1a)
│   ├── IGenerator.hpp          # 生成器接口 + CCodeWriter + 工具函数
│   ├── CTypesGenerator.hpp     # Types 头文件生成器
│   ├── CProxyGenerator.hpp     # Proxy 头文件生成器
│   ├── CSkeletonGenerator.hpp  # Skeleton 头文件生成器
│   └── CDdsIdlGenerator.hpp    # OMG IDL 生成器 + QoS XML 输出
├── src/
│   ├── main.cpp                # CLI 入口
│   ├── CFidlLexer.cpp          # 词法分析实现
│   ├── CFidlParser.cpp         # 语法分析实现
│   ├── CSchemaHash.cpp         # SHA-256 + FNV-1a 实现
│   ├── CTypesGenerator.cpp     # Types 生成实现
│   ├── CProxyGenerator.cpp     # Proxy 生成实现
│   ├── CSkeletonGenerator.cpp  # Skeleton 生成实现
│   └── CDdsIdlGenerator.cpp    # OMG IDL 生成实现 + QoS XML 输出
├── test/
│   ├── Calculator.fidl         # 测试用 Franca IDL 定义
│   ├── test_fidl_parser.cpp    # 库级单元测试 (4 测试)
│   └── test_cli_modes.cpp      # CLI 集成测试 (16 测试)
└── build/                      # 构建输出目录
```

---

## 3. 构建与安装

```bash
# 独立构建
cd modules/Com/generator
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .

# 安装到系统 PATH
cmake --install . --prefix /usr/local

# 含测试的构建
cmake .. -DLAP_SIDL_GEN_BUILD_TESTS=ON
cmake --build .
ctest --output-on-failure
```

---

## 4. CLI 使用

### 4.1 基本语法

```bash
lap-sidl-gen --input <file.fidl> --output <dir> [options]
lap-sidl-gen --input <file.fidl> --validate
lap-sidl-gen --input <file.fidl> --hash-only
```

### 4.2 运行模式

| 模式 | 标志 | 是否需要 `--output` | 说明 |
|------|------|-------------------|------|
| **验证** | `--validate` | ❌ | 仅检查 .fidl 语法，输出包名/接口数/TypeCollection 数 |
| **哈希** | `--hash-only` | ❌ | 输出 16 字符十六进制 Schema Hash |
| **生成** | `--proxy --skeleton --types --dds-idl --all` | ✅ | 生成代码文件，默认 `--all` |

### 4.3 完整选项

| 选项 | 缩写 | 说明 | 默认值 |
|------|------|------|--------|
| `--input` | `-i` | 输入 .fidl 文件 (必需) | — |
| `--output` | `-o` | 输出目录 (生成模式必需) | — |
| `--namespace` | `-n` | C++ 命名空间前缀 | `""` |
| `--author` | — | 头文件注释作者 | `"Aii"` |
| `--service-id` | — | 覆盖自动生成的 Service ID (十进制或 0x 十六进制) | 自动计算 |
| `--schema-hash` | — | 注入外部 Schema Hash（跳过自动计算） | 自动计算 |
| `--version-string` | — | 覆盖 OMG IDL 中的版本字符串 | 使用 .fidl 中的 version |
| `--proxy` | — | 仅生成 Proxy 头文件 | — |
| `--skeleton` | — | 仅生成 Skeleton 头文件 | — |
| `--types` | — | 仅生成 Types 头文件 | — |
| `--dds-idl` | — | 生成 OMG IDL v4.2，同时输出 `<Interface>_qos.xml` | — |
| `--com-config` | — | 平台级配置文件路径 (`com_config.yaml`)，含 QoS Profile 定义 | 无 (使用内置默认值) |
| `--service-deploy` | — | 部署清单路径 (`service_deploy.yaml`)，含 FIDL 元素级 QoS 绑定 | 无 (使用内置默认值) |
| `--slot-mapping` | — | 槽位映射配置路径 (`slot_mapping.yaml`) | 无 (可选) |
| `--instance-id` | — | 覆盖 Instance ID (十进制或 0x 十六进制) | 0x0001 |
| `--all` | — | 生成所有输出 | 无选项时默认 |
| `--validate` | — | 仅验证语法 | — |
| `--hash-only` | — | 仅输出 Schema Hash | — |
| `--help` | `-h` | 显示帮助 | — |
| `--version` | `-v` | 显示版本 | — |

### 4.4 退出码

| 退出码 | 含义 |
|--------|------|
| 0 | 成功 |
| 1 | 参数错误或文件读取失败 |
| 2 | 词法分析错误 |
| 3 | 语法分析错误 |
| 4 | 生成部分失败 |

### 4.5 使用示例

```bash
# 验证 .fidl 文件
lap-sidl-gen -i Calculator.fidl --validate

# 仅获取 Schema Hash
lap-sidl-gen -i Calculator.fidl --hash-only

# 全量生成
lap-sidl-gen -i Calculator.fidl -o gen/ --all

# 选择性生成 + 自定义命名空间
lap-sidl-gen -i Sensor.fidl -o gen/ --proxy --types -n lap::app

# 注入外部 Schema Hash
lap-sidl-gen -i Radar.fidl -o gen/ --all --schema-hash a3f7c9e2b5d14a8c

# 覆盖版本号
lap-sidl-gen -i Service.fidl -o gen/ --dds-idl --version-string 9.8.7

# 生成 OMG IDL，自动输出 QoS XML（从标准配置文件读取 QoS，缺失时用内置默认值）
lap-sidl-gen -i Radar.fidl -o gen/ --dds-idl \
    --com-config /etc/lap/com/com_config.yaml \
    --service-deploy manifests/ecu_adas_front/service_deploy.yaml

# 生成 OMG IDL，不提供配置文件 → 全部使用内置默认 QoS
lap-sidl-gen -i Radar.fidl -o gen/ --dds-idl

# 指定 Instance ID
lap-sidl-gen -i Radar.fidl -o gen/ --all --instance-id 0x0002
```

### 4.6 生产环境 5 步工作流

完整的 Franca → AUTOSAR + DDS 代码生成流程（步骤 1-4 由 `lap-sidl-gen` 单一可执行文件完成）：

```bash
#!/bin/bash
# tools/generate_all.sh
set -euo pipefail
FIDL="services/radar/RadarService.fidl"
OUT="generated/radar"

lap-sidl-gen -i "$FIDL" --validate                              # 1. 验证语法
HASH=$(lap-sidl-gen -i "$FIDL" --hash-only)                      # 2. Schema Hash
lap-sidl-gen -i "$FIDL" -o "$OUT/autosar" --proxy --skeleton \   # 3. AUTOSAR API
    --schema-hash "$HASH"
lap-sidl-gen -i "$FIDL" -o "$OUT/dds" --dds-idl \                # 4. OMG IDL + QoS XML
    --schema-hash "$HASH" \
    --com-config    /etc/lap/com/com_config.yaml \
    --service-deploy manifests/$(hostname)/service_deploy.yaml
# 5. TypeSupport (任选一款 OMG IDL 编译器)
fastddsgen -replace -typeobject -d "$OUT/dds" \                  # 方案 A: FastDDS-gen
    "$OUT/dds/RadarService.idl"
# opendds_idl "$OUT/dds/RadarService.idl"                       # 方案 B: OpenDDS
# idlpp -l c++ "$OUT/dds/RadarService.idl"                      # 方案 C: CycloneDDS
```

> **注意**: 步骤 4 同时输出 `RadarService.idl`（标准 OMG IDL v4.2）和 `RadarService_qos.xml`（DDS QoS Profile）。
> QoS 值按优先级从 `service_deploy.yaml` > `com_config.yaml` 解析；若任一文件缺失则平滑回落至内置默认值。
> FastDDS-gen 仅作为步骤 5 TypeSupport 生成的参考工具之一，可替换为任意 OMG IDL 编译器。

---

## 5. 处理流水线

```
 输入 .fidl
     │
     ▼
 ┌──────────┐     LexerError (exit 2)
 │  CFidlLexer  │────────────────────────→ 终止
 └─────┬────┘
       │ tokens
       ▼
 ┌──────────┐     ParserError (exit 3)
 │ CFidlParser │────────────────────────→ 终止
 └─────┬────┘
       │ FidlModel (AST)
       ▼
 ┌──────────┐
 │CSchemaHash│──→ 16-char hex hash
 └─────┬────┘
       │
       ├─→ CTypesGenerator    → <Interface>Types.hpp
       ├─→ CProxyGenerator    → <Interface>Proxy.hpp
       ├─→ CSkeletonGenerator → <Interface>Skeleton.hpp
       └─→ CDdsIdlGenerator   → <Interface>.idl
                               → <Interface>_qos.xml  (始终生成; QoS 来自 YAML 或内置默认值)
```

---

## 6. Franca IDL AST (`CFidlAst.hpp`)

命名空间: `lap::com::generator`

### 6.1 类型别名

生成器使用独立类型别名，避免对 `lap::core` 的编译依赖：

| 别名 | 实际类型 |
|------|---------|
| `Bool` | `bool` |
| `Char` | `char` |
| `Int32` | `std::int32_t` |
| `UInt8` / `UInt16` / `UInt32` / `UInt64` | `std::uint8/16/32/64_t` |
| `String` | `std::string` |
| `StringView` | `std::string_view` |

### 6.2 AST 节点

| 节点类型 | 说明 | 关键字段 |
|---------|------|---------|
| `SourceLocation` | 错误定位 | `file`, `line`, `column` |
| `Version` | 版本声明 | `major`, `minor`, `patch`; `IsValid()`, `ToString()` |
| `TypeRef` | 类型引用 | `name` (点分限定), `isArray`; `IsQualified()`, `ToCppName()` |
| `Field` | 类型字段 | `type: TypeRef`, `name` |
| `Enumerator` | 枚举值 | `name`, `value: Int32`, `hasExplicitValue` |
| `EnumDef` | 枚举定义 | `name`, `enumerators[]`, `location` |
| `StructDef` | 结构体 | `name`, `extends` (继承), `fields[]`, `location` |
| `TypedefDef` | 类型别名 | `name`, `targetType: TypeRef`, `location` |
| `ArrayDef` | 命名数组 | `name`, `elementType: TypeRef` |
| `MapDef` | 命名映射 | `name`, `keyType`, `valueType` |
| `MethodDef` | 方法 [SWS_CM_00800] | `name`, `isFireAndForget`, `inArgs[]`, `outArgs[]`, `errorArgs[]` |
| `BroadcastDef` | 广播事件 [SWS_CM_00700] | `name`, `outArgs[]` |
| `AttributeDef` | 属性/字段 [SWS_CM_00900] | `name`, `type`, `isReadonly`, `isNotify`, `isNoSubscriptions` |
| `TypeCollection` | 类型集合块 | `name`, `version`, `enums[]`, `structs[]`, `typedefs[]`, `arrays[]`, `maps[]` |
| `Interface` | 接口定义 [SWS_CM_00002/00004] | `name`, `version`, `extends`, `methods[]`, `broadcasts[]`, `attributes[]`, `enums[]`, `structs[]`, `typedefs[]` |
| `FidlModel` | 完整 .fidl 模型 | `packageName`, `imports[]`, `typeCollections[]`, `interfaces[]`, `sourceFile` |

### 6.3 生成器配置 (`GeneratorConfig`)

```cpp
struct GeneratorConfig {
    String outputDir       = "./generated";
    String namespacePrefix = "lap::com";
    String author          = "Aii";
    String schemaHashOverride;      // 非空则使用注入值
    String versionOverride;         // 覆盖 OMG IDL 版本
    // QoS 来源：从以下 3 个标准 YAML 文件读取；未指定路径则使用内置默认值
    String comConfigPath;           // com_config.yaml    (QoS Profile 定义)
    String serviceDeployPath;       // service_deploy.yaml (FIDL 元素级 QoS 绑定)
    String slotMappingPath;         // slot_mapping.yaml   (可选，槽位拓扑)
    Bool   generateProxy    = true;
    Bool   generateSkeleton = true;
    Bool   generateTypes    = true;
    Bool   generateDdsIdl   = false;
    Bool   headerOnly       = true;
    UInt16 serviceIdOverride = 0;   // 0 = 自动计算 (FNV-1a)
    UInt16 instanceIdOverride = 1;  // 默认 Instance ID = 0x0001
};
```

---

## 7. 词法分析器 (`CFidlLexer`)

### 7.1 Token 类型

**关键字 (30)**:

| 分类 | 关键字 |
|------|-------|
| 结构 | `package`, `import`, `typeCollection`, `interface`, `extends` |
| 版本 | `version`, `major`, `minor`, `patch` |
| 类型定义 | `struct`, `enumeration`, `typedef`, `is`, `array`, `of`, `map`, `to`, `union`, `const` |
| 通信 | `method`, `broadcast`, `attribute` |
| 方向 | `in`, `out`, `error` |
| 修饰符 | `fireAndForget`, `readonly`, `notify`, `noSubscriptions` |

**字面量**: `kIdentifier`, `kIntegerLiteral` (十进制 + `0x` 十六进制), `kStringLiteral`

**符号**: `{ } [ ] ( ) . , = ;`

**特殊**: `kEof`, `kUnknown`

### 7.2 词法规则

- 单遍逐字符扫描，维护 `m_pos` / `m_line` / `m_column`
- `Tokenize()` 返回 `vector<Token>`，末尾 `kEof`
- 空白跳过（空格、制表符、CR、LF）
- 注释：`//` 行注释 + `/* */` 块注释（未终止报错）
- 标识符/关键字：字母数字 + 下划线，通过 `unordered_map` 关键字表匹配
- 数字：十进制 + `0x` 十六进制前缀 + 负号前缀
- 字符串：双引号包裹，支持转义 (`\n`, `\t`, `\\`, `\"`)
- 未知字符抛出 `LexerError`（含 file:line:col 定位）

---

## 8. 语法分析器 (`CFidlParser`)

经典递归下降解析器，消费 Lexer 产出的 token 流。

### 8.1 语法 (伪 BNF)

```
fidl-file    := package-decl import-decl* (typeCollection | interface)* EOF
package-decl := 'package' qualified-name
import-decl  := 'import' STRING

typeCollection := 'typeCollection' IDENT '{' (version | enum | struct | typedef | array | map)* '}'
interface      := 'interface' IDENT ('extends' qualified-name)? '{'
                    (version | method | broadcast | attribute | enum | struct | typedef)* '}'

version   := 'version' '{' 'major' INT 'minor' INT ('patch' INT)? '}'
enum      := 'enumeration' IDENT '{' (IDENT ('=' INT)? ','?)* '}'
struct    := 'struct' IDENT ('extends' qualified-name)? '{' (field (','|';')?)* '}'
typedef   := 'typedef' IDENT 'is' ('array' 'of')? typeRef
array     := 'array' IDENT 'of' typeRef
map       := 'map' IDENT '{' typeRef 'to' typeRef '}'
method    := 'method' IDENT ('fireAndForget')? '{' (in-block | out-block | error-block)* '}'
broadcast := 'broadcast' IDENT '{' ('out' field-block)? '}'
attribute := 'attribute' typeRef IDENT ('readonly')? ('notify')? ('noSubscriptions')?

typeRef     := IDENT ('.' IDENT)* ('[' ']')?
field       := typeRef IDENT
field-block := '{' (field ','?)* '}'
```

### 8.2 关键特性

- 枚举值自动递增（无 `=` 时）
- 结构体继承 (`struct A extends B`)
- 接口继承 (`interface A extends B`)
- Error 块支持两种形式：`error { fields }` 和 `error TypeRef`
- Method fire-and-forget 支持
- Attribute 修饰符：`readonly`, `notify`, `noSubscriptions`
- 点分限定类型引用 + 数组后缀 `[]`
- 错误报告：`ParserError` 含 file:line:col 定位

---

## 9. Schema Hash (`CSchemaHash`)

### 9.1 Schema Hash 计算

`CSchemaHash::Compute(FidlModel)`:

1. 将 FidlModel 序列化为规范 JSON-like 字符串（确定性——相同模型始终产出相同字符串）
2. 计算 SHA-256（FIPS 180-4 纯 C++ 实现，无 OpenSSL 依赖）
3. 取前 64 位，返回 **16 字符十六进制字符串**

序列化内容包括：包名、TypeCollection 名称/版本/枚举/结构体、Interface 名称/版本/方法/广播/属性。

### 9.2 Service ID 生成

`CSchemaHash::GenerateServiceId(qualifiedName)`:

- FNV-1a 64 位哈希
- 折叠为 16 位
- 限定范围 **[1, 1022]**（避免 0=保留 和 1023=广播槽位）

### 9.3 覆盖机制

| 参数 | CLI 选项 | 效果 |
|------|---------|------|
| `schemaHashOverride` | `--schema-hash <hash>` | 使用注入值替代自动计算 |
| `serviceIdOverride` | `--service-id <id>` | 使用指定值替代 FNV-1a 计算 |

---

## 10. 生成器详细设计

### 10.1 生成器接口 (`IGenerator`)

```cpp
class IGenerator {
public:
    virtual Bool Generate( const FidlModel& model,
                           const GeneratorConfig& config ) = 0;
protected:
    void writeFileHeader( CCodeWriter& w, ... );
    void writeGuardOpen/Close( CCodeWriter& w, const String& guard );
    void writeNamespaceOpen/Close( CCodeWriter& w, const vector<String>& segments );
    String buildGuardMacro( const vector<String>& segments, const String& filename );
    String resolveCppType( const TypeRef& ref );
};
```

**`CCodeWriter` 工具类**: 4 空格缩进，`Line()` / `Raw()` / `Indent()` / `Dedent()` / `WriteToFile()`。

**命名转换工具函数**:

| 函数 | 输入 → 输出 | 用途 |
|------|------------|------|
| `ToPascalCase()` | `calculator` → `Calculator` | 类名 |
| `ToCamelCase()` | `Calculator` → `calculator` | 变量名 |
| `ToUpperSnake()` | `VehicleSpeed` → `VEHICLE_SPEED` | 宏名/常量 |
| `ToEnumValueName()` | `DIVIDE_BY_ZERO` → `kDivideByZero` | 枚举值 (k-前缀) |
| `ExtractNamespaceSegments()` | 包名 → 命名空间段 | 跳过第一段 (org)，其余段生成 `module` 嵌套 |
| `ToHexValue()` | 数值 → 十六进制字符串 | Service ID 输出 |

### 10.2 类型映射

**Franca → C++ 映射** (`MapFrancaToCpp`):

| Franca 类型 | C++ 类型 |
|------------|---------|
| `UInt8` / `UInt16` / `UInt32` / `UInt64` | `UInt8` / `UInt16` / `UInt32` / `UInt64` |
| `Int8` / `Int16` / `Int32` / `Int64` | `Int8` / `Int16` / `Int32` / `Int64` |
| `Float` / `Float32` | `Float` |
| `Float64` / `Double` | `Double` |
| `Boolean` | `Bool` |
| `String` | `String` |
| `ByteBuffer` | `::std::vector< UInt8 >` |

**Franca → OMG IDL 映射** (`MapFrancaToDds`):

| Franca 类型 | OMG IDL 类型 |
|------------|-------------|
| `UInt8` | `octet` |
| `UInt16` | `unsigned short` |
| `UInt32` | `unsigned long` |
| `UInt64` | `unsigned long long` |
| `Int8` | `char` |
| `Int16` | `short` |
| `Int32` | `long` |
| `Int64` | `long long` |
| `Float` / `Float32` | `float` |
| `Float64` / `Double` | `double` |
| `Boolean` | `boolean` |
| `String` | `string` |
| `ByteBuffer` | `sequence<octet>` |

---

### 10.3 CTypesGenerator

**输出**: `<Interface>Types.hpp` — 每个接口一个文件

**生成内容**:

| 类别 | 说明 |
|------|------|
| TypeCollection 类型 | 枚举 → `enum class : Int32` (k-前缀值)，结构体 (POD)，typedef → `using`，array → `std::vector`，map → `std::unordered_map` |
| Interface 内部类型 | 接口内定义的枚举、结构体、typedef |
| 方法输出结构体 | `<Method>Output`（>1 个输出参数时） |
| 广播事件结构体 | `<Broadcast>Event` |
| ADL 序列化 | 每个结构体/枚举的 `Serialize()` / `Deserialize()` 自由函数，使用 `ISerializer` / `IDeserializer` 接口 |
| `common` 命名空间 | `common::<Interface>` 类，含 `kServiceId`, `kServiceName`, `kSchemaHash`, `kVersionMajor/Minor` 常量 [SWS_CM_11501] |

### 10.4 CProxyGenerator

**输出**: `<Interface>Proxy.hpp` — 每个接口一个文件 [SWS_CM_00004]

**生成内容**:

```
class <Interface>Proxy final : public ProxyBase
├── proxy::events::<Event> final : public ProxyEvent<EventType>      [SWS_CM_00005]
├── proxy::methods::<Method> final : public ProxyMethod<Out, Args>   [SWS_CM_00191]
│   └── ProxyFireAndForgetMethod<Args> (fire-and-forget)
├── proxy::fields::<Field> final : public ProxyField<T>              [SWS_CM_00007]
├── static Result<Proxy> Create(HandleType)                          [SWS_CM_10438]
├── GetHandle()
├── onBindingContextReady() override → 传播 CBindingContext
└── Move-only, Non-copyable                                          [SWS_CM_11551-11554]
```

**Element ID 分配**:
- Events: 从 1 开始递增
- Methods: 从 0x100 开始递增
- Fields: 从 0x200 开始递增

### 10.5 CSkeletonGenerator

**输出**: `<Interface>Skeleton.hpp` — 每个接口一个文件 [SWS_CM_00002]

**生成内容**:

```
class <Interface>Skeleton : public SkeletonBase  (注意: 非 final, 用户需继承)
├── skeleton::events::<Event> final : public SkeletonEvent<EventType>
├── skeleton::methods::<Method> final : public SkeletonMethod<Out, Args>
├── skeleton::fields::<Field> final : public SkeletonField<T>
├── Constructor(InstanceSpecifier, MethodCallProcessingMode)          [SWS_CM_00130]
├── ~Skeleton() → 自动 StopOfferService                              [SWS_CM_11549]
├── doOfferService() override → Runtime::GetBindingManager()         [SWS_CM_00101]
├── doStopOfferService() override                                     [SWS_CM_00111]
└── Move-only, Non-copyable                                          [SWS_CM_11544-11547]
```

### 10.6 CDdsIdlGenerator

**输出**: `<Interface>.idl` — **标准 OMG IDL v4.2** 格式（符合 [OMG IDL 4.2 Specification](https://www.omg.org/spec/IDL/4.2)，不依赖任何特定 DDS 实现）

> 生成的 IDL 文件可被任意 OMG IDL 编译器处理：FastDDS-gen、OpenDDS idl_compiler、CycloneDDS idlpp 等。
> `lap-sidl-gen` 不生成 TypeSupport 代码，该职责由下游 IDL 编译器完成。

**生成内容**:

| 项目 | OMG IDL 输出 |
|------|-------------|
| 包名 | `module` 嵌套块 (如 `module org { module lap { module examples { ... }}}`) |
| Schema Hash | `const string SCHEMA_HASH = "<hash>";` |
| 版本 | `@version("x.y.z")` 注解 |
| TypeCollection 枚举 | 标准枚举 (非 `enum class`) |
| TypeCollection 结构体 | `struct` (含继承 `: base`) |
| TypeCollection typedef | `typedef` / `sequence<>` |
| Interface 内部类型 | 枚举、结构体 |
| 广播事件结构体 | `<Broadcast>Event` |
| 方法请求/响应结构体 | `<Method>Request`, `<Method>Response` |
| **QoS XML Profile** | 始终附加 `<Interface>_qos.xml`；QoS 值来源优先级：`service_deploy.yaml` > `com_config.yaml` > 内置默认值 |

### 10.7 OMG IDL 生成示例

以 RadarService.fidl 为例，`--dds-idl` 模式生成的 OMG IDL 输出（自动注入 Schema Hash 和版本）：

```idl
// Auto-generated from RadarService.fidl by lap-sidl-gen
// Format: OMG IDL v4.2 (https://www.omg.org/spec/IDL/4.2)
// Franca Schema Hash: a3f7c9e2b5d14a8c
// Version: 1.2.3

module lap {
module com {
module radar {
    const string SCHEMA_HASH = "a3f7c9e2b5d14a8c";
    @version("1.2.3")

    struct Point3D {
        float x;
        float y;
        float z;
    };

    struct RadarObject {
        unsigned long objectId;
        Point3D position;
        Point3D velocity;
        float rcs;
    };

    typedef sequence<RadarObject> RadarObjectList;

    // Broadcast → Event Topic Type
    struct ObjectsDetectedEvent {
        unsigned long long timestamp;
        RadarObjectList objects;
    };

}; // radar
}; // com
}; // lap
```

---

## 11. 双层 IDL 设计

> Franca IDL 作为 SSOT，同时驱动 AUTOSAR 应用层 API 和 OMG IDL 传输层描述。

### 11.1 架构对比

| **方面** | **传统单层 DDS IDL** | **双层 IDL (Franca + DDS)** |
|---------|---------------------|---------------------------|
| **接口定义** | DDS IDL v4.2 (耦合 DDS) | Franca IDL (平台无关) |
| **代码生成** | 仅 IDL 编译器 (FastDDS-gen 等) | lap-sidl-gen (AUTOSAR API) + OMG IDL 编译器 (FastDDS-gen / OpenDDS / CycloneDDS 等) |
| **应用依赖** | 必须依赖 FastDDS 库 | 仅依赖 AUTOSAR API (lap::com) |
| **Transport 抽象** | DDS 类型泄漏到应用 | Proxy 层完全隔离 DDS |
| **QoS 配置** | 写在 IDL 或代码中 | 独立 YAML 配置，运行时加载 |
| **版本管理** | 手动维护版本号 | Franca 强制 Major.Minor.Patch + Schema Hash |
| **一致性验证** | 仅 DDS TypeIdentifier | Franca Hash + DDS TypeID 双重验证 |
| **多 Transport** | 仅支持 DDS | 同一 Franca IDL 生成 CoreIPC/DDS/SOME-IP |
| **标准合规** | DDS 特定 | AUTOSAR ara::com 标准 API |
| **编译隔离** | 应用必须链接 FastDDS | 应用无需链接 DDS (仅 Runtime) |
| **升级影响** | DDS 升级影响所有应用 | DDS 升级仅影响 Proxy 层 |

### 11.2 核心价值

1. **单一真相源 (SSOT)** — 所有 ECU 从同一 Franca IDL 生成，Schema Hash 保证一致性
2. **平台无关** — 应用层完全不依赖 DDS，可无缝切换 Transport
3. **强制一致性验证** — CI/CD 语法检查 → 构建时 Hash 验证 → 运行时 SD-Proxy 拒绝不兼容服务
4. **QoS 独立配置** — 不污染 IDL 定义，不同 ECU/环境可用不同 QoS
5. **编译隔离** — 应用仅依赖 `lap_com_runtime` 头文件；FastDDS 仅链接到 Proxy 层

### 11.3 YAML 配置体系

双层 IDL 架构中，所有运行时配置均通过 YAML 文件管理，与 Franca IDL 定义解耦。
配置文件由 `yaml-cpp` 库解析，支持从 AUTOSAR ARXML 通过 `arxml2yaml` 工具转换。

#### 11.3.1 配置文件总览（3 文件合并方案）

原始 6 个配置文件合并为 **3 个**，减少文件散落和重复字段：

| 合并后 | 路径 | 合并来源 | 说明 |
|--------|------|---------|------|
| `com_config.yaml` | `/etc/lap/com/com_config.yaml` | binding_config + static_endpoints + 全局 QoS profiles | 平台级：Transport 插件 + 静态端点 + QoS Profile 定义 |
| `slot_mapping.yaml` | `/etc/lap/com/slot_mapping.yaml` | （保持独立） | 部署级：服务 → 槽位映射 + 系统/内存/心跳/安全配置 |
| `service_deploy.yaml` | `manifests/<ecu>/service_deploy.yaml` | ecu_services + 每服务 QoS 绑定 | ECU 级：服务清单 + QoS 按 FIDL 元素绑定 |

**合并原则**:
- `binding_config.yaml` + `static_endpoints.yaml` + `dds_qos.yaml` → **`com_config.yaml`**（都是平台级通用配置，一个 ECU 一份）
- `ecu_<name>_services.yaml` + `qos_config/<service>.yaml` → **`service_deploy.yaml`**（QoS 不再按服务独立成文件，而是内联到 FIDL 元素级别）
- `slot_mapping.yaml` 保持独立（部署拓扑相关，变更频率与其他配置不同）

#### 11.3.2 QoS Profile 定义与 FIDL 元素绑定

**核心改进**: QoS 不再绑定到"服务"粒度，而是绑定到 **FIDL 定义的通信元素**（event / method / field），通过可复用的 QoS Profile 实现。

**FIDL 元素与 QoS 的映射关系**:

| FIDL 元素 | ara::com 模式 | QoS 关注点 | DDS 映射 |
|-----------|--------------|-----------|---------|
| `broadcast` (Event) | Pub/Sub | reliability, durability, deadline, history | Topic QoS |
| `method` (Request/Reply) | RPC | timeout, retry, reliability | Service QoS |
| `method fireAndForget` | Fire-and-Forget | reliability (通常 BEST_EFFORT) | Topic QoS |
| `attribute` (Field) | Get/Set/Notify | notify 频率, cache depth, reliability | Topic QoS (notifier) |

**QoS Profile 定义** (在 `com_config.yaml` 中):

```yaml
# com_config.yaml — QoS Profiles 部分
qos_profiles:
  # ─── Event 类 Profile ───
  reliable_event:           # 可靠事件 (ASIL 级传感器数据)
    reliability: RELIABLE
    durability: TRANSIENT_LOCAL
    history: { kind: KEEP_LAST, depth: 10 }
    deadline_ms: 100
    liveliness: { kind: AUTOMATIC, lease_ms: 1000 }
    ownership: SHARED

  best_effort_event:        # 尽力事件 (高频低延迟, 允许丢帧)
    reliability: BEST_EFFORT
    durability: VOLATILE
    history: { kind: KEEP_LAST, depth: 1 }
    ownership: SHARED

  persistent_event:         # 持久化事件 (诊断日志)
    reliability: RELIABLE
    durability: PERSISTENT
    history: { kind: KEEP_ALL }

  # ─── Method 类 Profile ───
  sync_method:              # 同步方法 (标准 RPC)
    timeout_ms: 5000
    retry_count: 3
    reliability: RELIABLE

  realtime_method:          # 实时方法 (控制指令, 不重试)
    timeout_ms: 50
    retry_count: 0
    reliability: RELIABLE

  fire_and_forget:          # Fire-and-Forget (无响应)
    reliability: BEST_EFFORT
    timeout_ms: 0

  # ─── Field 类 Profile ───
  notify_field:             # 可通知字段 (值变化时推送)
    notify_reliability: RELIABLE
    notify_durability: TRANSIENT_LOCAL
    cache_depth: 1
    notify_deadline_ms: 500

  readonly_field:           # 只读字段 (仅 Get, 无 Notify)
    cache_depth: 1
    get_timeout_ms: 1000
```

**在 `service_deploy.yaml` 中按 FIDL 元素绑定 Profile**:

> **每个 event / method / field 均可独立指定不同的 QoS Profile 及 override 字段，互不影响。**
> 同一服务内的不同元素、不同服务内的同名元素，全部按元素名单独配置。

| 配置项 | 粒度 | 说明 |
|--------|------|------|
| `qos.events.<Name>.profile` | 每个 broadcast（事件）单独 | 从 `com_config.yaml` 中引用 Profile 名称 |
| `qos.events.<Name>.override` | 每个 broadcast 单独 | 仅覆盖指定字段，其余继承 Profile 值 |
| `qos.methods.<Name>.profile` | 每个 method 单独 | 同上 |
| `qos.methods.<Name>.override` | 每个 method 单独 | 同上 |
| `qos.fields.<Name>.profile` | 每个 attribute（字段）单独 | 同上 |
| `qos.fields.<Name>.override` | 每个 attribute 单独 | 同上 |
| 未列出的元素 | — | 按优先级回落：com_config 默认 Profile → 内置默认值 |

```yaml
# manifests/ecu_adas_front/service_deploy.yaml
ecu_id: "ECU_ADAS_Front"
contract_version: "v1.2.3"

services:
  - name: "RadarService"
    fidl: "services/radar/RadarService.fidl"
    service_id: 0x1234           # 可选: 不填则由 Schema Hash FNV-1a 自动生成
    instance_id: 0x0001          # 可选: 不填默认 0x0001
    schema_hash: "a3f7c9e2b5d14a8c"
    version: { major: 1, minor: 2, patch: 3 }
    role: provider

    # ── QoS 按 FIDL 元素绑定 ──────────────────────────────────────────
    # 每个 event / method / field 可独立选择不同 Profile，并按需 override
    qos:
      # ── Events (broadcast) ─────────────────────────────────────────
      events:
        ObjectsDetected:          # ← broadcast 名称 (来自 .fidl)
          profile: reliable_event # 可靠传输，TRANSIENT_LOCAL
          override:
            deadline_ms: 50       # 雷达 20Hz → 50ms deadline

        RawPointCloud:            # ← 另一个 broadcast，高频低延迟
          profile: best_effort_event  # 与 ObjectsDetected 完全不同的 Profile
          override:
            history_depth: 3      # 保留最近 3 帧

        FaultAlert:               # ← 故障告警，需持久化
          profile: persistent_event   # PERSISTENT durability，不覆盖其他字段

      # ── Methods ────────────────────────────────────────────────────
      methods:
        Calibrate:                # ← 长耗时标定，延长超时
          profile: sync_method
          override:
            timeout_ms: 10000     # 覆盖 profile 的 5000ms → 10000ms

        GetStatus:                # ← 快速查询，使用默认 sync_method 无需 override
          profile: sync_method

        SendDiagCmd:              # ← Fire-and-Forget 指令，不等响应
          profile: fire_and_forget    # 与上两个 method 完全不同的 Profile

      # ── Fields (attribute) ─────────────────────────────────────────
      fields:
        sensitivity:              # ← 可写可通知字段
          profile: notify_field
          override:
            notify_deadline_ms: 200   # 灵敏度变化通知最多 200ms 延迟

        firmwareVersion:          # ← 只读只查字段，无 Notify
          profile: readonly_field     # 与 sensitivity 不同 Profile

    # Transport 绑定 (每个 event/method 也可独立指定 binding)
    bindings:
      default: [coreipc, dds]
      overrides:
        ObjectsDetected:
          local: coreipc          # 同 ECU 订阅者用零拷贝
          remote: dds             # 跨 ECU 用 DDS
        RawPointCloud:
          local: coreipc          # 高频数据只走本地零拷贝
          remote: coreipc

  - name: "BrakeService"
    fidl: "services/brake/BrakeService.fidl"
    service_id: 0x5678
    instance_id: 0x0001          # 可选: 同一服务多实例时递增
    schema_hash: "d7b4e1f8a9c23456"
    version: { major: 1, minor: 0, patch: 0 }
    role: consumer
    qos:
      events:
        BrakeCommand:             # ASIL-D 高实时性事件
          profile: reliable_event
          override:
            deadline_ms: 10       # ASIL-D: 10ms deadline
        StatusReport:             # 状态上报，低优先级
          profile: best_effort_event  # 与 BrakeCommand 使用不同 Profile
      methods:
        EmergencyBrake:           # 实时控制指令，不重试
          profile: realtime_method
        DiagnosticQuery:          # 诊断查询，可以稍慢
          profile: sync_method    # 与 EmergencyBrake 使用不同 Profile
          override:
            timeout_ms: 3000
    bindings:
      default: [dds]
```

**QoS 解析优先级**: `service_deploy.yaml override` > `service_deploy.yaml profile` > `com_config.yaml profile 定义` > 内置默认值

#### 11.3.3 平台配置 (`com_config.yaml`)

合并了 Transport Binding 配置、静态端点、全局 QoS Profile 定义为单一文件：

```yaml
# /etc/lap/com/com_config.yaml

# ═══════════════════════════════════════════
# §1 Transport Bindings (原 binding_config.yaml)
# ═══════════════════════════════════════════
bindings:
  - name: coreipc
    priority: 100
    library: /usr/lib/lap/com/liblap_binding_coreipc.so
    enabled: true
    parameters:
      domain_id: 0
      node_name: lap_com_node
      max_publishers: 128

  - name: dds
    priority: 80
    library: /usr/lib/lap/com/liblap_binding_dds.so
    enabled: true
    parameters:
      domain_id: 42
      transport: af_xdp
      interface: eth0

  - name: someip
    priority: 60
    library: /usr/lib/lap/com/liblap_binding_someip.so
    enabled: true
    parameters:
      multicast_group: 239.0.0.1
      port_range: "30500-30600"

# 静态绑定覆盖 (强制指定 service → binding)
static_bindings:
  - service_id: 0xF001          # ASIL-D 制动控制
    binding: coreipc
  - service_id: 0x0301          # 诊断服务
    binding: someip

# ═══════════════════════════════════════════
# §2 静态端点 (原 static_endpoints.yaml)
# ═══════════════════════════════════════════
static_endpoints:               # [SWS_CM_02201, TPS_MANI_03313-03315]
  - service_id: 0x1234
    instance_id: 0x0001
    binding: coreipc
    endpoint:
      type: SharedMemory
      service_name: /perception/camera_front

  - service_id: 0x5678
    instance_id: 0x0001
    binding: dds
    endpoint:
      type: DDS
      topic_name: vehicle_status
      domain_id: 0

# ═══════════════════════════════════════════
# §3 QoS Profiles (详见 §11.3.2)
# ═══════════════════════════════════════════
qos_profiles:
  reliable_event:   { ... }   # → §11.3.2 完整定义
  best_effort_event: { ... }
  persistent_event: { ... }
  sync_method:      { ... }
  realtime_method:  { ... }
  fire_and_forget:  { ... }
  notify_field:     { ... }
  readonly_field:   { ... }

# ═══════════════════════════════════════════
# §4 Runtime 配置
# ═══════════════════════════════════════════
runtime:
  mode: library
  event_loop: binding_managed
  discovery:
    central_server: "192.168.1.100:34567"
    fallback_to_builtin: true
```

#### 11.3.4 槽位映射 (`slot_mapping.yaml`)

保持独立，关注点与平台/QoS 配置不同（共享内存拓扑 + FuSa 安全分区）：

```yaml
# /etc/lap/com/slot_mapping.yaml
slot_mapping:
  static_allocations:
    - service_interface: "RadarService"
      instance_id: 1
      slot_index: 10
      safety_level: QM

    - service_interface: "BrakeControl"
      instance_id: 1
      slot_index: 101
      safety_level: ASIL-D

  dynamic_allocation:
    enabled: true
    slot_range: { start: 200, end: 1023 }
    hash_algorithm: "FNV1A"
    collision_resolution: "linear_probing"

system:
  max_slots: 1024
  slot_size_bytes: 256
  memory:
    use_hugepages: true
    hugepage_size: "1GB"
    memfd_name: "lap_service_registry"
  heartbeat:
    interval_ms: 1000
    timeout_multiplier: 3
  safety:
    enable_qm_asil_separation: true
    asil_memfd_permissions: 0644
```

#### 11.3.5 配置与代码生成的关系

```text
                    ┌─────────────────┐
                    │  Franca IDL     │ ← 接口契约 (what)
                    │  (.fidl)        │
                    │  event / method │
                    │  / field 定义   │
                    └───────┬─────────┘
                            │ lap-sidl-gen
            ┌───────────────┼───────────────┐
            ▼               ▼               ▼
    AUTOSAR API (.hpp) OMG IDL (.idl)  Schema Hash
            │           + _qos.xml        │
            │               │               │
            ▼               ▼               ▼
    ┌───────────────────────────────────────────────────┐
    │            YAML 配置层 — 部署策略 (how/where)      │
    ├──────────────┬────────────────┬───────────────────┤
    │ com_config   │ slot_mapping   │ service_deploy    │
    │ .yaml        │ .yaml          │ .yaml (per ECU)   │
    │              │                │                   │
    │ · Bindings   │ · 槽位映射     │ · 服务清单        │
    │ · 静态端点   │ · 安全分区     │ · schema_hash     │
    │ · QoS Profile│ · 心跳/内存   │ · QoS 按 FIDL     │
    │   定义       │                │   元素绑定 Profile│
    └──────────────┴────────────────┴───────────────────┘
                            │
               schema_hash 一致性验证
                            │
    ┌───────────────────────▼───────────────────────────┐
    │  Runtime (ConfigParser + yaml-cpp)                 │
    │  加载 com_config → 加载 service_deploy             │
    │  → 验证 schema_hash → 按 event/method/field 解析   │
    │  QoS Profile → 创建 Proxy/Skeleton + Binding       │
    └───────────────────────────────────────────────────┘
```

**核心原则**:
1. Franca IDL 定义**接口契约** (what) — event / method / field 是 QoS 的绑定锚点
2. QoS Profile 在 `com_config.yaml` **集中定义**，`service_deploy.yaml` 按 FIDL 元素名 **引用 + 覆盖**
3. 两者通过 `schema_hash` 关联，确保配置与代码生成一致
4. 同一 Profile 可被多个服务的不同元素复用（如 `reliable_event` 用于雷达和摄像头事件）

#### 11.3.6 默认配置与自动分配策略

当 YAML 配置未显式指定某些字段时，系统使用以下默认值与自动分配规则：

**Service ID 默认分配**:

| 配置方式 | 优先级 | 说明 |
|---------|--------|------|
| `service_deploy.yaml` 中 `service_id: 0x1234` | 最高 | 显式指定，直接使用 |
| CLI `--service-id 0x1234` | 次高 | 命令行覆盖 |
| 自动计算 (FNV-1a) | 默认 | `CSchemaHash::GenerateServiceId(qualifiedName)`，范围 [1, 1022] |

**Instance ID 默认分配**:

| 配置方式 | 优先级 | 说明 |
|---------|--------|------|
| `service_deploy.yaml` 中 `instance_id: 0x0002` | 最高 | 显式指定，支持同一服务多实例部署 |
| CLI `--instance-id 0x0001` | 次高 | 命令行直接覆盖，优先于静态配置文件 |
| `slot_mapping.yaml` 中 `instance_id: 1` | 次之 | 部署级静态映射，低于 CLI |
| 默认值 `0x0001` | 默认 | 未指定时使用 |

> **多实例场景**: 同一 ServiceInterface 部署多个实例时（如前后雷达），需在 `service_deploy.yaml` 中为每个实例分配不同的 `instance_id`:
> ```yaml
> services:
>   - name: "RadarService"
>     instance_id: 0x0001    # 前雷达
>     role: provider
>   - name: "RadarService"
>     instance_id: 0x0002    # 后雷达
>     role: provider
> ```

**QoS 默认策略**:

| 配置层级 | 优先级 | 说明 |
|---------|--------|------|
| `service_deploy.yaml` 元素级 `override` | **最高** | 覆盖 Profile 中的个别字段 |
| `service_deploy.yaml` 元素级 `profile` 引用 | 次高 | 引用 `com_config.yaml` 中的 Profile |
| `com_config.yaml` 中 Profile 定义 | 中 | 集中定义的 QoS 参数集 |
| **内置默认值** | **最低** | 未配置时使用的保守默认值 |

**内置默认 QoS 值** (无任何 YAML 配置时):

```yaml
# 内置默认值 (hardcoded fallback)
_builtin_defaults:
  event:
    reliability: BEST_EFFORT      # 默认不保证可靠性
    durability: VOLATILE          # 不持久化
    history: { kind: KEEP_LAST, depth: 1 }
    deadline_ms: 0                # 不设 deadline (无限)
    ownership: SHARED
  method:
    timeout_ms: 5000              # 5 秒超时
    retry_count: 0                # 不重试
    reliability: RELIABLE         # 方法调用默认可靠
  field:
    cache_depth: 1
    notify_reliability: RELIABLE
    notify_durability: TRANSIENT_LOCAL
    notify_deadline_ms: 0
```

**Schema Hash 默认行为**: 始终自动计算。仅当 CLI `--schema-hash` 或 `service_deploy.yaml` `schema_hash` 字段非空时使用注入值。

#### 11.3.7 IDL 生成时附加 QoS 配置

`--dds-idl` 模式**始终**同步输出伴随的 **DDS QoS XML Profile** 文件。QoS 值按三级优先级从标准配置文件中读取；若任意级别的文件不存在，自动回落至内置默认值，**不会报错**。

**生成文件（固定输出，不依赖额外参数）**:

| 文件 | 说明 |
|------|------|
| `<Interface>.idl` | 标准 OMG IDL v4.2，不嵌入 QoS，保持 IDL 纯净 |
| `<Interface>_qos.xml` | DDS QoS XML Profile，始终生成 |

**QoS 值三级解析流程**:

```text
 优先级 1 (最高)          优先级 2                 优先级 3 (最低)
┌──────────────────┐   ┌──────────────────┐   ┌──────────────────────┐
│ service_deploy   │   │  com_config      │   │  内置默认值           │
│ .yaml            │   │  .yaml           │   │  (硬编码 fallback)    │
│                  │   │                  │   │                      │
│ 每个 FIDL 元素   │   │ QoS Profile 定义 │   │ event: BEST_EFFORT   │
│ 的 profile 引用  │   │ reliable_event   │   │ method: RELIABLE 5s  │
│ + override 字段  │   │ sync_method ...  │   │ field: RELIABLE      │
│                  │   │                  │   │ (见 §11.3.6)         │
│  --service-deploy│   │  --com-config    │   │  (无需任何参数)       │
└────────┬─────────┘   └────────┬─────────┘   └──────────┬───────────┘
         │ 文件存在时读取              │ 文件存在时读取              │ 以上均缺失时
         └──────────────────┬─────────┘                   │
                            ▼                             │
                    CDdsIdlGenerator                      │
                   QoS 解析引擎                 ←─────────┘
                            │
          ┌─────────────────┴──────────────────┐
          ▼                                     ▼
  <Interface>.idl                     <Interface>_qos.xml
  (标准 OMG IDL,                      (每个 event/method/field
   无 QoS)                             的 Topic QoS 块)
```

> **slot_mapping.yaml** (`--slot-mapping`) 为可选输入，提供共享内存槽位信息；
> QoS 解析引擎不强依赖它，仅在存在时将槽位拓扑注释写入 QoS XML 的 `<!-- -->` 块。

**QoS 解析伪代码**:

```cpp
// CDdsIdlGenerator::ResolveQoS(elementName, elementKind)
QosParams ResolveQoS(const String& name, ElementKind kind) {
    // 1. service_deploy.yaml 中有此元素的 override 字段？
    if (serviceDeployLoaded && deploy.HasOverride(name))
        return deploy.GetOverride(name);                    // 最高优先级

    // 2. service_deploy.yaml 中有 profile 引用，且 com_config 中有该 profile？
    if (serviceDeployLoaded && comConfigLoaded) {
        auto profileName = deploy.GetProfile(name);        // e.g. "reliable_event"
        if (comConfig.HasProfile(profileName))
            return comConfig.GetProfile(profileName);      // 次高优先级
    }

    // 3. com_config.yaml 中有对应 kind 的默认 profile？
    if (comConfigLoaded && comConfig.HasDefaultForKind(kind))
        return comConfig.GetDefaultForKind(kind);

    // 4. 以上均未命中 → 内置默认值 (不报错，不要求提供配置文件)
    return BuiltinDefaultQoS(kind);
}
```

**生成的 QoS XML 示例** — 不同 FIDL 元素输出各自独立的 Topic QoS 块:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- Auto-generated by lap-sidl-gen from RadarService.fidl -->
<!-- QoS source: service_deploy.yaml + com_config.yaml     -->
<!--   (missing entries fall back to built-in defaults)    -->
<dds xmlns="http://www.omg.org/dds"
     xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">

  <profiles>
    <!-- ══ Events (broadcast) ══════════════════════════════════════════ -->

    <!-- ObjectsDetected: profile=reliable_event + override deadline=50ms
         source: service_deploy override 合并 com_config profile           -->
    <topic_qos name="RadarService_ObjectsDetected">
      <reliability><kind>RELIABLE</kind></reliability>
      <durability><kind>TRANSIENT_LOCAL</kind></durability>
      <deadline><period><nanosec>50000000</nanosec></period></deadline><!-- override: 50ms -->
      <history><kind>KEEP_LAST</kind><depth>10</depth></history>
    </topic_qos>

    <!-- RawPointCloud: profile=best_effort_event + override history_depth=3
         与 ObjectsDetected 使用完全不同的 Profile，高频低延迟               -->
    <topic_qos name="RadarService_RawPointCloud">
      <reliability><kind>BEST_EFFORT</kind></reliability>
      <durability><kind>VOLATILE</kind></durability>
      <history><kind>KEEP_LAST</kind><depth>3</depth></history><!-- override: depth=3 -->
    </topic_qos>

    <!-- FaultAlert: profile=persistent_event，故障需持久化，无 override
         source: 完全来自 com_config.yaml 的 profile 定义                   -->
    <topic_qos name="RadarService_FaultAlert">
      <reliability><kind>RELIABLE</kind></reliability>
      <durability><kind>PERSISTENT</kind></durability>
      <history><kind>KEEP_ALL</kind></history>
    </topic_qos>

    <!-- ══ Methods ════════════════════════════════════════════════════ -->

    <!-- Calibrate: profile=sync_method + override timeout=10000ms
         method timeout 由 ara::com 运行时处理；DDS 层只写 reliability      -->
    <topic_qos name="RadarService_Calibrate_Request">
      <reliability><kind>RELIABLE</kind></reliability>
    </topic_qos>
    <topic_qos name="RadarService_Calibrate_Response">
      <reliability><kind>RELIABLE</kind></reliability>
    </topic_qos>

    <!-- GetStatus: profile=sync_method，无 override
         source: 与 Calibrate 相同 Profile，但独立配置条目                  -->
    <topic_qos name="RadarService_GetStatus_Request">
      <reliability><kind>RELIABLE</kind></reliability>
    </topic_qos>

    <!-- SendDiagCmd: profile=fire_and_forget，与上两个 method 完全不同 Profile -->
    <topic_qos name="RadarService_SendDiagCmd_Request">
      <reliability><kind>BEST_EFFORT</kind></reliability>
      <!-- fire-and-forget: 无 Response topic -->
    </topic_qos>

    <!-- ══ Fields (attribute) ═════════════════════════════════════════ -->

    <!-- sensitivity: profile=notify_field + override notify_deadline=200ms -->
    <topic_qos name="RadarService_sensitivity_Notify">
      <reliability><kind>RELIABLE</kind></reliability>
      <durability><kind>TRANSIENT_LOCAL</kind></durability>
      <deadline><period><nanosec>200000000</nanosec></period></deadline><!-- override: 200ms -->
    </topic_qos>

    <!-- firmwareVersion: profile=readonly_field，只读只查，无 Notify topic
         与 sensitivity 使用不同 Profile                                    -->
    <!-- (readonly_field 无 Notify topic，仅生成 Get 请求的 QoS)            -->

    <!-- ══ 回落场景示例 ════════════════════════════════════════════════ -->

    <!-- BrakeService.StatusReport: service_deploy 中未配置此 event
         → 自动回落至内置默认值: BEST_EFFORT / VOLATILE / KEEP_LAST-1      -->
    <topic_qos name="BrakeService_StatusReport">
      <!-- source: built-in defaults (element not listed in service_deploy.yaml) -->
      <reliability><kind>BEST_EFFORT</kind></reliability>
      <durability><kind>VOLATILE</kind></durability>
      <history><kind>KEEP_LAST</kind><depth>1</depth></history>
    </topic_qos>
  </profiles>
</dds>
```

> **设计原则**: IDL 文件保持纯净的标准 OMG IDL 格式，不嵌入任何 QoS 注解。
> QoS XML 始终生成并与 IDL 文件配套，由 DDS 运行时加载；配置文件全部可选，缺失时优雅降级。

> **更多配置详情**: [ARCHITECTURE_SUMMARY.md §13 配置管理与工具链](ARCHITECTURE_SUMMARY.md#13-配置管理与工具链)

---

## 12. Franca IDL 示例

`test/Calculator.fidl` 展示了所有支持的语法特性：

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

    method add {
        in  { Float64 a; Float64 b }
        out { Float64 sum }
    }

    method subtract {
        in  { Float64 a; Float64 b }
        out { Float64 difference }
    }

    method divide {
        in  { Float64 dividend; Float64 divisor }
        out { Float64 quotient; CalculatorTypes.ErrorCode status }
        error CalculatorTypes.ErrorCode
    }

    method reset fireAndForget { }

    broadcast resultReady {
        out { CalculatorTypes.OperationResult result; UInt64 timestamp }
    }

    attribute Float64 lastResult readonly
}
```

### 生成输出文件

```bash
lap-sidl-gen -i Calculator.fidl -o gen/ --all
```

生成:
- `gen/CalculatorTypes.hpp` — 枚举、结构体、typedef、ADL 序列化、common 类
- `gen/CalculatorProxy.hpp` — `CalculatorProxy final : public ProxyBase`
- `gen/CalculatorSkeleton.hpp` — `CalculatorSkeleton : public SkeletonBase`
- `gen/Calculator.idl` — OMG IDL v4.2（可由 FastDDS-gen / OpenDDS / CycloneDDS 等编译）
- `gen/Calculator_qos.xml` — DDS QoS Profile（始终同步生成；无 YAML 配置时使用内置默认值）

---

## 13. 测试覆盖

### 13.1 库级测试 (`test_fidl_parser.cpp`) — 4 个测试

| 测试 | 验证内容 |
|------|---------|
| **TestLexer** | 对 Calculator.fidl 进行 tokenize，验证 >10 tokens，EOF 结尾，找到 package/interface/method tokens |
| **TestParser** | 完整解析 Calculator.fidl：包名、1 个 TypeCollection、1 个 Interface、枚举值（名称+显式值）、结构体字段、4 个方法（含 fireAndForget）、1 个 broadcast (2 outArgs)、1 个 attribute (readonly) |
| **TestSchemaHash** | Hash 非空、16 字符、确定性；ServiceID 在 [1, 1022] 范围内 |
| **TestGenerators** | 运行全部 4 个生成器，验证输出文件存在，检查内容：`enum class ErrorCode`、`struct Operand`、k-前缀枚举值、`CalculatorProxy final`、`ProxyBase`、`kSchemaHash`、命名空间合规 (proxy/skeleton/events/methods/fields/common)、IDL 中的 `SCHEMA_HASH` |

### 13.2 CLI 集成测试 (`test_cli_modes.cpp`) — 16 个测试

| 测试 | 验证内容 |
|------|---------|
| **TestValidateOk** | `--validate` 返回 0，输出含 "OK:", "Package:", "Interfaces:" |
| **TestValidateBadFile** | 不存在文件返回非零 |
| **TestValidateNoOutput** | `--validate` 不需要 `--output`，不生成文件 |
| **TestHashOnly** | 输出恰好 16 个十六进制字符，无 banner |
| **TestHashDeterministic** | 两次 CLI 运行产出相同 hash；与库级 `CSchemaHash::Compute()` 一致 |
| **TestSchemaHashInjectDdsIdl** | `--schema-hash DEADBEEF12345678` 出现在 `.idl` 和 `_qos.xml` 注释中 |
| **TestSchemaHashInjectProxy** | 注入 hash 出现在 proxy 中；proxy 是 `final`，有 `namespace proxy` |
| **TestVersionStringOverride** | `--version-string 9.8.7` 出现在 IDL 头注释和 `@version` 注解中 |
| **TestConfigSchemaHashOverride** | 库级：自定义 hash 出现在 Proxy、Skeleton、OMG IDL 中 |
| **TestConfigVersionOverride** | 库级：版本覆盖出现在 OMG IDL 中 |
| **TestConfigNoOverrideUsesAuto** | 空覆盖 → 使用自动计算 hash |
| **TestFloat64Mapping** | `Float64` 映射为 OMG IDL 的 `double`（注释外无原始 "Float64"） |
| **TestMissingInput** | 无 `--input` → 非零退出 |
| **TestMissingOutputForGeneration** | `--all` 缺少 `--output` → 非零退出 |
| **TestHelpFlag** | `--help` 返回 0，显示所有选项 |
| **TestVersionFlag** | `--version` 返回 0，显示 "lap-sidl-gen" |
| **TestUnknownOption** | `--bogus-flag-xyz` → 非零退出 |

---

## 14. AUTOSAR 标准追溯

| AUTOSAR 需求 | 生成器实现 | R25-11 状态 |
|-------------|-----------|------------|
| [SWS_CM_00002] | Skeleton 类生成 (`class <Interface>Skeleton : public SkeletonBase`) | ✅ 合规 |
| [SWS_CM_00004] | Proxy 类生成 (`class <Interface>Proxy final : public ProxyBase`) | ✅ 合规 |
| [SWS_CM_00005] | 事件内部类 (`ProxyEvent<T>` / `SkeletonEvent<T>`) | ✅ 合规 |
| [SWS_CM_00007] | 字段内部类 (`ProxyField<T>` / `SkeletonField<T>`) | ✅ 合规 |
| [SWS_CM_00101] | Skeleton `doOfferService()` 生成 | ✅ 合规 |
| [SWS_CM_00111] | Skeleton `doStopOfferService()` 生成 | ✅ 合规 |
| [SWS_CM_00130] | Skeleton 构造函数 (`InstanceSpecifier` + `MethodCallProcessingMode`) | ✅ 合规 |
| [SWS_CM_00191] | 方法内部类 (`ProxyMethod<Out, Args>` / `ProxyFireAndForgetMethod<Args>`) | ✅ 合规 |
| [SWS_CM_00700] | BroadcastDef → `<Broadcast>Event` 结构体 | ✅ 合规 |
| [SWS_CM_00800] | MethodDef → 方法类（request/response + fire-and-forget） | ✅ 合规 |
| [SWS_CM_00900] | AttributeDef → 字段类 (readonly/notify/noSubscriptions) | ✅ 合规 |
| [SWS_CM_10438] | Proxy `static Result<Proxy> Create(HandleType)` | ✅ 合规 |
| [SWS_CM_11501] | `common::<Interface>` 命名空间 (kServiceId, kSchemaHash, kVersion) | ⚠️ 命名偏差 (§15.3) |
| [SWS_CM_11544-11547] | Skeleton Move-only, Non-copyable | ✅ 合规 |
| [SWS_CM_11549] | Skeleton 析构函数自动 StopOffer | ✅ 合规 |
| [SWS_CM_11551-11554] | Proxy Move-only, Non-copyable | ✅ 合规 |
| [SWS_CM_00721] | **Trigger::Send** (R22-11 新增) | ❌ 未实现 (§15.1 G1) |
| [SWS_CM_00723] | **Trigger::Subscribe** (R22-11 新增) | ❌ 未实现 (§15.1 G1) |
| [SWS_CM_01012] | 头文件命名 `<si-lower>_common.h` | ❌ 不合规 (§15.2 N1) |
| [SWS_CM_01015] | 头文件命名 `<si-lower>_proxy.h` | ❌ 不合规 (§15.2 N2) |
| [SWS_CM_01018] | 头文件命名 `<si-lower>_skeleton.h` | ❌ 不合规 (§15.2 N3) |
| [SWS_CM_11500] | 命名空间全小写 | ❌ 不合规 (§15.2 N4) |
| [SWS_CM_11508] | `serviceContractVersionMajor/Minor` | ⚠️ 命名偏差 (§15.3) |
| [SWS_CM_11506] | `serviceIdentifier: ServiceIdentifierType` | ⚠️ 类型偏差 (§15.3) |
| [SWS_CM_11507] | `serviceVersion: ServiceVersionType` | ❌ 未生成 (§15.3) |
| [SWS_CM_01073] | `GetServiceState()` (R23-11 新增) | ❌ 未实现 (§15.1 G2) |
| [SWS_CM_00622] | `FindService()` 重载 | ❌ 未实现 (§15.4) |
| [SWS_CM_00123] | `StartFindService()` 重载 | ❌ 未实现 (§15.4) |
| [SWS_LBAP_00065] | **Bitfield 数据类型** (R25-11 新增) | ❌ 未实现 (§15.1 G3) |

---

## 15. R25-11 合规差距分析与重构规划

> 基于 AUTOSAR AP R25-11 规范（2025-11-27 发布）对当前 `lap-sidl-gen` v1.0.0 的系统性对比分析。
> 参考文档：`SWS_CommunicationManagement` (675p)、`EXP_ARAComAPI` (125p)、`SWS_LanguageBindingForModeledAPdatatypes` (80p)、`TPS_ManifestSpecification`、`SWS_Core`。

### 15.1 R25-11 新增特性差距（生成器必须适配）

| # | R25-11 新特性 | SWS 需求 | 当前状态 | 优先级 | 重构影响 |
|---|-------------|---------|---------|--------|---------|
| **G1** | **Trigger 通信原语**（自 R22-11 起缺失） | SWS_CM_00721/00723/00726/00727 | ❌ 未实现 | **P0** | AST + Parser + 3 个生成器 |
| **G2** | **ServiceState API**（自 R23-11 起缺失） | SWS_CM_01071/01073 | ❌ 未实现 | **P1** | Proxy 生成器 |
| **G3** | **Bitfield 数据类型** | SWS_LBAP_00065/00066/00068 | ❌ 未实现 | **P1** | Types 生成器 + LanguageBinding |
| **G4** | **Inhibit Time 监控** | R25-11 Changelog | ❌ 未实现 | **P2** | Event/Trigger/Method 元数据 |
| **G5** | **kCommunicationFailure** | R25-11 (原 kNetworkBindingFailure) | ❌ 未适配 | **P1** | 错误码常量生成 |
| **G6** | **SampleAllocateePtr** | SWS_CM_00306/00308 | ❌ 未实现 | **P2** | Skeleton Event Send 签名 |
| **G7** | **FindService 签名变更** | SWS_CM_00622/00123/00125 | 部分 | **P1** | Proxy 生成器 |

#### G1 — Trigger 通信原语（最高优先级，自 R22-11 起缺失）

Trigger 自 **R22-11** 起纳入 `ServiceInterface` 规范（与 `event`/`method`/`field` 并列），当前生成器尚未支持：

- **语义**: 服务端通知客户端"某条件发生"，**不携带数据**（与 Event 的区别）
- **Proxy 侧**: `proxy::triggers::<TriggerName>` — `Subscribe()`/`Unsubscribe()`/`GetNewTriggers()`/`SetReceiveHandler()`
- **Skeleton 侧**: `skeleton::triggers::<TriggerName>` — `Send()` (无参)
- **命名空间**: 新增 `proxy::triggers` 和 `skeleton::triggers`（与 events/methods/fields 并列）

**所需改动**:

```
1. CFidlAst.hpp   — 新增 TriggerDef 节点 (仅含 name, 无 outArgs)
2. CFidlLexer.hpp — 新增 kTrigger keyword
3. CFidlParser.cpp — parseTriggerDef() 解析 "trigger <Name> { }"
4. CSchemaHash.cpp — 将 triggers 纳入 Hash 计算
5. CProxyGenerator  — proxy::triggers::<Name> : public ProxyTrigger
6. CSkeletonGenerator — skeleton::triggers::<Name> : public SkeletonTrigger
7. CDdsIdlGenerator — 无数据载荷，生成空 trigger marker
```

**Franca IDL 扩展** (建议语法):

```fidl
interface RadarService {
    // 现有元素
    broadcast ObjectsDetected { out { RadarObjectList objects } }
    method Calibrate { in { UInt32 mode } out { Boolean success } }
    attribute Float64 sensitivity notify

    // Trigger (自 R22-11 支持)
    trigger CalibrationComplete     // 无参数，仅通知
    trigger FaultDetected           // 故障触发
}
```

#### G2 — ServiceState API

ServiceState API 自 **R23-11** 起纳入规范，Proxy 级 `GetServiceState()` 和 `SetServiceStateChangeHandler()` 已正式发布：

- `enum class ServiceState : uint8_t { kNotAvailable = 0, kAvailable = 1 }`
- Proxy 需生成: `GetServiceState() noexcept → Result<ServiceState>`
- Proxy 需生成: `SetServiceStateChangeHandler(ServiceStateHandler)` / `UnsetServiceStateChangeHandler()`

#### G3 — Bitfield 数据类型

R25-11 + LanguageBinding 规范新增 Bitfield 类型支持 (SWS_LBAP_00065):

- C++ 表示: 基于 `ara::core::Bitset<N>` 的类（非 C 风格位域）
- 每个 CompuScale 生成一个类型别名 + getter/setter
- 需要 `#include "ara/core/bitset.h"` 依赖

**Franca IDL 扩展建议**: 复用或扩展 TypeCollection 语法:

```fidl
typeCollection SensorTypes {
    bitfield StatusFlags : UInt16 {
        ready       = 0..0      // bit 0
        calibrating = 1..1      // bit 1
        errorCode   = 2..5      // bits 2-5 (4 bits)
    }
}
```

### 15.2 头文件命名与命名空间合规差距

R25-11 对生成的头文件命名有严格规范，当前实现存在偏差：

| # | R25-11 规范 | SWS 需求 | 当前实现 | 差距 |
|---|-----------|---------|---------|------|
| **N1** | 头文件名: `<si-shortname-lower>_common.h` | SWS_CM_01012 | `<Interface>Types.hpp` | 文件名不合规 |
| **N2** | 头文件名: `<si-shortname-lower>_proxy.h` | SWS_CM_01015 | `<Interface>Proxy.hpp` | 同上 |
| **N3** | 头文件名: `<si-shortname-lower>_skeleton.h` | SWS_CM_01018 | `<Interface>Skeleton.hpp` | 同上 |
| **N4** | 命名空间全小写 | SWS_CM_11500 | PascalCase | 不合规 |
| **N5** | 移除自动 upper camel case 转换 | R25-11 Changelog | 强制 PascalCase | 不合规 |
| **N6** | 目录路径跟随命名空间层级 | SWS_CM_01005 | 平坦输出 | 不合规 |

**R25-11 要求的命名方案**:

```
当前输出:                           R25-11 规范要求:
─────────                           ──────────────
CalculatorTypes.hpp                → calculator_common.h
CalculatorProxy.hpp                → calculator_proxy.h
CalculatorSkeleton.hpp             → calculator_skeleton.h
gen/                               → org/lap/examples/
                                      calculator_common.h
                                      calculator_proxy.h
                                      calculator_skeleton.h

namespace Calculator { }           → namespace calculator { }
 (PascalCase)                        (lower-case per SWS_CM_11500)
```

**重构建议**: 新增 `--naming-style` CLI 选项:
- `--naming-style=legacy` — 当前行为 (向后兼容)
- `--naming-style=r25` — 完全合规 R25-11 命名 (默认)

### 15.3 Common Header 类成员差距

R25-11 §8.2.2 规定 Common Header 中的服务标识类成员：

| R25-11 成员 | 类型 | SWS 需求 | 当前生成 |
|-----------|------|---------|---------|
| `serviceContractVersionMajor` | `uint32_t` | SWS_CM_11508 | `kVersionMajor` (命名不同) |
| `serviceContractVersionMinor` | `uint32_t` | SWS_CM_11509 | `kVersionMinor` (命名不同) |
| `serviceIdentifier` | `ServiceIdentifierType` | SWS_CM_11506 | `kServiceId` (类型不同: UInt16) |
| `serviceVersion` | `ServiceVersionType` | SWS_CM_11507 | ❌ 未生成 |

**需要**:
1. 引入 `ServiceIdentifierType` 和 `ServiceVersionType` (§8.10, §8.11)
2. 成员命名改为 camelCase (R25-11 风格) 而非 k-前缀常量
3. 新增 `serviceVersion` 成员

### 15.4 Proxy 生成差距

| R25-11 Proxy API | SWS 需求 | 当前生成 | 差距 |
|-----------------|---------|---------|------|
| `proxy::triggers::<Name>` | SWS_CM_00721+ | ❌ | 缺失 trigger 命名空间 |
| `FindService(InstanceIdentifier)` | SWS_CM_00622 | ❌ | 仅生成 `Create(HandleType)` |
| `FindService(InstanceSpecifier)` | SWS_CM_00622 | ❌ | 同上 |
| `StartFindService(handler, ...)` (4 重载) | SWS_CM_00123 | ❌ | 未生成 |
| `StopFindService(handle)` | SWS_CM_00125 | ❌ | 未生成 |
| `GetServiceState()` | SWS_CM_01073 | ❌ | R23-11 新增 |
| `SetServiceStateChangeHandler(...)` (2 重载) | SWS_CM_01074+ | ❌ | R23-11 新增 |
| `UnsetServiceStateChangeHandler()` | — | ❌ | R23-11 新增 |
| `GetHandle()` | SWS_CM_00319 | ✅ | — |
| Move-only, Non-copyable | SWS_CM_11551-11554 | ✅ | — |
| HandleType 内部类 | SWS_CM_00312+ | ❌ | 未生成 |

### 15.5 Skeleton 生成差距

| R25-11 Skeleton API | SWS 需求 | 当前生成 | 差距 |
|-------------------|---------|---------|------|
| `skeleton::triggers::<Name>` | SWS_CM_00721+ | ❌ | 缺失 trigger 命名空间 |
| `Event::Allocate()` | SWS_CM_00136 | ❌ | zero-copy 分配 |
| `Event::Send(SampleAllocateePtr)` | SWS_CM_00306 | ❌ | zero-copy 发送 |
| `Event::GetSubscriptionState()` | SWS_CM_00710 | ❌ | 订阅状态查询 |
| `Event::SetSubscriptionStateChangeHandler()` | SWS_CM_00711 | ❌ | 订阅状态回调 |
| Field `RegisterGetHandler()`/`RegisterSetHandler()` | SWS_CM_00114/00116 | ❌ | 仅有结构，无 handler 注册 |
| `MethodCallProcessingMode` 参数 | SWS_CM_00130 | ✅ | — |
| Move-only, Non-copyable | SWS_CM_11544-11547 | ✅ | — |
| 析构自动 StopOffer | SWS_CM_11549 | ✅ | — |

### 15.6 现有已知限制（保留）

| 类别 | 当前状态 | 说明 |
|------|---------|------|
| **Union 类型** | 词法已定义，未解析 | Lexer 有 `kUnion` token，Parser 无 `parseUnionDef()` |
| **Const 声明** | 词法已定义，未解析 | Lexer 有 `kConst` token，Parser/Generator 未支持 |
| **Import 解析** | 仅解析未解决 | `imports[]` 已存储但不做跨文件类型解析 |
| **接口继承** | 仅解析未生成 | `extends` 已存储但生成的 C++ 类不产出继承 |
| **仅头文件输出** | 设计如此 | 所有输出为 `.hpp`，无 `.cpp` 生成 |
| **无 .fdepl 支持** | 未实现 | 无 Franca 部署文件支持 |
| **无增量生成** | 每次全量 | 无 up-to-date 检查 |
| **errorArgs** | 仅解析未生成 | `MethodDef::errorArgs` 未反映到 Proxy/Skeleton 签名 |
| **Map 类型 OMG IDL 映射** | 未实现 | `MapDef` 有 C++ 生成，但 OMG IDL 无显式 map 类型，需用结构体模拟 |
| **语义校验** | 未实现 | 无类型存在性检查、循环继承检测、重名检测 |
| **单文件处理** | 当前限制 | CLI 仅接受单个 `--input`；无批量/目录处理 |
| **SHA-256 验证** | 手写实现 | 纯 C++ 实现，未对 NIST 测试向量进行自动化验证 |

### 15.7 重构优先级路线图

```
Phase 1 — R25-11 核心合规 (v1.1.0)
────────────────────────────────────
 [P0] G1: Trigger 原语 (AST + Parser + 3 Generators)
 [P1] N1-N6: 头文件命名 + 命名空间合规 (--naming-style=r25)
 [P1] G5: kCommunicationFailure 错误码
 [P1] §15.3: Common Header 成员对齐 (serviceIdentifier/serviceVersion)

Phase 2 — API 完善 (v1.2.0)
────────────────────────────
 [P1] G2: ServiceState API (GetServiceState + handler)
 [P1] G7: FindService/StartFindService/StopFindService 重载
 [P1] §15.4: HandleType 内部类生成
 [P1] G3: Bitfield 数据类型 (ara::core::Bitset)
 [P1] §15.5: Skeleton Event Allocate/Send/SubscriptionState

Phase 3 — 高级特性 (v1.3.0)
────────────────────────────
 [P2] G4: Inhibit Time 监控元数据
 [P2] G6: SampleAllocateePtr zero-copy 发送
 [P2] errorArgs → Application Error Domain 生成
 [P2] 语义校验 (类型存在性/循环继承/重名检测)
 [P2] Import 跨文件解析 + 批量处理
```

---

## 16. 相关文档

- [ARCHITECTURE_SUMMARY.md](ARCHITECTURE_SUMMARY.md) — Com 模块整体架构
- [ARCHITECTURE_SUMMARY.md §9](ARCHITECTURE_SUMMARY.md#9-序列化策略-符合-autosar-标准) — 序列化策略 (IDL 驱动)
- [ARCHITECTURE_SUMMARY.md §13](ARCHITECTURE_SUMMARY.md#13-配置管理与工具链) — 配置管理与工具链 (含 arxml2yaml)
- [SERVICE_DISCOVERY_ARCHITECTURE.md §5.6](SERVICE_DISCOVERY_ARCHITECTURE.md#56-接口一致性保证机制) — 服务发现中的接口一致性验证
- [`archive/LEGACY_COMMONAPI_IMPLEMENTATION.md`](../archive/LEGACY_COMMONAPI_IMPLEMENTATION.md) — 旧 CommonAPI/Franca IDL 工作流（已归档）
