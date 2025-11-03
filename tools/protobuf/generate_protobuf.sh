#!/bin/bash
################################################################################
# @file     generate_protobuf.sh
# @brief    Generate C++ code from Protobuf definitions
# @author   LightAP Team
# @date     2025-10-30
################################################################################

set -e  # Exit on error

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROTO_DIR="${SCRIPT_DIR}"
OUTPUT_DIR="${SCRIPT_DIR}/generated"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================="
echo "  LightAP Protobuf Code Generator"
echo "========================================="

# Check if protoc is installed
if ! command -v protoc &> /dev/null; then
    echo -e "${RED}Error: protoc not found!${NC}"
    echo "Please install Protocol Buffers compiler:"
    echo "  Ubuntu/Debian: sudo apt-get install protobuf-compiler libprotobuf-dev"
    echo "  Fedora/CentOS: sudo dnf install protobuf protobuf-devel"
    echo "  macOS: brew install protobuf"
    exit 1
fi

# Print protoc version
PROTOC_VERSION=$(protoc --version)
echo -e "${GREEN}Found: ${PROTOC_VERSION}${NC}"

# Create output directory
mkdir -p "${OUTPUT_DIR}"

# Find all .proto files
PROTO_FILES=$(find "${PROTO_DIR}" -maxdepth 1 -name "*.proto")

if [ -z "${PROTO_FILES}" ]; then
    echo -e "${YELLOW}Warning: No .proto files found in ${PROTO_DIR}${NC}"
    exit 0
fi

echo ""
echo "Proto files to generate:"
echo "------------------------"
for proto_file in ${PROTO_FILES}; do
    echo "  - $(basename ${proto_file})"
done

echo ""
echo "Generating C++ code..."
echo "Output directory: ${OUTPUT_DIR}"

# Generate C++ code for each proto file
for proto_file in ${PROTO_FILES}; do
    echo -e "${GREEN}Processing: $(basename ${proto_file})${NC}"
    
    # Generate C++ code using proto3 syntax
    protoc --proto_path="${PROTO_DIR}" \
           --cpp_out="${OUTPUT_DIR}" \
           "${proto_file}"
    
    if [ $? -eq 0 ]; then
        echo -e "  ${GREEN}✓${NC} Generated successfully"
    else
        echo -e "  ${RED}✗${NC} Generation failed"
        exit 1
    fi
done

echo ""
echo "========================================="
echo -e "${GREEN}Generation completed successfully!${NC}"
echo "========================================="

# List generated files
echo ""
echo "Generated files:"
echo "---------------"
ls -lh "${OUTPUT_DIR}"/*.pb.h "${OUTPUT_DIR}"/*.pb.cc 2>/dev/null || echo "No files generated"

echo ""
echo "Usage in your code:"
echo "-------------------"
echo '#include "generated/calculator.pb.h"'
echo ""
echo "Example:"
echo "  lap::com::example::CalculateRequest request;"
echo "  request.set_operand1(10.5);"
echo "  request.set_operand2(3.2);"
echo "  request.set_operation(\"add\");"

exit 0
