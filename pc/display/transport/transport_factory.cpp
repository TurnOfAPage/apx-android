// 传输工厂（USB / TCP / NCM / 无线调试统一入口）
#include "transport/i_transport.hpp"

#include <string>

#include "common/log.hpp"

namespace apxdisp {

#ifdef _WIN32
std::unique_ptr<ITransport> createWinUsbTransport();
#else
std::unique_ptr<ITransport> createWinUsbTransport() { return nullptr; }
#endif
#ifdef APXDISP_USE_LIBUSB
std::unique_ptr<ITransport> createLibUsbTransport();
#endif
std::unique_ptr<ITransport> createLoopbackTransport();
#ifdef _WIN32
std::unique_ptr<ITransport> createTcpTransport();
#else
std::unique_ptr<ITransport> createTcpTransport() { return nullptr; }
#endif

std::unique_ptr<ITransport> createTransport(const TransportSpec& spec) {
    switch (spec.kind) {
        case TransportKind::WinUsb: {
            auto t = createWinUsbTransport();
            if (t && t->open(spec)) return t;
            APX_LOG_W("WinUSB 打开失败：%s", t ? t->lastError().c_str() : "无实现");
            return nullptr;
        }
        case TransportKind::LibUsb: {
#ifdef APXDISP_USE_LIBUSB
            auto t = createLibUsbTransport();
            if (t && t->open(spec)) return t;
            APX_LOG_W("libusb 打开失败：%s", t ? t->lastError().c_str() : "无实现");
            return nullptr;
#else
            APX_LOG_W("本构建未启用 libusb 后端（APXDISP_USE_LIBUSB=OFF）");
            return nullptr;
#endif
        }
        case TransportKind::Tcp: {
            auto t = createTcpTransport();
            if (t && t->open(spec)) return t;
            APX_LOG_W("TCP 打开失败：%s", t ? t->lastError().c_str() : "无实现");
            return nullptr;
        }
        case TransportKind::Ncm: {
            // Issue #14：显式指定 NCM（TCP over USB-ECM）
#ifdef APXDISP_ENABLE_NCM
            auto t = createNcmTransport();
            if (t && t->open(spec)) return t;
            APX_LOG_W("NCM 打开失败：%s", t ? t->lastError().c_str() : "无实现");
#else
            APX_LOG_W("本构建未启用 NCM（APXDISP_ENABLE_NCM=OFF）");
#endif
            return nullptr;
        }
        case TransportKind::Loopback: {
            auto t = createLoopbackTransport();
            if (t) t->open(spec);
            return t;
        }
        case TransportKind::Auto:
        default:
            break;
    }

    // Auto：优先 WinUSB（AOA bulk）；失败且已枚举到 CDC/ECM/RNDIS 网卡时试
    // NCM（免驱动，Issue #14）；仍失败退回 Loopback（保证自测可跑，不静默失败）。
    // 预检 listAdapters 非空才尝试 NCM：避免无网卡时白等一次 accept 超时。
    {
        TransportSpec usb = spec;
        usb.kind = TransportKind::WinUsb;
        auto win = createWinUsbTransport();
        if (win && win->open(usb)) return win;
        APX_LOG_W("无可用 USB 设备（%s），尝试 NCM 或退回 Loopback",
                  win ? win->lastError().c_str() : "无实现");
    }
#ifdef APXDISP_ENABLE_NCM
    {
        if (!NcmTransport::listAdapters().empty()) {
            TransportSpec ncm = spec;
            ncm.kind = TransportKind::Ncm;
            auto t = createNcmTransport();
            if (t && t->open(ncm)) {
                APX_LOG_I("NCM 承载就绪（TCP over USB-ECM）");
                return t;
            }
            APX_LOG_W("NCM 打开失败：%s", t ? t->lastError().c_str() : "无实现");
        }
    }
#endif
    {
        auto loop = createLoopbackTransport();
        if (loop) loop->open(spec);
        return loop;
    }
}

std::unique_ptr<ITransport> createTransport(TransportKind kind, const TransportSpec& spec) {
    TransportSpec s = spec;
    s.kind = kind;
    return createTransport(s);
}

}  // namespace apxdisp
