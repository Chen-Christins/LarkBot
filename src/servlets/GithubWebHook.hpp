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

enum class GithubEvent {
    PUSH,
    CREATE,
};

class GithubWebHook : public LarkBotServlet {
public:
    GithubWebHook();

    virtual int32_t handle(chen::http::HttpRequest::ptr request, 
                   chen::http::HttpResponse::ptr response, 
                   chen::http::HttpSession::ptr session,
                   Result::ptr result) override;

    static GithubEvent parseEvent(const std::string& event);

private:
    /**
     * @brief 处理 push 事件
     * @param payload 
     * @param result 
     * @return bool 
     */
    bool handlePushEvent(const Json::Value& payload, Result::ptr result);

    /**
     * @brief 处理 create 事件
     * @param payload 
     * @param result 
     * @return bool 
     */
    bool handleCreateEvent(const Json::Value& payload, Result::ptr result);
};

} // namespace bot