#include "Struct.hpp"

#include <chen/util/util.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

#include <ctime>

namespace bot {

Result::Result(int32_t c, const std::string& m)
    : code(c)
    , used(chen::GetCurrentUs())
    , msg(m) {
}

void Result::setResult(int32_t c, const std::string& m) {
    code = c;
    msg = m;
}

std::string Result::toJsonString() const {
    Json::Value v;
    v["code"] = std::to_string(code);
    v["msg"] = msg;
    v["used"] = ((chen::GetCurrentUs() - used) / 1000.0);
    if (!jsondata.isNull()) {
        v["data"] = jsondata;
    } else {
        // if (!datas.empty()) {
        //     auto& d = v["data"];
        //     for (auto& [key, value] : datas) {
        //         d[key] = value;
        //     }
        // }
    }
    return chen::JsonUtil::ToString(v);
}

LarkBotServlet::LarkBotServlet(const std::string& name)
    : chen::http::Servlet(name) {
}

int32_t LarkBotServlet::handle(chen::http::HttpRequest::ptr request
    , chen::http::HttpResponse::ptr response, chen::http::HttpSession::ptr session) {
    uint64_t ts = chen::GetCurrentUs();
    
    Result::ptr result = std::make_shared<Result>();
    int32_t ret = handle(request, response, session, result);

    uint64_t used = chen::GetCurrentUs() - ts;
    response->setHeader("used", std::to_string((used * 1.0 / 1000)) + "ms");
    return ret;
}

static std::string base64Encode(const unsigned char* data, size_t len) {
    size_t outLen = 4 * ((len + 2) / 3);
    std::string output(outLen, '\0');
    int actualLen = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(output.data()), data, len);
    output.resize(actualLen);
    return output;
}

std::string feishuSign(const std::string& secret, int64_t timestamp) {
    std::string signKey = std::to_string(timestamp) + "\n" + secret;

    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), signKey.data(), static_cast<int>(signKey.size()),
        reinterpret_cast<const unsigned char*>(""), 0,
        result, &len);

    return base64Encode(result, len);
}

} // namespace bot