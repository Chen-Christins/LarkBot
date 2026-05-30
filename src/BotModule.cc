#include "BotModule.h"

#include <chen/log/log.h>
#include <chen/application.h>
#include <chen/http/http_server.h>

#include "./servlets/GithubWebHook.hpp"

namespace bot {

static chen::Logger::ptr logger = LOG_NAME("bot");

BotModule::BotModule() 
    : chen::Module("BotModule", "1.0.0", "") {
}

bool BotModule::onLoad() {
    INFO(logger) << "onLoad";
    return true;
}

bool BotModule::onUnload() {
    INFO(logger) << "onUnload";
    return true;
}

bool BotModule::onServerReady() {
    INFO(logger) << "onServerReady";

    std::vector<chen::TcpServer::ptr> servers;
    if (chen::Application::GetInstance()->getServer("http", servers)) {
        registerServlets(servers);
    } else {
        ERROR(logger) << "http_server not open";
        return false;
    }

    return true;
}

bool BotModule::onServerUp() {
    INFO(logger) << "onServerUp";
    return true;
}

void BotModule::registerServlets(std::vector<chen::TcpServer::ptr>& servers) {
INFO(logger) << "registerServlets";

	for (auto& i : servers) {
        auto hs = std::dynamic_pointer_cast<chen::http::HttpServer>(i);
        auto dp = hs->getServletDispatch();

#define XX(clazz) chen::http::Servlet::ptr(new clazz)
        dp->addServlet("/api/v1/github_webhook", XX(GithubWebHook));

#undef XX
    }

}

} // namespace bot

extern "C" {

chen::Module* CreateModule() {
    chen::Module* module = new bot::BotModule;
    INFO(bot::logger) << "CreateModule " << module;
    return module;
}

void DestroyModule(chen::Module* module) {
    INFO(bot::logger) << "DestroyModule " << module;
    delete module;
}
}