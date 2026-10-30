#include "MealReminder.hpp"

#include <chen/config/config.h>
#include <chen/http/http_client.h>
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

static chen::ConfigVar<std::string>::ptr g_app_key = chen::Config::Lookup<std::string>("meal_reminder.app_key", "", "route.showapi.com app key");

static chen::ConfigVar<bool>::ptr g_meal_test_on_startup =
    chen::Config::Lookup<bool>("meal_reminder.test_on_startup", false, "启动后立即发送一次点餐提醒");

static chen::ConfigVar<std::string>::ptr g_meal_bot = chen::Config::Lookup<std::string>("meal_reminder.bot", "", "点餐提醒发送的机器人名称");

static constexpr uint64_t kDayMs = 24 * 3600 * 1000;

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

static void LoadTimers(std::set<int32_t>& timers)
{
    for (const auto& item : g_meal_timers->getValue()) {
        int32_t secondsOfDay = 0;
        if (!ParseTimer(item, secondsOfDay)) {
            WARN(logger) << "invalid meal_reminder.timers item: " << item << ", expected HH:MM";
            continue;
        }
        if (std::find(timers.begin(), timers.end(), secondsOfDay) == timers.end()) {
            timers.insert(secondsOfDay);
        }
    }
}

static bool IsWorkingDay(uint64_t time_ms)
{
    std::string url = "https://route.showapi.com/894-6";

    std::string key = g_app_key->getValue();
    url = url + "?appKey=" + key;

    std::string day = chen::TimeUtil::Time2Str(time_ms / 1000, "%Y-%m-%d");
    url = url + "&day=" + day;

    std::map<std::string, std::string> headers = {
        {"content-type", "application/x-www-form-urlencoded"},
    };

    auto ret = chen::http::HttpClient::get(url, 3000, headers, "");
    chen::http::HttpResult::ptr result = ret.get();

    ASSERT_RET(result, false);

    static constexpr int kWorkDayType = 1;
    if (result->response) {
        std::string body = result->response->getBody();

        TRACE(logger) << result->toString();

        Json::Value json_body;
        if (chen::JsonUtil::FromString(json_body, body)) {
            if (json_body["type"].asInt() == kWorkDayType) {
                return true;
            }
        }
    } else {
        WARN(logger) << "request fail, result=" << result->result << ", error=" << result->error;
    }

    return false;
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

    std::set<int32_t> timers;
    LoadTimers(timers);

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
        sendCard();
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

    std::set<int32_t> timers;
    LoadTimers(timers);

    if (timers.empty()) {
        ERROR(logger) << "meal reminder schedule failed, meal_reminder.timers is empty or invalid";
        return;
    }

    int64_t delay_ms = NextDelayMs(timers);
    if (delay_ms < 0) {
        ERROR(logger) << "meal reminder schedule failed, no valid send time";
        return;
    }

    timer_ = ioManager->addTimer(
        delay_ms,
        [this]() {
            if (!running_) {
                return;
            }

            if (!g_meal_enabled->getValue()) {
                INFO(logger) << "meal reminder disabled, stop schedule";
                running_ = false;
                return;
            }

            // check whether tomorrow is a working day
            uint64_t next_ms = chen::TimeUtil::GetCurrentMs() + kDayMs;
            if (!IsWorkingDay(next_ms)) {
                scheduleNext();
                return;
            }

            sendCard();
            scheduleNext();
        },
        false);

    INFO(logger) << "next meal reminder in " << delay_ms / 1000 << " seconds, "
                 << "time is " << chen::TimeUtil::Time2Str(chen::TimeUtil::GetCurrentSec() + delay_ms / 1000);
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

int64_t MealReminder::NextDelayMs(const std::set<int32_t>& timers)
{
    time_t now_sec = chen::TimeUtil::GetCurrentSec();
    time_t day_start_sec = chen::TimeUtil::GetDayStart(now_sec);

    uint64_t now_ms = chen::TimeUtil::GetCurrentMs();

    uint64_t best_ms = 0;
    for (int32_t secondsOfDay : timers) {
        uint64_t base_ms = chen::TimeUtil::UnixSecToMs(day_start_sec + secondsOfDay);
        uint64_t next_ms = chen::TimeUtil::GetNextResetTime(base_ms, kDayMs);

        if (next_ms <= now_ms) {
            next_ms += kDayMs;
        }

        if (best_ms == 0 || next_ms < best_ms) {
            best_ms = next_ms;
        }
    }

    return best_ms == 0 ? -1 : best_ms - now_ms;
}

} // namespace bot
