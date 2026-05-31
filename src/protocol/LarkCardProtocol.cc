#include "LarkCardProtocol.hpp"

#include <chen/util/json_util.h>

namespace bot {

LarkCardProtocol::LarkCardProtocol()
    : body_elements_(Json::arrayValue) {
}

void LarkCardProtocol::build() {
    data_.clear();

    Protocol::build();

    data_["msg_type"] = "interactive";
    data_["card"]["schema"] = "2.0";

    data_["card"]["config"]["update_multi"] = config_update_multi_;
    data_["card"]["config"]["style"]["text_size"]["normal_v2"]["default"] = "normal";
    data_["card"]["config"]["style"]["text_size"]["normal_v2"]["pc"] = "normal";
    data_["card"]["config"]["style"]["text_size"]["normal_v2"]["mobile"] = "heading";

    data_["card"]["header"]["title"]["tag"] = "plain_text";
    data_["card"]["header"]["title"]["content"] = header_title_;
    data_["card"]["header"]["subtitle"]["tag"] = "plain_text";
    data_["card"]["header"]["subtitle"]["content"] = header_subtitle_;
    data_["card"]["header"]["template"] = header_template_;
    data_["card"]["header"]["padding"] = header_padding_;

    data_["card"]["body"]["direction"] = body_direction_;
    data_["card"]["body"]["padding"] = body_padding_;
    data_["card"]["body"]["elements"] = body_elements_;
}

std::string LarkCardProtocol::toString() const {
    return chen::JsonUtil::ToString(data_);
}

void LarkCardProtocol::getData(Json::Value& value) const {
    if (data_.empty()) {
        const_cast<LarkCardProtocol*>(this)->build();
    }
    value = data_;
}

void LarkCardProtocol::setHeader(const std::string& title, const std::string& subtitle
        , const std::string& templateColor, const std::string& padding) {
    header_title_ = title;
    header_subtitle_ = subtitle;
    header_template_ = templateColor;
    header_padding_ = padding;
}

void LarkCardProtocol::setConfig(bool updateMulti) {
    config_update_multi_ = updateMulti;
}

void LarkCardProtocol::setBodyLayout(const std::string& direction, const std::string& padding) {
    body_direction_ = direction;
    body_padding_ = padding;
}

void LarkCardProtocol::addElement(const Json::Value& element) {
    body_elements_.append(element);
}

Json::Value LarkCardProtocol::plainText(const std::string& content) {
    Json::Value elem;
    elem["tag"] = "plain_text";
    elem["content"] = content;
    return elem;
}

Json::Value LarkCardProtocol::markdownElement(const std::string& content, const std::string& textAlign
        , const std::string& textSize, const std::string& margin) {
    Json::Value elem;
    elem["tag"] = "markdown";
    elem["content"] = content;
    elem["text_align"] = textAlign;
    elem["text_size"] = textSize;
    elem["margin"] = margin;
    return elem;
}

Json::Value LarkCardProtocol::buttonElement(const std::string& text,const std::string& url
        , const std::string& type, const std::string& width, const std::string& size, const std::string& margin) {
    Json::Value elem;
    elem["tag"] = "button";
    elem["text"]["tag"] = "plain_text";
    elem["text"]["content"] = text;
    elem["type"] = type;
    elem["width"] = width;
    elem["size"] = size;
    elem["margin"] = margin;

    Json::Value behavior;
    behavior["type"] = "open_url";
    behavior["default_url"] = url;
    behavior["pc_url"] = "";
    behavior["ios_url"] = "";
    behavior["android_url"] = "";
    elem["behaviors"].append(behavior);

    return elem;
}

} // namespace bot
