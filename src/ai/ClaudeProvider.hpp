/**
 * @file ClaudeProvider.hpp
 * @brief Claude / Anthropic 协议适配器
 * @author Christins
 * @date 2026-06-10
 * @copyright Apache 2.0
 */
#ifndef __BLOG_AI_CLAUDE_PROVIDER_H__
#define __BLOG_AI_CLAUDE_PROVIDER_H__

#include "AIProvider.hpp"

namespace blog {
namespace ai {

class ClaudeProvider : public AIProvider {
public:
    typedef std::shared_ptr<ClaudeProvider> ptr;

    std::string buildRequest(const std::string& model, int32_t maxTokens, const std::string& systemPrompt,
                             const std::string& prompt, bool stream = true) override;

    std::string endpointSuffix() const override;
    std::string extractContent(const Json::Value& parsed) override;
    std::string extractNonStreamingContent(const Json::Value& parsed) override;
    bool isTerminal(const Json::Value& parsed) override;
    std::string getError(const Json::Value& parsed) override;
    void authHeaders(const std::string& apiKey, std::map<std::string, std::string>& outHeaders) override;
};

} // namespace ai
} // namespace blog

#endif // __BLOG_AI_CLAUDE_PROVIDER_H__
