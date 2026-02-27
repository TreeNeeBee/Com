cmake_minimum_required ( VERSION "3.10.2" )

project ( ComCoreIPCBindingTest )

set ( CMAKE_CXX_STANDARD 17 )
set ( CMAKE_CXX_STANDARD_REQUIRED ON )

# Find dependencies
find_package( nlohmann_json REQUIRED )

# Source directories
set ( COM_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. )
set ( CORE_ROOT ${COM_ROOT}/../Core )
set ( LOG_ROOT ${COM_ROOT}/../LogAndTrace )

# Include directories
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/lap)

# Create symlinks for lap/core and lap/log
execute_process(
    COMMAND ${CMAKE_COMMAND} -E create_symlink
    ${CORE_ROOT}/source/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include/lap/core
)

execute_process(
    COMMAND ${CMAKE_COMMAND} -E create_symlink
    ${LOG_ROOT}/source/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include/lap/log
)

# Also create core and log symlinks
execute_process(
    COMMAND ${CMAKE_COMMAND} -E create_symlink
    ${CORE_ROOT}/source/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include/core
)

execute_process(
    COMMAND ${CMAKE_COMMAND} -E create_symlink
    ${LOG_ROOT}/source/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include/log
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${COM_ROOT}/source/binding/common
    ${COM_ROOT}/source/runtime/inc
    ${COM_ROOT}/registry/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CORE_ROOT}/source/inc
    ${LOG_ROOT}/source/inc
)

# Link directories
link_directories(
    /usr/local/lib
    ${CORE_ROOT}/build
    ${LOG_ROOT}/build
)

# Build Core IPC binding as shared library
add_library( lap_com_binding_coreipc SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CoreIPCBinding.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CCoreIPCCodec.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CCoreIPCServiceManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CCoreIPCEventManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CCoreIPCMethodManager.cpp
    ${COM_ROOT}/registry/src/CServiceRegistry.cpp
    ${COM_ROOT}/registry/src/CRegistryServer.cpp
    ${COM_ROOT}/registry/src/CRegistryProxy.cpp
    ${COM_ROOT}/registry/src/CRegistryDispatcher.cpp
)

target_link_libraries( lap_com_binding_coreipc
    lap_core
    lap_log
    pthread
    rt
    dl
)

# Installation
install( TARGETS lap_com_binding_coreipc
    LIBRARY DESTINATION lib
)

install( FILES
    ${COM_ROOT}/source/binding/coreipc/inc/CoreIPCBinding.hpp
    DESTINATION include/lap/com/binding
)

# Full functional test (Event + Method + Field)
add_executable( coreipc_binding_full_test
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_coreipc_binding_full.cpp
)

target_link_libraries( coreipc_binding_full_test
    lap_com_binding_coreipc
    lap_core
    lap_log
    pthread
    rt
    dl
)

target_include_directories( coreipc_binding_full_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${COM_ROOT}/source/binding/common
    ${COM_ROOT}/source/runtime/inc
    ${COM_ROOT}/registry/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CORE_ROOT}/source/inc
    ${LOG_ROOT}/source/inc
)

# Integration test (with CRegistryDispatcher in-process)
add_executable( coreipc_integration_test
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_coreipc_integration.cpp
)

target_link_libraries( coreipc_integration_test
    lap_com_binding_coreipc
    lap_core
    lap_log
    pthread
    rt
    dl
)

target_include_directories( coreipc_integration_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${COM_ROOT}/source/binding/common
    ${COM_ROOT}/source/runtime/inc
    ${COM_ROOT}/registry/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CORE_ROOT}/source/inc
    ${LOG_ROOT}/source/inc
)

message( STATUS "Core IPC binding configured successfully" )

# Comprehensive unit test (Codec + Config + Capability + Lifecycle)
add_executable( coreipc_unit_test
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_coreipc_unit.cpp
)

target_link_libraries( coreipc_unit_test
    lap_com_binding_coreipc
    lap_core
    lap_log
    pthread
    rt
    dl
)

target_include_directories( coreipc_unit_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${COM_ROOT}/source/binding/common
    ${COM_ROOT}/source/runtime/inc
    ${COM_ROOT}/registry/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CORE_ROOT}/source/inc
    ${LOG_ROOT}/source/inc
)

# Minimal registry IPC test
add_executable( registry_ipc_test
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_registry_ipc.cpp
)

target_link_libraries( registry_ipc_test
    lap_com_binding_coreipc
    lap_core
    lap_log
    pthread
    rt
    dl
)

target_include_directories( registry_ipc_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${COM_ROOT}/source/binding/common
    ${COM_ROOT}/source/runtime/inc
    ${COM_ROOT}/registry/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CORE_ROOT}/source/inc
    ${LOG_ROOT}/source/inc
)
