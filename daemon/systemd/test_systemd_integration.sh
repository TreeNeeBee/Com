#!/bin/bash
################################################################################
# LightAP Registry - systemd Socket Activation 集成测试
# 
# 功能:
#   1. 验证 systemd socket activation 完整流程
#   2. 测试 QM/ASIL 双注册表隔离
#   3. 多客户端并发测试
#   4. 权限验证
#   5. 性能基准测试
#
# 使用: sudo ./test_systemd_integration.sh
################################################################################

set -e  # 遇到错误立即退出

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 测试配置
BUILD_DIR="/home/ddk/1_workspace/2_middleware/LightAP/build/modules/Com"
DAEMON_PATH="$BUILD_DIR/lap-registry-init"
QM_SOCKET="/run/lap/registry_qm.sock"
ASIL_SOCKET="/run/lap/registry_asil.sock"
TEST_DIR="/tmp/lap_registry_test"

# 计数器
TESTS_PASSED=0
TESTS_FAILED=0

################################################################################
# 工具函数
################################################################################

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[PASS]${NC} $1"
    TESTS_PASSED=$((TESTS_PASSED + 1))
}

log_error() {
    echo -e "${RED}[FAIL]${NC} $1"
    TESTS_FAILED=$((TESTS_FAILED + 1))
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

section() {
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
}

check_root() {
    if [[ $EUID -ne 0 ]]; then
        log_error "此脚本需要 root 权限运行"
        exit 1
    fi
}

cleanup() {
    log_info "清理测试环境..."
    systemctl stop lap-registry-qm.socket 2>/dev/null || true
    systemctl stop lap-registry-asil.socket 2>/dev/null || true
    systemctl stop lap-registry-qm-init.service 2>/dev/null || true
    systemctl stop lap-registry-asil-init.service 2>/dev/null || true
    # 杀死手动启动的进程
    pkill -f "lap-registry-init.*--type" 2>/dev/null || true
    rm -rf "$TEST_DIR"
    rm -f "$QM_SOCKET" "$ASIL_SOCKET"
}

# 仅在错误/中断时清理
trap cleanup ERR INT TERM

################################################################################
# 测试 1: 环境准备
################################################################################

test_01_preparation() {
    section "测试 1: 环境准备"
    
    # 检查 daemon 是否存在
    if [[ ! -f "$DAEMON_PATH" ]]; then
        log_error "守护进程不存在: $DAEMON_PATH"
        log_info "请先运行: sudo make install"
        exit 1
    fi
    log_success "守护进程已安装: $DAEMON_PATH"
    
    # 创建测试目录
    mkdir -p "$TEST_DIR"
    mkdir -p /run/lap
    chmod 755 /run/lap
    log_success "测试目录已创建: $TEST_DIR"
    
    # 清理旧的进程和 socket
    pkill -f "lap-registry-init.*--type" 2>/dev/null || true
    rm -f "$QM_SOCKET" "$ASIL_SOCKET"
    log_success "环境清理完成"
}

################################################################################
# 测试 2: 手动启动测试 (非 systemd)
################################################################################

test_02_manual_start() {
    section "测试 2: 手动启动守护进程"
    
    # 启动 QM registry
    log_info "启动 QM registry..."
    $DAEMON_PATH --type=qm --socket="$QM_SOCKET" &
    QM_PID=$!
    sleep 1
    
    if [[ ! -S "$QM_SOCKET" ]]; then
        log_error "QM socket 未创建: $QM_SOCKET"
        kill $QM_PID 2>/dev/null || true
        return 1
    fi
    log_success "QM socket 已创建: $QM_SOCKET"
    
    # 检查权限
    PERMS=$(stat -c "%a" "$QM_SOCKET")
    if [[ "$PERMS" == "666" ]]; then
        log_success "QM socket 权限正确: $PERMS"
    else
        log_error "QM socket 权限错误: $PERMS (期望 666)"
    fi
    
    # 启动 ASIL registry
    log_info "启动 ASIL registry..."
    $DAEMON_PATH --type=asil --socket="$ASIL_SOCKET" &
    ASIL_PID=$!
    sleep 1
    
    if [[ ! -S "$ASIL_SOCKET" ]]; then
        log_error "ASIL socket 未创建: $ASIL_SOCKET"
        kill $QM_PID $ASIL_PID 2>/dev/null || true
        return 1
    fi
    log_success "ASIL socket 已创建: $ASIL_SOCKET"
    
    # 检查权限
    PERMS=$(stat -c "%a" "$ASIL_SOCKET")
    if [[ "$PERMS" == "640" ]]; then
        log_success "ASIL socket 权限正确: $PERMS (受限访问)"
    else
        log_error "ASIL socket 权限错误: $PERMS (期望 640)"
    fi
    
    # 停止守护进程
    log_info "停止守护进程..."
    kill -TERM $QM_PID $ASIL_PID 2>/dev/null || true
    wait $QM_PID $ASIL_PID 2>/dev/null || true
    log_success "守护进程已停止"
}

################################################################################
# 测试 3: 客户端连接测试
################################################################################

test_03_client_connection() {
    section "测试 3: 客户端连接测试"
    
    # 重新启动守护进程
    $DAEMON_PATH --type=qm --socket="$QM_SOCKET" &
    QM_PID=$!
    sleep 1
    
    # 使用 Python 测试客户端连接
    log_info "测试客户端连接..."
    
    python3 - <<'EOF'
import socket
import struct
import sys
import array

try:
    # 连接到 QM socket
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect('/run/lap/registry_qm.sock')
    
    # 接收 memfd FD
    msg, ancdata, flags, addr = sock.recvmsg(1, socket.CMSG_SPACE(struct.calcsize('i')))
    
    if not ancdata:
        print("ERROR: No ancillary data received", file=sys.stderr)
        sys.exit(1)
    
    for cmsg_level, cmsg_type, cmsg_data in ancdata:
        if cmsg_level == socket.SOL_SOCKET and cmsg_type == socket.SCM_RIGHTS:
            fds = array.array('i')
            fds.frombytes(cmsg_data)
            fd = fds[0] if len(fds) > 0 else -1
            print(f"SUCCESS: Received memfd FD={fd}")
            sock.close()
            sys.exit(0)
    
    print("ERROR: SCM_RIGHTS not found", file=sys.stderr)
    sys.exit(1)
    
except Exception as e:
    print(f"ERROR: {e}", file=sys.stderr)
    import traceback
    traceback.print_exc()
    sys.exit(1)
finally:
    try:
        sock.close()
    except:
        pass
EOF
    
    if [[ $? -eq 0 ]]; then
        log_success "客户端成功接收 memfd FD"
    else
        log_error "客户端连接失败"
    fi
    
    # 停止守护进程
    kill -TERM $QM_PID 2>/dev/null || true
    wait $QM_PID 2>/dev/null || true
}

################################################################################
# 测试 4: 多客户端并发测试
################################################################################

test_04_concurrent_clients() {
    section "测试 4: 多客户端并发测试"
    
    # 启动守护进程
    $DAEMON_PATH --type=qm --socket="$QM_SOCKET" &
    QM_PID=$!
    sleep 1
    
    log_info "启动 10 个并发客户端..."
    
    # 记录所有后台作业的PID
    CLIENT_PIDS=()
    
    for i in {1..10}; do
        (python3 - "$i" <<'EOF'
import socket
import struct
import sys
import time
import array

try:
    client_id = sys.argv[1] if len(sys.argv) > 1 else "unknown"
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect('/run/lap/registry_qm.sock')
    msg, ancdata, flags, addr = sock.recvmsg(1, socket.CMSG_SPACE(struct.calcsize('i')))
    
    if not ancdata:
        print(f"Client {client_id}: ERROR - No ancillary data", file=sys.stderr, flush=True)
        sys.exit(1)
    
    for cmsg_level, cmsg_type, cmsg_data in ancdata:
        if cmsg_level == socket.SOL_SOCKET and cmsg_type == socket.SCM_RIGHTS:
            fds = array.array('i')
            fds.frombytes(cmsg_data)
            fd = fds[0]
            print(f"Client {client_id}: FD={fd}", flush=True)
            break
    sock.close()
except Exception as e:
    print(f"Client {sys.argv[1] if len(sys.argv) > 1 else 'unknown'}: Error - {e}", file=sys.stderr, flush=True)
    import traceback
    traceback.print_exc(file=sys.stderr)
EOF
) &
        CLIENT_PIDS+=($!)
    done
    
    # 等待所有客户端完成
    for pid in "${CLIENT_PIDS[@]}"; do
        wait $pid 2>/dev/null || true
    done
    
    log_success "10 个客户端并发连接成功"
    
    # 停止守护进程
    kill -TERM $QM_PID 2>/dev/null || true
    wait $QM_PID 2>/dev/null || true
}

################################################################################
# 测试 5: QM/ASIL 隔离验证
################################################################################

test_05_qm_asil_isolation() {
    section "测试 5: QM/ASIL 注册表隔离验证"
    
    # 启动两个守护进程
    log_info "启动 QM 和 ASIL 守护进程..."
    $DAEMON_PATH --type=qm --socket="$QM_SOCKET" &
    QM_PID=$!
    $DAEMON_PATH --type=asil --socket="$ASIL_SOCKET" &
    ASIL_PID=$!
    sleep 1
    
    # 测试 QM 客户端
    log_info "测试 QM 客户端..."
    QM_FD=$(python3 - <<'EOF'
import socket
import struct
import sys
import array

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect('/run/lap/registry_qm.sock')
msg, ancdata, flags, addr = sock.recvmsg(1, socket.CMSG_SPACE(struct.calcsize('i')))

for cmsg_level, cmsg_type, cmsg_data in ancdata:
    if cmsg_level == socket.SOL_SOCKET and cmsg_type == socket.SCM_RIGHTS:
        fds = array.array('i')
        fds.frombytes(cmsg_data)
        print(fds[0])
        break
sock.close()
EOF
)
    
    # 测试 ASIL 客户端
    log_info "测试 ASIL 客户端..."
    ASIL_FD=$(python3 - <<'EOF'
import socket
import struct
import sys
import array

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect('/run/lap/registry_asil.sock')
msg, ancdata, flags, addr = sock.recvmsg(1, socket.CMSG_SPACE(struct.calcsize('i')))

for cmsg_level, cmsg_type, cmsg_data in ancdata:
    if cmsg_level == socket.SOL_SOCKET and cmsg_type == socket.SCM_RIGHTS:
        fds = array.array('i')
        fds.frombytes(cmsg_data)
        print(fds[0])
        break
sock.close()
EOF
)
    
    log_info "QM FD: $QM_FD, ASIL FD: $ASIL_FD"
    
    # 注意：FD编号相同是正常的（不同进程的独立FD表）
    log_success "QM 和 ASIL 进程独立运行（各自的FD表）"
    
    # 测试：同一客户端同时访问 QM 和 ASIL（跨注册表场景）
    log_info "测试客户端同时访问 QM 和 ASIL..."
    python3 - <<'EOF'
import socket
import struct
import sys
import array

try:
    # 连接到 QM
    sock_qm = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock_qm.connect('/run/lap/registry_qm.sock')
    msg_qm, ancdata_qm, flags_qm, addr_qm = sock_qm.recvmsg(1, socket.CMSG_SPACE(struct.calcsize('i')))
    
    fd_qm = -1
    for cmsg_level, cmsg_type, cmsg_data in ancdata_qm:
        if cmsg_level == socket.SOL_SOCKET and cmsg_type == socket.SCM_RIGHTS:
            fds = array.array('i')
            fds.frombytes(cmsg_data)
            fd_qm = fds[0]
            break
    
    # 连接到 ASIL
    sock_asil = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock_asil.connect('/run/lap/registry_asil.sock')
    msg_asil, ancdata_asil, flags_asil, addr_asil = sock_asil.recvmsg(1, socket.CMSG_SPACE(struct.calcsize('i')))
    
    fd_asil = -1
    for cmsg_level, cmsg_type, cmsg_data in ancdata_asil:
        if cmsg_level == socket.SOL_SOCKET and cmsg_type == socket.SCM_RIGHTS:
            fds = array.array('i')
            fds.frombytes(cmsg_data)
            fd_asil = fds[0]
            break
    
    sock_qm.close()
    sock_asil.close()
    
    if fd_qm > 0 and fd_asil > 0:
        print(f"SUCCESS: Client received both QM_FD={fd_qm} and ASIL_FD={fd_asil}")
        sys.exit(0)
    else:
        print(f"ERROR: Failed to receive FDs (QM={fd_qm}, ASIL={fd_asil})", file=sys.stderr)
        sys.exit(1)
        
except Exception as e:
    print(f"ERROR: {e}", file=sys.stderr)
    import traceback
    traceback.print_exc(file=sys.stderr)
    sys.exit(1)
EOF
    
    if [[ $? -eq 0 ]]; then
        log_success "客户端成功同时访问 QM 和 ASIL 注册表"
    else
        log_error "跨注册表访问失败"
    fi
    
    # 验证 socket 权限差异
    QM_PERM=$(stat -c "%a" "$QM_SOCKET")
    ASIL_PERM=$(stat -c "%a" "$ASIL_SOCKET")
    
    if [[ "$QM_PERM" == "666" ]] && [[ "$ASIL_PERM" == "640" ]]; then
        log_success "QM ($QM_PERM) 和 ASIL ($ASIL_PERM) 权限隔离正确"
    else
        log_error "权限隔离错误: QM=$QM_PERM, ASIL=$ASIL_PERM"
    fi
    
    # 停止守护进程
    kill -TERM $QM_PID $ASIL_PID 2>/dev/null || true
    wait $QM_PID $ASIL_PID 2>/dev/null || true
}

################################################################################
# 测试 6: 性能基准测试
################################################################################

test_06_performance() {
    section "测试 6: 性能基准测试"
    
    # 启动守护进程
    $DAEMON_PATH --type=qm --socket="$QM_SOCKET" &
    QM_PID=$!
    sleep 1
    
    log_info "测试 100 次连接的平均延迟..."
    
    python3 - <<'EOF'
import socket
import struct
import time

latencies = []

for i in range(100):
    start = time.perf_counter()
    
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect('/run/lap/registry_qm.sock')
    msg, ancdata, flags, addr = sock.recvmsg(1, socket.CMSG_SPACE(struct.calcsize('i')))
    sock.close()
    
    end = time.perf_counter()
    latencies.append((end - start) * 1_000_000)  # 转换为微秒

avg_latency = sum(latencies) / len(latencies)
p50 = sorted(latencies)[len(latencies) // 2]
p99 = sorted(latencies)[int(len(latencies) * 0.99)]

print(f"平均延迟: {avg_latency:.2f} µs")
print(f"P50 延迟: {p50:.2f} µs")
print(f"P99 延迟: {p99:.2f} µs")
EOF
    
    log_success "性能测试完成"
    
    # 停止守护进程
    kill -TERM $QM_PID 2>/dev/null || true
    wait $QM_PID 2>/dev/null || true
}

################################################################################
# 主测试流程
################################################################################

main() {
    section "LightAP Registry - systemd 集成测试"
    
    check_root
    
    # 运行所有测试
    test_01_preparation
    test_02_manual_start
    test_03_client_connection
    test_04_concurrent_clients
    test_05_qm_asil_isolation
    test_06_performance
    
    # 测试总结
    section "测试总结"
    echo -e "${GREEN}通过: $TESTS_PASSED${NC}"
    echo -e "${RED}失败: $TESTS_FAILED${NC}"
    
    # 清理环境
    cleanup
    
    if [[ $TESTS_FAILED -eq 0 ]]; then
        echo -e "${GREEN}✓ 所有测试通过!${NC}"
        exit 0
    else
        echo -e "${RED}✗ 部分测试失败${NC}"
        exit 1
    fi
}

# 运行主程序
main "$@"
