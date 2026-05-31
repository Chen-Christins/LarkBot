/**
 * @file TextProtocol.hpp
 * @brief 文本协议解析器
 * @author Christins (chen.christins@qq.com)
 * @date 2026-05-30
 * @copyright Apache 2.0
 */
#pragma once

#include "Protocol.hpp"

namespace bot {

class TextProtocol : public Protocol {
public:
    virtual void build() override;

    virtual std::string toString() const override;

    virtual void getData(Json::Value& value) const override;

    virtual void setEnableSignature(bool enable) override;

    void setText(const std::string& text);

private:
    std::string text_;
};
} // namespace bot