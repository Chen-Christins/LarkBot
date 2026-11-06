/**
 * @file AiReviewer.hpp
 * @brief AI 代码审查 — 自动对 PR 进行 AI 审查并发表评论
 * @author Christins (chen.christins@qq.com)
 * @date 2026-06-28
 * @copyright Apache 2.0
 */
#pragma once

#include <memory>
#include <sstream>
#include <string>

#include <chen/config/config.h>
#include <chen/util/json_util.h>

namespace bot {

/**
 * @brief GitHub App 配置，对应 github.*
 */
struct GithubAppConf {
    std::string app_id;
    std::string private_key_path;

    bool operator==(const GithubAppConf& other) const { return app_id == other.app_id && private_key_path == other.private_key_path; }
};

/**
 * @brief AI Review 配置，对应 ai_review.*
 */
struct AIReviewConf {
    bool enabled = false;
    std::string provider = "openai";
    std::string api_key;
    std::string api_url = "https://api.openai.com/v1";
    std::string model = "gpt-4o";
    int32_t max_tokens = 4096;
    std::string review_trigger = "opened,synchronize";
    std::string review_comment_mode = "update";

    bool operator==(const AIReviewConf& other) const
    {
        return enabled == other.enabled && provider == other.provider && api_key == other.api_key && api_url == other.api_url && model == other.model
            && max_tokens == other.max_tokens && review_trigger == other.review_trigger && review_comment_mode == other.review_comment_mode;
    }
};

} // namespace bot

namespace chen {

template <>
class LexicalCast<std::string, bot::GithubAppConf> {
public:
    bot::GithubAppConf operator()(const std::string& v)
    {
        YAML::Node node = YAML::Load(v);
        bot::GithubAppConf conf;
        conf.app_id = node["app_id"].as<std::string>(conf.app_id);
        conf.private_key_path = node["private_key_path"].as<std::string>(conf.private_key_path);
        return conf;
    }
};

template <>
class LexicalCast<bot::GithubAppConf, std::string> {
public:
    std::string operator()(const bot::GithubAppConf& conf)
    {
        YAML::Node node;
        node["app_id"] = conf.app_id;
        node["private_key_path"] = conf.private_key_path;
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

template <>
class LexicalCast<std::string, bot::AIReviewConf> {
public:
    bot::AIReviewConf operator()(const std::string& v)
    {
        YAML::Node node = YAML::Load(v);
        bot::AIReviewConf conf;
        conf.enabled = chen::LexicalCast<std::string, bool>()(node["enabled"].Scalar());
        conf.provider = node["provider"].as<std::string>(conf.provider);
        conf.api_key = node["api_key"].as<std::string>(conf.api_key);
        conf.api_url = node["api_url"].as<std::string>(conf.api_url);
        conf.model = node["model"].as<std::string>(conf.model);
        conf.max_tokens = node["max_tokens"].as<int32_t>(conf.max_tokens);
        conf.review_trigger = node["review_trigger"].as<std::string>(conf.review_trigger);
        conf.review_comment_mode = node["review_comment_mode"].as<std::string>(conf.review_comment_mode);
        return conf;
    }
};

template <>
class LexicalCast<bot::AIReviewConf, std::string> {
public:
    std::string operator()(const bot::AIReviewConf& conf)
    {
        YAML::Node node;
        node["enabled"] = conf.enabled;
        node["provider"] = conf.provider;
        node["api_key"] = conf.api_key;
        node["api_url"] = conf.api_url;
        node["model"] = conf.model;
        node["max_tokens"] = conf.max_tokens;
        node["review_trigger"] = conf.review_trigger;
        node["review_comment_mode"] = conf.review_comment_mode;
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

} // namespace chen

namespace bot {

class AIReviewer {
public:
    typedef std::shared_ptr<AIReviewer> ptr;

    /**
     * @brief 获取当前 AI Review 配置（每次从 ConfigVar 读取，支持热更新）
     */
    static AIReviewConf Config();

    /**
     * @brief 对 PR 执行 AI 审查（异步，调度到 IOManager）
     * @param payload PR webhook 的完整 JSON payload
     */
    static void ReviewPullRequest(const Json::Value& payload);

private:
    /// 生成 GitHub App JWT（有效期 10 分钟）
    static std::string GenerateJWT(const std::string& appId, const std::string& privateKeyPath);

    /// 用 JWT 换取安装访问令牌
    static std::string GetInstallationToken(const std::string& jwt, int64_t installationId);

    /// 获取 PR 的代码变更（所有文件的 patch）
    static std::string GetPRDiff(const std::string& token, const std::string& owner, const std::string& repo, int prNumber);

    /// 调用 AI 进行代码审查
    static std::string CallAIReview(
        const std::string& diff,
        const std::string& title,
        const std::string& body,
        const std::string& repoName,
        int prNumber);

    /// 向 PR 发表审查评论
    static void PostReviewComment(
        const std::string& token,
        const std::string& owner,
        const std::string& repo,
        int prNumber,
        const std::string& review);

    /// 构建 AI 审查的 prompt
    static std::string BuildReviewPrompt(
        const std::string& diff,
        const std::string& title,
        const std::string& body,
        const std::string& repoName,
        int prNumber);
};

} // namespace bot
