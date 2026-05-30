#include "Protocol.hpp"

#include <mutex>

namespace bot {

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