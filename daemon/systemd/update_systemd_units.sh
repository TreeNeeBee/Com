#!/bin/bash
################################################################################
# LightAP Registry - systemd 单元文件更新脚本
#
# 功能: 更新 QM 和 ASIL 的 systemd 配置，使用正确的路径和参数
################################################################################

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="/home/ddk/1_workspace/2_middleware/LightAP/build/modules/Com"

echo "更新 systemd 单元文件..."

# 创建 QM socket 配置
cat > "$SCRIPT_DIR/lap-registry-qm.socket" <<'EOF'
[Unit]
Description=LightAP Service Registry QM Socket
Documentation=file:///usr/share/doc/lap/com/SERVICE_DISCOVERY_ARCHITECTURE.md
PartOf=lap-registry.target
Before=multi-user.target

[Socket]
# Unix Domain Socket 配置 - QM 注册表
ListenStream=/run/lap/registry_qm.sock

# 权限: 所有进程可访问 (QM + ASIL-A/B)
SocketMode=0666
SocketUser=root
SocketGroup=root

# 性能优化
Backlog=128
Accept=false
KeepAlive=true
MaxConnections=1024

# Socket 清理
RemoveOnStop=true

[Install]
WantedBy=sockets.target
EOF

# 创建 QM service 配置
cat > "$SCRIPT_DIR/lap-registry-qm-init.service" <<EOF
[Unit]
Description=LightAP Service Registry QM Initializer
Documentation=file:///usr/share/doc/lap/com/SERVICE_DISCOVERY_ARCHITECTURE.md
Requires=lap-registry-qm.socket
After=lap-registry-qm.socket

[Service]
Type=exec
ExecStart=$BUILD_DIR/lap-registry-init --type=qm --socket=/run/lap/registry_qm.sock

# 标准输入/输出
StandardOutput=journal
StandardError=journal

# 资源限制
MemoryMax=50M
CPUQuota=20%
TasksMax=20

# 运行时目录
RuntimeDirectory=lap
RuntimeDirectoryMode=0755

# 安全加固
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/run/lap

# 超时控制
TimeoutStartSec=10s
TimeoutStopSec=5s

# 重启策略
Restart=on-failure
RestartSec=5s

[Install]
WantedBy=multi-user.target
EOF

# 创建 ASIL socket 配置
cat > "$SCRIPT_DIR/lap-registry-asil.socket" <<'EOF'
[Unit]
Description=LightAP Service Registry ASIL Socket
Documentation=file:///usr/share/doc/lap/com/SERVICE_DISCOVERY_ARCHITECTURE.md
PartOf=lap-registry.target
Before=multi-user.target

[Socket]
# Unix Domain Socket 配置 - ASIL 注册表
ListenStream=/run/lap/registry_asil.sock

# 权限: 受控访问 (ASIL-C/D only)
SocketMode=0640
SocketUser=root
SocketGroup=root

# 性能优化
Backlog=128
Accept=false
KeepAlive=true
MaxConnections=512

# Socket 清理
RemoveOnStop=true

[Install]
WantedBy=sockets.target
EOF

# 创建 ASIL service 配置
cat > "$SCRIPT_DIR/lap-registry-asil-init.service" <<EOF
[Unit]
Description=LightAP Service Registry ASIL Initializer
Documentation=file:///usr/share/doc/lap/com/SERVICE_DISCOVERY_ARCHITECTURE.md
Requires=lap-registry-asil.socket
After=lap-registry-asil.socket

[Service]
Type=exec
ExecStart=$BUILD_DIR/lap-registry-init --type=asil --socket=/run/lap/registry_asil.sock

# 标准输入/输出
StandardOutput=journal
StandardError=journal

# 资源限制 (ASIL 级别更严格)
MemoryMax=50M
CPUQuota=20%
TasksMax=20

# 运行时目录
RuntimeDirectory=lap
RuntimeDirectoryMode=0755

# 安全加固 (ASIL-D 级别)
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ProtectKernelTunables=true
ProtectKernelModules=true
ProtectControlGroups=true
RestrictRealtime=true
ReadWritePaths=/run/lap

# 超时控制
TimeoutStartSec=10s
TimeoutStopSec=5s

# 重启策略
Restart=on-failure
RestartSec=5s

[Install]
WantedBy=multi-user.target
EOF

echo "✓ systemd 单元文件已更新"
echo ""
echo "文件列表:"
echo "  - lap-registry-qm.socket"
echo "  - lap-registry-qm-init.service"
echo "  - lap-registry-asil.socket"
echo "  - lap-registry-asil-init.service"
echo ""
echo "运行测试:"
echo "  sudo ./test_systemd_integration.sh"
