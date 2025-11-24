# Phase 4: DDS Binding Build Configuration
# Author: LightAP Team
# Date: 2025-11-23
# Description: CMake configuration for DDS transport binding with FastDDS

# =============================================================================
# DDS Binding Configuration
# =============================================================================

message( STATUS "=== Configuring DDS Binding ===" )

# Try to find FastDDS (eProsima Fast-DDS) first
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

if( DDS_FOUND )
    message( STATUS "Using DDS implementation: ${DDS_IMPL}" )
    
    # IDL generated source files
    set( IDL_GENERATED_DIR ${MODULE_ROOT_DIR}/source/binding/dds/idl )
    set( IDL_SOURCES
        ${IDL_GENERATED_DIR}/LapComMessage.cxx
        ${IDL_GENERATED_DIR}/LapComMessagePubSubTypes.cxx
    )
    
    # Build DDS binding as shared library
    add_library( lap_com_binding_dds SHARED
        ${MODULE_ROOT_DIR}/source/binding/dds/src/DdsBinding.cpp
        ${IDL_SOURCES}
    )
    
    target_include_directories( lap_com_binding_dds PRIVATE
        ${MODULE_ROOT_DIR}/source/binding/dds/inc
        ${MODULE_ROOT_DIR}/source/binding/dds/idl
        ${MODULE_ROOT_DIR}/source/binding/common
        ${MODULE_ROOT_DIR}/source/inc
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )
    
    target_link_libraries( lap_com_binding_dds PRIVATE
        lap_core
        lap_log
        ${DDS_LIBRARIES}
        pthread
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
            ${MODULE_ROOT_DIR}/source/inc
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
        
        # Discovery test
        add_executable( test_discovery
            ${MODULE_ROOT_DIR}/source/binding/dds/test/test_discovery.cpp
        )
        
        target_include_directories( test_discovery PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/dds/inc
            ${MODULE_ROOT_DIR}/source/binding/dds/idl
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/inc
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
        
        # Cross-process functional test
        add_executable( test_dds_cross_process
            ${MODULE_ROOT_DIR}/test/binding/dds/test_dds_cross_process.cpp
        )
        
        target_include_directories( test_dds_cross_process PRIVATE
            ${MODULE_ROOT_DIR}/source/binding/dds/inc
            ${MODULE_ROOT_DIR}/source/binding/dds/idl
            ${MODULE_ROOT_DIR}/source/binding/common
            ${MODULE_ROOT_DIR}/source/inc
        )
        
        target_link_libraries( test_dds_cross_process PRIVATE
            lap_com_binding_dds
            lap_core
            lap_log
            ${DDS_LIBRARIES}
            pthread
        )
        
        message( STATUS "DDS Binding tests configured (test_discovery, test_dds_cross_process)" )
        
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
            ${MODULE_ROOT_DIR}/source/inc
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
            ${MODULE_ROOT_DIR}/source/inc
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
