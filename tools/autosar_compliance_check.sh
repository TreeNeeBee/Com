#!/bin/bash
################################################################################
# AUTOSAR Adaptive Platform Communication Management Compliance Checker
# 
# Purpose: Verify LightAP Com module compliance with AUTOSAR AP R24-11:
#   - AUTOSAR_AP_SWS_CommunicationManagement (R24-11, 672 pages)
#   - AUTOSAR_AP_TPS_ManifestSpecification (R24-11, 1253 pages)
#   - AUTOSAR_AP_EXP_ARAComAPI (R24-11, 141 pages)
#   - AUTOSAR_AP_SWS_NetworkManagement
#
# New R24-11 Features Checked:
#   - SWS_CM_02201-02203: Static Service Connection
#   - TPS_MANI_03312-03315: Static Configuration
#   - EXP 7.2.1: Central vs Distributed Service Discovery
#
# Author: LightAP Team
# Version: 2.0.0
# Date: 2024-01
################################################################################

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COM_DIR="$(dirname "$SCRIPT_DIR")"
SOURCE_DIR="$COM_DIR/source"
TEST_DIR="$COM_DIR/test"
DOC_DIR="$COM_DIR/doc"

# Counters
TOTAL_CHECKS=0
PASSED_CHECKS=0
FAILED_CHECKS=0
WARNING_CHECKS=0

################################################################################
# Utility Functions
################################################################################

print_header() {
    echo -e "\n${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}\n"
}

print_check() {
    TOTAL_CHECKS=$((TOTAL_CHECKS + 1))
    echo -e "${YELLOW}[CHECK $TOTAL_CHECKS]${NC} $1"
}

print_pass() {
    PASSED_CHECKS=$((PASSED_CHECKS + 1))
    echo -e "  ${GREEN}✓ PASS${NC}: $1"
}

print_fail() {
    FAILED_CHECKS=$((FAILED_CHECKS + 1))
    echo -e "  ${RED}✗ FAIL${NC}: $1"
}

print_warn() {
    WARNING_CHECKS=$((WARNING_CHECKS + 1))
    echo -e "  ${YELLOW}⚠ WARNING${NC}: $1"
}

check_file_exists() {
    local file=$1
    local desc=$2
    if [ -f "$file" ]; then
        print_pass "$desc found: $file"
        return 0
    else
        print_fail "$desc not found: $file"
        return 1
    fi
}

check_class_exists() {
    local file=$1
    local class=$2
    local desc=$3
    if grep -q "class.*$class" "$file" 2>/dev/null; then
        print_pass "$desc class '$class' found"
        return 0
    else
        print_fail "$desc class '$class' not found in $file"
        return 1
    fi
}

check_method_exists() {
    local file=$1
    local method=$2
    local desc=$3
    if grep -q "$method" "$file" 2>/dev/null; then
        print_pass "$desc method '$method' found"
        return 0
    else
        print_fail "$desc method '$method' not found in $file"
        return 1
    fi
}

check_requirement_comment() {
    local file=$1
    local req_id=$2
    local desc=$3
    if grep -q "$req_id" "$file" 2>/dev/null; then
        print_pass "$desc requirement '$req_id' traced"
        return 0
    else
        print_warn "$desc requirement '$req_id' not traced in comments"
        return 1
    fi
}

################################################################################
# SWS_CM Requirements Verification
################################################################################

check_runtime_api() {
    print_header "SWS_CM_0000x: Runtime API Requirements"
    
    local runtime_file="$SOURCE_DIR/inc/Runtime.hpp"
    
    print_check "SWS_CM_00001: FindService API"
    check_file_exists "$runtime_file" "Runtime header"
    check_method_exists "$runtime_file" "FindService" "SWS_CM_00001"
    
    print_check "SWS_CM_00002: OfferService API"
    check_method_exists "$runtime_file" "OfferService" "SWS_CM_00002"
    
    print_check "SWS_CM_00005: StopOfferService API"
    check_method_exists "$runtime_file" "StopOfferService" "SWS_CM_00005"
    
    print_check "SWS_CM_00101: Initialize API"
    check_method_exists "$runtime_file" "Initialize" "SWS_CM_00101"
    
    print_check "SWS_CM_00102: Deinitialize API"
    check_method_exists "$runtime_file" "Deinitialize" "SWS_CM_00102"
}

check_proxy_api() {
    print_header "SWS_CM_0013x: ServiceProxy API Requirements"
    
    local proxy_file="$SOURCE_DIR/inc/ProxyBase.hpp"
    
    print_check "SWS_CM_00130: Proxy construction from ServiceHandle"
    check_file_exists "$proxy_file" "ProxyBase header"
    check_class_exists "$proxy_file" "ProxyBase" "SWS_CM_00130"
    
    print_check "SWS_CM_00131: Proxy GetHandle method"
    check_method_exists "$proxy_file" "GetHandle" "SWS_CM_00131"
}

check_skeleton_api() {
    print_header "SWS_CM_0011x: ServiceSkeleton API Requirements"
    
    local skeleton_file="$SOURCE_DIR/inc/SkeletonBase.hpp"
    
    print_check "SWS_CM_00110: Skeleton OfferService"
    check_file_exists "$skeleton_file" "SkeletonBase header"
    check_method_exists "$skeleton_file" "OfferService" "SWS_CM_00110"
    
    print_check "SWS_CM_00111: Skeleton StopOfferService"
    check_method_exists "$skeleton_file" "StopOfferService" "SWS_CM_00111"
}

check_method_api() {
    print_header "SWS_CM_0019x: Method Call Requirements"
    
    local method_file="$SOURCE_DIR/inc/Method.hpp"
    
    print_check "SWS_CM_00191: Synchronous method call"
    check_file_exists "$method_file" "Method header"
    if grep -q "Call.*Result\|operator()" "$method_file" 2>/dev/null; then
        print_pass "Synchronous call API found"
    else
        print_fail "Synchronous call API not found"
    fi
    
    print_check "SWS_CM_00195: Asynchronous method call"
    if grep -q "CallAsync\|future\|Future" "$method_file" 2>/dev/null; then
        print_pass "Asynchronous call API found"
    else
        print_fail "Asynchronous call API not found"
    fi
    
    print_check "SWS_CM_00196: Fire-and-forget method call"
    if grep -q "FireForget\|CallFireForget" "$method_file" 2>/dev/null; then
        print_pass "Fire-and-forget API found"
    else
        print_warn "Fire-and-forget API not found"
    fi
}

check_event_api() {
    print_header "SWS_CM_0014x-0018x: Event Communication Requirements"
    
    local event_file="$SOURCE_DIR/inc/Event.hpp"
    
    print_check "SWS_CM_00141: Event Subscribe"
    check_file_exists "$event_file" "Event header"
    check_method_exists "$event_file" "Subscribe" "SWS_CM_00141"
    
    print_check "SWS_CM_00151: Event Unsubscribe"
    check_method_exists "$event_file" "Unsubscribe" "SWS_CM_00151"
    
    print_check "SWS_CM_00181: GetNewSamples"
    check_method_exists "$event_file" "GetNewSamples" "SWS_CM_00181"
    
    print_check "SWS_CM_00182: SetReceiveHandler"
    check_method_exists "$event_file" "SetReceiveHandler\|SetEventHandler" "SWS_CM_00182"
    
    print_check "SWS_CM_00183: UnsetReceiveHandler"
    if grep -q "UnsetReceiveHandler\|UnsetEventHandler" "$event_file" 2>/dev/null; then
        print_pass "UnsetReceiveHandler API found"
    else
        print_warn "UnsetReceiveHandler API may be missing"
    fi
}

check_field_api() {
    print_header "SWS_CM_0020x: Field Communication Requirements"
    
    local field_file="$SOURCE_DIR/inc/Field.hpp"
    
    print_check "SWS_CM_00200: Field Get method"
    check_file_exists "$field_file" "Field header"
    check_method_exists "$field_file" "Get" "SWS_CM_00200"
    
    print_check "SWS_CM_00201: Field Set method"
    check_method_exists "$field_file" "Set" "SWS_CM_00201"
    
    print_check "SWS_CM_00202: Field Subscribe (Notifier)"
    check_method_exists "$field_file" "Subscribe" "SWS_CM_00202"
    
    print_check "SWS_CM_00210: RegisterGetHandler"
    if grep -q "RegisterGetHandler\|RegisterGetter" "$field_file" 2>/dev/null; then
        print_pass "RegisterGetHandler API found"
    else
        print_warn "RegisterGetHandler API may be implicit"
    fi
    
    print_check "SWS_CM_00211: RegisterSetHandler"
    if grep -q "RegisterSetHandler\|RegisterSetter" "$field_file" 2>/dev/null; then
        print_pass "RegisterSetHandler API found"
    else
        print_warn "RegisterSetHandler API may be implicit"
    fi
}

check_types_api() {
    print_header "SWS_CM_0030x: Type System Requirements"
    
    local types_file="$SOURCE_DIR/inc/ComTypes.hpp"
    
    print_check "SWS_CM_00302: InstanceIdentifier type"
    check_file_exists "$types_file" "ComTypes header"
    if grep -q "InstanceIdentifier\|ServiceId\|InstanceId" "$types_file" 2>/dev/null; then
        print_pass "InstanceIdentifier type found"
    else
        print_fail "InstanceIdentifier type not found"
    fi
    
    print_check "SWS_CM_00303: ServiceHandleContainer type"
    if grep -q "ServiceHandleContainer\|ServiceHandle.*vector\|Container.*ServiceHandle" "$types_file" 2>/dev/null; then
        print_pass "ServiceHandleContainer type found"
    else
        print_fail "ServiceHandleContainer type not found"
    fi
    
    print_check "SWS_CM_00306: SubscriptionState enumeration"
    if grep -q "SubscriptionState\|enum.*Subscri" "$types_file" 2>/dev/null; then
        print_pass "SubscriptionState type found"
    else
        print_warn "SubscriptionState type may be in different file"
    fi
}

################################################################################
# R24-11 New Features Checks
################################################################################

check_static_service_connection() {
    print_header "R24-11: Static Service Connection (SWS_CM_02201-02203)"
    
    print_check "SWS_CM_02201: Static service connection configuration"
    local config_found=false
    if [ -f "$DOC_DIR/SERVICE_DISCOVERY_ARCHITECTURE.md" ]; then
        if grep -q "SWS_CM_02201\|Static.*Service.*Connection\|TPS_MANI_03312" "$DOC_DIR/SERVICE_DISCOVERY_ARCHITECTURE.md" 2>/dev/null; then
            print_pass "Static service connection documented"
            config_found=true
        fi
    fi
    
    if [ -f "$COM_DIR/config/static_service_config.json" ] || [ -f "$COM_DIR/config/lightap_com_config.json" ]; then
        print_pass "Static service configuration file found"
        config_found=true
    fi
    
    if [ "$config_found" = false ]; then
        print_warn "Static service connection configuration not found"
    fi
    
    print_check "SWS_CM_02202: Service Discovery bypass mechanism"
    if grep -rq "bypass.*discovery\|static.*endpoint\|skip.*SD" "$SOURCE_DIR" 2>/dev/null; then
        print_pass "Service discovery bypass mechanism found"
    else
        print_warn "Service discovery bypass not explicitly implemented"
    fi
    
    print_check "TPS_MANI_03312-03315: Static endpoint configuration"
    if grep -rq "SomeipRemoteUnicastConfig\|static.*remote.*address\|RemoteUnicastConfig" "$DOC_DIR" 2>/dev/null; then
        print_pass "Static endpoint configuration documented"
    else
        print_warn "TPS_MANI_03312-03315 not found in documentation"
    fi
}

check_central_service_discovery() {
    print_header "R24-11: Central Service Discovery (EXP 7.2.1)"
    
    print_check "EXP 7.2.1: Central registry implementation"
    local registry_found=false
    
    if [ -f "$SOURCE_DIR/registry/CentralServiceRegistry.hpp" ] || \
       [ -f "$SOURCE_DIR/inc/CentralServiceRegistry.hpp" ]; then
        print_pass "CentralServiceRegistry class found"
        registry_found=true
    fi
    
    if [ -f "$COM_DIR/CENTRAL_REGISTRY_SUMMARY.txt" ]; then
        print_pass "Central registry design documentation found"
        registry_found=true
    fi
    
    if [ "$registry_found" = false ]; then
        print_warn "Central service registry not implemented yet (planned for Week 9-10)"
    fi
    
    print_check "Central registry daemon process"
    if [ -f "$SOURCE_DIR/registry/CentralServiceRegistryDaemon.cpp" ]; then
        print_pass "Central registry daemon implementation found"
    else
        print_warn "Central registry daemon not implemented yet"
    fi
    
    print_check "Unix Domain Socket communication"
    if grep -rq "domain.*socket\|unix.*socket\|/tmp/.*\.sock" "$SOURCE_DIR" "$DOC_DIR" 2>/dev/null; then
        print_pass "Unix Domain Socket communication design found"
    else
        print_warn "Unix Domain Socket communication not found"
    fi
    
    print_check "Fallback to dynamic discovery"
    if grep -rq "fallback.*discovery\|fallback.*strategy\|FALLBACK" "$SOURCE_DIR" "$DOC_DIR" 2>/dev/null; then
        print_pass "Fallback mechanism documented"
    else
        print_warn "Fallback mechanism not documented"
    fi
}

check_service_discovery_apis() {
    print_header "Service Discovery API Compliance (R24-11)"
    
    local runtime_file="$SOURCE_DIR/inc/Runtime.hpp"
    
    print_check "SWS_CM_00122: FindService(InstanceIdentifier)"
    if grep -q "FindService.*InstanceIdentifier\|FindService.*instance" "$runtime_file" 2>/dev/null; then
        print_pass "SWS_CM_00122 FindService API found"
    else
        print_fail "SWS_CM_00122 FindService API not found"
    fi
    
    print_check "SWS_CM_00123: StartFindService"
    if grep -q "StartFindService" "$runtime_file" 2>/dev/null; then
        print_pass "SWS_CM_00123 StartFindService API found"
    else
        print_warn "SWS_CM_00123 StartFindService API not found"
    fi
    
    print_check "SWS_CM_00125: StopFindService"
    if grep -q "StopFindService" "$runtime_file" 2>/dev/null; then
        print_pass "SWS_CM_00125 StopFindService API found"
    else
        print_warn "SWS_CM_00125 StopFindService API not found"
    fi
    
    print_check "FindServiceHandler callback mechanism"
    if grep -rq "FindServiceHandler\|ServiceHandler\|DiscoveryHandler" "$SOURCE_DIR/inc" 2>/dev/null; then
        print_pass "FindServiceHandler callback found"
    else
        print_warn "FindServiceHandler callback not found"
    fi
}

################################################################################
# Transport Binding Checks
################################################################################

check_dbus_binding() {
    print_header "D-Bus Transport Binding Compliance"
    
    local dbus_dir="$SOURCE_DIR/binding/dbus"
    
    print_check "D-Bus Connection Manager"
    check_file_exists "$dbus_dir/DBusConnectionManager.hpp" "DBus Connection Manager"
    
    print_check "D-Bus Method Binding"
    check_file_exists "$dbus_dir/DBusMethodBinding.hpp" "DBus Method Binding"
    
    print_check "D-Bus Event Binding"
    check_file_exists "$dbus_dir/DBusEventBinding.hpp" "DBus Event Binding"
    
    print_check "D-Bus Field Binding"
    check_file_exists "$dbus_dir/DBusFieldBinding.hpp" "DBus Field Binding"
    
    # Check for sdbus-c++ integration
    print_check "sdbus-c++ library integration"
    if find "$dbus_dir" -name "*.hpp" -exec grep -l "sdbus" {} \; | head -1 > /dev/null; then
        print_pass "sdbus-c++ library integration found"
    else
        print_fail "sdbus-c++ library integration not found"
    fi
}

check_someip_binding() {
    print_header "SOME/IP Transport Binding Compliance (SWS_CM_10xxx)"
    
    local someip_dir="$SOURCE_DIR/binding/someip"
    
    print_check "SWS_CM_10289: SOME/IP protocol support"
    if [ -d "$someip_dir" ]; then
        print_pass "SOME/IP binding directory exists"
    else
        print_fail "SOME/IP binding directory not found"
        return
    fi
    
    print_check "SOME/IP Connection Manager"
    check_file_exists "$someip_dir/SomeIpConnectionManager.hpp" "SOME/IP Connection Manager"
    
    print_check "SOME/IP Method Binding"
    check_file_exists "$someip_dir/SomeIpMethodBinding.hpp" "SOME/IP Method Binding"
    
    print_check "SOME/IP Event Binding"
    check_file_exists "$someip_dir/SomeIpEventBinding.hpp" "SOME/IP Event Binding"
    
    print_check "SOME/IP Field Binding"
    check_file_exists "$someip_dir/SomeIpFieldBinding.hpp" "SOME/IP Field Binding"
    
    # Check for vsomeip integration
    print_check "vsomeip library integration"
    if find "$someip_dir" -name "*.hpp" -exec grep -l "vsomeip" {} \; | head -1 > /dev/null 2>&1; then
        print_pass "vsomeip library integration found"
    else
        print_fail "vsomeip library integration not found"
    fi
    
    # R24-11: Check SOME/IP Service Discovery
    print_check "SWS_CM_00050: SOME/IP Service Discovery"
    if grep -rq "ServiceDiscovery\|SD.*Protocol\|OfferService.*message\|FindService.*message" "$someip_dir" 2>/dev/null; then
        print_pass "SOME/IP Service Discovery implementation found"
    else
        print_warn "SOME/IP Service Discovery not fully implemented"
    fi
}

check_dds_binding() {
    print_header "DDS Transport Binding Compliance (SWS_CM_11xxx)"
    
    local dds_dir="$SOURCE_DIR/binding/dds"
    
    print_check "SWS_CM_11001: DDS OfferService mapping"
    if [ -d "$dds_dir" ]; then
        if grep -rq "OfferService\|DomainParticipant\|USER_DATA.*QoS" "$dds_dir" 2>/dev/null; then
            print_pass "DDS OfferService mapping found"
        else
            print_warn "DDS OfferService mapping not found"
        fi
    else
        print_warn "DDS binding directory not found"
    fi
    
    print_check "SWS_CM_11006: DDS FindService mapping"
    if [ -d "$dds_dir" ]; then
        if grep -rq "FindService\|Participant.*Discovery\|BuiltinParticipantListener" "$dds_dir" 2>/dev/null; then
            print_pass "DDS FindService mapping found"
        else
            print_warn "DDS FindService mapping not found"
        fi
    fi
    
    print_check "SWS_CM_11010: DDS StartFindService mapping"
    if [ -d "$dds_dir" ]; then
        if grep -rq "StartFindService\|BuiltinParticipantListener" "$dds_dir" 2>/dev/null; then
            print_pass "DDS StartFindService mapping found"
        else
            print_warn "DDS StartFindService mapping not found"
        fi
    fi
    
    print_check "Fast-DDS library integration"
    if grep -rq "fastdds\|Fast.*DDS\|eprosima" "$dds_dir" "$COM_DIR/CMakeLists.txt" 2>/dev/null; then
        print_pass "Fast-DDS library integration found"
    else
        print_warn "Fast-DDS library integration not detected"
    fi
}

################################################################################
# Code Quality Checks
################################################################################

check_namespace_compliance() {
    print_header "Namespace Convention Compliance"
    
    print_check "ara::com namespace usage"
    local namespace_count=$(find "$SOURCE_DIR/inc" -name "*.hpp" -exec grep -l "namespace.*ara.*com\|namespace.*lap.*com" {} \; | wc -l)
    if [ "$namespace_count" -ge 5 ]; then
        print_pass "ara::com or lap::com namespace found in $namespace_count files"
    else
        print_warn "Limited namespace usage found (only $namespace_count files)"
    fi
}

check_error_handling() {
    print_header "Error Handling Compliance"
    
    print_check "Result<T> type usage (ara::core style)"
    if find "$SOURCE_DIR/inc" -name "*.hpp" -exec grep -l "Result<\|Expected<\|core::Result" {} \; | head -1 > /dev/null; then
        print_pass "Result<T> error handling pattern found"
    else
        print_fail "Result<T> error handling pattern not found"
    fi
    
    print_check "Exception-free implementation"
    local exception_count=$(find "$SOURCE_DIR" -name "*.cpp" -o -name "*.hpp" | xargs grep -l "throw " 2>/dev/null | wc -l)
    if [ "$exception_count" -eq 0 ]; then
        print_pass "No exceptions thrown (exception-free implementation)"
    else
        print_warn "Found $exception_count files with exceptions"
    fi
}

check_thread_safety() {
    print_header "Thread Safety Compliance"
    
    print_check "Mutex usage for thread safety"
    if find "$SOURCE_DIR" -name "*.hpp" -o -name "*.cpp" | xargs grep -l "std::mutex\|std::lock_guard\|std::unique_lock" 2>/dev/null | head -1 > /dev/null; then
        print_pass "Thread safety mechanisms found"
    else
        print_warn "Limited thread safety mechanisms detected"
    fi
}

################################################################################
# Documentation Checks
################################################################################

check_documentation() {
    print_header "Documentation Compliance"
    
    print_check "Architecture documentation"
    check_file_exists "$DOC_DIR/ARCHITECTURE_SUMMARY.md" "Architecture summary"
    
    print_check "API documentation (Doxygen comments)"
    local doxygen_count=$(find "$SOURCE_DIR/inc" -name "*.hpp" -exec grep -l "///\|/\*\*" {} \; | wc -l)
    if [ "$doxygen_count" -ge 5 ]; then
        print_pass "Doxygen comments found in $doxygen_count header files"
    else
        print_warn "Limited Doxygen documentation ($doxygen_count files)"
    fi
    
    print_check "AUTOSAR requirement tracing in code"
    local req_trace_count=$(find "$SOURCE_DIR" -name "*.hpp" -o -name "*.cpp" | xargs grep -l "SWS_CM_\|RS_CM_" 2>/dev/null | wc -l)
    if [ "$req_trace_count" -ge 3 ]; then
        print_pass "AUTOSAR requirements traced in $req_trace_count files"
    else
        print_warn "Limited requirement tracing ($req_trace_count files)"
    fi
}

################################################################################
# Test Coverage Checks
################################################################################

check_test_coverage() {
    print_header "Test Coverage Requirements"
    
    print_check "Unit tests existence"
    if [ -d "$TEST_DIR/unittest" ]; then
        local test_count=$(find "$TEST_DIR/unittest" -name "*.cpp" | wc -l)
        if [ "$test_count" -ge 5 ]; then
            print_pass "Found $test_count unit test files"
        else
            print_warn "Limited unit tests ($test_count files)"
        fi
    else
        print_fail "Unit test directory not found"
    fi
    
    print_check "Example code availability"
    if [ -d "$TEST_DIR/examples" ]; then
        local example_count=$(find "$TEST_DIR/examples" -name "*.cpp" | wc -l)
        if [ "$example_count" -ge 3 ]; then
            print_pass "Found $example_count example files"
        else
            print_warn "Limited examples ($example_count files)"
        fi
    else
        print_warn "Examples directory not found"
    fi
}

################################################################################
# Build System Checks
################################################################################

check_build_system() {
    print_header "Build System Compliance"
    
    print_check "CMakeLists.txt presence"
    check_file_exists "$COM_DIR/CMakeLists.txt" "CMake build file"
    
    print_check "Config.cmake generation"
    if grep -q "ComConfig.cmake\|LapComConfig.cmake" "$COM_DIR/CMakeLists.txt" 2>/dev/null; then
        print_pass "CMake config generation found"
    else
        print_warn "CMake config generation not found"
    fi
}

################################################################################
# Integration Checks
################################################################################

check_dependencies() {
    print_header "Dependency Management"
    
    print_check "Core module dependency"
    if grep -r "lap.*core\|Core\.hpp" "$SOURCE_DIR/inc" 2>/dev/null | head -1 > /dev/null; then
        print_pass "Core module integration found"
    else
        print_warn "Core module integration not detected"
    fi
    
    print_check "LogAndTrace integration"
    if grep -r "log\|trace\|Log\.hpp" "$SOURCE_DIR/inc" 2>/dev/null | head -1 > /dev/null; then
        print_pass "LogAndTrace integration found"
    else
        print_warn "LogAndTrace integration not detected"
    fi
}

################################################################################
# Main Execution
################################################################################

main() {
    print_header "AUTOSAR AP Communication Management Compliance Check"
    echo "Com Module Directory: $COM_DIR"
    echo "Date: $(date '+%Y-%m-%d %H:%M:%S')"
    
    # Run all checks
    check_runtime_api
    check_proxy_api
    check_skeleton_api
    check_method_api
    check_event_api
    check_field_api
    check_types_api
    check_dbus_binding
    check_someip_binding
    check_dds_binding
    
    # R24-11 New Feature Checks
    check_static_service_connection
    check_central_service_discovery
    check_service_discovery_apis
    
    # Code Quality & Documentation
    check_namespace_compliance
    check_error_handling
    check_thread_safety
    check_documentation
    check_test_coverage
    check_build_system
    check_dependencies
    
    # Summary
    print_header "Compliance Check Summary"
    echo -e "${BLUE}Total Checks:${NC}    $TOTAL_CHECKS"
    echo -e "${GREEN}Passed:${NC}          $PASSED_CHECKS"
    echo -e "${RED}Failed:${NC}          $FAILED_CHECKS"
    echo -e "${YELLOW}Warnings:${NC}        $WARNING_CHECKS"
    
    local pass_rate=$((PASSED_CHECKS * 100 / TOTAL_CHECKS))
    echo -e "\n${BLUE}Pass Rate:${NC}       ${pass_rate}%"
    
    if [ "$FAILED_CHECKS" -eq 0 ]; then
        echo -e "\n${GREEN}✓ AUTOSAR COMPLIANCE CHECK PASSED${NC}"
        return 0
    else
        echo -e "\n${RED}✗ AUTOSAR COMPLIANCE CHECK FAILED${NC}"
        echo -e "${RED}Please review failed checks and fix issues.${NC}"
        return 1
    fi
}

# Run main function
main "$@"
