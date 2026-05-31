/**
 * @file GithubWebHook.h
 * @brief Github WebHook Servlet
 * @author Christins (chen.christins@qq.com)
 * @date 2026-05-30
 * @copyright Apache 2.0
 */
#pragma once

#include <chen/http/servlet.h>
#include "../Struct.hpp"

namespace bot {

enum class GithubEvent {
    PUSH,
    CREATE,
    PULL_REQUEST,
    DELETE,
    WORKFLOW_RUN,
    RELEASE,
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

    /**
     * @brief 处理 pull_request 事件
     * @param payload
     * @param result
     * @return bool
     */
    bool handlePullRequestEvent(const Json::Value& payload, Result::ptr result);

    /**
     * @brief 处理 delete 事件（分支/标签删除）
     */
    bool handleDeleteEvent(const Json::Value& payload, Result::ptr result);

    /**
     * @brief 处理 workflow_run 事件
     */
    bool handleWorkflowRunEvent(const Json::Value& payload, Result::ptr result);

    /**
     * @brief 处理 release 事件
     */
    bool handleReleaseEvent(const Json::Value& payload, Result::ptr result);

    /**
     * @brief 发送飞书消息
     * @param content 消息内容（JSON 格式）
     */
    void sendFeishuMessage(const std::string& content);
};

} // namespace bot