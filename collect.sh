#!/usr/bin/env bash
#
# collect.sh — 收集 LarkBot 部署所需文件并打包
#
# 用法:
#   ./collect.sh [ENV]        # ENV 预留（LarkBot 配置为静态 YAML，实际部署时由 CONFIG_DIR 覆盖）
#
# 产物:
#   dist/                         # 部署目录（可检查）
#   larkbot-<时间戳>.tar.gz       # 打包产物
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
STAGE_DIR="$ROOT_DIR/dist"
PKG_NAME="$ROOT_DIR/larkbot-$(date +%Y%m%d%H%M%S).tar.gz"

# 使用 SDK 提供的 server 程序，而不是业务构建的 bot_server
SDK_SERVER="$ROOT_DIR/bin/server"

# 查找 chen-sdk 的 libchen.so
CHEN_SDK_LIB=""
for lib in "$ROOT_DIR"/chen-sdk-*/lib/libchen.so; do
    if [ -f "$lib" ]; then
        CHEN_SDK_LIB="$lib"
        break
    fi
done

check_file() {
    local path="$1"
    if [ ! -e "$path" ]; then
        echo "错误: 缺少文件 $path" >&2
        exit 1
    fi
}

if [ ! -f "$SDK_SERVER" ]; then
    echo "错误: 缺少 $SDK_SERVER，请先执行 make apply 同步 SDK 的 bin/server" >&2
    exit 1
fi
if [ -z "$CHEN_SDK_LIB" ]; then
    echo "错误: 找不到 chen-sdk-*/lib/libchen.so" >&2
    exit 1
fi
check_file "$ROOT_DIR/bin/conf"

shopt -s nullglob
module_sos=("$ROOT_DIR"/bin/module/*.so)
shopt -u nullglob
if [ ${#module_sos[@]} -eq 0 ]; then
    echo "错误: bin/module/ 下没有 .so 模块" >&2
    exit 1
fi

echo "==> 打包 LarkBot"

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/conf" "$STAGE_DIR/module" "$STAGE_DIR/lib" "$STAGE_DIR/logs"

# 可执行文件（SDK 提供的 server）
cp "$SDK_SERVER" "$STAGE_DIR/server"
chmod +x "$STAGE_DIR/server"

# 模块
for so in "${module_sos[@]}"; do
    cp "$so" "$STAGE_DIR/module/"
done

# chen 框架库
cp "$CHEN_SDK_LIB" "$STAGE_DIR/lib/"

# 配置
for f in "$ROOT_DIR"/bin/conf/*; do
    [ -f "$f" ] && cp "$f" "$STAGE_DIR/conf/"
done

# 打包
tar czf "$PKG_NAME" -C "$STAGE_DIR" .

echo "==> 打包完成: $PKG_NAME"
echo "==> 部署目录: $STAGE_DIR"
