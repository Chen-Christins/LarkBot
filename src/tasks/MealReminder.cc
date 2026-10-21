#include "MealReminder.hpp"

#include <chen/config/config.h>
#include <chen/iomanager/iomanager.h>
#include <chen/log/log.h>
#include <chen/util/string_util.h>
#include <chen/util/time_util.h>

#include <algorithm>

#include "../feishu/FeishuBotManager.hpp"
#include "../protocol/LarkCardProtocol.hpp"

namespace bot {

static chen::Logger::ptr logger = LOG_NAME("bot");

static chen::ConfigVar<bool>::ptr g_meal_enabled = chen::Config::Lookup<bool>("meal_reminder.enabled", true, "是否启用每日点餐提醒");

static chen::ConfigVar<int32_t>::ptr g_meal_hour = chen::Config::Lookup<int32_t>("meal_reminder.hour", 10, "点餐提醒发送小时");

static chen::ConfigVar<int32_t>::ptr g_meal_minute = chen::Config::Lookup<int32_t>("meal_reminder.minute", 30, "点餐提醒发送分钟");

static chen::ConfigVar<std::vector<int32_t>>::ptr g_meal_skip_weekdays =
    chen::Config::Lookup<std::vector<int32_t>>("meal_reminder.skip_weekdays", std::vector<int32_t>{}, "不发送的星期，1=周一 ... 7=周日");

static chen::ConfigVar<bool>::ptr g_meal_test_on_startup =
    chen::Config::Lookup<bool>("meal_reminder.test_on_startup", false, "启动后立即发送一次点餐提醒");

static chen::ConfigVar<std::string>::ptr g_meal_bot =
    chen::Config::Lookup<std::string>("meal_reminder.bot", "", "点餐提醒发送的机器人，为空使用 github_bot");

static bool IsSkipDay(time_t ts)
{
    auto skipWeekdays = g_meal_skip_weekdays->getValue();
    if (skipWeekdays.empty()) {
        return false;
    }

    int dayOfWeek = chen::TimeUtil::GetDayOfWeek(ts);
    int isoDayOfWeek = (dayOfWeek == 0) ? 7 : dayOfWeek;

    return std::find(skipWeekdays.begin(), skipWeekdays.end(), isoDayOfWeek) != skipWeekdays.end();
}

void MealReminder::start()
{
    if (running_.exchange(true)) {
        return;
    }

    if (!g_meal_enabled->getValue()) {
        INFO(logger) << "meal reminder disabled";
        running_ = false;
        return;
    }

    auto ioManager = chen::IOManager::GetThis();
    if (!ioManager) {
        ERROR(logger) << "meal reminder start failed, no IOManager in current thread";
        running_ = false;
        return;
    }

    INFO(logger) << "meal reminder started, send at " << g_meal_hour->getValue() << ":" << g_meal_minute->getValue();

    if (g_meal_test_on_startup->getValue()) {
        if (IsSkipDay(chen::TimeUtil::GetCurrentSec())) {
            INFO(logger) << "meal reminder test_on_startup skipped on configured weekday";
        } else {
            INFO(logger) << "meal reminder test_on_startup, sending now";
            sendCard();
        }
    }

    scheduleNext();
}

void MealReminder::stop()
{
    running_ = false;
    if (timer_) {
        timer_->cancel();
        timer_.reset();
    }
}

void MealReminder::scheduleNext()
{
    auto ioManager = chen::IOManager::GetThis();
    if (!ioManager) {
        ERROR(logger) << "meal reminder schedule failed, no IOManager in current thread";
        return;
    }

    int hour = g_meal_hour->getValue();
    int minute = g_meal_minute->getValue();
    if (hour < 0 || hour > 23) {
        ERROR(logger) << "invalid meal_reminder.hour=" << hour << ", fallback to 10";
        hour = 10;
    }
    if (minute < 0 || minute > 59) {
        ERROR(logger) << "invalid meal_reminder.minute=" << minute << ", fallback to 30";
        minute = 30;
    }

    int64_t delay = nextDelayMs(hour, minute);
    timer_ = ioManager->addTimer(
        delay,
        [this]() {
            if (!running_) {
                return;
            }
            if (!g_meal_enabled->getValue()) {
                INFO(logger) << "meal reminder disabled, stop schedule";
                running_ = false;
                return;
            }

            time_t now = chen::TimeUtil::GetCurrentSec();
            if (IsSkipDay(now)) {
                INFO(logger) << "meal reminder skipped on configured weekday";
                scheduleNext();
                return;
            }

            sendCard();
            scheduleNext();
        },
        false);

    INFO(logger) << "next meal reminder in " << delay / 1000 << " seconds";
}

void MealReminder::sendCard()
{
    LarkCardProtocol card;
    card.setHeader("禾禾乐点餐通知", "", "orange");
    card.addElement(LarkCardProtocol::markdownElement("食材细心，做的用心，吃的放心"));
    card.addElement(LarkCardProtocol::markdownElement("💡每日现做，美味上线"));
    card.addElement(LarkCardProtocol::markdownElement("<at id=all></at> 大家记得及时点餐哦~"));

    card.build();

    Json::Value cardJson;
    card.getData(cardJson);

    std::string botName = chen::StringUtil::Trim(g_meal_bot->getValue());
    bool sent = false;
    if (botName.empty()) {
        WARN(logger) << "meal reminder send failed, bot is empty";
    } else {
        sent = FeishuBotMgr::GetInstance()->send(botName, cardJson);
        INFO(logger) << "meal reminder sent to bot=" << botName << ", result=" << sent;
    }

    if (!sent) {
        ERROR(logger) << "meal reminder send failed, bot=" << (botName.empty() ? "github" : botName);
    }
}

int64_t MealReminder::nextDelayMs(int hour, int minute)
{
    constexpr uint64_t kDayMs = 24 * 3600 * 1000;

    auto nowSec = chen::TimeUtil::GetCurrentSec();
    auto dayStart = chen::TimeUtil::GetDayStart(nowSec);
    uint64_t baseMs = chen::TimeUtil::UnixSecToMs(dayStart + hour * 3600 + minute * 60);
    uint64_t nextMs = chen::TimeUtil::GetNextResetTime(baseMs, kDayMs);

    // 跳过配置的星期，最多向后找 7 天
    for (int i = 0; i < 7; ++i) {
        time_t candidate = nextMs / 1000;
        if (!IsSkipDay(candidate)) {
            break;
        }
        nextMs += kDayMs;
    }

    if (IsSkipDay(nextMs / 1000)) {
        WARN(logger) << "all weekdays are in meal_reminder.skip_weekdays, no valid send day";
    }

    uint64_t nowMs = chen::TimeUtil::GetCurrentMs();

    if (nextMs <= nowMs) {
        return kDayMs;
    }
    return static_cast<int64_t>(nextMs - nowMs);
}

} // namespace bot
