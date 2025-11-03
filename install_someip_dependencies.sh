#!/bin/bash
# Quick installation helper for SOME/IP dependencies
# This script guides the user through installing vsomeip and CommonAPI-SomeIP

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

INSTALL_DIR="${HOME}/someip_install"
WORKSPACE_ROOT="/home/ddk/1_workspace/2_middleware/LightAP"

echo -e "${BLUE}=== SOME/IP Dependencies Installation Helper ===${NC}\n"

echo -e "${YELLOW}This script will help you install:${NC}"
echo "  1. vsomeip (SOME/IP implementation)"
echo "  2. CommonAPI-SomeIP Runtime"
echo "  3. CommonAPI-SomeIP Generator"
echo ""
echo -e "${YELLOW}Installation directory: ${BLUE}$INSTALL_DIR${NC}"
echo ""

read -p "Continue? (y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    exit 0
fi

# Create installation directory
mkdir -p "$INSTALL_DIR"
cd "$INSTALL_DIR"

# Function to install vsomeip
install_vsomeip() {
    echo -e "\n${BLUE}[1/3] Installing vsomeip...${NC}"
    
    if ldconfig -p 2>/dev/null | grep -q libvsomeip3; then
        echo -e "${GREEN}  ✓ vsomeip already installed${NC}"
        return 0
    fi
    
    echo "  Cloning vsomeip repository..."
    if [ ! -d "vsomeip" ]; then
        git clone https://github.com/COVESA/vsomeip.git
    fi
    
    cd vsomeip
    git checkout 3.3.8  # Or latest stable version
    
    echo "  Building vsomeip..."
    mkdir -p build
    cd build
    cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
    make -j$(nproc)
    
    echo "  Installing vsomeip (requires sudo)..."
    sudo make install
    sudo ldconfig
    
    cd ../..
    
    # Verify installation
    if ldconfig -p 2>/dev/null | grep -q libvsomeip3; then
        echo -e "${GREEN}  ✓ vsomeip installed successfully${NC}"
    else
        echo -e "${RED}  ✗ vsomeip installation failed${NC}"
        return 1
    fi
}

# Function to install CommonAPI-SomeIP Runtime
install_commonapi_someip_runtime() {
    echo -e "\n${BLUE}[2/3] Installing CommonAPI-SomeIP Runtime...${NC}"
    
    if ldconfig -p 2>/dev/null | grep -q libCommonAPI-SomeIP; then
        echo -e "${GREEN}  ✓ CommonAPI-SomeIP Runtime already installed${NC}"
        return 0
    fi
    
    # First, ensure CommonAPI Core Runtime is installed
    if ! ldconfig -p 2>/dev/null | grep -q libCommonAPI; then
        echo "  Installing CommonAPI Core Runtime first..."
        if [ ! -d "capicxx-core-runtime" ]; then
            git clone https://github.com/COVESA/capicxx-core-runtime.git
        fi
        
        cd capicxx-core-runtime
        git checkout 3.2.3
        mkdir -p build
        cd build
        cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
        make -j$(nproc)
        sudo make install
        sudo ldconfig
        cd ../..
    fi
    
    echo "  Cloning CommonAPI-SomeIP Runtime repository..."
    if [ ! -d "capicxx-someip-runtime" ]; then
        git clone https://github.com/COVESA/capicxx-someip-runtime.git
    fi
    
    cd capicxx-someip-runtime
    git checkout 3.2.3
    
    echo "  Building CommonAPI-SomeIP Runtime..."
    mkdir -p build
    cd build
    cmake -DCMAKE_INSTALL_PREFIX=/usr/local \
          -DUSE_INSTALLED_COMMONAPI=ON \
          -DUSE_INSTALLED_VSOMEIP=ON \
          ..
    make -j$(nproc)
    
    echo "  Installing CommonAPI-SomeIP Runtime (requires sudo)..."
    sudo make install
    sudo ldconfig
    
    cd ../..
    
    # Verify installation
    if ldconfig -p 2>/dev/null | grep -q libCommonAPI-SomeIP; then
        echo -e "${GREEN}  ✓ CommonAPI-SomeIP Runtime installed successfully${NC}"
    else
        echo -e "${RED}  ✗ CommonAPI-SomeIP Runtime installation failed${NC}"
        return 1
    fi
}

# Function to install CommonAPI-SomeIP Generator
install_commonapi_someip_generator() {
    echo -e "\n${BLUE}[3/3] Installing CommonAPI-SomeIP Generator...${NC}"
    
    GENERATORS_DIR="$WORKSPACE_ROOT/modules/Com/tools/commonapi/generators"
    mkdir -p "$GENERATORS_DIR"
    
    if [ -f "$GENERATORS_DIR/commonapi-someip-generator-linux-x86_64" ]; then
        echo -e "${GREEN}  ✓ CommonAPI-SomeIP Generator already installed${NC}"
        return 0
    fi
    
    echo "  Downloading generator..."
    cd "$GENERATORS_DIR"
    
    # Download the pre-built generator
    GENERATOR_VERSION="3.2.0.1"
    GENERATOR_URL="https://github.com/COVESA/capicxx-someip-tools/releases/download/${GENERATOR_VERSION}/commonapi_someip_generator.zip"
    
    wget "$GENERATOR_URL" -O commonapi_someip_generator.zip
    unzip -o commonapi_someip_generator.zip
    chmod +x commonapi-someip-generator-linux-x86_64
    rm commonapi_someip_generator.zip
    
    echo -e "${GREEN}  ✓ CommonAPI-SomeIP Generator installed${NC}"
    
    cd "$INSTALL_DIR"
}

# Main installation flow
echo -e "\n${BLUE}Starting installation...${NC}"

install_vsomeip
install_commonapi_someip_runtime
install_commonapi_someip_generator

# Final verification
echo -e "\n${BLUE}=== Installation Summary ===${NC}"

if ldconfig -p 2>/dev/null | grep -q libvsomeip3; then
    echo -e "vsomeip:                  ${GREEN}✓ Installed${NC}"
else
    echo -e "vsomeip:                  ${RED}✗ Not found${NC}"
fi

if ldconfig -p 2>/dev/null | grep -q libCommonAPI-SomeIP; then
    echo -e "CommonAPI-SomeIP Runtime: ${GREEN}✓ Installed${NC}"
else
    echo -e "CommonAPI-SomeIP Runtime: ${RED}✗ Not found${NC}"
fi

GENERATORS_DIR="$WORKSPACE_ROOT/modules/Com/tools/commonapi/generators"
if [ -f "$GENERATORS_DIR/commonapi-someip-generator-linux-x86_64" ]; then
    echo -e "CommonAPI-SomeIP Generator: ${GREEN}✓ Installed${NC}"
else
    echo -e "CommonAPI-SomeIP Generator: ${RED}✗ Not found${NC}"
fi

echo -e "\n${GREEN}Installation complete!${NC}"
echo ""
echo -e "${YELLOW}Next steps:${NC}"
echo "  1. Verify: Run ./verify_someip_integration.sh"
echo "  2. Generate code: cd tools/commonapi && ./generate_new.sh ../fidl/examples/Calculator.fidl someip"
echo "  3. Test example: See test/examples/someip/calculator_server.cpp"
echo ""
echo -e "For more information: ${BLUE}tools/someip/README.md${NC}"
