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

set ( ENABLE_BUILD_SHARED_LIBRARY ON CACHE BOOL "Build com shared library" FORCE )
set ( MODULE_EXTERNAL_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/include )
set ( MODULE_EXTERNAL_LIB_DIR /usr/local/lib )
set ( MODULE_EXTERNAL_LIB pthread rt lap_core lap_log sdbus-c++ ${Protobuf_LIBRARIES} ${LOCAL_PROTO_NAME} )

set ( MODULE_SOURCE_DIR ${MODULE_ROOT_DIR}/source )
set ( MODULE_SOURCE_CXX_DIR ${MODULE_ROOT_DIR}/source/comapi )
set ( MODULE_INSTALL_SRC_DIR ${MODULE_SOURCE_DIR}/inc)
include ( ../../BuildTemplate/SharedLibrary.cmake.in )

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
