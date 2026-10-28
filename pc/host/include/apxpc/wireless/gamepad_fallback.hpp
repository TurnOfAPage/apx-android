// ============================================================================
// 手柄「免驱降级」的纯算术（issue#47 第 5 条）
//
// 背景：手机把 0x07 手柄帧发到 PC 后，PC 侧能做什么**取决于平台**：
//   · Linux  → /dev/uinput 虚拟出真手柄（BTN_A/ABS_X…），游戏原生识别；
//   · Windows→ 有 ViGEmBus 时真手柄，没有则键盘/鼠标/滚轮降级；
//   · macOS  → 没有 CGEvent 手柄通道；虚拟 HID（IOHIDUserDevice）需要 Apple 受限
//              entitlement，ad-hoc 签名一注入就被 AMFI 杀掉 —— 只能降级。
// 三端降级的**算术完全一样**，所以抽到这里：平台文件只保留「按钮位 → 本平台键码」
// 的那张表，边沿计算、死区、缩放、限速都由本头文件给出，并有 gamepad_fallback_test
// 在 CI 里钉住（mac/Windows 只能靠 CI 验证，算术下沉到 host 就能本地跑）。
// ============================================================================
#pragma once

#include <cstdint>

namespace apxpc::wireless {

/// 摇杆死区：i8 轴绝对值 ≤ 12 视为回中（与 Windows 老实现一致，别改小 —— 廉价手柄抖动大）
inline constexpr int kGamepadDeadZone = 12;
/// 左摇杆 → 鼠标相对位移的除数（1/4 缩放；不减速度的话摇杆帧率下光标会飞）
inline constexpr int kGamepadMouseDivisor = 4;
/// 右摇杆 → 滚轮的除数
inline constexpr int kGamepadWheelDivisor = 8;
/// 滚轮限速间隔：摇杆帧率可达几十 Hz，不限速会疯狂滚动
inline constexpr uint64_t kGamepadWheelIntervalMs = 50;
/// 手柄按钮位数（与手机端位序一致：A B X Y L1 R1 L2 R2 SELECT START C Z MODE THUMBL THUMBR）
inline constexpr int kGamepadButtonCount = 16;

/// 一帧的按钮边沿：pressed = 本帧新按下，released = 本帧新抬起
struct GamepadEdges {
    uint16_t pressed = 0;
    uint16_t released = 0;
};

/// 由「上一帧位图 + 本帧位图」算边沿。降级注入是差量语义，必须算边沿，
/// 否则按住不放会变成每帧重复按下（游戏里连点、菜单乱跳）。
inline GamepadEdges gamepadEdges(uint16_t prev, uint16_t now) {
    GamepadEdges e;
    e.pressed = static_cast<uint16_t>(now & ~prev);
    e.released = static_cast<uint16_t>(prev & ~now);
    return e;
}

/// 摇杆是否越过死区（越过了才注入，回中不产生任何事件）
inline bool gamepadStickActive(int8_t v) {
    return v > kGamepadDeadZone || v < -kGamepadDeadZone;
}

/// 摇杆 → 鼠标相对位移（已按除数缩放并钳在 i8 范围内）
inline int8_t gamepadStickToMouse(int8_t v) {
    const int s = static_cast<int>(v) / kGamepadMouseDivisor;
    if (s > 127) return 127;
    if (s < -127) return -127;
    return static_cast<int8_t>(s);
}

/// 摇杆 → 单步滚动量（调用方决定正负方向：垂直滚动取负号，因为摇杆前推是负值）
inline int gamepadStickStep(int8_t v, int divisor = kGamepadWheelDivisor) {
    return static_cast<int>(v) / divisor;
}

/// 滚轮限速：距上次注入 ≥ interval 才允许
inline bool gamepadWheelAllowed(uint64_t lastMs, uint64_t nowMs,
                                uint64_t intervalMs = kGamepadWheelIntervalMs) {
    return nowMs - lastMs >= intervalMs;
}

}  // namespace apxpc::wireless
