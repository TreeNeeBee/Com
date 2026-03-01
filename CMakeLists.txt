cmake_minimum_required ( VERSION "3.10.2" )
include ( ../../BuildTemplate/Config.cmake.in )

project ( Com )

set ( MODULE_NAME "Com" )
set ( MODULE_VERNO 1.0.0 )

set ( MODULE_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR} )

set ( Boost_USE_STATIC_LIBS OFF )
set ( Boost_USE_MULTITHREADED ON )

find_package( nlohmann_json REQUIRED )

if( ENABLE_BUILD_TESTS )
    enable_testing()
    find_package( GTest REQUIRED )
endif()

# Create symlinks for headers to match include structure
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include)

get_filename_component(COM_PARENT_DIR ${CMAKE_CURRENT_LIST_DIR} DIRECTORY)
if (EXISTS ${COM_PARENT_DIR}/Core/source/inc)
    set(CORE_INC_DIR ${COM_PARENT_DIR}/Core/source/inc)
    set(LOG_INC_DIR ${COM_PARENT_DIR}/LogAndTrace/source/inc)
else()
    set(CORE_INC_DIR ${CMAKE_SOURCE_DIR}/modules/Core/source/inc)
    set(LOG_INC_DIR ${CMAKE_SOURCE_DIR}/modules/LogAndTrace/source/inc)
endif()

file(CREATE_LINK ${CORE_INC_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include/core SYMBOLIC)
file(CREATE_LINK ${LOG_INC_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include/log SYMBOLIC)

# Also provide `lap/` prefixed include layout
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/lap)
file(CREATE_LINK ${CORE_INC_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include/lap/core SYMBOLIC)
file(CREATE_LINK ${LOG_INC_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include/lap/log SYMBOLIC)

# Provide <com/...> include layout (e.g. <com/ComTypes.hpp>)
file(CREATE_LINK ${CMAKE_CURRENT_SOURCE_DIR}/source/runtime/inc
    ${CMAKE_CURRENT_BINARY_DIR}/include/com SYMBOLIC)

# Set source directories first (required by MODULE_EXTERNAL_INCLUDE_DIR)
set ( MODULE_SOURCE_DIR ${MODULE_ROOT_DIR}/source )
set ( MODULE_INSTALL_SRC_DIR ${MODULE_SOURCE_DIR}/api)

set ( ENABLE_BUILD_SHARED_LIBRARY ON CACHE BOOL "Build com shared library" FORCE )
set ( MODULE_EXTERNAL_INCLUDE_DIR 
    ${CMAKE_CURRENT_BINARY_DIR}/include 
    ${MODULE_SOURCE_DIR}/runtime/inc
    ${MODULE_ROOT_DIR}/registry/inc
    ${MODULE_SOURCE_DIR}/binding/manager/inc
    ${MODULE_SOURCE_DIR}/binding/common
    ${CORE_INC_DIR}
    ${CORE_INC_DIR}/ipc
)
set ( MODULE_EXTERNAL_LIB_DIR /usr/local/lib )
set ( MODULE_EXTERNAL_LIB pthread rt dl yaml-cpp lap_core lap_log )

# ServiceDiscovery.cpp uses FastDDS for service discovery; add transitive link dependency
find_package(fastdds QUIET)
if(fastdds_FOUND)
    list(APPEND MODULE_EXTERNAL_LIB fastdds fastcdr)
endif()

# Standalone build: use sibling module build outputs when available
if (NOT DEFINED COM_PARENT_DIR)
    get_filename_component(COM_PARENT_DIR ${CMAKE_CURRENT_LIST_DIR} DIRECTORY)
endif()

if (EXISTS ${COM_PARENT_DIR}/Core/build/liblap_core.so)
    link_directories(
        ${COM_PARENT_DIR}/Core/build
        ${COM_PARENT_DIR}/LogAndTrace/build
    )
endif()

# Use multi-directory source collection (BuildTemplate supports MODULE_SOURCE_CXX_DIRS since v1.1.0)
set ( MODULE_SOURCE_CXX_DIRS 
    ${MODULE_SOURCE_DIR}/runtime/src
    ${MODULE_ROOT_DIR}/registry/src
    ${MODULE_SOURCE_DIR}/binding/manager/src
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
# Registry Daemon (IPC-based service discovery v2.0)
# ============================================================================

add_subdirectory( registry )

# NOTE: D-Bus, Socket/Protobuf, CommonAPI, SOME/IP examples archived to archive/test/examples/
# Only CoreIPC and DDS bindings are active.  See cmake/DdsBindingConfig.cmake for DDS examples.

# ============================
# Unit tests (GTest + CTest)
# ============================
set ( MODULE_TEST_DIR ${MODULE_ROOT_DIR}/test )
set ( ENABLE_BUILD_TEST ON CACHE BOOL "Build com tests" FORCE )
set ( ENABLE_BUILD_UNITTEST ON CACHE BOOL "Build com unit tests" FORCE )

# Legacy com_test target removed (required sdbus-c++ for obsolete D-Bus/SOME-IP tests).
# Active tests use standalone targets below.

# ============================================================================
# Phase 1: Service Registry Tests (Zero-Daemon Architecture)
# ============================================================================

if(ENABLE_BUILD_TESTS)
    # ServiceRegistry/SeqLock tests removed (IPC-based v2.0)

    # Test: Runtime Integration (rewritten for R25-11 API)
    add_executable( test_runtime
        ${MODULE_ROOT_DIR}/test/runtime/test_runtime.cpp
    )

    target_include_directories( test_runtime PRIVATE
        ${MODULE_SOURCE_DIR}/runtime/inc
        ${MODULE_ROOT_DIR}/registry/inc
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

    # Test: Runtime Systemd Integration (R25-11 API, rewritten)
    add_executable( test_runtime_systemd
        ${MODULE_ROOT_DIR}/test/runtime/test_runtime_systemd.cpp
    )

    target_include_directories( test_runtime_systemd PRIVATE
        ${MODULE_SOURCE_DIR}/runtime/inc
        ${MODULE_ROOT_DIR}/registry/inc
        ${MODULE_SOURCE_DIR}/api
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )

    target_link_libraries( test_runtime_systemd PRIVATE
        lap_com
        lap_core
        pthread
        rt
        GTest::GTest
        GTest::Main
    )

    add_test( NAME RuntimeSystemdTest COMMAND test_runtime_systemd )

    message( STATUS "Phase 1 service registry tests configured" )

    # Test: Runtime Serialization Strategies
    add_executable( com_runtime_serialization_test
        ${MODULE_ROOT_DIR}/test/unittest/com_runtime_serialization_test.cpp
    )

    target_include_directories( com_runtime_serialization_test PRIVATE
        ${MODULE_SOURCE_DIR}/runtime/inc
        ${MODULE_SOURCE_DIR}/binding/common
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )

    target_link_libraries( com_runtime_serialization_test PRIVATE
        lap_com
        lap_core
        fastcdr
        nlohmann_json::nlohmann_json
        pthread
        GTest::GTest
        GTest::Main
    )

    add_test( NAME RuntimeSerializationTest COMMAND com_runtime_serialization_test )

    # Test: Proxy/Skeleton Communication (MockTransportBinding)
    add_executable( test_proxy_skeleton
        ${MODULE_ROOT_DIR}/test/unittest/test_proxy_skeleton.cpp
    )

    target_include_directories( test_proxy_skeleton PRIVATE
        ${MODULE_SOURCE_DIR}/runtime/inc
        ${MODULE_SOURCE_DIR}/binding/common
        ${MODULE_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )

    target_link_libraries( test_proxy_skeleton PRIVATE
        lap_com
        lap_core
        pthread
        GTest::GTest
        GTest::Main
    )

    add_test( NAME ProxySkeletonTest COMMAND test_proxy_skeleton )

    # Test: Future::Then() continuation (Core module feature, tested in Com)
    add_executable( test_future_then
        ${MODULE_ROOT_DIR}/test/unittest/test_future_then.cpp
    )

    target_include_directories( test_future_then PRIVATE
        ${MODULE_SOURCE_DIR}/runtime/inc
        ${MODULE_SOURCE_DIR}/binding/common
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )

    target_link_libraries( test_future_then PRIVATE
        lap_com
        lap_core
        pthread
        GTest::GTest
        GTest::Main
    )

    add_test( NAME FutureThenTest COMMAND test_future_then )

endif()  # ENABLE_BUILD_TESTS Phase 1

# Socket/Protobuf unit tests removed (obsolete binding, files archived).

# ============================================================================
# Socket Event/Field Unit Tests — removed (obsolete binding)
# ============================================================================

# ============================================================================
# Core IPC Binding (Zero-Copy Local IPC)
# ============================================================================

option( ENABLE_COREIPC_BINDING "Enable Core IPC binding for zero-copy local IPC" ON )
option( ENABLE_BUILD_EXAMPLES "Build example programs" OFF )

if( ENABLE_COREIPC_BINDING )
    message( STATUS "Core IPC binding enabled (v1.0.0)" )

    # Build Core IPC binding as shared library (.so)
    # NOTE: CoreIPC managers depend on CRegistryProxy which depends on CServiceRegistry.
    # Since liblap_com uses hidden visibility, registry symbols must be compiled here too.
    add_library( lap_com_binding_coreipc SHARED
        ${MODULE_ROOT_DIR}/source/binding/coreipc/src/CoreIPCBinding.cpp
        ${MODULE_ROOT_DIR}/source/binding/coreipc/src/CCoreIPCCodec.cpp
        ${MODULE_ROOT_DIR}/source/binding/coreipc/src/CCoreIPCEventManager.cpp
        ${MODULE_ROOT_DIR}/source/binding/coreipc/src/CCoreIPCMethodManager.cpp
        ${MODULE_ROOT_DIR}/source/binding/coreipc/src/CCoreIPCServiceManager.cpp
        ${MODULE_ROOT_DIR}/registry/src/CRegistryProxy.cpp
        ${MODULE_ROOT_DIR}/registry/src/CRegistryServer.cpp
        ${MODULE_ROOT_DIR}/registry/src/CRegistryDispatcher.cpp
        ${MODULE_ROOT_DIR}/registry/src/CServiceRegistry.cpp
        ${MODULE_ROOT_DIR}/registry/src/CSDProxyService.cpp
    )

    target_include_directories( lap_com_binding_coreipc PRIVATE
        ${MODULE_ROOT_DIR}/source/binding/coreipc/inc
        ${MODULE_ROOT_DIR}/source/binding/common
        ${MODULE_ROOT_DIR}/source/binding/manager/inc
        ${MODULE_ROOT_DIR}/source/runtime/inc
        ${MODULE_ROOT_DIR}/registry/inc
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )

    target_link_libraries( lap_com_binding_coreipc PRIVATE
        lap_com
        lap_core
        lap_log
        pthread
        dl
    )

    # Install binding library
    install( TARGETS lap_com_binding_coreipc
        LIBRARY DESTINATION lib
    )

    # Install binding header
    install( FILES
        ${MODULE_ROOT_DIR}/source/binding/coreipc/inc/CoreIPCBinding.hpp
        DESTINATION include/lap/com/binding
    )

    message( STATUS "Core IPC binding configured successfully" )

    # Unit tests for Core IPC binding
    if( ENABLE_BUILD_TESTS )
        add_executable( test_coreipc_binding
            ${MODULE_ROOT_DIR}/source/binding/coreipc/test/test_coreipc_binding_full.cpp
        )

        target_include_directories( test_coreipc_binding PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/coreipc/inc
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/runtime/inc
            ${MODULE_ROOT_DIR}/registry/inc
            ${CMAKE_CURRENT_BINARY_DIR}/include
        )

        target_link_libraries( test_coreipc_binding PRIVATE
            lap_com_binding_coreipc
            lap_com
            lap_core
            lap_log
            pthread
            GTest::GTest
            GTest::Main
        )

        add_test( NAME CoreIPCBindingTest COMMAND test_coreipc_binding )
        set_tests_properties( CoreIPCBindingTest PROPERTIES
            TIMEOUT 30
            LABELS "infra"
        )
        
        message( STATUS "Core IPC binding tests configured" )
    endif()

    # Core IPC Examples
    if( ENABLE_BUILD_EXAMPLES )

        # =================================================================
        # HelloWorld Example — Standard Workflow
        #
        #   1. HelloWorld.fidl          (service definition)
        #   2. gen/                     (generated: Types, Proxy, Skeleton)
        #   3. helloworld_server.cpp    (server using Skeleton)
        #   4. helloworld_client.cpp    (client using Proxy)
        #   5. helloworld_test.cpp      (single-process CI test)
        # =================================================================

        # -- Generated code include path + runtime / binding headers ------
        set( HELLOWORLD_INCLUDE_DIRS
            ${MODULE_ROOT_DIR}/examples/helloworld/gen
            ${MODULE_ROOT_DIR}/source/runtime/inc
            ${MODULE_ROOT_DIR}/source/binding/coreipc/inc
            ${MODULE_ROOT_DIR}/source/binding/manager/inc
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source
            ${MODULE_ROOT_DIR}/registry/inc
            ${CMAKE_CURRENT_BINARY_DIR}/include
        )

        set( HELLOWORLD_LINK_LIBS
            lap_com_binding_coreipc
            lap_com
            lap_core
            lap_log
            pthread
        )

        # -- Server (Skeleton-based) -------------------------------------
        add_executable( helloworld_server
            ${MODULE_ROOT_DIR}/examples/helloworld/helloworld_server.cpp
        )
        target_include_directories( helloworld_server PRIVATE
            ${HELLOWORLD_INCLUDE_DIRS} )
        target_link_libraries( helloworld_server PRIVATE
            ${HELLOWORLD_LINK_LIBS} )

        # -- Client (Proxy-based) -----------------------------------------
        add_executable( helloworld_client
            ${MODULE_ROOT_DIR}/examples/helloworld/helloworld_client.cpp
        )
        target_include_directories( helloworld_client PRIVATE
            ${HELLOWORLD_INCLUDE_DIRS} )
        target_link_libraries( helloworld_client PRIVATE
            ${HELLOWORLD_LINK_LIBS} )

        # -- Integration Test (single-process, CTest) ---------------------
        add_executable( helloworld_test
            ${MODULE_ROOT_DIR}/examples/helloworld/helloworld_test.cpp
        )
        target_include_directories( helloworld_test PRIVATE
            ${HELLOWORLD_INCLUDE_DIRS} )
        target_link_libraries( helloworld_test PRIVATE
            ${HELLOWORLD_LINK_LIBS} )

        if( ENABLE_BUILD_TESTS )
            add_test( NAME HelloWorldIPCTest COMMAND helloworld_test )
            set_tests_properties( HelloWorldIPCTest PROPERTIES
                TIMEOUT 30
                LABELS "example;generated"
            )
        endif()

        # -- Code Generation Target (convenience) -------------------------
        set( FIDL_FILE
            ${MODULE_ROOT_DIR}/examples/helloworld/HelloWorld.fidl )
        set( GEN_DIR
            ${MODULE_ROOT_DIR}/examples/helloworld/gen )
        set( GENERATOR
            ${MODULE_ROOT_DIR}/generator/build/lap_sidl_gen )

        add_custom_target( helloworld_generate
            COMMAND ${GENERATOR}
                --input  ${FIDL_FILE}
                --output ${GEN_DIR}
                --namespace "lap::com::examples"
                --all
            COMMENT "Generating HelloWorld framework code from FIDL"
            WORKING_DIRECTORY ${MODULE_ROOT_DIR}
        )

        message( STATUS "HelloWorld example configured: server, client, test" )
    endif()
endif()

# =============================================================================
# Phase 4: DDS Binding Configuration
# =============================================================================

include( ${CMAKE_CURRENT_SOURCE_DIR}/cmake/DdsBindingConfig.cmake )

# =============================================================================
# SOME/IP Binding (Lightweight UDP — no vsomeip dependency)
# =============================================================================

option( ENABLE_SOMEIP_BINDING "Enable SOME/IP binding (lightweight UDP)" ON )

if( ENABLE_SOMEIP_BINDING )
    message( STATUS "SOME/IP binding enabled (lightweight UDP, v2.0)" )

    add_library( lap_com_binding_someip SHARED
        ${MODULE_ROOT_DIR}/source/binding/someip/src/SomeIpBinding.cpp
    )

    target_include_directories( lap_com_binding_someip PRIVATE
        ${MODULE_ROOT_DIR}/source/binding/someip/inc
        ${MODULE_ROOT_DIR}/source/binding/common
        ${MODULE_ROOT_DIR}/source/runtime/inc
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )

    target_link_libraries( lap_com_binding_someip PRIVATE
        lap_com
        lap_core
        lap_log
        pthread
    )

    install( TARGETS lap_com_binding_someip LIBRARY DESTINATION lib )

    # SOME/IP binding unit tests (GTest)
    if( ENABLE_BUILD_TESTS )
        add_executable( test_someip_binding
            ${MODULE_ROOT_DIR}/source/binding/someip/test/test_someip_binding.cpp
        )
        target_include_directories( test_someip_binding PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/someip/inc
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/runtime/inc
            ${CMAKE_CURRENT_BINARY_DIR}/include
        )
        target_link_libraries( test_someip_binding PRIVATE
            lap_com_binding_someip
            lap_com
            lap_core
            lap_log
            pthread
            GTest::GTest
            GTest::Main
        )
        add_test( NAME SomeIpBindingTest COMMAND test_someip_binding )
        set_tests_properties( SomeIpBindingTest PROPERTIES
            TIMEOUT 30
            LABELS "binding"
        )
    endif()

    message( STATUS "SOME/IP binding configured successfully" )
endif()

# =============================================================================
# D-Bus Binding (sd-bus / libsystemd)
# =============================================================================

option( ENABLE_DBUS_BINDING "Enable D-Bus binding (sd-bus)" ON )

if( ENABLE_DBUS_BINDING )
    find_package( PkgConfig )
    if( PkgConfig_FOUND )
        pkg_check_modules( SYSTEMD QUIET libsystemd )
    endif()

    if( SYSTEMD_FOUND OR EXISTS "/usr/include/systemd/sd-bus.h" )
        message( STATUS "D-Bus binding enabled (sd-bus, v2.0)" )

        add_library( lap_com_binding_dbus SHARED
            ${MODULE_ROOT_DIR}/source/binding/dbus/src/DbusBinding.cpp
        )

        target_include_directories( lap_com_binding_dbus PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/dbus/inc
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/runtime/inc
            ${CMAKE_CURRENT_BINARY_DIR}/include
        )

        target_link_libraries( lap_com_binding_dbus PRIVATE
            lap_com
            lap_core
            lap_log
            systemd
            pthread
        )

        install( TARGETS lap_com_binding_dbus LIBRARY DESTINATION lib )

        # D-Bus binding unit tests (GTest)
        if( ENABLE_BUILD_TESTS )
            add_executable( test_dbus_binding
                ${MODULE_ROOT_DIR}/source/binding/dbus/test/test_dbus_binding.cpp
            )
            target_include_directories( test_dbus_binding PRIVATE
                ${MODULE_ROOT_DIR}/source/binding/dbus/inc
                ${MODULE_ROOT_DIR}/source/binding/common
                ${MODULE_ROOT_DIR}/source/runtime/inc
                ${CMAKE_CURRENT_BINARY_DIR}/include
            )
            target_link_libraries( test_dbus_binding PRIVATE
                lap_com_binding_dbus
                lap_com
                lap_core
                lap_log
                systemd
                pthread
                GTest::GTest
                GTest::Main
            )
            add_test( NAME DbusBindingTest COMMAND test_dbus_binding )
            set_tests_properties( DbusBindingTest PROPERTIES
                TIMEOUT 30
                LABELS "binding"
            )
        endif()

        message( STATUS "D-Bus binding configured successfully" )
    else()
        message( STATUS "D-Bus binding SKIPPED (libsystemd not found)" )
    endif()
endif()

# =============================================================================
# Socket Binding (Unix domain / TCP)
# =============================================================================

option( ENABLE_SOCKET_BINDING "Enable Socket binding (Unix/TCP)" ON )

if( ENABLE_SOCKET_BINDING )
    message( STATUS "Socket binding enabled (Unix/TCP, v2.0)" )

    add_library( lap_com_binding_socket SHARED
        ${MODULE_ROOT_DIR}/source/binding/socket/src/SocketBinding.cpp
    )

    target_include_directories( lap_com_binding_socket PRIVATE
        ${MODULE_ROOT_DIR}/source/binding/socket/inc
        ${MODULE_ROOT_DIR}/source/binding/common
        ${MODULE_ROOT_DIR}/source/runtime/inc
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )

    target_link_libraries( lap_com_binding_socket PRIVATE
        lap_com
        lap_core
        lap_log
        pthread
    )

    install( TARGETS lap_com_binding_socket LIBRARY DESTINATION lib )

    # Socket binding unit tests (GTest)
    if( ENABLE_BUILD_TESTS )
        add_executable( test_socket_binding
            ${MODULE_ROOT_DIR}/source/binding/socket/test/test_socket_binding.cpp
        )
        target_include_directories( test_socket_binding PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/socket/inc
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/runtime/inc
            ${CMAKE_CURRENT_BINARY_DIR}/include
        )
        target_link_libraries( test_socket_binding PRIVATE
            lap_com_binding_socket
            lap_com
            lap_core
            lap_log
            pthread
            GTest::GTest
            GTest::Main
        )
        add_test( NAME SocketBindingTest COMMAND test_socket_binding )
        set_tests_properties( SocketBindingTest PROPERTIES
            TIMEOUT 30
            LABELS "binding"
        )
    endif()

    message( STATUS "Socket binding configured successfully" )
endif()

