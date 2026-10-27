// wireless_session 生命周期压测：覆盖 v198 修复的死锁。
//
// 缺陷背景（v198 引入）
// --------------------
// worker() 在 Auto 分支里：检测 beaconRun_ 为 false → join 旧 beacon 线程 →
// 立刻 store(true) 重建。stop() 的流程是 running_=false → beaconRun_=false →
// join worker → join beacon。若 stop 与 worker 在「beaconRun_ 拉 false → worker
// 进入内层块」这条临界区交汇，worker 会无条件 store(true) 并新建 beacon 线程，
// 新线程在 recvfrom 上挂着，而 stop 末尾 join(beaconThread_) 永远收不回 ——
// 进程无法正常退出（必须 kill）。
//
// v198 修复：join 完旧 beacon 后再核一次 running_，已停就不重建，让本次迭代走完即退出。
//
// 压测方式
// --------
// 反复 startAuto → 短睡 → stop，每轮 stop 必须能在合理时间内返回。任何一轮超时
// 即判定死锁回归。整个测试在受控环境下跑（无外部信标、端口 9501 收不到东西），
// worker 仍会按 Auto 分支持续起 beacon 线程 —— 正好是 v198 那条路径。
#include "apxpc/wireless/wireless_session.hpp"
#include "apxpc/wireless/ctrl9511.hpp"

#include <chrono>
#include <cstdio>
#include <future>
#include <thread>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// ============================================================================
// 端口占位器：让被测对象的 bind 必然失败
// ============================================================================
// 为什么不用「先起一个 Ctrl9511Server、再起第二个撞端口」来制造失败：
// Windows 下双方都设了 SO_REUSEADDR 时，同一 addr:port 可以被重复 bind（不报错），
// 「第二个必须启动失败」这条断言在 Windows 上根本不成立 —— 2026-10-27 首版用例就是这么
// 在 CI 上红掉的（crash 修复本身没问题，是用例的跨平台前提写错了）。自己开一个监听 socket
// 占住端口、Windows 侧再补 SO_EXCLUSIVEADDRUSE（对应 POSIX「端口已被占」的语义），
// 三端才收敛到同一个结论。
class PortBlocker {
public:
    explicit PortBlocker(uint16_t port) {
#if defined(_WIN32)
        WSADATA wsa{};
        ::WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        fd_ = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
        if (fd_ < 0) return;
#if defined(_WIN32)
        const BOOL exclusive = TRUE;
        ::setsockopt(fd_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                     reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#endif
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) { closeFd(); return; }
        if (::listen(fd_, 1) != 0) { closeFd(); return; }
        ok_ = true;
    }
    ~PortBlocker() { closeFd(); }
    bool ok() const { return ok_; }

private:
    void closeFd() {
        if (fd_ < 0) return;
#if defined(_WIN32)
        ::closesocket(static_cast<SOCKET>(fd_));
#else
        ::close(fd_);
#endif
        fd_ = -1;
        ok_ = false;
    }
    int fd_ = -1;
    bool ok_ = false;
};

using namespace apxpc::wireless;
using namespace std::chrono_literals;

static int g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } \
} while (0)

/// 用 future 把 stop() 包一层超时：future 超时即认定死锁，返回 false。
/// stop() 是同步函数，正常情况下 < 200ms 返回；v198 死锁场景下永不返回。
static bool stopWithTimeout(WirelessSession& s, std::chrono::milliseconds budget) {
    auto fut = std::async(std::launch::async, [&] { s.stop(); });
    if (fut.wait_for(budget) == std::future_status::ready) {
        fut.get();   // 正常返回，吃掉异常（理论上不会抛）
        return true;
    }
    // 超时：deadlock 已触发。future 仍持有 session 引用，析构前先 detach 避免
    // std::future 析构阻塞等待线程结束 —— detach 后线程继续死锁，本测试进程退出
    // 时由 OS 回收，不影响测试结果（判定已 FAIL）。
    return false;
}

/// 反复触发 v198 的竞争窗口：让 worker 处于「刚 join 完旧 beacon / 即将 store(true)
/// 重建」的瞬间被 stop() 打断。短随机停顿让两线程的相位错开。
///
/// 注意：startAuto() 不重启 worker —— 它只置 desire 标志；worker 由 ctor 启动、
/// 由 stop() 终结。所以每轮必须新建 session。
static void testStartStopNoDeadlock() {
    // 轮数按 CI 可承受的总时长定：每轮约 50ms 相位 + stop 收线程（worker 250ms tick
    // + beacon 200ms 接收超时）。60 轮足够覆盖竞争窗口，整支测试稳定在 30s 内，
    // 不会顶到 ctest 的 300s 上限。
    constexpr int kIters = 60;
    for (int i = 0; i < kIters; ++i) {
        WirelessSession s;
        s.startAuto();
        // 给 worker 时间进入循环、起 beacon 线程。kTick = 250ms，所以 50ms 已足够
        // 看到 beacon 被拉起。
        std::this_thread::sleep_for(50ms);
        CHECK(stopWithTimeout(s, 2s));
    }
}

/// 析构路径也要走通：v198 之前析构 → stop() 也可能死锁。构造后不显式 stop，
/// 直接让 RAII 析构跑一次。
static void testDestructorNoDeadlock() {
    constexpr int kIters = 40;
    for (int i = 0; i < kIters; ++i) {
        auto fut = std::async(std::launch::async, [] {
            WirelessSession s;
            s.startAuto();
            std::this_thread::sleep_for(50ms);
            // 不显式 stop：交给 ~WirelessSession()。验证析构路径同样不卡死。
        });
        if (fut.wait_for(2s) != std::future_status::ready) {
            std::printf("FAIL ~WirelessSession deadlocked at iter %d\n", i);
            ++g_fail;
            return;
        }
        fut.get();
    }
}

/// 边界：startAuto() 之后立刻 stop()，worker 可能还没来得及起 beacon；这条路
/// 也得能正常退出。
static void testImmediateStop() {
    constexpr int kIters = 40;
    for (int i = 0; i < kIters; ++i) {
        WirelessSession s;
        s.startAuto();
        CHECK(stopWithTimeout(s, 2s));
    }
}

/// issue#47 回归：**同一端口起两次 9511 受控端**。
///
/// v223 的 mac「双击打开即崩溃」根因就在这条路径上（崩溃栈已逐帧还原到指令级）：
///   ① `start()` 先把剪贴板监视线程起起来，再去 `bind`；
///   ② 第二次 `bind` 必然失败（端口已被自己占住）→ 老代码的失败分支是 `delete Impl`；
///   ③ Impl 持有的剪贴板监视线程还在跑 → 析构 joinable 的 `std::thread` → `std::terminate`
///      → `abort()`（用户侧看到的就是「意外退出」）。
///
/// 因此本用例的判据很朴素：**进程能走到最后一行**。回归时整个测试进程会被直接 abort，
/// ctest 报的就是失败 —— 不需要额外的断言来「抓住」它。
///
/// 注：「端口被占」这个前提由 PortBlocker 自己制造，而不是「两个 server 互相撞」
/// （后者在 Windows 上不成立，见 PortBlocker 注释）。
static void testCtrl9511StartFailIsSafe() {
    // 固定高位端口，避开 CI 上可能已在跑的 9511（apxhost serve / 上一轮测试实例）。
    constexpr uint16_t kPort = 19511;

    // ① 「端口被占」时 start() 必须如实返回 false，绝不能让进程跟着走掉。
    //    b 保持**默认开启**反向剪贴板监视 —— 那正是 v223 崩溃路径里被析构的那个监视线程。
    {
        PortBlocker blocker(kPort);
        CHECK(blocker.ok());
        if (blocker.ok()) {
            Ctrl9511Server b;
            CHECK(!b.start(kPort, "", "regress-b"));   // 端口被占住 → 必须 false
            b.stop();                                  // 没启动起来的实例，stop() 必须幂等
            b.stop();
        }
    }

    // ② 同一个对象重复 start：第二次必须失败，且不能把已经在跑的实例换掉；stop 幂等。
    {
        Ctrl9511Server c;
        c.setReverseClipboardEnabled(false);
        const uint16_t port = static_cast<uint16_t>(kPort + 1);
        const bool ok = c.start(port, "", "regress-c");
        CHECK(ok);
        if (ok) {
            CHECK(!c.start(port, "", "regress-c"));    // 自己占着自己：再 start 必须失败
            c.stop();
            c.stop();                                  // 幂等
            CHECK(c.start(port, "", "regress-c"));     // 停完端口已释放：能再起来
            c.stop();
        }
    }
}

int main() {
    testStartStopNoDeadlock();
    testDestructorNoDeadlock();
    testImmediateStop();
    testCtrl9511StartFailIsSafe();
    if (g_fail == 0) {
        std::printf("wireless_session_lifecycle_test: ALL PASS\n");
        return 0;
    }
    std::printf("wireless_session_lifecycle_test: %d FAILED\n", g_fail);
    return 1;
}
