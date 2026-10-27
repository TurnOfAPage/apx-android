// 手柄免驱降级的算术（issue#47 第 5 条回归用例）。
//
// 「手柄接上电脑没反应」这条在 macOS 上根本编不出真手柄（虚拟 HID 需 Apple 受限
// entitlement，ad-hoc 签名一注入就被 AMFI 杀），只能降级成键盘/鼠标/滚轮；Windows
// 没有 ViGEmBus 时同理。降级的算术三端共用（gamepad_fallback.hpp），用例就在这里
// 钉住：边沿、死区、缩放、限速。mac/Windows 的注入代码只能靠 CI 编译验证，
// 把算术下沉到 host 是为了「至少这半边能在本机真跑」。
#include "apxpc/wireless/gamepad_fallback.hpp"

#include <cstdio>

using namespace apxpc::wireless;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

static void testEdges() {
    // 首帧：上一帧为 0 → 全部算「新按下」（与老 Windows 实现同语义）
    GamepadEdges e = gamepadEdges(0x0000, 0x0001);
    CHECK(e.pressed == 0x0001);
    CHECK(e.released == 0x0000);

    // 保持按下：不得产生任何边沿（否则按住会变成每帧连点）
    e = gamepadEdges(0x0001, 0x0001);
    CHECK(e.pressed == 0);
    CHECK(e.released == 0);

    // 抬起
    e = gamepadEdges(0x0001, 0x0000);
    CHECK(e.pressed == 0);
    CHECK(e.released == 0x0001);

    // 一按一抬同时发生（游戏里常见）
    e = gamepadEdges(0x0002, 0x0001);
    CHECK(e.pressed == 0x0001);
    CHECK(e.released == 0x0002);

    // 高位（bit15 未使用，但位序必须原样保留，不能被截断）
    e = gamepadEdges(0x0000, 0x8000);
    CHECK(e.pressed == 0x8000);
}

static void testDeadZone() {
    CHECK(!gamepadStickActive(0));
    CHECK(!gamepadStickActive(kGamepadDeadZone));       // 边界值本身算回中
    CHECK(!gamepadStickActive(-kGamepadDeadZone));
    CHECK(gamepadStickActive(kGamepadDeadZone + 1));
    CHECK(gamepadStickActive(-kGamepadDeadZone - 1));
    CHECK(gamepadStickActive(-128));                    // i8 极值
    CHECK(gamepadStickActive(127));
}

static void testScale() {
    // 位移按 1/4 缩放
    CHECK(gamepadStickToMouse(0) == 0);
    CHECK(gamepadStickToMouse(127) == 31);
    CHECK(gamepadStickToMouse(-128) == -32);
    // 结果必须留在 i8 内（协议外注入 API 收的就是 int8）
    CHECK(gamepadStickToMouse(127) <= 127);
    CHECK(gamepadStickToMouse(-128) >= -128);

    // 滚轮步长按 1/8
    CHECK(gamepadStickStep(0) == 0);
    CHECK(gamepadStickStep(127) == 15);
    CHECK(gamepadStickStep(-128) == -16);
    // 垂直方向取负：摇杆前推（ry 为负）应得到正滚轮（内容上滚）
    CHECK(-gamepadStickStep(-128) > 0);
}

static void testWheelRateLimit() {
    // 从未滚过（last=0）时第一帧就允许
    CHECK(gamepadWheelAllowed(0, 1000));
    // 间隔内禁止
    CHECK(!gamepadWheelAllowed(1000, 1049));
    // 到达间隔放行
    CHECK(gamepadWheelAllowed(1000, 1050));
    CHECK(gamepadWheelAllowed(1000, 2000));
    // 时钟回绕/相等（steady 时钟不会倒流，但相等要禁止，否则每帧都滚）
    CHECK(!gamepadWheelAllowed(1000, 1000));
}

int main() {
    testEdges();
    testDeadZone();
    testScale();
    testWheelRateLimit();
    if (g_fail) {
        std::printf("gamepad_fallback_test: %d 项失败\n", g_fail);
        return 1;
    }
    std::printf("gamepad_fallback_test: OK\n");
    return 0;
}
