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

namespace bot {

/**
 * @brief 每日点餐提醒
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
    static int64_t NextDelayMs(const std::set<int32_t>& timers);

private:
    chen::Timer::ptr timer_;
    std::atomic<bool> running_{false};
};

using MealReminderMgr = chen::Singleton<MealReminder>;

} // namespace bot
