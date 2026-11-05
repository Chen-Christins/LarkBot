#include "SSEStreamParser.hpp"

#include <chen/util/json_util.h>

namespace bot {
namespace ai {

inline void SendSSEJson(chen::http::SSESession::ptr session, const std::string& type, const std::string& key, const std::string& value)
{
    Json::Value obj;
    obj["type"] = type;
    obj[key] = value;
    session->sendEvent(chen::JsonUtil::ToString(obj));
}

SSEStreamParser::SSEStreamParser(chen::http::SSESession::ptr session, AIProvider::ptr provider)
    : session_(session)
    , provider_(provider)
{
    sse_client_.setCallback([this](const chen::http::SSEClient::SSEvent& event) -> bool {
        processEvent(event.data);
        return true;
    });
}

bool SSEStreamParser::operator()(const char* data, size_t len)
{
    sse_client_.feed(data, len);
    return true;
}

void SSEStreamParser::processEvent(const std::string& data)
{
    if (provider_->isDoneMarker(data)) {
        SendSSEJson(session_, "done", "message", "总结生成完成");
        return;
    }

    Json::Value parsed;
    if (!chen::JsonUtil::FromString(parsed, data)) {
        return;
    }

    std::string content = provider_->extractContent(parsed);
    if (!content.empty()) {
        SendSSEJson(session_, "chunk", "content", content);
        full_text += content;
    }

    if (provider_->isTerminal(parsed)) {
        SendSSEJson(session_, "done", "message", "总结生成完成");
        return;
    }

    std::string errMsg = provider_->getError(parsed);
    if (!errMsg.empty()) {
        SendSSEJson(session_, "error", "message", errMsg);
    }
}

} // namespace ai
} // namespace bot
