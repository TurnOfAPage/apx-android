#pragma once
// LocalSend 互传 · PC 侧接收端（协议 v2.2，与官方 localsend 及本工程 Android 端互通）。
//
// 为什么要有它：Android 端 `com.allperiph.localsend` 早就有完整的接收/发送实现
// （多播发现 + info/register/prepare-upload/upload/cancel），但 `pc/host` 里一个字节都没有 ——
// 结果是「手机 ↔ 官方桌面版」能通，「手机 ↔ 本工程 PC 端」不通。这个类补的就是 PC 这一半。
//
// 行为：
//   · UDP：加入多播组 224.0.0.167:53317，收 announce 记设备；周期性自广播（带 announce 标志），
//     让手机端设备列表能看见本机；收到对方 announce 后主动 POST register 应答。
//   · HTTP：53317（被占用时依次回退 53318/53319，实际端口由 announce 带出去），
//     实现 /api/localsend/v2/{info,register,prepare-upload,upload,cancel}。
//   · 落盘：默认「下载/AllPeriph」，重名不覆盖；声明了 sha256 就边写边校验并如实记录结果。
//
// 退出的正确性（吃过教训）：所有 socket 读写都走 select + 短超时并检查 running 标志，
// 不依赖「关掉 fd 打断阻塞的系统调用」。上一个同类缺陷正是 beacon 线程阻塞在 recvfrom 上
// 收不回，导致停止时卡死、CI 判 300s 超时（见 v219 的 SO_RCVTIMEO 修复）。
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace apxpc::localsend {

/// 协议固定的多播组与默认端口（端口同时是 HTTP 端口）
constexpr const char* kMulticastGroup = "224.0.0.167";
constexpr uint16_t kDefaultPort = 53317;
constexpr const char* kProtocolVersion = "2.2";

/// 路径常量（与 Android 端 LocalSendProtocol 逐字符一致）
constexpr const char* kPathInfo = "/api/localsend/v2/info";
constexpr const char* kPathRegister = "/api/localsend/v2/register";
constexpr const char* kPathPrepare = "/api/localsend/v2/prepare-upload";
constexpr const char* kPathUpload = "/api/localsend/v2/upload";
constexpr const char* kPathCancel = "/api/localsend/v2/cancel";

struct Device {
    std::string alias;
    std::string deviceType;   // phone / tablet / desktop / ...
    std::string ip;
    std::string fingerprint;
    uint16_t port = 0;
    int64_t lastSeenMs = 0;
};

struct Options {
    uint16_t port = kDefaultPort;
    std::string dir;                      // 空 = 默认（下载/AllPeriph）
    std::string alias;                    // 空 = 主机名
    std::string deviceModel;              // 空 = 同 alias
    std::string deviceType = "desktop";
    std::string multicastGroup = kMulticastGroup;
    uint32_t announcePeriodMs = 10'000;   // 自广播周期
};

/// prepare-upload 声明的一个文件（id 兼作 upload 的 token，与 Android 端实现一致）
struct PreparedFile {
    std::string id;
    std::string fileName;
    std::string sha256;   // 可能为空（对方没声明）
    uint64_t size = 0;
};

/// 面板状态快照（纯数据：两个面板与单测共用同一份字段口径，不碰 socket）
struct StatusSnapshot {
    bool running = false;
    uint16_t port = 0;            ///< 实际 HTTP 端口（回退过就是回退后的值）
    std::string dir;              ///< 落盘目录
    std::string error;            ///< 起不来的原因（running=false 时才有意义）
    std::string warn;             ///< 起来了但能力受限（如多播端口被占 → 发现能力下降）
    int received = 0;             ///< 本次运行累计收到的文件数
    std::vector<Device> devices;  ///< 60s 内可见的设备，最近可见优先
};

/// 快照 → 面板 state JSON 里的 localsend 段。
/// 字段名是**前端契约**（pc/web/src/api/bridge.ts 的 HostState.localsend），改名即破坏前端。
std::string statusJson(const StatusSnapshot& s);

class LocalSendReceiver {
public:
    /// 每个文件落盘完成时回调（在接收线程调用）。
    /// from = 发送方 alias（对端没报 alias 时是「未知设备」）—— 面板要拿它做提示文案，
    /// 所以由接收端一并给出，而不是让面板再回头去猜是哪个会话。
    using FileCallback = std::function<void(const std::string& path, uint64_t size,
                                           const std::string& from)>;
    /// 首次发现某台设备时回调（在 UDP 线程调用）
    using DeviceCallback = std::function<void(const Device& dev)>;

    LocalSendReceiver();
    ~LocalSendReceiver();
    LocalSendReceiver(const LocalSendReceiver&) = delete;
    LocalSendReceiver& operator=(const LocalSendReceiver&) = delete;

    /// 启动。端口全部失败 / socket 创建失败时返回 false，原因见 lastError()。
    bool start(const Options& opt, FileCallback onFile, DeviceCallback onDevice = {});
    void stop();
    bool running() const;

    uint16_t listenPort() const;         // 实际 HTTP 端口（可能回退过）
    std::string fingerprint() const;
    std::string lastError() const;
    std::string lastFilePath() const;
    int receivedCount() const;
    /// 60s 内见过的设备，最近可见优先
    std::vector<Device> devices() const;
    /// 面板用状态快照（未启动时也给得出诚实值：running=false + 默认端口 + 落盘目录）
    StatusSnapshot snapshot() const;
    /// 默认落盘目录：用户「下载」目录下的 AllPeriph（不存在会被创建）
    static std::string defaultDir();

    // ---------------- 纯函数：不碰 socket，单测直接调 ----------------
    /// 文件名清洗（与 Android 端同规则）：路径分隔与非法字符替换、去首尾空白与开头点、限长
    static std::string sanitizeName(const std::string& raw);
    /// 扩展名 → LocalSend 的 fileType（image / video / audio / text / application）
    static std::string guessFileType(const std::string& name);
    /// info / register 的响应体
    static std::string deviceInfoJson(const std::string& alias, const std::string& deviceModel,
                                      const std::string& deviceType, const std::string& fingerprint,
                                      const std::string& version, bool download);
    /// 多播 announce 报文（设备信息 + port + protocol + announce 标志）
    static std::string announceJson(const std::string& alias, const std::string& deviceModel,
                                    const std::string& deviceType, const std::string& fingerprint,
                                    const std::string& version, bool download, uint16_t port);
    /// 解析 prepare-upload 请求体；err 非空表示结构不合法
    static std::vector<PreparedFile> parsePrepareRequest(std::string_view body, std::string* err,
                                                         std::string* senderAlias = nullptr);
    /// prepare-upload 响应体 {sessionId, files:{id:token}}
    static std::string buildPrepareResponse(const std::string& sessionId,
                                           const std::vector<PreparedFile>& files);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace apxpc::localsend
