# ============================================================================
# ARXML to YAML Converter CMake Integration
# ============================================================================
# Purpose: Automatically convert AUTOSAR ARXML to YAML during build
# Usage: include(${CMAKE_CURRENT_LIST_DIR}/ArxmlToYaml.cmake)
# ============================================================================

# Find Python3 interpreter
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Set converter script path
set(ARXML2YAML_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/../tools/arxml2yaml/arxml2yaml.py")

#
# Function: arxml_to_yaml
#
# Convert ARXML file(s) to YAML configuration
#
# Parameters:
#   TARGET_NAME - CMake target name
#   ARXML_FILES - List of input ARXML files (can use wildcards)
#   OUTPUT_FILE - Output YAML file path
#   STRATEGY    - Slot allocation strategy (auto/static/dynamic)
#
# Example:
#   arxml_to_yaml(
#       TARGET_NAME generate_config
#       ARXML_FILES ${CMAKE_SOURCE_DIR}/config/services.arxml
#       OUTPUT_FILE ${CMAKE_BINARY_DIR}/slot_mapping.yaml
#       STRATEGY auto
#   )
#
function(arxml_to_yaml)
    set(options VERBOSE)
    set(oneValueArgs TARGET_NAME OUTPUT_FILE STRATEGY)
    set(multiValueArgs ARXML_FILES)
    
    cmake_parse_arguments(
        ARXML2YAML
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )
    
    # Validate arguments
    if(NOT ARXML2YAML_TARGET_NAME)
        message(FATAL_ERROR "arxml_to_yaml: TARGET_NAME is required")
    endif()
    
    if(NOT ARXML2YAML_OUTPUT_FILE)
        message(FATAL_ERROR "arxml_to_yaml: OUTPUT_FILE is required")
    endif()
    
    if(NOT ARXML2YAML_ARXML_FILES)
        message(FATAL_ERROR "arxml_to_yaml: ARXML_FILES is required")
    endif()
    
    # Default strategy
    if(NOT ARXML2YAML_STRATEGY)
        set(ARXML2YAML_STRATEGY "auto")
    endif()
    
    # Build command
    set(CONVERT_CMD
        ${Python3_EXECUTABLE}
        ${ARXML2YAML_SCRIPT}
        -i ${ARXML2YAML_ARXML_FILES}
        -o ${ARXML2YAML_OUTPUT_FILE}
        --strategy ${ARXML2YAML_STRATEGY}
    )
    
    if(ARXML2YAML_VERBOSE)
        list(APPEND CONVERT_CMD -v)
    endif()
    
    # Create custom command
    add_custom_command(
        OUTPUT ${ARXML2YAML_OUTPUT_FILE}
        COMMAND ${CONVERT_CMD}
        DEPENDS ${ARXML2YAML_ARXML_FILES}
        COMMENT "Converting ARXML to YAML: ${ARXML2YAML_OUTPUT_FILE}"
        VERBATIM
    )
    
    # Create custom target
    add_custom_target(
        ${ARXML2YAML_TARGET_NAME} ALL
        DEPENDS ${ARXML2YAML_OUTPUT_FILE}
    )
    
    message(STATUS "ARXML to YAML conversion target: ${ARXML2YAML_TARGET_NAME}")
    message(STATUS "  Input files: ${ARXML2YAML_ARXML_FILES}")
    message(STATUS "  Output file: ${ARXML2YAML_OUTPUT_FILE}")
    message(STATUS "  Strategy: ${ARXML2YAML_STRATEGY}")
    
endfunction()

#
# Function: install_yaml_config
#
# Install YAML configuration file to system
#
# Parameters:
#   YAML_FILE   - YAML file to install
#   DESTINATION - Installation directory (default: /etc/lap/com)
#
# Example:
#   install_yaml_config(
#       YAML_FILE ${CMAKE_BINARY_DIR}/slot_mapping.yaml
#       DESTINATION /etc/lap/com
#   )
#
function(install_yaml_config)
    set(oneValueArgs YAML_FILE DESTINATION)
    
    cmake_parse_arguments(
        INSTALL_YAML
        ""
        "${oneValueArgs}"
        ""
        ${ARGN}
    )
    
    if(NOT INSTALL_YAML_YAML_FILE)
        message(FATAL_ERROR "install_yaml_config: YAML_FILE is required")
    endif()
    
    if(NOT INSTALL_YAML_DESTINATION)
        set(INSTALL_YAML_DESTINATION "/etc/lap/com")
    endif()
    
    install(
        FILES ${INSTALL_YAML_YAML_FILE}
        DESTINATION ${INSTALL_YAML_DESTINATION}
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
    )
    
    message(STATUS "YAML config will be installed to: ${INSTALL_YAML_DESTINATION}")
    
endfunction()

# ============================================================================
# Example Usage in CMakeLists.txt
# ============================================================================
#
# include(cmake/ArxmlToYaml.cmake)
#
# # Find all ARXML files
# file(GLOB ARXML_FILES "${CMAKE_SOURCE_DIR}/config/*.arxml")
#
# # Convert to YAML
# arxml_to_yaml(
#     TARGET_NAME generate_slot_mapping
#     ARXML_FILES ${ARXML_FILES}
#     OUTPUT_FILE ${CMAKE_BINARY_DIR}/slot_mapping.yaml
#     STRATEGY auto
#     VERBOSE
# )
#
# # Install YAML configuration
# install_yaml_config(
#     YAML_FILE ${CMAKE_BINARY_DIR}/slot_mapping.yaml
#     DESTINATION /etc/lap/com
# )
#
# # Make other targets depend on generated config
# add_dependencies(lap_com_runtime generate_slot_mapping)
#
# ============================================================================
