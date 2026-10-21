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

- `bin/server` — SDK 提供的框架启动程序
- `bin/module/libbot.so` — 动态模块

### 配置

编辑 `bin/conf/LarkBot.yml`。飞书支持配置多个机器人，业务按 `name` 取对应的 webhook 发送：

```yaml
feishu:
  github_bot: "default"      # GitHub Webhook 通知固定使用这个机器人
  default_bot: "default"     # 未指定名字时使用的默认机器人

  bots:
    - name: "default"
      webhook_url: "https://open.feishu.cn/open-apis/bot/v2/hook/xxxx"
      enable_signature: true
      secret: "your-feishu-secret"
      enabled: true

    - name: "daily"          # 定时推送
      webhook_url: "https://open.feishu.cn/open-apis/bot/v2/hook/yyyy"
      enable_signature: true
      secret: "your-daily-secret"

    - name: "alert"          # 告警
      webhook_url: "https://open.feishu.cn/open-apis/bot/v2/hook/zzzz"
      enable_signature: false
```

业务代码按名字发送：

```cpp
auto mgr = bot::FeishuBotMgr::GetInstance();
mgr->sendText("daily", "今日构建全部通过");
mgr->sendCard("alert", cardJson);
```

GitHub Webhook 地址保持不变，仍然发到 `feishu.github_bot` 指定的机器人。

修改 `LarkBot.yml` 后通过框架的配置变更回调自动生效，无需重启。

注意：`webhook_url` 和 `secret` 属于敏感信息，请勿提交到代码仓库。

### 运行

```bash
cd bin
./server -d      # 后台运行
# 或 ./server -s # 前台运行
```

### 定时点餐提醒

`meal_reminder` 每天（跳过 `skip_weekdays` 中的星期）在指定时间发送点餐提醒卡片：

```yaml
meal_reminder:
  enabled: true
  hour: 10
  minute: 30
  skip_weekdays: [ 5, 6 ]    # 周一=1 ... 周日=7，周五、周六不发送
  test_on_startup: true    # 启动后立即发一次，方便测试
  bot: ""                  # 为空时发到 feishu.github_bot
```

`skip_weekdays` 使用 ISO 星期：1=周一、2=周二、3=周三、4=周四、5=周五、6=周六、7=周日；`[5, 6]` 表示周五和周六不发送。

`test_on_startup` 正式环境建议改为 `false`。卡片里使用了 `<at id=all></at>` 真正 @所有人，群主需要开启“@所有人”权限。

### 配置 GitHub Webhook

在 GitHub 仓库 Settings → Webhooks 中添加：

- **Payload URL**: `http://<your-server>:8090/api/v1/github_webhook`
- **Content type**: `application/json`
- **Events**: 勾选所需事件

## 依赖管理

通过 `dependencies.json` 声明依赖，`deps.py` 负责下载：

```json
{
  "chen-sdk":   { "repo": "Chen-Christins/chen", "version": "1.6.0", "arch": "auto" },
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

| 模式   | 行为                                                                          |
|--------|-------------------------------------------------------------------------------|
| `cold` | 停服 → 清空运行目录 → 解压产物 → 用 `CONFIG_DIR` 覆盖 `conf/` → `./server -d` |
| `hot`  | 解压产物 → 拷贝 `module/*.so` 到运行目录 → `./server -s reload`               |
| `stop` | 执行 `./server -s stop`                                                       |

产物来源：tag → Release 资产；分支 → 该分支最近一次成功的 `Build & Package` artifact（`larkbot-amd64`）。

服务器端逻辑在 `scripts/deploy.sh`。 **Secrets**：`SSH_HOST`/`SSH_USER`/`SSH_PORT`/`SSH_PRIVATE_KEY`； **Variables**：
`DEPLOY_PATH`、`CONFIG_DIR`（内含权威配置，`server.work_path` 需指向 `DEPLOY_PATH`）。

## 项目结构

```
src/
├── bot.cc                      # 入口
├── BotModule.h / .cc           # 模块生命周期，Servlet 注册
├── Struct.hpp / .cc            # Result / LarkBotServlet / feishuSign
├── Utils.hpp / .cc             # 业务侧通用工具（配置解析、URL/标识符校验）
├── feishu/
│   └── FeishuBotManager.hpp / .cc # 多机器人配置、按名字查找、签名与发送
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
