#!/bin/bash
# Com模块旧目录清理脚本
# 日期: 2025-11-20
# 用途: 在验证新目录结构正常工作后，清理旧的冗余目录

set -e  # 遇到错误立即退出

COM_SOURCE="/home/ddk/1_workspace/2_middleware/LightAP/modules/Com/source"

cd "$COM_SOURCE"

echo "================================================"
echo "Com模块目录清理脚本"
echo "================================================"
echo ""
echo "⚠️  警告: 此脚本将删除以下旧目录:"
echo "  - comapi/"
echo "  - inc/binding/"
echo "  - inc/registry/"
echo "  - src/binding/"
echo "  - src/registry/"
echo "  - binding/ (旧版)"
echo ""
echo "🔍 检查前置条件..."

# 检查新目录是否存在
if [ ! -d "runtime/inc" ] || [ ! -d "registry/inc" ]; then
    echo "❌ 错误: 新目录结构不完整，请先完成目录重组"
    exit 1
fi

# 检查文件是否已移动
if [ ! -f "registry/inc/SharedMemoryRegistry.hpp" ]; then
    echo "❌ 错误: SharedMemoryRegistry.hpp未移动到registry/inc/"
    exit 1
fi

if [ ! -f "runtime/inc/Runtime.hpp" ]; then
    echo "❌ 错误: Runtime.hpp未移动到runtime/inc/"
    exit 1
fi

echo "✅ 前置条件检查通过"
echo ""

# 交互式确认
echo "📋 待删除目录清单:"
echo ""
echo "1. comapi/            (已合并到runtime/)"
echo "2. inc/binding/       (已移动到binding/*/inc/)"
echo "3. inc/registry/      (已移动到registry/inc/)"
echo "4. src/binding/       (已移动到binding/*/src/)"
echo "5. src/registry/      (已移动到registry/src/)"
echo "6. binding/commonapi/ (旧版，已重组)"
echo "7. binding/dbus/      (旧版，已重组)"
echo "8. binding/socket/    (旧版，已重组)"
echo "9. binding/someip/    (旧版，已重组)"
echo ""
read -p "确认删除? (yes/no): " confirm

if [ "$confirm" != "yes" ]; then
    echo "❌ 用户取消，退出清理脚本"
    exit 0
fi

echo ""
echo "🗑️  开始清理..."
echo ""

# 1. 删除comapi
if [ -d "comapi" ]; then
    echo "  - 删除 comapi/"
    rm -rf comapi
fi

# 2. 删除inc/binding
if [ -d "inc/binding" ]; then
    echo "  - 删除 inc/binding/"
    rm -rf inc/binding
fi

# 3. 删除inc/registry (但保留inc/目录本身，可能还有其他文件)
if [ -d "inc/registry" ]; then
    echo "  - 删除 inc/registry/"
    rm -rf inc/registry
fi

# 4. 删除src/binding
if [ -d "src/binding" ]; then
    echo "  - 删除 src/binding/"
    rm -rf src/binding
fi

# 5. 删除src/registry
if [ -d "src/registry" ]; then
    echo "  - 删除 src/registry/"
    rm -rf src/registry
fi

# 6. 删除binding下的旧版子目录 (保留新结构)
if [ -d "binding/commonapi" ]; then
    echo "  - 删除 binding/commonapi/ (旧版)"
    rm -rf binding/commonapi
fi

echo ""
echo "✅ 清理完成!"
echo ""
echo "📂 保留的目录结构:"
tree -L 2 -d .

echo ""
echo "================================================"
echo "清理完成。请运行以下命令验证编译:"
echo "  cd /home/ddk/1_workspace/2_middleware/LightAP/build"
echo "  cmake .."
echo "  make lap_com"
echo "================================================"
