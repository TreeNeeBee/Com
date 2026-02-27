# ==============================================================================
# LightAP Registry Daemon - CMakeLists.txt
# ==============================================================================
#
# Registry daemon providing IPC-based service discovery (v2.0)
# Architecture:
#   - Shared memory: memfd_create + SCM_RIGHTS FD passing
#   - Single-writer model: MPSC requests + SPMC responses via Core IPC
#   - Dual registry: QM (0x0001~0x0417) + ASIL (0xF001~0xF3FE)
#
# Targets:
#   lap-registry-init  - Registry initialization daemon executable
#   rctrl              - Registry diagnostic & management CLI tool
#
# Usage:
#   add_subdirectory( registry )   # from parent Com/CMakeLists.txt
#
# ==============================================================================

set ( REGISTRY_DIR ${CMAKE_CURRENT_SOURCE_DIR} )
set ( REGISTRY_INC_DIR ${REGISTRY_DIR}/inc )
set ( REGISTRY_SRC_DIR ${REGISTRY_DIR}/src )
set ( REGISTRY_DAEMON_DIR ${REGISTRY_DIR}/daemon )
set ( REGISTRY_TOOLS_DIR ${REGISTRY_DIR}/tools )

# ==============================================================================
# Registry Source Files
# ==============================================================================

set ( REGISTRY_HEADERS
    ${REGISTRY_INC_DIR}/ServiceSlot.hpp
    ${REGISTRY_INC_DIR}/CServiceRegistry.hpp
    ${REGISTRY_INC_DIR}/RegistryIpcMessage.hpp
    ${REGISTRY_INC_DIR}/CRegistryServer.hpp
    ${REGISTRY_INC_DIR}/CRegistryDispatcher.hpp
    ${REGISTRY_INC_DIR}/CRegistryProxy.hpp
)

set ( REGISTRY_SOURCES
    ${REGISTRY_SRC_DIR}/CServiceRegistry.cpp
    ${REGISTRY_SRC_DIR}/CRegistryServer.cpp
    ${REGISTRY_SRC_DIR}/CRegistryDispatcher.cpp
    ${REGISTRY_SRC_DIR}/CRegistryProxy.cpp
)

# ==============================================================================
# Registry Daemon Executable (lap-registry-init)
# ==============================================================================

add_executable ( lap-registry-init
    ${REGISTRY_DAEMON_DIR}/main.cpp
    ${REGISTRY_SRC_DIR}/CRegistryServer.cpp
    ${REGISTRY_SRC_DIR}/CServiceRegistry.cpp
)

target_include_directories ( lap-registry-init PRIVATE
    ${REGISTRY_INC_DIR}
    ${MODULE_SOURCE_DIR}/inc
    ${CMAKE_BINARY_DIR}/include
)

target_link_libraries ( lap-registry-init PRIVATE
    lap_core
    lap_log
    pthread
)

# Install daemon to /usr/local/bin
install ( TARGETS lap-registry-init
    RUNTIME DESTINATION bin
)

# ==============================================================================
# Registry CLI Tool (lap-registry-ctl)
# ==============================================================================
# Diagnostic tool integrated with Core IPC + LogAndTrace.
# Read ops:  CServiceRegistry::InitializeFromSocket() → read-only mmap
# Write ops: CRegistryProxy → Core IPC MPSC → CRegistryDispatcher → slot write
# ==============================================================================

add_executable ( rctrl
    ${REGISTRY_TOOLS_DIR}/lap-registry-ctl.cpp
    ${REGISTRY_SRC_DIR}/CServiceRegistry.cpp
    ${REGISTRY_SRC_DIR}/CRegistryProxy.cpp
)

target_include_directories ( rctrl PRIVATE
    ${REGISTRY_INC_DIR}
    ${MODULE_SOURCE_DIR}/inc
    ${CMAKE_BINARY_DIR}/include
)

target_link_libraries ( rctrl PRIVATE
    lap_core
    lap_log
    pthread
)

# Install tool to /usr/local/bin
install ( TARGETS rctrl
    RUNTIME DESTINATION bin
)

# ==============================================================================
# Install systemd service units
# ==============================================================================

if ( EXISTS ${REGISTRY_DIR}/systemd )
    install ( FILES
        ${REGISTRY_DIR}/systemd/lap-registry-qm.socket
        ${REGISTRY_DIR}/systemd/lap-registry-qm-init.service
        ${REGISTRY_DIR}/systemd/lap-registry-asil.socket
        ${REGISTRY_DIR}/systemd/lap-registry-asil-init.service
        DESTINATION /etc/systemd/system
    )
endif ()

# Create runtime directory for sockets
install ( DIRECTORY DESTINATION /run/lap
    DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                          GROUP_READ GROUP_WRITE GROUP_EXECUTE
                          WORLD_READ WORLD_EXECUTE
)

message ( STATUS "[Registry] Daemon target: lap-registry-init" )
message ( STATUS "[Registry] Tool target:   rctrl" )
message ( STATUS "[Registry] Sources: ${REGISTRY_SRC_DIR}" )
message ( STATUS "[Registry] Headers: ${REGISTRY_INC_DIR}" )
message ( STATUS "[Registry] Tools:   ${REGISTRY_TOOLS_DIR}" )
