# ARXML to YAML Converter

将 AUTOSAR Adaptive Platform ARXML 服务清单转换为 LightAP YAML 配置格式。

## 功能特性

- ✅ 解析 ARXML 服务接口定义 (SERVICE-INTERFACE)
- ✅ 提取服务实例配置 (PROVIDED/REQUIRED-SERVICE-INSTANCE)
- ✅ 自动计算服务 ID (FNV-1a 哈希)
- ✅ 智能槽位分配 (静态/动态/ASIL-D)
- ✅ 支持多个 ARXML 文件合并
- ✅ 生成完整 YAML 配置 (slot_mapping.yaml)

## 依赖项

```bash
pip install PyYAML
```

## 使用方法

### 基本用法

```bash
# 转换单个 ARXML 文件
./arxml2yaml.py -i service_manifest.arxml -o slot_mapping.yaml

# 转换多个 ARXML 文件
./arxml2yaml.py -i services/*.arxml -o slot_mapping.yaml

# 使用静态分配策略
./arxml2yaml.py -i manifest.arxml -o config.yaml --strategy static

# 详细输出
./arxml2yaml.py -i manifest.arxml -o config.yaml -v
```

### 命令行选项

| 选项 | 说明 |
|------|------|
| `-i, --input` | 输入 ARXML 文件 (可多个) |
| `-o, --output` | 输出 YAML 文件 |
| `--strategy` | 槽位分配策略: auto/static/dynamic (默认: auto) |
| `--validate` | 验证 ARXML 符合 AUTOSAR 规范 |
| `-v, --verbose` | 详细输出 |

## ARXML 输入示例

```xml
<?xml version="1.0" encoding="UTF-8"?>
<AUTOSAR xmlns="http://autosar.org/schema/r4.0">
  <AR-PACKAGES>
    <AR-PACKAGE>
      <SHORT-NAME>Services</SHORT-NAME>
      
      <!-- Service Interface Definition -->
      <ELEMENTS>
        <SERVICE-INTERFACE>
          <SHORT-NAME>PerceptionService</SHORT-NAME>
          <MAJOR-VERSION>1</MAJOR-VERSION>
          <MINOR-VERSION>0</MINOR-VERSION>
          <DESC>Sensor fusion and object detection</DESC>
        </SERVICE-INTERFACE>
      </ELEMENTS>
      
      <!-- Service Instance Configuration -->
      <SERVICE-INSTANCES>
        <PROVIDED-SERVICE-INSTANCE>
          <SHORT-NAME>PerceptionInstance1</SHORT-NAME>
          <SERVICE-INTERFACE-REF>/Services/PerceptionService</SERVICE-INTERFACE-REF>
          <INSTANCE-ID>1</INSTANCE-ID>
          <COMMUNICATION-CONNECTOR>
            <TYPE>ICEORYX2</TYPE>
            <ENDPOINT>/lap/perception</ENDPOINT>
          </COMMUNICATION-CONNECTOR>
        </PROVIDED-SERVICE-INSTANCE>
      </SERVICE-INSTANCES>
      
    </AR-PACKAGE>
  </AR-PACKAGES>
</AUTOSAR>
```

## YAML 输出示例

```yaml
version: '1.0'

metadata:
  generated_by: arxml2yaml.py
  source: AUTOSAR ARXML
  date: '2025-11-19'
  total_services: 1

slot_mapping:
  static_allocations:
    - slot_index: 10
      service_name: PerceptionService
      service_id: '0x12345678'
      instance_id: 1
      version: '1.0'
      binding: iceoryx2
      endpoint: /lap/perception
      safety_level: QM
      description: Sensor fusion and object detection
  
  dynamic_allocations: []
  
  asil_allocations: []

system_config:
  total_slots: 1024
  static_range: 0-199
  dynamic_range: 200-923
  asil_range: 924-1023
  enable_hugepages: true
  enable_guard_pages: true

security:
  enable_crc32_check: true
  enable_exec_whitelist: true
  qm_asil_separation: true
```

## 槽位分配策略

### Auto (自动)

- **静态服务**: 使用哈希分配到 0-199
- **动态服务**: 顺序分配到 200-923
- **ASIL-D 服务**: 顺序分配到 924-1023

### Static (静态)

- 所有服务使用一致性哈希分配到 0-199
- 适合服务数量固定的场景

### Dynamic (动态)

- 所有 QM 服务顺序分配到 200-923
- 适合服务数量动态变化的场景

## 服务 ID 计算

使用 FNV-1a 哈希算法从服务名称计算 32 位服务 ID：

```python
def compute_service_id(service_name: str) -> int:
    FNV_PRIME = 0x01000193
    FNV_OFFSET = 0x811c9dc5
    
    hash_value = FNV_OFFSET
    for byte in service_name.encode('utf-8'):
        hash_value ^= byte
        hash_value = (hash_value * FNV_PRIME) & 0xFFFFFFFF
    
    return hash_value
```

## 安全等级推断

自动从服务名称推断安全等级：

- 包含 `control` 或 `safety` → ASIL-D (槽位 924-1023)
- 其他 → QM (槽位 0-923)

可在生成的 YAML 中手动调整。

## 集成到构建流程

### CMakeLists.txt

```cmake
# 添加 ARXML 转换目标
find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/slot_mapping.yaml
    COMMAND ${Python3_EXECUTABLE} 
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/arxml2yaml/arxml2yaml.py
            -i ${CMAKE_CURRENT_SOURCE_DIR}/config/*.arxml
            -o ${CMAKE_CURRENT_BINARY_DIR}/slot_mapping.yaml
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/config/*.arxml
    COMMENT "Converting ARXML to YAML configuration"
)

add_custom_target(generate_config ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/slot_mapping.yaml
)
```

### Makefile

```makefile
ARXML_FILES = $(wildcard config/*.arxml)
YAML_CONFIG = build/slot_mapping.yaml

$(YAML_CONFIG): $(ARXML_FILES)
	@python3 tools/arxml2yaml/arxml2yaml.py \
		-i $(ARXML_FILES) \
		-o $@ \
		-v

.PHONY: config
config: $(YAML_CONFIG)
```

## 验证输出

```bash
# 检查 YAML 语法
yamllint slot_mapping.yaml

# 查看生成的槽位分配
yq '.slot_mapping.static_allocations[] | .slot_index + ": " + .service_name' slot_mapping.yaml

# 统计槽位使用情况
yq '.metadata.total_services' slot_mapping.yaml
```

## 扩展功能

### 自定义槽位映射

创建 `custom_mapping.json` 指定特定服务的槽位：

```json
{
  "PerceptionService": 10,
  "PlanningService": 20,
  "ControlService": 950
}
```

使用自定义映射：

```bash
./arxml2yaml.py -i manifest.arxml -o config.yaml --custom-mapping custom_mapping.json
```

### 批量处理

```bash
#!/bin/bash
# 批量转换多个项目的 ARXML

for project in perception planning control; do
    ./arxml2yaml.py \
        -i projects/${project}/*.arxml \
        -o configs/${project}_slots.yaml \
        -v
done

# 合并所有配置
yq eval-all 'select(fileIndex == 0) * select(fileIndex > 0)' \
    configs/*.yaml > final_config.yaml
```

## 故障排查

### 问题 1: 找不到服务接口

**原因**: ARXML 文件中缺少 SERVICE-INTERFACE 定义

**解决**: 确保 ARXML 包含完整的服务接口定义

### 问题 2: 槽位冲突

**原因**: 静态分配时哈希冲突

**解决**: 使用 `--strategy dynamic` 或手动指定槽位

### 问题 3: 编码错误

**原因**: ARXML 文件编码不是 UTF-8

**解决**:

```bash
iconv -f GB2312 -t UTF-8 input.arxml > output.arxml
./arxml2yaml.py -i output.arxml -o config.yaml
```

## 性能

- **解析速度**: ~1000 服务/秒
- **内存占用**: < 50MB (1000 服务)
- **输出大小**: ~1KB/服务

## 版本历史

- **1.0** (2025-11-19): 初始版本
  - ARXML 解析
  - YAML 生成
  - 槽位分配策略

## 许可证

Copyright (c) 2025 LightAP Project
