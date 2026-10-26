// LocalSend 接收端协议层单测（离线可跑，不占端口、不起线程）。
//
// 覆盖两块最容易悄悄错的东西：
//   1) SHA-256 —— 自实现，必须用标准向量钉住（含 100 万字节那条，专门走多块路径）
//   2) prepare-upload 的 JSON 契约与文件名清洗 —— 对手是官方 localsend，字段名/结构错了就是静默不通
#include "apxpc/localsend/local_send_receiver.hpp"
#include "apxpc/localsend/sha256.hpp"
#include "apxpc/net/json.hpp"

#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

using namespace apxpc::localsend;

static int g_fail = 0;

template <typename T>
static std::string toStr(const T& v) {
    if constexpr (std::is_same_v<std::decay_t<T>, std::string>) {
        return v;
    } else if constexpr (std::is_arithmetic_v<std::decay_t<T>>) {
        return std::to_string(v);
    } else {
        return std::string(v);
    }
}

#define CHECK(cond) do { \
    if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } \
} while (0)

#define CHECK_EQ(a, b) do { \
    const auto _a = (a); const auto _b = (b); \
    if (!(_a == _b)) { \
        std::printf("FAIL %s:%d  %s == %s\n", __FILE__, __LINE__, #a, #b); \
        std::printf("     实际 [%s]\n     期望 [%s]\n", toStr(_a).c_str(), toStr(_b).c_str()); \
        ++g_fail; \
    } \
} while (0)

// Json 取值一律带空指针兜底：真缺字段时给出 FAIL 行，而不是测试进程直接崩掉
static std::string jStr(const apxpc::net::Json& j, const char* key) {
    const apxpc::net::Json* p = j.find(key);
    return (p && p->isString()) ? p->asString() : std::string("<missing>");
}
static bool jBool(const apxpc::net::Json& j, const char* key) {
    const apxpc::net::Json* p = j.find(key);
    return p && p->isBool() && p->asBool();
}
static int jInt(const apxpc::net::Json& j, const char* key) {
    const apxpc::net::Json* p = j.find(key);
    return (p && p->isNumber()) ? p->asInt(-1) : -1;
}

static void testSha256Vectors() {
    // NIST / FIPS 180-4 标准向量
    CHECK_EQ(sha256Hex("", 0),
             "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK_EQ(sha256Hex("abc", 3),
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string s2 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK_EQ(sha256Hex(s2.data(), s2.size()),
             "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // 100 万个 'a'：跨多个 64 字节块 + 收尾填充，最容易暴露缓冲/长度处理错误
    const std::string million(1000000, 'a');
    CHECK_EQ(sha256Hex(million.data(), million.size()),
             "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    // 分批喂与一次喂必须一致（流式路径 vs 一次性路径）
    Sha256 s;
    s.update(million.data(), 600001);
    s.update(million.data() + 600001, 399999);
    CHECK_EQ(s.hex(),
             "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    // 取完摘要自动 reset，实例可继续复用
    Sha256 t;
    t.update("abc", 3);
    (void)t.hex();
    t.update("abc", 3);
    CHECK_EQ(t.hex(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

static void testSanitizeName() {
    // 路径穿越与分隔符：结果里不允许还剩分隔符，也不允许以 '.' 开头
    const std::string a = LocalSendReceiver::sanitizeName("../../etc/passwd");
    CHECK(a.find('/') == std::string::npos);
    CHECK(a.find('\\') == std::string::npos);
    CHECK(!a.empty() && a.front() != '.');
    CHECK_EQ(a, "_.._etc_passwd");

    CHECK_EQ(LocalSendReceiver::sanitizeName("a<b>c:d*e?f\"g|h"), "a_b_c_d_e_f_g_h");

    // 控制字符也替换
    CHECK_EQ(LocalSendReceiver::sanitizeName(std::string("x") + '\x01' + "y"), "x_y");

    // 首尾空白去掉
    CHECK_EQ(LocalSendReceiver::sanitizeName("  report.pdf  "), "report.pdf");

    // 空 / 全非法 → 兜底名
    CHECK_EQ(LocalSendReceiver::sanitizeName(""), "file.bin");
    CHECK_EQ(LocalSendReceiver::sanitizeName("..."), "file.bin");

    // 超长截断到 200
    CHECK_EQ(LocalSendReceiver::sanitizeName(std::string(500, 'z')).size(), size_t(200));
}

static void testGuessFileType() {
    CHECK_EQ(LocalSendReceiver::guessFileType("a.JPG"), "image");
    CHECK_EQ(LocalSendReceiver::guessFileType("b.mp4"), "video");
    CHECK_EQ(LocalSendReceiver::guessFileType("c.Mp3"), "audio");
    CHECK_EQ(LocalSendReceiver::guessFileType("d.md"), "text");
    CHECK_EQ(LocalSendReceiver::guessFileType("e.bin"), "application");
    CHECK_EQ(LocalSendReceiver::guessFileType("noext"), "application");
}

static void testJsonBuilders() {
    std::string err;
    const std::string text = LocalSendReceiver::announceJson(
        "PC-01", "PC-01", "desktop", "fp-1", kProtocolVersion, true, 53317);
    const apxpc::net::Json j = apxpc::net::Json::parse(text, &err);
    CHECK(err.empty());
    CHECK(j.isObject());
    CHECK_EQ(jStr(j, "alias"), "PC-01");
    CHECK_EQ(jStr(j, "version"), "2.2");
    CHECK_EQ(jStr(j, "deviceType"), "desktop");
    CHECK_EQ(jStr(j, "fingerprint"), "fp-1");
    CHECK(jBool(j, "download"));
    CHECK_EQ(jInt(j, "port"), 53317);
    CHECK_EQ(jStr(j, "protocol"), "http");
    CHECK(jBool(j, "announce"));

    const std::string info = LocalSendReceiver::deviceInfoJson(
        "PC-01", "PC-01", "desktop", "fp-1", kProtocolVersion, true);
    const apxpc::net::Json ji = apxpc::net::Json::parse(info, &err);
    CHECK(ji.isObject());
    CHECK_EQ(jStr(ji, "alias"), "PC-01");
    CHECK(jBool(ji, "download"));
    // port / announce 只属于多播报文，info 里不该出现
    CHECK(ji.find("port") == nullptr);
    CHECK(ji.find("announce") == nullptr);
}

static void testParsePrepareRequest() {
    // 形状与 Android 端 LocalSendClient.send 发出的报文一致
    const std::string body =
        R"({"info":{"alias":"Pixel 9","version":"2.2","deviceModel":"Pixel 9",)"
        R"("deviceType":"phone","fingerprint":"fp-phone","download":true},)"
        R"("files":{"id-1":{"fileName":"假日 照片.jpg","size":1234,)"
        R"("sha256":"ab12","fileType":"image"},)"
        R"("id-2":{"fileName":"log.txt","size":9,"sha256":"cd34","fileType":"text"}}})";

    std::string err;
    std::string sender;
    const std::vector<PreparedFile> files = LocalSendReceiver::parsePrepareRequest(body, &err, &sender);
    CHECK(err.empty());
    CHECK_EQ(sender, "Pixel 9");
    CHECK_EQ(files.size(), size_t(2));
    if (files.size() == 2) {
        CHECK_EQ(files[0].id, "id-1");
        CHECK_EQ(files[0].fileName, "假日 照片.jpg");   // UTF-8 原样透传
        CHECK_EQ(files[0].size, uint64_t(1234));
        CHECK_EQ(files[0].sha256, "ab12");
        CHECK_EQ(files[1].id, "id-2");
        CHECK_EQ(files[1].size, uint64_t(9));
    }

    // 缺 files → 报错，而不是「当成 0 个文件」的成功
    std::string err2;
    const std::vector<PreparedFile> none =
        LocalSendReceiver::parsePrepareRequest(R"({"info":{"alias":"x"}})", &err2);
    CHECK(!err2.empty());
    CHECK(none.empty());

    // 非 JSON → 报错
    std::string err3;
    const std::vector<PreparedFile> bad = LocalSendReceiver::parsePrepareRequest("not json at all", &err3);
    CHECK(!err3.empty());
    CHECK(bad.empty());

    // size 缺失时按 0 记，不拒绝整份清单（对端可能没填）
    std::string err4;
    const std::vector<PreparedFile> partial = LocalSendReceiver::parsePrepareRequest(
        R"({"files":{"k":{"fileName":"n.bin"}}})", &err4);
    CHECK(err4.empty());
    CHECK_EQ(partial.size(), size_t(1));
    if (!partial.empty()) {
        CHECK_EQ(partial[0].size, uint64_t(0));
        CHECK_EQ(partial[0].fileName, "n.bin");
        CHECK_EQ(partial[0].sha256, "");
    }
}

static void testBuildPrepareResponse() {
    std::vector<PreparedFile> files(2);
    files[0].id = "a1";
    files[1].id = "b2";
    std::string err;
    const apxpc::net::Json j = apxpc::net::Json::parse(
        LocalSendReceiver::buildPrepareResponse("sess-9", files), &err);
    CHECK(err.empty());
    CHECK_EQ(jStr(j, "sessionId"), "sess-9");
    const apxpc::net::Json* f = j.find("files");
    CHECK(f != nullptr && f->isObject());
    if (f && f->isObject()) {
        CHECK_EQ(f->asObject().size(), size_t(2));
        // token 回落到发送方的 fileId 上（upload 的 fileId 参数就用它）
        CHECK_EQ(jStr(*f, "a1"), "a1");
        CHECK_EQ(jStr(*f, "b2"), "b2");
    }
}

int main() {
    testSha256Vectors();
    testSanitizeName();
    testGuessFileType();
    testJsonBuilders();
    testParsePrepareRequest();
    testBuildPrepareResponse();
    if (g_fail == 0) {
        std::printf("localsend_protocol_test: ALL PASS\n");
        return 0;
    }
    std::printf("localsend_protocol_test: %d FAILED\n", g_fail);
    return 1;
}
