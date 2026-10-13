#!/usr/bin/env bash
#
# deploy.sh — 在部署服务器上执行 LarkBot 的 冷启动 / 热启动 / 停服
#
# 用法:
#   DEPLOY_PATH=/home/web/apps/larkbot CONFIG_DIR=/home/web/apps/larkbot_conf \
#     ./deploy.sh --mode cold|hot|stop --artifact /tmp/larkbot-v1.2.3-amd64.tar.gz
#
#   cold : 停服 → 清空运行目录 → 解压产物 → 用 CONFIG_DIR 覆盖 conf/ → ./bot_server -d 启动
#   hot  : 解压产物到临时目录 → 拷贝 module/*.so 到运行目录 → ./bot_server -s reload
#   stop : 仅执行 ./bot_server -s stop
#
set -euo pipefail

MODE=""
ARTIFACT=""

# 可按需覆盖：服务器二进制名 / pid 文件名
SERVER_BIN="${SERVER_BIN:-bot_server}"
PID_NAME="${PID_NAME:-bot.pid}"

usage() { sed -n '2,13p' "$0"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --mode)     MODE="${2:-}"; shift 2 ;;
        --mode=*)   MODE="${1#*=}"; shift ;;
        --artifact) ARTIFACT="${2:-}"; shift 2 ;;
        --artifact=*) ARTIFACT="${1#*=}"; shift ;;
        -h|--help)  usage; exit 0 ;;
        *) echo "未知参数: $1" >&2; usage >&2; exit 2 ;;
    esac
done

: "${DEPLOY_PATH:?需要设置环境变量 DEPLOY_PATH（运行目录）}"
PID_FILE="$DEPLOY_PATH/$PID_NAME"
SERVER="$DEPLOY_PATH/$SERVER_BIN"
export LD_LIBRARY_PATH="$DEPLOY_PATH/lib:${LD_LIBRARY_PATH:-}"

log() { echo "[deploy] $*"; }
die() { echo "[deploy][ERROR] $*" >&2; exit 1; }

is_running() {
    [ -f "$PID_FILE" ] || return 1
    local pid
    pid="$(cat "$PID_FILE" 2>/dev/null || true)"
    [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null
}

wait_stopped() {
    local i
    for i in $(seq 1 30); do
        is_running || return 0
        sleep 1
    done
    return 1
}

stop_service() {
    if is_running; then
        log "停止服务: $SERVER -s stop"
        ( cd "$DEPLOY_PATH" && "$SERVER" -s stop ) || true
        wait_stopped || die "服务未能在超时内停止（pid=$(cat "$PID_FILE" 2>/dev/null)）"
        log "服务已停止"
    else
        log "服务未在运行"
    fi
}

start_service() {
    log "启动服务: $SERVER -d"
    ( cd "$DEPLOY_PATH" && "$SERVER" -d )
    local i
    for i in $(seq 1 30); do
        if is_running; then
            log "服务已启动 pid=$(cat "$PID_FILE")"
            return 0
        fi
        sleep 1
    done
    die "服务启动后未检测到 pid 文件存活: $PID_FILE"
}

require_artifact() {
    [ -n "$ARTIFACT" ] || die "--mode ${MODE} 需要 --artifact <tar.gz>"
    [ -f "$ARTIFACT" ] || die "产物不存在: $ARTIFACT"
}

case "$MODE" in
    cold)
        require_artifact
        [ -n "${CONFIG_DIR:-}" ] || die "cold 需要设置环境变量 CONFIG_DIR（指定配置目录）"
        [ -d "$CONFIG_DIR" ] || die "配置目录不存在: $CONFIG_DIR"
        stop_service
        log "清空运行目录: $DEPLOY_PATH"
        rm -rf "$DEPLOY_PATH"
        mkdir -p "$DEPLOY_PATH"
        log "解压产物: $ARTIFACT -> $DEPLOY_PATH"
        tar xzf "$ARTIFACT" -C "$DEPLOY_PATH"
        log "覆盖配置: $CONFIG_DIR -> $DEPLOY_PATH/conf"
        rm -rf "$DEPLOY_PATH/conf"
        cp -a "$CONFIG_DIR" "$DEPLOY_PATH/conf"
        chmod +x "$DEPLOY_PATH/$SERVER_BIN" || true
        start_service
        ;;
    hot)
        require_artifact
        is_running || die "服务未运行，无法热启动（请改用 cold）"
        tmp="$(mktemp -d)"
        trap 'rm -rf "$tmp"' EXIT
        log "解压产物到临时目录: $tmp"
        tar xzf "$ARTIFACT" -C "$tmp"
        [ -d "$tmp/module" ] || die "产物内没有 module/ 目录"
        log "热更新模块: $tmp/module/*.so -> $DEPLOY_PATH/module/"
        mkdir -p "$DEPLOY_PATH/module"
        cp -a "$tmp"/module/*.so "$DEPLOY_PATH/module/"
        log "热重载: $SERVER -s reload"
        ( cd "$DEPLOY_PATH" && "$SERVER" -s reload )
        sleep 1
        is_running || die "热重载后服务未存活"
        log "热启动完成"
        ;;
    stop)
        stop_service
        ;;
    *)
        die "未知 --mode: '${MODE}'（应为 cold|hot|stop）"
        ;;
esac
