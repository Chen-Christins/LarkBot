#include "RichTextProtocol.hpp"

#include <chen/util/json_util.h>
#include <chen/config/config.h>

#include "../Struct.hpp"

namespace bot {

static chen::ConfigVar<int32_t>::ptr g_feishu_enable_signature = 
    chen::Config::Lookup<int32_t>("feishu.enable_signature", false, "飞书签名启用");

static chen::ConfigVar<std::string>::ptr g_feishu_secret = 
    chen::Config::Lookup<std::string>("feishu.secret", "", "飞书密钥");

void RichTextProtocol::build() {
    data_.clear();
    data_["msg_type"] = "post";
    data_["content"]["post"]["zh_cn"]["title"] = title_;
    data_["content"]["post"]["zh_cn"]["content"] = paragraphs_;

    if (g_feishu_enable_signature->getValue()) {
        int64_t now = time(0);
        data_["timestamp"] = now;
        data_["sign"] = feishuSign(g_feishu_secret->getValue(), now);
    }
}

std::string RichTextProtocol::toString() const {
    return chen::JsonUtil::ToString(data_);
}

void RichTextProtocol::getData(Json::Value& value) const {
    if (data_.empty()) {
        const_cast<RichTextProtocol*>(this)->build();
    }
    value = data_;
}

void RichTextProtocol::setTitle(const std::string& title) {
    title_ = title;
}

void RichTextProtocol::addParagraph(const Json::Value& paragraph) {
    paragraphs_.append(paragraph);
}

Json::Value RichTextProtocol::textElement(const std::string& text) {
    Json::Value elem;
    elem["tag"] = "text";
    elem["text"] = text;
    return elem;
}

Json::Value RichTextProtocol::linkElement(const std::string& text, const std::string& href) {
    Json::Value elem;
    elem["tag"] = "a";
    elem["text"] = text;
    elem["href"] = href;
    return elem;
}

Json::Value RichTextProtocol::atElement(const std::string& user_id) {
    Json::Value elem;
    elem["tag"] = "at";
    elem["user_id"] = user_id;
    return elem;
}

Json::Value RichTextProtocol::atAllElement() {
    Json::Value elem;
    elem["tag"] = "at";
    elem["user_id"] = "all";
    return elem;
}

} // namespace bot
