// LocalSend 接收端「真开端口」的集成测试：走完 prepare-upload → upload → 落盘 → 校验。
//
// 为什么还要一个集成测试：localsend_protocol_test 只碰纯函数，路由、token 查找、
// 重名、半截文件、停止路径这些一个都没覆盖。真机上「手机发过来收不到」这类问题
// 恰恰出在这些地方，而它们只有把服务真跑起来才验得到。
//
// 只在 127.0.0.1 上收发，不依赖局域网、不需要组播；客户端手写 HTTP（服务端已声明
// Connection: close + Content-Length，读到 EOF 就是完整响应），不引第三方库。
//
// 两条纪律（Windows 上踩过）：
//   1) 测试里用的文件名一律 ASCII。非 ASCII 名字在 Windows 上要过 std::filesystem
//      的窄字符转换，本机 codepage 表示不了就出事；中文名清洗已经由
//      localsend_protocol_test 的纯函数用例钉住了，集成测试不必再踩一遍。
//   2) 每个用例前打 [step] 标记：Windows 上崩了的话，日志里最后一条 step 就是案发现场。
#include "apxpc/localsend/local_send_receiver.hpp"
#include "apxpc/localsend/sha256.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

using namespace apxpc::localsend;

static int g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); std::fflush(stdout); ++g_fail; } \
} while (0)

static void step(const char* s) {
    std::printf("[step] %s\n", s);
    std::fflush(stdout);
}

// ---------------------------------------------------------------- socket 垫片
#if defined(_WIN32)
using Sock = SOCKET;
static const Sock kBad = INVALID_SOCKET;
static void closeSock(Sock s) { ::closesocket(s); }
static void ensureNet() {
    static const bool once = [] { WSADATA w{}; ::WSAStartup(MAKEWORD(2, 2), &w); return true; }();
    (void)once;
}
#else
using Sock = int;
static const Sock kBad = -1;
static void closeSock(Sock s) { ::close(s); }
static void ensureNet() {}
#endif

// 客户端一律带 5s 收发超时：真出问题时是「这条用例失败」而不是「CI 卡到超时」
static Sock connectTo(uint16_t port) {
    const Sock s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kBad) return kBad;
#if defined(_WIN32)
    DWORD tv = 5000;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    timeval tv{};
    tv.tv_sec = 5;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        closeSock(s);
        return kBad;
    }
    return s;
}

struct Resp {
    int status = 0;
    std::string body;
};

static Resp doReq(uint16_t port, const std::string& raw) {
    Resp r;
    const Sock s = connectTo(port);
    if (s == kBad) return r;
    size_t off = 0;
    while (off < raw.size()) {
        const int n = ::send(s, raw.data() + off, static_cast<int>(raw.size() - off), 0);
        if (n <= 0) break;
        off += static_cast<size_t>(n);
    }
    std::string all;
    char buf[4096];
    while (true) {
        const int n = ::recv(s, buf, static_cast<int>(sizeof buf), 0);
        if (n <= 0) break;
        all.append(buf, static_cast<size_t>(n));
    }
    closeSock(s);

    const size_t sp = all.find(' ');
    if (sp != std::string::npos && sp + 1 < all.size()) r.status = std::atoi(all.c_str() + sp + 1);
    const size_t he = all.find("\r\n\r\n");
    if (he != std::string::npos) r.body = all.substr(he + 4);
    return r;
}

static Resp httpReq(uint16_t port, const std::string& method, const std::string& target,
                    const std::string& body = std::string(),
                    const std::string& ctype = "application/json") {
    std::string raw = method + " " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n";
    if (!body.empty()) raw += "Content-Type: " + ctype + "\r\n";
    raw += "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
    raw += body;
    return doReq(port, raw);
}

// ---------------------------------------------------------------- 小工具
static std::string jfield(const std::string& json, const std::string& key) {
    const std::string pat = "\"" + key + "\":\"";
    const size_t p = json.find(pat);
    if (p == std::string::npos) return std::string();
    const size_t b = p + pat.size();
    const size_t e = json.find('"', b);
    return e == std::string::npos ? std::string() : json.substr(b, e - b);
}

static std::string peerInfo() {
    return R"({"alias":"Phone","version":"2.2","deviceModel":"X","deviceType":"mobile",)"
           R"("fingerprint":"FP-TEST","port":53318,"protocol":"http","download":false})";
}

static std::string readFile(const std::string& path, bool* ok) {
    std::ifstream in(path.c_str(), std::ios::binary);
    *ok = in.good();
    if (!*ok) return std::string();
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// 只做「把 payload 发过去」的封装；状态码交给调用方判断
static Resp prepareAndUpload(uint16_t port, const std::string& fileName, const std::string& payload,
                             const std::string& shaOrEmpty, const std::string& fileId,
                             std::string* sidOut) {
    std::string file = "{\"id\":\"" + fileId + "\",\"fileName\":\"" + fileName + "\",\"size\":" +
                       std::to_string(payload.size()) + ",\"fileType\":\"application\"";
    if (!shaOrEmpty.empty()) file += ",\"sha256\":\"" + shaOrEmpty + "\"";
    file += "}";
    const std::string prep =
        "{\"info\":" + peerInfo() + ",\"files\":{\"" + fileId + "\":" + file + "}}";
    const Resp pr = httpReq(port, "POST", "/api/localsend/v2/prepare-upload", prep);
    if (pr.status != 200) return pr;
    const std::string sid = jfield(pr.body, "sessionId");
    if (sidOut) *sidOut = sid;
    return httpReq(port, "POST",
                   "/api/localsend/v2/upload?sessionId=" + sid + "&fileId=" + fileId,
                   payload, "application/octet-stream");
}

// ---------------------------------------------------------------- 用例
static void run(uint16_t port, const std::string& dir) {
    step("info");
    {
        const Resp r = httpReq(port, "GET", "/api/localsend/v2/info");
        CHECK(r.status == 200);
        CHECK(jfield(r.body, "version") == "2.2");
        CHECK(!jfield(r.body, "alias").empty());
        CHECK(!jfield(r.body, "fingerprint").empty());
        CHECK(r.body.find("\"announce\"") == std::string::npos);   // 设备信息不带 announce 标志
    }

    step("register");
    {
        const Resp r = httpReq(port, "POST", "/api/localsend/v2/register", peerInfo());
        CHECK(r.status == 200);
        CHECK(jfield(r.body, "deviceType") == "desktop");
    }

    step("prepare + upload（穿越名 + sha256 校验）");
    {
        const std::string payload = std::string("hello localsend ") + std::string(2000, 'x');
        const std::string sha = sha256Hex(payload.data(), payload.size());
        std::string sid;
        // 名字里带 ../：落盘时必须被替换掉，不能跑出目标目录
        const Resp ur = prepareAndUpload(port, "../../etc/passwd", payload, sha, "f1", &sid);
        CHECK(!sid.empty());
        CHECK(ur.status == 204);   // 与 Android 端实现一致（2xx 即成功）

        bool ok = false;
        const std::string data = readFile(dir + "/_.._etc_passwd", &ok);
        CHECK(ok);
        CHECK(data == payload);
        CHECK(sha256Hex(data.data(), data.size()) == sha);
    }

    step("重名不覆盖：(2) 后缀");
    {
        for (int i = 0; i < 2; ++i) {
            const Resp ur = prepareAndUpload(port, "dup.bin", "abc", std::string(), "d", nullptr);
            CHECK(ur.status == 204);
        }
        bool ok1 = false, ok2 = false;
        const std::string a = readFile(dir + "/dup.bin", &ok1);
        const std::string b = readFile(dir + "/dup (2).bin", &ok2);
        CHECK(ok1);
        CHECK(ok2);
        CHECK(a == "abc" && b == "abc");
    }

    step("未知会话/未知 token → 404");
    {
        const Resp r = httpReq(port, "POST", "/api/localsend/v2/upload?sessionId=nope&fileId=nope",
                               "x", "application/octet-stream");
        CHECK(r.status == 404);
    }

    step("upload 缺 Content-Length → 400");
    {
        const Resp pr = httpReq(port, "POST", "/api/localsend/v2/prepare-upload",
                                std::string("{\"info\":") + peerInfo() +
                                    ",\"files\":{\"z\":{\"id\":\"z\",\"fileName\":\"nc.bin\","
                                    "\"size\":5,\"fileType\":\"application\"}}}");
        CHECK(pr.status == 200);
        const std::string sid = jfield(pr.body, "sessionId");
        const Resp r = doReq(port, "POST /api/localsend/v2/upload?sessionId=" + sid +
                                      "&fileId=z HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                                      "Connection: close\r\n\r\n");
        CHECK(r.status == 400);
    }

    step("清单不合法 → 400");
    {
        const Resp r = httpReq(port, "POST", "/api/localsend/v2/prepare-upload", "not json at all");
        CHECK(r.status == 400);
        const Resp r2 = httpReq(port, "POST", "/api/localsend/v2/prepare-upload",
                                "{\"info\":{\"alias\":\"x\"},\"files\":{}}");
        CHECK(r2.status == 400);
    }

    step("声明 sha256 不符：保留文件但如实告警");
    {
        const std::string zeros(64, '0');
        const Resp ur = prepareAndUpload(port, "bad.bin", "abc", zeros, "b", nullptr);
        CHECK(ur.status == 204);
        bool ok = false;
        const std::string got = readFile(dir + "/bad.bin", &ok);
        CHECK(ok);
        CHECK(got == "abc");
    }

    step("cancel 不崩");
    {
        const Resp r = httpReq(port, "POST", "/api/localsend/v2/cancel?sessionId=whatever");
        CHECK(r.status >= 200 && r.status < 300);
    }
}

int main() {
    ensureNet();
    step("启动接收端");
    // 落盘目录就用进程当前目录下的固定名：路径全 ASCII，Windows 上不必过窄字符转换
    const std::string dir = "apxpc_localsend_upload_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    Options opt;
    opt.port = 53981;                     // 与默认 53317 错开，免得撞上真在跑的实例
    opt.dir = dir;
    opt.alias = "APX-Test-PC";
    opt.deviceModel = "APX-Test-PC";
    opt.multicastGroup = "239.255.77.77"; // 测试专用组播组，别去真地址上广播
    opt.announcePeriodMs = 600000;

    LocalSendReceiver rx;
    int fileCallbacks = 0;
    if (!rx.start(opt, [&fileCallbacks](const std::string&, uint64_t) { ++fileCallbacks; })) {
        std::printf("FAIL 接收端起不来：%s\n", rx.lastError().c_str());
        return 1;
    }
    CHECK(rx.running());
    CHECK(!rx.fingerprint().empty());

    run(rx.listenPort(), dir);

    step("回调计数");
    CHECK(fileCallbacks == 4);   // 正常 1 + 重名 2 + sha 不符 1

    // ---- stop() 必须收得回来 ----
    // 这个接收端起两个常驻线程 + 4 个 worker，全部靠 select + 200ms 超时看 running 退出。
    // 上一个同类缺陷（beacon 阻塞在 recvfrom）就是这样把停止路径卡死、CI 判超时的。
    step("stop()");
    const auto t0 = std::chrono::steady_clock::now();
    rx.stop();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count();
    CHECK(!rx.running());
    CHECK(ms < 2000);

    std::filesystem::remove_all(dir, ec);

    if (g_fail == 0) {
        std::printf("localsend_upload_test: ALL PASS (stop %lldms)\n", static_cast<long long>(ms));
        return 0;
    }
    std::printf("localsend_upload_test: %d FAILED\n", g_fail);
    return 1;
}
