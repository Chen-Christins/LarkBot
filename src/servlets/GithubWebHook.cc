#include "GithubWebHook.hpp"

#include <chen/log/log.h>
#include <chen/config/config.h>
#include <chen/http/http_connection.h>
#include <chen/iomanager/iomanager.h>

#include <sstream>

#include "../Struct.hpp"
#include "../protocol/LarkCardProtocol.hpp"

namespace bot {

static chen::Logger::ptr logger = LOG_NAME("bot");

static chen::ConfigVar<std::string>::ptr g_feishu_webhook_url = 
    chen::Config::Lookup<std::string>("feishu.webhook_url", "", "飞书 Webhook 地址");

GithubWebHook::GithubWebHook()
    : LarkBotServlet("github_webhook") {
}

GithubEvent GithubWebHook::parseEvent(const std::string& event) {
    if (event == "push") {
        return GithubEvent::PUSH;
    } else if (event == "create") {
        return GithubEvent::CREATE;
    } else if (event == "pull_request") {
        return GithubEvent::PULL_REQUEST;
    } else if (event == "delete") {
        return GithubEvent::DELETE;
    } else if (event == "workflow_run") {
        return GithubEvent::WORKFLOW_RUN;
    } else if (event == "release") {
        return GithubEvent::RELEASE;
    } else {
        throw std::invalid_argument("unsupported event: " + event);
    }
}

int32_t GithubWebHook::handle(chen::http::HttpRequest::ptr request
    , chen::http::HttpResponse::ptr response, chen::http::HttpSession::ptr session, Result::ptr result) {
    do {
        std::string event = request->getHeader("X-GitHub-Event");
        if (event.empty()) {
            result->setResult(400, "missing X-GitHub-Event header");
            break;
        }

        auto body = request->getBody();
        Json::Value payload;
        if (!chen::JsonUtil::FromString(payload, body)) {
            result->setResult(400, "invalid json body");
            break;
        }

        switch (parseEvent(event)) {
        case GithubEvent::PUSH:
            if (!handlePushEvent(payload, result)) {
                result->setResult(500, "failed to handle push event");
                break;
            }
            break;
        case GithubEvent::CREATE:
            if (!handleCreateEvent(payload, result)) {
                result->setResult(500, "failed to handle create event");
                break;
            }
            break;
        case GithubEvent::PULL_REQUEST:
            if (!handlePullRequestEvent(payload, result)) {
                result->setResult(500, "failed to handle pull_request event");
                break;
            }
            break;
        case GithubEvent::DELETE:
            if (!handleDeleteEvent(payload, result)) {
                result->setResult(500, "failed to handle delete event");
                break;
            }
            break;
        case GithubEvent::WORKFLOW_RUN:
            if (!handleWorkflowRunEvent(payload, result)) {
                result->setResult(500, "failed to handle workflow_run event");
                break;
            }
            break;
        case GithubEvent::RELEASE:
            if (!handleReleaseEvent(payload, result)) {
                result->setResult(500, "failed to handle release event");
                break;
            }
            break;
        default:
            result->setResult(400, "unsupported event: " + event);
            break;
        }

    } while (0);
    response->setBody(result->toJsonString());
    return 0;
}

void GithubWebHook::sendFeishuMessage(const std::string& content) {
    std::string feishuWebhookUrl = g_feishu_webhook_url->getValue();
    if (feishuWebhookUrl.empty()) {
        WARN(logger) << "feishu webhook url is empty, skip sending message";
        return;
    }
    // 投递到协程调度器异步发送，不阻塞 GitHub WebHook 响应
    chen::IOManager::GetThis()->schedule([url = std::move(feishuWebhookUrl), content]() {
        auto headers = std::map<std::string, std::string>{
            {"Content-Type", "application/json"}
        };
        auto ret = chen::http::HttpConnection::DoRequest(chen::http::HttpMethod::POST, url, 2000, headers, content);
        INFO(logger) << "send feishu message, data=" << ret->toString();
    });
}

bool GithubWebHook::handlePushEvent(const Json::Value& payload, Result::ptr result) {
    std::string ref = payload.get("ref", "").asString();
    std::string before = payload.get("before", "").asString();
    std::string after = payload.get("after", "").asString();
    bool forced = payload.get("forced", false).asBool();
    bool deleted = payload.get("deleted", false).asBool();
    std::string compare = payload.get("compare", "").asString();

    // 删除/创建分支由 delete/create 事件处理，commits 为空时跳过
    const Json::Value& commits = payload["commits"];
    if (deleted || commits.empty()) {
        INFO(logger) << "push event skipped (deleted=" << deleted << ", commits=" << commits.size() << ")";
        return true;
    }

    // 提取分支名或 tag 名
    std::string refName;
    std::string refType;
    static const std::string BRANCH_PREFIX = "refs/heads/";
    static const std::string TAG_PREFIX = "refs/tags/";

    if (ref.compare(0, BRANCH_PREFIX.size(), BRANCH_PREFIX) == 0) {
        refName = ref.substr(BRANCH_PREFIX.size());
        refType = "branch";
    } else if (ref.compare(0, TAG_PREFIX.size(), TAG_PREFIX) == 0) {
        refName = ref.substr(TAG_PREFIX.size());
        refType = "tag";
    } else {
        refName = ref;
        refType = "ref";
    }

    const Json::Value& repo = payload["repository"];
    const Json::Value& pusher = payload["pusher"];
    std::string repoName = repo.get("full_name", "").asString();
    std::string pusherName = pusher.get("name", "").asString();

    // 构建飞书卡片消息
    LarkCardProtocol card;

    // 头部
    std::string title = "[" + repoName + "] " + pusherName + " pushed";
    if (refType == "tag") {
        title += " tag 🏷️  ";
    } else {
        title += " to ";
    }
    title += refName;
    if (forced) {
        title += " ⚡";
    }
    title += " 📦";

    card.setHeader(title, "", "blue");

    // commit 摘要
    std::ostringstream summary;
    summary << "**" << commits.size() << " commit(s)** pushed to `" << refName << "`\n"
            << "From `" << before.substr(0, 7) << "` → `" << after.substr(0, 7) << "`\n"
            << "[Compare changes](" << compare << ")";
    card.addElement(LarkCardProtocol::markdownElement(summary.str()));

    // 每个 commit 详情
    for (const auto& commit : commits) {
        std::stringstream detail;
        detail << "**" << commit["id"].asString().substr(0, 7) << "**  "
               << commit["author"]["name"].asString() << "  \n"
               << commit["message"].asString() << "  \n";

        if (!commit["added"].empty() || !commit["removed"].empty() || !commit["modified"].empty()) {
            detail << "```\n";
            for (const auto& f : commit["added"]) {
                detail << "A  " << f.asString() << "\n";
            }
            for (const auto& f : commit["removed"]) {
                detail << "D  " << f.asString() << "\n";
            }
            for (const auto& f : commit["modified"]) {
                detail << "M  " << f.asString() << "\n";
            }
            detail << "```";
        }

        card.addElement(LarkCardProtocol::markdownElement(detail.str()));
    }

    // 查看对比按钮
    card.addElement(LarkCardProtocol::buttonElement("View Compare", compare));

    card.build();
    
    Json::Value cardJson;
    card.getData(cardJson);
    
    sendFeishuMessage(cardJson.toStyledString());

    INFO(logger) << "push event: " << repoName << " " << refName << " " 
        << before.substr(0, 7) << "->" << after.substr(0, 7) << " commits:" << commits.size();

    return true;
}

bool GithubWebHook::handlePullRequestEvent(const Json::Value& payload, Result::ptr result) {
    std::string action = payload.get("action", "").asString();
    const Json::Value& pr = payload["pull_request"];
    const Json::Value& repo = payload["repository"];
    const Json::Value& sender = payload["sender"];

    std::string repoName = repo.get("full_name", "").asString();
    std::string senderName = sender.get("login", "").asString();
    int number = pr.get("number", 0).asInt();
    std::string title = pr.get("title", "").asString();
    std::string body = pr.get("body", "").asString();
    std::string state = pr.get("state", "").asString();
    bool merged = pr.get("merged", false).asBool();
    std::string htmlUrl = pr.get("html_url", "").asString();
    std::string baseRef = pr["base"].get("ref", "").asString();
    std::string headRef = pr["head"].get("ref", "").asString();

    // 构建飞书卡片消息
    LarkCardProtocol card;

    // 头部 - 根据 action 选择颜色和 emoji
    std::string headerTitle = "[" + repoName + "] PR #" + std::to_string(number) + ": " + title;
    std::string templateColor = "blue";
    std::string emoji;
    if (action == "opened") {
        templateColor = "green";
        emoji = "🟢";
    } else if (action == "closed") {
        if (merged) {
            templateColor = "purple";
            emoji = "🟣";
        } else {
            templateColor = "red";
            emoji = "🔴";
        }
    } else if (action == "reopened") {
        emoji = "🔄";
    }
    if (!emoji.empty()) {
        headerTitle += " " + emoji;
    }
    card.setHeader(headerTitle, "", templateColor);

    // PR 摘要
    std::ostringstream summary;
    summary << "**" << senderName << "** ";
    if (action == "opened") {
        summary << "opened a pull request";
    } else if (action == "closed") {
        summary << (merged ? "merged" : "closed") << " a pull request";
    } else if (action == "reopened") {
        summary << "reopened a pull request";
    } else {
        summary << action << " a pull request";
    }
    summary << "\n";
    summary << "`" << headRef << "` → `" << baseRef << "`\n";

    if (!body.empty()) {
        // 截取前 200 字符
        std::string preview = body.size() > 200 ? body.substr(0, 200) + "..." : body;
        summary << "\n" << preview;
    }
    card.addElement(LarkCardProtocol::markdownElement(summary.str()));

    // 查看 PR 按钮
    card.addElement(LarkCardProtocol::buttonElement("View Pull Request", htmlUrl));

    card.build();

    Json::Value cardJson;
    card.getData(cardJson);

    sendFeishuMessage(cardJson.toStyledString());

    INFO(logger) << "pull_request event: " << repoName << " #" << number
        << " action=" << action << " " << headRef << "->" << baseRef;

    return true;
}

bool GithubWebHook::handleCreateEvent(const Json::Value& payload, Result::ptr result) {
    std::string ref = payload.get("ref", "").asString();
    std::string refType = payload.get("ref_type", "").asString();
    const Json::Value& repo = payload["repository"];
    const Json::Value& sender = payload["sender"];
    std::string repoName = repo.get("full_name", "").asString();
    std::string senderName = sender.get("login", "").asString();
    std::string masterBranch = payload.get("master_branch", "").asString();

    // 提取名称 (refs/heads/xxx -> xxx, refs/tags/xxx -> xxx)
    std::string refName;
    std::string prefix;
    if (refType == "branch") {
        prefix = "refs/heads/";
    } else if (refType == "tag") {
        prefix = "refs/tags/";
    }
    if (!prefix.empty() && ref.compare(0, prefix.size(), prefix) == 0) {
        refName = ref.substr(prefix.size());
    } else {
        refName = ref;
    }

    LarkCardProtocol card;

    std::string headerTitle = "[" + repoName + "] " + refType + " created: " + refName + (refType == "branch" ? " 🌿" : " 🏷️");
    card.setHeader(headerTitle, "", "green");

    std::ostringstream summary;
    summary << "**" << senderName << "** created a " << refType << " `" << refName << "`";
    if (refType == "branch" && !masterBranch.empty()) {
        summary << "\nDefault branch: `" << masterBranch << "`";
    }
    card.addElement(LarkCardProtocol::markdownElement(summary.str()));

    std::string repoUrl = repo.get("html_url", "").asString();
    if (!repoUrl.empty()) {
        card.addElement(LarkCardProtocol::buttonElement("View Repository", repoUrl));
    }

    card.build();

    Json::Value cardJson;
    card.getData(cardJson);

    sendFeishuMessage(cardJson.toStyledString());

    INFO(logger) << "create event: " << repoName << " " << refType << " " << refName;

    return true;
}

bool GithubWebHook::handleDeleteEvent(const Json::Value& payload, Result::ptr result) {
    std::string ref = payload.get("ref", "").asString();
    std::string refType = payload.get("ref_type", "").asString();
    const Json::Value& repo = payload["repository"];
    const Json::Value& sender = payload["sender"];
    std::string repoName = repo.get("full_name", "").asString();
    std::string senderName = sender.get("login", "").asString();

    LarkCardProtocol card;

    std::string headerTitle = "[" + repoName + "] " + refType + " deleted: " + ref + " 🗑️";
    card.setHeader(headerTitle, "", "red");

    std::ostringstream summary;
    summary << "**" << senderName << "** deleted " << refType << " `" << ref << "`";
    card.addElement(LarkCardProtocol::markdownElement(summary.str()));

    std::string repoUrl = repo.get("html_url", "").asString();
    if (!repoUrl.empty()) {
        card.addElement(LarkCardProtocol::buttonElement("View Repository", repoUrl));
    }

    card.build();

    Json::Value cardJson;
    card.getData(cardJson);

    sendFeishuMessage(cardJson.toStyledString());

    INFO(logger) << "delete event: " << repoName << " " << refType << " " << ref;

    return true;
}

bool GithubWebHook::handleWorkflowRunEvent(const Json::Value& payload, Result::ptr result) {
    std::string action = payload.get("action", "").asString();
    const Json::Value& run = payload["workflow_run"];
    const Json::Value& repo = payload["repository"];
    // const Json::Value& sender = payload["sender"];

    // 只报告已完成的 workflow
    if (action != "completed") {
        INFO(logger) << "workflow_run action=" << action << " skip";
        return true;
    }

    std::string repoName = repo.get("full_name", "").asString();
    std::string name = run.get("name", "").asString();
    std::string conclusion = run.get("conclusion", "").asString();
    std::string branch = run.get("head_branch", "").asString();
    std::string htmlUrl = run.get("html_url", "").asString();
    std::string displayTitle = run.get("display_title", "").asString();
    int runNumber = run.get("run_number", 0).asInt();
    std::string headSha = run["head_commit"].get("id", "").asString().substr(0, 7);

    LarkCardProtocol card;

    // 根据结论选择颜色和 emoji
    std::string headerTitle = "[" + repoName + "] Workflow #" + std::to_string(runNumber) + ": " + name;
    std::string templateColor;
    std::string emoji;
    if (conclusion == "success") {
        templateColor = "green";
        emoji = "✅";
    } else if (conclusion == "failure") {
        templateColor = "red";
        emoji = "❌";
    } else if (conclusion == "cancelled") {
        templateColor = "yellow";
        emoji = "⚠️";
    } else {
        templateColor = "blue";
        emoji = "❓";
    }
    headerTitle += " " + emoji;
    card.setHeader(headerTitle, "", templateColor);

    std::ostringstream summary;
    summary << emoji << " **" << conclusion << "**  \n"
            << "Branch: `" << branch << "`  \n"
            << "Commit: `" << headSha << "` — " << displayTitle;
    card.addElement(LarkCardProtocol::markdownElement(summary.str()));

    card.addElement(LarkCardProtocol::buttonElement("View Workflow Run", htmlUrl));

    card.build();

    Json::Value cardJson;
    card.getData(cardJson);

    sendFeishuMessage(cardJson.toStyledString());

    INFO(logger) << "workflow_run event: " << repoName << " " << name
        << " conclusion=" << conclusion << " branch=" << branch;

    return true;
}

bool GithubWebHook::handleReleaseEvent(const Json::Value& payload, Result::ptr result) {
    std::string action = payload.get("action", "").asString();
    const Json::Value& release = payload["release"];
    const Json::Value& repo = payload["repository"];

    std::string repoName = repo.get("full_name", "").asString();
    std::string tagName = release.get("tag_name", "").asString();
    std::string name = release.get("name", "").asString();
    std::string body = release.get("body", "").asString();
    bool prerelease = release.get("prerelease", false).asBool();
    bool draft = release.get("draft", false).asBool();
    std::string htmlUrl = release.get("html_url", "").asString();
    std::string targetCommitish = release.get("target_commitish", "").asString();

    LarkCardProtocol card;

    std::string displayName = name.empty() ? tagName : name;
    std::string headerTitle = "[" + repoName + "] Release: " + displayName;
    std::string templateColor;
    std::string emoji;
    if (draft) {
        templateColor = "grey";
        emoji = "📝";
    } else if (action == "published" && !prerelease) {
        templateColor = "green";
        emoji = "🚀";
    } else if (prerelease) {
        templateColor = "orange";
        emoji = "🧪";
    } else if (action == "deleted") {
        templateColor = "red";
        emoji = "🗑️";
    } else {
        templateColor = "blue";
    }
    if (!emoji.empty()) {
        headerTitle += " " + emoji;
    }
    card.setHeader(headerTitle, "", templateColor);

    std::ostringstream summary;
    summary << "**" << repoName << "** ";
    if (action == "published") {
        summary << (prerelease ? "pre-release" : "released");
    } else if (action == "prereleased") {
        summary << "pre-released";
    } else {
        summary << action;
    }
    summary << "\n";
    summary << "Tag: `" << tagName << "`  \n";
    summary << "Target: `" << targetCommitish << "`  \n";

    if (!body.empty()) {
        std::string preview = body.size() > 200 ? body.substr(0, 200) + "..." : body;
        summary << "\n" << preview;
    }
    card.addElement(LarkCardProtocol::markdownElement(summary.str()));

    card.addElement(LarkCardProtocol::buttonElement("View Release", htmlUrl));

    card.build();

    Json::Value cardJson;
    card.getData(cardJson);

    sendFeishuMessage(cardJson.toStyledString());

    INFO(logger) << "release event: " << repoName << " tag=" << tagName
        << " action=" << action << " prerelease=" << prerelease;

    return true;
}

} // namespace bot
