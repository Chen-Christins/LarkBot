#include "Utils.hpp"

#include <chen/http/uri.h>
#include <chen/util/string_util.h>

#include <cctype>

namespace bot {

bool IsValidUrl(const std::string& url)
{
    try {
        auto uri = chen::Uri::Create(url);
        if (!uri) {
            return false;
        }
        std::string scheme = chen::StringUtil::ToLower(uri->getScheme());
        return (scheme == "http" || scheme == "https") && !uri->getHost().empty();
    } catch (...) {
        return false;
    }
}

bool IsValidIdentifier(const std::string& value)
{
    if (value.empty()) {
        return false;
    }
    for (char c : value) {
        auto uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

} // namespace bot
