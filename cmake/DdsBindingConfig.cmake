# Phase 4: DDS Binding Build Configuration
# Author: LightAP Team
# Date: 2025-11-23
# Description: CMake configuration for DDS transport binding with FastDDS

# =============================================================================
# DDS Binding Configuration
# =============================================================================

message( STATUS "=== Configuring DDS Binding ===" )

# Try to find FastDDS (eProsima Fast-DDS) first
find_package( fastdds QUIET )

if( fastdds_FOUND )
    message( STATUS "FastDDS (fastdds) found: ${fastdds_VERSION}" )
    set( DDS_IMPL "FastDDS" )
    set( DDS_LIBRARIES fastdds )
    set( DDS_FOUND TRUE )
else()
    # Legacy package name fallback
    find_package( fastrtps QUIET )
    if( fastrtps_FOUND )
        message( STATUS "FastDDS (fastrtps) found: ${fastrtps_VERSION}" )
        set( DDS_IMPL "FastDDS" )
        set( DDS_LIBRARIES fastrtps )
        set( DDS_FOUND TRUE )
    else()
        # Try CycloneDDS as fallback
        find_package( CycloneDDS QUIET )
    
        if( CycloneDDS_FOUND )
            message( STATUS "CycloneDDS found: ${CycloneDDS_VERSION}" )
            set( DDS_IMPL "CycloneDDS" )
            set( DDS_LIBRARIES CycloneDDS::ddsc )
            set( DDS_FOUND TRUE )
        else()
            set( DDS_FOUND FALSE )
        endif()
    endif()
endif()

if( DDS_FOUND )
    message( STATUS "Using DDS implementation: ${DDS_IMPL}" )
    
    # Build DDS binding as shared library
    add_library( lap_com_binding_dds SHARED
        ${MODULE_ROOT_DIR}/source/binding/dds/src/DdsBinding.cpp
        ${MODULE_ROOT_DIR}/source/binding/dds/src/CCdrChannel.cpp
        ${MODULE_ROOT_DIR}/source/binding/dds/src/CDdsCodec.cpp
        ${MODULE_ROOT_DIR}/source/binding/dds/src/CDdsEventManager.cpp
        ${MODULE_ROOT_DIR}/source/binding/dds/src/CDdsMethodManager.cpp
        ${MODULE_ROOT_DIR}/source/binding/dds/src/CDdsServiceManager.cpp
        ${MODULE_ROOT_DIR}/source/binding/dds/src/DdsDiscoveryListener.cpp
        ${MODULE_ROOT_DIR}/source/binding/dds/src/DdsReaderListener.cpp
        ${MODULE_ROOT_DIR}/source/binding/dds/src/CDdsDiscoveryServerMonitor.cpp
    )
    
    target_include_directories( lap_com_binding_dds PRIVATE
        ${MODULE_ROOT_DIR}/source/binding/dds/inc
        ${MODULE_ROOT_DIR}/source/binding/common
        ${MODULE_ROOT_DIR}/source/runtime/inc
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )
    
    target_link_libraries( lap_com_binding_dds PRIVATE
        lap_core
        lap_log
        ${DDS_LIBRARIES}
        pthread
    )

    target_compile_definitions( lap_com_binding_dds PUBLIC
        FASTDDS_GEN_API_VER=3
    )
    
    # Add DDS implementation define
    target_compile_definitions( lap_com_binding_dds PRIVATE
        DDS_IMPL_${DDS_IMPL}
    )
    
    # Set library properties
    set_target_properties( lap_com_binding_dds PROPERTIES
        VERSION 1.0.0
        SOVERSION 1
        OUTPUT_NAME "lap_com_binding_dds"
    )
    
    # Install binding library
    install( TARGETS lap_com_binding_dds
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )
    
    # Install binding header
    install( FILES
        ${MODULE_ROOT_DIR}/source/binding/dds/inc/DdsBinding.hpp
        DESTINATION include/lap/com/binding
    )
    
    # Install IDL generated headers
    install( FILES
        ${IDL_GENERATED_DIR}/LapComMessage.h
        ${IDL_GENERATED_DIR}/LapComMessagePubSubTypes.h
        DESTINATION include/lap/com/binding/dds
    )
    
    message( STATUS "DDS Binding build configured successfully" )
    message( STATUS "  IDL sources: ${IDL_SOURCES}" )
    
    # =============================================================================
    # DDS Binding Tests
    # =============================================================================
    
    if( ENABLE_BUILD_TESTS )
        message( STATUS "Configuring DDS Binding tests" )
        
        # Basic DDS binding test
        add_executable( test_dds_binding
            ${MODULE_ROOT_DIR}/test/binding/dds/test_dds_binding.cpp
        )
        
        target_include_directories( test_dds_binding PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/dds/inc
            ${MODULE_ROOT_DIR}/source/binding/dds/idl
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/runtime/inc
            ${GTEST_INCLUDE_DIRS}
        )
        
        target_link_libraries( test_dds_binding PRIVATE
            lap_com_binding_dds
            lap_core
            lap_log
            ${DDS_LIBRARIES}
            ${GTEST_BOTH_LIBRARIES}
            pthread
        )
        
        add_test( NAME DdsBindingTest COMMAND test_dds_binding )
        set_tests_properties( DdsBindingTest PROPERTIES
            TIMEOUT 30
            LABELS "infra"
        )
        
        # Discovery test
        add_executable( test_discovery
            ${MODULE_ROOT_DIR}/source/binding/dds/test/test_discovery.cpp
        )
        
        target_include_directories( test_discovery PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/dds/inc
            ${MODULE_ROOT_DIR}/source/binding/dds/idl
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/runtime/inc
            ${GTEST_INCLUDE_DIRS}
        )
        
        target_link_libraries( test_discovery PRIVATE
            lap_com_binding_dds
            lap_core
            lap_log
            ${DDS_LIBRARIES}
            ${GTEST_BOTH_LIBRARIES}
            pthread
        )
        
        add_test( NAME DdsDiscoveryTest COMMAND test_discovery )
        set_tests_properties( DdsDiscoveryTest PROPERTIES
            TIMEOUT 10
            LABELS "infra"
        )
        
        # Cross-process functional test
        add_executable( test_dds_cross_process
            ${MODULE_ROOT_DIR}/test/binding/dds/test_dds_cross_process.cpp
        )
        
        target_include_directories( test_dds_cross_process PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/dds/inc
            ${MODULE_ROOT_DIR}/source/binding/dds/idl
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/runtime/inc
        )
        
        target_link_libraries( test_dds_cross_process PRIVATE
            lap_com_binding_dds
            lap_core
            lap_log
            ${DDS_LIBRARIES}
            pthread
        )

        # Cross-process full functional test (event/method/field)
        add_executable( test_dds_full
            ${MODULE_ROOT_DIR}/test/binding/dds/test_dds_full.cpp
        )

        target_include_directories( test_dds_full PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/dds/inc
            ${MODULE_ROOT_DIR}/source/binding/dds/idl
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/runtime/inc
        )

        target_link_libraries( test_dds_full PRIVATE
            lap_com_binding_dds
            lap_core
            lap_log
            ${DDS_LIBRARIES}
            pthread
        )
        
        # Discovery Server Monitor test
        add_executable( test_ds_monitor
            ${MODULE_ROOT_DIR}/source/binding/dds/test/test_ds_monitor.cpp
        )

        target_include_directories( test_ds_monitor PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/dds/inc
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/runtime/inc
            ${CMAKE_CURRENT_BINARY_DIR}/include
            ${GTEST_INCLUDE_DIRS}
        )

        target_link_libraries( test_ds_monitor PRIVATE
            lap_com_binding_dds
            lap_core
            lap_log
            ${DDS_LIBRARIES}
            ${GTEST_BOTH_LIBRARIES}
            pthread
        )

        add_test( NAME DdsDiscoveryServerMonitorTest COMMAND test_ds_monitor )
        set_tests_properties( DdsDiscoveryServerMonitorTest PROPERTIES
            TIMEOUT 30
            LABELS "infra"
        )

        # Cross-ECU DDS Discovery simulation test (PDP + DS modes)
        add_executable( test_cross_ecu_dds
            ${MODULE_ROOT_DIR}/test/test_cross_ecu_dds.cpp
        )

        target_include_directories( test_cross_ecu_dds PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/dds/inc
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/runtime/inc
            ${MODULE_ROOT_DIR}/registry/inc
            ${CMAKE_CURRENT_BINARY_DIR}/include
            ${GTEST_INCLUDE_DIRS}
        )

        target_link_libraries( test_cross_ecu_dds PRIVATE
            lap_com_binding_dds
            lap_com_binding_coreipc
            lap_com
            lap_core
            lap_log
            ${DDS_LIBRARIES}
            ${GTEST_BOTH_LIBRARIES}
            pthread
        )

        add_test( NAME CrossEcuDdsDiscoveryTest COMMAND test_cross_ecu_dds )
        set_tests_properties( CrossEcuDdsDiscoveryTest PROPERTIES
            TIMEOUT 120
            LABELS "infra;cross-ecu;dds"
        )

        message( STATUS "DDS Binding tests configured (test_discovery, test_dds_cross_process, test_dds_full, test_ds_monitor, test_cross_ecu_dds)" )
        
        # DDS Publisher/Subscriber examples
        message( STATUS "Configuring DDS examples" )
        
        # Publisher example
        add_executable( dds_publisher
            ${MODULE_ROOT_DIR}/test/examples/dds_publisher.cpp
        )
        
        target_include_directories( dds_publisher PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/dds/inc
            ${MODULE_ROOT_DIR}/source/binding/dds/idl
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/runtime/inc
        )
        
        target_link_libraries( dds_publisher PRIVATE
            lap_com_binding_dds
            lap_core
            lap_log
            ${DDS_LIBRARIES}
            pthread
        )
        
        # Subscriber example
        add_executable( dds_subscriber
            ${MODULE_ROOT_DIR}/test/examples/dds_subscriber.cpp
        )
        
        target_include_directories( dds_subscriber PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/dds/inc
            ${MODULE_ROOT_DIR}/source/binding/dds/idl
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/runtime/inc
        )
        
        target_link_libraries( dds_subscriber PRIVATE
            lap_com_binding_dds
            lap_core
            lap_log
            ${DDS_LIBRARIES}
            pthread
        )
        
        message( STATUS "DDS examples configured" )
    endif()

    # =============================================================================
    # HelloWorld2 Example — Dual-Binding (CoreIPC + DDS)
    #
    #   Standard AUTOSAR AP R25-11 development flow:
    #   1. HelloWorld2.fidl              (service interface definition)
    #   2. gen/                          (generated: Types, Proxy, Skeleton,
    #                                    DdsAdapter, IDL, QoS XML, PubSubTypes)
    #   3. helloworld2_server.cpp        (server: dual-binding Skeleton)
    #   4. helloworld2_client.cpp        (client: dual-binding Proxy)
    #   5. helloworld2_test.cpp          (single-process integration test)
    #
    # Regenerate from FIDL:
    #   generator/build/lap_sidl_gen \
    #       --input  examples/helloworld2/HelloWorld2.fidl \
    #       --output examples/helloworld2/gen \
    #       --author Aii --all
    #   fastddsgen examples/helloworld2/gen/HelloWorld2Service.idl \
    #       -d examples/helloworld2/gen -replace
    # Or via CMake:
    #   cmake --build . --target helloworld2_generate
    # =============================================================================
    if( ENABLE_BUILD_EXAMPLES )

        set( HELLOWORLD2_FIDL_FILE
            ${MODULE_ROOT_DIR}/examples/helloworld2/HelloWorld2.fidl )
        set( HELLOWORLD2_GEN_DIR
            ${MODULE_ROOT_DIR}/examples/helloworld2/gen )
        set( HELLOWORLD2_GENERATOR
            ${MODULE_ROOT_DIR}/generator/build/lap_sidl_gen )

        # Locate fastddsgen for IDL → typed C++ PubSubType generation
        find_program( FASTDDSGEN_EXECUTABLE fastddsgen )
        if( NOT FASTDDSGEN_EXECUTABLE )
            message( WARNING "fastddsgen not found — typed DDS adapters disabled" )
        else()
            message( STATUS "fastddsgen found: ${FASTDDSGEN_EXECUTABLE}" )
        endif()

        # Custom target: regenerate gen/ from HelloWorld2.fidl
        #   Step 1: lap_sidl_gen  → Types, Proxy, Skeleton, DdsAdapter, IDL, QoS
        #   Step 2: fastddsgen   → PubSubType, CdrAux, TypeObjectSupport from IDL
        add_custom_target( helloworld2_generate
            COMMAND ${HELLOWORLD2_GENERATOR}
                --input  ${HELLOWORLD2_FIDL_FILE}
                --output ${HELLOWORLD2_GEN_DIR}
                --author Aii --all
            COMMAND ${FASTDDSGEN_EXECUTABLE}
                ${HELLOWORLD2_GEN_DIR}/HelloWorld2Service.idl
                -d ${HELLOWORLD2_GEN_DIR}
                -replace
            COMMENT "Regenerating HelloWorld2: FIDL → gen/ → fastddsgen PubSubTypes"
            WORKING_DIRECTORY ${MODULE_ROOT_DIR}
        )

        # fastddsgen-generated sources (compiled into each helloworld2 target)
        set( HELLOWORLD2_FASTDDS_SOURCES
            ${HELLOWORLD2_GEN_DIR}/HelloWorld2ServicePubSubTypes.cxx
            ${HELLOWORLD2_GEN_DIR}/HelloWorld2ServiceTypeObjectSupport.cxx
        )

        # Dual-binding include dirs: CoreIPC + DDS + runtime + binding manager
        set( HELLOWORLD2_INCLUDE_DIRS
            ${MODULE_ROOT_DIR}/examples/helloworld2/gen
            ${MODULE_ROOT_DIR}/examples/helloworld2
            ${MODULE_ROOT_DIR}/source/runtime/inc
            ${MODULE_ROOT_DIR}/source/binding/coreipc/inc
            ${MODULE_ROOT_DIR}/source/binding/dds/inc
            ${MODULE_ROOT_DIR}/source/binding/manager/inc
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source
            ${MODULE_ROOT_DIR}/registry/inc
            ${CMAKE_CURRENT_BINARY_DIR}/include
        )

        # Dual-binding link libs: CoreIPC + DDS + core framework
        set( HELLOWORLD2_LINK_LIBS
            lap_com_binding_coreipc
            lap_com_binding_dds
            lap_com
            lap_core
            lap_log
            ${DDS_LIBRARIES}
            pthread
        )

        # HelloWorld2 Server (dual-binding)
        add_executable( helloworld2_server
            ${MODULE_ROOT_DIR}/examples/helloworld2/helloworld2_server.cpp
            ${HELLOWORLD2_FASTDDS_SOURCES}
        )
        target_include_directories( helloworld2_server PRIVATE ${HELLOWORLD2_INCLUDE_DIRS} )
        target_link_libraries( helloworld2_server PRIVATE ${HELLOWORLD2_LINK_LIBS} )

        # HelloWorld2 Client (dual-binding)
        add_executable( helloworld2_client
            ${MODULE_ROOT_DIR}/examples/helloworld2/helloworld2_client.cpp
            ${HELLOWORLD2_FASTDDS_SOURCES}
        )
        target_include_directories( helloworld2_client PRIVATE ${HELLOWORLD2_INCLUDE_DIRS} )
        target_link_libraries( helloworld2_client PRIVATE ${HELLOWORLD2_LINK_LIBS} )

        # HelloWorld2 Integration Test (single-process, dual-binding, CTest)
        add_executable( helloworld2_test
            ${MODULE_ROOT_DIR}/examples/helloworld2/helloworld2_test.cpp
            ${HELLOWORLD2_FASTDDS_SOURCES}
        )
        target_include_directories( helloworld2_test PRIVATE ${HELLOWORLD2_INCLUDE_DIRS} )
        target_link_libraries( helloworld2_test PRIVATE ${HELLOWORLD2_LINK_LIBS} )

        if( ENABLE_BUILD_TESTS )
            add_test( NAME HelloWorld2DualBindingTest COMMAND helloworld2_test )
            set_tests_properties( HelloWorld2DualBindingTest PROPERTIES
                TIMEOUT 90
                LABELS "example;generated;dual-binding"
            )
        endif()

        message( STATUS "HelloWorld2 dual-binding example configured: server, client, test (CoreIPC + DDS)" )
    endif()
    
else()
    message( WARNING "No DDS implementation found. DDS Binding will not be built." )
    message( WARNING "To install DDS:" )
    message( WARNING "  FastDDS: sudo apt install libfastrtps-dev" )
    message( WARNING "  CycloneDDS: sudo apt install cyclonedds-dev" )
    message( WARNING "  Or build from source:" )
    message( WARNING "    https://github.com/eProsima/Fast-DDS" )
    message( WARNING "    https://github.com/eclipse-cyclonedds/cyclonedds" )
endif()

message( STATUS "=== DDS Binding configuration complete ===" )
