# LarkBot

GitHub Webhook → 飞书消息通知机器人。接收 GitHub Webhook 事件，转换为飞书交互式卡片消息推送到飞书群。

## 支持的 GitHub 事件

| 事件 | 说明 |
|------|------|
| `push` | 代码推送，展示 commits 摘要及文件变更 |
| `create` | 分支 / 标签创建 |
| `delete` | 分支 / 标签删除 |
| `pull_request` | PR 打开 / 关闭 / 合并 / 重开 |
| `pull_request_review` | PR Review（通过 / 请求修改 / 评论 / 驳回） |
| `pull_request_review_comment` | PR 行级评论，含文件路径和行号 |
| `workflow_run` | Actions 工作流完成（成功 / 失败 / 取消） |
| `release` | Release 发布 / 预发布 / 草稿 / 删除 |
| `watch` | Star 仓库 |

## 快速开始

### 依赖

- cmake >= 3.22、ninja、g++ (C++20)、python3
- chen-sdk（HTTP 服务框架）与三方依赖（protobuf / yaml-cpp / tinyxml2 / jsoncpp / hiredis-vip）
- 通过 `dependencies.json` 声明，`deps.py` 自动下载（详见「依赖管理」）

### 编译

```bash
make dist            # 一键：同步依赖 → apply SDK → 生成协议头 → 构建 → 打包
# 或分步：
make deps            # 下载 chen-sdk + 三方依赖（架构按本机自动识别）
make apply           # 用 SDK 的 apply.sh 同步 bin/tools/protocol/resources
make proto           # 由 protocol/xml 生成 protocol/include
make -j"$(nproc)"    # 构建
make collect         # 打包到 dist/ 与 larkbot-<时间戳>.tar.gz
```

产物在 `bin/` 目录下：
- `bin/bot_server` — 可执行文件
- `bin/module/libbot.so` — 动态模块

### 配置

编辑 `bin/conf/system.yml`，设置飞书 webhook 地址：

```yaml
feishu:
  webhook_url: "https://open.feishu.cn/open-apis/bot/v2/hook/xxxx-xxxx-xxxx-xxxx"
```

支持飞书签名验证（可选）：

    feishu:
      webhook_url: "..."
      enable_signature: 1
      secret: "your-feishu-secret"

注意：`webhook_url` 和 `secret` 属于敏感信息，请勿提交到代码仓库。

### 运行

```bash
cd bin
./bot_server
```

### 配置 GitHub Webhook

在 GitHub 仓库 Settings → Webhooks 中添加：

- **Payload URL**: `http://<your-server>:8090/api/v1/github_webhook`
- **Content type**: `application/json`
- **Events**: 勾选所需事件

## 依赖管理

通过 `dependencies.json` 声明依赖，`deps.py` 负责下载：

```json
{
  "chen-sdk":   { "repo": "Chen-Christins/chen", "version": "1.5.5", "arch": "auto" },
  "thirdparty": { "repo": "Chen-Christins/chen", "tag": "v1.0.0", "asset": "deps-{arch}.zip" }
}
```

- `chen-sdk`：框架 SDK（`chen-sdk-<version>-<arch>.zip`），解压为根目录 `chen-sdk-<version>/`。
- `thirdparty`：三方依赖包（protobuf / yaml-cpp / tinyxml2 / jsoncpp / hiredis-vip 的头文件 + 静态库），解压到 `deps/`。

| 命令 | 说明 |
|------|------|
| `./deps.py sync` | 下载 chen-sdk + thirdparty（默认全部） |
| `./deps.py sync --sdk-only` | 只下载 chen-sdk |
| `./deps.py thirdparty` | 只下载三方依赖到 `deps/` |
| `./deps.py current` / `list` / `clean` | 查看 / 列远端资产 / 清理 |

架构按主机自动识别（`arch: auto`）：mac(arm64)/Linux(amd64) 各自 `./deps.py sync` 即可。

## CI 与部署

**CI（`.github/workflows/build.yml`）**：在 `ubuntu-24.04`(amd64) 与 `ubuntu-24.04-arm`(arm64) 矩阵上执行 `make` 同款流程，产出 `larkbot-<tag>-<arch>.tar.gz`；tag `v*` 时发布到 GitHub Release。

**部署（`.github/workflows/deploy.yml`）**：手动触发（可选分支或 tag），三种模式：

| 模式 | 行为 |
|------|------|
| `cold` | 停服 → 清空运行目录 → 解压产物 → 用 `CONFIG_DIR` 覆盖 `conf/` → `./bot_server -d` |
| `hot` | 解压产物 → 拷贝 `module/*.so` 到运行目录 → `./bot_server -s reload` |
| `stop` | 执行 `./bot_server -s stop` |

产物来源：tag → Release 资产；分支 → 该分支最近一次成功的 `Build & Package` artifact（`larkbot-amd64`）。

服务器端逻辑在 `scripts/deploy.sh`。**Secrets**：`SSH_HOST`/`SSH_USER`/`SSH_PORT`/`SSH_PRIVATE_KEY`；**Variables**：`DEPLOY_PATH`、`CONFIG_DIR`（内含权威 `system.yml`，其 `server.work_path` 必须指向 `DEPLOY_PATH`）。

## 项目结构

```
src/
├── bot.cc                      # 入口
├── BotModule.h / .cc           # 模块生命周期，Servlet 注册
├── Struct.hpp / .cc            # Result / LarkBotServlet / feishuSign
├── servlets/
│   └── GithubWebHook.hpp / .cc # GitHub Webhook 处理
└── protocol/
    ├── Protocol.hpp / .cc           # 协议基类
    ├── LarkCardProtocol.hpp / .cc   # 飞书交互式卡片消息
    ├── RichTextProtocol.hpp / .cc   # 飞书富文本消息
    └── TextProtocol.hpp / .cc       # 飞书纯文本消息
```

## License

Licensed under the [Apache License 2.0](LICENSE).
