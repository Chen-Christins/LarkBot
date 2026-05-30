#include "GithubWebHook.hpp"

#include <chen/log/log.h>

#include "../struct.hpp"

namespace bot {

static chen::Logger::ptr logger = LOG_NAME("bot");

GithubWebHook::GithubWebHook()
    : LarkBotServlet("github_webhook") {
}

int32_t GithubWebHook::handle(chen::http::HttpRequest::ptr request
    , chen::http::HttpResponse::ptr response, chen::http::HttpSession::ptr session, Result::ptr result) {
    do {
        INFO(logger) << "handle github webhook";
        INFO(logger) << "req: " << request->toString();

    } while (0);
    response->setBody(result->toJsonString());
    return 0;
}

} // namespace bot