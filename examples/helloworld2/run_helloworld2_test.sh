#!/usr/bin/env bash
# ==========================================================================
# run_helloworld2_test.sh
#
# Orchestrates the HelloWorld2 integration test (standard CoreIPC flow):
#   1. Kills leftover processes from previous runs
#   2. Starts helloworld2_server in background
#   3. Runs   helloworld2_test   (waits for server to appear)
#   4. Collects exit code, cleans up
#
# Generated artifacts in gen/ are produced by:
#   python3 tools/gen_pipeline.py \
#       -i HelloWorld2.fidl -o gen/ --backend all \
#       --sidl-gen <path>/lap_sidl_gen
#
# Usage:
#   ./run_helloworld2_test.sh [build_dir]
#
# ==========================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${1:-$SCRIPT_DIR}"

SERVER_BIN="${BUILD_DIR}/helloworld2_server"
TEST_BIN="${BUILD_DIR}/helloworld2_test"

for bin in "$SERVER_BIN" "$TEST_BIN"; do
    if [[ ! -x "$bin" ]]; then
        echo "[ERROR] Binary not found or not executable: $bin"
        echo "        Build the example first."
        exit 1
    fi
done

# ---- Cleanup ---------------------------------------------------------------
SERVER_PID=""

cleanup() {
    local ec=$?
    echo ""
    echo "[Cleanup] Stopping server ..."
    if [[ -n "${SERVER_PID:-}" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -TERM "$SERVER_PID" 2>/dev/null || true
        sleep 0.5
        kill -9  "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    echo "[Cleanup] Done (exit $ec)"
    exit "$ec"
}
trap cleanup EXIT INT TERM

# ---- Kill leftover processes -----------------------------------------------
# Use exact binary name matching (not -f) to avoid killing ourselves.
# pkill -f matches command-line arguments which include our own $TEST_BIN path.
MY_PID=$$
echo "[Setup] Killing any leftover helloworld2 processes ..."
pkill -x "helloworld2_server" 2>/dev/null || true
pkill -x "helloworld2_test"   2>/dev/null || true
sleep 0.5

# ---- Start server ----------------------------------------------------------
echo "[Setup] Starting server ..."
"$SERVER_BIN" &
SERVER_PID=$!
echo "[Setup] Server PID=$SERVER_PID"

# ---- Run test --------------------------------------------------------------
echo "[Test ] Waiting for server then running test ..."
sleep 2   # give registry dispatcher time to bind

"$TEST_BIN"
TEST_EXIT=$?

# ---- Report -----------------------------------------------------------------
if [[ $TEST_EXIT -eq 0 ]]; then
    echo ""
    echo "=== TEST PASSED ==="
else
    echo ""
    echo "=== TEST FAILED (exit $TEST_EXIT) ==="
fi

exit "$TEST_EXIT"
