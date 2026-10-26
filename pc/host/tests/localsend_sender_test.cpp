// LocalSend 发送端测试：
//   ① 纯函数（fileType 表、prepare 报文契约、应答解析）—— 不碰 socket，钉住与接收端/官方一致的字段
//   ② 真开端口跑一遍：起一个**只实现五个端点**的假对端（官方协议的最小子集），
//      用 LocalSendSender 走完 register → prepare-upload → upload，
//      并验证「取消」真能中断、且对端收到 cancel。
//
// 为什么不驱真接收端：接收端在 local_send_upload_test 里已经被真端口验证过；
// 这里要验的是**发送端**的行为，用假对端才能精确断言它发了什么（Body 字段、query 参数、
// Content-Length、是否发了 cancel）。两边各验各自那一半。
#include "apxpc/localsend/local_send_sender.hpp"
#include "apxpc/localsend/sha256.hpp"
#include "apxpc/net/json.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
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

namespace fs = std::filesystem;
using apxpc::localsend::LocalSendSender;
using apxpc::localsend::OutgoingFile;
using apxpc::localsend::SenderIdentity;
using apxpc::localsend::Target;

static int g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
            ++g_fail;                                                        \
        }                                                                    \
    } while (0)

// ---------------------------------------------------------------- 假对端
// 只实现发送端会用的五个端点；记录收到的请求要点供断言。
struct FakePeer {
    int port = 0;
    std::atomic<bool> run{false};
    std::thread th;
#if defined(_WIN32)
    SOCKET ls = INVALID_SOCKET;
#else
    int ls = -1;
#endif

    std::mutex mu;
    std::string regBody;
    std::string prepBody;
    std::string uploadQuery;
    uint64_t uploadBytes = 0;
    std::string uploadSha;      // 上传内容实际算出来的摘要
    int uploads = 0;
    bool gotCancel = false;
    // 让第 N 次 upload 直接断连（模拟对端中途失败）
    int dropUploadAt = 0;

    bool start() {
#if defined(_WIN32)
        WSADATA w{};
        ::WSAStartup(MAKEWORD(2, 2), &w);
#endif
        ls = ::socket(AF_INET, SOCK_STREAM, 0);
        if (ls < 0) return false;
        int one = 1;
        ::setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sa.sin_port = 0;   // 让内核分配，避免测试之间抢端口
        if (::bind(ls, reinterpret_cast<sockaddr*>(&sa), sizeof sa) != 0) return false;
        if (::listen(ls, 8) != 0) return false;
        socklen_t len = sizeof sa;
        ::getsockname(ls, reinterpret_cast<sockaddr*>(&sa), &len);
        port = ntohs(sa.sin_port);
        run.store(true);
        th = std::thread([this] { loop(); });
        return true;
    }

    void stop() {
        run.store(false);
        if (th.joinable()) th.join();
        if (ls >= 0) {
#if defined(_WIN32)
            ::closesocket(ls);
#else
            ::close(ls);
#endif
        }
        ls = -1;
    }

    void loop() {
        while (run.load()) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(ls, &rfds);
            timeval tv{0, 200 * 1000};
            const int r = ::select(static_cast<int>(ls) + 1, &rfds, nullptr, nullptr, &tv);
            if (r <= 0) continue;
            sockaddr_in peer{};
            socklen_t pl = sizeof peer;
            const int c = static_cast<int>(::accept(ls, reinterpret_cast<sockaddr*>(&peer), &pl));
            if (c < 0) continue;
            handle(c);
#if defined(_WIN32)
            ::closesocket(c);
#else
            ::close(c);
#endif
        }
    }

    static bool readLine(int c, std::string& out) {
        out.clear();
        char ch = 0;
        while (true) {
            const int n = static_cast<int>(::recv(c, &ch, 1, 0));
            if (n <= 0) return false;
            if (ch == '\n') {
                if (!out.empty() && out.back() == '\r') out.pop_back();
                return true;
            }
            out.push_back(ch);
            if (out.size() > 16 * 1024) return false;
        }
    }

    void respond(int c, int code, const std::string& body) {
        const std::string s = "HTTP/1.1 " + std::to_string(code) + " X\r\nContent-Length: " +
                              std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        ::send(c, s.data(), static_cast<int>(s.size()), 0);
    }

    void handle(int c) {
        std::string line;
        if (!readLine(c, line)) return;
        std::string method, path;
        {
            const size_t a = line.find(' ');
            const size_t b = line.find(' ', a + 1);
            if (a == std::string::npos || b == std::string::npos) return;
            method = line.substr(0, a);
            path = line.substr(a + 1, b - a - 1);
        }
        uint64_t contentLen = 0;
        while (readLine(c, line) && !line.empty()) {
            const std::string low = [&] {
                std::string t = line;
                for (char& ch : t) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                return t;
            }();
            if (low.rfind("content-length:", 0) == 0) {
                contentLen = std::strtoull(line.c_str() + 15, nullptr, 10);
            }
        }

        // path 形如 /api/localsend/v2/<端点>[?query]；只取端点名做分发
        const std::string ep = path.substr(path.find_last_of('/') + 1);

        if (ep.rfind("register", 0) == 0) {
            std::string body(static_cast<size_t>(contentLen), '\0');
            size_t got = 0;
            while (got < body.size()) {
                const int n = static_cast<int>(::recv(c, body.data() + got,
                                                     static_cast<int>(body.size() - got), 0));
                if (n <= 0) break;
                got += static_cast<size_t>(n);
            }
            {
                std::lock_guard<std::mutex> lk(mu);
                regBody = body;
            }
            respond(c, 200, R"({"alias":"FakePeer","version":"2.2","deviceType":"desktop","fingerprint":"fp-peer","download":true})");
            return;
        }
        if (ep.rfind("prepare-upload", 0) == 0) {
            std::string body(static_cast<size_t>(contentLen), '\0');
            size_t got = 0;
            while (got < body.size()) {
                const int n = static_cast<int>(::recv(c, body.data() + got,
                                                     static_cast<int>(body.size() - got), 0));
                if (n <= 0) break;
                got += static_cast<size_t>(n);
            }
            std::lock_guard<std::mutex> lk(mu);
            prepBody = body;
            // 回 sessionId + files{id:id}（token 直接等于 id，与两端实现一致）。
            // 直接解析收到的 JSON，避免用字符串搜索去猜 id 边界。
            std::string files = "{";
            const apxpc::net::Json root = apxpc::net::Json::parse(body, nullptr);
            if (const apxpc::net::Json* fs = root.find("files"); fs && fs->isObject()) {
                bool first = true;
                for (const auto& kv : fs->asObject()) {
                    if (!first) files += ",";
                    files += "\"" + kv.first + "\":\"" + kv.first + "\"";
                    first = false;
                }
            }
            files += "}";
            respond(c, 200, "{\"sessionId\":\"sess-test\",\"files\":" + files + "}");
            return;
        }
        if (ep.rfind("upload", 0) == 0) {
            std::lock_guard<std::mutex> lk(mu);
            ++uploads;
            uploadQuery = path;
            if (dropUploadAt > 0 && uploads >= dropUploadAt && contentLen > 8) {
                // 只读一半就关连接：模拟对端中途挂掉
                char buf[4096];
                ::recv(c, buf, static_cast<int>(sizeof buf), 0);
                return;
            }
            std::string all;
            all.reserve(static_cast<size_t>(contentLen));
            char buf[64 * 1024];
            uint64_t got = 0;
            while (got < contentLen) {
                const int n = static_cast<int>(::recv(c, buf, static_cast<int>(sizeof buf), 0));
                if (n <= 0) break;
                got += static_cast<size_t>(n);
                all.append(buf, static_cast<size_t>(n));
            }
            uploadBytes = got;
            apxpc::localsend::Sha256 sha;
            sha.update(all.data(), all.size());
            uploadSha = sha.hex();
            respond(c, 204, "");
            return;
        }
        if (ep.rfind("cancel", 0) == 0) {
            std::lock_guard<std::mutex> lk(mu);
            gotCancel = true;
            respond(c, 200, "{}");
            return;
        }
        respond(c, 404, "");
    }
};

static std::string sha256OfFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    apxpc::localsend::Sha256 sha;
    sha.update(all.data(), all.size());
    return sha.hex();
}

// ---------------------------------------------------------------- ① 纯函数
static void testGuessFileType() {
    std::printf("[step] fileType 表\n");
    const std::pair<const char*, const char*> cases[] = {
        {"a.JPG", "image"}, {"b.png", "image"}, {"c.mp4", "video"}, {"d.mp3", "audio"},
        {"e.PDF", "text"},  {"f.txt", "text"}, {"g.bin", "application"}, {"noext", "application"},
    };
    for (const auto& [name, want] : cases) {
        const std::string got = LocalSendSender::guessFileType(name);
        if (got != want) std::printf("  %s → %s（期望 %s）\n", name, got.c_str(), want);
        CHECK(got == want);
    }
}

static void testBuildPrepareBody() {
    std::printf("[step] prepare 报文契约\n");
    SenderIdentity self;
    self.alias = "PC-Test";
    self.deviceModel = "Model-X";
    const std::vector<OutgoingFile> files = {{"/tmp/x/a.png", ""}, {"/tmp/x/b.bin", "改过的名.bin"}};
    const std::vector<std::string> ids = {"id-1", "id-2"};
    const std::vector<std::string> shas = {std::string(64, 'a'), std::string(64, 'b')};
    const std::vector<uint64_t> sizes = {123, 4567890123ULL};
    const std::string body = LocalSendSender::buildPrepareBody(self, "fp-1", ids, files, shas, sizes);

    // 字段名是官方 dto_v2.rs 的契约：fileName / size / sha256 / fileType，外面套 info / files
    CHECK(body.find("\"alias\":\"PC-Test\"") != std::string::npos);
    CHECK(body.find("\"deviceModel\":\"Model-X\"") != std::string::npos);
    CHECK(body.find("\"deviceType\":\"desktop\"") != std::string::npos);
    CHECK(body.find("\"fingerprint\":\"fp-1\"") != std::string::npos);
    CHECK(body.find("\"id-1\"") != std::string::npos);
    CHECK(body.find("\"fileName\":\"a.png\"") != std::string::npos);      // 空 name → 取路径名
    CHECK(body.find("\"fileName\":\"改过的名.bin\"") != std::string::npos); // UTF-8 原样
    CHECK(body.find("\"fileType\":\"image\"") != std::string::npos);
    CHECK(body.find("\"fileType\":\"application\"") != std::string::npos);
    CHECK(body.find("\"size\":4567890123") != std::string::npos);         // double 承载无精度损失
    CHECK(body.find(std::string("\"sha256\":\"") + std::string(64, 'a') + "\"") != std::string::npos);
}

static void testParseResponses() {
    std::printf("[step] 应答解析\n");
    std::string sid;
    std::vector<std::pair<std::string, std::string>> toks;
    LocalSendSender::parsePrepareResponse(
        R"({"sessionId":"S1","files":{"f1":"t1","f2":"t2"}})", &sid, &toks);
    CHECK(sid == "S1");
    CHECK(toks.size() == 2);

    // 不合法 / 缺字段时不崩、给出空结果（调用方据此报「对端未返回 sessionId」）
    LocalSendSender::parsePrepareResponse("not json", &sid, &toks);
    CHECK(sid.empty());
    CHECK(toks.empty());
    LocalSendSender::parsePrepareResponse(R"({"files":{}})", &sid, &toks);
    CHECK(sid.empty());

    CHECK(LocalSendSender::parseRegisterAlias(R"({"alias":"手机","deviceType":"phone"})") == "手机");
    CHECK(LocalSendSender::parseRegisterAlias("[]").empty());
    CHECK(LocalSendSender::randomId().size() == 36);   // UUID 形式
}

// ---------------------------------------------------------------- ② 真端口
static void testRealSend() {
    std::printf("[step] 真端口：register → prepare → upload\n");
    FakePeer peer;
    CHECK(peer.start());

    const fs::path dir = fs::temp_directory_path() / "apxpc_localsend_send_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    // 小文件 + 4MB 文件：覆盖「一次 write 就完」与「多块流式」两条路径
    fs::path small = dir / "小.txt";
    fs::path big = dir / "big.bin";
    {
        std::ofstream o(small, std::ios::binary);
        o << "hello localsend\n";
    }
    {
        std::ofstream o(big, std::ios::binary | std::ios::trunc);
        std::vector<char> chunk(1024);
        for (size_t i = 0; i < chunk.size(); ++i) chunk[i] = static_cast<char>(i * 7 + 3);
        for (int i = 0; i < 4096; ++i) o.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    }

    Target t;
    t.ip = "127.0.0.1";
    t.port = static_cast<uint16_t>(peer.port);
    SenderIdentity self;
    self.alias = "PC-Test";
    self.fingerprint = "fp-test";

    std::vector<OutgoingFile> files = {{small.string(), ""}, {big.string(), ""}};
    uint64_t lastTotal = 0;
    int progressCalls = 0;
    const auto res = LocalSendSender().send(t, self, files,
        [&](const apxpc::localsend::SendProgress& pr) {
            ++progressCalls;
            lastTotal = pr.totalBytes;
            return true;
        });

    if (!res.ok) std::printf("  send 失败：%s\n", res.error.c_str());
    CHECK(res.ok);
    CHECK(res.error.empty());
    CHECK(res.peerAlias == "FakePeer");
    CHECK(res.deliveredNames.size() == 2);
    CHECK(peer.uploads == 2);
    CHECK(peer.uploadQuery.find("sessionId=sess-test") != std::string::npos);
    CHECK(peer.uploadQuery.find("fileId=") != std::string::npos);
    CHECK(peer.uploadBytes == fs::file_size(big));           // 最后一次 upload 是 big.bin
    CHECK(peer.uploadSha == sha256OfFile(big));              // 内容完整、字节不差
    CHECK(progressCalls > 0);
    CHECK(lastTotal >= fs::file_size(big));
    CHECK(res.bytesSent == fs::file_size(small) + fs::file_size(big));
    // register / prepare 都带了合法 JSON，且 prepare 里两个文件都在
    CHECK(peer.regBody.find("PC-Test") != std::string::npos);
    CHECK(peer.prepBody.find("big.bin") != std::string::npos);
    CHECK(peer.prepBody.find("小.txt") != std::string::npos);

    // 取消：第一次进度回调就喊停 → 返回「已取消」、不崩、且对端收到 cancel
    std::printf("[step] 取消\n");
    FakePeer p2;
    CHECK(p2.start());
    Target t2;
    t2.ip = "127.0.0.1";
    t2.port = static_cast<uint16_t>(p2.port);
    const auto r2 = LocalSendSender().send(t2, self, {{big.string(), ""}},
        [](const apxpc::localsend::SendProgress&) { return false; });
    CHECK(!r2.ok);
    CHECK(r2.error == "已取消");
    CHECK(p2.uploads == 1);
    CHECK(p2.gotCancel);

    // 目标不可达：如实报错，不抛、不崩
    std::printf("[step] 目标不可达\n");
    Target dead;
    dead.ip = "127.0.0.1";
    dead.port = 1;   // 基本不可能有人监听
    const auto r3 = LocalSendSender().send(dead, self, {{small.string(), ""}});
    CHECK(!r3.ok);
    CHECK(!r3.error.empty());

    // 文件不存在：在动手连之前就该报读取失败
    std::printf("[step] 文件不存在\n");
    const auto r4 = LocalSendSender().send(t2, self, {{(dir / "不存在.bin").string(), ""}});
    if (r4.ok) std::printf("  不存在文件竟然成功\n");
    CHECK(!r4.ok);
    CHECK(r4.error.find("读取失败") != std::string::npos);

    peer.stop();
    p2.stop();
    fs::remove_all(dir, ec);
}

int main() {
    std::printf("== localsend_sender_test ==\n");
    testGuessFileType();
    testBuildPrepareBody();
    testParseResponses();
    testRealSend();
    if (g_fail == 0) {
        std::printf("localsend_sender_test: ALL PASS\n");
        return 0;
    }
    std::printf("localsend_sender_test: %d FAILURE(S)\n", g_fail);
    return 1;
}
