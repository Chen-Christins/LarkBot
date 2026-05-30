#include "GithubWebHook.hpp"

#include <chen/log/log.h>
#include <chen/config/config.h>
#include <chen/http/http_connection.h>

#include <sstream>
#include "../Struct.hpp"
#include "../protocol/LarkCardProtocol.hpp"

namespace bot {

static chen::Logger::ptr logger = LOG_NAME("bot");

static chen::ConfigVar<std::string>::ptr feishu_webhook_url = 
    chen::Config::Lookup<std::string>("feishu.webhook_url", "", "飞书 Webhook 地址");

GithubWebHook::GithubWebHook()
    : LarkBotServlet("github_webhook") {
}

GithubEvent GithubWebHook::parseEvent(const std::string& event) {
    if (event == "push") {
        return GithubEvent::PUSH;
    } else if (event == "create") {
        return GithubEvent::CREATE;
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
        default:
            result->setResult(400, "unsupported event: " + event);
            break;
        }

    } while (0);
    response->setBody(result->toJsonString());
    return 0;
}

bool GithubWebHook::handlePushEvent(const Json::Value& payload, Result::ptr result) {
    std::string ref = payload.get("ref", "").asString();
    std::string before = payload.get("before", "").asString();
    std::string after = payload.get("after", "").asString();
    bool forced = payload.get("forced", false).asBool();
    std::string compare = payload.get("compare", "").asString();

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
    const Json::Value& commits = payload["commits"];
    std::string repoName = repo.get("full_name", "").asString();
    std::string pusherName = pusher.get("name", "").asString();

    // 构建飞书卡片消息
    LarkCardProtocol card;

    // 头部
    std::string title = "[" + repoName + "] " + pusherName + " pushed";
    if (refType == "tag") {
        title += " tag ";
    } else {
        title += " to ";
    }
    title += refName;
    if (forced) {
        title += " (force push)";
    }

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

    std::string feishuWebhookUrl = feishu_webhook_url->getValue();
    if (!feishuWebhookUrl.empty()) {
        auto headers = std::map<std::string, std::string>{
            {"Content-Type", "application/json"}
        };
        chen::http::HttpConnection::DoRequest(chen::http::HttpMethod::POST, feishuWebhookUrl, 2000, headers, card.toString());
    } else {
        WARN(logger) << "feishu webhook url is empty, skip sending message";
    }

    card.getData(result->jsondata);

    INFO(logger) << "push event: " << repoName << " " << refName << " " 
        << before.substr(0, 7) << "->" << after.substr(0, 7) << " commits:" << commits.size();

    return true;
}

bool GithubWebHook::handleCreateEvent(const Json::Value& payload, Result::ptr result) {
    INFO(logger) << "handle create event";
    return true;
}

} // namespace bot
