#include "AiReviewer.hpp"

#include "../ai/AIProvider.hpp"

#include <chen/config/config.h>
#include <chen/http/http_connection.h>
#include <chen/iomanager/iomanager.h>
#include <chen/log/log.h>
#include <chen/schedule/schedule.h>
#include <chen/util/encryptor_util.h>
#include <chen/util/util.h>

#include <sstream>

namespace bot {

static chen::Logger::ptr logger = LOG_NAME("bot");

static chen::ConfigVar<std::string>::ptr g_github_app_id =
    chen::Config::Lookup<std::string>("github.app_id", "", "GitHub App ID");

static chen::ConfigVar<std::string>::ptr g_github_private_key_path =
    chen::Config::Lookup<std::string>("github.private_key_path", "", "GitHub App 私钥文件路径");

static chen::ConfigVar<std::string>::ptr g_ai_provider =
    chen::Config::Lookup<std::string>("ai_review.provider", "openai", "AI 厂商: openai / claude");

static chen::ConfigVar<std::string>::ptr g_ai_api_key =
    chen::Config::Lookup<std::string>("ai_review.api_key", "", "AI API Key");

static chen::ConfigVar<std::string>::ptr g_ai_api_url =
    chen::Config::Lookup<std::string>("ai_review.api_url", "https://api.openai.com/v1", "AI API 基础地址");

static chen::ConfigVar<std::string>::ptr g_ai_model =
    chen::Config::Lookup<std::string>("ai_review.model", "gpt-4o", "AI 模型名");

static chen::ConfigVar<int32_t>::ptr g_ai_max_tokens =
    chen::Config::Lookup<int32_t>("ai_review.max_tokens", 4096, "AI 最大输出 token");

static chen::ConfigVar<std::string>::ptr g_ai_review_trigger =
    chen::Config::Lookup<std::string>("ai_review.review_trigger", "opened,synchronize", "触发 review 的 PR action 列表");

static chen::ConfigVar<std::string>::ptr g_ai_review_comment_mode =
    chen::Config::Lookup<std::string>("ai_review.review_comment_mode", "update", "评论模式: update(增量) / append(每次新评论)");

std::string AiReviewer::generateJWT(const std::string& appId, const std::string& privateKeyPath) {
    if (appId.empty()) {
        ERROR(logger) << "generateJWT: github.app_id is not configured";
        return "";
    }
    if (privateKeyPath.empty()) {
        ERROR(logger) << "generateJWT: github.private_key_path is not configured";
        return "";
    }

    // 读取私钥
    std::string pem = chen::FSUtil::ReadFileToString(privateKeyPath);
    if (pem.empty()) {
        ERROR(logger) << "generateJWT: failed to read private key file: " << privateKeyPath;
        return "";
    }

    // 构建 JWT header: {"alg":"RS256","typ":"JWT"}
    Json::Value header;
    header["alg"] = "RS256";
    header["typ"] = "JWT";

    // 构建 JWT payload: {"iat":now,"exp":now+600,"iss":appId}
    int64_t now = static_cast<int64_t>(time(nullptr));
    Json::Value payload;
    payload["iat"] = static_cast<Json::Value::Int64>(now);
    payload["exp"] = static_cast<Json::Value::Int64>(now + 600);
    payload["iss"] = appId;

    std::string headerB64 = chen::StringUtil::Base64UrlEncode(chen::JsonUtil::ToString(header));
    std::string payloadB64 = chen::StringUtil::Base64UrlEncode(chen::JsonUtil::ToString(payload));

    // 签名
    std::string signingInput = headerB64 + "." + payloadB64;
    std::string sig = chen::EncryptorUtil::RS256Sign(signingInput, pem);
    if (sig.empty()) {
        return "";
    }
    std::string sigB64 = chen::StringUtil::Base64UrlEncode(sig);

    return headerB64 + "." + payloadB64 + "." + sigB64;
}

/// GitHub API 基础地址
static const char* kGithubApiBase = "https://api.github.com";

/// 执行一次 GitHub API 请求（带重试），返回响应 body
static std::string githubApiCall(const std::string& method, const std::string& url
        , const std::string& token, const std::string& body, int64_t timeoutMs = 30000) {
    auto headers = std::map<std::string, std::string>{
        {"Authorization", "Bearer " + token},
        {"Accept", "application/vnd.github+json"},
        {"User-Agent", "LarkBot/1.0"},
    };
    if (!body.empty()) {
        headers["Content-Type"] = "application/json";
    }

    chen::http::HttpMethod httpMethod = chen::http::HttpMethod::GET;
    if (method == "POST") {
        httpMethod = chen::http::HttpMethod::POST;
    } else if (method == "PATCH") {
        httpMethod = chen::http::HttpMethod::PATCH;
    }

    constexpr int kMaxRetries = 3;
    for (int attempt = 1; attempt <= kMaxRetries; ++attempt) {
        auto ret = chen::http::HttpConnection::DoRequest(httpMethod, url, timeoutMs, headers, body);
        if (!ret) {
            WARN(logger) << "GitHub API request failed (null result), attempt " << attempt << "/" << kMaxRetries
                << ", url=" << url;
            continue;
        }
        if (ret->result == static_cast<int>(chen::http::HttpResult::Error::OK)) {
            if (ret->response) {
                return ret->response->getBody();
            }
            WARN(logger) << "GitHub API request no response, attempt " << attempt << "/" << kMaxRetries
                << ", url=" << url;
            continue;
        }
        // 只对临时性错误重试
        if (ret->result != static_cast<int>(chen::http::HttpResult::Error::TIMEOUT)
                && ret->result != static_cast<int>(chen::http::HttpResult::Error::CONNECT_FAIL)
                && ret->result != static_cast<int>(chen::http::HttpResult::Error::SEND_CLOSE_BY_PEER)
                && ret->result != static_cast<int>(chen::http::HttpResult::Error::SEND_SOCKET_ERROR)) {
            ERROR(logger) << "GitHub API request failed, non-retryable, url=" << url
                << " result=" << ret->result << " body=" << ret->toString();
            return "";
        }
        WARN(logger) << "GitHub API request failed, attempt " << attempt << "/" << kMaxRetries
            << " result=" << ret->result << " url=" << url;
    }
    ERROR(logger) << "GitHub API request exhausted all retries, url=" << url;
    return "";
}

std::string AiReviewer::getInstallationToken(const std::string& jwt, int64_t installationId) {
    std::string url = std::string(kGithubApiBase)
        + "/app/installations/" + std::to_string(installationId) + "/access_tokens";

    std::string resp = githubApiCall("POST", url, jwt, "", 30000);
    if (resp.empty()) {
        return "";
    }

    Json::Value parsed;
    if (!chen::JsonUtil::FromString(parsed, resp)) {
        ERROR(logger) << "getInstallationToken: failed to parse response JSON, body=" << resp;
        return "";
    }

    std::string token = parsed["token"].asString();
    if (token.empty()) {
        ERROR(logger) << "getInstallationToken: no token in response, body=" << resp;
        return "";
    }

    DEBUG(logger) << "getInstallationToken: got token (installation_id=" << installationId << ")";
    return token;
}

std::string AiReviewer::getPRDiff(const std::string& token, const std::string& owner
        , const std::string& repo, int prNumber) {
    std::string url = std::string(kGithubApiBase) + "/repos/" + owner + "/" + repo + "/pulls/" + std::to_string(prNumber) + "/files";

    // 用带有 diff 格式的 Accept header 获取 patch
    auto headers = std::map<std::string, std::string>{
        {"Authorization", "Bearer " + token},
        {"Accept", "application/vnd.github.v3.diff"},
        {"User-Agent", "LarkBot/1.0"},
    };

    auto ret = chen::http::HttpConnection::DoRequest(chen::http::HttpMethod::GET, url, 30000, headers, "");
    if (!ret || ret->result != static_cast<int>(chen::http::HttpResult::Error::OK)) {
        // 回退：用 JSON 格式获取，然后提取 patch 字段
        WARN(logger) << "getPRDiff: diff format failed, falling back to JSON format, url=" << url;
        headers["Accept"] = "application/vnd.github+json";

        ret = chen::http::HttpConnection::DoRequest(chen::http::HttpMethod::GET, url, 30000, headers, "");
        
        if (!ret || ret->result != static_cast<int>(chen::http::HttpResult::Error::OK)) {
            ERROR(logger) << "getPRDiff: both diff and JSON format failed";
            return "";
        }
        // 从 JSON 中提取每个文件的 patch
        std::string resp = ret->response->getBody();
        Json::Value files;
        if (!chen::JsonUtil::FromString(files, resp) || !files.isArray()) {
            ERROR(logger) << "getPRDiff: failed to parse JSON response";
            return "";
        }
        std::ostringstream oss;
        for (const auto& file : files) {
            std::string filename = file["filename"].asString();
            std::string status = file["status"].asString();
            std::string patch = file["patch"].asString();
            int additions = file["additions"].asInt();
            int deletions = file["deletions"].asInt();
            oss << "## " << filename << " (" << status
                << ", +" << additions << "/-" << deletions << ")\n";
            if (!patch.empty()) {
                oss << patch << "\n\n";
            } else {
                oss << "(binary or empty file)\n\n";
            }
        }
        return oss.str();
    }

    // diff 格式成功，直接返回
    if (!ret->response) return "";
    return ret->response->getBody();
}

/// 从 AI 审查报告中提取 📋 变更摘要 部分
static std::string extractSummaryFromReview(const std::string& review) {
    std::string marker = "### 📋 变更摘要";
    auto start = review.find(marker);
    if (start == std::string::npos) {
        // 兼容旧格式：取前 200 字符作为摘要
        return review.substr(0, 200);
    }
    start += marker.size();
    auto end = review.find("### 审查详情", start);
    if (end == std::string::npos) {
        end = review.find("### 评分", start);
    }
    if (end == std::string::npos) {
        return review.substr(start);
    }
    std::string summary = review.substr(start, end - start);
    // 去掉首尾空白
    while (!summary.empty() && (summary.back() == ' ' || summary.back() == '\n' || summary.back() == '\r')) {
        summary.pop_back();
    }
    return summary;
}

/// 更新 PR 描述，追加/刷新 AI 摘要
static void updatePRSummary(const std::string& token, const std::string& owner
        , const std::string& repo, int prNumber, const std::string& review) {
    // 获取当前 PR 描述
    std::string url = std::string(kGithubApiBase)
        + "/repos/" + owner + "/" + repo + "/pulls/" + std::to_string(prNumber);
    std::string resp = githubApiCall("GET", url, token, "", 30000);
    if (resp.empty()) {
        ERROR(logger) << "updatePRSummary: failed to get PR #" << prNumber;
        return;
    }
    Json::Value prData;
    if (!chen::JsonUtil::FromString(prData, resp)) {
        ERROR(logger) << "updatePRSummary: failed to parse PR data";
        return;
    }

    std::string currentBody = prData["body"].asString();
    std::string summary = extractSummaryFromReview(review);
    if (summary.empty()) {
        WARN(logger) << "updatePRSummary: empty summary, skip";
        return;
    }

    // 构造新的 PR 描述
    std::string aiSection = "\n\n---\n## 🤖 AI 审查摘要\n" + summary + "\n";
    std::string newBody;
    std::string aiMarker = "## 🤖 AI 审查摘要";
    auto aiPos = currentBody.find(aiMarker);
    if (aiPos != std::string::npos) {
        // 替换已有 AI 摘要段落
        newBody = currentBody.substr(0, aiPos);
        // 去掉尾部多余的空白
        while (!newBody.empty() && (newBody.back() == ' ' || newBody.back() == '\n' || newBody.back() == '\r')) {
            newBody.pop_back();
        }
        newBody += aiSection;
    } else {
        // 追加
        newBody = currentBody + aiSection;
    }

    Json::Value patchBody;
    patchBody["body"] = newBody;
    std::string bodyStr = chen::JsonUtil::ToString(patchBody);
    resp = githubApiCall("PATCH", url, token, bodyStr, 30000);
    if (resp.empty()) {
        ERROR(logger) << "updatePRSummary: failed to update PR #" << prNumber;
        return;
    }
    INFO(logger) << "updatePRSummary: PR #" << prNumber << " description updated";
}

/// 查找 PR 上已有的 AI review 评论 ID，未找到返回 -1
static int64_t findExistingReviewComment(const std::string& token, const std::string& owner
        , const std::string& repo, int prNumber) {
    std::string url = std::string(kGithubApiBase)
        + "/repos/" + owner + "/" + repo + "/issues/" + std::to_string(prNumber) + "/comments";

    std::string resp = githubApiCall("GET", url, token, "", 30000);
    if (resp.empty()) return -1;

    Json::Value comments;
    if (!chen::JsonUtil::FromString(comments, resp) || !comments.isArray()) return -1;

    for (const auto& c : comments) {
        std::string body = c["body"].asString();
        if (body.find("🤖 AI 代码审查报告") != std::string::npos) {
            return c["id"].asInt64();
        }
    }
    return -1;
}

void AiReviewer::postReviewComment(const std::string& token, const std::string& owner
        , const std::string& repo, int prNumber, const std::string& review) {
    std::string mode = g_ai_review_comment_mode->getValue();

    Json::Value body;
    body["body"] = review;
    std::string bodyStr = chen::JsonUtil::ToString(body);

    if (mode == "update") {
        // 查找已有 AI review 评论并更新
        int64_t commentId = findExistingReviewComment(token, owner, repo, prNumber);
        if (commentId > 0) {
            std::string url = std::string(kGithubApiBase)
                + "/repos/" + owner + "/" + repo + "/issues/comments/" + std::to_string(commentId);
            std::string resp = githubApiCall("PATCH", url, token, bodyStr, 30000);
            if (resp.empty()) {
                ERROR(logger) << "postReviewComment: failed to update comment " << commentId << " for PR #" << prNumber;
                return;
            }
            INFO(logger) << "postReviewComment: comment " << commentId << " updated for PR #" << prNumber;
            return;
        }
        // 未找到已有评论，回退到新建
        INFO(logger) << "postReviewComment: no existing AI review comment found, creating new one for PR #" << prNumber;
    }

    // append 模式或 update 模式未找到已有评论：新建
    std::string url = std::string(kGithubApiBase)
        + "/repos/" + owner + "/" + repo + "/issues/" + std::to_string(prNumber) + "/comments";
    std::string resp = githubApiCall("POST", url, token, bodyStr, 30000);
    if (resp.empty()) {
        ERROR(logger) << "postReviewComment: failed to post comment for PR #" << prNumber;
        return;
    }
    INFO(logger) << "postReviewComment: comment posted for PR #" << prNumber;
}

std::string AiReviewer::buildReviewPrompt(const std::string& diff, const std::string& title
        , const std::string& body, const std::string& repoName, int prNumber) {
    std::stringstream prompt;
    prompt << "你是一个资深的代码审查专家。请对以下 Pull Request 进行代码审查。\n\n"
           << "## PR 信息\n"
           << "- 仓库: " << repoName << "\n"
           << "- PR #" << prNumber << "\n"
           << "- 标题: " << title << "\n"
           << "- 描述: " << (body.empty() ? "(无描述)" : body) << "\n\n"
           << "## 代码变更\n"
           << "```diff\n"
           << diff
           << "\n```\n\n"
           << "## 审查要求\n"
           << "请从以下维度进行分析，如果某个维度没有问题则跳过：\n"
           << "1. **逻辑错误** — 是否存在潜在的 bug、边界条件未处理等问题\n"
           << "2. **安全风险** — 是否存在 SQL 注入、XSS、敏感信息泄露等安全问题\n"
           << "3. **性能问题** — 是否存在不必要的循环、内存泄漏、数据库查询过多等性能隐患\n"
           << "4. **代码质量** — 可读性、复杂度、重复代码、命名规范、错误处理等\n"
           << "5. **兼容性破坏** — 是否有破坏向后兼容性的变更\n\n"
           << "## 输出格式\n"
           << "请严格按照以下 Markdown 结构输出：\n\n"
           << "### 📋 变更摘要\n"
           << "用 2-4 条要点概括 PR 的核心变更，每条 15 字以内。例如：\n"
           << "\n"
           << "- 修复了 XX 场景下的空指针崩溃\n"
           << "- 重构了 XX 模块，拆分出 XX 类\n"
           << "- 新增了 XX 接口支持 XX 功能\n"
           << "\n"
           << "### 审查详情\n"
           << "然后按维度列出发现的问题，每个问题请标注：\n"
           << "- 严重程度: 🔴 严重 / 🟡 中等 / 🟢 建议\n"
           << "- 文件位置: 具体的文件名和行号（如果有）\n"
           << "- 问题描述与改进建议\n\n"
           << "### 评分\n"
           << "末尾给出综合评分：\n"
           << "**综合评分: XX/100**\n"
           << "评分依据：...\n"
           << "评分参考标准：\n"
           << "- 90-100: 代码质量优秀，仅少量改进建议\n"
           << "- 70-89: 代码基本良好，有一些小问题\n"
           << "- 50-69: 存在明显问题，需要改进\n"
           << "- 0-49: 存在严重问题，建议重大调整\n\n"
           << "如果代码整体质量良好，也请在最后给出正面评价。";

    return prompt.str();
}

std::string AiReviewer::callAIReview(const std::string& diff, const std::string& title
        , const std::string& body, const std::string& repoName, int prNumber) {
    std::string providerType = g_ai_provider->getValue();
    std::string apiKey = g_ai_api_key->getValue();
    std::string apiUrl = g_ai_api_url->getValue();
    std::string model = g_ai_model->getValue();
    int32_t maxTokens = g_ai_max_tokens->getValue();

    if (apiKey.empty()) {
        ERROR(logger) << "callAIReview: ai_review.api_key is not configured";
        return "❌ AI 审查失败: API Key 未配置";
    }

    // 通过工厂创建对应的 AI 厂商适配器
    auto aiProvider = blog::ai::createProvider(providerType);
    if (!aiProvider) {
        ERROR(logger) << "callAIReview: failed to create provider for type=" << providerType;
        return "❌ AI 审查失败: 不支持的 AI 厂商";
    }

    // 构建请求
    std::string systemPrompt = "你是一个资深代码审查专家，擅长发现代码中的问题并提供改进建议。";
    std::string userPrompt = buildReviewPrompt(diff, title, body, repoName, prNumber);
    std::string requestBody = aiProvider->buildRequest(model, maxTokens, systemPrompt, userPrompt, false);

    std::string endpoint = apiUrl + aiProvider->endpointSuffix();
    auto headers = std::map<std::string, std::string>{
        {"Content-Type", "application/json"},
        {"User-Agent", "LarkBot/1.0"},
    };
    aiProvider->authHeaders(apiKey, headers);

    DEBUG(logger) << "callAIReview: provider=" << providerType << " model=" << model << " endpoint=" << endpoint;

    // 发起请求（带重试）
    constexpr int kAiMaxRetries = 3;
    std::string reviewText;
    for (int attempt = 1; attempt <= kAiMaxRetries; ++attempt) {
        auto ret = chen::http::HttpConnection::DoRequest(chen::http::HttpMethod::POST, endpoint, 120000, headers, requestBody);
        if (!ret || ret->result != static_cast<int>(chen::http::HttpResult::Error::OK)) {
            if (!ret || (ret->result != static_cast<int>(chen::http::HttpResult::Error::TIMEOUT)
                    && ret->result != static_cast<int>(chen::http::HttpResult::Error::CONNECT_FAIL)
                    && ret->result != static_cast<int>(chen::http::HttpResult::Error::SEND_CLOSE_BY_PEER)
                    && ret->result != static_cast<int>(chen::http::HttpResult::Error::SEND_SOCKET_ERROR))) {
                std::string errInfo = ret ? ret->toString() : "null";
                ERROR(logger) << "callAIReview: request failed, non-retryable, result="
                    << (ret ? std::to_string(ret->result) : "null") << " body=" << errInfo;
                return "❌ AI 审查请求失败，请稍后重试。";
            }
            WARN(logger) << "callAIReview: request failed, attempt " << attempt << "/" << kAiMaxRetries
                << " result=" << (ret ? std::to_string(ret->result) : "null");
            continue;
        }

        // 检查 HTTP 状态码
        if (ret->response) {
            int httpStatus = static_cast<int>(ret->response->getStatus());
            if (httpStatus == 429 || httpStatus >= 500) {
                // 429 限流 / 5xx 服务端错误 — 可重试
                WARN(logger) << "callAIReview: HTTP " << httpStatus << ", attempt " << attempt << "/" << kAiMaxRetries;
                continue;
            }
            if (httpStatus >= 400) {
                ERROR(logger) << "callAIReview: HTTP " << httpStatus << " body=" << ret->response->getBody();
                return "❌ AI 审查服务返回错误 (HTTP " + std::to_string(httpStatus) + ")";
            }
        }

        // 解析响应
        std::string respBody = ret->response ? ret->response->getBody() : "";
        Json::Value parsed;
        if (!chen::JsonUtil::FromString(parsed, respBody)) {
            ERROR(logger) << "callAIReview: failed to parse JSON response";
            return "❌ AI 审查响应解析失败。";
        }

        // 提取非流式完整内容
        reviewText = aiProvider->extractNonStreamingContent(parsed);
        if (reviewText.empty()) {
            // 检查是否有错误信息
            std::string errMsg = aiProvider->getError(parsed);
            if (!errMsg.empty()) {
                ERROR(logger) << "callAIReview: API error: " << errMsg;
                return "❌ AI 审查失败: " + errMsg;
            }
            // 空内容（非错误）— 可重试
            WARN(logger) << "callAIReview: empty review, attempt " << attempt << "/" << kAiMaxRetries;
            continue;
        }
        // 成功拿到审查结果
        break;
    }

    if (reviewText.empty()) {
        ERROR(logger) << "callAIReview: exhausted all retries";
        return "❌ AI 审查请求多次重试后仍然失败。";
    }

    INFO(logger) << "callAIReview: received review (" << reviewText.size() << " chars)";
    return reviewText;
}

void AiReviewer::reviewPullRequest(const Json::Value& payload) {
    // 从 payload 中提取必要信息
    std::string action = payload.get("action", "").asString();

    // 检查是否在触发列表中
    std::string triggerStr = g_ai_review_trigger->getValue();
    if (triggerStr.empty()) {
        INFO(logger) << "reviewPullRequest: review_trigger is empty, skip";
        return;
    }
    // 解析 trigger list
    bool shouldReview = false;
    std::istringstream iss(triggerStr);
    std::string token;
    while (std::getline(iss, token, ',')) {
        // 去除空白
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        if (token == action) {
            shouldReview = true;
            break;
        }
    }
    if (!shouldReview) {
        INFO(logger) << "reviewPullRequest: action=" << action << " not in trigger list, skip";
        return;
    }

    const Json::Value& pr = payload["pull_request"];
    const Json::Value& repo = payload["repository"];
    int64_t installationId = payload["installation"]["id"].asInt64();

    if (installationId == 0) {
        ERROR(logger) << "reviewPullRequest: no installation id in payload";
        return;
    }

    int number = pr.get("number", 0).asInt();
    std::string title = pr.get("title", "").asString();
    std::string body = pr.get("body", "").asString();
    std::string fullName = repo.get("full_name", "").asString();
    auto slashPos = fullName.find('/');
    std::string owner, repoName;
    if (slashPos != std::string::npos) {
        owner = fullName.substr(0, slashPos);
        repoName = fullName.substr(slashPos + 1);
    } else {
        ERROR(logger) << "reviewPullRequest: invalid repository full_name: " << fullName;
        return;
    }

    // 异步调度到 IO 线程池执行
    chen::Scheduler::GetThis()->schedule([owner, repoName, number, title, body, installationId]() {
        INFO(logger) << "AI review started for " << owner << "/" << repoName << " PR #" << number;

        // 1. 生成 JWT
        std::string appId = g_github_app_id->getValue();
        std::string privateKeyPath = g_github_private_key_path->getValue();
        std::string jwt = AiReviewer::generateJWT(appId, privateKeyPath);
        if (jwt.empty()) {
            ERROR(logger) << "PR #" << number << ": failed to generate JWT, skip review";
            return;
        }

        // 2. 换取 Installation Token
        std::string token = AiReviewer::getInstallationToken(jwt, installationId);
        if (token.empty()) {
            ERROR(logger) << "PR #" << number << ": failed to get installation token, skip review";
            return;
        }

        // 3. 获取 PR Diff
        std::string diff = AiReviewer::getPRDiff(token, owner, repoName, number);
        if (diff.empty()) {
            ERROR(logger) << "PR #" << number << ": failed to get PR diff, skip review";
            return;
        }

        // 限制 diff 长度（防止超出 AI token 限制）
        constexpr size_t kMaxDiffLen = 80000;  // ~80k chars 对于大多数模型是安全的
        if (diff.size() > kMaxDiffLen) {
            WARN(logger) << "PR #" << number << ": diff too large (" << diff.size()
                << " chars), truncating to " << kMaxDiffLen;
            diff.resize(kMaxDiffLen);
            diff += "\n\n... (diff truncated due to size)";
        }

        // 4. 调用 AI 审查
        std::string review = AiReviewer::callAIReview(diff, title, body, owner + "/" + repoName, number);
        if (review.empty()) {
            // callAIReview 内部已记录错误
            return;
        }

        // 包装成完整的评论
        std::ostringstream comment;
        comment << "## 🤖 AI 代码审查报告\n\n"
                << "> 自动审查 PR #" << number << ": **" << title << "**\n\n"
                << "---\n\n"
                << review << "\n\n"
                << "---\n"
                << "> ⚡ 由 AI 自动生成，仅供参考。请人工复核后再合并。\n";

        // 5. 发表评论
        AiReviewer::postReviewComment(token, owner, repoName, number, comment.str());

        // 6. 更新 PR 描述追加摘要
        updatePRSummary(token, owner, repoName, number, review);

        INFO(logger) << "AI review completed for " << owner << "/" << repoName << " PR #" << number;
    });
}

} // namespace bot
