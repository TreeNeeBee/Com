#!/bin/bash
# LightAP Service Registry - systemd Installation Script
# 用途: 安装 systemd socket activation 单元文件

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SYSTEMD_DIR="/etc/systemd/system"
RUNTIME_DIR="/run/lap"
BIN_DIR="/usr/lib/lap/com"

echo "==================================================================="
echo "LightAP Service Registry - systemd Socket Activation 安装程序"
echo "==================================================================="
echo ""

# 检查 root 权限
if [ "$EUID" -ne 0 ]; then
    echo "错误: 请使用 root 权限运行此脚本"
    echo "用法: sudo $0"
    exit 1
fi

# 创建运行时目录
echo "[1/5] 创建运行时目录..."
mkdir -p "$RUNTIME_DIR"
chmod 755 "$RUNTIME_DIR"
echo "      ✓ $RUNTIME_DIR (权限: 755)"

# 创建二进制目录
echo "[2/5] 创建二进制目录..."
mkdir -p "$BIN_DIR"
chmod 755 "$BIN_DIR"
echo "      ✓ $BIN_DIR (权限: 755)"

# 安装 systemd 单元文件
echo "[3/5] 安装 systemd 单元文件..."
cp -f "$SCRIPT_DIR/lap-registry.socket" "$SYSTEMD_DIR/"
cp -f "$SCRIPT_DIR/lap-registry-init.service" "$SYSTEMD_DIR/"
chmod 644 "$SYSTEMD_DIR/lap-registry.socket"
chmod 644 "$SYSTEMD_DIR/lap-registry-init.service"
echo "      ✓ lap-registry.socket -> $SYSTEMD_DIR/"
echo "      ✓ lap-registry-init.service -> $SYSTEMD_DIR/"

# 重载 systemd 配置
echo "[4/5] 重载 systemd 配置..."
systemctl daemon-reload
echo "      ✓ systemd daemon-reload 完成"

# 启用并启动 socket
echo "[5/5] 启用并启动 lap-registry.socket..."
systemctl enable lap-registry.socket
systemctl start lap-registry.socket
echo "      ✓ lap-registry.socket 已启用并启动"

echo ""
echo "==================================================================="
echo "安装完成！"
echo "==================================================================="
echo ""
echo "验证安装:"
echo "  sudo systemctl status lap-registry.socket"
echo ""
echo "查看日志:"
echo "  sudo journalctl -u lap-registry.socket -u lap-registry-init.service -f"
echo ""
echo "Socket 路径:"
echo "  /run/lap/registry.sock"
echo ""
echo "注意事项:"
echo "  1. lap-registry-init 二进制文件需编译并放置到 $BIN_DIR/lap-registry-init"
echo "  2. 首次客户端连接时，systemd 会自动启动 lap-registry-init.service"
echo "  3. lap-registry-init.service 是 oneshot 类型，完成初始化后立即退出"
echo "  4. 无任何常驻守护进程，零内存占用"
echo ""
