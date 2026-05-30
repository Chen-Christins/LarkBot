#include "GithubWebHook.hpp"

#include <chen/log/log.h>

namespace bot {

static chen::Logger::ptr logger = LOG_NAME("bot");

GithubWebHook::GithubWebHook()
    : chen::http::Servlet("github_webhook") {
}

int32_t GithubWebHook::handle(chen::http::HttpRequest::ptr request
    , chen::http::HttpResponse::ptr response, chen::http::HttpSession::ptr session) {
    INFO(logger) << "Received GitHub WebHook: " << request->toString();

    

    return 0;
}

} // namespace bot