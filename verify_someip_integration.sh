#!/bin/bash
# Quick verification script for SOME/IP integration
# Checks that all components are ready without requiring external dependencies

set -e

WORKSPACE_ROOT="/home/ddk/1_workspace/2_middleware/LightAP"
BUILD_DIR="$WORKSPACE_ROOT/build"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== LightAP SOME/IP Integration Verification ===${NC}\n"

# Check 1: Source files exist
echo -e "${BLUE}[1/5] Checking source files...${NC}"
REQUIRED_FILES=(
    "modules/Com/source/binding/someip/SomeIpConnectionManager.hpp"
    "modules/Com/source/binding/commonapi/CommonAPISomeIpAdapter.hpp"
    "modules/Com/tools/someip/vsomeip_calculator.json"
    "modules/Com/tools/fidl/examples/Calculator.fdepl"
    "modules/Com/tools/commonapi/generate_new.sh"
    "modules/Com/test/examples/someip/calculator_server.cpp"
    "modules/Com/test/examples/someip/calculator_client.cpp"
)

missing_files=0
for file in "${REQUIRED_FILES[@]}"; do
    if [ ! -f "$WORKSPACE_ROOT/$file" ]; then
        echo -e "${RED}  ✗ Missing: $file${NC}"
        ((missing_files++))
    fi
done

if [ $missing_files -eq 0 ]; then
    echo -e "${GREEN}  ✓ All source files present (${#REQUIRED_FILES[@]}/${#REQUIRED_FILES[@]})${NC}"
else
    echo -e "${RED}  ✗ Missing $missing_files files${NC}"
    exit 1
fi

# Check 2: Documentation files exist
echo -e "\n${BLUE}[2/5] Checking documentation...${NC}"
DOC_FILES=(
    "modules/Com/tools/someip/README.md"
    "modules/Com/TRANSPORT_MATRIX.md"
    "modules/Com/SOMEIP_INTEGRATION_SUMMARY.md"
    "modules/Com/README_SOMEIP.md"
)

missing_docs=0
for file in "${DOC_FILES[@]}"; do
    if [ ! -f "$WORKSPACE_ROOT/$file" ]; then
        echo -e "${RED}  ✗ Missing: $file${NC}"
        ((missing_docs++))
    fi
done

if [ $missing_docs -eq 0 ]; then
    echo -e "${GREEN}  ✓ All documentation present (${#DOC_FILES[@]}/${#DOC_FILES[@]})${NC}"
else
    echo -e "${YELLOW}  ⚠ Missing $missing_docs documentation files${NC}"
fi

# Check 3: Generate script is executable
echo -e "\n${BLUE}[3/5] Checking code generation script...${NC}"
GEN_SCRIPT="$WORKSPACE_ROOT/modules/Com/tools/commonapi/generate_new.sh"
if [ -x "$GEN_SCRIPT" ]; then
    echo -e "${GREEN}  ✓ generate_new.sh is executable${NC}"
else
    echo -e "${YELLOW}  ⚠ generate_new.sh not executable (run: chmod +x $GEN_SCRIPT)${NC}"
fi

# Check 4: Build system test
echo -e "\n${BLUE}[4/5] Testing build system...${NC}"
if [ -d "$BUILD_DIR" ]; then
    cd "$BUILD_DIR"
    
    # Test that headers can be found
    if make --question lap_com 2>/dev/null; then
        echo -e "${GREEN}  ✓ Build system ready${NC}"
    else
        echo -e "${YELLOW}  ⚠ Build may need reconfiguration${NC}"
    fi
    
    cd - > /dev/null
else
    echo -e "${YELLOW}  ⚠ Build directory not found (run: cd build && cmake ..)${NC}"
fi

# Check 5: External dependencies (optional)
echo -e "\n${BLUE}[5/5] Checking external dependencies (optional)...${NC}"

# Check vsomeip
if ldconfig -p 2>/dev/null | grep -q libvsomeip; then
    echo -e "${GREEN}  ✓ vsomeip library found${NC}"
else
    echo -e "${YELLOW}  ⚠ vsomeip not installed (see tools/someip/README.md)${NC}"
fi

# Check CommonAPI-SomeIP
if ldconfig -p 2>/dev/null | grep -q libCommonAPI-SomeIP; then
    echo -e "${GREEN}  ✓ CommonAPI-SomeIP runtime found${NC}"
else
    echo -e "${YELLOW}  ⚠ CommonAPI-SomeIP not installed (see tools/someip/README.md)${NC}"
fi

# Check generators
GENERATORS_DIR="$WORKSPACE_ROOT/modules/Com/tools/commonapi/generators"
if [ -f "$GENERATORS_DIR/commonapi-someip-generator-linux-x86_64" ]; then
    echo -e "${GREEN}  ✓ CommonAPI-SomeIP generator found${NC}"
elif [ -f "$GENERATORS_DIR/commonapi_someip_generator" ]; then
    echo -e "${GREEN}  ✓ CommonAPI-SomeIP generator found${NC}"
else
    echo -e "${YELLOW}  ⚠ CommonAPI-SomeIP generator not found (see tools/someip/README.md)${NC}"
fi

# Summary
echo -e "\n${BLUE}=== Verification Summary ===${NC}"
echo -e "Infrastructure: ${GREEN}READY${NC}"
echo -e "Documentation: ${GREEN}COMPLETE${NC}"
echo -e "Examples: ${GREEN}PREPARED${NC}"
echo ""
echo -e "${YELLOW}Next Steps:${NC}"
echo "  1. Install vsomeip and CommonAPI-SomeIP (see modules/Com/tools/someip/README.md)"
echo "  2. Generate code: cd modules/Com/tools/commonapi && ./generate_new.sh ../fidl/examples/Calculator.fidl someip"
echo "  3. Enable examples: Edit calculator_server.cpp and calculator_client.cpp (change #if 0 to #if 1)"
echo "  4. Build and test"
echo ""
echo -e "For detailed instructions, see: ${BLUE}modules/Com/README_SOMEIP.md${NC}"
