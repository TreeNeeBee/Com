# YAML 统一配置格式 - 实施总结

**日期**: 2025-11-19  
**版本**: 1.0  
**状态**: ✅ 完成

---

## 1. 目标

将 LightAP 通信模块所有配置文件统一为 YAML 格式，并提供 AUTOSAR ARXML 到 YAML 的自动转换工具。

---

## 2. 已完成的工作

### 2.1 ARXML 到 YAML 转换工具

✅ **arxml2yaml.py** - 主转换工具
- 路径: `modules/Com/tools/arxml2yaml/arxml2yaml.py`
- 功能:
  - 解析 AUTOSAR ARXML 服务清单
  - 提取服务接口定义 (SERVICE-INTERFACE)
  - 提取服务实例配置 (PROVIDED/REQUIRED-SERVICE-INSTANCE)
  - 自动计算服务 ID (FNV-1a 哈希)
  - 智能槽位分配 (静态/动态/ASIL-D)
  - 生成完整 YAML 配置

✅ **README.md** - 工具文档
- 路径: `modules/Com/tools/arxml2yaml/README.md`
- 内容:
  - 使用方法和示例
  - 槽位分配策略说明
  - CMake/Makefile 集成示例
  - 故障排查指南

✅ **service_manifest.arxml** - 测试示例
- 路径: `modules/Com/tools/arxml2yaml/examples/service_manifest.arxml`
- 包含 4 个服务定义:
  - PerceptionService (感知)
  - PlanningService (规划)
  - VehicleControlService (控制, ASIL-D)
  - StateManagerService (状态管理)

✅ **test_conversion.sh** - 自动化测试
- 路径: `modules/Com/tools/arxml2yaml/test/test_conversion.sh`
- 测试覆盖:
  - 基本转换功能
  - YAML 语法验证
  - 槽位范围检查
  - 服务 ID 唯一性
  - 多文件合并
  - 分配策略切换

**测试结果**: ✅ 所有测试通过

```
[Test 1] Basic ARXML conversion... ✅ PASSED
[Test 2] Validating YAML syntax... ✅ PASSED
[Test 3] Verifying slot allocation ranges... ✅ PASSED
[Test 4] Checking service ID uniqueness... ✅ PASSED
[Test 5] Testing multiple ARXML input... ✅ PASSED
[Test 6] Testing static allocation strategy... ✅ PASSED
```

### 2.2 CMake 集成

✅ **ArxmlToYaml.cmake** - CMake 模块
- 路径: `modules/Com/cmake/ArxmlToYaml.cmake`
- 功能:
  - `arxml_to_yaml()` 函数 - 自动转换 ARXML 到 YAML
  - `install_yaml_config()` 函数 - 安装配置文件
  - 支持依赖追踪 (ARXML 文件修改时自动重新生成)

**使用示例**:

```cmake
include(cmake/ArxmlToYaml.cmake)

file(GLOB ARXML_FILES "${CMAKE_SOURCE_DIR}/config/*.arxml")

arxml_to_yaml(
    TARGET_NAME generate_slot_mapping
    ARXML_FILES ${ARXML_FILES}
    OUTPUT_FILE ${CMAKE_BINARY_DIR}/slot_mapping.yaml
    STRATEGY auto
    VERBOSE
)

install_yaml_config(
    YAML_FILE ${CMAKE_BINARY_DIR}/slot_mapping.yaml
    DESTINATION /etc/lap/com
)
```

### 2.3 YAML 配置文件更新

✅ **slot_mapping.yaml** - 服务槽位映射
- 路径: `modules/Com/source/config/slot_mapping.yaml`
- 更新内容:
  - 添加 ARXML 转换说明
  - 版本升级到 3.1
  - 保持现有 30+ 服务定义

✅ **allowed_executables.yaml** - 可执行文件白名单
- 路径: `modules/Com/daemon/systemd/allowed_executables.yaml`
- 内容:
  - ASIL-D 级可执行文件定义
  - QM 级可执行文件定义
  - SHA256 哈希验证配置
  - 安全策略设置

✅ **installation_config.yaml** - 安装配置
- 路径: `modules/Com/daemon/systemd/installation_config.yaml`
- 替代硬编码的 install.sh
- 内容:
  - 用户和组定义
  - systemd 单元文件列表
  - 可执行文件路径
  - 配置文件路径
  - 安装步骤定义
  - 验证检查项
  - 故障排查指南

### 2.4 基于 YAML 的安装脚本

✅ **install_yaml.py** - Python 安装脚本
- 路径: `modules/Com/daemon/systemd/install_yaml.py`
- 功能:
  - 读取 installation_config.yaml
  - 创建用户和组
  - 安装 systemd 单元
  - 安装可执行文件和配置
  - 启用和启动服务
  - 验证安装完整性
  - 支持 dry-run 模式

**使用方法**:

```bash
# 安装所有组件
sudo ./install_yaml.py

# Dry-run (预览操作)
sudo ./install_yaml.py --dry-run

# 验证安装
sudo ./install_yaml.py --verify

# 自定义配置文件
sudo ./install_yaml.py --config my_config.yaml
```

---

## 3. 配置文件层次结构

```text
LightAP/modules/Com/
├── source/config/
│   └── slot_mapping.yaml              # 服务槽位映射 (核心配置)
│
├── daemon/systemd/
│   ├── allowed_executables.yaml       # 可执行文件白名单 (安全配置)
│   ├── installation_config.yaml       # 安装配置 (部署配置)
│   ├── install_yaml.py                # 基于 YAML 的安装脚本
│   ├── lap-registry.socket            # systemd socket 单元
│   ├── lap-registry-init.service      # systemd service 单元
│   ├── lap-registry-asil.socket       # ASIL-D socket 单元
│   └── lap-registry-asil-init.service # ASIL-D service 单元
│
├── tools/arxml2yaml/
│   ├── arxml2yaml.py                  # ARXML 转换工具
│   ├── README.md                      # 工具文档
│   ├── examples/
│   │   └── service_manifest.arxml     # 示例 ARXML
│   └── test/
│       ├── test_conversion.sh         # 自动化测试
│       └── output/                    # 测试输出
│
└── cmake/
    └── ArxmlToYaml.cmake              # CMake 集成模块
```

---

## 4. YAML 配置格式规范

### 4.1 slot_mapping.yaml 结构

```yaml
version: "1.1"

metadata:
  generated_by: "arxml2yaml.py"  # or "manual"
  source: "AUTOSAR ARXML"
  date: "2025-11-19"
  total_services: 30

slot_mapping:
  static_allocations:          # 静态槽位 (0-199)
    - slot_index: 10
      service_name: "PerceptionService"
      service_id: "0x12345678"
      instance_id: 1
      version: "1.0"
      binding: "iceoryx2"
      endpoint: "/lap/perception"
      safety_level: "QM"
      description: "..."
  
  dynamic_allocations:         # 动态槽位 (200-923)
    - ...
  
  asil_allocations:            # ASIL-D 槽位 (924-1023)
    - ...

system_config:
  total_slots: 1024
  static_range: "0-199"
  dynamic_range: "200-923"
  asil_range: "924-1023"
  enable_hugepages: true
  enable_guard_pages: true

security:
  enable_crc32_check: true
  enable_exec_whitelist: true
  qm_asil_separation: true
```

### 4.2 allowed_executables.yaml 结构

```yaml
version: "1.0"

asil_d_executables:
  - path: "/usr/bin/lap-control"
    sha256: "abc123..."
    permissions:
      read_qm: true
      write_qm: false
      read_asil: true
      write_asil: true

qm_executables:
  - path: "/usr/bin/lap-perception"
    sha256: "def456..."
    permissions:
      write_asil: false

security:
  enable_hash_verification: true
  allow_unlisted_executables: false
  violation_policy:
    terminate_on_violation: true
```

### 4.3 installation_config.yaml 结构

```yaml
installation:
  name: "LightAP Service Registry"
  version: "3.1"
  
  paths:
    systemd_unit_dir: "/etc/systemd/system"
    lap_config_dir: "/etc/lap/com"
    # ...
  
  groups:
    - name: "lap-control"
      gid: 1000
  
  systemd_units:
    - name: "lap-registry.socket"
      source: "daemon/systemd/lap-registry.socket"
      enabled: true
  
  config_files:
    - name: "slot_mapping.yaml"
      source: "source/config/slot_mapping.yaml"
      destination: "/etc/lap/com/slot_mapping.yaml"

installation_steps:
  install:
    - action: "create_users"
    - action: "install_systemd_units"
    # ...

verification:
  checks:
    - name: "socket_exists"
      files:
        - "/run/lap/registry.sock"
```

---

## 5. ARXML 到 YAML 转换流程

```text
┌─────────────────────┐
│   ARXML 文件        │
│  (AUTOSAR 标准)     │
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│  arxml2yaml.py      │
│  - 解析 XML         │
│  - 提取服务定义     │
│  - 计算服务 ID      │
│  - 分配槽位         │
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│  YAML 配置          │
│  slot_mapping.yaml  │
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│  部署到系统         │
│  /etc/lap/com/      │
└─────────────────────┘
```

---

## 6. 优势总结

### 6.1 统一性
- ✅ 所有配置使用 YAML 格式
- ✅ 结构化、可读性强
- ✅ 支持注释和文档

### 6.2 自动化
- ✅ ARXML 自动转换
- ✅ CMake 集成
- ✅ 自动化测试

### 6.3 可维护性
- ✅ 配置与代码分离
- ✅ 版本控制友好
- ✅ 易于审查和修改

### 6.4 安全性
- ✅ SHA256 哈希验证
- ✅ 权限配置集中管理
- ✅ 审计友好

### 6.5 AUTOSAR 兼容性
- ✅ 支持标准 ARXML 输入
- ✅ 保留服务版本信息
- ✅ 符合 AP R24-11 规范

---

## 7. 后续工作建议

### 7.1 短期 (1-2 周)
- [ ] 完善 ARXML 解析器 (支持更多 AUTOSAR 元素)
- [ ] 添加 YAML 配置验证工具 (schema validation)
- [ ] 实现 install_yaml.py 的卸载功能

### 7.2 中期 (1 个月)
- [ ] 集成到 CI/CD 流程
- [ ] 创建配置管理 GUI 工具
- [ ] 支持配置热更新 (无需重启)

### 7.3 长期 (3 个月)
- [ ] 支持配置加密
- [ ] 实现配置版本管理
- [ ] 支持分布式配置 (etcd/Consul)

---

## 8. 文档清单

| 文档 | 路径 | 描述 |
|------|------|------|
| ARXML 转换工具文档 | `tools/arxml2yaml/README.md` | 使用说明、示例 |
| CMake 集成文档 | `cmake/ArxmlToYaml.cmake` | 构建集成 |
| 安装配置说明 | `daemon/systemd/installation_config.yaml` | 部署配置 |
| 安全架构总结 | `doc/SECURITY_ARCHITECTURE_SUMMARY.md` | 安全设计 |
| 服务发现架构 | `doc/SERVICE_DISCOVERY_ARCHITECTURE.md` | 核心架构 |

---

## 9. 命令快速参考

```bash
# 转换 ARXML 到 YAML
cd modules/Com/tools/arxml2yaml
./arxml2yaml.py -i examples/service_manifest.arxml -o output.yaml -v

# 运行测试
cd test
./test_conversion.sh

# 安装系统组件
cd ../../daemon/systemd
sudo ./install_yaml.py --dry-run  # 预览
sudo ./install_yaml.py            # 安装

# 验证安装
sudo ./install_yaml.py --verify

# CMake 构建 (自动转换 ARXML)
mkdir build && cd build
cmake ..
make generate_slot_mapping
```

---

**完成时间**: 2025-11-19  
**维护者**: LightAP Architecture Team  
**状态**: ✅ 生产就绪
