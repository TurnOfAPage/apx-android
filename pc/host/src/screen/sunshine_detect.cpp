#include "apxpc/screen/sunshine_detect.hpp"

#include <sys/stat.h>

#include <cstdlib>

#include "apxpc/net/tcp_probe.hpp"

namespace apxpc::screen {
namespace {

bool pathExists(const std::string& p) {
    struct stat st {};
    return ::stat(p.c_str(), &st) == 0;
}

std::string homeDir() {
    const char* h = std::getenv("HOME");
    return (h && *h) ? std::string(h) : std::string(".");
}

}  // namespace

SunshineStatus sunshineStatus() {
    SunshineStatus s;
    const std::string home = homeDir();

    // 证据一：Sunshine 的家目录配置（mac 与 Linux 同一约定：~/.config/sunshine/）
    const bool conf = pathExists(home + "/.config/sunshine/sunshine.conf") ||
                      pathExists(home + "/.config/sunshine");

    // 证据二：macOS 的 app bundle（/Applications/Sunshine.app）。
    // Linux 上包管理器的安装位置太散（/usr/bin、flatpak…），不猜 —— Linux 只认配置目录，
    // 面板文案里会写清楚「检测不到就按没装处理」。
#if defined(__APPLE__)
    const bool app = pathExists("/Applications/Sunshine.app");
#else
    const bool app = false;
#endif

    // 证据三：默认 https 端口有人监听（Sunshine 默认 47990）。
    // 注意本工程托盘的默认端口恰好也是 47990 —— 所以「有人在听」**单独不构成**
    // 已安装证据（可能是本工程另一个实例），必须与配置/应用存在同真才算「已装且在跑」。
    s.running = apxpc::net::tcpPortRespondsLocal(s.httpsPort, 250);

    s.installed = conf || app;
    return s;
}

std::string sunshineUrl(const SunshineStatus& s) {
    if (!s.running) return {};
    return "https://localhost:" + std::to_string(s.httpsPort);
}

}  // namespace apxpc::screen
