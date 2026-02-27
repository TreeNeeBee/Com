cmake_minimum_required ( VERSION "3.10.2" )

project ( ComBindingManager )

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

# Build Binding Manager as shared library
add_library( lap_com_binding_manager SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/BindingManager.cpp
)

target_link_libraries( lap_com_binding_manager
    lap_core
    lap_log
    yaml-cpp
    pthread
    dl
)

target_include_directories( lap_com_binding_manager PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${COM_ROOT}/source/binding/common
    ${COM_ROOT}/source/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CORE_ROOT}/source/inc
    ${LOG_ROOT}/source/inc
)

# Install headers
install( TARGETS lap_com_binding_manager LIBRARY DESTINATION lib )
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/inc/
    DESTINATION include/lap/com/binding
)

message( STATUS "BindingManager configured successfully" )
