/**
 * @file LarkCardProtocol.hpp
 * @brief Lark卡片消息协议解析器
 * @author Christins (chen.christins@qq.com)
 * @date 2026-05-30
 * @copyright Apache 2.0
 */
#pragma once

#include "Protocol.hpp"

namespace bot {

class LarkCardProtocol : public Protocol {
public:
    LarkCardProtocol();

    virtual void build() override;

    virtual std::string toString() const override;

    virtual void getData(Json::Value& value) const override;

    virtual void setEnableSignature(bool enable) override;

    // 设置卡片头部
    void setHeader(const std::string& title,
                   const std::string& subtitle = "",
                   const std::string& templateColor = "blue",
                   const std::string& padding = "12px 12px 12px 12px");

    // 设置卡片配置
    void setConfig(bool updateMulti = true);

    // 设置卡片主体布局
    void setBodyLayout(const std::string& direction = "vertical",
                       const std::string& padding = "12px 12px 12px 12px");

    // 向 body 添加元素
    void addElement(const Json::Value& element);

    // 静态工厂方法：常用卡片元素
    static Json::Value plainText(const std::string& content);
    static Json::Value markdownElement(const std::string& content,
                                       const std::string& textAlign = "left",
                                       const std::string& textSize = "normal_v2",
                                       const std::string& margin = "0px 0px 0px 0px");

    static Json::Value buttonElement(const std::string& text,
                                     const std::string& url,
                                     const std::string& type = "default",
                                     const std::string& width = "default",
                                     const std::string& size = "medium",
                                     const std::string& margin = "0px 0px 0px 0px");

private:
    std::string header_title_;
    std::string header_subtitle_;
    std::string header_template_ = "blue";
    std::string header_padding_ = "12px 12px 12px 12px";

    bool config_update_multi_ = true;

    std::string body_direction_ = "vertical";
    std::string body_padding_ = "12px 12px 12px 12px";
    Json::Value body_elements_;
};
} // namespace bot
