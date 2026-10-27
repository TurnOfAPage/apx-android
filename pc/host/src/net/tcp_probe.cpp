// 本机 TCP 端口占用探测 —— 见头文件里的语义边界说明。
//
// 为什么要单独一个 TU 而不是留在 panel_http.cpp 里：
//   ① 它需要一条**能真跑起来**的用例（起监听 → 探 → 关监听 → 再探）；
//      panel_http.cpp 拖着整个 UI/HostBundle 依赖链，链进测试太重；
//   ② Windows 侧（web_panel 走 WebView2）也可能要用，放 core 里两边都能引用。
#include "apxpc/net/tcp_probe.hpp"

#include <cerrno>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_type = int;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socklen_type = socklen_t;
#endif

namespace apxpc::net {
namespace {

#if defined(_WIN32)
struct WsaOnce {
    bool ok{false};
    WsaOnce() {
        WSADATA d{};
        ok = (WSAStartup(MAKEWORD(2, 2), &d) == 0);
    }
    ~WsaOnce() {
        if (ok) WSACleanup();
    }
};
void ensureSockets() {
    static WsaOnce once;
    (void)once;
}
void closeSock(int fd) { ::closesocket(static_cast<SOCKET>(fd)); }
int lastErr() { return WSAGetLastError(); }
constexpr int kInProgress = WSAEWOULDBLOCK;
#else
void ensureSockets() {}
void closeSock(int fd) { ::close(fd); }
int lastErr() { return errno; }
constexpr int kInProgress = EINPROGRESS;
#endif

}  // namespace

bool tcpPortResponds(const char* host, uint16_t port, int timeoutMs) {
    if (!host || !*host || port == 0) return false;
    ensureSockets();

    const int fd = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
#if defined(_WIN32)
    if (fd == static_cast<int>(INVALID_SOCKET)) return false;
#else
    if (fd < 0) return false;
#endif

    bool open = false;
    for (;;) {   // 单轮 + break：用循环只是想让「清理」只有一处
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1) break;

        if (timeoutMs <= 0) timeoutMs = 1;

        // 非阻塞 connect + select：回环上本应立刻出结果，但沙箱/防火墙可能给出
        // 丢包地址 —— 阻塞 connect 会把启动路径挂住，这里必须设上限。
#if defined(_WIN32)
        u_long nb = 1;
        ::ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &nb);
#else
        const int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif

        const int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (rc == 0) {
            open = true;
            break;
        }
        if (lastErr() != kInProgress) break;   // 立刻被拒（ECONNREFUSED 等）

        fd_set wf;
        FD_ZERO(&wf);
        FD_SET(fd, &wf);
        timeval tv{};
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        if (::select(fd + 1, nullptr, &wf, nullptr, &tv) <= 0) break;   // 超时/出错 → 当作没人

        int err = 0;
        socklen_type elen = sizeof(err);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR,
                         reinterpret_cast<char*>(&err), &elen) == 0) {
            open = (err == 0);
        }
        break;
    }

    closeSock(fd);
    return open;
}

bool tcpPortRespondsLocal(uint16_t port, int timeoutMs) {
    return tcpPortResponds("127.0.0.1", port, timeoutMs);
}

}  // namespace apxpc::net
