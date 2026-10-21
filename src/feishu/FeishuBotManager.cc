#include "FeishuBotManager.hpp"

#include <chen/config/config.h>
#include <chen/http/http_connection.h>
#include <chen/iomanager/iomanager.h>
#include <chen/log/log.h>
#include <chen/util/string_util.h>
#include <chen/util/time_util.h>

#include <map>

#include "../Struct.hpp"
#include "../Utils.hpp"

namespace bot {

static chen::Logger::ptr logger = LOG_NAME("bot");

static chen::ConfigVar<std::vector<FeishuBotConf>>::ptr g_feishu_bots =
    chen::Config::Lookup<std::vector<FeishuBotConf>>("feishu.bots", std::vector<FeishuBotConf>{}, "飞书机器人列表");

namespace {

void SendHttp(const FeishuBot::ptr& bot, const Json::Value& payload)
{
    Json::Value body = payload;
    if (bot->enable_signature) {
        int64_t now = chen::TimeUtil::GetCurrentSec();
        body["timestamp"] = now;
        body["sign"] = feishuSign(bot->secret, now);
    }

    std::string content = chen::JsonUtil::ToString(body);
    auto headers = std::map<std::string, std::string>{{"Content-Type", "application/json"}};

    constexpr int kMaxRetries = 5;
    for (int attempt = 1; attempt <= kMaxRetries; ++attempt) {
        auto ret = chen::http::HttpConnection::DoRequest(chen::http::HttpMethod::POST, bot->webhook_url, 5000, headers, content);
        if (!ret) {
            WARN(logger) << "send feishu message failed, bot=" << bot->name << " null result (attempt " << attempt << "/" << kMaxRetries << ")";
            continue;
        }

        if (ret->result != static_cast<int>(chen::http::HttpResult::Error::OK)) {
            // 只对临时性错误重试
            bool retryable = ret->result == static_cast<int>(chen::http::HttpResult::Error::TIMEOUT)
                || ret->result == static_cast<int>(chen::http::HttpResult::Error::CONNECT_FAIL)
                || ret->result == static_cast<int>(chen::http::HttpResult::Error::SEND_CLOSE_BY_PEER)
                || ret->result == static_cast<int>(chen::http::HttpResult::Error::SEND_SOCKET_ERROR);
            if (!retryable) {
                ERROR(logger) << "send feishu message failed, bot=" << bot->name << " non-retryable error, data=" << ret->toString();
                return;
            }
            WARN(logger) << "send feishu message failed, bot=" << bot->name << " (attempt " << attempt << "/" << kMaxRetries
                         << "), data=" << ret->toString();
            continue;
        }

        // HTTP 200 不代表飞书业务成功，继续检查响应码
        int32_t code = 0;
        std::string message;
        if (ret->response) {
            const std::string& responseBody = ret->response->getBody();
            Json::Value response;
            if (chen::JsonUtil::FromString(response, responseBody)) {
                if (response.isMember("code")) {
                    code = response["code"].asInt();
                    message = response.get("msg", "").asString();
                } else if (response.isMember("StatusCode")) {
                    code = response["StatusCode"].asInt();
                    message = response.get("StatusMessage", "").asString();
                }
            } else if (!responseBody.empty()) {
                WARN(logger) << "send feishu message response is not json, bot=" << bot->name << " body=" << responseBody;
            }
        }

        if (code != 0) {
            ERROR(logger) << "send feishu message failed, bot=" << bot->name << " code=" << code << " message=" << message;
            return;
        }

        INFO(logger) << "send feishu message success, bot=" << bot->name;
        return;
    }

    ERROR(logger) << "send feishu message exhausted all retries, bot=" << bot->name;
}

} // namespace

FeishuBot::ptr FeishuBotManager::get(const std::string& name)
{
    std::string key = chen::StringUtil::ToLower(chen::StringUtil::Trim(name));
    if (key.empty()) {
        return nullptr;
    }

    // 每次发送直接读最新配置，不维护缓存
    auto botsConf = g_feishu_bots->getValue();
    for (const auto& conf : botsConf) {
        std::string confName = chen::StringUtil::ToLower(chen::StringUtil::Trim(conf.name));
        if (confName != key) {
            continue;
        }

        if (!conf.enabled) {
            INFO(logger) << "feishu bot disabled, skip: name=" << conf.name;
            return nullptr;
        }
        if (!IsValidIdentifier(key)) {
            ERROR(logger) << "feishu bot has invalid name, skip: name=" << conf.name;
            return nullptr;
        }

        std::string url = chen::StringUtil::Trim(conf.webhook_url);
        if (url.empty()) {
            WARN(logger) << "feishu bot webhook_url is empty, skip: name=" << conf.name;
            return nullptr;
        }
        if (!IsValidUrl(url)) {
            ERROR(logger) << "feishu bot has invalid webhook_url, skip: name=" << conf.name;
            return nullptr;
        }
        if (conf.enable_signature && conf.secret.empty()) {
            ERROR(logger) << "feishu bot signature enabled but secret is empty, skip: name=" << conf.name;
            return nullptr;
        }

        auto bot = std::make_shared<FeishuBot>();
        bot->name = key;
        bot->webhook_url = url;
        bot->secret = conf.secret;
        bot->enable_signature = conf.enable_signature;
        bot->enabled = conf.enabled;
        return bot;
    }

    return nullptr;
}

bool FeishuBotManager::send(const std::string& name, const Json::Value& payload)
{
    auto bot = get(name);
    if (!bot) {
        ERROR(logger) << "send feishu message failed, bot=" << name << " not found";
        return false;
    }
    return send(bot, payload);
}

bool FeishuBotManager::send(const FeishuBot::ptr& bot, const Json::Value& payload)
{
    if (!bot) {
        ERROR(logger) << "send feishu message failed, bot is null";
        return false;
    }

    chen::Scheduler* scheduler = chen::IOManager::GetThis();
    if (!scheduler) {
        scheduler = chen::Scheduler::GetThis();
    }
    if (!scheduler) {
        ERROR(logger) << "send feishu message failed, bot=" << bot->name << " no scheduler in current thread";
        return false;
    }

    scheduler->schedule([bot, payload]() {
        SendHttp(bot, payload);
    });
    return true;
}

bool FeishuBotManager::sendJson(const std::string& name, const std::string& json)
{
    Json::Value payload;
    if (!chen::JsonUtil::FromString(payload, json)) {
        ERROR(logger) << "send feishu message failed, bot=" << name << " invalid json";
        return false;
    }
    return send(name, payload);
}

bool FeishuBotManager::sendText(const std::string& name, const std::string& text)
{
    Json::Value payload;
    payload["msg_type"] = "text";
    payload["content"]["text"] = text;
    return send(name, payload);
}

bool FeishuBotManager::sendCard(const std::string& name, const Json::Value& cardJson)
{
    return send(name, cardJson);
}

} // namespace bot
