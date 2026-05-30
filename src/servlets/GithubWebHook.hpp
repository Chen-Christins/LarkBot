/**
 * @file GithubWebHook.h
 * @brief Github WebHook Servlet
 * @author Christins (chen.christins@qq.com)
 * @date 2026-05-30
 * @copyright Apache 2.0
 */
#pragma once

#include <chen/http/servlet.h>
#include "../struct.hpp"

namespace bot {

class GithubWebHook : public LarkBotServlet {
public:
    GithubWebHook();

    virtual int32_t handle(chen::http::HttpRequest::ptr request, 
                   chen::http::HttpResponse::ptr response, 
                   chen::http::HttpSession::ptr session,
                   Result::ptr result) override;
};

} // namespace bot