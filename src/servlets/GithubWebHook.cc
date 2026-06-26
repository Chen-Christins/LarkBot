#include "GithubWebHook.hpp"

#include <chen/log/log.h>
#include <chen/config/config.h>
#include <chen/http/http_connection.h>
#include <chen/iomanager/iomanager.h>
#include <chen/rpc/rpc_client_pool.h>

#include <sstream>

#include "../Struct.hpp"
#include "../protocol/LarkCardProtocol.hpp"
#include "protocol_ss_github.h"

namespace bot {

static chen::Logger::ptr logger = LOG_NAME("bot");

static chen::ConfigVar<std::string>::ptr g_feishu_webhook_url =
    chen::Config::Lookup<std::string>("feishu.webhook_url", "", "飞书 Webhook 地址");

static chen::ConfigVar<std::string>::ptr g_blog_rpc_address =
    chen::Config::Lookup<std::string>("blog.rpc_address", "127.0.0.1:8092", "blog 后端 RPC 地址");

/// tagGithubPRInfo 转字符串（用于日志打印）
static std::string InfoToString(const tagGithubPRInfo& info) {
    std::ostringstream oss;
    oss << "PR #" << info.Number
        << " Action=" << info.Action
        << " Title=" << info.Title
        << " State=" << info.State
        << " Author=" << info.Author
        << " HeadBranch=" << info.HeadBranch
        << " BaseBranch=" << info.BaseBranch
        << " HeadSha=" << info.HeadSha
        << " Merged=" << (info.Merged ? "true" : "false")
        << " ChangedFiles=" << info.ChangedFiles
        << " Additions=" << info.Additions
        << " Deletions=" << info.Deletions
        << " CreatedAt=" << info.CreatedAt
        << " ClosedAt=" << info.ClosedAt
        << " MergedAt=" << info.MergedAt
        << " RepoOwner=" << info.RepoOwner
        << " RepoName=" << info.RepoName;
    return oss.str();
}

/// tagGithubPRReviewInfo 转字符串（用于日志打印）
static std::string ReviewInfoToString(const tagGithubPRReviewInfo& info) {
    std::ostringstream oss;
    oss << "PR #" << info.PRNumber
        << " Action=" << info.Action
        << " Reviewer=" << info.Reviewer
        << " State=" << info.State
        << " SubmittedAt=" << info.SubmittedAt
        << " RepoOwner=" << info.RepoOwner
        << " RepoName=" << info.RepoName;
    return oss.str();
}

/// 异步转发 PR 数据到 blog 后端
static void ForwardPRToBlog(const tagGithubPRInfo& info) {
    std::string addr = g_blog_rpc_address->getValue();
    if (addr.empty()) {
        WARN(logger) << "blog.rpc_address not configured, skip forwarding PR #" << info.Number;
        return;
    }
    chen::Scheduler::GetThis()->schedule([addr, info]() {
        try {
            auto client = chen::rpc::RpcClientPoolMgr::GetInstance()->getClient(addr);
            int32_t ret = client->call<int32_t>("GithubPRWebhook", info);

            DEBUG(logger) << "ForwardPRToBlog: " << InfoToString(info);

            if (ret == 0) {
                INFO(logger) << "ForwardPRToBlog: PR #" << info.Number << " synced OK";
            } else {
                WARN(logger) << "ForwardPRToBlog: PR #" << info.Number << " sync failed, ret=" << ret;
            }
        } catch (std::exception& e) {
            WARN(logger) << "ForwardPRToBlog: PR #" << info.Number
                << " RPC call failed: " << e.what();
        }
    });
}

/// 异步转发 PR Review 数据到 blog 后端
static void ForwardPRReviewToBlog(const tagGithubPRReviewInfo& info) {
    std::string addr = g_blog_rpc_address->getValue();
    if (addr.empty()) {
        WARN(logger) << "blog.rpc_address not configured, skip forwarding review";
        return;
    }
    chen::Scheduler::GetThis()->schedule([addr, info]() {
        try {
            auto client = chen::rpc::RpcClientPoolMgr::GetInstance()->getClient(addr);
            int32_t ret = client->call<int32_t>("GithubPRReviewWebhook", info);
            
            DEBUG(logger) << "ForwardPRReviewToBlog: " << ReviewInfoToString(info);

            if (ret == 0) {
                INFO(logger) << "ForwardPRReviewToBlog: PR #" << info.PRNumber << " synced OK";
            } else {
                WARN(logger) << "ForwardPRReviewToBlog: PR #" << info.PRNumber << " sync failed, ret=" << ret;
            }
        } catch (std::exception& e) {
            WARN(logger) << "ForwardPRReviewToBlog: PR #" << info.PRNumber
                << " RPC call failed: " << e.what();
        }
    });
}

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
    } else if (event == "watch") {
        return GithubEvent::WATCH;
    } else if (event == "pull_request_review") {
        return GithubEvent::PULL_REQUEST_REVIEW;
    } else if (event == "pull_request_review_comment") {
        return GithubEvent::PULL_REQUEST_REVIEW_COMMENT;
    } else if (event == "fork") {
        return GithubEvent::FORK;
    } else {
        WARN(logger) << "unsupported github event: " << event;
        return GithubEvent::UNKNOWN;
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
        case GithubEvent::WATCH:
            if (!handleWatchEvent(payload, result)) {
                result->setResult(500, "failed to handle watch event");
                break;
            }
            break;
        case GithubEvent::PULL_REQUEST_REVIEW:
            if (!handlePullRequestReviewEvent(payload, result)) {
                result->setResult(500, "failed to handle pull_request_review event");
                break;
            }
            break;
        case GithubEvent::PULL_REQUEST_REVIEW_COMMENT:
            if (!handlePullRequestReviewCommentEvent(payload, result)) {
                result->setResult(500, "failed to handle pull_request_review_comment event");
                break;
            }
            break;
        case GithubEvent::FORK:
            if (!handleForkEvent(payload, result)) {
                result->setResult(500, "failed to handle fork event");
                break;
            }
            break;
        case GithubEvent::UNKNOWN:
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

    chen::IOManager::GetThis()->schedule([url = std::move(feishuWebhookUrl), content]() {
        auto headers = std::map<std::string, std::string>{
            {"Content-Type", "application/json"}
        };

        constexpr int kMaxRetries = 5;
        for (int attempt = 1; attempt <= kMaxRetries; ++attempt) {
            auto ret = chen::http::HttpConnection::DoRequest(chen::http::HttpMethod::POST, url, 5000, headers, content);
            if (ret->result == static_cast<int>(chen::http::HttpResult::Error::OK)) {
                INFO(logger) << "send feishu message success";
                return;
            }
            // 只对临时性错误重试
            if (ret->result != static_cast<int>(chen::http::HttpResult::Error::TIMEOUT)
                    && ret->result != static_cast<int>(chen::http::HttpResult::Error::CONNECT_FAIL)
                    && ret->result != static_cast<int>(chen::http::HttpResult::Error::SEND_CLOSE_BY_PEER)
                    && ret->result != static_cast<int>(chen::http::HttpResult::Error::SEND_SOCKET_ERROR)) {
                WARN(logger) << "send feishu message failed, non-retryable error, data=" << ret->toString();
                return;
            }
            WARN(logger) << "send feishu message failed (attempt " << attempt << "/" << kMaxRetries
                << "), data=" << ret->toString();
        }
        ERROR(logger) << "send feishu message exhausted all retries";
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
        summary << "\n" << body;
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

    // 转发到 blog 后端
    {
        tagGithubPRInfo blog_info;
        blog_info.Action     = action;
        blog_info.Number     = number;
        blog_info.Title      = title;
        blog_info.Body       = body;
        blog_info.State      = state;
        blog_info.Author     = senderName;
        blog_info.HeadBranch = headRef;
        blog_info.BaseBranch = baseRef;
        blog_info.HeadSha    = pr["head"]["sha"].asString();
        blog_info.Merged     = merged;
        blog_info.ChangedFiles = pr.get("changed_files", 0).asInt();
        blog_info.Additions    = pr.get("additions", 0).asInt();
        blog_info.Deletions    = pr.get("deletions", 0).asInt();
        blog_info.CreatedAt  = pr.get("created_at", "").asString();
        blog_info.ClosedAt   = pr.get("closed_at", "").asString();
        blog_info.MergedAt   = pr.get("merged_at", "").asString();
        // full_name = "owner/repo"
        std::string fullName = repo.get("full_name", "").asString();
        auto slash_pos = fullName.find('/');
        if (slash_pos != std::string::npos) {
            blog_info.RepoOwner = fullName.substr(0, slash_pos);
            blog_info.RepoName  = fullName.substr(slash_pos + 1);
        }
        ForwardPRToBlog(blog_info);
    }

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
        summary << "\n" << body;
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

bool GithubWebHook::handleWatchEvent(const Json::Value& payload, Result::ptr result) {
    std::string action = payload.get("action", "").asString();
    if (action != "started") {
        INFO(logger) << "watch event action=" << action << " skip";
        return true;
    }

    const Json::Value& repo = payload["repository"];
    const Json::Value& sender = payload["sender"];
    std::string repoName = repo.get("full_name", "").asString();
    std::string senderName = sender.get("login", "").asString();
    int stargazersCount = repo.get("stargazers_count", 0).asInt();

    LarkCardProtocol card;

    card.setHeader("[" + repoName + "] New Star ⭐", "", "yellow");

    std::ostringstream summary;
    summary << "**" << senderName << "** starred " << repoName << "\n"
            << "Total stars: **" << stargazersCount << "** ⭐";
    card.addElement(LarkCardProtocol::markdownElement(summary.str()));

    std::string repoUrl = repo.get("html_url", "").asString();
    if (!repoUrl.empty()) {
        card.addElement(LarkCardProtocol::buttonElement("View Repository", repoUrl));
    }

    card.build();

    Json::Value cardJson;
    card.getData(cardJson);

    sendFeishuMessage(cardJson.toStyledString());

    INFO(logger) << "watch event: " << senderName << " starred " << repoName
        << " (total: " << stargazersCount << ")";

    return true;
}

bool GithubWebHook::handlePullRequestReviewEvent(const Json::Value& payload, Result::ptr result) {
    std::string action = payload.get("action", "").asString();
    const Json::Value& review = payload["review"];
    const Json::Value& pr = payload["pull_request"];
    const Json::Value& repo = payload["repository"];
    const Json::Value& sender = payload["sender"];

    std::string repoName = repo.get("full_name", "").asString();
    std::string senderName = sender.get("login", "").asString();
    int prNumber = pr.get("number", 0).asInt();
    std::string prTitle = pr.get("title", "").asString();
    std::string prUrl = pr.get("html_url", "").asString();
    std::string reviewState = review.get("state", "").asString();
    std::string reviewBody = review.get("body", "").asString();
    std::string reviewUrl = review.get("html_url", "").asString();

    LarkCardProtocol card;

    // 根据 review state 选择颜色和 emoji
    std::string headerTitle = "[" + repoName + "] PR #" + std::to_string(prNumber) + " Review: " + prTitle;
    std::string templateColor;
    std::string emoji;
    if (reviewState == "approved") {
        templateColor = "green";
        emoji = "✅";
    } else if (reviewState == "changes_requested") {
        templateColor = "red";
        emoji = "🔴";
    } else if (reviewState == "commented") {
        templateColor = "blue";
        emoji = "💬";
    } else if (reviewState == "dismissed") {
        templateColor = "grey";
        emoji = "↩️";
    } else {
        templateColor = "blue";
    }
    if (!emoji.empty()) {
        headerTitle += " " + emoji;
    }
    card.setHeader(headerTitle, "", templateColor);

    std::ostringstream summary;
    summary << "**" << senderName << "** ";
    if (reviewState == "approved") {
        summary << "approved";
    } else if (reviewState == "changes_requested") {
        summary << "requested changes";
    } else if (reviewState == "commented") {
        summary << "commented";
    } else if (reviewState == "dismissed") {
        summary << "dismissed a review";
    } else {
        summary << reviewState;
    }
    summary << " on PR #" << prNumber;

    if (!reviewBody.empty()) {
        summary << "\n\n" << reviewBody;
    }
    card.addElement(LarkCardProtocol::markdownElement(summary.str()));

    if (!reviewUrl.empty()) {
        card.addElement(LarkCardProtocol::buttonElement("View Review", reviewUrl));
    }
    card.addElement(LarkCardProtocol::buttonElement("View Pull Request", prUrl));

    card.build();

    Json::Value cardJson;
    card.getData(cardJson);

    sendFeishuMessage(cardJson.toStyledString());

    INFO(logger) << "pull_request_review event: " << repoName << " PR #" << prNumber
        << " state=" << reviewState << " by " << senderName;

    // 转发到 blog 后端
    {
        tagGithubPRReviewInfo blog_info;
        blog_info.Action      = action;
        blog_info.PRNumber    = prNumber;
        blog_info.Reviewer    = senderName;
        blog_info.State       = reviewState;
        blog_info.SubmittedAt = review.get("submitted_at", "").asString();
        // full_name = "owner/repo"
        std::string fullName = repo.get("full_name", "").asString();
        auto slash_pos = fullName.find('/');
        if (slash_pos != std::string::npos) {
            blog_info.RepoOwner = fullName.substr(0, slash_pos);
            blog_info.RepoName  = fullName.substr(slash_pos + 1);
        }
        ForwardPRReviewToBlog(blog_info);
    }

    return true;
}

bool GithubWebHook::handlePullRequestReviewCommentEvent(const Json::Value& payload, Result::ptr result) {
    std::string action = payload.get("action", "").asString();
    const Json::Value& comment = payload["comment"];
    const Json::Value& pr = payload["pull_request"];
    const Json::Value& repo = payload["repository"];
    const Json::Value& sender = payload["sender"];

    std::string repoName = repo.get("full_name", "").asString();
    std::string senderName = sender.get("login", "").asString();
    int prNumber = pr.get("number", 0).asInt();
    std::string prTitle = pr.get("title", "").asString();
    std::string prUrl = pr.get("html_url", "").asString();
    std::string commentBody = comment.get("body", "").asString();
    std::string commentUrl = comment.get("html_url", "").asString();
    std::string filePath = comment.get("path", "").asString();
    int line = comment.get("line", 0).asInt();
    int startLine = comment.get("start_line", 0).asInt();

    LarkCardProtocol card;

    std::string headerTitle = "[" + repoName + "] PR #" + std::to_string(prNumber) + " Comment: " + prTitle + " 💬";
    card.setHeader(headerTitle, "", "blue");

    std::ostringstream summary;
    summary << "**" << senderName << "** " << action << " a review comment on PR #" << prNumber << "\n";
    if (!filePath.empty()) {
        summary << "File: `" << filePath << "`";
        if (startLine > 0) {
            summary << " L" << startLine;
            if (line > startLine) {
                summary << "-L" << line;
            }
        } else if (line > 0) {
            summary << " L" << line;
        }
        summary << "\n";
    }

    if (!commentBody.empty()) {
        summary << "\n" << commentBody;
    }
    card.addElement(LarkCardProtocol::markdownElement(summary.str()));

    if (!commentUrl.empty()) {
        card.addElement(LarkCardProtocol::buttonElement("View Comment", commentUrl));
    }
    card.addElement(LarkCardProtocol::buttonElement("View Pull Request", prUrl));

    card.build();

    Json::Value cardJson;
    card.getData(cardJson);

    sendFeishuMessage(cardJson.toStyledString());

    INFO(logger) << "pull_request_review_comment event: " << repoName << " PR #" << prNumber
        << " file=" << filePath << " by " << senderName;

    return true;
}

bool GithubWebHook::handleForkEvent(const Json::Value& payload, Result::ptr result) {
    const Json::Value& forkee = payload["forkee"];
    const Json::Value& repo = payload["repository"];
    const Json::Value& sender = payload["sender"];

    std::string repoName = repo.get("full_name", "").asString();
    std::string forkeeName = forkee.get("full_name", "").asString();
    std::string senderName = sender.get("login", "").asString();
    std::string forkeeUrl = forkee.get("html_url", "").asString();
    std::string repoUrl = repo.get("html_url", "").asString();
    std::string description = forkee.get("description", "").asString();
    int forksCount = repo.get("forks_count", 0).asInt();

    LarkCardProtocol card;

    card.setHeader("[" + repoName + "] New Fork 🍴", "", "purple");

    std::ostringstream summary;
    summary << "**" << senderName << "** forked " << repoName << "\n"
            << "Fork: **" << forkeeName << "**\n"
            << "Total forks: **" << forksCount << "** 🍴";

    if (!description.empty()) {
        summary << "\n\n" << description;
    }
    card.addElement(LarkCardProtocol::markdownElement(summary.str()));

    if (!forkeeUrl.empty()) {
        card.addElement(LarkCardProtocol::buttonElement("View Fork", forkeeUrl));
    }
    if (!repoUrl.empty()) {
        card.addElement(LarkCardProtocol::buttonElement("View Original Repository", repoUrl));
    }

    card.build();

    Json::Value cardJson;
    card.getData(cardJson);

    sendFeishuMessage(cardJson.toStyledString());

    INFO(logger) << "fork event: " << senderName << " forked " << repoName
        << " -> " << forkeeName << " (total forks: " << forksCount << ")";

    return true;
}

} // namespace bot
