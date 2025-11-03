#!/bin/bash
# CommonAPI Code Generator Script
# Generates C++ Proxy/Stub from Franca IDL for D-Bus and SOME/IP communication

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GENERATORS_DIR="${SCRIPT_DIR}/generators"
CORE_GEN="${GENERATORS_DIR}/commonapi-core-generator-linux-x86_64"
DBUS_GEN="${GENERATORS_DIR}/commonapi-dbus-generator-linux-x86_64"
SOMEIP_GEN="${GENERATORS_DIR}/commonapi-someip-generator-linux-x86_64"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_usage() {
    echo "Usage: $0 <fidl-file> [transport] [output-dir]"
    echo ""
    echo "Arguments:"
    echo "  fidl-file    Path to Franca IDL file (.fidl)"
    echo "  transport    Transport type: dbus, someip, or both (default: both)"
    echo "  output-dir   Output directory for generated code (default: ./gen)"
    echo ""
    echo "Examples:"
    echo "  $0 ../fidl/examples/Calculator.fidl"
    echo "  $0 ../fidl/examples/Calculator.fidl dbus"
    echo "  $0 ../fidl/examples/Calculator.fidl someip ./output"
    echo "  $0 ../fidl/examples/Calculator.fidl both"
}

check_generators() {
    local transport=$1
    
    if [ ! -f "${CORE_GEN}" ]; then
        echo -e "${RED}Error: CommonAPI Core Generator not found${NC}"
        echo "Expected: ${CORE_GEN}"
        echo ""
        echo "Download from: https://github.com/COVESA/capicxx-core-tools/releases"
        exit 1
    fi

    if [ "$transport" == "dbus" ] || [ "$transport" == "both" ]; then
        if [ ! -f "${DBUS_GEN}" ]; then
            echo -e "${RED}Error: CommonAPI D-Bus Generator not found${NC}"
            echo "Expected: ${DBUS_GEN}"
            echo ""
            echo "Download from: https://github.com/COVESA/capicxx-dbus-tools/releases"
            exit 1
        fi
        
        if [ ! -x "${DBUS_GEN}" ]; then
            echo -e "${YELLOW}Warning: D-Bus generator not executable, fixing...${NC}"
            chmod +x "${DBUS_GEN}"
        fi
    fi

    if [ "$transport" == "someip" ] || [ "$transport" == "both" ]; then
        if [ ! -f "${SOMEIP_GEN}" ]; then
            echo -e "${RED}Error: CommonAPI SOME/IP Generator not found${NC}"
            echo "Expected: ${SOMEIP_GEN}"
            echo ""
            echo "Download from: https://github.com/COVESA/capicxx-someip-tools/releases"
            exit 1
        fi
        
        if [ ! -x "${SOMEIP_GEN}" ]; then
            echo -e "${YELLOW}Warning: SOME/IP generator not executable, fixing...${NC}"
            chmod +x "${SOMEIP_GEN}"
        fi
    fi

    if [ ! -x "${CORE_GEN}" ]; then
        echo -e "${YELLOW}Warning: Core generator not executable, fixing...${NC}"
        chmod +x "${CORE_GEN}"
    fi
}

# Parse arguments
if [ $# -lt 1 ]; then
    print_usage
    exit 1
fi

FIDL_FILE="$1"
TRANSPORT="${2:-both}"
OUTPUT_DIR="${3:-${SCRIPT_DIR}/gen}"

# Validate transport
if [ "$TRANSPORT" != "dbus" ] && [ "$TRANSPORT" != "someip" ] && [ "$TRANSPORT" != "both" ]; then
    echo -e "${RED}Error: Invalid transport type: $TRANSPORT${NC}"
    echo "Valid options: dbus, someip, both"
    exit 1
fi

# Check if FIDL file exists
if [ ! -f "${FIDL_FILE}" ]; then
    echo -e "${RED}Error: FIDL file not found: ${FIDL_FILE}${NC}"
    exit 1
fi

# Get absolute paths
FIDL_FILE=$(realpath "${FIDL_FILE}")
FIDL_DIR=$(dirname "${FIDL_FILE}")
FIDL_NAME=$(basename "${FIDL_FILE}" .fidl)

# Check for deployment file (.fdepl) for SOME/IP
FDEPL_FILE="${FIDL_DIR}/${FIDL_NAME}.fdepl"
if [ "$TRANSPORT" == "someip" ] || [ "$TRANSPORT" == "both" ]; then
    if [ ! -f "${FDEPL_FILE}" ]; then
        echo -e "${YELLOW}Warning: SOME/IP deployment file not found: ${FDEPL_FILE}${NC}"
        echo -e "${YELLOW}SOME/IP generation will use default deployment${NC}"
        FDEPL_FILE=""
    fi
fi

# Check generators
check_generators "$TRANSPORT"

# Create output directory
mkdir -p "${OUTPUT_DIR}"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}CommonAPI Code Generator${NC}"
echo -e "${BLUE}========================================${NC}"
echo "FIDL File:    ${FIDL_FILE}"
echo "Transport:    ${TRANSPORT}"
echo "Output Dir:   ${OUTPUT_DIR}"
if [ -n "${FDEPL_FILE}" ]; then
    echo "Deploy File:  ${FDEPL_FILE}"
fi
echo -e "${BLUE}========================================${NC}"
echo ""

# Step 1: Generate Core (common for all transports)
echo -e "${BLUE}[1/3] Generating CommonAPI Core code...${NC}"
"${CORE_GEN}" -sk -d "${OUTPUT_DIR}" "${FIDL_FILE}"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Core code generated successfully${NC}"
else
    echo -e "${RED}✗ Core code generation failed${NC}"
    exit 1
fi

echo ""

# Step 2: Generate D-Bus binding (if requested)
if [ "$TRANSPORT" == "dbus" ] || [ "$TRANSPORT" == "both" ]; then
    echo -e "${BLUE}[2/3] Generating D-Bus binding code...${NC}"
    "${DBUS_GEN}" -d "${OUTPUT_DIR}" "${FIDL_FILE}"

    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ D-Bus binding generated successfully${NC}"
    else
        echo -e "${RED}✗ D-Bus binding generation failed${NC}"
        exit 1
    fi
    echo ""
fi

# Step 3: Generate SOME/IP binding (if requested)
if [ "$TRANSPORT" == "someip" ] || [ "$TRANSPORT" == "both" ]; then
    echo -e "${BLUE}[3/3] Generating SOME/IP binding code...${NC}"
    
    if [ -n "${FDEPL_FILE}" ]; then
        # Use deployment file
        "${SOMEIP_GEN}" -d "${OUTPUT_DIR}" "${FDEPL_FILE}"
    else
        # Use FIDL only (default deployment)
        "${SOMEIP_GEN}" -d "${OUTPUT_DIR}" "${FIDL_FILE}"
    fi

    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ SOME/IP binding generated successfully${NC}"
    else
        echo -e "${RED}✗ SOME/IP binding generation failed${NC}"
        exit 1
    fi
    echo ""
fi

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Code generation completed!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Generated files are in: ${OUTPUT_DIR}"
echo ""
echo "Next steps:"
echo "  1. Include generated headers in your project"
echo "  2. Implement service using *StubDefault class"
echo "  3. Create client using *Proxy class"
echo "  4. Use CommonAPIAdapter or SomeIpAdapter for LightAP integration"
echo ""

if [ "$TRANSPORT" == "dbus" ] || [ "$TRANSPORT" == "both" ]; then
    echo "D-Bus usage:"
    echo "  - See test/examples/commonapi/ for examples"
    echo "  - Use CommonAPIAdapter from binding/commonapi/CommonAPIAdapter.hpp"
fi

if [ "$TRANSPORT" == "someip" ] || [ "$TRANSPORT" == "both" ]; then
    echo ""
    echo "SOME/IP usage:"
    echo "  - Configure vsomeip: tools/someip/vsomeip_*.json"
    echo "  - Initialize SomeIpConnectionManager before creating adapters"
    echo "  - Use SomeIpProxyAdapter/SomeIpStubAdapter from binding/commonapi/CommonAPISomeIpAdapter.hpp"
    echo "  - See test/examples/someip/ for examples"
fi

exit 0
