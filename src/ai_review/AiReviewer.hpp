/**
 * @file AiReviewer.hpp
 * @brief AI 代码审查 — 自动对 PR 进行 AI 审查并发表评论
 * @author Christins (chen.christins@qq.com)
 * @date 2026-06-28
 * @copyright Apache 2.0
 */
#pragma once

#include <json/json.h>

#include <memory>
#include <string>

namespace bot {

class AiReviewer {
public:
    typedef std::shared_ptr<AiReviewer> ptr;

    /**
     * @brief 对 PR 执行 AI 审查（异步，调度到 IOManager）
     * @param payload PR webhook 的完整 JSON payload
     */
    static void reviewPullRequest(const Json::Value& payload);

private:
    /// 生成 GitHub App JWT（有效期 10 分钟）
    static std::string generateJWT(const std::string& appId, const std::string& privateKeyPath);

    /// 用 JWT 换取安装访问令牌
    static std::string getInstallationToken(const std::string& jwt, int64_t installationId);

    /// 获取 PR 的代码变更（所有文件的 patch）
    static std::string getPRDiff(const std::string& token, const std::string& owner,
                                 const std::string& repo, int prNumber);

    /// 调用 AI 进行代码审查
    static std::string callAIReview(const std::string& diff, const std::string& title,
                                    const std::string& body, const std::string& repoName, int prNumber);

    /// 向 PR 发表审查评论
    static void postReviewComment(const std::string& token, const std::string& owner,
                                  const std::string& repo, int prNumber, const std::string& review);

    /// 构建 AI 审查的 prompt
    static std::string buildReviewPrompt(const std::string& diff, const std::string& title,
                                         const std::string& body, const std::string& repoName, int prNumber);
};

} // namespace bot
