#include "struct.hpp"

#include <chen/util/util.h>

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

} // namespace bot