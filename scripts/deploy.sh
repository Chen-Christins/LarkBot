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

# 可按需覆盖：服务器二进制名 / pid 文件名（实际以上面的 conf/system.yml 为准）
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
export LD_LIBRARY_PATH="$DEPLOY_PATH/lib:${LD_LIBRARY_PATH:-}"

log() { echo "[deploy] $*"; }
die() { echo "[deploy][ERROR] $*" >&2; exit 1; }

# 运行时真实 pid 文件：读 conf/system.yml 的 server.work_path + server.pid_file
pid_file_path() {
    local cfg="$DEPLOY_PATH/conf/system.yml" wp="" pf=""
    if [ -f "$cfg" ]; then
        wp="$(awk '/^[[:space:]]*work_path:/{print $2; exit}' "$cfg")"
        pf="$(awk '/^[[:space:]]*pid_file:/{print $2; exit}' "$cfg")"
        wp="${wp%\"}"; wp="${wp#\"}"; wp="${wp%\'}"; wp="${wp#\'}"
        pf="${pf%\"}"; pf="${pf#\"}"; pf="${pf%\'}"; pf="${pf#\'}"
    fi
    if [ -n "$wp" ] && [ -n "$pf" ]; then
        printf '%s/%s' "${wp%/}" "$pf"
    else
        printf '%s/%s' "$DEPLOY_PATH" "$PID_NAME"
    fi
}

# 用于 -s 命令的 server 二进制（优先 SERVER_BIN，其次同目录下的 server）
server_bin() {
    if   [ -x "$DEPLOY_PATH/$SERVER_BIN" ]; then printf '%s' "$DEPLOY_PATH/$SERVER_BIN"
    elif [ -x "$DEPLOY_PATH/server" ];      then printf '%s' "$DEPLOY_PATH/server"
    else printf '%s' "$DEPLOY_PATH/$SERVER_BIN"; fi
}

is_running() {
    local pf pid
    pf="$(pid_file_path)"
    [ -f "$pf" ] || return 1
    pid="$(cat "$pf" 2>/dev/null || true)"
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
    local server; server="$(server_bin)"
    if is_running; then
        log "停止服务: $server -s stop"
        ( cd "$DEPLOY_PATH" && "$server" -s stop ) || true
        wait_stopped || die "服务未能在超时内停止（pid 文件: $(pid_file_path)）"
        log "服务已停止"
    else
        log "服务未在运行（pid 文件: $(pid_file_path)）"
    fi
}

start_service() {
    local server; server="$(server_bin)"
    log "启动服务: $server -d"
    ( cd "$DEPLOY_PATH" && "$server" -d ) || die "启动命令执行失败"
    local i
    for i in $(seq 1 30); do
        if is_running; then
            log "服务已启动 pid=$(cat "$(pid_file_path)")"
            return 0
        fi
        sleep 1
    done
    die "服务启动后未检测到 pid（pid 文件: $(pid_file_path)）"
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
        # 安全：避免误清空其他服务的运行目录
        if [ -e "$DEPLOY_PATH" ] && [ -n "$(ls -A "$DEPLOY_PATH" 2>/dev/null || true)" ] \
           && [ ! -e "$DEPLOY_PATH/$SERVER_BIN" ] && [ ! -e "$DEPLOY_PATH/server" ] \
           && [ "${FORCE_WIPE:-0}" != "1" ]; then
            die "拒绝清空 $DEPLOY_PATH：目录非空且不含 $SERVER_BIN/server（疑似其他服务目录）。确认无误请设置 FORCE_WIPE=1 重试"
        fi
        stop_service
        log "清空运行目录: $DEPLOY_PATH"
        rm -rf "$DEPLOY_PATH"
        mkdir -p "$DEPLOY_PATH"
        log "解压产物: $ARTIFACT -> $DEPLOY_PATH"
        tar xzf "$ARTIFACT" -C "$DEPLOY_PATH"
        log "覆盖配置: $CONFIG_DIR -> $DEPLOY_PATH/conf"
        rm -rf "$DEPLOY_PATH/conf"
        cp -a "$CONFIG_DIR" "$DEPLOY_PATH/conf"
        chmod +x "$DEPLOY_PATH/$SERVER_BIN" 2>/dev/null || true
        start_service
        ;;
    hot)
        require_artifact
        is_running || die "服务未运行，无法热启动（请改用 cold）。pid 文件: $(pid_file_path)"
        tmp="$(mktemp -d)"
        trap 'rm -rf "$tmp"' EXIT
        log "解压产物到临时目录: $tmp"
        tar xzf "$ARTIFACT" -C "$tmp"
        [ -d "$tmp/module" ] || die "产物内没有 module/ 目录"
        log "热更新模块: $tmp/module/*.so -> $DEPLOY_PATH/module/"
        mkdir -p "$DEPLOY_PATH/module"
        cp -a "$tmp"/module/*.so "$DEPLOY_PATH/module/"
        server="$(server_bin)"
        log "热重载: $server -s reload"
        ( cd "$DEPLOY_PATH" && "$server" -s reload )
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
