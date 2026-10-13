#!/usr/bin/env bash
#
# collect.sh — 收集 LarkBot 部署所需文件并打包
#
# 用法:
#   ./collect.sh [ENV]        # ENV 预留（LarkBot 配置为静态 system.yml，实际部署时由 CONFIG_DIR 覆盖）
#
# 产物:
#   dist/                         # 部署目录（可检查）
#   larkbot-<时间戳>.tar.gz       # 打包产物
#
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
STAGE_DIR="$ROOT_DIR/dist"
PKG_NAME="$ROOT_DIR/larkbot-$(date +%Y%m%d%H%M%S).tar.gz"

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

check_file "$ROOT_DIR/bin/bot_server"
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

# 可执行文件
cp "$ROOT_DIR/bin/bot_server" "$STAGE_DIR/bot_server"
chmod +x "$STAGE_DIR/bot_server"

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
