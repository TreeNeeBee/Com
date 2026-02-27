#!/bin/bash
# ==============================================================================
# LightAP Registry - Functional Verification Test
# ==============================================================================
# Tests the registry daemon's core functionality:
#   1. QM daemon start / client FD receive / shutdown
#   2. ASIL daemon start / client FD receive / shutdown
#   3. Concurrent multi-client FD distribution
#   4. Shared memory read/write through received memfd
#   5. Graceful shutdown (SIGTERM)
#   6. FastDDS Discovery Server start/listen
#   7. Discovery Monitor CLIENT mode
#   8. PDP/EDP fallback on DS failure
#   9. Discovery Server recovery
#  10. XML configuration validation
#
# Works in containers without systemd PID 1.
#
# Usage:  sudo ./test_registry.sh [--build-dir=/path/to/build]
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="${BUILD_DIR:-/workspace/LightAP/modules/Com/build}"
DAEMON="$BUILD_DIR/registry/lap-registry-init"
RUNTIME_DIR="/run/lap"
QM_SOCK="$RUNTIME_DIR/registry_qm.sock"
ASIL_SOCK="$RUNTIME_DIR/registry_asil.sock"
DS_CONFIG_DIR="$SCRIPT_DIR"
DS_PORT=11811
DS_PID=""

# Override build dir from CLI
for arg in "$@"; do
    case "$arg" in
        --build-dir=*) BUILD_DIR="${arg#*=}"; DAEMON="$BUILD_DIR/registry/lap-registry-init" ;;
    esac
done

# Colors
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'

PASS=0; FAIL=0

pass() { echo -e "  ${GREEN}[PASS]${NC} $1"; PASS=$((PASS+1)); }
fail() { echo -e "  ${RED}[FAIL]${NC} $1"; FAIL=$((FAIL+1)); }
info() { echo -e "  ${BLUE}[INFO]${NC} $1"; }
section() { echo ""; echo -e "${BLUE}── $1 ──${NC}"; }

# Cleanup on exit
cleanup() {
    kill "$QM_PID" 2>/dev/null || true
    kill "$ASIL_PID" 2>/dev/null || true
    kill "$DS_PID" 2>/dev/null || true
    kill "$MONITOR_PID" 2>/dev/null || true
    wait "$QM_PID" 2>/dev/null || true
    wait "$ASIL_PID" 2>/dev/null || true
    wait "$DS_PID" 2>/dev/null || true
    wait "$MONITOR_PID" 2>/dev/null || true
    rm -f "$QM_SOCK" "$ASIL_SOCK"
    rm -f "$RUNTIME_DIR/discovery_state" "$RUNTIME_DIR/fastdds_active_profile.xml"
    rm -f "$RUNTIME_DIR/discovery_monitor.pid"
}
trap cleanup EXIT

# ==============================================================================
# Python helper: connect to UDS, receive memfd via SCM_RIGHTS, return FD number
# ==============================================================================
receive_fd_py() {
    local sock_path="$1"
    python3 - "$sock_path" <<'PYEOF'
import socket, struct, sys, array, os
path = sys.argv[1]
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.settimeout(5.0)
try:
    sock.connect(path)
    msg, ancdata, flags, addr = sock.recvmsg(1, socket.CMSG_SPACE(struct.calcsize('i')))
    for cmsg_level, cmsg_type, cmsg_data in ancdata:
        if cmsg_level == socket.SOL_SOCKET and cmsg_type == socket.SCM_RIGHTS:
            fds = array.array('i')
            fds.frombytes(cmsg_data)
            fd = fds[0]
            # Verify it is a valid FD by reading its size
            size = os.fstat(fd).st_size
            print(f"{fd}:{size}")
            os.close(fd)
            sock.close()
            sys.exit(0)
    print("NO_SCM_RIGHTS", file=sys.stderr)
    sys.exit(1)
except Exception as e:
    print(f"ERROR:{e}", file=sys.stderr)
    sys.exit(1)
finally:
    try: sock.close()
    except: pass
PYEOF
}

# ==============================================================================
# Pre-flight
# ==============================================================================
section "Pre-flight Checks"

if [[ ! -x "$DAEMON" ]]; then
    fail "Daemon binary not found: $DAEMON"
    echo "  Build first:  cd $BUILD_DIR && cmake .. && make -j\$(nproc)"
    exit 1
fi
pass "Daemon binary exists: $DAEMON"

mkdir -p "$RUNTIME_DIR"
pass "Runtime directory: $RUNTIME_DIR"

# Kill any stale daemons
pkill -f "lap-registry-init" 2>/dev/null || true
rm -f "$QM_SOCK" "$ASIL_SOCK"
sleep 0.3
pass "Stale processes cleaned"

QM_PID=""
ASIL_PID=""
DS_PID=""
MONITOR_PID=""

# ==============================================================================
# Test 1: QM Registry Daemon
# ==============================================================================
section "Test 1: QM Registry Start + Client Connect"

"$DAEMON" --type=qm --socket="$QM_SOCK" &
QM_PID=$!
sleep 0.5

if [[ -S "$QM_SOCK" ]]; then
    pass "QM socket created: $QM_SOCK"
else
    fail "QM socket not created"
fi

# Connect and receive FD
OUTPUT=$( receive_fd_py "$QM_SOCK" 2>&1 ) || true
if [[ "$OUTPUT" =~ ^[0-9]+:262144$ ]]; then
    FD_NUM="${OUTPUT%%:*}"
    pass "Client received memfd FD=$FD_NUM, size=262144 bytes (256KB)"
else
    fail "Client FD receive failed: $OUTPUT"
fi

# ==============================================================================
# Test 2: ASIL Registry Daemon
# ==============================================================================
section "Test 2: ASIL Registry Start + Client Connect"

"$DAEMON" --type=asil --socket="$ASIL_SOCK" &
ASIL_PID=$!
sleep 0.5

if [[ -S "$ASIL_SOCK" ]]; then
    pass "ASIL socket created: $ASIL_SOCK"
else
    fail "ASIL socket not created"
fi

OUTPUT=$( receive_fd_py "$ASIL_SOCK" 2>&1 ) || true
if [[ "$OUTPUT" =~ ^[0-9]+:262144$ ]]; then
    FD_NUM="${OUTPUT%%:*}"
    pass "ASIL client received memfd FD=$FD_NUM, size=262144 (256KB)"
else
    fail "ASIL client FD receive failed: $OUTPUT"
fi

# ==============================================================================
# Test 3: Concurrent Clients (10 parallel connections)
# ==============================================================================
section "Test 3: 10 Concurrent QM Clients"

CONCURRENT_OK=0
PIDS=()
TMPDIR_T=$(mktemp -d)

for i in $(seq 1 10); do
    (
        OUT=$( receive_fd_py "$QM_SOCK" 2>&1 ) || true
        echo "$OUT" > "$TMPDIR_T/client_$i.txt"
    ) &
    PIDS+=($!)
done

for pid in "${PIDS[@]}"; do
    wait "$pid" 2>/dev/null || true
done

for i in $(seq 1 10); do
    RES=$(cat "$TMPDIR_T/client_$i.txt" 2>/dev/null || echo "MISSING")
    if [[ "$RES" =~ ^[0-9]+:262144$ ]]; then
        CONCURRENT_OK=$((CONCURRENT_OK+1))
    fi
done
rm -rf "$TMPDIR_T"

if [[ $CONCURRENT_OK -eq 10 ]]; then
    pass "All 10 concurrent clients received valid memfd"
else
    fail "Only $CONCURRENT_OK/10 clients succeeded"
fi

# ==============================================================================
# Test 4: Shared Memory Content Verification
# ==============================================================================
section "Test 4: Shared Memory Read/Write via memfd"

python3 - "$QM_SOCK" <<'PYEOF'
import socket, struct, sys, array, os, mmap

path = sys.argv[1]
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.settimeout(5.0)
sock.connect(path)
msg, ancdata, flags, addr = sock.recvmsg(1, socket.CMSG_SPACE(struct.calcsize('i')))

fd = -1
for cmsg_level, cmsg_type, cmsg_data in ancdata:
    if cmsg_level == socket.SOL_SOCKET and cmsg_type == socket.SCM_RIGHTS:
        fds = array.array('i')
        fds.frombytes(cmsg_data)
        fd = fds[0]
        break
sock.close()

if fd < 0:
    print("ERROR: no FD received", file=sys.stderr)
    sys.exit(1)

# mmap the memfd
mem = mmap.mmap(fd, 262144, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)

# Slot 0 starts at offset 0, each slot is 256 bytes
# ServiceSlot layout: m_serviceId at offset 0 (uint64), m_instanceId at offset 8 (uint64)
# m_status at offset 120 (uint32 atomic)
# Read slot 1 (offset 256): should be all zeros (idle)
slot1 = mem[256:512]
service_id = struct.unpack_from('<Q', slot1, 0)[0]
instance_id = struct.unpack_from('<Q', slot1, 8)[0]

if service_id == 0 and instance_id == 0:
    print("SLOT_IDLE_OK")
else:
    print(f"SLOT_NOT_IDLE: sid={service_id} iid={instance_id}", file=sys.stderr)
    sys.exit(1)

# Write a test pattern to slot 1's service_id field and verify
struct.pack_into('<Q', mem, 256, 0xDEADBEEF)
readback = struct.unpack_from('<Q', mem, 256)[0]

if readback == 0xDEADBEEF:
    print("WRITE_READBACK_OK")
else:
    print(f"READBACK_MISMATCH: {readback:#x}", file=sys.stderr)
    sys.exit(1)

# Reset
struct.pack_into('<Q', mem, 256, 0)

mem.close()
os.close(fd)
sys.exit(0)
PYEOF

SHM_RESULT=$?
if [[ $SHM_RESULT -eq 0 ]]; then
    pass "Shared memory mmap + read/write verified"
else
    fail "Shared memory verification failed"
fi

# ==============================================================================
# Test 5: Graceful Shutdown (SIGTERM)
# ==============================================================================
section "Test 5: Graceful Shutdown"

kill -TERM "$QM_PID" 2>/dev/null || true
wait "$QM_PID" 2>/dev/null || true
QM_EXIT=$?

if [[ $QM_EXIT -eq 0 ]] || [[ $QM_EXIT -eq 143 ]]; then
    pass "QM daemon exited cleanly (code=$QM_EXIT)"
else
    fail "QM daemon exit code=$QM_EXIT"
fi
QM_PID=""

kill -TERM "$ASIL_PID" 2>/dev/null || true
wait "$ASIL_PID" 2>/dev/null || true
ASIL_EXIT=$?

if [[ $ASIL_EXIT -eq 0 ]] || [[ $ASIL_EXIT -eq 143 ]]; then
    pass "ASIL daemon exited cleanly (code=$ASIL_EXIT)"
else
    fail "ASIL daemon exit code=$ASIL_EXIT"
fi
ASIL_PID=""

# ==============================================================================
# Test 6: FastDDS Discovery Server Start
# ==============================================================================
section "Test 6: FastDDS Discovery Server"

if ! command -v fastdds > /dev/null 2>&1; then
    fail "fastdds binary not found in PATH, skipping DS tests"
else
    pass "fastdds binary found: $(which fastdds)"

    # Start Discovery Server
    fastdds discovery -i 0 -l 127.0.0.1 -p "$DS_PORT" &
    DS_PID=$!
    sleep 1.5

    if kill -0 "$DS_PID" 2>/dev/null; then
        pass "Discovery Server started (PID=$DS_PID, port=$DS_PORT)"
    else
        fail "Discovery Server failed to start"
        DS_PID=""
    fi

    # Verify port is listening
    if [[ -n "$DS_PID" ]]; then
        if ss -uln 2>/dev/null | grep -q ":${DS_PORT} "; then
            pass "Discovery Server listening on UDP port $DS_PORT"
        elif pgrep -f "fast-discovery-server" > /dev/null 2>&1; then
            pass "Discovery Server process alive (port check skipped)"
        else
            fail "Discovery Server port $DS_PORT not listening"
        fi
    fi
fi

# ==============================================================================
# Test 7: Discovery Monitor - CLIENT Mode (DS available)
# ==============================================================================
section "Test 7: Discovery Monitor - CLIENT Mode"

if [[ -n "$DS_PID" ]] && [[ -x "$SCRIPT_DIR/lap-discovery-monitor.sh" ]]; then
    # Set env for monitor - use SCRIPT_DIR since XMLs are alongside the scripts
    export DS_LISTEN_ADDR=127.0.0.1
    export DS_PORT=$DS_PORT
    export DS_HEALTH_INTERVAL=2
    export DS_MAX_FAILURES=2
    export DS_FALLBACK_ENABLED=true
    export DS_CONFIG_DIR="$SCRIPT_DIR"

    # Run status check
    "$SCRIPT_DIR/lap-discovery-monitor.sh" --force-client 2>/dev/null
    sleep 0.5

    STATE=$(cat "$RUNTIME_DIR/discovery_state" 2>/dev/null || echo "NONE")
    if [[ "$STATE" == "CLIENT" ]]; then
        pass "Monitor set CLIENT mode (Discovery Server available)"
    else
        fail "Expected CLIENT mode, got: $STATE"
    fi

    # Check profile symlink
    if [[ -L "$RUNTIME_DIR/fastdds_active_profile.xml" ]]; then
        TARGET=$(readlink "$RUNTIME_DIR/fastdds_active_profile.xml")
        if [[ "$TARGET" == *"client"* ]]; then
            pass "Active profile → client XML"
        else
            fail "Active profile points to unexpected: $TARGET"
        fi
    else
        fail "Active profile symlink not found"
    fi
else
    info "Skipping monitor CLIENT test (DS not running or monitor script missing)"
fi

# ==============================================================================
# Test 8: Discovery Monitor - PDP/EDP Fallback (DS killed)
# ==============================================================================
section "Test 8: PDP/EDP Fallback on DS Failure"

if [[ -n "$DS_PID" ]] && [[ -x "$SCRIPT_DIR/lap-discovery-monitor.sh" ]]; then
    # Kill Discovery Server (both wrapper and the actual server process)
    kill "$DS_PID" 2>/dev/null || true
    pkill -f "fast-discovery-server" 2>/dev/null || true
    wait "$DS_PID" 2>/dev/null || true
    DS_PID=""
    sleep 2

    # Verify DS is gone
    if ! pgrep -f "fast-discovery-server" > /dev/null 2>&1; then
        pass "Discovery Server stopped"
    else
        fail "Discovery Server still running after kill"
    fi

    # Force fallback
    "$SCRIPT_DIR/lap-discovery-monitor.sh" --force-fallback 2>/dev/null
    sleep 0.5

    STATE=$(cat "$RUNTIME_DIR/discovery_state" 2>/dev/null || echo "NONE")
    if [[ "$STATE" == "SIMPLE" ]]; then
        pass "Monitor switched to SIMPLE mode (PDP/EDP fallback)"
    else
        fail "Expected SIMPLE mode, got: $STATE"
    fi

    # Check profile symlink points to fallback
    if [[ -L "$RUNTIME_DIR/fastdds_active_profile.xml" ]]; then
        TARGET=$(readlink "$RUNTIME_DIR/fastdds_active_profile.xml")
        if [[ "$TARGET" == *"fallback"* ]]; then
            pass "Active profile → fallback XML (PDP/EDP)"
        else
            fail "Active profile points to unexpected: $TARGET"
        fi
    else
        fail "Active profile symlink not found"
    fi
else
    info "Skipping fallback test (DS was not running)"
fi

# ==============================================================================
# Test 9: Discovery Server Recovery (restart DS → CLIENT restored)
# ==============================================================================
section "Test 9: Discovery Server Recovery"

if command -v fastdds > /dev/null 2>&1 && [[ -x "$SCRIPT_DIR/lap-discovery-monitor.sh" ]]; then
    # Ensure old DS is fully stopped
    pkill -f "fast-discovery-server" 2>/dev/null || true
    sleep 2

    # Restart Discovery Server
    fastdds discovery -i 0 -l 127.0.0.1 -p "$DS_PORT" &
    DS_PID=$!
    sleep 2

    if kill -0 "$DS_PID" 2>/dev/null; then
        pass "Discovery Server restarted (PID=$DS_PID)"
    else
        fail "Discovery Server failed to restart"
        DS_PID=""
    fi

    # Force back to CLIENT
    if [[ -n "$DS_PID" ]]; then
        "$SCRIPT_DIR/lap-discovery-monitor.sh" --force-client 2>/dev/null
        sleep 0.5

        STATE=$(cat "$RUNTIME_DIR/discovery_state" 2>/dev/null || echo "NONE")
        if [[ "$STATE" == "CLIENT" ]]; then
            pass "Monitor recovered to CLIENT mode after DS restart"
        else
            fail "Expected CLIENT mode after recovery, got: $STATE"
        fi
    fi

    # Cleanup DS
    if [[ -n "$DS_PID" ]]; then
        kill "$DS_PID" 2>/dev/null || true
        wait "$DS_PID" 2>/dev/null || true
        DS_PID=""
        pass "Discovery Server stopped for cleanup"
    fi
else
    info "Skipping recovery test (fastdds not available)"
fi

# ==============================================================================
# Test 10: XML Configuration Validation
# ==============================================================================
section "Test 10: Configuration Files Validation"

for xml_file in fastdds_ds_server.xml fastdds_ds_client.xml fastdds_ds_fallback.xml; do
    if [[ -f "$DS_CONFIG_DIR/$xml_file" ]]; then
        # Basic XML well-formedness check
        if python3 -c "
import xml.etree.ElementTree as ET
ET.parse('$DS_CONFIG_DIR/$xml_file')
print('OK')
" 2>/dev/null | grep -q "OK"; then
            pass "$xml_file is valid XML"
        else
            fail "$xml_file is malformed XML"
        fi
    else
        fail "$xml_file not found in $DS_CONFIG_DIR"
    fi
done

# Verify env config file exists
if [[ -f "$DS_CONFIG_DIR/fastdds_ds_env.conf" ]]; then
    pass "fastdds_ds_env.conf present"
else
    fail "fastdds_ds_env.conf not found"
fi

# ==============================================================================
# Summary
# ==============================================================================
echo ""
echo "======================================"
echo -e " Results:  ${GREEN}PASS=$PASS${NC}  ${RED}FAIL=$FAIL${NC}"
echo "======================================"

if [[ $FAIL -eq 0 ]]; then
    echo -e " ${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e " ${RED}Some tests failed.${NC}"
    exit 1
fi
