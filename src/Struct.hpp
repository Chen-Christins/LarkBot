/**
 * @file Struct.hpp
 * @brief 结构体定义
 * @author Christins (chen.christins@qq.com)
 * @date 2026-05-30
 * @copyright Apache 2.0
 */
#pragma once

#include <chen/util/json_util.h>
#include <chen/http/servlet.h>

namespace bot {

struct Result {
    typedef std::shared_ptr<Result> ptr;

    Result(int32_t c = 200, const std::string& message = "ok");

    int32_t code_;
    int64_t used_;
    std::string message_;
    Json::Value jsondata_;

    template <class T>
    void set(const std::string& key, const T& v) {
        jsondata_[key] = v;
    }
    void set(const std::string& key, const char* v) {
        jsondata_[key] = v;
    }
    void set(const std::string& key, const std::string& v) {
        jsondata_[key] = v;
    }

    template <class T>
    void append(const std::string& key, const T& v) {
        jsondata_[key].append(v);
    }

    void setResult(int32_t c, const std::string& message);

    std::string toJsonString() const;
};

class LarkBotServlet : public chen::http::Servlet {
public:
    LarkBotServlet(const std::string& name);

    virtual int32_t handle(chen::http::HttpRequest::ptr request, 
                   chen::http::HttpResponse::ptr response, 
                   chen::http::HttpSession::ptr session) override;

    virtual int32_t handle(chen::http::HttpRequest::ptr request, 
                   chen::http::HttpResponse::ptr response, 
                   chen::http::HttpSession::ptr session,
                   Result::ptr result) = 0;
};

/**
 * @brief 飞书签名算法
 * @param secret 飞书机器人的签名校验密钥
 * @param timestamp 当前时间戳(秒)，距当前不超过 1 小时
 * @return Base64 编码的签名结果
 */
std::string feishuSign(const std::string& secret, int64_t timestamp);

} // namespace bot