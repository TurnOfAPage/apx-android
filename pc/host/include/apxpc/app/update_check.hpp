#pragma once
#include <string>

namespace apxpc::app {

struct UpdateCheckResult {
    bool ok = false;           // 网络与 API 是否成功
    bool hasNewer = false;     // 是否存在更新版本
    std::string latestTag;     // 如 "v206"（ok=false 时可能为空）
    std::string error;         // 失败原因（UTF-8，ok=true 时为空）
};

/// 查 GitHub releases/latest 并与当前版本比对。
/// 比对算法与 Android UpdateChecker.kt 一致：去 v 前缀、按非数字切段、
/// 逐段比数值、缺段补 0 —— 所以 v206.1 > v206 > v205 恒成立。
UpdateCheckResult checkForUpdate();

/// 版本段比对：a > b 返回正数，a < b 返回负数，相等返回 0。
int compareVersion(const std::string& a, const std::string& b);

}  // namespace apxpc::app
