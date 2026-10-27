// 手机模式状态帧（0x31）位布局（issue #47 面板三开关改状态显示）。
//
// 这份位图两端各写一遍（C++ 面板侧 / Android ModeStatus.kt），是最容易漂移的那类东西：
// 位序错了不会崩，只会「面板显示蓝牙当前在用、手机其实走的无线」。这里把位序钉死，
// Android 侧的 ModeStatusTest 用同一组期望值对拍（见 android/app/src/test/...）。
#include "apxpc/wireless/phone_mode.hpp"

#include <cstdio>

using namespace apxpc::wireless;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

static void testBitLayout() {
    // 位序是本帧的对外契约，改动必须同时改 PROTOCOL.md 与 Kotlin 侧
    CHECK(kModeWirelessAvailable == 0x01);
    CHECK(kModeBluetoothAvailable == 0x02);
    CHECK(kModeUsbAvailable == 0x04);
    CHECK(kModeWirelessActive == 0x08);
    CHECK(kModeBluetoothActive == 0x10);
    CHECK(kModeUsbActive == 0x20);
}

static void testPackUnpack() {
    // 只有无线可用且在用
    uint8_t f = packPhoneMode(true, false, false, "wireless");
    CHECK(f == 0x09);
    PhoneMode m = decodePhoneMode(f);
    CHECK(m.reported);
    CHECK(m.wirelessAvailable);
    CHECK(!m.bluetoothAvailable);
    CHECK(!m.usbAvailable);
    CHECK(m.active == "wireless");

    // 蓝牙可用在用，另外两条都可用但没在用
    f = packPhoneMode(true, true, true, "bluetooth");
    CHECK(f == 0x17);
    m = decodePhoneMode(f);
    CHECK(m.wirelessAvailable && m.bluetoothAvailable && m.usbAvailable);
    CHECK(m.active == "bluetooth");

    // USB 在用
    CHECK(decodePhoneMode(packPhoneMode(false, false, true, "usb")).active == "usb");

    // 没有任何出口在用（"none"）：可用性位与在用位互不影响
    f = packPhoneMode(true, true, true, "none");
    CHECK(f == 0x07);
    CHECK(decodePhoneMode(f).active == "none");

    // 未知字符串不得拼出任何「在用」位
    CHECK(packPhoneMode(false, false, false, "wifi") == 0x00);
}

static void testActivePriority() {
    // 多位置位（理论上不该出现）：按无线 > 蓝牙 > USB 取，与 Uplink.resolve 同优先级
    CHECK(phoneModeActiveName(0x08 | 0x10 | 0x20) == "wireless");
    CHECK(phoneModeActiveName(0x10 | 0x20) == "bluetooth");
    CHECK(phoneModeActiveName(0x20) == "usb");
    CHECK(phoneModeActiveName(0x00) == "none");
    // 只有可用位、没有在用位 → none（不能把「可用」当成「在用」）
    CHECK(phoneModeActiveName(0x07) == "none");
}

int main() {
    testBitLayout();
    testPackUnpack();
    testActivePriority();
    if (g_fail) {
        std::printf("phone_mode_test: %d 项失败\n", g_fail);
        return 1;
    }
    std::printf("phone_mode_test: OK\n");
    return 0;
}
