/**
 * @file MealReminder.hpp
 * @brief 每日点餐提醒定时任务
 * @author Christins (chen.christins@qq.com)
 * @date 2026-10-21
 * @copyright Apache 2.0
 */
#pragma once

#include <chen/timer/timer.h>
#include <chen/util/singleton.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace bot {

/**
 * @brief 每日点餐提醒
 * @details 配置项：
 *   meal_reminder.enabled          是否启用
 *   meal_reminder.timers           每天发送的时间点列表，格式 "HH:MM"（如 ["11:30", "18:00"]）
 *   meal_reminder.skip_weekdays    不发送的星期，1=周一 ... 7=周日
 *   meal_reminder.test_on_startup  启动后是否立即发送一次，便于测试
 *   meal_reminder.bot              发送目标机器人名称，不能为空
 */
class MealReminder {
public:
    MealReminder() = default;

    /**
     * @brief 启动定时任务
     */
    void start();

    /**
     * @brief 停止定时任务并取消定时器
     */
    void stop();

private:
    /**
     * @brief 安排下一次发送
     */
    void scheduleNext();

    /**
     * @brief 发送点餐提醒卡片
     */
    void sendCard();

    /**
     * @brief 计算距离下一次发送时间点的毫秒数
     * @param timers 当天秒偏移列表（已排序去重）
     * @return int64_t 距下一次发送的毫秒数，无有效时间返回 -1
     */
    static int64_t nextDelayMs(const std::vector<int32_t>& timers);

private:
    chen::Timer::ptr timer_;
    std::atomic<bool> running_{false};
};

using MealReminderMgr = chen::Singleton<MealReminder>;

} // namespace bot
