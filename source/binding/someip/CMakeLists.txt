cmake_minimum_required ( VERSION "3.10.2" )

project ( ComSomeIpBindingTest )

set ( CMAKE_CXX_STANDARD 17 )
set ( CMAKE_CXX_STANDARD_REQUIRED ON )

# Find dependencies
find_package( nlohmann_json REQUIRED )

# Source directories
set ( COM_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. )
set ( CORE_ROOT ${COM_ROOT}/../Core )
set ( LOG_ROOT ${COM_ROOT}/../LogAndTrace )

# Create include symlinks for lap/core and lap/log
file( MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include )
file( MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/lap )

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
    ${COM_ROOT}/source/inc
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

# Build SOME/IP binding as shared library
add_library( lap_com_binding_someip SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SomeIpBinding.cpp
)

target_link_libraries( lap_com_binding_someip
    lap_core
    lap_log
    pthread
)

target_include_directories( lap_com_binding_someip PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${COM_ROOT}/source/binding/common
    ${COM_ROOT}/source/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CORE_ROOT}/source/inc
    ${LOG_ROOT}/source/inc
)

# Install headers
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/inc/
    DESTINATION include/lap/com/binding
)

message( STATUS "SOME/IP binding (stub) configured successfully" )

# Stub test
add_executable( someip_stub_test
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_someip_stub.cpp
)

target_link_libraries( someip_stub_test
    lap_com_binding_someip
    lap_core
    lap_log
    pthread
    rt
    dl
)

target_include_directories( someip_stub_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${COM_ROOT}/source/binding/common
    ${COM_ROOT}/source/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CORE_ROOT}/source/inc
    ${LOG_ROOT}/source/inc
)
