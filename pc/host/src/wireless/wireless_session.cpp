#include "apxpc/wireless/wireless_session.hpp"

#include "apxpc/log.hpp"

#include <chrono>
#include <string_view>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>
#include <cerrno>
#include <unistd.h>
#endif

namespace apxpc::wireless {
namespace {

constexpr auto kTick = std::chrono::milliseconds(250);
constexpr uint16_t kBeaconPort = 9501;

#if defined(_WIN32)
using Sock = SOCKET;   // Windows 套接字句柄（原 wireless_link.hpp 里的别名，迁移后此处自带）

// 进程级 WSA 初始化（只做一次）。apxdesktop 单独运行（不跑 HttpServer / ctrl9511）时，
// 若此处不初始化，beaconLoop 里的 socket() 会返回 INVALID_SOCKET，发现线程直接退出，
// 而 beaconRun_ 仍为 true 不会重试 —— 表现为状态永远「正在发现」、连不上。
// 与 ctrl9511.cpp / media_session.cpp 各自持有一份 static，无副作用。
static void ensureWsa() {
    static bool once = [] { WSADATA d{}; WSAStartup(MAKEWORD(2, 2), &d); return true; }();
    (void)once;
}
#else
using Sock = int;      // POSIX 文件描述符
#endif

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// 解析 "APX1TV|APX1PC|APX1PH <name> <port> <token>"，返回 (host-ip, port)。
/// phone 出参：是否手机信标（APX1PH）—— 自动发现手机优先，见 onBeacon。name/token 忽略。
bool parseBeacon(const char* buf, size_t n, std::string& host, uint16_t& port,
                 bool* phone = nullptr) {
    // 简单按空格切分，取第 2、3 段（name、port）。host 由调用方从来源地址填入。
    std::string_view s(buf, n);
    // 必须以 "APX1TV"(TV) / "APX1PC"(PC) / "APX1PH"(手机被控) 开头 —— 三种前缀同为 6 字符。
    // 少认一种，那一类设备就会永远「正在发现」（手机改发 APX1PH 后就踩过这个坑）。
    if (s.size() < 6) return false;
    const std::string_view head = s.substr(0, 6);
    if (head != "APX1TV" && head != "APX1PC" && head != "APX1PH") return false;
    if (phone) *phone = (head == "APX1PH");
    // 找 name 与 port 两个 token
    size_t i = 6;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    size_t name0 = i;
    while (i < s.size() && s[i] != ' ' && s[i] != '\t') ++i;
    size_t name1 = i;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    size_t port0 = i;
    while (i < s.size() && s[i] != ' ' && s[i] != '\t') ++i;
    size_t port1 = i;
    if (name1 == name0 || port1 == port0) return false;
    std::string portStr(s.substr(port0, port1 - port0));
    try {
        port = static_cast<uint16_t>(std::stoi(portStr));
    } catch (...) {
        return false;
    }
    (void)name0;
    return port != 0;
}

}  // namespace

// 非阻塞 connect + select 探测单个 IP:port 是否可达。
static bool portOpen(const std::string& ip, uint16_t port, int timeoutMs) {
    Sock s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == kBadSock) return false;
    bool ok = false;
#if defined(_WIN32)
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
#else
    int fl = fcntl(s, F_GETFL, 0); fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
#if defined(_WIN32)
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
#else
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
#endif
    int rc = connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc == 0) {
        ok = true;
    } else {
        bool inProgress = false;
#if defined(_WIN32)
        int e = WSAGetLastError();
        inProgress = (e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY);
#else
        inProgress = (errno == EINPROGRESS);
#endif
        if (inProgress) {
            fd_set wf; FD_ZERO(&wf); FD_SET(s, &wf);
            struct timeval tv{}; tv.tv_sec = timeoutMs / 1000; tv.tv_usec = (timeoutMs % 1000) * 1000;
            int sr = select(static_cast<int>(s) + 1, nullptr, &wf, nullptr, &tv);
            if (sr > 0) {
                int soerr = 0;
#if defined(_WIN32)
                int sl = sizeof(soerr);
                getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soerr), &sl);
#else
                socklen_t sl = sizeof(soerr);
                getsockopt(s, SOL_SOCKET, SO_ERROR, &soerr, &sl);
#endif
                ok = (soerr == 0);
            }
        }
    }
#if defined(_WIN32)
    closesocket(s);
#else
    close(s);
#endif
    return ok;
}

// 扫描本机所在 /24 网段的 9511 端口。并发 8 线程，总耗时约 (254/8)*timeout。
std::vector<std::string> scanLan9511(int timeoutMs) {
    std::vector<std::string> result;
    std::mutex mu;
    ensureWsa();

    std::string prefix;
    {
        Sock s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s != kBadSock) {
            sockaddr_in remote{};
            remote.sin_family = AF_INET;
            remote.sin_port = htons(53);
#if defined(_WIN32)
            remote.sin_addr.s_addr = inet_addr("8.8.8.8");
#else
            inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);
#endif
            if (connect(s, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) == 0) {
                sockaddr_in local{};
                socklen_t l = sizeof(local);
                if (getsockname(s, reinterpret_cast<sockaddr*>(&local), &l) == 0) {
                    char buf[32] = {0};
#if defined(_WIN32)
                    strncpy_s(buf, sizeof(buf) - 1, inet_ntoa(local.sin_addr), _TRUNCATE);
#else
                    inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
#endif
                    std::string ip(buf);
                    size_t d = ip.rfind('.');
                    if (d != std::string::npos) prefix = ip.substr(0, d + 1);
                }
            }
#if defined(_WIN32)
            closesocket(s);
#else
            close(s);
#endif
        }
    }
    if (prefix.empty()) return result;

    std::vector<std::thread> ws;
    for (int w = 0; w < 8; ++w) {
        ws.emplace_back([&, w]() {
            for (int host = w + 1; host <= 254; host += 8) {
                std::string ip = prefix + std::to_string(host);
                if (portOpen(ip, 9511, timeoutMs)) {
                    std::lock_guard<std::mutex> lk(mu);
                    result.push_back(ip);
                }
            }
        });
    }
    for (auto& t : ws) t.join();
    return result;
}

const char* linkPhaseName(LinkPhase p) noexcept {
    switch (p) {
        case LinkPhase::Idle:        return "未启用";
        case LinkPhase::Discovering: return "正在发现设备";
        case LinkPhase::Connecting:  return "正在连接";
        case LinkPhase::Connected:   return "已连接";
        case LinkPhase::Failed:      return "连接失败";
        default:                     return "?";
    }
}

WirelessSession::WirelessSession() {
    // 远程输入回传（0x26）：对端用户在手机/TV 上敲的字，就地注入**本机当前光标处**。
    // 回调跑在 9511 的 reader 线程；injectSystemText 内部是 SendInput / 剪贴板 + Ctrl+V，
    // 都是线程安全的，不需要绕回 UI 线程。
    client_.onRemoteInputText = [](const std::string& text, uint8_t flags) {
        // v184：BACKSPACE（0x02）此前被当作空文本直接吞掉 —— 手机端删除键失灵的根因
        if (flags & 0x02) {   // INPUT_FLAG_BACKSPACE（ApxFrame.kt 定义，PC 侧无此常量）
            // 0x08 = Windows VK_BACK：协议两侧按同一 VK 码空间解释（见 input_injector_*）
            apxpc::wireless::injectSpecialKey(0, 0x08);
            return;
        }
        if (!apxpc::wireless::injectSystemText(text))
            APX_LOGW("远程输入文本注入失败（{} 字节）", static_cast<unsigned>(text.size()));
    };
    // v184：0x28 特殊键/组合键（手机编辑快捷键排：方向键/Home/End/Del/Ctrl+A…）
    client_.onSpecialKey = [](uint8_t mod, uint8_t vk) {
        apxpc::wireless::injectSpecialKey(mod, vk);
    };
    client_.onRemoteInputDone = [] { APX_LOGI("远程输入完成"); };
    // v184：反向剪贴板（0x21）—— 对端（手机 / TV）上复制的内容写进**本机剪贴板**。
    // 此前只有 CLI（apxhost ctrl-connect / ctrl-remote）挂了这条回调，
    // **桌面面板从不注册** —— 于是"在手机上复制、回 PC 粘贴"永远是空的，
    // 用户可见的症状是"剪贴板功能不对"。现在面板这条链路补齐。
    client_.onReverseClipboard = [](const std::string& text) {
        const bool ok = apxpc::wireless::setSystemClipboard(text);
        if (ok) APX_LOGI("反向剪贴板：已写入本机剪贴板（{} 字节）",
                         static_cast<unsigned>(text.size()));
        else APX_LOGW("反向剪贴板：写入本机剪贴板失败（{} 字节）",
                      static_cast<unsigned>(text.size()));
    };

    // 反向远程输入（0x25）：对端（手机 / TV）请**本机**输入 —— 三端对等的另一半。
    // 此前 client_.onRequestInput 从未注册：手机 / TV 喊电脑打字时，电脑完全没反应。
    // 回调同样跑在 reader 线程，这里只做一次带锁转发；**建窗口必须由 UI 层自己 Post**
    // （跨线程 CreateWindow 会消息泵错乱）。
    client_.onRequestInput = [this](const std::string& hint) {
        std::function<void(const std::string&)> cb;
        {
            std::lock_guard<std::mutex> lk(cbMu_);
            cb = onRequestInput_;
        }
        if (cb) cb(hint);
    };

    running_.store(true);
    worker_ = std::thread(&WirelessSession::worker, this);
}

WirelessSession::~WirelessSession() { stop(); }

void WirelessSession::stop() {
    if (!running_.exchange(false)) return;
    beaconRun_.store(false);
    client_.disconnect();
    // ★ 顺序不能反：worker 线程内部也会 join / 重建 beaconThread_（Idle、Manual、Auto
    //   三个分支都有），若这里先 join(beacon)，就会与 worker **并发 join 同一个
    //   std::thread**（未定义行为，实践中表现为退出时偶发 terminate / 卡死）；而 worker
    //   重建时对仍 joinable 的 thread 赋值同样会 terminate。现在只 join worker（由它收
    //   beacon），这里仅兜底，避免 beaconThread_ 析构时仍 joinable 触发 terminate。
    if (worker_.joinable()) worker_.join();
    if (beaconThread_.joinable()) beaconThread_.join();
}

void WirelessSession::startAuto() {
    std::lock_guard<std::mutex> lk(mu_);
    desire_ = Desire{Mode::Auto, {}, 0, true};
    snap_.error.clear();
    manualFailed_ = false;
    haveBeacon_ = false;
    APX_LOGI("9511 会话：切到自动发现模式");
}

void WirelessSession::connectManual(const std::string& host, uint16_t port) {
    std::lock_guard<std::mutex> lk(mu_);
    desire_ = Desire{Mode::Manual, host, port, true};
    snap_.error.clear();
    manualFailed_ = false;
    APX_LOGI("9511 会话：手动连接 {}:{}", host.c_str(), static_cast<unsigned>(port));
}

void WirelessSession::disconnect() {
    std::lock_guard<std::mutex> lk(mu_);
    desire_ = Desire{Mode::Idle, {}, 0, true};
    snap_.error.clear();
    manualFailed_ = false;
}

void WirelessSession::onBeacon(const std::string& host, uint16_t port, bool phone) {
    std::lock_guard<std::mutex> lk(mu_);
    // 手机信标优先（修复⑫）：TV 端 app 常驻自启、持续广播 APX1TV，此前「先到先连」
    // 会在手机断链重发现时把会话绑到 TV 上 —— TV 连接稳定不掉线，自愈泵永不触发，
    // 媒体通道（含音频流）从此全推给电视，手机端饿死无声，且两端 UI 都显示「已连接」。
    // 规则：已有手机信标待连时不替换；已有非手机信标时允许手机信标插队顶替。
    if (haveBeacon_ && (beaconPhone_ || !phone)) return;
    beaconHost_ = host;
    beaconPort_ = port;
    beaconPhone_ = phone;
    haveBeacon_ = true;
}

void WirelessSession::requestOpenScreen() {
    // 9511 受控端忽略 0x05；保留接口以兼容面板。
    client_.sendControl({0x05});
}

void WirelessSession::requestModule(int /*idx*/, bool /*on*/) {
    // 9511 控制面无模块开关子命令；保留接口以兼容面板。
}

bool WirelessSession::takeKeyFrameRequest() {
    // 9511 副屏走媒体通道，无需控制面关键帧请求。
    return false;
}

void WirelessSession::setTouchRect(int /*x*/, int /*y*/, int /*w*/, int /*h*/) {
    // 9511 控制面无触摸矩形概念；保留接口以兼容面板。
}

void WirelessSession::worker() {
    Mode mode = Mode::Idle;
    std::string host;
    uint16_t port = 0;
    bool attempt = false;

    while (running_.load()) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (desire_.fresh) {
                desire_.fresh = false;
                mode = desire_.mode;
                host = desire_.host;
                port = desire_.port;
                attempt = (mode != Mode::Idle);
                haveBeacon_ = false;
                manualFailed_ = false;
                connectStartMs_ = 0;
            }
        }

        if (mode == Mode::Idle) {
            if (client_.ready()) client_.disconnect();
            if (beaconRun_.load()) {
                beaconRun_.store(false);
                if (beaconThread_.joinable()) beaconThread_.join();
            }
            publish(LinkPhase::Idle, mode);
        } else if (mode == Mode::Auto) {
            if (client_.ready()) {
                publish(LinkPhase::Connected, mode);
            } else {
                if (!beaconRun_.load()) {
                    if (beaconThread_.joinable()) beaconThread_.join();
                    if (!running_.load()) {
                        publish(LinkPhase::Idle, mode);
                    } else {
                        beaconRun_.store(true);
                        beaconThread_ = std::thread(&WirelessSession::beaconLoop, this);
                    }
                }
                std::string bhost;
                uint16_t bport = 0;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    if (haveBeacon_) { bhost = beaconHost_; bport = beaconPort_; haveBeacon_ = false; }
                }
                if (!bhost.empty()) {
                    publish(LinkPhase::Connecting, mode);
                    if (client_.connect(bhost, bport, "")) {
                        connectedPeer_ = bhost + ":" + std::to_string(bport);
                        connectStartMs_ = nowMs();
                        publish(LinkPhase::Connected, mode);
                    } else {
                        publish(LinkPhase::Discovering, mode);
                    }
                } else {
                    publish(LinkPhase::Discovering, mode);
                }
            }
        } else {  // Manual
            if (beaconRun_.load()) {
                beaconRun_.store(false);
                if (beaconThread_.joinable()) beaconThread_.join();
            }
            if (client_.ready()) {
                publish(LinkPhase::Connected, mode);
            } else if (attempt) {
                attempt = false;
                publish(LinkPhase::Connecting, mode);
                if (client_.connect(host, port, "")) {
                    connectedPeer_ = host + ":" + std::to_string(port);
                    connectStartMs_ = nowMs();
                    publish(LinkPhase::Connected, mode);
                } else {
                    manualFailed_ = true;
                    publish(LinkPhase::Failed, mode);
                }
            } else {
                publish(manualFailed_ ? LinkPhase::Failed : LinkPhase::Idle, mode);
            }
        }

        std::this_thread::sleep_for(kTick);
    }
}

void WirelessSession::publish(LinkPhase ph, Mode m) {
    const auto st = client_.status();
    std::lock_guard<std::mutex> lk(mu_);
    const bool changed = snap_.phase != ph;
    snap_.phase = ph;
    snap_.autoMode = (m == Mode::Auto);
    snap_.peer = st.connected ? connectedPeer_ : (m == Mode::Manual ? desire_.host : "");
    snap_.rttMs = st.rttMs;
    snap_.upMs = (connectStartMs_ > 0 && st.connected) ? (nowMs() - connectStartMs_) : 0;
    if (ph == LinkPhase::Failed) snap_.error = "连不上 " + desire_.host;
    else if (ph == LinkPhase::Connected) snap_.error.clear();
    snap_.counters.mouse = st.mouse;
    snap_.counters.touch = st.touch;
    snap_.counters.keyboard = st.keyboard;
    snap_.counters.consumer = st.consumer;
    snap_.counters.dropped = st.dropped;
    if (changed) APX_LOGI("9511 会话状态：{}", linkPhaseName(ph));
}

SessionSnapshot WirelessSession::snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    return snap_;
}

bool WirelessSession::sendClipboard(const std::string& text) {
    // client_ 自带线程安全（内部入队 + writer 线程），UI 线程可直接调，不必绕后台。
    if (!client_.ready()) return false;
    return client_.sendClipboard(text);
}

bool WirelessSession::sendModuleToggle(const std::string& id, bool on) {
    if (!client_.ready()) return false;
    return client_.sendModuleToggle(id, on);
}

bool WirelessSession::connected() const { return client_.ready(); }

bool WirelessSession::requestInput(const std::string& hint) {
    if (!client_.ready()) return false;
    return client_.sendRequestInput(hint);
}

void WirelessSession::setOnRequestInput(std::function<void(const std::string&)> cb) {
    std::lock_guard<std::mutex> lk(cbMu_);
    onRequestInput_ = std::move(cb);
}

bool WirelessSession::sendInputText(const std::string& text, uint8_t flags) {
    if (!client_.ready()) return false;
    return client_.sendInputText(text, flags);
}

bool WirelessSession::sendInputDone() {
    if (!client_.ready()) return false;
    return client_.sendInputDone();
}

#if defined(_WIN32)
/// 来源地址是否就是本机（网络字节序）。
/// 为什么必须有：本机的定向广播会被自己收到，不过滤就会把**自己的信标**当成对端，
/// 于是面板「已连接」到自己 —— 看着连上了，实际一开始就没连到手机。
bool isSelfAddress(uint32_t saddrNetOrder) {
    static uint32_t addrs[32] = {0};
    static int n = -1;
    static long long ms = 0;
    const long long now = nowMs();
    if (n < 0 || now - ms > 5000) {   // 5s 刷新：插拔网卡 / 换网随时可能变
        ms = now;
        n = 0;
        SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s != INVALID_SOCKET) {
            INTERFACE_INFO infos[32]{};
            DWORD bytes = 0;
            if (WSAIoctl(s, SIO_GET_INTERFACE_LIST, nullptr, 0, infos, sizeof(infos),
                         &bytes, nullptr, nullptr) == 0) {
                const size_t cnt = bytes / sizeof(INTERFACE_INFO);
                for (size_t i = 0; i < cnt && n < 32; ++i)
                    addrs[n++] = infos[i].iiAddress.AddressIn.sin_addr.s_addr;
            }
            closesocket(s);
        }
    }
    for (int i = 0; i < n; ++i) if (addrs[i] == saddrNetOrder) return true;
    return false;
}
#endif

void WirelessSession::beaconLoop() {
#if defined(_WIN32)
    ensureWsa();   // 必须先初始化 Winsock，否则 socket() 失败导致发现永久失效
    Sock b = socket(AF_INET, SOCK_DGRAM, 0);
    if (b == INVALID_SOCKET) return;
    BOOL br = TRUE;
    setsockopt(b, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&br), sizeof(br));
#else
    int b = socket(AF_INET, SOCK_DGRAM, 0);
    if (b < 0) return;
    int br = 1;
    setsockopt(b, SOL_SOCKET, SO_BROADCAST, &br, sizeof(br));
    int reuse = 1;
    setsockopt(b, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(kBeaconPort);
    if (bind(b, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
#if defined(_WIN32)
        closesocket(b);
#else
        close(b);
#endif
        return;
    }
#if defined(_WIN32)
    DWORD rcvToMs = 200;
    setsockopt(b, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&rcvToMs), sizeof(rcvToMs));
#else
    timeval rcvTo{};
    rcvTo.tv_sec = 0;
    rcvTo.tv_usec = 200 * 1000;
    setsockopt(b, SOL_SOCKET, SO_RCVTIMEO, &rcvTo, sizeof(rcvTo));
#endif
    char buf[1024];
    while (beaconRun_.load()) {
        sockaddr_in from{};
#if defined(_WIN32)
        int fromLen = sizeof(from);
#else
        socklen_t fromLen = sizeof(from);
#endif
        const int n = recvfrom(b, buf, sizeof(buf) - 1, 0,
                                reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }
        buf[n] = '\0';
        std::string hostIp;
        char ipStr[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &from.sin_addr, ipStr, sizeof(ipStr)))
            hostIp = ipStr;
        uint16_t bport = 0;
        std::string dummy;
        bool phone = false;
        if (!hostIp.empty() && parseBeacon(buf, static_cast<size_t>(n), dummy, bport, &phone)
#if defined(_WIN32)
            && !isSelfAddress(from.sin_addr.s_addr)
#endif
        ) {
            onBeacon(hostIp, bport, phone);
        }
    }
#if defined(_WIN32)
    closesocket(b);
#else
    close(b);
#endif
}

}  // namespace apxpc::wireless
