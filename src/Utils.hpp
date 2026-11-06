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

/**
 * @brief 判断 HTTP 错误是否属于临时性、可重试错误
 * @param result chen::http::HttpResult::result
 */
bool IsRetryableHttpError(int result);

/**
 * @brief 解析 GitHub 仓库 full_name（owner/repo）
 * @param fullName GitHub 返回的 full_name
 * @param owner [out] 仓库所有者
 * @param repo [out] 仓库名
 * @return true 解析成功
 */
bool ParseRepoFullName(const std::string& fullName, std::string& owner, std::string& repo);

} // namespace bot
