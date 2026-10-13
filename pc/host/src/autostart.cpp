// autostart.cpp —— Windows 开机自启实现
// 写入 HKCU\Software\Microsoft\Windows\CurrentVersion\Run 注册表项。

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

#endif  // _WIN32