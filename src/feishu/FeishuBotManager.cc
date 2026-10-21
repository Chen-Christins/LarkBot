#include "FeishuBotManager.hpp"

#include <chen/config/config.h>
#include <chen/http/http_connection.h>
#include <chen/iomanager/iomanager.h>
#include <chen/log/log.h>
#include <chen/util/string_util.h>

#include <ctime>

#include "../Struct.hpp"
#include "../Utils.hpp"

namespace bot {

static chen::Logger::ptr logger = LOG_NAME("bot");

static chen::ConfigVar<std::vector<FeishuBotConf>>::ptr g_feishu_bots =
    chen::Config::Lookup<std::vector<FeishuBotConf>>("feishu.bots", std::vector<FeishuBotConf>{}, "飞书机器人列表");

static chen::ConfigVar<std::string>::ptr g_feishu_github_bot =
    chen::Config::Lookup<std::string>("feishu.github_bot", "", "GitHub 通知使用的飞书机器人");

static chen::ConfigVar<std::string>::ptr g_feishu_default_bot = chen::Config::Lookup<std::string>("feishu.default_bot", "", "默认飞书机器人");

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

void FeishuBotManager::init()
{
    if (!listener_removers_.empty()) {
        rebuildFromCurrent();
        return;
    }

    rebuildFromCurrent();

    auto onChange = [this](const auto&, const auto&) {
        scheduleRebuild();
    };
    auto addListener = [this, &onChange](auto var) {
        auto id = var->addListener(onChange);
        listener_removers_.push_back([var, id]() {
            var->delListener(id);
        });
    };

    addListener(g_feishu_bots);
    addListener(g_feishu_github_bot);
    addListener(g_feishu_default_bot);
}

void FeishuBotManager::shutdown()
{
    for (auto& remove : listener_removers_) {
        if (remove) {
            remove();
        }
    }
    listener_removers_.clear();
    rebuild_pending_ = false;
}

FeishuBot::ptr FeishuBotManager::get(const std::string& name)
{
    auto snap = snapshot();
    if (!snap) {
        return nullptr;
    }
    std::string key = chen::StringUtil::ToLower(chen::StringUtil::Trim(name));
    auto it = snap->bots.find(key);
    if (it == snap->bots.end()) {
        return nullptr;
    }
    return it->second;
}

FeishuBot::ptr FeishuBotManager::defaultBot()
{
    auto snap = snapshot();
    if (!snap || snap->default_bot.empty()) {
        return nullptr;
    }
    auto it = snap->bots.find(snap->default_bot);
    if (it == snap->bots.end()) {
        return nullptr;
    }
    return it->second;
}

FeishuBot::ptr FeishuBotManager::githubBot()
{
    auto snap = snapshot();
    if (!snap || snap->github_bot.empty()) {
        return nullptr;
    }
    auto it = snap->bots.find(snap->github_bot);
    if (it == snap->bots.end()) {
        return nullptr;
    }
    return it->second;
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

bool FeishuBotManager::sendGithub(const Json::Value& payload)
{
    auto snap = snapshot();
    if (!snap || snap->github_bot.empty()) {
        ERROR(logger) << "send github feishu message failed, github bot is not configured";
        return false;
    }
    auto it = snap->bots.find(snap->github_bot);
    if (it == snap->bots.end()) {
        ERROR(logger) << "send github feishu message failed, bot=" << snap->github_bot << " not found";
        return false;
    }
    return send(it->second, payload);
}

bool FeishuBotManager::sendGithubJson(const std::string& json)
{
    Json::Value payload;
    if (!chen::JsonUtil::FromString(payload, json)) {
        ERROR(logger) << "send github feishu message failed, invalid json";
        return false;
    }
    return sendGithub(payload);
}

FeishuBotManager::Snapshot::ptr FeishuBotManager::snapshot()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

void FeishuBotManager::rebuildFromCurrent()
{
    auto snap = buildSnapshot(g_feishu_bots->getValue(), g_feishu_github_bot->getValue(), g_feishu_default_bot->getValue());

    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = snap;
    }

    INFO(logger) << "feishu bots loaded: count=" << snap->bots.size() << " default=" << snap->default_bot << " github=" << snap->github_bot;
    for (const auto& [name, bot] : snap->bots) {
        INFO(logger) << "feishu bot: name=" << name << " enabled=" << (bot->enabled ? "true" : "false")
                     << " signature=" << (bot->enable_signature ? "true" : "false");
    }
}

void FeishuBotManager::scheduleRebuild()
{
    chen::Scheduler* scheduler = chen::IOManager::GetThis();
    if (!scheduler) {
        scheduler = chen::Scheduler::GetThis();
    }
    if (!scheduler) {
        WARN(logger) << "feishu config changed but no scheduler in current thread, skip rebuild";
        return;
    }
    if (rebuild_pending_.exchange(true)) {
        return;
    }

    scheduler->schedule([this]() {
        rebuild_pending_.store(false);
        rebuildFromCurrent();
    });
}

FeishuBotManager::Snapshot::ptr FeishuBotManager::buildSnapshot(
    const std::vector<FeishuBotConf>& botsConf,
    const std::string& githubBot,
    const std::string& defaultBot)
{
    auto snap = std::make_shared<Snapshot>();

    for (const auto& conf : botsConf) {
        std::string rawName = chen::StringUtil::Trim(conf.name);
        std::string name = chen::StringUtil::ToLower(rawName);
        std::string url = chen::StringUtil::Trim(conf.webhook_url);
        std::string secret = conf.secret;
        bool enableSignature = conf.enable_signature;
        bool enabled = conf.enabled;

        if (!enabled) {
            INFO(logger) << "feishu bot disabled, skip: name=" << rawName;
            continue;
        }
        if (!IsValidIdentifier(name)) {
            ERROR(logger) << "feishu bot has invalid name, skip: name=" << rawName;
            continue;
        }
        if (snap->bots.find(name) != snap->bots.end()) {
            ERROR(logger) << "duplicate feishu bot name, skip: name=" << name;
            continue;
        }
        if (url.empty()) {
            WARN(logger) << "feishu bot webhook_url is empty, skip: name=" << name;
            continue;
        }
        if (!IsValidUrl(url)) {
            ERROR(logger) << "feishu bot has invalid webhook_url, skip: name=" << name;
            continue;
        }
        if (enableSignature && secret.empty()) {
            ERROR(logger) << "feishu bot signature enabled but secret is empty, skip: name=" << name;
            continue;
        }

        auto bot = std::make_shared<FeishuBot>();
        bot->name = name;
        bot->webhook_url = url;
        bot->secret = secret;
        bot->enable_signature = enableSignature;
        bot->enabled = enabled;
        snap->bots[name] = bot;
    }

    if (snap->bots.empty()) {
        WARN(logger) << "no feishu bots configured";
    }

    // 默认机器人
    std::string defaultName = chen::StringUtil::ToLower(chen::StringUtil::Trim(defaultBot));
    if (defaultName.empty()) {
        if (snap->bots.find("default") != snap->bots.end()) {
            defaultName = "default";
        } else if (!snap->bots.empty()) {
            defaultName = snap->bots.begin()->first;
        }
    }
    if (!defaultName.empty() && snap->bots.find(defaultName) == snap->bots.end()) {
        WARN(logger) << "feishu default_bot not found: " << defaultName << ", fallback to default";
        if (snap->bots.find("default") != snap->bots.end()) {
            defaultName = "default";
        } else if (!snap->bots.empty()) {
            defaultName = snap->bots.begin()->first;
        } else {
            defaultName.clear();
        }
    }
    snap->default_bot = defaultName;

    // GitHub 通知机器人
    std::string githubName = chen::StringUtil::ToLower(chen::StringUtil::Trim(githubBot));
    if (githubName.empty()) {
        githubName = snap->default_bot;
    } else if (snap->bots.find(githubName) == snap->bots.end()) {
        WARN(logger) << "feishu github_bot not found: " << githubName << ", fallback to default=" << snap->default_bot;
        githubName = snap->default_bot;
    }
    snap->github_bot = githubName;

    return snap;
}

} // namespace bot
