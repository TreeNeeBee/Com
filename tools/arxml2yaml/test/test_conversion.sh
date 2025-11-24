#!/bin/bash
#
# Test script for ARXML to YAML converter
#
# Usage:
#   ./test_conversion.sh
#
# Expected output:
#   - Generated YAML configuration
#   - Validation results
#   - Statistics
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONVERTER="$SCRIPT_DIR/../arxml2yaml.py"
EXAMPLE_ARXML="$SCRIPT_DIR/../examples/service_manifest.arxml"
OUTPUT_YAML="$SCRIPT_DIR/output/slot_mapping_test.yaml"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================="
echo "ARXML to YAML Converter Test"
echo "========================================="
echo ""

# Create output directory
mkdir -p "$SCRIPT_DIR/output"

# Test 1: Basic conversion
echo -e "${YELLOW}[Test 1]${NC} Basic ARXML conversion..."
python3 "$CONVERTER" \
    -i "$EXAMPLE_ARXML" \
    -o "$OUTPUT_YAML" \
    -v

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✅ Test 1 PASSED${NC}"
else
    echo -e "${RED}❌ Test 1 FAILED${NC}"
    exit 1
fi

echo ""

# Test 2: Validate YAML syntax
echo -e "${YELLOW}[Test 2]${NC} Validating YAML syntax..."
python3 -c "
import yaml
with open('$OUTPUT_YAML', 'r') as f:
    config = yaml.safe_load(f)
    print(f'  Total services: {config[\"metadata\"][\"total_services\"]}')
    print(f'  Static slots: {len(config[\"slot_mapping\"][\"static_allocations\"])}')
    print(f'  Dynamic slots: {len(config[\"slot_mapping\"][\"dynamic_allocations\"])}')
    print(f'  ASIL-D slots: {len(config[\"slot_mapping\"][\"asil_allocations\"])}')
"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✅ Test 2 PASSED${NC}"
else
    echo -e "${RED}❌ Test 2 FAILED${NC}"
    exit 1
fi

echo ""

# Test 3: Check slot allocation ranges
echo -e "${YELLOW}[Test 3]${NC} Verifying slot allocation ranges..."
python3 -c "
import yaml

with open('$OUTPUT_YAML', 'r') as f:
    config = yaml.safe_load(f)
    
# Check static slots (0-199)
for entry in config['slot_mapping']['static_allocations']:
    slot = entry['slot_index']
    if not (0 <= slot <= 199):
        print(f'ERROR: Static slot {slot} out of range!')
        exit(1)

# Check dynamic slots (200-923)
for entry in config['slot_mapping']['dynamic_allocations']:
    slot = entry['slot_index']
    if not (200 <= slot <= 923):
        print(f'ERROR: Dynamic slot {slot} out of range!')
        exit(1)

# Check ASIL-D slots (924-1023)
for entry in config['slot_mapping']['asil_allocations']:
    slot = entry['slot_index']
    if not (924 <= slot <= 1023):
        print(f'ERROR: ASIL-D slot {slot} out of range!')
        exit(1)

print('  All slots within valid ranges')
"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✅ Test 3 PASSED${NC}"
else
    echo -e "${RED}❌ Test 3 FAILED${NC}"
    exit 1
fi

echo ""

# Test 4: Verify service IDs are unique
echo -e "${YELLOW}[Test 4]${NC} Checking service ID uniqueness..."
python3 -c "
import yaml

with open('$OUTPUT_YAML', 'r') as f:
    config = yaml.safe_load(f)

service_ids = set()
all_services = (
    config['slot_mapping']['static_allocations'] +
    config['slot_mapping']['dynamic_allocations'] +
    config['slot_mapping']['asil_allocations']
)

for entry in all_services:
    svc_id = entry['service_id']
    if svc_id in service_ids:
        print(f'ERROR: Duplicate service ID {svc_id}')
        exit(1)
    service_ids.add(svc_id)

print(f'  All {len(service_ids)} service IDs are unique')
"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✅ Test 4 PASSED${NC}"
else
    echo -e "${RED}❌ Test 4 FAILED${NC}"
    exit 1
fi

echo ""

# Test 5: Multiple ARXML files
echo -e "${YELLOW}[Test 5]${NC} Testing multiple ARXML input..."
python3 "$CONVERTER" \
    -i "$EXAMPLE_ARXML" "$EXAMPLE_ARXML" \
    -o "$SCRIPT_DIR/output/merged_test.yaml" \
    2>&1 | grep -q "Total services"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✅ Test 5 PASSED${NC}"
else
    echo -e "${RED}❌ Test 5 FAILED${NC}"
    exit 1
fi

echo ""

# Test 6: Static allocation strategy
echo -e "${YELLOW}[Test 6]${NC} Testing static allocation strategy..."
python3 "$CONVERTER" \
    -i "$EXAMPLE_ARXML" \
    -o "$SCRIPT_DIR/output/static_test.yaml" \
    --strategy static \
    2>&1 | grep -q "Generated YAML"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✅ Test 6 PASSED${NC}"
else
    echo -e "${RED}❌ Test 6 FAILED${NC}"
    exit 1
fi

echo ""
echo "========================================="
echo -e "${GREEN}All tests passed!${NC}"
echo "========================================="
echo ""

# Display generated YAML
echo "Generated YAML preview:"
echo "--------------------------------------"
head -n 30 "$OUTPUT_YAML"
echo "..."
echo "--------------------------------------"
echo ""
echo "Full output: $OUTPUT_YAML"
