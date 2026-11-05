#include "TextProtocol.hpp"

#include <chen/util/json_util.h>

namespace bot {

void TextProtocol::build()
{
    data_.clear();

    Protocol::build();

    data_["msg_type"] = "text";
    data_["content"]["text"] = text_;
}

void TextProtocol::setText(const std::string& text)
{
    text_ = text;
}

} // namespace bot