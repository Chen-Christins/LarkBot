#include "Utils.hpp"

#include <chen/http/http_connection.h>
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

bool IsRetryableHttpError(int result)
{
    switch (static_cast<chen::http::HttpResult::Error>(result)) {
    case chen::http::HttpResult::Error::TIMEOUT:
    case chen::http::HttpResult::Error::CONNECT_FAIL:
    case chen::http::HttpResult::Error::SEND_CLOSE_BY_PEER:
    case chen::http::HttpResult::Error::SEND_SOCKET_ERROR:
        return true;
    default:
        return false;
    }
}

bool ParseRepoFullName(const std::string& fullName, std::string& owner, std::string& repo)
{
    auto parts = chen::StringUtil::Split(fullName, '/');
    if (parts.size() != 2) {
        return false;
    }
    owner = parts[0];
    repo = parts[1];
    return true;
}

} // namespace bot
