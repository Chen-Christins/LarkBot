/**
 * @file FeishuBotManager.hpp
 * @brief 飞书机器人管理：多机器人配置、按名字查找、签名与异步发送
 * @author Christins (chen.christins@qq.com)
 * @date 2026-10-21
 * @copyright Apache 2.0
 */
#pragma once

#include <chen/config/config.h>
#include <chen/util/json_util.h>
#include <chen/util/singleton.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace bot {

/**
 * @brief 飞书机器人配置，对应 feishu.bots 列表项
 */
struct FeishuBotConf {
    std::string name;
    std::string webhook_url;
    std::string secret;
    bool enable_signature = false;
    bool enabled = true;

    bool operator==(const FeishuBotConf& other) const
    {
        return name == other.name && webhook_url == other.webhook_url && secret == other.secret && enable_signature == other.enable_signature
            && enabled == other.enabled;
    }
};

} // namespace bot

namespace chen {

template <>
class LexicalCast<std::string, bot::FeishuBotConf> {
public:
    bot::FeishuBotConf operator()(const std::string& v)
    {
        YAML::Node node = YAML::Load(v);
        bot::FeishuBotConf conf;
        conf.name = node["name"].as<std::string>(conf.name);
        conf.webhook_url = node["webhook_url"].as<std::string>(conf.webhook_url);
        conf.secret = node["secret"].as<std::string>(conf.secret);
        conf.enable_signature = node["enable_signature"].as<bool>(conf.enable_signature);
        conf.enabled = node["enabled"].as<bool>(conf.enabled);
        return conf;
    }
};

template <>
class LexicalCast<bot::FeishuBotConf, std::string> {
public:
    std::string operator()(const bot::FeishuBotConf& conf)
    {
        YAML::Node node;
        node["name"] = conf.name;
        node["webhook_url"] = conf.webhook_url;
        node["secret"] = conf.secret;
        node["enable_signature"] = conf.enable_signature;
        node["enabled"] = conf.enabled;
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

} // namespace chen

namespace bot {

/**
 * @brief 单个飞书机器人配置
 */
struct FeishuBot {
    using ptr = std::shared_ptr<const FeishuBot>;

    std::string name;              // 机器人名字（业务 key，统一小写）
    std::string webhook_url;       // 飞书 Webhook 地址
    std::string secret;            // 签名密钥
    bool enable_signature = false; // 是否开启签名
    bool enabled = true;           // 是否启用
};

/**
 * @brief 飞书机器人管理器
 * @details 配置项：
 *   feishu.github_bot    GitHub Webhook 通知固定使用的机器人
 *   feishu.default_bot   未指定名字时使用的默认机器人
 *   feishu.bots          机器人列表
 *
 * 配置变更通过 ConfigVar 的 addListener 回调重建内存快照；发送失败只记录日志。
 */
class FeishuBotManager {
public:
    FeishuBotManager() = default;

    /**
     * @brief 初始化：构建快照并注册配置变更回调
     */
    void init();

    /**
     * @brief 停止：移除配置变更回调
     */
    void shutdown();

    /**
     * @brief 按名字查找机器人，不存在返回 nullptr
     */
    FeishuBot::ptr get(const std::string& name);

    /**
     * @brief 默认机器人
     */
    FeishuBot::ptr defaultBot();

    /**
     * @brief GitHub Webhook 通知使用的机器人
     */
    FeishuBot::ptr githubBot();

    /**
     * @brief 按名字异步发送消息
     * @param payload 完整的飞书消息 JSON（包含 msg_type）
     * @return true 表示已成功调度；实际发送结果见日志
     */
    bool send(const std::string& name, const Json::Value& payload);

    /**
     * @brief 向指定机器人异步发送消息
     */
    bool send(const FeishuBot::ptr& bot, const Json::Value& payload);

    /**
     * @brief 按名字发送序列化后的消息 JSON
     */
    bool sendJson(const std::string& name, const std::string& json);

    /**
     * @brief 按名字发送纯文本消息
     */
    bool sendText(const std::string& name, const std::string& text);

    /**
     * @brief 按名字发送卡片消息
     * @param name 机器人名称
     * @param cardJson 完整的卡片消息 JSON（LarkCardProtocol::getData 的结果）
     */
    bool sendCard(const std::string& name, const Json::Value& cardJson);

    /**
     * @brief 向 GitHub 通知机器人异步发送消息
     */
    bool sendGithub(const Json::Value& payload);

    bool sendGithubJson(const std::string& json);

private:
    struct Snapshot {
        using ptr = std::shared_ptr<const Snapshot>;

        std::map<std::string, FeishuBot::ptr> bots;
        std::string default_bot;
        std::string github_bot;
    };

    /**
     * @brief 获取当前配置快照
     */
    Snapshot::ptr snapshot();

    /**
     * @brief 从当前配置重建快照
     */
    void rebuildFromCurrent();

    /**
     * @brief 配置变更后延迟重建，避免在 ConfigVar 回调持锁期间读取配置
     */
    void scheduleRebuild();

    static Snapshot::ptr buildSnapshot(const std::vector<FeishuBotConf>& botsConf, const std::string& githubBot, const std::string& defaultBot);

private:
    std::mutex mutex_;
    Snapshot::ptr snapshot_;
    std::vector<std::function<void()>> listener_removers_;
    std::atomic<bool> rebuild_pending_{false};
};

using FeishuBotMgr = chen::Singleton<FeishuBotManager>;

} // namespace bot
