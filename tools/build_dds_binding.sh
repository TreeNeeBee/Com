#!/bin/bash
# Quick DDS Binding build test script
# Purpose: Compile only DDS Binding to verify FastDDS integration

set -e  # Exit on error

echo "========================================"
echo "DDS Binding Quick Build Test"
echo "========================================"

BUILD_DIR="build_dds_test"
SOURCE_DIR=$(pwd)

# Clean previous build
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo
echo "[1/4] Configuring CMake..."
cmake "$SOURCE_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_BUILD_TESTS=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo
echo "[2/4] Building DDS IDL types..."
cmake --build . --target lap_com_binding_dds -j$(nproc) 2>&1 | tee build_dds.log

if [ $? -eq 0 ]; then
    echo
    echo "✅ DDS Binding compilation SUCCESS"
    echo
    echo "[3/4] Checking generated library..."
    find . -name "lap_com_binding_dds.so" -o -name "liblap_com_binding_dds.so" | head -5
    
    echo
    echo "[4/4] Building DDS tests..."
    cmake --build . --target test_dds_binding -j$(nproc) 2>&1 | tee build_test.log
    
    if [ $? -eq 0 ]; then
        echo
        echo "✅ DDS Test compilation SUCCESS"
        echo
        echo "========================================"
        echo "Build Summary:"
        echo "========================================"
        ls -lh modules/Com/*.so 2>/dev/null || echo "Shared libraries: (check modules/Com/)"
        ls -lh modules/Com/test_dds_binding 2>/dev/null || echo "Test binary: (check modules/Com/test/)"
        echo
        echo "To run tests:"
        echo "  cd $BUILD_DIR && ctest -R DdsBinding -V"
        echo
    else
        echo "❌ DDS Test compilation FAILED"
        echo "Check: build_test.log"
        exit 1
    fi
else
    echo
    echo "❌ DDS Binding compilation FAILED"
    echo "Check: build_dds.log"
    echo
    echo "Common issues:"
    echo "  1. FastDDS not installed: sudo apt install fastdds-tools libfastrtps-dev"
    echo "  2. IDL files not generated: run fastddsgen on LapComMessage.idl"
    echo "  3. Missing dependencies: lap_core, lap_log"
    exit 1
fi

echo
echo "========================================"
echo "Next steps:"
echo "========================================"
echo "1. Run tests: cd $BUILD_DIR && ctest -R DdsBinding -V"
echo "2. Run specific test: cd $BUILD_DIR && ./modules/Com/test_dds_binding"
echo "3. Check binding manager integration"
echo "4. Performance benchmarks"
