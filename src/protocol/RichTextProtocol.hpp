/**
 * @file RichTextProtocol.hpp
 * @brief 富文本协议解析器
 * @author Christins (chen.christins@qq.com)
 * @date 2026-05-30
 * @copyright Apache 2.0
 */
#pragma once

#include "Protocol.hpp"

namespace bot {

class RichTextProtocol : public Protocol {
public:
    virtual void build() override;

    virtual std::string toString() const override;

    virtual void getData(Json::Value& value) const override;

    virtual void setEnableSignature(bool enable) override;

    void setTitle(const std::string& title);

    void addParagraph(const Json::Value& paragraph);

    // 静态工厂方法：创建富文本元素
    static Json::Value textElement(const std::string& text);
    static Json::Value linkElement(const std::string& text, const std::string& href);
    static Json::Value atElement(const std::string& user_id);
    static Json::Value atAllElement();

private:
    std::string title_;
    Json::Value paragraphs_;  // Json::arrayValue
};
} // namespace bot
