// 相对位移换算 / 拖动类型判定（issue #47 第 6 条回归用例）。
//
// 这两件事此前只是 macOS 注入器里的一段内联算术，没有任何用例看着；
// 「delta 写进 CGEvent 但落点用老坐标」这种错在 Linux / Windows 上根本编不出来，
// 只能靠把换算抽出来 + 真机验 CGEvent 两层一起兜。这里盯住第一层。
#include "apxpc/wireless/relative_move.hpp"

#include <cstdio>

using apxpc::wireless::BoxD;
using apxpc::wireless::MoveKind;
using apxpc::wireless::PointD;
using apxpc::wireless::clampToBox;
using apxpc::wireless::moveKindForButtons;
using apxpc::wireless::relativeMoveTarget;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

static bool near(double a, double b) {
    const double d = a - b;
    return (d < 0 ? -d : d) < 1e-9;
}

static void testRelativeMove() {
    // 正增量、负增量、零增量
    PointD p = relativeMoveTarget(PointD{100.0, 200.0}, 5, -7);
    CHECK(near(p.x, 105.0));
    CHECK(near(p.y, 193.0));

    p = relativeMoveTarget(PointD{-30.0, 12.5}, -4, 0);
    CHECK(near(p.x, -34.0));
    CHECK(near(p.y, 12.5));

    p = relativeMoveTarget(PointD{0.0, 0.0}, 0, 0);
    CHECK(near(p.x, 0.0));
    CHECK(near(p.y, 0.0));

    // 关键回归：dx/dy 非零时**目标必须与当前不同**。
    // 旧实现把光标"移到它现在的位置"，这一条就是那个 bug 的最小断言。
    const PointD cur{640.0, 480.0};
    const PointD moved = relativeMoveTarget(cur, 3, 0);
    CHECK(moved.x != cur.x);

    // int8_t 边界（协议 0x01 帧的 dx/dy 是 int8）
    p = relativeMoveTarget(PointD{1000.0, 1000.0}, -128, 127);
    CHECK(near(p.x, 872.0));
    CHECK(near(p.y, 1127.0));
}

static void testClamp() {
    const BoxD box{0.0, 0.0, 1919.0, 1079.0};

    PointD p = clampToBox(PointD{500.0, 500.0}, box);
    CHECK(near(p.x, 500.0) && near(p.y, 500.0));   // 框内不动

    p = clampToBox(PointD{-40.0, -40.0}, box);
    CHECK(near(p.x, 0.0) && near(p.y, 0.0));

    p = clampToBox(PointD{5000.0, 5000.0}, box);
    CHECK(near(p.x, 1919.0) && near(p.y, 1079.0));

    // 副屏在主屏左侧时坐标可以是负的，只要框覆盖得到就原样保留
    const BoxD left{ -1600.0, 0.0, 1919.0, 1079.0 };
    p = clampToBox(PointD{-1200.0, 400.0}, left);
    CHECK(near(p.x, -1200.0) && near(p.y, 400.0));

    // 无效框（取不到显示器列表）不夹，别把光标吸到原点
    const BoxD bad{0.0, 0.0, 0.0, 0.0};
    p = clampToBox(PointD{1234.0, 567.0}, bad);
    CHECK(near(p.x, 1234.0) && near(p.y, 567.0));
}

static void testMoveKind() {
    CHECK(moveKindForButtons(0) == MoveKind::Move);
    CHECK(moveKindForButtons(0x01) == MoveKind::LeftDrag);     // 左键按住 → 左拖
    CHECK(moveKindForButtons(0x02) == MoveKind::RightDrag);
    CHECK(moveKindForButtons(0x04) == MoveKind::OtherDrag);
    // 同时按住多个键时按优先级取左 → 右 → 中（拖动只需一种事件类型）
    CHECK(moveKindForButtons(0x01 | 0x02) == MoveKind::LeftDrag);
    CHECK(moveKindForButtons(0x02 | 0x04) == MoveKind::RightDrag);
    // 高位（协议里 buttons 是 8 位，0x10 等）不得被误判成拖动
    CHECK(moveKindForButtons(0x10) == MoveKind::Move);
}

int main() {
    testRelativeMove();
    testClamp();
    testMoveKind();
    if (g_fail) {
        std::printf("relative_move_test: %d 项失败\n", g_fail);
        return 1;
    }
    std::printf("relative_move_test: OK\n");
    return 0;
}
