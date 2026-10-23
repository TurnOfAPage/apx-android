#pragma once
// 开机自启：Windows 写 HKCU\...\Run 注册表项；macOS 写 LaunchAgent plist（见 autostart.cpp 平台分支）。
// 声明跨平台；注册表相关常量仅 Windows 使用。
#ifndef APXPC_AUTOSTART_HPP
#define APXPC_AUTOSTART_HPP

#include <string>

namespace apxpc::autostart {

#ifdef _WIN32

// 注册表 Run 键路径（当前用户）
inline constexpr const wchar_t* kRunKeyPath =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

// 注册表值名（与产品名一致）
inline constexpr const wchar_t* kValueName = L"AllPeriph";

#endif  // _WIN32

// 获取当前 exe 的完整路径（用于写入自启配置）
std::string getExePath();

// 设置开机自启：enable=true 写入配置，enable=false 删除配置
// 返回 true 表示操作成功（本来就不在也算成功）
bool setAutostart(bool enable);

// 查询当前开机自启状态（配置存在且路径指向当前 exe）
bool isAutostartEnabled();

}  // namespace apxpc::autostart

#endif  // APXPC_AUTOSTART_HPP
