#pragma once
// LocalSend 互传 · PC 侧发送端（协议 v2.2，与官方 localsend 及本工程 Android 端互通）。
//
// 为什么要有它：接收端（local_send_receiver.hpp）补上了「手机 → PC」，但反方向仍然只能
// 靠旧的 9512 私有通道 —— 那条通道要求控制链路先连上、且对方必须是本 App。官方
// LocalSend / 别的机器发来的文件，PC 这边都**发不出去**。这个类补的就是这一半。
//
// 与 Android 端 `com.allperiph.localsend.LocalSendClient` 逐条对齐（同协议、同三步）：
//   ① POST register       —— 先确认对端可达，顺带拿到它报的 alias（失败即「连接对端失败」）
//   ② POST prepare-upload —— 每个文件带 sha256（边读边算）+ size + fileName + fileType
//   ③ POST upload         —— 逐文件按 ?sessionId=&fileId= 流式推（对端回 2xx 即成功）
// 协议细节取自官方 localsend dto_v2.rs：upload 同时收 POST 和 PUT，本实现用 POST
// （与 Android 端一致），省掉「PUT 不支持时如何重试」这条分支。
//
// 设计边界：
//   · 纯阻塞、无后台线程 —— 发送是「用户点了就发完」的一次性动作，线程由调用方决定
//     （面板用独立 send 线程，命令行直接跑）。这与接收端的常驻线程模型不同，是故意的。
//   · 进度通过回调上报（已发字节 / 总字节 + 当前文件名），面板据此画进度、命令行据此打点。
//   · 取消：Progress 回调返回 false 即中止，并且**尽力**向对端发 cancel，
//     免得对端把半截文件当成完整文件留着。
//   · 错误一律如实写进 Result::error（含 HTTP 状态码），不抛异常。
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace apxpc::localsend {

/// 本机设备信息（register / prepare 的 info 字段；alias 为空时用主机名）
struct SenderIdentity {
    std::string alias;                  ///< 空 = 主机名
    std::string deviceType = "desktop"; ///< phone / tablet / desktop
    std::string deviceModel;            ///< 空 = 同 alias
    std::string fingerprint;            ///< 空 = 本次进程内随机生成（http 模式下官方也是随机串）
};

/// 要发的一个文件：本地路径由发送端自己打开与统计大小
struct OutgoingFile {
    std::string path;   ///< 本地路径（UTF-8）
    std::string name;   ///< 对端看到的文件名；空 = 取 path 的文件名
};

/// 一台已发现的设备（由接收端的 devices() 给出，或调用方按 ip:port 直接给）
struct Target {
    std::string ip;
    uint16_t port = 0;
    std::string alias;   ///< 仅用于界面提示（真正的 alias 以对端 register 应答为准）
};

struct SendProgress {
    std::string fileName;      ///< 当前正在传的文件
    uint64_t fileBytes = 0;    ///< 当前文件已发字节
    uint64_t fileTotal = 0;    ///< 当前文件总字节
    uint64_t totalBytes = 0;   ///< 本次任务已发字节（含已完成的文件）
    uint64_t grandTotal = 0;   ///< 本次任务总字节
    int filesDone = 0;
    int filesTotal = 0;
};

struct SendResult {
    bool ok = false;
    std::string error;                        ///< 失败原因（含 HTTP 状态码）；ok=true 时为空
    std::string peerAlias;                    ///< 对端在 register 应答里报的 alias
    std::vector<std::string> deliveredNames;  ///< 已确认送达的文件名
    uint64_t bytesSent = 0;
};

class LocalSendSender {
public:
    /// 进度回调（在调用 send() 的线程里同步调用）。
    /// 返回 false = 用户取消：发送端会尽力通知对端 cancel，然后以 error="已取消" 返回。
    using ProgressCallback = std::function<bool(const SendProgress&)>;

    LocalSendSender();
    ~LocalSendSender();
    LocalSendSender(const LocalSendSender&) = delete;
    LocalSendSender& operator=(const LocalSendSender&) = delete;

    /// 同步发送（阻塞到全部完成 / 失败 / 取消）。放不放到后台线程由调用方决定。
    /// progress 可为空（不关心进度，例如命令行只想打一行结果）。
    SendResult send(const Target& target, const SenderIdentity& self,
                    const std::vector<OutgoingFile>& files,
                    const ProgressCallback& progress = nullptr);

    // ---------------- 纯函数：不碰 socket，单测直接调 ----------------
    /// 扩展名 → LocalSend 的 fileType（image / video / audio / text / application）。
    /// 与接收端的 guessFileType 同表，也与 Android 端 LocalSendClient.guessType 一致。
    static std::string guessFileType(const std::string& name);
    /// prepare-upload 请求体：{"info":{…},"files":{"<id>":{"fileName","size","sha256","fileType"}}}
    /// ids / files / sha256s / sizes 必须等长且一一对应
    static std::string buildPrepareBody(const SenderIdentity& self, const std::string& fingerprint,
                                        const std::vector<std::string>& ids,
                                        const std::vector<OutgoingFile>& files,
                                        const std::vector<std::string>& sha256s,
                                        const std::vector<uint64_t>& sizes);
    /// 解析 prepare-upload 应答（sessionId + id→token 映射）。解析不出时两者留空。
    static void parsePrepareResponse(const std::string& body,
                                     std::string* sessionId,
                                     std::vector<std::pair<std::string, std::string>>* tokens);
    /// 解析 register 应答，取对端 alias（没有则返回空串）
    static std::string parseRegisterAlias(const std::string& body);
    /// 生成会话 / 文件 id（UUID 形式，与官方一致）
    static std::string randomId();
};

}  // namespace apxpc::localsend
