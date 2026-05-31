#include "TextProtocol.hpp"

#include <chen/util/json_util.h>

namespace bot {

void TextProtocol::build() {
    data_.clear();

    Protocol::build();

    data_["msg_type"] = "text";
    data_["content"]["text"] = text_;
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

} // namespace bot