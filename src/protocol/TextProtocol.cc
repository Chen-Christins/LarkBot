#include "TextProtocol.hpp"

#include <chen/util/json_util.h>
#include <chen/config/config.h>

#include "../Struct.hpp"

namespace bot {

static chen::ConfigVar<int32_t>::ptr g_feishu_enable_signature = 
    chen::Config::Lookup<int32_t>("feishu.enable_signature", false, "飞书签名启用");

static chen::ConfigVar<std::string>::ptr g_feishu_secret = 
    chen::Config::Lookup<std::string>("feishu.secret", "", "飞书密钥");

void TextProtocol::build() {
    data_.clear();
    data_["msg_type"] = "text";
    data_["content"]["text"] = text_;

    if (g_feishu_enable_signature->getValue()) {
        int64_t now = time(0);
        data_["timestamp"] = now;
        data_["sign"] = feishuSign(g_feishu_secret->getValue(), now);
    }
}

std::string TextProtocol::toString() const {
    return chen::JsonUtil::ToString(data_);
}

void TextProtocol::getData(Json::Value& value) const {
    if (data_.empty()) {
        const_cast<TextProtocol*>(this)->build();
    }
    value = data_;
}

void TextProtocol::setText(const std::string& text) {
    text_ = text;
}

void TextProtocol::setEnableSignature(bool enable) {
    enableSignature_ = enable;
}

} // namespace bot