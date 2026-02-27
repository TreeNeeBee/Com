#!/bin/bash
# =============================================================================
# HelloWorld multi-process test
# Launches server and client as separate processes.
# Usage: ./run_test.sh [build_dir]
# =============================================================================
set -euo pipefail

BUILD_DIR="${1:-$(cd "$(dirname "$0")/../../build" && pwd)}"
SERVER="$BUILD_DIR/helloworld_server"
CLIENT="$BUILD_DIR/helloworld_client"

echo "========================================="
echo " HelloWorld Multi-Process Test"
echo "========================================="
echo "Build dir : $BUILD_DIR"
echo ""

# Check binaries exist
for bin in "$SERVER" "$CLIENT"; do
    if [[ ! -x "$bin" ]]; then
        echo "[ERROR] Binary not found: $bin"
        echo "Build with: cd $BUILD_DIR && cmake --build . --target helloworld_server helloworld_client"
        exit 1
    fi
done

# Clean shared memory
rm -f /dev/shm/lap_*

# Start server in background
echo "[Test] Starting server ..."
"$SERVER" &
SERVER_PID=$!

# Give server time to initialize and offer service
sleep 2

# Run client (foreground)
echo "[Test] Starting client ..."
RC=0
"$CLIENT" || RC=$?

# Stop server
echo ""
echo "[Test] Stopping server (PID=$SERVER_PID) ..."
kill -SIGINT "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true

# Cleanup
echo "[Cleanup] Removing shared memory segments ..."
rm -f /dev/shm/lap_*

echo ""
echo "========================================="
if [[ $RC -eq 0 ]]; then
    echo " HELLOWORLD TEST: PASSED"
else
    echo " HELLOWORLD TEST: FAILED (rc=$RC)"
fi
echo "========================================="

exit $RC
