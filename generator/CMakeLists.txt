# ==============================================================
# CMakeLists.txt — lap-sidl-gen (Franca IDL Code Generator)
# ==============================================================
#
# Standalone build — no external dependencies beyond C++17 stdlib.
#
# Build:
#   mkdir -p build && cd build
#   cmake .. -DCMAKE_BUILD_TYPE=Release
#   cmake --build .
#
# Install:
#   cmake --install . --prefix /usr/local
# ==============================================================

cmake_minimum_required( VERSION 3.16 )

project( lap_sidl_gen
    VERSION     1.0.0
    LANGUAGES   CXX
    DESCRIPTION "Franca IDL → AUTOSAR C++ code generator"
)

# ==================== C++ Standard ====================
set( CMAKE_CXX_STANDARD 17 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_CXX_EXTENSIONS OFF )

# ==================== Compiler Warnings ====================
if( CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" )
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -Wconversion
        -Wsign-conversion
    )
endif()

# ==================== Source Files ====================
set( GENERATOR_SOURCES
    src/CFidlLexer.cpp
    src/CFidlParser.cpp
    src/CSchemaHash.cpp
    src/CQosLoader.cpp
    src/CTypesGenerator.cpp
    src/CProxyGenerator.cpp
    src/CSkeletonGenerator.cpp
    src/CDdsIdlGenerator.cpp
    src/CDdsAdapterGenerator.cpp
    src/CServerAppGenerator.cpp
    src/CClientAppGenerator.cpp
    src/main.cpp
)

# ==================== Executable Target ====================
add_executable( lap_sidl_gen ${GENERATOR_SOURCES} )

target_include_directories( lap_sidl_gen
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

# ==================== Install Directories ====================
include( GNUInstallDirs )

# ==================== Install ====================
install( TARGETS lap_sidl_gen
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ==================== Test Support ====================
option( LAP_SIDL_GEN_BUILD_TESTS "Build generator tests" OFF )

if( LAP_SIDL_GEN_BUILD_TESTS )
    enable_testing()

    # Test 1: Parse and generate from example .fidl (library-level)
    add_executable( test_fidl_parser
        test/test_fidl_parser.cpp
        src/CFidlLexer.cpp
        src/CFidlParser.cpp
        src/CSchemaHash.cpp
        src/CQosLoader.cpp
        src/CTypesGenerator.cpp
        src/CProxyGenerator.cpp
        src/CSkeletonGenerator.cpp
        src/CDdsIdlGenerator.cpp
    )

    target_include_directories( test_fidl_parser
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/inc
    )

    add_test( NAME FidlParserTest COMMAND test_fidl_parser )

    # Test 2: CLI integration tests (--validate, --hash-only, --schema-hash, etc.)
    add_executable( test_cli_modes
        test/test_cli_modes.cpp
        src/CFidlLexer.cpp
        src/CFidlParser.cpp
        src/CSchemaHash.cpp
        src/CQosLoader.cpp
        src/CTypesGenerator.cpp
        src/CProxyGenerator.cpp
        src/CSkeletonGenerator.cpp
        src/CDdsIdlGenerator.cpp
    )

    target_include_directories( test_cli_modes
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/inc
    )

    # test_cli_modes needs lap_sidl_gen binary built first
    add_dependencies( test_cli_modes lap_sidl_gen )

    add_test( NAME CliModesTest COMMAND test_cli_modes )
endif()
