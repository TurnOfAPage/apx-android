// ============================================================================
// macOS 输入注入 + 剪贴板（9511 受控端）
// 注入走 CoreGraphics CGEvent（C API，免驱）；剪贴板走 NSPasteboard（Objective-C）。
// 编译为 Objective-C++（.mm）。多媒体键在 macOS 需 NXSystemDefined 事件，本实现暂忽略。
// ============================================================================
#include "apxpc/wireless/ctrl9511.hpp"
// 相对位移 → 目标坐标 / 拖动类型：纯算术抽出来给用例盯着（issue #47 第 6 条）。
#include "apxpc/wireless/relative_move.hpp"

#if defined(__APPLE__)

#include <CoreGraphics/CoreGraphics.h>
#import <AppKit/AppKit.h>

#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <vector>

namespace apxpc::wireless {
namespace {

// HID usage (0x07) → macOS 虚拟键码（CGKeyCode）。仅覆盖常用键。
int hidToMacKey(uint8_t u) {
    static const int A = 0x00, B = 0x0B, C = 0x08, D = 0x02, E = 0x0E, F = 0x03, G = 0x05,
                     H = 0x04, I = 0x22, J = 0x26, K = 0x28, L = 0x25, M = 0x2E, N = 0x2D,
                     O = 0x1F, P = 0x23, Q = 0x0C, R = 0x0F, S = 0x01, T = 0x11, U = 0x20,
                     V = 0x09, W = 0x0D, X = 0x07, Y = 0x10, Z = 0x06;
    switch (u) {
        case 0x04: return A; case 0x05: return B; case 0x06: return C; case 0x07: return D;
        case 0x08: return E; case 0x09: return F; case 0x0A: return G; case 0x0B: return H;
        case 0x0C: return I; case 0x0D: return J; case 0x0E: return K; case 0x0F: return L;
        case 0x10: return M; case 0x11: return N; case 0x12: return O; case 0x13: return P;
        case 0x14: return Q; case 0x15: return R; case 0x16: return S; case 0x17: return T;
        case 0x18: return U; case 0x19: return V; case 0x1A: return W; case 0x1B: return X;
        case 0x1C: return Y; case 0x1D: return Z;
        case 0x1E: return 0x12; case 0x1F: return 0x13; case 0x20: return 0x14; case 0x21: return 0x15;
        case 0x22: return 0x17; case 0x23: return 0x16; case 0x24: return 0x1A; case 0x25: return 0x1C;
        case 0x26: return 0x19; case 0x27: return 0x1D;
        case 0x28: return 0x24;   // Enter
        case 0x29: return 0x35;   // Esc
        case 0x2A: return 0x33;   // Backspace
        case 0x2B: return 0x30;   // Tab
        case 0x2C: return 0x31;   // Space
        case 0x4F: return 0x7B;   // Right
        case 0x50: return 0x7C;   // Left
        case 0x51: return 0x7D;   // Down
        case 0x52: return 0x7E;   // Up
        case 0x4C: return 0x75;   // Delete (forward)
        default: return -1;
    }
}

// ---------- 自由函数共用：稳态时钟 + 剪贴板回环抑制（对齐 win 版 clipSuppressFor(600)）---
long long steadyMsNow() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
std::atomic<long long> g_clipSuppressMs{0};

// ---------- 光标定位 / 显示器并集 / 拖动事件类型（issue #47 第 6 条）----------
// 这里三个都是 injectMouse 的支撑：读当前光标、算出夹取范围、决定投递哪种事件。

/// 读当前光标位置。`CGEventCreate(nullptr)` 造的是「无源事件」，只用来取坐标，
/// 读完必须 CFRelease —— 旧实现写成 `CGEventGetLocation(CGEventCreate(nullptr))`，
/// 每帧漏一个事件对象（连续滑动时是无上限的泄漏）。
CGPoint currentCursorPos() {
    CGEventRef e = CGEventCreate(nullptr);
    const CGPoint p = e ? CGEventGetLocation(e) : CGPointMake(0, 0);
    if (e) CFRelease(e);
    return p;
}

/// 全部活动显示器的并集（副屏在主屏左侧时 origin 为负，所以要并集而不是主屏 bounds）。
/// 取不到时返回**无效框**：clampToBox 对无效框不夹，宁可交给系统，也不要把光标吸到 (0, 0)。
BoxD activeDisplayBox() {
    uint32_t n = 0;
    if (CGGetActiveDisplayList(0, nullptr, &n) != kCGErrorSuccess || n == 0) return BoxD{};
    std::vector<CGDirectDisplayID> ids(n);
    uint32_t got = 0;
    if (CGGetActiveDisplayList(n, ids.data(), &got) != kCGErrorSuccess || got == 0) return BoxD{};
    CGRect u = CGRectNull;
    for (uint32_t i = 0; i < got; ++i) u = CGRectUnion(u, CGDisplayBounds(ids[i]));
    if (CGRectIsNull(u)) return BoxD{};
    return BoxD{u.origin.x, u.origin.y,
                u.origin.x + u.size.width - 1.0, u.origin.y + u.size.height - 1.0};
}

/// 按住键时的移动必须换成「拖动」事件，否则部分应用收不到拖拽（选区、拖文件、拖窗口）。
CGEventType moveEventType(MoveKind k) {
    switch (k) {
        case MoveKind::LeftDrag:  return kCGEventLeftMouseDragged;
        case MoveKind::RightDrag: return kCGEventRightMouseDragged;
        case MoveKind::OtherDrag: return kCGEventOtherMouseDragged;
        case MoveKind::Move:
        default:                  return kCGEventMouseMoved;
    }
}

/// 移动事件的按键参数要与拖动类型配套，不能一律 kCGMouseButtonLeft。
CGMouseButton moveButton(MoveKind k) {
    switch (k) {
        case MoveKind::RightDrag: return kCGMouseButtonRight;
        case MoveKind::OtherDrag: return kCGMouseButtonCenter;
        default:                  return kCGMouseButtonLeft;
    }
}

}  // namespace（匿名）

class MacInjector : public InputInjector {
public:
    void injectMouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel, int8_t pan = 0) override {
        // 顺序：先在**当前位置**结算按键状态变化（按下 / 抬起），再移动，最后滚轮。
        // 反过来的话「按下 + 同帧位移」会在键还没按下时先发拖动事件（部分应用直接丢），
        // 「抬起 + 同帧位移」则会先移动再抬手，拖拽尾巴被判成一次普通移动。
        const CGPoint cur = currentCursorPos();
        applyButtonTransitions(buttons, cur);
        if (dx || dy) {
            // ★ 落点必须是**算出来的目标坐标**（当前坐标 + 增量）。
            //   旧实现把增量塞进 kCGMouseEventDeltaX/Y、落点却仍用当前坐标 ——
            //   投递事件的落点由 location 决定，增量字段不驱动光标，于是每帧都等价于
            //   「把光标移到它现在所在的位置」：光标纹丝不动，用户看到的就是
            //   「触控板划了没反应」。键盘走 CGEventCreateKeyboardEvent，不经过这条路，
            //   所以现象正好是「键盘能用、触控板不能用」（issue #47 第 6 条）。
            const PointD target = clampToBox(
                relativeMoveTarget(PointD{cur.x, cur.y}, dx, dy), activeDisplayBox());
            const MoveKind kind = moveKindForButtons(mouseButtons_);
            CGEventRef e = CGEventCreateMouseEvent(nullptr, moveEventType(kind),
                                                   CGPointMake(target.x, target.y),
                                                   moveButton(kind));
            CGEventSetIntegerValueField(e, kCGMouseEventDeltaX, dx);
            CGEventSetIntegerValueField(e, kCGMouseEventDeltaY, dy);
            CGEventPost(kCGHIDEventTap, e); CFRelease(e);
        }
        if (wheel || pan) {
            // 轴数 2 = (垂直, 水平)。CGEventCreateScrollWheelEvent 是变参函数：
            // axisCount 给几个，后面就按顺序读几个 int32 —— 常量 1 的老写法只能带垂直。
            CGEventRef e = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine,
                                                         2, wheel, pan);
            CGEventPost(kCGHIDEventTap, e); CFRelease(e);
        }
    }

    /// 结算按键状态变化。每个键位用**对应**的 CGMouseButton：旧实现一律传
    /// kCGMouseButtonLeft，右键 / 中键在部分应用里会被认成左键事件。
    void applyButtonTransitions(uint8_t buttons, CGPoint at) {
        const struct { uint8_t bit; CGEventType down; CGEventType up; CGMouseButton btn; } table[] = {
            {0x01, kCGEventLeftMouseDown, kCGEventLeftMouseUp, kCGMouseButtonLeft},
            {0x02, kCGEventRightMouseDown, kCGEventRightMouseUp, kCGMouseButtonRight},
            {0x04, kCGEventOtherMouseDown, kCGEventOtherMouseUp, kCGMouseButtonCenter},
        };
        for (const auto& t : table) {
            const bool was = (mouseButtons_ & t.bit) != 0, now = (buttons & t.bit) != 0;
            if (was == now) continue;
            CGEventRef e = CGEventCreateMouseEvent(nullptr, now ? t.down : t.up, at, t.btn);
            CGEventPost(kCGHIDEventTap, e); CFRelease(e);
        }
        mouseButtons_ = buttons;
    }

    void injectTouch(uint8_t action, uint8_t buttons, uint16_t x, uint16_t y) override {
        (void)buttons;
        CGRect pb = CGDisplayBounds(CGMainDisplayID());
        const CGFloat px = static_cast<CGFloat>(x) / 65535.0 * (pb.origin.x + pb.size.width);
        const CGFloat py = static_cast<CGFloat>(y) / 65535.0 * (pb.origin.y + pb.size.height);
        CGPoint pos = CGPointMake(px, py);
        constexpr uint8_t kDown = 0, kUp = 1;
        CGEventType type = (action == kDown) ? kCGEventLeftMouseDown
                        : (action == kUp) ? kCGEventLeftMouseUp : kCGEventMouseMoved;
        CGEventRef e = CGEventCreateMouseEvent(nullptr, type, pos, kCGMouseButtonLeft);
        CGEventPost(kCGHIDEventTap, e); CFRelease(e);
    }

    void injectKeyboard(uint8_t mod, const uint8_t* keys, size_t count) override {
        const size_t n = count > 6 ? 6 : count;
        // 修饰键：Mac 不区分左右，统一用左侧键码（控制=0x3B/0x38? 用 0x3B=Control,0x38=Shift,0x3A=Option,0x37=Cmd）
        const int modMap[8] = {0x3B, 0x38, 0x3A, 0x37, 0x3B, 0x38, 0x3A, 0x37};
        for (int bit = 0; bit < 8; ++bit) {
            const uint8_t m = static_cast<uint8_t>(1u << bit);
            const bool was = (kbMod_ & m) != 0, now = (mod & m) != 0;
            if (was == now) continue;
            CGKeyCode kc = static_cast<CGKeyCode>(modMap[bit]);
            CGEventRef e = CGEventCreateKeyboardEvent(nullptr, kc, now);
            CGEventPost(kCGHIDEventTap, e); CFRelease(e);
        }
        for (uint8_t i = 0; i < 6; ++i) {
            const uint8_t k = kbKeys_[i]; if (!k) continue;
            bool still = false; for (size_t j = 0; j < n; ++j) if (keys[j] == k) { still = true; break; }
            if (!still) postKey(k, false);
        }
        for (size_t j = 0; j < n; ++j) {
            const uint8_t k = keys[j]; if (!k) continue;
            bool existed = false; for (uint8_t i = 0; i < 6; ++i) if (kbKeys_[i] == k) { existed = true; break; }
            if (!existed) postKey(k, true);
        }
        kbMod_ = mod;
        for (uint8_t i = 0; i < 6; ++i) kbKeys_[i] = (i < n) ? keys[i] : 0;
    }
    void postKey(uint8_t usage, bool down) {
        const int kc = hidToMacKey(usage);
        if (kc < 0) return;
        CGEventRef e = CGEventCreateKeyboardEvent(nullptr, static_cast<CGKeyCode>(kc), down);
        CGEventPost(kCGHIDEventTap, e); CFRelease(e);
    }

    void injectConsumer(uint16_t) override {
        // macOS 多媒体键需 NXSystemDefined，跨版本不稳定，暂忽略（不影响鼠标/键盘/触摸）。
    }

    void injectGamepad(uint16_t buttons, int8_t x, int8_t y, int8_t rx, int8_t ry) override {
        // macOS 的 CGEvent 不提供游戏手柄注入；需虚拟 HID 驱动（不在本仓库零依赖范围内）。
        // 帧已接收，但按钮/轴不会送达游戏——如实降级，仅告警一次。
        (void)buttons; (void)x; (void)y; (void)rx; (void)ry;
        static bool warned = false;
        if (!warned) { warned = true; std::fprintf(stderr, "[AllPeriph] gamepad-over-network 在 macOS 需虚拟 HID 驱动，当前构建未包含，按钮/轴不会注入游戏\n"); }
    }

    void setClipboard(const std::string& text) override {
        NSString* s = [NSString stringWithUTF8String:text.c_str()];
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        [pb setString:s forType:NSPasteboardTypeString];
    }

    uint8_t mouseButtons_ = 0;
    uint8_t kbMod_ = 0;
    uint8_t kbKeys_[6] = {0, 0, 0, 0, 0, 0};
};

class MacClipboardWatcher : public ClipboardWatcher {
public:
    explicit MacClipboardWatcher(std::function<void(const std::string&)> cb) : cb_(std::move(cb)) {}
    /* 析构必须 stop：start() 会起一个跑在大局外的线程，detach 版本的实现（历史）会
       让线程活过对象生存期（use-after-free）；不 stop 则未 join 的 std::thread 析构
       直接 std::terminate。v223 崩溃 issue#47 的两处根因就在这里——
       实测崩溃栈：#7 线程析构 → #6/#5/#4 terminate → abort，帧 #7 所在函数 = 本类析构。
       修法两条一起上：① 析构停线程；② 线程内用 shared_ptr 持有「活着」标志，
       join 失败（极少见）时线程自行退出、不再碰已析构的 this。 */
    ~MacClipboardWatcher() override { stop(); }
    bool start() override {
        if (thread_.joinable()) return false;      // 重复 start：直接拒绝，不覆盖线程句柄
        running_ = true;
        auto alive = alive_;
        auto cb = cb_;
        thread_ = std::thread([alive, cb] {
            std::string last;
            while (alive->load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(800));
                if (!alive->load()) break;
                // 回环抑制：setSystemClipboard 写入后 600ms 内不回传（与 win 版一致，
                // 否则反向剪贴板 0x21 → 写本机 → watcher → 又发回对端 = 刷乱对端剪贴板）
                if (steadyMsNow() - g_clipSuppressMs.load() < 600) continue;
                NSPasteboard* pb = [NSPasteboard generalPasteboard];
                NSString* s = [pb stringForType:NSPasteboardTypeString];
                if (s) {
                    std::string cur([s UTF8String]);
                    if (!cur.empty() && cur != last) { last = cur; cb(cur); }
                }
            }
        });
        return true;
    }
    void stop() override {
        running_ = false;
        if (alive_) alive_->store(false);
        if (thread_.joinable()) thread_.join();
    }
private:
    std::function<void(const std::string&)> cb_;
    std::atomic<bool> running_{false};
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    std::thread thread_;
};

std::unique_ptr<InputInjector> createPlatformInjector() { return std::make_unique<MacInjector>(); }
std::unique_ptr<ClipboardWatcher> createPlatformClipboardWatcher(
    std::function<void(const std::string&)> cb) {
    return std::make_unique<MacClipboardWatcher>(std::move(cb));
}

// ============================================================================
// 与 input_injector_mac.mm 对等的自由函数（ctrl9511.hpp 声明，无线会话/宿主直接调用）
// ============================================================================

// Windows VK 码 → macOS 虚拟键码（协议两侧共用 VK 码空间，见 wireless_session 注释）
static int vkToMacKey(uint8_t vk) {
    switch (vk) {
        case 0x08: return 0x33;  // Backspace（VK_BACK）
        case 0x09: return 0x30;  // Tab
        case 0x0D: return 0x24;  // Enter
        case 0x1B: return 0x35;  // Esc
        case 0x20: return 0x31;  // Space
        case 0x21: return 0x74;  // PageUp
        case 0x22: return 0x79;  // PageDown
        case 0x23: return 0x77;  // End
        case 0x24: return 0x73;  // Home
        case 0x25: return 0x7B;  // Left
        case 0x26: return 0x7E;  // Up
        case 0x27: return 0x7C;  // Right
        case 0x28: return 0x7D;  // Down
        case 0x2E: return 0x75;  // Delete（forward delete）
        case 0x70: return 0x7A;  // F1
        case 0x71: return 0x78;  // F2
        case 0x72: return 0x63;  // F3
        case 0x73: return 0x76;  // F4
        case 0x74: return 0x60;  // F5
        case 0x75: return 0x61;  // F6
        case 0x76: return 0x62;  // F7
        case 0x77: return 0x64;  // F8
        case 0x78: return 0x65;  // F9
        case 0x79: return 0x6D;  // F10
        case 0x7A: return 0x67;  // F11
        case 0x7B: return 0x6F;  // F12
        default:   return -1;
    }
}

static void postVk(int cgKey, bool down) {
    if (cgKey < 0) return;
    CGEventRef e = CGEventCreateKeyboardEvent(nullptr, static_cast<CGKeyCode>(cgKey), down);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

// 特殊键/组合键（0x28）：mod 位图 1=Ctrl 2=Shift 4=Alt（及高位镜像），vk = Windows VK 码
bool injectSpecialKey(uint8_t mod, uint8_t vk) {
    const int target = vkToMacKey(vk);
    if (target < 0) return false;
    const int modKeys[3] = {0x3B, 0x38, 0x3A};   // Ctrl / Shift / Option（左侧）
    const uint8_t bits[3] = {0x01, 0x02, 0x04};
    for (int i = 0; i < 3; ++i) if (mod & bits[i]) postVk(modKeys[i], true);
    postVk(target, true);
    postVk(target, false);
    for (int i = 2; i >= 0; --i) if (mod & bits[i]) postVk(modKeys[i], false);
    return true;
}

std::string readSystemClipboard() {
    @autoreleasepool {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        NSString* s = [pb stringForType:NSPasteboardTypeString];
        if (!s) return {};
        const char* u = [s UTF8String];
        return u ? std::string(u) : std::string{};
    }
}

// 反向剪贴板（0x21）：写本机 NSPasteboard；写入后 600ms 内 watcher 不回传（防回环）
bool setSystemClipboard(const std::string& text) {
    @autoreleasepool {
        NSString* s = [NSString stringWithUTF8String:text.c_str()];
        if (!s) return false;
        g_clipSuppressMs.store(steadyMsNow());
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        return [pb setString:s forType:NSPasteboardTypeString] ? true : false;
    }
}

// 文本注入当前光标处：ASCII 逐键（CGEventKeyboardSetUnicodeString，不走布局表）；
// 非 ASCII 剪贴板 + Cmd+V 后还原（与 Windows 版同策略，避免逐键丢字）。
bool injectSystemText(const std::string& text) {
    if (text.empty()) return true;
    bool ascii = true;
    for (unsigned char c : text) { if (c >= 0x80) { ascii = false; break; } }
    @autoreleasepool {
        if (ascii) {
            for (unsigned char c : text) {
                if (c == '\n') { postVk(0x24, true); postVk(0x24, false); continue; }
                if (c == '\r') continue;
                if (c == '\t') { postVk(0x30, true); postVk(0x30, false); continue; }
                const UniChar u = c;
                // 注意签名：(event, UniCharCount 长度, const UniChar* 字符串) —— 长度在第 2 参
                CGEventRef e = CGEventCreateKeyboardEvent(nullptr, 0, true);
                CGEventKeyboardSetUnicodeString(e, 1, &u);
                CGEventPost(kCGHIDEventTap, e); CFRelease(e);
                e = CGEventCreateKeyboardEvent(nullptr, 0, false);
                CGEventKeyboardSetUnicodeString(e, 1, &u);
                CGEventPost(kCGHIDEventTap, e); CFRelease(e);
            }
            return true;
        }
        const std::string old = readSystemClipboard();
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        NSString* s = [NSString stringWithUTF8String:text.c_str()];
        if (!s) return false;
        g_clipSuppressMs.store(steadyMsNow());
        [pb clearContents];
        [pb setString:s forType:NSPasteboardTypeString];
        postVk(0x37, true);    // Cmd down
        postVk(0x09, true);    // V
        postVk(0x09, false);
        postVk(0x37, false);   // Cmd up
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        g_clipSuppressMs.store(steadyMsNow());
        NSString* o = [NSString stringWithUTF8String:old.c_str()];
        [pb clearContents];
        if (o) [pb setString:o forType:NSPasteboardTypeString];
        else   [pb setString:@"" forType:NSPasteboardTypeString];
        return true;
    }
}

}  // namespace apxpc::wireless

#endif  // __APPLE__
