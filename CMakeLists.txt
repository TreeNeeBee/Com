cmake_minimum_required ( VERSION "3.10.2" )
include ( ../../BuildTemplate/Config.cmake.in )

project ( Com )

set ( MODULE_NAME "Com" )
set ( MODULE_VERNO 1.0.0 )

set ( MODULE_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR} )

set ( Boost_USE_STATIC_LIBS OFF )
set ( Boost_USE_MULTITHREADED ON )

find_package( nlohmann_json REQUIRED )
find_package( sdbus-c++ REQUIRED )
find_package( Protobuf REQUIRED )

message( STATUS "Protobuf found: ${Protobuf_VERSION}" )
message( STATUS "Protobuf include dir: ${Protobuf_INCLUDE_DIRS}" )
message( STATUS "Protobuf libraries: ${Protobuf_LIBRARIES}" )

# Create symlinks for headers to match include structure
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include)
file(CREATE_LINK ${CMAKE_SOURCE_DIR}/modules/Core/source/inc 
     ${CMAKE_CURRENT_BINARY_DIR}/include/core SYMBOLIC)
file(CREATE_LINK ${CMAKE_SOURCE_DIR}/modules/LogAndTrace/source/inc 
     ${CMAKE_CURRENT_BINARY_DIR}/include/log SYMBOLIC)

# Also provide `lap/` prefixed include layout
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/lap)
file(CREATE_LINK ${CMAKE_SOURCE_DIR}/modules/Core/source/inc 
     ${CMAKE_CURRENT_BINARY_DIR}/include/lap/core SYMBOLIC)
file(CREATE_LINK ${CMAKE_SOURCE_DIR}/modules/LogAndTrace/source/inc 
     ${CMAKE_CURRENT_BINARY_DIR}/include/lap/log SYMBOLIC)

# Set source directories first (required by MODULE_EXTERNAL_INCLUDE_DIR)
set ( MODULE_SOURCE_DIR ${MODULE_ROOT_DIR}/source )
set ( MODULE_INSTALL_SRC_DIR ${MODULE_SOURCE_DIR}/api)

set ( ENABLE_BUILD_SHARED_LIBRARY ON CACHE BOOL "Build com shared library" FORCE )
set ( MODULE_EXTERNAL_INCLUDE_DIR 
    ${CMAKE_CURRENT_BINARY_DIR}/include 
    ${MODULE_SOURCE_DIR}/runtime/inc
    ${MODULE_SOURCE_DIR}/registry/inc
    ${MODULE_SOURCE_DIR}/inc
)
set ( MODULE_EXTERNAL_LIB_DIR /usr/local/lib )
set ( MODULE_EXTERNAL_LIB pthread rt lap_core lap_log sdbus-c++ ${Protobuf_LIBRARIES} ${LOCAL_PROTO_NAME} )

# Use multi-directory source collection (BuildTemplate supports MODULE_SOURCE_CXX_DIRS since v1.1.0)
set ( MODULE_SOURCE_CXX_DIRS 
    ${MODULE_SOURCE_DIR}/runtime/src
    ${MODULE_SOURCE_DIR}/registry/src
)

include ( ../../BuildTemplate/SharedLibrary.cmake.in )

# Symbol visibility control: Hide all symbols by default, export only LAP_COM_API marked symbols
set_target_properties(lap_com PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Define LAP_COM_BUILDING when compiling the library (for Windows DLL export)
target_compile_definitions(lap_com PRIVATE LAP_COM_BUILDING)

# ============================================================================
# Phase 2: Registry Initialization Daemon (UDS FD Passing)
# ============================================================================

# lap-registry-init daemon
add_executable( lap-registry-init
    ${MODULE_ROOT_DIR}/daemon/lap-registry-init.cpp
    ${MODULE_SOURCE_DIR}/registry/src/RegistryInitializer.cpp
)

target_include_directories( lap-registry-init PRIVATE
    ${MODULE_SOURCE_DIR}/inc
    ${MODULE_SOURCE_DIR}/registry/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_link_libraries( lap-registry-init PRIVATE
    lap_core
    lap_log
    pthread
)

# Install daemon to /usr/local/bin
install(TARGETS lap-registry-init
    RUNTIME DESTINATION bin
)

# Install systemd service units
install(FILES
    ${MODULE_ROOT_DIR}/systemd/lap-registry-qm.socket
    ${MODULE_ROOT_DIR}/systemd/lap-registry-qm-init.service
    ${MODULE_ROOT_DIR}/systemd/lap-registry-asil.socket
    ${MODULE_ROOT_DIR}/systemd/lap-registry-asil-init.service
    DESTINATION /etc/systemd/system
)

# Create runtime directory for sockets
install(DIRECTORY DESTINATION /run/lap
    DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE 
                          GROUP_READ GROUP_WRITE GROUP_EXECUTE 
                          WORLD_READ WORLD_EXECUTE
)

# ============================================================================
# D-Bus Examples
# ============================================================================

# Simple D-Bus Event Publisher (POD only)
add_executable( simple_dbus_publisher
    ${MODULE_ROOT_DIR}/test/examples/dbus/simple_publisher.cpp
)

target_include_directories( simple_dbus_publisher PRIVATE
    ${MODULE_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_link_libraries( simple_dbus_publisher PRIVATE
    lap_core
    lap_log
    sdbus-c++
    pthread
)

# Simple D-Bus Event Subscriber (POD only)
add_executable( simple_dbus_subscriber
    ${MODULE_ROOT_DIR}/test/examples/dbus/simple_subscriber.cpp
)

target_include_directories( simple_dbus_subscriber PRIVATE
    ${MODULE_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_link_libraries( simple_dbus_subscriber PRIVATE
    lap_core
    lap_log
    sdbus-c++
    pthread
)

# D-Bus Method Server
add_executable( dbus_method_server
    ${MODULE_ROOT_DIR}/test/examples/dbus/method_server.cpp
)

target_include_directories( dbus_method_server PRIVATE
    ${MODULE_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_link_libraries( dbus_method_server PRIVATE
    lap_core
    lap_log
    sdbus-c++
    pthread
)

# D-Bus Method Client
add_executable( dbus_method_client
    ${MODULE_ROOT_DIR}/test/examples/dbus/method_client.cpp
)

target_include_directories( dbus_method_client PRIVATE
    ${MODULE_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_link_libraries( dbus_method_client PRIVATE
    lap_core
    lap_log
    sdbus-c++
    pthread
)

# D-Bus Field Server
add_executable( dbus_field_server
    ${MODULE_ROOT_DIR}/test/examples/dbus/field_server.cpp
)

target_include_directories( dbus_field_server PRIVATE
    ${MODULE_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_link_libraries( dbus_field_server PRIVATE
    lap_core
    lap_log
    sdbus-c++
    pthread
)

# D-Bus Field Client
add_executable( dbus_field_client
    ${MODULE_ROOT_DIR}/test/examples/dbus/field_client.cpp
)

target_include_directories( dbus_field_client PRIVATE
    ${MODULE_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_link_libraries( dbus_field_client PRIVATE
    lap_core
    lap_log
    sdbus-c++
    pthread
)

message( STATUS "D-Bus examples configured: Event, Method, Field bindings" )

# ============================================================================
# Protobuf Code Generation
# ============================================================================

set( PROTOBUF_TOOL_DIR ${MODULE_ROOT_DIR}/tools/protobuf )
set( PROTOBUF_GENERATED_DIR ${PROTOBUF_TOOL_DIR}/generated )

# Find all .proto files
file( GLOB PROTO_FILES ${PROTOBUF_TOOL_DIR}/*.proto )

if( PROTO_FILES )
    message( STATUS "Found protobuf files: ${PROTO_FILES}" )
    
    # Generate C++ code from .proto files
    set( PROTOBUF_GENERATED_SOURCES )
    set( PROTOBUF_GENERATED_HEADERS )
    
    foreach( PROTO_FILE ${PROTO_FILES} )
        get_filename_component( PROTO_NAME ${PROTO_FILE} NAME_WE )
        
        set( PROTO_SRCS ${PROTOBUF_GENERATED_DIR}/${PROTO_NAME}.pb.cc )
        set( PROTO_HDRS ${PROTOBUF_GENERATED_DIR}/${PROTO_NAME}.pb.h )
        
        list( APPEND PROTOBUF_GENERATED_SOURCES ${PROTO_SRCS} )
        list( APPEND PROTOBUF_GENERATED_HEADERS ${PROTO_HDRS} )
        
        add_custom_command(
            OUTPUT ${PROTO_SRCS} ${PROTO_HDRS}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${PROTOBUF_GENERATED_DIR}
            COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                --cpp_out=${PROTOBUF_GENERATED_DIR}
                --proto_path=${PROTOBUF_TOOL_DIR}
                ${PROTO_FILE}
            DEPENDS ${PROTO_FILE}
            COMMENT "Generating C++ code from ${PROTO_FILE}"
            VERBATIM
        )
    endforeach()
    
    # Create a custom target to ensure protobuf generation happens before main build
    add_custom_target( generate_protobuf_code ALL
        DEPENDS ${PROTOBUF_GENERATED_SOURCES} ${PROTOBUF_GENERATED_HEADERS}
        COMMENT "Generating protobuf C++ code"
    )
    
    message( STATUS "Protobuf code generation configured" )
else()
    message( STATUS "No protobuf files found in ${PROTOBUF_TOOL_DIR}" )
endif()

# ============================================================================
# Socket Transport Examples (Protobuf)
# ============================================================================

if( PROTO_FILES )
    # Calculator Server
    add_executable( calculator_server
        ${MODULE_ROOT_DIR}/test/examples/socket/calculator_server.cpp
        ${PROTOBUF_GENERATED_SOURCES}
    )
    
    target_include_directories( calculator_server PRIVATE
        ${MODULE_SOURCE_DIR}/inc
        ${MODULE_ROOT_DIR}/source
        ${CMAKE_CURRENT_BINARY_DIR}/include
        ${PROTOBUF_GENERATED_DIR}
        ${Protobuf_INCLUDE_DIRS}
    )
    
    target_link_libraries( calculator_server PRIVATE
        lap_core
        lap_log
        ${Protobuf_LIBRARIES}
        pthread
    )
    
    add_dependencies( calculator_server generate_protobuf_code )
    
    # Calculator Client
    add_executable( calculator_client
        ${MODULE_ROOT_DIR}/test/examples/socket/calculator_client.cpp
        ${PROTOBUF_GENERATED_SOURCES}
    )
    
    target_include_directories( calculator_client PRIVATE
        ${MODULE_SOURCE_DIR}/inc
        ${MODULE_ROOT_DIR}/source
        ${CMAKE_CURRENT_BINARY_DIR}/include
        ${PROTOBUF_GENERATED_DIR}
        ${Protobuf_INCLUDE_DIRS}
    )
    
    target_link_libraries( calculator_client PRIVATE
        lap_core
        lap_log
        ${Protobuf_LIBRARIES}
        pthread
    )
    
    add_dependencies( calculator_client generate_protobuf_code )
    
    message( STATUS "Socket transport examples configured: Calculator Server/Client" )
endif()

# ============================================================================
# Socket Event/Field Examples (Protobuf)
# ============================================================================

if( PROTO_FILES )
    # Event Publisher
    add_executable( socket_event_publisher
        ${MODULE_ROOT_DIR}/test/examples/socket/event_publisher.cpp
        ${PROTOBUF_GENERATED_SOURCES}
    )
    target_include_directories( socket_event_publisher PRIVATE
        ${MODULE_SOURCE_DIR}/inc
        ${MODULE_ROOT_DIR}/source
        ${CMAKE_CURRENT_BINARY_DIR}/include
        ${PROTOBUF_GENERATED_DIR}
        ${Protobuf_INCLUDE_DIRS}
    )
    target_link_libraries( socket_event_publisher PRIVATE
        lap_core
        lap_log
        ${Protobuf_LIBRARIES}
        pthread
    )
    add_dependencies( socket_event_publisher generate_protobuf_code )

    # Event Subscriber
    add_executable( socket_event_subscriber
        ${MODULE_ROOT_DIR}/test/examples/socket/event_subscriber.cpp
        ${PROTOBUF_GENERATED_SOURCES}
    )
    target_include_directories( socket_event_subscriber PRIVATE
        ${MODULE_SOURCE_DIR}/inc
        ${MODULE_ROOT_DIR}/source
        ${CMAKE_CURRENT_BINARY_DIR}/include
        ${PROTOBUF_GENERATED_DIR}
        ${Protobuf_INCLUDE_DIRS}
    )
    target_link_libraries( socket_event_subscriber PRIVATE
        lap_core
        lap_log
        ${Protobuf_LIBRARIES}
        pthread
    )
    add_dependencies( socket_event_subscriber generate_protobuf_code )

    # Field Server
    add_executable( socket_field_server
        ${MODULE_ROOT_DIR}/test/examples/socket/field_server.cpp
        ${PROTOBUF_GENERATED_SOURCES}
    )
    target_include_directories( socket_field_server PRIVATE
        ${MODULE_SOURCE_DIR}/inc
        ${MODULE_ROOT_DIR}/source
        ${CMAKE_CURRENT_BINARY_DIR}/include
        ${PROTOBUF_GENERATED_DIR}
        ${Protobuf_INCLUDE_DIRS}
    )
    target_link_libraries( socket_field_server PRIVATE
        lap_core
        lap_log
        ${Protobuf_LIBRARIES}
        pthread
    )
    add_dependencies( socket_field_server generate_protobuf_code )

    # Field Client
    add_executable( socket_field_client
        ${MODULE_ROOT_DIR}/test/examples/socket/field_client.cpp
        ${PROTOBUF_GENERATED_SOURCES}
    )
    target_include_directories( socket_field_client PRIVATE
        ${MODULE_SOURCE_DIR}/inc
        ${MODULE_ROOT_DIR}/source
        ${CMAKE_CURRENT_BINARY_DIR}/include
        ${PROTOBUF_GENERATED_DIR}
        ${Protobuf_INCLUDE_DIRS}
    )
    target_link_libraries( socket_field_client PRIVATE
        lap_core
        lap_log
        ${Protobuf_LIBRARIES}
        pthread
    )
    add_dependencies( socket_field_client generate_protobuf_code )

    message( STATUS "Socket event/field examples configured" )
endif()

# ============================
# Unit tests (GTest + CTest)
# ============================
set ( MODULE_TEST_DIR ${MODULE_ROOT_DIR}/test )
set ( ENABLE_BUILD_TEST ON CACHE BOOL "Build com tests" FORCE )
set ( ENABLE_BUILD_UNITTEST ON CACHE BOOL "Build com unit tests" FORCE )
# Link against core/log, sdbus-c++, pthread and GTest
set ( MODULE_EXTERNAL_TEST_LIB lap_core lap_log sdbus-c++ ${Protobuf_LIBRARIES} pthread GTest::GTest GTest::Main )

include ( ../../BuildTemplate/Test.cmake.in )

# Register with CTest
add_test(NAME com_tests COMMAND com_test)

# ============================================================================
# Phase 1: Service Registry Tests (Zero-Daemon Architecture)
# ============================================================================

# Test: SeqLock mechanism (Week 1)
add_executable( test_seqlock
    ${MODULE_ROOT_DIR}/test/registry/test_seqlock.cpp
)

target_include_directories( test_seqlock PRIVATE
    ${MODULE_SOURCE_DIR}/registry/inc
    ${MODULE_SOURCE_DIR}/runtime/inc
    ${MODULE_SOURCE_DIR}/api
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_link_libraries( test_seqlock PRIVATE
    lap_core
    pthread
    GTest::GTest
    GTest::Main
)

add_test( NAME SeqLockTest COMMAND test_seqlock )

# Test: SharedMemoryRegistry (Week 2)
add_executable( test_registry
    ${MODULE_ROOT_DIR}/test/registry/test_registry.cpp
    ${MODULE_ROOT_DIR}/source/registry/src/SharedMemoryRegistry.cpp
)

target_include_directories( test_registry PRIVATE
    ${MODULE_SOURCE_DIR}/registry/inc
    ${MODULE_SOURCE_DIR}/runtime/inc
    ${MODULE_SOURCE_DIR}/api
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_link_libraries( test_registry PRIVATE
    lap_core
    pthread
    rt
    GTest::GTest
    GTest::Main
)

add_test( NAME SharedMemoryRegistryTest COMMAND test_registry )

# Test: Runtime Integration (Week 3)
add_executable( test_runtime
    ${MODULE_ROOT_DIR}/test/runtime/test_runtime.cpp
)

target_include_directories( test_runtime PRIVATE
    ${MODULE_SOURCE_DIR}/runtime/inc
    ${MODULE_SOURCE_DIR}/registry/inc
    ${MODULE_SOURCE_DIR}/api
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_link_libraries( test_runtime PRIVATE
    lap_com
    lap_core
    pthread
    rt
    GTest::GTest
    GTest::Main
)

add_test( NAME RuntimeIntegrationTest COMMAND test_runtime )

# Test: Runtime systemd Socket Activation (Phase 2)
add_executable( test_runtime_systemd
    ${MODULE_ROOT_DIR}/test/runtime/test_runtime_systemd.cpp
)

target_include_directories( test_runtime_systemd PRIVATE
    ${MODULE_SOURCE_DIR}/runtime/inc
    ${MODULE_SOURCE_DIR}/registry/inc
    ${MODULE_SOURCE_DIR}/api
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_link_libraries( test_runtime_systemd PRIVATE
    lap_com
    lap_core
    pthread
    rt
)

message( STATUS "Phase 1 service registry tests configured" )

# ============================================================================
# Socket Transport Unit Tests
# ============================================================================

# Test: SocketConnectionManager
add_executable( com_socket_connection_test
    ${MODULE_ROOT_DIR}/test/unittest/com_socket_connection_test.cpp
)

target_include_directories( com_socket_connection_test PRIVATE
    ${MODULE_SOURCE_DIR}/inc
    ${MODULE_ROOT_DIR}/source
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_link_libraries( com_socket_connection_test PRIVATE
    lap_core
    lap_log
    pthread
    GTest::GTest
    GTest::Main
)

add_test( NAME SocketConnectionTest COMMAND com_socket_connection_test )

# Test: ProtobufSerializer
if( PROTO_FILES )
    add_executable( com_socket_serializer_test
        ${MODULE_ROOT_DIR}/test/unittest/com_socket_serializer_test.cpp
    )
    
    target_include_directories( com_socket_serializer_test PRIVATE
        ${MODULE_SOURCE_DIR}/inc
        ${MODULE_ROOT_DIR}/source
        ${CMAKE_CURRENT_BINARY_DIR}/include
        ${PROTOBUF_GENERATED_DIR}
        ${Protobuf_INCLUDE_DIRS}
    )
    
    target_link_libraries( com_socket_serializer_test PRIVATE
        lap_core
        lap_log
        ${Protobuf_LIBRARIES}
        pthread
        GTest::GTest
        GTest::Main
    )
    
    add_test( NAME ProtobufSerializerTest COMMAND com_socket_serializer_test )
endif()

# Test: SocketMethodBinding
if( PROTO_FILES )
    add_executable( com_socket_method_test
        ${MODULE_ROOT_DIR}/test/unittest/com_socket_method_test.cpp
    )
    
    target_include_directories( com_socket_method_test PRIVATE
        ${MODULE_SOURCE_DIR}/inc
        ${MODULE_ROOT_DIR}/source
        ${CMAKE_CURRENT_BINARY_DIR}/include
        ${PROTOBUF_GENERATED_DIR}
        ${Protobuf_INCLUDE_DIRS}
    )
    
    target_link_libraries( com_socket_method_test PRIVATE
        lap_core
        lap_log
        ${Protobuf_LIBRARIES}
        pthread
        GTest::GTest
        GTest::Main
    )
    
    add_test( NAME SocketMethodBindingTest COMMAND com_socket_method_test )
endif()

message( STATUS "Socket transport unit tests configured" )

# ============================================================================
# Phase 2: Multi-Process Registry Test
# ============================================================================

if( BUILD_TESTING )
    add_executable( test_multiprocess_registry
        ${MODULE_ROOT_DIR}/test/unittest/test_multiprocess_registry.cpp
        ${MODULE_SOURCE_DIR}/registry/src/RegistryInitializer.cpp
        ${MODULE_SOURCE_DIR}/registry/src/SharedMemoryRegistry.cpp
    )
    
    target_include_directories( test_multiprocess_registry PRIVATE
        ${MODULE_SOURCE_DIR}/inc
        ${MODULE_SOURCE_DIR}/registry/inc
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )
    
    target_link_libraries( test_multiprocess_registry PRIVATE
        lap_core
        lap_log
        pthread
        GTest::GTest
        GTest::Main
    )
    
    add_test( NAME MultiProcessRegistryTest COMMAND test_multiprocess_registry )
    
    message( STATUS "Phase 2 multi-process registry test configured" )
endif()

# ============================================================================
# Socket Event/Field Unit Tests
# ============================================================================

if( PROTO_FILES )
    # Event tests
    add_executable( com_socket_event_test
        ${MODULE_ROOT_DIR}/test/unittest/com_socket_event_test.cpp
        ${PROTOBUF_GENERATED_SOURCES}
    )
    target_include_directories( com_socket_event_test PRIVATE
        ${MODULE_SOURCE_DIR}/inc
        ${MODULE_ROOT_DIR}/source
        ${CMAKE_CURRENT_BINARY_DIR}/include
        ${PROTOBUF_GENERATED_DIR}
        ${Protobuf_INCLUDE_DIRS}
    )
    target_link_libraries( com_socket_event_test PRIVATE
        lap_core
        lap_log
        ${Protobuf_LIBRARIES}
        pthread
        GTest::GTest
        GTest::Main
    )
    add_test( NAME SocketEventBindingTest COMMAND com_socket_event_test )
    add_dependencies( com_socket_event_test generate_protobuf_code )

    # Field tests
    add_executable( com_socket_field_test
        ${MODULE_ROOT_DIR}/test/unittest/com_socket_field_test.cpp
        ${PROTOBUF_GENERATED_SOURCES}
    )
    target_include_directories( com_socket_field_test PRIVATE
        ${MODULE_SOURCE_DIR}/inc
        ${MODULE_ROOT_DIR}/source
        ${CMAKE_CURRENT_BINARY_DIR}/include
        ${PROTOBUF_GENERATED_DIR}
        ${Protobuf_INCLUDE_DIRS}
    )
    target_link_libraries( com_socket_field_test PRIVATE
        lap_core
        lap_log
        ${Protobuf_LIBRARIES}
        pthread
        GTest::GTest
        GTest::Main
    )
    add_test( NAME SocketFieldBindingTest COMMAND com_socket_field_test )
    add_dependencies( com_socket_field_test generate_protobuf_code )
endif()

# ============================================================================
# iceoryx2 Binding (Zero-Copy IPC)
# ============================================================================

option( ENABLE_ICEORYX_BINDING "Enable iceoryx binding for zero-copy IPC" ON )

if( ENABLE_ICEORYX_BINDING )
    message( STATUS "iceoryx2 binding enabled (v0.7.0)" )

    # Set iceoryx2 paths
    set( ICEORYX2_ROOT_DIR "${MODULE_ROOT_DIR}/third_library/iceoryx2-0.7.0" )
    set( ICEORYX2_CXX_INCLUDE_DIR "${ICEORYX2_ROOT_DIR}/iceoryx2-cxx/include" )
    set( ICEORYX2_C_INCLUDE_DIR "${ICEORYX2_ROOT_DIR}/target/release/iceoryx2-ffi-c-cbindgen/include" )
    set( ICEORYX2_FFI_LIB "${ICEORYX2_ROOT_DIR}/target/release/libiceoryx2_ffi_c.so" )

    # Check if iceoryx2 C FFI library exists
    if( EXISTS "${ICEORYX2_FFI_LIB}" )
        message( STATUS "iceoryx2 C FFI library found: ${ICEORYX2_FFI_LIB}" )
    else()
        message( WARNING "iceoryx2 C FFI library not found. Run 'cargo build --release -p iceoryx2-ffi-c' in ${ICEORYX2_ROOT_DIR}" )
    endif()

    message( STATUS "iceoryx2 source: ${ICEORYX2_ROOT_DIR}" )
    message( STATUS "iceoryx2 C++ include: ${ICEORYX2_CXX_INCLUDE_DIR}" )
    message( STATUS "iceoryx2 C include: ${ICEORYX2_C_INCLUDE_DIR}" )

    # Build iceoryx2 binding as shared library (.so)
    add_library( lap_com_binding_iceoryx SHARED
        ${MODULE_ROOT_DIR}/source/binding/iceoryx2/src/Iceoryx2Binding.cpp
    )

    target_include_directories( lap_com_binding_iceoryx PRIVATE
        ${MODULE_ROOT_DIR}/source/binding/iceoryx2/inc
        ${MODULE_ROOT_DIR}/source/binding/common
        ${MODULE_ROOT_DIR}/source/binding/inc
        ${MODULE_ROOT_DIR}/source/inc
        ${CMAKE_CURRENT_BINARY_DIR}/include
        ${ICEORYX2_CXX_INCLUDE_DIR}
        ${ICEORYX2_C_INCLUDE_DIR}
    )

    target_link_libraries( lap_com_binding_iceoryx PRIVATE
        lap_core
        lap_log
        pthread
        dl
    )

    # Link iceoryx2 C FFI library with absolute path
    target_link_libraries( lap_com_binding_iceoryx PRIVATE ${ICEORYX2_FFI_LIB} )

    # Set RPATH for runtime library lookup
    set_target_properties( lap_com_binding_iceoryx PROPERTIES
        BUILD_RPATH "${ICEORYX2_ROOT_DIR}/target/release"
        INSTALL_RPATH "${ICEORYX2_ROOT_DIR}/target/release"
    )

    # Install binding library
    install( TARGETS lap_com_binding_iceoryx
        LIBRARY DESTINATION lib
    )

    # Install binding header
    install( FILES
        ${MODULE_ROOT_DIR}/source/binding/iceoryx2/inc/Iceoryx2Binding.hpp
        DESTINATION include/lap/com/binding
    )

    # Unit tests for iceoryx binding
    if( ENABLE_BUILD_TESTS )
        add_executable( test_iceoryx_binding
            ${MODULE_ROOT_DIR}/test/unit/binding/test_iceoryx2_binding.cpp
        )

        target_include_directories( test_iceoryx_binding PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/iceoryx2/inc
            ${MODULE_ROOT_DIR}/source/binding/inc
            ${MODULE_ROOT_DIR}/source/inc
            ${CMAKE_CURRENT_BINARY_DIR}/include
            ${ICEORYX_INCLUDE_DIR}
        )

        target_link_libraries( test_iceoryx_binding PRIVATE
            lap_com_binding_iceoryx
            lap_core
            lap_log
            pthread
            GTest::GTest
            GTest::Main
        )

        add_test( NAME Iceoryx2BindingTest COMMAND test_iceoryx_binding )
        
        # Simple pub/sub test
        add_executable( test_iceoryx2_pubsub
            ${MODULE_ROOT_DIR}/test/binding/iceoryx2/test_iceoryx2_pubsub.cpp
        )
        
        target_include_directories( test_iceoryx2_pubsub PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/iceoryx2/inc
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/common/inc
            ${ICEORYX2_C_INCLUDE_DIR}
        )
        
        target_link_libraries( test_iceoryx2_pubsub PRIVATE
            lap_com_binding_iceoryx
            lap_core
            lap_log
            pthread
        )
        
        # Set RPATH for test executable
        set_target_properties( test_iceoryx2_pubsub PROPERTIES
            BUILD_RPATH "${ICEORYX2_ROOT_DIR}/target/release"
        )
        
        # Multi-size message test
        add_executable( test_multi_size_messages
            ${MODULE_ROOT_DIR}/test/binding/iceoryx2/test_multi_size_messages.cpp
        )
        
        target_include_directories( test_multi_size_messages PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/iceoryx2/inc
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/common/inc
            ${ICEORYX2_C_INCLUDE_DIR}
        )
        
        target_link_libraries( test_multi_size_messages PRIVATE
            lap_com_binding_iceoryx
            lap_core
            lap_log
            pthread
        )
        
        set_target_properties( test_multi_size_messages PROPERTIES
            BUILD_RPATH "${ICEORYX2_ROOT_DIR}/target/release"
        )

        # iceoryx2 + BindingManager Integration Test
        add_executable( test_iceoryx2_binding_manager_integration
            ${MODULE_ROOT_DIR}/test/binding/iceoryx2/test_iceoryx2_binding_manager_integration.cpp
        )
        
        target_include_directories( test_iceoryx2_binding_manager_integration PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/iceoryx2/inc
            ${MODULE_ROOT_DIR}/source/binding/manager/inc
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/common/inc
            ${ICEORYX2_C_INCLUDE_DIR}
            ${GTEST_INCLUDE_DIRS}
        )
        
        target_link_libraries( test_iceoryx2_binding_manager_integration PRIVATE
            lap_com_binding_iceoryx
            lap_core
            lap_log
            ${GTEST_BOTH_LIBRARIES}
            pthread
        )
        
        set_target_properties( test_iceoryx2_binding_manager_integration PROPERTIES
            BUILD_RPATH "${ICEORYX2_ROOT_DIR}/target/release"
        )
        
        # Add to CTest
        add_test(
            NAME test_iceoryx2_binding_manager_integration
            COMMAND test_iceoryx2_binding_manager_integration
        )
    endif()

    # iceoryx2 Examples
    if( ENABLE_BUILD_EXAMPLES )
        # Publisher example
        add_executable( iceoryx2_publisher_example
            ${MODULE_ROOT_DIR}/examples/iceoryx2/publisher_example.cpp
        )
        
        target_include_directories( iceoryx2_publisher_example PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/iceoryx2/inc
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/common/inc
            ${ICEORYX2_C_INCLUDE_DIR}
        )
        
        target_link_libraries( iceoryx2_publisher_example PRIVATE
            lap_com_binding_iceoryx
            lap_core
            lap_log
            pthread
        )
        
        set_target_properties( iceoryx2_publisher_example PROPERTIES
            BUILD_RPATH "${ICEORYX2_ROOT_DIR}/target/release"
        )

        # Subscriber example
        add_executable( iceoryx2_subscriber_example
            ${MODULE_ROOT_DIR}/examples/iceoryx2/subscriber_example.cpp
        )
        
        target_include_directories( iceoryx2_subscriber_example PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/iceoryx2/inc
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/common/inc
            ${ICEORYX2_C_INCLUDE_DIR}
        )
        
        target_link_libraries( iceoryx2_subscriber_example PRIVATE
            lap_com_binding_iceoryx
            lap_core
            lap_log
            pthread
        )
        
        set_target_properties( iceoryx2_subscriber_example PROPERTIES
            BUILD_RPATH "${ICEORYX2_ROOT_DIR}/target/release"
        )

        # Ping-Pong example
        add_executable( iceoryx2_ping_pong_example
            ${MODULE_ROOT_DIR}/examples/iceoryx2/ping_pong_example.cpp
        )
        
        target_include_directories( iceoryx2_ping_pong_example PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/iceoryx2/inc
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/common/inc
            ${ICEORYX2_C_INCLUDE_DIR}
        )
        
        target_link_libraries( iceoryx2_ping_pong_example PRIVATE
            lap_com_binding_iceoryx
            lap_core
            lap_log
            pthread
        )
        
        set_target_properties( iceoryx2_ping_pong_example PROPERTIES
            BUILD_RPATH "${ICEORYX2_ROOT_DIR}/target/release"
        )

        message( STATUS "iceoryx2 examples configured: publisher, subscriber, ping-pong" )
    endif()
endif()

# =============================================================================
# Phase 4: DDS Binding Configuration
# =============================================================================

include( ${CMAKE_CURRENT_SOURCE_DIR}/cmake/DdsBindingConfig.cmake )

