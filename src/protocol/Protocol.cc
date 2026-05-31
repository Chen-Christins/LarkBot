#include "Protocol.hpp"

#include <chen/config/config.h>

#include "../Struct.hpp"

#include <mutex>

namespace bot {

static chen::ConfigVar<int32_t>::ptr g_feishu_enable_signature = 
    chen::Config::Lookup<int32_t>("feishu.enable_signature", false, "飞书签名启用");

static chen::ConfigVar<std::string>::ptr g_feishu_secret = 
    chen::Config::Lookup<std::string>("feishu.secret", "", "飞书密钥");

void Protocol::build() {
    if (g_feishu_enable_signature->getValue()) {
        int64_t now = time(0);
        data_["timestamp"] = now;
        data_["sign"] = feishuSign(g_feishu_secret->getValue(), now);
    }
}

void ProtocolManager::registerProtocol(const std::string& name, Protocol::ptr protocol) {
    std::unique_lock lock(mutex_);
    protocols_[name] = protocol;
}
    
Protocol::ptr ProtocolManager::getProtocol(const std::string& name) const {
    std::shared_lock lock(mutex_);
    auto it = protocols_.find(name);
    if (it != protocols_.end()) {
        return it->second;
    }
    return nullptr;
}

} // namespace bot