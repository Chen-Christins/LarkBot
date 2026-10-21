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

namespace bot {

/**
 * @brief 每日点餐提醒
 * @details 配置项：
 *   meal_reminder.enabled          是否启用
 *   meal_reminder.hour             每天发送的小时（0-23）
 *   meal_reminder.minute           每天发送的分钟（0-59）
 *   meal_reminder.test_on_startup  启动后是否立即发送一次，便于测试
 *   meal_reminder.bot              发送目标机器人，为空时使用 feishu.github_bot
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
     * @brief 计算距离下一次 hour:minute 的毫秒数
     */
    static int64_t nextDelayMs(int hour, int minute);

private:
    chen::Timer::ptr timer_;
    std::atomic<bool> running_{false};
};

using MealReminderMgr = chen::Singleton<MealReminder>;

} // namespace bot
