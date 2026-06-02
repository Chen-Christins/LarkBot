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

- cmake >= 3.22
- g++ (C++20)
- chen-sdk-1.2.1 — HTTP 服务框架
- jsoncpp, yaml-cpp, libevent, openssl

### 编译

```bash
cmake -B build -S .
cmake --build build -j$(nproc)
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

```yaml
feishu:
  webhook_url: "..."
  enable_signature: 1
  secret: "your-feishu-secret"
```

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

Apache 2.0
