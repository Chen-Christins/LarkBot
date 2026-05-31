/**
 * @file Protocol.hpp
 * @brief 协议相关定义
 * @author Christins (chen.christins@qq.com)
 * @date 2026-05-30
 * @copyright Apache 2.0
 */
#pragma once

#include <map>
#include <shared_mutex>

#include <chen/singleton.h>
#include <chen/util/json_util.h>

namespace bot {

class Protocol {
public:
    using ptr = std::shared_ptr<Protocol>;

    Protocol() = default;
    virtual ~Protocol() = default;

    /**
     * @brief 构建协议内容
     */
    virtual void build();

    /**
     * @brief 协议内容转换为字符串
     * @return std::string 
     */
    virtual std::string toString() const = 0;

    /**
     * @brief 获取协议数据
     * @param value 输出参数，协议数据以Json格式返回
     */
    virtual void getData(Json::Value& value) const = 0;

protected:
    // 协议数据，具体内容由子类实现决定
    mutable Json::Value data_;
};

class ProtocolManager {
public:
    ProtocolManager() = default;

    void registerProtocol(const std::string& name, Protocol::ptr protocol);
    
    Protocol::ptr getProtocol(const std::string& name) const;

private:
    mutable std::shared_mutex mutex_;
    std::map<std::string, Protocol::ptr> protocols_;
};

using ProtocolMgr = chen::Singleton<ProtocolManager>;

} // namespace bot