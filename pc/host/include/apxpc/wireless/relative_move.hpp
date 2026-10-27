// ============================================================================
// 相对位移 → 绝对坐标 / 拖动事件类型（纯逻辑，跨平台可测）
//
// 起因（issue #47 第 6 条）：「无线模式下 pad 实体键盘触摸板、app 虚拟触控板都不可用」。
// 旧 macOS 注入实现把相对位移写进 kCGMouseEventDeltaX / kCGMouseEventDeltaY，
// 却仍在**当前坐标**上投递 kCGEventMouseMoved：
//     CGEventCreateMouseEvent(…, kCGEventMouseMoved,
//                             CGEventGetLocation(…),   // ← 还是老位置
//                             kCGMouseButtonLeft);
//     CGEventSetIntegerValueField(e, kCGMouseEventDeltaX, dx);
// 投递事件的落点由 location 决定，DeltaX/Y 只是事件自带的「增量信息」，不驱动光标。
// 于是每一帧都等价于「把光标移到它现在所在的位置」—— 光标纹丝不动，用户看到的就是
// 「触控板划了没反应」。键盘走 CGEventCreateKeyboardEvent，不经过这条路，
// 所以现象正好是「键盘能用、触控板不能用」。
//
// 正确做法：先读当前坐标 → 加上增量得到目标坐标 → 按目标坐标投递。这段换算与
// 「按住键时应该投递拖动事件而不是普通移动」都是纯算术，放在这个头里，
// Linux / Windows 的 CI 也能跑它的用例（macOS 的 CGEvent 调用本身必须真机验）。
// ============================================================================
#pragma once

namespace apxpc::wireless {

/// 屏幕坐标（浮点）。macOS 的 CoreGraphics 坐标，单位是点而非像素。
struct PointD {
    double x = 0.0;
    double y = 0.0;
};

/// 屏幕区域（含边界）。left/top 通常为 0，多显示器时可能是负值。
struct BoxD {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
};

/// 目标坐标 = 当前坐标 + 相对位移。不做取整：调用方按平台需要再处理。
inline PointD relativeMoveTarget(PointD cur, int dx, int dy) {
    return PointD{cur.x + static_cast<double>(dx),
                  cur.y + static_cast<double>(dy)};
}

/// 把点夹进区域。区域无效（right <= left 或 bottom <= top）时原样返回 ——
/// 宁可让系统自己去夹，也不要把光标吸到 (0, 0)（取不到显示器列表时就是这样）。
inline PointD clampToBox(PointD p, const BoxD& b) {
    if (b.right <= b.left || b.bottom <= b.top) return p;
    if (p.x < b.left) p.x = b.left;
    else if (p.x > b.right) p.x = b.right;
    if (p.y < b.top) p.y = b.top;
    else if (p.y > b.bottom) p.y = b.bottom;
    return p;
}

/// 按住键时的移动应该投递「拖动」事件，否则部分应用收不到拖拽（选区、拖文件、拖窗口）。
/// 位定义与协议 0x01 帧的 buttons 一致：0x01 左、0x02 右、0x04 中。
enum class MoveKind {
    Move,
    LeftDrag,
    RightDrag,
    OtherDrag,
};

inline MoveKind moveKindForButtons(unsigned buttons) {
    if (buttons & 0x01u) return MoveKind::LeftDrag;
    if (buttons & 0x02u) return MoveKind::RightDrag;
    if (buttons & 0x04u) return MoveKind::OtherDrag;
    return MoveKind::Move;
}

}  // namespace apxpc::wireless
