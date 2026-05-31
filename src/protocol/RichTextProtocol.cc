#include "RichTextProtocol.hpp"

#include <chen/util/json_util.h>

namespace bot {

void RichTextProtocol::build() {
    data_.clear();

    Protocol::build();

    data_["msg_type"] = "post";
    data_["content"]["post"]["zh_cn"]["title"] = title_;
    data_["content"]["post"]["zh_cn"]["content"] = paragraphs_;
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
