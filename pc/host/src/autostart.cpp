// autostart.cpp —— 开机自启实现
// Windows：写入 HKCU\Software\Microsoft\Windows\CurrentVersion\Run 注册表项。
// macOS：写入 ~/Library/LaunchAgents/com.allperiph.host.plist（launchd 登录项，
//        RunAtLoad 拉起 `apxhost serve`）。

#ifdef _WIN32

#include "apxpc/autostart.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace apxpc::autostart {

std::string getExePath() {
    char buf[MAX_PATH] = {};
    DWORD len = ::GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};
    return buf;
}

bool setAutostart(bool enable) {
    HKEY hKey = nullptr;
    LONG rc = ::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath,
                              0, KEY_SET_VALUE, &hKey);
    if (rc != ERROR_SUCCESS) return false;

    bool ok = false;
    if (enable) {
        std::string exe = getExePath();
        if (exe.empty()) { ::RegCloseKey(hKey); return false; }
        // 值格式："C:\path\to\apxdesktop.exe" --minimized
        std::string val = "\"" + exe + "\" --minimized";
        rc = ::RegSetValueExA(hKey, "AllPeriph", 0, REG_SZ,
                              reinterpret_cast<const BYTE*>(val.c_str()),
                              static_cast<DWORD>(val.size() + 1));
        ok = (rc == ERROR_SUCCESS);
    } else {
        rc = ::RegDeleteValueW(hKey, kValueName);
        // ERROR_FILE_NOT_FOUND 表示本来就不在，也算成功
        ok = (rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND);
    }
    ::RegCloseKey(hKey);
    return ok;
}

bool isAutostartEnabled() {
    HKEY hKey = nullptr;
    LONG rc = ::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath,
                              0, KEY_READ, &hKey);
    if (rc != ERROR_SUCCESS) return false;

    char buf[MAX_PATH] = {};
    DWORD bufLen = MAX_PATH;
    DWORD type = 0;
    rc = ::RegQueryValueExA(hKey, "AllPeriph", nullptr, &type,
                            reinterpret_cast<BYTE*>(buf), &bufLen);
    ::RegCloseKey(hKey);

    if (rc != ERROR_SUCCESS || type != REG_SZ) return false;
    // 验证路径是否指向当前 exe（防止用户手动改了 exe 位置后残留旧值）
    std::string currentExe = getExePath();
    return currentExe.empty() || buf == currentExe;
}

}  // namespace apxpc::autostart

#elif defined(__APPLE__)

#include "apxpc/autostart.hpp"

#include <mach-o/dyld.h>
#include <stdlib.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace apxpc::autostart {
namespace {

// LaunchAgent plist 路径（用户级；launchd 在每次登录时自动加载 ~/Library/LaunchAgents）
std::string plistPath() {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : "") + "/Library/LaunchAgents/com.allperiph.host.plist";
}

std::string xmlEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  o += "&amp;";  break;
            case '<':  o += "&lt;";   break;
            case '>':  o += "&gt;";   break;
            case '"': o += "&quot;"; break;
            default:   o += c;
        }
    }
    return o;
}

// 写 plist：RunAtLoad=true，登录即拉起 `apxhost serve`（无 GUI 时代面板走 CLI 常驻）。
// KeepAlive=false：崩了不强拉（避免反复重启刷 /tmp 日志；launchd 下次登录仍会拉起）。
bool writePlist(const std::string& exe) {
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(plistPath()).parent_path(), ec);
    std::ofstream f(plistPath(), std::ios::trunc);
    if (!f) return false;
    f <<
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "  <key>Label</key>\n"
        "  <string>com.allperiph.host</string>\n"
        "  <key>ProgramArguments</key>\n"
        "  <array>\n"
        "    <string>" << xmlEscape(exe) << "</string>\n"
        "    <string>serve</string>\n"
        "  </array>\n"
        "  <key>RunAtLoad</key>\n"
        "  <true/>\n"
        "  <key>KeepAlive</key>\n"
        "  <false/>\n"
        "  <key>StandardOutPath</key>\n"
        "  <string>/tmp/allperiph-host.log</string>\n"
        "  <key>StandardErrorPath</key>\n"
        "  <string>/tmp/allperiph-host.log</string>\n"
        "</dict>\n"
        "</plist>\n";
    return f.good();
}

// load/unload 仅影响**本次会话**；是否开机自启由 plist 文件存在与否决定（登录时
// launchd 自动加载 ~/Library/LaunchAgents）。这里顺带让开关立即生效/失效。
// 路径由 $HOME + 固定名构成（不含单引号的常规安装路径）；用 system 调系统 launchctl。
void launchctl(const char* verb, const std::string& path) {
    const std::string cmd = std::string("/bin/launchctl ") + verb + " '" + path + "'";
    (void)std::system(cmd.c_str());
}

}  // namespace

std::string getExePath() {
    char buf[4096];
    uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) != 0) return {};
    char real[4096];
    if (::realpath(buf, real)) return real;
    return buf;
}

bool setAutostart(bool enable) {
    if (enable) {
        const std::string exe = getExePath();
        if (exe.empty()) return false;
        if (!writePlist(exe)) return false;
        launchctl("load", plistPath());
        return true;
    }
    launchctl("unload", plistPath());   // 未加载时返回非 0，忽略
    std::error_code ec;
    std::filesystem::remove(plistPath(), ec);
    return true;                         // 本来就不在也算成功（对齐 Win 语义）
}

bool isAutostartEnabled() {
    std::ifstream f(plistPath());
    if (!f.good()) return false;
    const std::string content((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    // 对齐 Win：验证 plist 指向当前 exe（换位置后残留旧值视为未启用）
    const std::string exe = getExePath();
    if (exe.empty()) return true;
    return content.find(xmlEscape(exe)) != std::string::npos;
}

}  // namespace apxpc::autostart

#else

// Linux 等：暂未实现（无 GUI 调用方；GNOME/systemd user unit 后续再说）。
// 提供空实现保证声明/定义成对，避免引用方在链接期才炸。
#include "apxpc/autostart.hpp"

namespace apxpc::autostart {
std::string getExePath() { return {}; }
bool setAutostart(bool) { return false; }
bool isAutostartEnabled() { return false; }
}  // namespace apxpc::autostart

#endif  // 平台分支
