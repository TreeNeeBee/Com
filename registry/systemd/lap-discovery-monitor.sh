#!/bin/bash
# ==============================================================================
# LightAP Discovery Server - Health Monitor & PDP/EDP Fallback
# ==============================================================================
# Monitors the FastDDS Discovery Server and switches DDS participants to
# PDP/EDP (SIMPLE) fallback mode when the server becomes unavailable.
#
# Architecture reference:
#   SERVICE_DISCOVERY_ARCHITECTURE §2.0.6 (Fault Recovery)
#   SERVICE_DISCOVERY_ARCHITECTURE §5.2.3 (FastDDS Discovery Server)
#
# Degradation chain:
#   Discovery Server (CLIENT) → health-check failure × N
#     → Switch to PDP/EDP (SIMPLE) multicast discovery
#     → Continue monitoring → auto-recover to CLIENT when server returns
#
# Usage:
#   ./lap-discovery-monitor.sh                    # Run in foreground
#   ./lap-discovery-monitor.sh --daemon            # Run as background daemon
#   ./lap-discovery-monitor.sh --status            # One-shot status check
#   ./lap-discovery-monitor.sh --force-fallback    # Force PDP/EDP mode
#   ./lap-discovery-monitor.sh --force-client      # Force CLIENT mode
#
# Environment: sources fastdds_ds_env.conf for configuration
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# ==============================================================================
# Configuration (defaults, overridden by env file, then by env vars)
# ==============================================================================
# Save any pre-set env vars (they take priority)
_DS_LISTEN_ADDR="${DS_LISTEN_ADDR:-}"
_DS_PORT="${DS_PORT:-}"
_DS_HEALTH_INTERVAL="${DS_HEALTH_INTERVAL:-}"
_DS_MAX_FAILURES="${DS_MAX_FAILURES:-}"
_DS_FALLBACK_ENABLED="${DS_FALLBACK_ENABLED:-}"
_DS_CONFIG_DIR="${DS_CONFIG_DIR:-}"

# Source env file (provides defaults)
[[ -f "$SCRIPT_DIR/fastdds_ds_env.conf" ]] && source "$SCRIPT_DIR/fastdds_ds_env.conf"

# Restore pre-set env vars (override env file values)
[[ -n "$_DS_LISTEN_ADDR" ]]     && DS_LISTEN_ADDR="$_DS_LISTEN_ADDR"
[[ -n "$_DS_PORT" ]]            && DS_PORT="$_DS_PORT"
[[ -n "$_DS_HEALTH_INTERVAL" ]] && DS_HEALTH_INTERVAL="$_DS_HEALTH_INTERVAL"
[[ -n "$_DS_MAX_FAILURES" ]]    && DS_MAX_FAILURES="$_DS_MAX_FAILURES"
[[ -n "$_DS_FALLBACK_ENABLED" ]]&& DS_FALLBACK_ENABLED="$_DS_FALLBACK_ENABLED"
[[ -n "$_DS_CONFIG_DIR" ]]      && DS_CONFIG_DIR="$_DS_CONFIG_DIR"

# Apply final defaults for anything still unset
DS_LISTEN_ADDR="${DS_LISTEN_ADDR:-127.0.0.1}"
DS_PORT="${DS_PORT:-11811}"
DS_HEALTH_INTERVAL="${DS_HEALTH_INTERVAL:-5}"
DS_MAX_FAILURES="${DS_MAX_FAILURES:-3}"
DS_FALLBACK_ENABLED="${DS_FALLBACK_ENABLED:-true}"
DS_CONFIG_DIR="${DS_CONFIG_DIR:-/etc/lap/discovery}"

# Also source from installed config dir if different from script dir
[[ -f "$DS_CONFIG_DIR/fastdds_ds_env.conf" ]] && \
    [[ "$DS_CONFIG_DIR" != "$SCRIPT_DIR" ]] && \
    source "$DS_CONFIG_DIR/fastdds_ds_env.conf"

# Runtime state
STATE_FILE="/run/lap/discovery_state"
ACTIVE_PROFILE_LINK="/run/lap/fastdds_active_profile.xml"
CLIENT_XML="$DS_CONFIG_DIR/fastdds_ds_client.xml"
FALLBACK_XML="$DS_CONFIG_DIR/fastdds_ds_fallback.xml"

# Colors (only for terminal output)
if [[ -t 1 ]]; then
    RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
    BLUE='\033[0;34m'; NC='\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; BLUE=''; NC=''
fi

log_info()  { echo -e "${BLUE}[INFO]${NC}  $(date '+%H:%M:%S') $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $(date '+%H:%M:%S') $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $(date '+%H:%M:%S') $*"; }
log_ok()    { echo -e "${GREEN}[OK]${NC}    $(date '+%H:%M:%S') $*"; }

# ==============================================================================
# Health check: probe Discovery Server UDP port via netcat/python
# ==============================================================================
check_ds_health() {
    # Method 1: Check if the DS process is alive (fast, local only)
    if pgrep -f "fast-discovery-server" > /dev/null 2>&1; then
        # Process is running; additionally verify the port is open
        if command -v ss > /dev/null 2>&1; then
            if ss -uln | grep -q ":${DS_PORT} " 2>/dev/null; then
                return 0
            fi
        else
            # ss not available, trust the process check
            return 0
        fi
    fi

    # Method 2: Try UDP probe (for remote Discovery Server)
    if command -v python3 > /dev/null 2>&1; then
        python3 -c "
import socket, sys
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(2.0)
try:
    # Send a minimal RTPS header probe to the DS port
    # Magic: RTPS, version 2.3, vendorId eprosima
    probe = b'RTPS\x02\x03\x01\x0f' + b'\x00' * 12
    sock.sendto(probe, ('${DS_LISTEN_ADDR}', ${DS_PORT}))
    # If sendto doesn't raise, port is at least reachable
    sys.exit(0)
except Exception:
    sys.exit(1)
finally:
    sock.close()
" 2>/dev/null
        return $?
    fi

    # Method 3: Simple port check via /dev/udp (bash built-in, may not work)
    if (echo > /dev/udp/"${DS_LISTEN_ADDR}"/"${DS_PORT}") 2>/dev/null; then
        return 0
    fi

    return 1
}

# ==============================================================================
# Get current discovery mode
# ==============================================================================
get_current_mode() {
    if [[ -f "$STATE_FILE" ]]; then
        cat "$STATE_FILE"
    else
        echo "unknown"
    fi
}

set_mode() {
    local mode="$1"
    echo "$mode" > "$STATE_FILE"
}

# ==============================================================================
# Switch to CLIENT mode (Discovery Server available)
# ==============================================================================
activate_client_mode() {
    log_info "Activating CLIENT mode (Discovery Server at ${DS_LISTEN_ADDR}:${DS_PORT})"

    if [[ -f "$CLIENT_XML" ]]; then
        ln -sf "$CLIENT_XML" "$ACTIVE_PROFILE_LINK"
        set_mode "CLIENT"
        log_ok "CLIENT mode active → $ACTIVE_PROFILE_LINK → $CLIENT_XML"
    else
        log_error "Client XML not found: $CLIENT_XML"
        return 1
    fi
}

# ==============================================================================
# Switch to SIMPLE mode (PDP/EDP fallback)
# ==============================================================================
activate_fallback_mode() {
    log_warn "Activating SIMPLE mode (PDP/EDP multicast fallback)"

    if [[ -f "$FALLBACK_XML" ]]; then
        ln -sf "$FALLBACK_XML" "$ACTIVE_PROFILE_LINK"
        set_mode "SIMPLE"
        log_warn "SIMPLE (PDP/EDP) mode active → $ACTIVE_PROFILE_LINK → $FALLBACK_XML"
        log_warn "⚠ Discovery latency degraded: <5ms → 10-50ms"
        log_warn "⚠ Multicast bandwidth usage increased"
    else
        log_error "Fallback XML not found: $FALLBACK_XML"
        return 1
    fi
}

# ==============================================================================
# One-shot status
# ==============================================================================
do_status() {
    echo "======================================"
    echo " LightAP Discovery Server Status"
    echo "======================================"
    echo ""

    # Discovery Server process
    echo "Discovery Server:"
    local ds_pid
    ds_pid=$( pgrep -f "fast-discovery-server" 2>/dev/null || echo "" )
    if [[ -n "$ds_pid" ]]; then
        echo -e "  ${GREEN}●${NC} Running (PID $ds_pid)"
        echo "  Endpoint: ${DS_LISTEN_ADDR}:${DS_PORT}"
    else
        echo -e "  ${RED}●${NC} Not running"
    fi

    echo ""
    echo "Active Discovery Mode:"
    local mode
    mode=$( get_current_mode )
    case "$mode" in
        CLIENT)
            echo -e "  ${GREEN}●${NC} CLIENT (centralized Discovery Server)"
            ;;
        SIMPLE)
            echo -e "  ${YELLOW}●${NC} SIMPLE (PDP/EDP multicast fallback)"
            ;;
        *)
            echo -e "  ${RED}●${NC} Unknown / not initialized"
            ;;
    esac

    echo ""
    echo "Profile symlink:"
    if [[ -L "$ACTIVE_PROFILE_LINK" ]]; then
        echo "  $ACTIVE_PROFILE_LINK → $(readlink "$ACTIVE_PROFILE_LINK")"
    else
        echo "  Not configured"
    fi

    echo ""
    echo "Health check:"
    if check_ds_health; then
        echo -e "  ${GREEN}●${NC} Discovery Server reachable"
    else
        echo -e "  ${RED}●${NC} Discovery Server unreachable"
    fi

    echo ""
    echo "Configuration:"
    echo "  Health interval:  ${DS_HEALTH_INTERVAL}s"
    echo "  Max failures:     ${DS_MAX_FAILURES}"
    echo "  Fallback enabled: ${DS_FALLBACK_ENABLED}"
    echo "  Config dir:       ${DS_CONFIG_DIR}"
}

# ==============================================================================
# Monitor loop: continuously check DS health, switch modes as needed
# ==============================================================================
do_monitor() {
    log_info "Discovery Monitor started"
    log_info "Target: ${DS_LISTEN_ADDR}:${DS_PORT}"
    log_info "Health interval: ${DS_HEALTH_INTERVAL}s, max failures: ${DS_MAX_FAILURES}"
    log_info "Fallback enabled: ${DS_FALLBACK_ENABLED}"

    mkdir -p /run/lap

    local failure_count=0
    local recovery_count=0

    # Initial check
    if check_ds_health; then
        activate_client_mode
        failure_count=0
    else
        log_warn "Discovery Server not available at startup"
        if [[ "$DS_FALLBACK_ENABLED" == "true" ]]; then
            activate_fallback_mode
        fi
        failure_count=$DS_MAX_FAILURES
    fi

    # Continuous monitoring loop
    while true; do
        sleep "$DS_HEALTH_INTERVAL"

        if check_ds_health; then
            # Server is healthy
            if [[ $failure_count -gt 0 ]]; then
                failure_count=0
                recovery_count=$((recovery_count + 1))
                log_ok "Discovery Server recovered (recovery #${recovery_count})"

                if [[ "$(get_current_mode)" != "CLIENT" ]]; then
                    activate_client_mode
                    log_ok "Restored CLIENT mode from PDP/EDP fallback"
                fi
            fi
        else
            # Server unreachable
            failure_count=$((failure_count + 1))

            if [[ $failure_count -le $DS_MAX_FAILURES ]]; then
                log_warn "Health check failed ($failure_count/$DS_MAX_FAILURES)"
            fi

            if [[ $failure_count -eq $DS_MAX_FAILURES ]] && \
               [[ "$DS_FALLBACK_ENABLED" == "true" ]]; then
                log_error "Discovery Server unreachable after $DS_MAX_FAILURES checks"
                activate_fallback_mode
            fi
        fi
    done
}

# ==============================================================================
# Main
# ==============================================================================
case "${1:-}" in
    --status)
        do_status
        ;;
    --force-fallback)
        mkdir -p /run/lap
        activate_fallback_mode
        ;;
    --force-client)
        mkdir -p /run/lap
        activate_client_mode
        ;;
    --daemon)
        # Redirect to journal-friendly output, run in background
        do_monitor &
        echo $! > /run/lap/discovery_monitor.pid
        log_info "Monitor daemonized (PID $!)"
        ;;
    *)
        do_monitor
        ;;
esac
