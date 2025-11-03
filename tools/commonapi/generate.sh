#!/bin/bash
# CommonAPI Code Generator Script
# Generates C++ Proxy/Stub from Franca IDL for D-Bus communication

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GENERATORS_DIR="${SCRIPT_DIR}/generators"
CORE_GEN="${GENERATORS_DIR}/commonapi-core-generator-linux-x86_64"
DBUS_GEN="${GENERATORS_DIR}/commonapi-dbus-generator-linux-x86_64"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_usage() {
    echo "Usage: $0 <fidl-file> [output-dir]"
    echo ""
    echo "Arguments:"
    echo "  fidl-file    Path to Franca IDL file (.fidl)"
    echo "  output-dir   Output directory for generated code (default: ../generated)"
    echo ""
    echo "Example:"
    echo "  $0 ../fidl/examples/Calculator.fidl"
    echo "  $0 ../fidl/Calculator.fidl ../generated"
}

check_generators() {
    if [ ! -f "${CORE_GEN}" ]; then
        echo -e "${RED}Error: CommonAPI Core Generator not found${NC}"
        echo "Expected: ${CORE_GEN}"
        echo ""
        echo "Download from: https://github.com/COVESA/capicxx-core-tools/releases"
        exit 1
    fi

    if [ ! -f "${DBUS_GEN}" ]; then
        echo -e "${RED}Error: CommonAPI D-Bus Generator not found${NC}"
        echo "Expected: ${DBUS_GEN}"
        echo ""
        echo "Download from: https://github.com/COVESA/capicxx-dbus-tools/releases"
        exit 1
    fi

    if [ ! -x "${CORE_GEN}" ]; then
        echo -e "${YELLOW}Warning: Core generator not executable, fixing...${NC}"
        chmod +x "${CORE_GEN}"
    fi

    if [ ! -x "${DBUS_GEN}" ]; then
        echo -e "${YELLOW}Warning: D-Bus generator not executable, fixing...${NC}"
        chmod +x "${DBUS_GEN}"
    fi
}

# Parse arguments
if [ $# -lt 1 ]; then
    print_usage
    exit 1
fi

FIDL_FILE="$1"
OUTPUT_DIR="${2:-${SCRIPT_DIR}/../generated}"

# Validate input
if [ ! -f "${FIDL_FILE}" ]; then
    echo -e "${RED}Error: Franca IDL file not found: ${FIDL_FILE}${NC}"
    exit 1
fi

if [[ ! "${FIDL_FILE}" =~ \.fidl$ ]]; then
    echo -e "${RED}Error: File must have .fidl extension${NC}"
    exit 1
fi

# Check generators
check_generators

# Create output directory
mkdir -p "${OUTPUT_DIR}"
OUTPUT_DIR="$(cd "${OUTPUT_DIR}" && pwd)"

echo -e "${GREEN}=== CommonAPI Code Generation ===${NC}"
echo "FIDL file: ${FIDL_FILE}"
echo "Output directory: ${OUTPUT_DIR}"
echo ""

# Step 1: Generate Core C++ (Proxy/Stub interfaces)
echo -e "${YELLOW}[1/2] Generating CommonAPI Core code...${NC}"
"${CORE_GEN}" -dest "${OUTPUT_DIR}" "${FIDL_FILE}"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Core generation complete${NC}"
else
    echo -e "${RED}✗ Core generation failed${NC}"
    exit 1
fi

# Step 2: Generate D-Bus binding
echo -e "${YELLOW}[2/2] Generating D-Bus binding code...${NC}"
"${DBUS_GEN}" -dest "${OUTPUT_DIR}" "${FIDL_FILE}"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ D-Bus generation complete${NC}"
else
    echo -e "${RED}✗ D-Bus generation failed${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}=== Generation Successful ===${NC}"
echo "Generated files are in: ${OUTPUT_DIR}"
echo ""
echo "Next steps:"
echo "1. Review generated code in ${OUTPUT_DIR}"
echo "2. Add generated files to your CMakeLists.txt"
echo "3. Implement service logic in StubDefault class"
echo "4. Build and test your application"
