// ============================================================================
// 手机「三种接入模式」状态帧（9511 控制面 0x31）的位布局与解码
//
// 为什么需要它（issue#47 面板侧诉求）：Web 面板的连接卡上有「无线 / 蓝牙 / USB」
// 三个开关，但其中**只有无线是真开关**（启停 9511 会话），蓝牙与 USB 的真开关在手机端
// （配对 / root 配 gadget），PC 只是被动 host —— 摆个开关在界面上，用户拨了没反应，
// 只能认定「坏了」。改成：手机把自己三种模式的**可用性 + 当前实际在用哪个**报上来，
// 面板照实显示，开关不再骗人。
//
// 线路格式（0x31，控制端=手机 → 被控端=PC）：
//   [0]=0x31  [1]=flags  [2]=active（可选，老版本没有这一字节时按 flags 高位推）
//   flags bit0=无线可用 bit1=蓝牙可用 bit2=USB 可用
//          bit3=无线当前在用 bit4=蓝牙当前在用 bit5=USB 当前在用
// 「可用」＝这条出口此刻具备工作条件（例如蓝牙适配器已开、USB gadget 已就绪、
// 已选定无线受控目标）；「在用」＝上一次真实输入走的出口。两者都不是「曾经连过」。
//
// 位序在这里钉住，Android 侧 ModeStatus.kt 与用例 phone_mode_test 两侧对齐 ——
// 这个仓库被「两端各写一遍、悄悄漂移」坑过太多次。
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace apxpc::wireless {

inline constexpr uint8_t kModeWirelessAvailable = 1u << 0;
inline constexpr uint8_t kModeBluetoothAvailable = 1u << 1;
inline constexpr uint8_t kModeUsbAvailable = 1u << 2;
inline constexpr uint8_t kModeWirelessActive = 1u << 3;
inline constexpr uint8_t kModeBluetoothActive = 1u << 4;
inline constexpr uint8_t kModeUsbActive = 1u << 5;

/// 解码后的手机模式状态（面板直接用）
struct PhoneMode {
    bool reported = false;           ///< false = 手机从未上报（未连入 9511）
    bool wirelessAvailable = false;
    bool bluetoothAvailable = false;
    bool usbAvailable = false;
    /// 当前在用的出口："wireless" / "bluetooth" / "usb" / "none"
    std::string active = "none";
};

/// flags → 当前出口名。**只有一个**位算「在用」；多位置位（理论上不该出现）按
/// 无线 > 蓝牙 > USB 取，与 [Uplink.resolve] 的优先级一致，避免同一份数据两种口径。
inline std::string phoneModeActiveName(uint8_t flags) {
    if (flags & kModeWirelessActive) return "wireless";
    if (flags & kModeBluetoothActive) return "bluetooth";
    if (flags & kModeUsbActive) return "usb";
    return "none";
}

/// flags → [PhoneMode]
inline PhoneMode decodePhoneMode(uint8_t flags) {
    PhoneMode m;
    m.reported = true;
    m.wirelessAvailable = (flags & kModeWirelessAvailable) != 0;
    m.bluetoothAvailable = (flags & kModeBluetoothAvailable) != 0;
    m.usbAvailable = (flags & kModeUsbAvailable) != 0;
    m.active = phoneModeActiveName(flags);
    return m;
}

/// 反向打包（Android 侧同款逻辑的参照实现；用例两侧对拍位序）
inline uint8_t packPhoneMode(bool wirelessAvailable, bool bluetoothAvailable, bool usbAvailable,
                             const std::string& active) {
    uint8_t f = 0;
    if (wirelessAvailable) f |= kModeWirelessAvailable;
    if (bluetoothAvailable) f |= kModeBluetoothAvailable;
    if (usbAvailable) f |= kModeUsbAvailable;
    if (active == "wireless") f |= kModeWirelessActive;
    else if (active == "bluetooth") f |= kModeBluetoothActive;
    else if (active == "usb") f |= kModeUsbActive;
    return f;
}

}  // namespace apxpc::wireless
