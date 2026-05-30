#include "GithubWebHook.hpp"

#include <chen/log/log.h>

#include "../struct.hpp"

namespace bot {

static chen::Logger::ptr logger = LOG_NAME("bot");

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
    INFO(logger) << "handle push event";
    return true;
}

bool GithubWebHook::handleCreateEvent(const Json::Value& payload, Result::ptr result) {
    INFO(logger) << "handle create event";
    return true;
}

} // namespace bot