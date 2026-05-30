/**
 * @file larkbot.h
 * @brief LarkBot 模块文件
 * @author Christins (chen.christins@qq.com)
 * @date 2026-05-30
 * @copyright Apache 2.0
 */
#pragma once

#include <chen/module.h>
#include <chen/tcp/tcp_server.h>

namespace bot {

class BotModule : public chen::Module {
public:
    /**
     * @brief 构造函数
     */
    BotModule();

    /**
     * @brief 模块加载时调用
     * @return bool 是否成功
     */
    bool onLoad() override;

    /**
     * @brief 模块卸载时调用
     * @return bool 是否成功
     */
    bool onUnload() override;

    /**
     * @brief 服务器准备就绪时调用
     * @return bool 是否成功
     */
    bool onServerReady() override;

    /**
     * @brief 服务器启动完成时调用
     * @return bool 是否成功
     */
    bool onServerUp() override;

private:
    /**
     * @brief 注册Servlets
     */
    void registerServlets(std::vector<chen::TcpServer::ptr>& servers);
};

} // namespace bot