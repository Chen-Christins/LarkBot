#include "MealReminder.hpp"

#include <chen/config/config.h>
#include <chen/iomanager/iomanager.h>
#include <chen/log/log.h>
#include <chen/util/string_util.h>
#include <chen/util/time_util.h>
#include <chen/util/type_util.h>

#include <algorithm>

#include "../feishu/FeishuBotManager.hpp"
#include "../protocol/LarkCardProtocol.hpp"

namespace bot {

static chen::Logger::ptr logger = LOG_NAME("bot");

static chen::ConfigVar<bool>::ptr g_meal_enabled = chen::Config::Lookup<bool>("meal_reminder.enabled", true, "是否启用每日点餐提醒");

static chen::ConfigVar<std::vector<std::string>>::ptr g_meal_timers =
    chen::Config::Lookup<std::vector<std::string>>("meal_reminder.timers", {}, "Reminder timer list");

static chen::ConfigVar<std::vector<int32_t>>::ptr g_meal_skip_weekdays =
    chen::Config::Lookup<std::vector<int32_t>>("meal_reminder.skip_weekdays", std::vector<int32_t>{}, "不发送的星期，1=周一 ... 7=周日");

static chen::ConfigVar<bool>::ptr g_meal_test_on_startup =
    chen::Config::Lookup<bool>("meal_reminder.test_on_startup", false, "启动后立即发送一次点餐提醒");

static chen::ConfigVar<std::string>::ptr g_meal_bot = chen::Config::Lookup<std::string>("meal_reminder.bot", "", "点餐提醒发送的机器人名称");

/**
 * @brief 解析 "HH:MM" 或 "HH:MM:SS" 为当天秒偏移
 */
static bool ParseTimer(const std::string& value, int32_t& secondsOfDay)
{
    auto parts = chen::StringUtil::Split(value, ':');
    if (parts.size() != 2 && parts.size() != 3) {
        return false;
    }

    int32_t nums[3] = {0, 0, 0};
    for (size_t i = 0; i < parts.size(); ++i) {
        std::string part = chen::StringUtil::Trim(parts[i]);
        bool digits = !part.empty() && std::all_of(part.begin(), part.end(), [](char c) {
            return c >= '0' && c <= '9';
        });
        if (!digits) {
            return false;
        }
        nums[i] = chen::TypeUtil::Atoi(part);
    }

    if (nums[0] > 23 || nums[1] > 59 || nums[2] > 59) {
        return false;
    }

    secondsOfDay = nums[0] * 3600 + nums[1] * 60 + nums[2];
    return true;
}

/**
 * @brief 读取并解析 meal_reminder.timers，按时间排序去重
 */
static std::vector<int32_t> LoadTimers()
{
    std::vector<int32_t> timers;
    for (const auto& item : g_meal_timers->getValue()) {
        int32_t secondsOfDay = 0;
        if (!ParseTimer(item, secondsOfDay)) {
            WARN(logger) << "invalid meal_reminder.timers item: " << item << ", expected HH:MM";
            continue;
        }
        if (std::find(timers.begin(), timers.end(), secondsOfDay) == timers.end()) {
            timers.push_back(secondsOfDay);
        }
    }
    std::sort(timers.begin(), timers.end());
    return timers;
}

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

    auto timers = LoadTimers();
    if (timers.empty()) {
        ERROR(logger) << "meal reminder start failed, meal_reminder.timers is empty or invalid";
        running_ = false;
        return;
    }

    std::vector<std::string> timerTexts;
    for (const auto& item : g_meal_timers->getValue()) {
        std::string timer = chen::StringUtil::Trim(item);
        if (!timer.empty()) {
            timerTexts.push_back(timer);
        }
    }
    INFO(logger) << "meal reminder started, send at " << chen::StringUtil::Join(timerTexts, ", ");

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

    auto timers = LoadTimers();
    if (timers.empty()) {
        ERROR(logger) << "meal reminder schedule failed, meal_reminder.timers is empty or invalid";
        return;
    }

    int64_t delay = nextDelayMs(timers);
    if (delay < 0) {
        ERROR(logger) << "meal reminder schedule failed, no valid send time";
        return;
    }

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
    if (botName.empty()) {
        ERROR(logger) << "meal reminder send failed, meal_reminder.bot is empty";
        return;
    }

    bool sent = FeishuBotMgr::GetInstance()->send(botName, cardJson);
    INFO(logger) << "meal reminder sent to bot=" << botName << ", result=" << sent;
    if (!sent) {
        ERROR(logger) << "meal reminder send failed, bot=" << botName;
    }
}

int64_t MealReminder::nextDelayMs(const std::vector<int32_t>& timers)
{
    constexpr uint64_t kDayMs = 24 * 3600 * 1000;

    auto nowSec = chen::TimeUtil::GetCurrentSec();
    auto dayStart = chen::TimeUtil::GetDayStart(nowSec);
    uint64_t nowMs = chen::TimeUtil::GetCurrentMs();

    uint64_t bestMs = 0;
    for (int32_t secondsOfDay : timers) {
        uint64_t baseMs = chen::TimeUtil::UnixSecToMs(dayStart + secondsOfDay);
        uint64_t nextMs = chen::TimeUtil::GetNextResetTime(baseMs, kDayMs);

        if (nextMs <= nowMs) {
            nextMs += kDayMs;
        }

        // 跳过配置的星期，最多向后找 7 天
        for (int i = 0; i < 7 && IsSkipDay(nextMs / 1000); ++i) {
            nextMs += kDayMs;
        }

        if (IsSkipDay(nextMs / 1000)) {
            WARN(logger) << "all weekdays are in meal_reminder.skip_weekdays, no valid send day";
            continue;
        }

        if (bestMs == 0 || nextMs < bestMs) {
            bestMs = nextMs;
        }
    }

    if (bestMs == 0) {
        return -1;
    }
    return bestMs - nowMs;
}

} // namespace bot
