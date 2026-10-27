// 桌面端入口：GUI 子系统（双击无控制台窗口）。
//
// 架构 v2（WebView2 面板）：
//   desktop_main 负责三件事：
//     ① 崩溃取证（VEH + UnhandledExceptionFilter + terminate handler —— 面板是 GUI，
//        崩溃时用户只会觉得"手机突然控不了本机"，现场必须落盘到 exe 同目录）
//     ② 基础设施：防火墙放行、9511 受控端注册、TrayIcon、HotkeyManager
//     ③ 创建完整宿主栈（WirelessSession / MediaSession / ScreenPush /
//        AudioCapture / MicBridge / FileReceiver）→ HostBundle → runWebPanel()
//
//   UI 层（pc/web/）通过 IPC postMessage 调宿主方法、400ms tick 推状态 JSON。
//   与 CLI apxhost.exe 的分工：apxhost 只做常驻服务（无 GUI）；apxdesktop 做 GUI + 常驻。
//
// 回退：APXPC_BUILD_UI_WEBVIEW2=OFF 时编译旧 Win32 GDI+ 面板（panel_win32.cpp），
// 入口仍是 runPanel()。旧面板自身在 Panel 构造函数里创建/持有完整宿主栈生命周期。
//
// macOS 分支（mac-desktop）：GUI 三件事里只有防火墙/DPI 是 Win 专属，崩溃取证与日志
// 落盘在 POSIX 侧同样必须做（双击启动没有终端，见下方 POSIX 取证块）；
// 桌面壳走与 apxhost panel 相同的 runWebPanelHttp（HTTP 面板，内部自含 9511 受控端 /
// 9512 收文件 / startAuto / 媒体泵 / 托盘接线 —— 9511 的唯一创建点）；
// ScreenPush 在 mac **完全不声明**（见下方链接说明）；Linux 报错退出。

#include "apxpc/log.hpp"
#include "apxpc/ui/web_panel.hpp"
#include "apxpc/ui/panel_http.hpp"
#include "apxpc/wireless/wireless_session.hpp"
#include "apxpc/wireless/ctrl9511.hpp"
#include "apxpc/wireless/file_receiver.hpp"
#include "apxpc/localsend/local_send_receiver.hpp"
#include "apxpc/media/media_session.hpp"
#include "apxpc/media/screen_push.hpp"
#include "apxpc/media/audio_capture.hpp"
#include "apxpc/media/mic_bridge.hpp"
#include "apxpc/tray/tray_win32.hpp"
#include "apxpc/hotkey/hotkey.hpp"
#include "apxpc/config/app_config.hpp"
#include "apxpc/api/action_router.hpp"
#include "apxpc/version.hpp"   // APXPC_VERSION_STRING：崩溃报告里要带版本号

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <share.h>
#include <exception>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#pragma comment(lib, "dbghelp")
#pragma comment(lib, "shell32")
#else
#include <unistd.h>
#include <exception>
#include <csignal>
#include <cstdlib>
#include <execinfo.h>   // backtrace / backtrace_symbols_fd（macOS 与 Linux 都有）
#include <sys/stat.h>
#include <fcntl.h>
#include <string>
#if defined(__APPLE__)
#include <mach-o/dyld.h>   // _dyld_get_image_header：崩溃报告的 imageOffset 基准
#endif
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace {

#if defined(_WIN32)
// —————— 防火墙放行（与 host_service.cpp 同款做法）——
bool firewallRuleExists(const char* name) {
    std::string cmd = std::string("netsh advfirewall firewall show rule name=\"") + name + "\"";
    FILE* f = _popen(cmd.c_str(), "r");
    if (!f) return false;
    char buf[512]; bool found = false;
    while (std::fgets(buf, sizeof(buf), f)) {
        if (std::string(buf).find(name) != std::string::npos) { found = true; break; }
    }
    _pclose(f);
    return found;
}
void ensurePanelFirewallRule() {
    const char* ruleName = "AllPeriph apxdesktop ctrl";
    if (firewallRuleExists(ruleName)) return;
    char exePath[MAX_PATH] = {0};
    if (::GetModuleFileNameA(nullptr, exePath, MAX_PATH) == 0) return;
    std::string params = "advfirewall firewall add rule name=\"";
    params += ruleName; params += "\" dir=in action=allow program=\"";
    params += exePath; params += "\" profile=private,public";
    ::ShellExecuteA(nullptr, "runas", "netsh", params.c_str(), nullptr, SW_HIDE);
}

// —————— 崩溃取证（与 host_service.cpp 同款做法，见 v1.33 注释）——
bool crashPath(wchar_t* dst, size_t cap, const wchar_t* name) {
    DWORD n = ::GetModuleFileNameW(nullptr, dst, static_cast<DWORD>(cap));
    if (n == 0 || n >= cap) return false;
    wchar_t* slash = wcsrchr(dst, L'\\');
    if (!slash) return false;
    *slash = L'\0';
    if (wcslen(dst) + 1 + wcslen(name) >= cap) return false;
    wcscat_s(dst, cap, L"\\"); wcscat_s(dst, cap, name);
    return true;
}

constexpr int kMaxMods = 160;
struct ModEntry { uintptr_t base = 0; uintptr_t end = 0; char name[48] = {0}; };
ModEntry g_mods[kMaxMods]; int g_modCount = 0;

void snapshotModules() {
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                             ::GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32W me{}; me.dwSize = sizeof(me);
    for (BOOL ok = ::Module32FirstW(snap, &me); ok && g_modCount < kMaxMods;
         ok = ::Module32NextW(snap, &me)) {
        if (me.modBaseSize == 0) continue;
        ModEntry& e = g_mods[g_modCount]; e.name[0] = '\0';
        ::WideCharToMultiByte(CP_UTF8, 0, me.szModule, -1, e.name, sizeof(e.name) - 1, nullptr, nullptr);
        if (e.name[0] == '\0') continue;
        e.base = reinterpret_cast<uintptr_t>(me.modBaseAddr);
        e.end = e.base + me.modBaseSize; ++g_modCount;
    }
    ::CloseHandle(snap);
}

void formatAddr(char* dst, size_t cap, uintptr_t a) {
    for (int i = 0; i < g_modCount; ++i) {
        if (a >= g_mods[i].base && a < g_mods[i].end) {
            std::snprintf(dst, cap, "%s+0x%llX", g_mods[i].name,
                          static_cast<unsigned long long>(a - g_mods[i].base));
            return;
        }
    }
    std::snprintf(dst, cap, "0x%p（运行期加载）", reinterpret_cast<void*>(a));
}

int appendLine(char* buf, size_t cap, int off, const char* line) {
    const int capI = static_cast<int>(cap);
    if (off < 0 || off >= capI - 1) return capI;
    const int n = std::snprintf(buf + off, cap - static_cast<size_t>(off), "%s\r\n", line);
    if (n < 0) return capI;
    return (off + n < capI) ? off + n : capI;
}

void writeCrashText(const char* what) {
    wchar_t path[MAX_PATH]{};
    if (!crashPath(path, MAX_PATH, L"crash.txt")) return;
    HANDLE f = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    char buf[1024];
    const int n = std::snprintf(buf, sizeof(buf), "terminate: %s\r\n", what ? what : "(no message)");
    if (n > 0) { DWORD written = 0; ::WriteFile(f, buf, static_cast<DWORD>(n), &written, nullptr); }
    ::CloseHandle(f);
}

void writeCrashReport(const char* kind, const char* why, int sig) {
    (void)sig;
    char buf[1024];
    std::snprintf(buf, sizeof(buf), "%s: %s", kind ? kind : "crash", why ? why : "(unknown)");
    writeCrashText(buf);
}

LONG WINAPI apxCrashFilter(EXCEPTION_POINTERS* ep) {
    wchar_t path[MAX_PATH]{};
    if (crashPath(path, MAX_PATH, L"crash.dmp")) {
        HANDLE f = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei{::GetCurrentThreadId(), ep, FALSE};
            ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), f, MiniDumpNormal, &mei, nullptr, nullptr);
            ::CloseHandle(f);
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

LONG WINAPI apxVehHandler(EXCEPTION_POINTERS* ep) {
    if (!ep) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != 0xC0000409u && code != 0xC0000005u && code != 0xC000001Du && code != 0xC0000374u)
        return EXCEPTION_CONTINUE_SEARCH;
    wchar_t path[MAX_PATH]{};
    if (crashPath(path, MAX_PATH, L"crash_veh.txt")) {
        HANDLE f = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            static char report[8192];
            char line[320], where[160];
            int off = 0;
            const char* en = (code == 0xC0000005u) ? "ACCESS_VIOLATION"
                            : (code == 0xC0000409u) ? "STACK_BUFFER_OVERRUN/__fastfail"
                            : (code == 0xC000001Du) ? "ILLEGAL_INSTRUCTION"
                            : "HEAP_CORRUPTION";
            std::snprintf(line, sizeof(line), "异常：0x%08lX  %s", static_cast<unsigned long>(code), en);
            off = appendLine(report, sizeof(report), off, line);
            formatAddr(where, sizeof(where), reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress));
            std::snprintf(line, sizeof(line), "出错地址：%s", where);
            off = appendLine(report, sizeof(report), off, line);
            off = appendLine(report, sizeof(report), off, "调用栈（「模块+偏移」配 PDB 定位；crash.dmp 可用 cdb 打开）：");
            void* frames[40]{};
            const USHORT n = ::CaptureStackBackTrace(0, 40, frames, nullptr);
            for (USHORT i = 0; i < n; ++i) {
                formatAddr(where, sizeof(where), reinterpret_cast<uintptr_t>(frames[i]));
                std::snprintf(line, sizeof(line), "  #%-2u %s", i, where);
                off = appendLine(report, sizeof(report), off, line);
            }
            const char bom[3] = {'\xEF', '\xBB', '\xBF'};
            DWORD written = 0;
            ::WriteFile(f, bom, sizeof(bom), &written, nullptr);
            const int len = (off > 0 && off < static_cast<int>(sizeof(report))) ? off : 0;
            if (len > 0) ::WriteFile(f, report, static_cast<DWORD>(len), &written, nullptr);
            ::CloseHandle(f);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void apxInvalidParamHandler(const wchar_t* expr, const wchar_t* func, const wchar_t* file, unsigned line, uintptr_t) {
    wchar_t path[MAX_PATH]{};
    if (crashPath(path, MAX_PATH, L"crash_param.txt")) {
        HANDLE f = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            wchar_t buf[1024]{};
            const int n = std::swprintf(buf, 1024, L"invalid parameter: expr=%s func=%s file=%s line=%u\r\n",
                                        expr ? expr : L"?", func ? func : L"?", file ? file : L"?", line);
            DWORD written = 0;
            if (n > 0) ::WriteFile(f, buf, static_cast<DWORD>(n) * sizeof(wchar_t), &written, nullptr);
            ::CloseHandle(f);
        }
    }
}

void apxTerminateHandler() {
    const char* what = "(unknown exception)";
    if (std::exception_ptr cur = std::current_exception()) {
        try { std::rethrow_exception(cur); }
        catch (const std::exception& e) { what = e.what(); }
        catch (...) { what = "(non-std exception)"; }
    }
    writeCrashText(what);
    wchar_t path[MAX_PATH]{}, txt[MAX_PATH]{};
    crashPath(path, MAX_PATH, L"crash.txt"); crashPath(txt, MAX_PATH, L"crash_stamp");
    wchar_t msg[700]{};
    std::swprintf(msg, 700, L"程序遇到未处理的异常，已退出。\n\n原因：%hs\n\n报告：%s", what, path);
    ::MessageBoxW(nullptr, msg, L"全能外设 · 异常退出", MB_OK | MB_ICONERROR);
}

void crashForensicsSelfCheck() {
    char probe[160];
    formatAddr(probe, sizeof(probe), reinterpret_cast<uintptr_t>(::GetModuleHandleW(nullptr)));
    APX_LOGI("崩溃取证就绪：模块快照 {} 个；地址翻译自检 = {}", g_modCount, probe);
}

void startFileLogging() {
    wchar_t dir[MAX_PATH]{};
    if (::GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH) == 0) return;
    static const std::wstring g_dir = std::wstring(dir) + L"\\AllPeriph";
    static const std::wstring g_path = g_dir + L"\\apxdesktop.log";
    static std::mutex g_mu;
    static HANDLE g_hf = nullptr;

    ::CreateDirectoryW(g_dir.c_str(), nullptr);

    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (::GetFileAttributesExW(g_path.c_str(), ::GetFileExInfoStandard, &fa) &&
        fa.nFileSizeHigh > 0) {
        ::DeleteFileW(g_path.c_str());
    } else if (fa.nFileSizeLow > 2u * 1024u * 1024u) {
        ::DeleteFileW(g_path.c_str());
    }

    apxpc::Logger::instance().setSink([](apxpc::LogLevel, const std::string& line) {
        std::lock_guard<std::mutex> g(g_mu);
        if (g_hf == nullptr || g_hf == INVALID_HANDLE_VALUE) {
            g_hf = ::CreateFileW(g_path.c_str(), FILE_APPEND_DATA,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }
        if (g_hf != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            ::WriteFile(g_hf, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
            ::WriteFile(g_hf, "\r\n", 2, &written, nullptr);
            ::FlushFileBuffers(g_hf);
        }
    });
}
#else  // —————— POSIX（macOS / Linux）：崩溃取证 + 日志落盘 ——————
std::string logDir() {
    const char* home = std::getenv("HOME");
    const std::string h = (home && *home) ? home : ".";
#if defined(__APPLE__)
    return h + "/Library/Logs/AllPeriph";
#else
    const char* state = std::getenv("XDG_STATE_HOME");
    if (state && *state) return std::string(state) + "/AllPeriph";
    return h + "/.local/state/AllPeriph";
#endif
}

size_t hexStr(char* out, uintptr_t v) {
    char tmp[16];
    size_t n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) {
        const unsigned nib = static_cast<unsigned>(v & 0xF);
        tmp[n++] = static_cast<char>(nib < 10 ? '0' + nib : 'a' + (nib - 10));
        v >>= 4;
    }
    size_t k = 0;
    out[k++] = '0'; out[k++] = 'x';
    while (n) out[k++] = tmp[--n];
    out[k] = '\0';
    return k;
}

size_t decStr(char* out, long long v) {
    char tmp[24];
    size_t n = 0;
    unsigned long long u = (v < 0) ? static_cast<unsigned long long>(-v)
                                   : static_cast<unsigned long long>(v);
    if (u == 0) tmp[n++] = '0';
    while (u) { tmp[n++] = static_cast<char>('0' + (u % 10)); u /= 10; }
    if (v < 0) tmp[n++] = '-';
    size_t k = 0;
    while (n) out[k++] = tmp[--n];
    out[k] = '\0';
    return k;
}

void bufAppend(char* dst, size_t cap, size_t& off, const char* s) {
    if (!s) return;
    for (size_t i = 0; s[i] != '\0' && off + 1 < cap; ++i) dst[off++] = s[i];
    dst[off] = '\0';
}

void bufAppendHex(char* dst, size_t cap, size_t& off, uintptr_t v) {
    char tmp[20];
    hexStr(tmp, v);
    bufAppend(dst, cap, off, tmp);
}

void bufAppendDec(char* dst, size_t cap, size_t& off, long long v) {
    char tmp[24];
    decStr(tmp, v);
    bufAppend(dst, cap, off, tmp);
}

void writeCrashReport(const char* kind, const char* why, int sig) {
    char report[8192];
    size_t off = 0;
    report[0] = '\0';

    bufAppend(report, sizeof(report), off, "apxdesktop 崩溃报告\n版本: ");
    bufAppend(report, sizeof(report), off, APXPC_VERSION_STRING);
    bufAppend(report, sizeof(report), off, "\n类型: ");
    bufAppend(report, sizeof(report), off, kind);
    if (sig != 0) {
        bufAppend(report, sizeof(report), off, "（信号 ");
        bufAppendDec(report, sizeof(report), off, sig);
        bufAppend(report, sizeof(report), off, "）");
    }
    bufAppend(report, sizeof(report), off, "\n原因: ");
    bufAppend(report, sizeof(report), off, (why && *why) ? why : "(没有可用的原因文本)");
    bufAppend(report, sizeof(report), off, "\n");

#if defined(__APPLE__)
    const uintptr_t imgBase = reinterpret_cast<uintptr_t>(_dyld_get_image_header(0));
    bufAppend(report, sizeof(report), off, "主可执行文件加载地址: ");
    bufAppendHex(report, sizeof(report), off, imgBase);
    bufAppend(report, sizeof(report), off, "\n");
#endif

    bufAppend(report, sizeof(report), off,
              "调用栈（#0 是最内层；mac 上 imageOffset 与苹果崩溃报告同口径）:\n");
    void* frames[40];
    const int n = ::backtrace(frames, 40);
    for (int i = 0; i < n; ++i) {
        const uintptr_t a = reinterpret_cast<uintptr_t>(frames[i]);
        bufAppend(report, sizeof(report), off, "  #");
        bufAppendDec(report, sizeof(report), off, i);
        bufAppend(report, sizeof(report), off, " ");
        bufAppendHex(report, sizeof(report), off, a);
#if defined(__APPLE__)
        bufAppend(report, sizeof(report), off, "  imageOffset ");
        bufAppendHex(report, sizeof(report), off, a - imgBase);
#endif
        bufAppend(report, sizeof(report), off, "\n");
    }
    bufAppend(report, sizeof(report), off, "\n");

    const std::string dir = logDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string path = dir + "/crash.txt";
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        const char* p = report;
        size_t left = off;
        while (left > 0) {
            const ssize_t w = ::write(fd, p, left);
            if (w <= 0) break;
            p += w;
            left -= static_cast<size_t>(w);
        }
        ::close(fd);
    }
    if (off > 0) { const ssize_t r = ::write(STDERR_FILENO, report, off); (void)r; }
}

void apxTerminateHandler() {
    const char* why = "(unknown exception)";
    std::string msg;
    if (std::exception_ptr cur = std::current_exception()) {
        try { std::rethrow_exception(cur); }
        catch (const std::exception& e) { msg = e.what(); }
        catch (...) { msg = "(non-std exception)"; }
    }
    if (!msg.empty()) why = msg.c_str();
    writeCrashReport("terminate（C++ 未捕获异常）", why, 0);
}

void apxSignalHandler(int sig) {
    writeCrashReport("fatal signal", "进程收到致命信号", sig);
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

void installCrashForensics() {
    std::set_terminate(apxTerminateHandler);
    {
        void* warm[4];
        (void)::backtrace(warm, 4);
    }
    const int sigs[] = {SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL, SIGTRAP};
    for (const int s : sigs) {
        struct sigaction sa{};
        sa.sa_handler = apxSignalHandler;
        sa.sa_flags = SA_NODEFER;
        sigemptyset(&sa.sa_mask);
        ::sigaction(s, &sa, nullptr);
    }
    APX_LOGI("崩溃取证就绪：报告目录 {}", logDir().c_str());
}

void startFileLogging() {
    const std::string dir = logDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string path = dir + "/apxdesktop.log";
    {
        std::error_code e2;
        const auto sz = std::filesystem::file_size(path, e2);
        if (!e2 && sz > 2u * 1024u * 1024u) std::filesystem::remove(path, e2);
    }
    static std::mutex mu;
    static std::FILE* f = std::fopen(path.c_str(), "a");
    if (f == nullptr) { std::fprintf(stderr, "[AllPeriph] 日志落盘不可用: %s\n", path.c_str()); return; }
    apxpc::Logger::instance().setSink([](apxpc::LogLevel, const std::string& line) {
        std::lock_guard<std::mutex> g(mu);
        if (f == nullptr) return;
        std::fwrite(line.data(), 1, line.size(), f);
        std::fputc('\n', f);
        std::fflush(f);
    });
    APX_LOGI("日志落盘：{}", path.c_str());
}
#endif  // GUI 基础设施

}  // namespace

int runDesktopMain() {
#if defined(_WIN32)
    {
        HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
        using SetCtx_t = BOOL (WINAPI*)(void*);
        auto setCtx = reinterpret_cast<SetCtx_t>(user32
            ? ::GetProcAddress(user32, "SetProcessDpiAwarenessContext") : nullptr);
        if (setCtx) setCtx((void*)(intptr_t)-4);
    }

    ::AddVectoredExceptionHandler(1, apxVehHandler);
    ::SetUnhandledExceptionFilter(apxCrashFilter);
    std::set_terminate(apxTerminateHandler);
    _set_invalid_parameter_handler(apxInvalidParamHandler);
    startFileLogging();
    ensurePanelFirewallRule();
    snapshotModules();
    crashForensicsSelfCheck();
#endif

    std::string hostUtf8;
#if defined(_WIN32)
    wchar_t hostW[64] = {};
    DWORD hn = sizeof(hostW);
    if (!::GetComputerNameW(hostW, &hn) || hostW[0] == L'\0') wcscpy_s(hostW, (size_t)sizeof(hostW)/sizeof(hostW[0]), L"PC");
    {
        const int n = ::WideCharToMultiByte(CP_UTF8, 0, hostW, -1, nullptr, 0, nullptr, nullptr);
        if (n > 0) { hostUtf8.resize(static_cast<size_t>(n));
                     ::WideCharToMultiByte(CP_UTF8, 0, hostW, -1, hostUtf8.data(), n, nullptr, nullptr); }
    }
#else
    {
        char hn[256] = {};
        if (gethostname(hn, sizeof(hn) - 1) == 0 && hn[0] != '\0') {
            hn[sizeof(hn) - 1] = '\0';
            hostUtf8 = hn;
        }
        if (hostUtf8.empty()) hostUtf8 = "Mac";
    }
#endif
#if defined(_WIN32)
    apxpc::wireless::Ctrl9511Server ctrlSrv;
    if (!ctrlSrv.start(9511, "", hostUtf8))
        APX_LOGW("9511 受控端启动失败（端口可能已被 apxhost 占用）");
    else
        APX_LOGI("9511 受控端已启动（桌面端自带）");
#else
    apxpc::wireless::Ctrl9511Server ctrlSrv;
    APX_LOGI("9511 受控端由面板入口创建（本进程不重复创建，主机名 {}）", hostUtf8.c_str());
#endif

    std::string cfgPath = apxpc::config::defaultConfigPath();
    auto cfg = apxpc::config::loadConfig(cfgPath);
    if (cfg.logLevel == "debug") apxpc::Logger::instance().setLevel(apxpc::LogLevel::Debug);
    else if (cfg.logLevel == "warn") apxpc::Logger::instance().setLevel(apxpc::LogLevel::Warn);
    else if (cfg.logLevel == "error") apxpc::Logger::instance().setLevel(apxpc::LogLevel::Error);
    APX_LOGI("全能外设宿主启动（WebView2 面板）");

    auto session     = std::make_unique<apxpc::wireless::WirelessSession>();
    auto media       = std::make_unique<apxpc::media::MediaSession>();
#if defined(_WIN32)
    auto screenPush  = std::make_unique<apxpc::media::ScreenPush>();
#endif
    auto audio       = std::make_unique<apxpc::media::AudioCapture>();
    auto micBridge   = std::make_unique<apxpc::media::MicBridge>();
    auto fileRecv    = std::make_unique<apxpc::wireless::FileReceiver>();
    auto localSend   = std::make_unique<apxpc::localsend::LocalSendReceiver>();
    auto tray        = std::make_unique<apxpc::tray::TrayIcon>();
    auto hk          = std::make_shared<apxpc::hotkey::HotkeyManager>();
    auto router      = std::make_shared<apxpc::api::ActionRouter>();
    router->setConfigPath(cfgPath);
    router->config() = cfg;
    router->setHotkeyManager(hk);

#if defined(_WIN32)
    tray->setQuitCallback([] { ::PostQuitMessage(0); });
#endif
    if (!tray->create("全能外设 · WebView2"))
        APX_LOGW("托盘图标创建失败");

    if (cfg.autostart) apxpc::tray::setAutostart(true);

#if defined(_WIN32)
    fileRecv->start(9512, std::string{}, [](const std::string& path, uint64_t size) {
        APX_LOGI("收到对端文件: {}（{} 字节）", path.c_str(), size);
        apxpc::ui::notifyFileReceived(path);
    });
#endif

    apxpc::ui::HostBundle bundle;
    bundle.session      = session.get();
    bundle.fileReceiver = fileRecv.get();
    bundle.localSend    = localSend.get();
    bundle.media        = media.get();
#if defined(_WIN32)
    bundle.screenPush   = screenPush.get();
#else
    bundle.screenPush   = nullptr;
#endif
    bundle.audio        = audio.get();
    bundle.micBridge    = micBridge.get();
    bundle.ctrlSrv      = &ctrlSrv;
    bundle.tray         = tray.get();
    bundle.hotkey       = hk.get();
    bundle.cfg          = &cfg;
    bundle.configPath   = cfgPath;

#if defined(_WIN32)
    const int rc = apxpc::ui::runWebPanel(bundle);
#elif defined(__APPLE__)
    const int rc = apxpc::ui::runWebPanelHttp(bundle, {});
#else
    std::fprintf(stderr,
                 "apxdesktop 桌面壳暂仅支持 Windows/macOS；Linux 请用 apxhost panel\n");
    const int rc = 1;
#endif

    APX_LOGI("宿主栈关闭中…");
    localSend->stop();
    fileRecv->stop();
    audio->stop();
    micBridge->stop();
#if defined(_WIN32)
    screenPush->stop();
#endif
    media->disconnect();
    session->stop();
    ctrlSrv.stop();
    hk->stop();
    tray->quit();
    apxpc::config::saveConfig(cfgPath, cfg);
    return rc;
}

int main(int /*argc*/, char** /*argv*/) {
#if !defined(_WIN32)
    installCrashForensics();
    startFileLogging();
#endif
    try {
        return runDesktopMain();
    } catch (const std::exception& e) {
        writeCrashReport("exception（main 兜底）", e.what(), 0);
        std::fprintf(stderr, "[AllPeriph] 桌面壳异常退出：%s\n", e.what());
        return 2;
    } catch (...) {
        writeCrashReport("exception（main 兜底）", "(非 std 异常)", 0);
        std::fprintf(stderr, "[AllPeriph] 桌面壳异常退出（非 std 异常）\n");
        return 2;
    }
}
