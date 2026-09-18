#include "BotModule.h"

#include <chen/log/log.h>
#include <chen/application.h>

#include "./servlets/GithubWebHook.hpp"

namespace bot {

static chen::Logger::ptr logger = LOG_NAME("bot");

BotModule::BotModule() : Module("BotModule", "1.0.0", "") {}

void BotModule::onBeforeArgsParse(int argc, char** argv) {
    INFO(logger) << "onBeforeArgsParse";
}

void BotModule::onAfterArgsParse(int argc, char** argv) {
    INFO(logger) << "onAfterArgsParse";
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

    std::vector<chen::http::HttpServer::ptr> servers;
    getAllHttpServer(servers);

    if (!servers.empty()) {
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

bool BotModule::onActivate() {
    INFO(logger) << "onActivate";
    return true;
}

bool BotModule::onDeactivate() {
    INFO(logger) << "onDeactivate";
    return true;
}

void BotModule::onTick() {
    INFO(logger) << "onTick";
}

uint64_t BotModule::getTickIntervalMs() {
    return 0;
}

void BotModule::registerServlets(std::vector<chen::http::HttpServer::ptr>& servers) {
    INFO(logger) << "registerServlets";

	for (auto& hs : servers) {
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
    return module;
}

void DestroyModule(chen::Module* module) {
    delete module;
}
}