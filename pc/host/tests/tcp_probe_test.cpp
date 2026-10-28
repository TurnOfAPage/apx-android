// tcp_probe 的用例：真起监听、真探、真关掉再探一遍。
//
// 为什么要真起 socket：这个函数的全部价值就是「说准有没有人在监听」。
// 拿 mock 去测它等于什么都没测 —— 上一轮的教训正是「测试跑过了」不等于
// 「这条路径真的被执行」。所以这里走真实回环连接，除 OS 外零依赖。
#include "apxpc/net/tcp_probe.hpp"

#include <cstdio>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_type = int;
namespace {
void ensureSockets() {
    static const bool once = [] {
        WSADATA d{};
        return WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }();
    (void)once;
}
void closeSock(int fd) { ::closesocket(static_cast<SOCKET>(fd)); }
int sockBad() { return static_cast<int>(INVALID_SOCKET); }
}  // namespace
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socklen_type = socklen_t;
namespace {
void ensureSockets() {}
void closeSock(int fd) { ::close(fd); }
int sockBad() { return -1; }
}  // namespace
#endif

namespace {

int g_fail = 0;
int g_pass = 0;

void check(bool cond, const char* what) {
    if (cond) {
        ++g_pass;
        std::printf("  ok   %s\n", what);
    } else {
        ++g_fail;
        std::printf("  FAIL %s\n", what);
    }
}

/// 起一个监听 socket，绑 INADDR_ANY、端口由内核分配，返回 fd 并回填端口。
/// 绑 INADDR_ANY 是刻意的：**真受控端就是这么绑的**（见 ctrl9511.cpp），
/// 探测方从回环进来必须同样能探到 —— 这条要是错了，功能就整体失效。
int listenAny(uint16_t* outPort) {
    ensureSockets();
    const int fd = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (fd == sockBad()) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(fd, 4) != 0) {
        closeSock(fd);
        return -1;
    }
    socklen_type alen = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &alen) != 0) {
        closeSock(fd);
        return -1;
    }
    *outPort = ntohs(addr.sin_port);
    return fd;
}

}  // namespace

int main() {
    std::printf("tcp_probe_test\n");

    // 1) 有人在监听 → true
    uint16_t port = 0;
    const int lfd = listenAny(&port);
    if (lfd < 0) {
        std::printf("  无法起监听 socket，环境不支持：用例作废\n");
        return 0;
    }
    std::printf("  监听端口 = %u\n", static_cast<unsigned>(port));
    check(apxpc::net::tcpPortRespondsLocal(port, 300), "监听中 → true");

    // 2) 关掉之后 → false（同一端口，此刻无人监听）
    closeSock(lfd);
    check(!apxpc::net::tcpPortRespondsLocal(port, 300), "已释放 → false");

    // 3) 连续探测结果一致（确认没有副作用）
    uint16_t port2 = 0;
    const int lfd2 = listenAny(&port2);
    if (lfd2 >= 0) {
        const bool a = apxpc::net::tcpPortRespondsLocal(port2, 300);
        const bool b = apxpc::net::tcpPortRespondsLocal(port2, 300);
        check(a && b, "连续两次探测结果一致（无副作用）");
        closeSock(lfd2);
    }

    // 4) 参数边界：一律 false，不抛异常、也不去连
    check(!apxpc::net::tcpPortRespondsLocal(0, 100), "端口 0 → false");
    check(!apxpc::net::tcpPortResponds(nullptr, port, 100), "host=null → false");
    check(!apxpc::net::tcpPortResponds("", port, 100), "host 空串 → false");
    check(!apxpc::net::tcpPortResponds("not-an-ip", port, 100), "非法 IP → false");

    std::printf("结果：%d 通过，%d 失败\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
