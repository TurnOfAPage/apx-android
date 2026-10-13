#pragma once
// Windows 开机自启：写入/删除 HKCU\...\Run 注册表项。
// 纯 Windows 实现，跨平台编译需 #ifdef _WIN32 保护。
#ifndef APXPC_AUTOSTART_HPP
#define APXPC_AUTOSTART_HPP

#ifdef _WIN32

#include <string>

namespace apxpc::autostart {

// 注册表 Run 键路径（当前用户）
inline constexpr const wchar_t* kRunKeyPath =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

// 注册表值名（与产品名一致）
inline constexpr const wchar_t* kValueName = L"AllPeriph";

// 获取当前 exe 的完整路径（用于写入注册表）
std::string getExePath();

// 设置开机自启：enable=true 写注册表，enable=false 删注册表项
// 返回 true 表示操作成功
bool setAutostart(bool enable);

// 查询当前开机自启状态（读注册表判断值是否存在且路径匹配）
bool isAutostartEnabled();

}  // namespace apxpc::autostart

#endif  // _WIN32
#endif  // APXPC_AUTOSTART_HPP