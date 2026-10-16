// 传输抽象（PROTOCOL §3 / §4）
//
// 通道模型：
//   - 视频（streamId=0）走 **可丢包** 通道：USB bulk 本身不丢包，所谓「可丢」体现在
//     写入前的策略层（帧过旧/队列过深就丢弃，见 pipeline），并在帧头打 kFlagDropable。
//   - 控制面（streamId=3）走 **可靠有序** 通道：控制帧绝不丢弃，且视频与控制共用同一条
//     bulk OUT 时，控制帧优先插队（见 UsbTransportWin::write 的实现）。
//   - 触控上行（streamId=2）走 bulk IN，读取线程优先处理。
//
// AOA / NCM：UsbFilter.kind 决定走 AOA bulk 还是 NCM（TCP over USB-ECM）。
// AOA/WinUSB bulk 已实现；NCM 接口已定义（Issue #14），实现见 ncm_transport.cpp。
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace apxdisp {

// Google AOA（Android Open Accessory）VID/PID
constexpr uint16_t kAoaVid       = 0x18D1;
constexpr uint16_t kAoaPidAcc    = 0x2D00;  // accessory
constexpr uint16_t kAoaPidAccAdb = 0x2D01;  // accessory + adb

// NCM（Network Control Model）CDC 子类
constexpr uint16_t kNcmVid       = 0x18D1;  // Google
constexpr uint16_t kNcmPid       = 0x4EE1;  // NCM function
constexpr uint8_t  kNcmSubClass  = 0x0D;    // CDC NCM
constexpr uint8_t  kNcmProtocol  = 0x00;

enum class UsbKind {
    Auto    = 0,
    AoaBulk = 1,  // AOA bulk
    Ncm     = 2,  // NCM / RNDIS
};

struct UsbFilter {
    uint16_t vid        = kAoaVid;
    uint16_t pid        = kAoaPidAccAdb;
    std::string serial;      // 可选
    std::string devicePath;  // 可选，直接指定（Windows 设备接口路径）
    uint8_t  bulkOutEp  = 0x01;
    uint8_t  bulkInEp   = 0x81;
    UsbKind  kind       = UsbKind::Auto;
};

enum class TransportKind {
    Auto = 0, WinUsb = 1, LibUsb = 2, Loopback = 3, Tcp = 4, Ncm = 5
};

// ——— NCM 网络配置（必须定义在 TransportSpec 之前） ———
struct NcmConfig {
    std::string interfaceName;   // 可选：指定网络接口（如 "USB Ethernet"）
    std::string localIp;         // 本端 IP（空=DHCP 自动）
    std::string peerIp;          // 对端 IP（空=不校验）
    uint16_t    port = 9512;     // TCP 端口（默认 9512，区别于 AOA 的 9511）
    bool        isServer = true; // true=本端监听，false=连接对端
    std::string token;           // 配对令牌
};

// 统一传输描述：USB 用 usb，TCP 用 host/port，NCM 用 netConfig。互斥。
struct TransportSpec {
    TransportKind kind = TransportKind::Auto;
    UsbFilter     usb{};        // kind 为 USB 系列时有效
    std::string   host;         // Tcp：非空=客户端连接该主机；空=本端作服务端监听
    uint16_t      port = 0;     // Tcp：端口
    std::string   token;        // 无线模式令牌（配对阶段校验；为空表示不校验）
    NcmConfig     netConfig{};  // kind 为 Ncm 时有效

    static TransportSpec usbSpec(const UsbFilter& f) {
        TransportSpec s; s.kind = TransportKind::Auto; s.usb = f; return s;
    }
    static TransportSpec tcpClient(const std::string& h, uint16_t p, std::string tok = {}) {
        TransportSpec s; s.kind = TransportKind::Tcp; s.host = h; s.port = p; s.token = std::move(tok); return s;
    }
    static TransportSpec tcpServer(uint16_t p, std::string tok = {}) {
        TransportSpec s; s.kind = TransportKind::Tcp; s.port = p; s.token = std::move(tok); return s;
    }
    static TransportSpec ncmSpec(const NcmConfig& cfg) {
        TransportSpec s; s.kind = TransportKind::Ncm; s.netConfig = cfg; return s;
    }
};

class IChannel {
public:
    virtual ~IChannel() = default;
    virtual size_t write(const uint8_t* data, size_t len, uint32_t timeoutMs) = 0;
    virtual size_t read(uint8_t* buf, size_t cap, uint32_t timeoutMs) = 0;
    virtual bool reliable() const = 0;
    virtual void cancel() = 0;
    virtual std::string lastError() const = 0;
};

class ITransport {
public:
    virtual ~ITransport() = default;
    virtual bool open(const TransportSpec& spec) = 0;
    bool open(const UsbFilter& filter) { return open(TransportSpec::usbSpec(filter)); }
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual IChannel* videoChannel()   = 0;
    virtual IChannel* controlChannel() = 0;
    virtual IChannel* touchChannel()   = 0;
    virtual bool reset() = 0;
    virtual std::string lastError() const = 0;
    virtual TransportKind kind() const = 0;
};

// NCM 传输接口（Issue #14）
//
// NCM 是 USB CDC 网络类标准协议，操作系统原生支持（免驱动）。
// TCP over USB-ECM 比 raw bulk 更适合控制面（可靠有序）。
//
// 使用方式：
//   1. NcmTransport::open() 枚举 USB CDC ECM 设备，建立网络适配器
//   2. 通过 NCM 网卡建立 TCP 连接（connect/listen）
//   3. 分离 video/control/touch 三个逻辑通道（复用同一条 TCP）
//
class NcmTransport : public ITransport {
public:
    static std::vector<std::string> listAdapters();
    bool open(const TransportSpec& spec) override;
    void close() override;
    bool isOpen() const override;
    IChannel* videoChannel()   override;
    IChannel* controlChannel() override;
    IChannel* touchChannel()   override;
    bool reset() override;
    std::string lastError() const override;
    TransportKind kind() const override { return TransportKind::Ncm; }
    std::string localIp() const;
    std::string peerIp() const;
    bool isLinkUp() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<ITransport> createTransport(const TransportSpec& spec);
std::unique_ptr<ITransport> createTransport(TransportKind kind, const TransportSpec& spec);
inline std::unique_ptr<ITransport> createTransport(TransportKind kind, const UsbFilter& f) {
    TransportSpec s; s.kind = kind; s.usb = f;
    return createTransport(kind, s);
}

std::unique_ptr<ITransport> createWinUsbTransport();
std::unique_ptr<ITransport> createLoopbackTransport();
std::unique_ptr<ITransport> createTcpTransport();
std::unique_ptr<ITransport> createNcmTransport();   // Issue #14

}  // namespace apxdisp