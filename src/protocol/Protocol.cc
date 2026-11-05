#include "Protocol.hpp"

#include <mutex>

namespace bot {

void Protocol::build()
{
    // 消息签名由 FeishuBotManager 在发送时按目标机器人的 secret 注入
}

std::string Protocol::toString() const
{
    Json::Value value;
    getData(value);
    return chen::JsonUtil::ToString(value);
}

void Protocol::getData(Json::Value& value) const
{
    if (data_.empty()) {
        const_cast<Protocol*>(this)->build();
    }
    value = data_;
}

void ProtocolManager::registerProtocol(const std::string& name, Protocol::ptr protocol)
{
    std::unique_lock lock(mutex_);
    protocols_[name] = protocol;
}

Protocol::ptr ProtocolManager::getProtocol(const std::string& name) const
{
    std::shared_lock lock(mutex_);
    auto it = protocols_.find(name);
    if (it != protocols_.end()) {
        return it->second;
    }
    return nullptr;
}

} // namespace bot