// LocalSend 接收端「真开端口」的集成测试：走完 prepare-upload → upload → 落盘 → 校验。
//
// 为什么还要一个集成测试：localsend_protocol_test 只碰纯函数，路由、token 查找、
// 重名、半截文件、停止路径这些一个都没覆盖。真机上「手机发过来收不到」这类问题
// 恰恰出在这些地方，而它们只有把服务真跑起来才验得到。
//
// 只在 127.0.0.1 上收发，不依赖局域网、不需要组播；客户端手写 HTTP（服务端已声明
// Connection: close + Content-Length，读到 EOF 就是完整响应），不引第三方库。
#include "apxpc/localsend/local_send_receiver.hpp"
#include "apxpc/localsend/sha256.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

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
namespace fs = std::filesystem;

static int g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } \
} while (0)

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

// 客户端一律带 5s 收发超时：真出问题时是「测试失败」而不是「CI 卡到超时」
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
    char buf[8192];
    while (true) {
        const int n = ::recv(s, buf, sizeof buf, 0);
        if (n <= 0) break;
        all.append(buf, static_cast<size_t>(n));
    }
    closeSock(s);

    const size_t sp = all.find(' ');
    if (sp != std::string::npos) r.status = std::atoi(all.c_str() + sp + 1);
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

// ---------------------------------------------------------------- 用例
static void run(const fs::path& dir, uint16_t port) {
    // ---- info ----
    {
        const Resp r = httpReq(port, "GET", "/api/localsend/v2/info");
        CHECK(r.status == 200);
        CHECK(jfield(r.body, "version") == "2.2");
        CHECK(!jfield(r.body, "alias").empty());
        CHECK(!jfield(r.body, "fingerprint").empty());
        CHECK(r.body.find("\"announce\"") == std::string::npos);   // 设备信息不带 announce 标志
    }

    // ---- register：必须回本机的设备信息，对端据此确认可达 ----
    {
        const Resp r = httpReq(port, "POST", "/api/localsend/v2/register", peerInfo());
        CHECK(r.status == 200);
        CHECK(jfield(r.body, "deviceType") == "desktop");
    }

    // ---- 正常三步：prepare-upload → upload → 落盘 ----
    const std::string payload = std::string("hello localsend ") + std::string(2000, 'x');
    const std::string sha = sha256Hex(payload.data(), payload.size());
    std::string sid, token;
    {
        const std::string prep =
            "{\"info\":" + peerInfo() +
            ",\"files\":{\"f1\":{\"id\":\"f1\",\"fileName\":\"../../测试 文件.bin\","
            "\"size\":" + std::to_string(payload.size()) +
            ",\"fileType\":\"application\",\"sha256\":\"" + sha + "\"}}}";
        const Resp r = httpReq(port, "POST", "/api/localsend/v2/prepare-upload", prep);
        CHECK(r.status == 200);
        sid = jfield(r.body, "sessionId");
        token = "f1";
        // files 是个对象，fileId 直接用 id 当 token（与 Android 端 respFiles.put(key, key) 一致）
        CHECK(!sid.empty());
        CHECK(r.body.find("\"f1\":\"f1\"") != std::string::npos);
    }
    {
        const Resp r = httpReq(port, "POST",
                               "/api/localsend/v2/upload?sessionId=" + sid + "&fileId=" + token,
                               payload, "application/octet-stream");
        CHECK(r.status == 204);   // 与 Android 端实现一致（2xx 即成功）
    }
    // 落盘：名字里的分隔符被替换、超不出目标目录、内容与摘要都对得上
    {
        std::vector<fs::path> got;
        for (const auto& e : fs::directory_iterator(dir)) got.push_back(e.path());
        CHECK(got.size() == 1);
        if (got.size() == 1) {
            CHECK(got[0].filename().string().find('/') == std::string::npos);
            CHECK(got[0].filename().string().find('\\') == std::string::npos);
            const std::string data = [&] {
                std::FILE* f = std::fopen(got[0].string().c_str(), "rb");
                std::string s;
                if (f) {
                    char b[4096];
                    size_t n;
                    while ((n = std::fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
                    std::fclose(f);
                }
                return s;
            }();
            CHECK(data == payload);
            CHECK(sha256Hex(data.data(), data.size()) == sha);
            CHECK(fs::absolute(got[0]).string().rfind(fs::absolute(dir).string(), 0) == 0);
        }
    }

    // ---- 重名不覆盖：(2) 后缀 ----
    {
        const std::string name = "dup.bin";
        for (int i = 0; i < 2; ++i) {
            const std::string prep =
                "{\"info\":" + peerInfo() +
                ",\"files\":{\"d\":{\"id\":\"d\",\"fileName\":\"" + name +
                "\",\"size\":3,\"fileType\":\"application\"}}}";
            const Resp pr = httpReq(port, "POST", "/api/localsend/v2/prepare-upload", prep);
            CHECK(pr.status == 200);
            const std::string s2 = jfield(pr.body, "sessionId");
            const Resp ur = httpReq(port, "POST",
                                    "/api/localsend/v2/upload?sessionId=" + s2 + "&fileId=d",
                                    "abc", "application/octet-stream");
            CHECK(ur.status == 204);
        }
        bool base = false, dup = false;
        for (const auto& e : fs::directory_iterator(dir)) {
            const std::string n = e.path().filename().string();
            if (n == name) base = true;
            if (n.find("(2)") != std::string::npos) dup = true;
        }
        CHECK(base);
        CHECK(dup);
    }

    // ---- 未知会话/未知 token → 404 ----
    {
        const Resp r = httpReq(port, "POST", "/api/localsend/v2/upload?sessionId=nope&fileId=nope",
                               "x", "application/octet-stream");
        CHECK(r.status == 404);
    }

    // ---- upload 没有 Content-Length → 400（不知道边界就不能猜着读）----
    {
        const std::string prep =
            "{\"info\":" + peerInfo() +
            ",\"files\":{\"z\":{\"id\":\"z\",\"fileName\":\"nc.bin\",\"size\":5,"
            "\"fileType\":\"application\"}}}";
        const Resp pr = httpReq(port, "POST", "/api/localsend/v2/prepare-upload", prep);
        CHECK(pr.status == 200);
        const std::string s3 = jfield(pr.body, "sessionId");
        const Resp r = doReq(port, "POST /api/localsend/v2/upload?sessionId=" + s3 +
                                      "&fileId=z HTTP/1.1\r\nHost: 127.0.0.1\r\n"
                                      "Connection: close\r\n\r\n");
        CHECK(r.status == 400);
    }

    // ---- 清单不合法 → 400（不能回 200 让对端以为受理了）----
    {
        const Resp r = httpReq(port, "POST", "/api/localsend/v2/prepare-upload", "not json at all");
        CHECK(r.status == 400);
        const Resp r2 = httpReq(port, "POST", "/api/localsend/v2/prepare-upload",
                                R"({"info":{"alias":"x"},"files":{}})");
        CHECK(r2.status == 400);
    }

    // ---- 声明 sha256 不符：文件保留但记录告警（不静默、也不删用户数据）----
    {
        const std::string prep =
            "{\"info\":" + peerInfo() +
            ",\"files\":{\"b\":{\"id\":\"b\",\"fileName\":\"bad.bin\",\"size\":3,"
            "\"fileType\":\"application\",\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\"}}}";
        const Resp pr = httpReq(port, "POST", "/api/localsend/v2/prepare-upload", prep);
        CHECK(pr.status == 200);
        const std::string s4 = jfield(pr.body, "sessionId");
        const Resp ur = httpReq(port, "POST", "/api/localsend/v2/upload?sessionId=" + s4 +
                                            "&fileId=b", "abc", "application/octet-stream");
        CHECK(ur.status == 204);
        CHECK(fs::exists(dir / "bad.bin"));
    }

    // ---- cancel 不崩、返回 2xx ----
    {
        const Resp r = httpReq(port, "POST", "/api/localsend/v2/cancel?sessionId=whatever");
        CHECK(r.status >= 200 && r.status < 300);
    }
}

int main() {
    ensureNet();
    const fs::path dir = fs::temp_directory_path() / "apxpc_localsend_upload_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    Options opt;
    opt.port = 53981;                     // 与默认 53317 错开，免得撞上真在跑的实例
    opt.dir = dir.string();
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
    const uint16_t port = rx.listenPort();

    run(dir, port);

    CHECK(fileCallbacks == 4);   // 正常 1 + 重名 2 + sha 不符 1

    // ---- stop() 必须收得回来 ----
    // 这个接收端起两个常驻线程 + 4 个 worker，全部靠 select + 200ms 超时看 running 退出。
    // 上一个同类缺陷（beacon 阻塞在 recvfrom）就是这样把停止路径卡死、CI 判超时的。
    const auto t0 = std::chrono::steady_clock::now();
    rx.stop();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count();
    CHECK(!rx.running());
    CHECK(ms < 2000);

    fs::remove_all(dir, ec);
    if (g_fail == 0) {
        std::printf("localsend_upload_test: ALL PASS（stop %lldms）\n",
                    static_cast<long long>(ms));
        return 0;
    }
    std::printf("localsend_upload_test: %d FAILED\n", g_fail);
    return 1;
}
