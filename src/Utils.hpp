/**
 * @file Utils.hpp
 * @brief 业务侧通用工具函数
 * @author Christins (chen.christins@qq.com)
 * @date 2026-10-21
 * @copyright Apache 2.0
 */
#pragma once

#include <string>

namespace bot {

/**
 * @brief 校验 http/https URL
 */
bool IsValidUrl(const std::string& url);

/**
 * @brief 校验业务标识符：字母、数字、下划线、短横线
 */
bool IsValidIdentifier(const std::string& value);

} // namespace bot
