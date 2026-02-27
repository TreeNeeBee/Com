#!/bin/bash
# ==============================================================================
# LightAP Registry - Installation Script
# ==============================================================================
# Installs the registry daemon binary and systemd units.
#
# Usage:
#   sudo ./install.sh                 # Full install
#   sudo ./install.sh --uninstall     # Remove all
#   sudo ./install.sh --status        # Show status
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REGISTRY_DIR="$( cd "$SCRIPT_DIR/.." && pwd )"
COM_BUILD_DIR="$( cd "$REGISTRY_DIR/../build" && pwd 2>/dev/null || echo "" )"

BIN_SRC="$COM_BUILD_DIR/registry/lap-registry-init"
BIN_DST="/usr/local/bin/lap-registry-init"
SYSTEMD_DIR="/etc/systemd/system"
RUNTIME_DIR="/run/lap"
DS_CONFIG_DIR="/etc/lap/discovery"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

ok()   { echo -e "  ${GREEN}✓${NC} $1"; }
warn() { echo -e "  ${YELLOW}!${NC} $1"; }
err()  { echo -e "  ${RED}✗${NC} $1"; }

# ==============================================================================
# Uninstall
# ==============================================================================
do_uninstall() {
    echo "Uninstalling LightAP Registry..."

    # Stop services (including Discovery Server)
    systemctl stop lap-discovery-monitor.service 2>/dev/null || true
    systemctl stop lap-discovery-server.service 2>/dev/null || true
    systemctl stop lap-registry-qm-init.service 2>/dev/null || true
    systemctl stop lap-registry-asil-init.service 2>/dev/null || true
    systemctl stop lap-registry-qm.socket 2>/dev/null || true
    systemctl stop lap-registry-asil.socket 2>/dev/null || true

    # Disable
    systemctl disable lap-discovery-monitor.service 2>/dev/null || true
    systemctl disable lap-discovery-server.service 2>/dev/null || true
    systemctl disable lap-registry-qm.socket 2>/dev/null || true
    systemctl disable lap-registry-asil.socket 2>/dev/null || true

    # Remove units
    rm -f "$SYSTEMD_DIR/lap-registry-qm.socket"
    rm -f "$SYSTEMD_DIR/lap-registry-qm-init.service"
    rm -f "$SYSTEMD_DIR/lap-registry-asil.socket"
    rm -f "$SYSTEMD_DIR/lap-registry-asil-init.service"
    rm -f "$SYSTEMD_DIR/lap-discovery-server.service"
    rm -f "$SYSTEMD_DIR/lap-discovery-monitor.service"

    # Reload
    systemctl daemon-reload 2>/dev/null || true

    # Remove binary
    rm -f "$BIN_DST"

    # Remove Discovery Server configs
    rm -rf "$DS_CONFIG_DIR"

    # Remove runtime dir
    rm -rf "$RUNTIME_DIR"

    ok "Uninstall complete"
}

# ==============================================================================
# Status
# ==============================================================================
do_status() {
    echo "LightAP Registry Status"
    echo "========================"

    echo ""
    echo "Binary:"
    if [[ -x "$BIN_DST" ]]; then
        ok "$BIN_DST (installed)"
    else
        err "$BIN_DST (not found)"
    fi

    echo ""
    echo "Runtime:"
    if [[ -d "$RUNTIME_DIR" ]]; then
        ok "$RUNTIME_DIR exists"
        ls -la "$RUNTIME_DIR/" 2>/dev/null || true
    else
        warn "$RUNTIME_DIR does not exist"
    fi

    echo ""
    echo "Discovery Server:"
    if [[ -d "$DS_CONFIG_DIR" ]]; then
        ok "$DS_CONFIG_DIR exists"
        for f in fastdds_ds_server.xml fastdds_ds_client.xml fastdds_ds_fallback.xml \
                 fastdds_ds_env.conf lap-discovery-monitor.sh; do
            if [[ -f "$DS_CONFIG_DIR/$f" ]]; then
                ok "  $f"
            else
                err "  $f (missing)"
            fi
        done
    else
        warn "$DS_CONFIG_DIR does not exist"
    fi

    # Check active discovery mode
    if [[ -f "/run/lap/discovery_state" ]]; then
        local mode
        mode=$(cat /run/lap/discovery_state)
        echo ""
        echo "Active Discovery Mode: $mode"
    fi

    echo ""
    echo "systemd units:"
    for unit in lap-discovery-server.service lap-discovery-monitor.service \
                lap-registry-qm.socket lap-registry-qm-init.service \
                lap-registry-asil.socket lap-registry-asil-init.service; do
        if [[ -f "$SYSTEMD_DIR/$unit" ]]; then
            state=$( systemctl is-active "$unit" 2>/dev/null || echo "inactive" )
            ok "$unit  [$state]"
        else
            err "$unit  (not installed)"
        fi
    done
}

# ==============================================================================
# Install
# ==============================================================================
do_install() {
    echo "======================================"
    echo " LightAP Registry - Install"
    echo "======================================"
    echo ""

    # Check root
    if [[ $EUID -ne 0 ]]; then
        err "Please run as root: sudo $0"
        exit 1
    fi

    # Step 1: Check binary
    echo "[1/4] Checking binary..."
    if [[ ! -f "$BIN_SRC" ]]; then
        err "Binary not found: $BIN_SRC"
        err "Build first:  cd $COM_BUILD_DIR && cmake .. && make -j\$(nproc)"
        exit 1
    fi
    ok "Binary found: $BIN_SRC"

    # Step 2: Install binary
    echo "[2/4] Installing binary..."
    cp -f "$BIN_SRC" "$BIN_DST"
    chmod 755 "$BIN_DST"
    ok "Installed: $BIN_DST"

    # Step 3: Create runtime directory
    echo "[3/6] Creating runtime directory..."
    mkdir -p "$RUNTIME_DIR"
    chmod 755 "$RUNTIME_DIR"
    ok "Created: $RUNTIME_DIR"

    # Step 4: Install Discovery Server configuration
    echo "[4/6] Installing Discovery Server configuration..."
    mkdir -p "$DS_CONFIG_DIR"

    for f in fastdds_ds_server.xml fastdds_ds_client.xml fastdds_ds_fallback.xml \
             fastdds_ds_env.conf; do
        if [[ -f "$SCRIPT_DIR/$f" ]]; then
            cp -f "$SCRIPT_DIR/$f" "$DS_CONFIG_DIR/"
            chmod 644 "$DS_CONFIG_DIR/$f"
            ok "  $f"
        else
            warn "  $f not found in source"
        fi
    done

    # Install monitor script (executable)
    if [[ -f "$SCRIPT_DIR/lap-discovery-monitor.sh" ]]; then
        cp -f "$SCRIPT_DIR/lap-discovery-monitor.sh" "$DS_CONFIG_DIR/"
        chmod 755 "$DS_CONFIG_DIR/lap-discovery-monitor.sh"
        ok "  lap-discovery-monitor.sh (executable)"
    fi

    # Create initial active-profile symlink → CLIENT mode (default)
    ln -sf "$DS_CONFIG_DIR/fastdds_ds_client.xml" "$RUNTIME_DIR/fastdds_active_profile.xml"
    echo "CLIENT" > "$RUNTIME_DIR/discovery_state"
    ok "Default discovery mode: CLIENT"

    # Step 5: Install systemd units (if systemd is PID 1)
    echo "[5/6] Installing systemd units..."
    if pidof systemd > /dev/null 2>&1; then
        cp -f "$SCRIPT_DIR/lap-registry-qm.socket"         "$SYSTEMD_DIR/"
        cp -f "$SCRIPT_DIR/lap-registry-qm-init.service"   "$SYSTEMD_DIR/"
        cp -f "$SCRIPT_DIR/lap-registry-asil.socket"        "$SYSTEMD_DIR/"
        cp -f "$SCRIPT_DIR/lap-registry-asil-init.service"  "$SYSTEMD_DIR/"
        cp -f "$SCRIPT_DIR/lap-discovery-server.service"    "$SYSTEMD_DIR/"
        cp -f "$SCRIPT_DIR/lap-discovery-monitor.service"   "$SYSTEMD_DIR/"
        chmod 644 "$SYSTEMD_DIR"/lap-registry-*.socket "$SYSTEMD_DIR"/lap-registry-*-init.service
        chmod 644 "$SYSTEMD_DIR"/lap-discovery-*.service

        systemctl daemon-reload

        # Enable Discovery Server and monitor
        systemctl enable lap-discovery-server.service
        systemctl enable lap-discovery-monitor.service
        systemctl start  lap-discovery-server.service
        systemctl start  lap-discovery-monitor.service

        # Enable registry sockets
        systemctl enable lap-registry-qm.socket
        systemctl enable lap-registry-asil.socket
        systemctl start  lap-registry-qm.socket
        systemctl start  lap-registry-asil.socket

        ok "systemd units installed and started"
    else
        warn "systemd not running as PID 1 (container mode)"
        warn "Units installed to $SCRIPT_DIR/ for reference"
        warn "Manual mode:"
        warn "  fastdds discovery -i 0 -l 127.0.0.1 -p 11811 -x $DS_CONFIG_DIR/fastdds_ds_server.xml &"
        warn "  $DS_CONFIG_DIR/lap-discovery-monitor.sh --daemon"
        warn "  lap-registry-init --type=qm --socket=/run/lap/registry_qm.sock"
    fi

    # Step 6: Verify Discovery Server
    echo "[6/6] Verifying FastDDS Discovery Server..."
    if command -v fastdds > /dev/null 2>&1; then
        ok "fastdds binary found: $(which fastdds)"
    else
        warn "fastdds binary not found in PATH"
    fi

    echo ""
    echo "======================================"
    echo " Install complete!"
    echo "======================================"
}

# ==============================================================================
# Main
# ==============================================================================
case "${1:-}" in
    --uninstall) do_uninstall ;;
    --status)    do_status ;;
    *)           do_install ;;
esac
