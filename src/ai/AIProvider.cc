#include "AIProvider.hpp"

#include "OpenAIProvider.hpp"
#include "ClaudeProvider.hpp"
#include "GLMProvider.hpp"

namespace blog {
namespace ai {

AIProvider::ptr createProvider(const std::string& type) {
    if (type == "claude") {
        return std::make_shared<ClaudeProvider>();
    }
    if (type == "glm") {
        return std::make_shared<GLMProvider>();
    }
    return std::make_shared<OpenAIProvider>();  // 默认 OpenAI
}

}  // namespace ai
}  // namespace blog
