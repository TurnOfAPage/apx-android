#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（反馈#8 触摸板两条）：
  Q1 物理触摸板映射：PhysKeyBridge 增拦 dispatchGenericMotionEvent（hover 相对化 + 按键 + 滚轮），
     TouchpadModule 增 feedPointer() 直通 dispatch 三出口（USB 鼠标帧/蓝牙/无线同手势链路）。
  Q2 三指滑动：GestureEngine 三指分支改阶梯判定（≥60px 主轴明确 = 滑动，
     [12,60) 保留中键按住，<12 点按中键），onThreeSwipe 回调在外层转译为
     上=Alt+Tab、下=Win+D、左/右=Ctrl+Win+←/→（鼠标链路 Windows 看不见手指）。

两阶段：①按期望次数全量校验，失配一次性报全；②全过才写盘。失配 → 退出非零 → 不提交。
锚点字符串若以双引号收尾，三引号必须先换一行再闭合（run#5 教训）。
"""
import sys

EDITS = [
    # ————— GestureEngine：三指滑动 —————
    ("android/app/src/main/java/com/allperiph/touchpad/GestureEngine.kt", [
        ("""    MIDDLE,         // 三指按下 → 中键
}
""",
         """    MIDDLE,         // 三指按下 → 中键
    THREE_SWIPE,    // 三指滑动 → 组合键（上/下/左/右，由外层转译）
}
""", 1),
        ("    var onFeedback: ((GestureFeedback) -> Unit)? = null\n",
         """    var onFeedback: ((GestureFeedback) -> Unit)? = null

    /** 三指滑动方向回调（0上 1下 2左 3右）：鼠标/无线链路 Windows 看不见手指，由外层转译组合键 */
    var onThreeSwipe: ((Int) -> Unit)? = null
""", 1),
        ("""            3 -> {
                // (d) 三指**有位移**才发中键按下；静止三指留给「三指点按」，
                //     由 ACTION_POINTER_UP 的 3→2 抬起沿补发中键按下+抬起。
                if (pendingButtons and 0x04 == 0 && threeDownTime > 0L) {
                    val (cx, cy) = centroid(ev)
                    val moved3 = hypot((cx - threeDownX).toDouble(), (cy - threeDownY).toDouble())
                    if (moved3 >= TAP_SLOP) {
                        pendingButtons = pendingButtons or 0x04
                        sink.send(TouchpadFrame(0, 0, pendingButtons, tsNs = ev.eventTime * 1_000_000L))
                        onFeedback?.invoke(GestureFeedback.MIDDLE)
                        gestureConsumed = true
                    }
                }
            }
""",
         """            3 -> {
                // 三指阶梯判定（反馈#8：旧实现 moved≥12px 一律进中键，三指滑动全被吃掉）：
                //   ≥ SWIPE_MIN      → 三指滑动（单次，转译组合键；Windows 鼠标链路看不见手指）
                //   [TAP_SLOP, 60)   → 中键按住（原行为保留，中键拖拽仍可用）
                //   < TAP_SLOP       → 静止三指，留给「三指点按」由 3→2 抬起沿补发
                if (threeDownTime > 0L) {
                    val (cx, cy) = centroid(ev)
                    val dx3 = cx - threeDownX
                    val dy3 = cy - threeDownY
                    val dist = hypot(dx3.toDouble(), dy3.toDouble())
                    if (dist >= SWIPE_MIN) {
                        if (pendingButtons and 0x04 != 0) {
                            pendingButtons = pendingButtons and 0x04.inv()
                            sink.send(TouchpadFrame(0, 0, pendingButtons, tsNs = ev.eventTime * 1_000_000L))
                        }
                        val dir = if (Math.abs(dx3) >= Math.abs(dy3)) {
                            if (dx3 > 0f) 3 else 2
                        } else {
                            if (dy3 > 0f) 1 else 0
                        }
                        onThreeSwipe?.invoke(dir)
                        onFeedback?.invoke(GestureFeedback.THREE_SWIPE)
                        gestureConsumed = true
                        threeDownTime = 0L
                        com.allperiph.shared.util.Log.i("GestureEngine", "3-finger swipe dir=$dir")
                    } else if (dist >= TAP_SLOP && pendingButtons and 0x04 == 0 && !gestureConsumed) {
                        pendingButtons = pendingButtons or 0x04
                        sink.send(TouchpadFrame(0, 0, pendingButtons, tsNs = ev.eventTime * 1_000_000L))
                        onFeedback?.invoke(GestureFeedback.MIDDLE)
                        gestureConsumed = true
                    }
                }
            }
""", 1),
        ("        private const val TAP_SLOP = 12f\n",
         """        private const val TAP_SLOP = 12f
        /** 三指滑动起步阈值：与点按(12)/中键拖拽拉开档次，60px 主轴明确才触发 */
        private const val SWIPE_MIN = 60f
""", 1),
    ]),
    # ————— TouchpadModule：三指转译接线 + feedPointer —————
    ("android/app/src/main/java/com/allperiph/touchpad/TouchpadModule.kt", [
        ("import com.allperiph.core.Uplink\n",
         """import com.allperiph.core.Uplink
import com.allperiph.hid.HidKeys
""", 1),
        ("""        eng.onFeedback = { kind -> onGestureFeedback(kind) }
        engineRef.set(eng)
""",
         """        eng.onFeedback = { kind -> onGestureFeedback(kind) }
        // 反馈#8：三指滑动 → Windows 手势组合键（鼠标报告链路看不见手指，只能端上转译）
        eng.onThreeSwipe = { dir ->
            val combo = when (dir) {
                0 -> HidKeys.Combo("3指上", HidKeys.MOD_ALT, 0x2B)                       // Alt+Tab 切换应用
                1 -> HidKeys.Combo("3指下", HidKeys.MOD_WIN, 0x07)                       // Win+D 显示桌面
                2 -> HidKeys.Combo("3指左", HidKeys.MOD_CTRL or HidKeys.MOD_WIN, 0x50)   // Ctrl+Win+← 切桌面
                else -> HidKeys.Combo("3指右", HidKeys.MOD_CTRL or HidKeys.MOD_WIN, 0x4F)
            }
            HidKeys.combo(combo)
        }
        engineRef.set(eng)
""", 1),
        ("""    /**
     * Drag support：长按 550ms 锁定拖拽。
""",
         """    // ———— 反馈#8：物理触摸板指针直通（蓝牙触摸板 → 本机 → 电脑） ————
    private var ptrFracX = 0f
    private var ptrFracY = 0f
    private var ptrWheelAcc = 0f
    private var ptrPanAcc = 0f
    private var ptrButtons = 0

    /**
     * 物理触摸板（BT 指针设备）的相对位移/按键/滚轮直通：由 PhysKeyBridge
     * 在 dispatchGenericMotionEvent 里喂入，直接走 [dispatch] 三出口（与手势同链路）。
     * 不经 GestureEngine —— 指针是设备已归一化好的，端上不再做手势识别（除滚轮轴累加）。
     */
    fun feedPointer(dx: Float, dy: Float, buttons: Int, wheel: Float, pan: Float, tsNs: Long) {
        val ctx = ctxRef ?: return
        ptrFracX += dx; ptrFracY += dy
        ptrWheelAcc += wheel; ptrPanAcc += pan
        val idx = ptrFracX.toInt().coerceIn(-127, 127); ptrFracX -= idx
        val idy = ptrFracY.toInt().coerceIn(-127, 127); ptrFracY -= idy
        val w = ptrWheelAcc.toInt().coerceIn(-127, 127); ptrWheelAcc -= w
        val p = ptrPanAcc.toInt().coerceIn(-127, 127); ptrPanAcc -= p
        if (idx == 0 && idy == 0 && w == 0 && p == 0 && buttons == ptrButtons) return
        ptrButtons = buttons
        dispatch(ctx, TouchpadFrame(idx, idy, buttons, wheel = w, pan = p, tsNs = tsNs))
    }

    /**
     * Drag support：长按 550ms 锁定拖拽。
""", 1),
    ]),
    # ————— PhysKeyBridge：触摸板指针拦截 —————
    ("android/app/src/main/java/com/allperiph/ui/PhysKeyBridge.kt", [
        ("""    private inner class Wrapper(private val delegate: Window.Callback) : Window.Callback by delegate {
        override fun dispatchKeyEvent(event: KeyEvent): Boolean =
            if (forward(event)) true else delegate.dispatchKeyEvent(event)
    }
""",
         """    private inner class Wrapper(private val delegate: Window.Callback) : Window.Callback by delegate {
        override fun dispatchKeyEvent(event: KeyEvent): Boolean =
            if (forward(event)) true else delegate.dispatchKeyEvent(event)

        // 反馈#8：物理触摸板也桥接 —— hover=移动、click=按键、滚轮轴=滚动（Q1）
        override fun dispatchGenericMotionEvent(event: android.view.MotionEvent): Boolean =
            if (forwardPointer(event)) true else delegate.dispatchGenericMotionEvent(event)
    }
""", 1),
        ("    /** 出口就绪：判据统一走 Uplink.resolve（与 HidKeys.send 完全同口径） */",
         """    // ———— 反馈#8：物理触摸板指针状态 ————
    private var lastGX = Float.NaN
    private var lastGY = Float.NaN

    /**
     * Q1 触摸板映射：Android 把 BT 指针设备读成系统光标，窗口收到的是
     * **绝对 hover 坐标 + buttonState + 滚轮轴**；这里换算相对位移直通
     * [com.allperiph.touchpad.TouchpadModule.feedPointer]（三出口与手势同链路）。
     * 注：光标离开本窗口（分屏/悬浮球未开）时收不到 hover，回到窗口再续上，
     * 800px 跳变按「重入」处理不转发，避免甩屏误动。
     * @return true = 已消费（系统光标照常移动，App 内不再做本地 hover 响应）
     */
    private fun forwardPointer(event: android.view.MotionEvent): Boolean {
        val src = event.source
        val isPointer = src and android.view.InputDevice.SOURCE_MOUSE != 0 ||
            src and android.view.InputDevice.SOURCE_TOUCHPAD != 0
        if (!isPointer) return false
        if (act.currentFocus is EditText) return false   // 本地输入态：触摸板先伺候手机
        if (!ready()) return false                        // 没连电脑：就是普通触摸板/鼠标
        val mod = AgentController.module(ModuleId.TOUCHPAD) as? com.allperiph.touchpad.TouchpadModule
            ?: return false
        val x = event.x
        val y = event.y
        var dx = 0f
        var dy = 0f
        if (!lastGX.isNaN()) {
            dx = x - lastGX; dy = y - lastGY
            if (kotlin.math.abs(dx) > 800f || kotlin.math.abs(dy) > 800f) { dx = 0f; dy = 0f } // 重入不甩
        }
        lastGX = x; lastGY = y
        // buttonState 位与 TouchpadFrame 约定天然一致：primary=1 secondary=2 tertiary=4
        val btn = event.buttonState and 0x07
        val v = event.getAxisValue(android.view.MotionEvent.AXIS_VSCROLL)
        val h = event.getAxisValue(android.view.MotionEvent.AXIS_HSCROLL)
        val moving = dx != 0f || dy != 0f || v != 0f || h != 0f
        val clicked = event.actionMasked == android.view.MotionEvent.ACTION_DOWN ||
            event.actionMasked == android.view.MotionEvent.ACTION_UP
        if (!moving && !clicked) return true
        mod.feedPointer(dx, dy, btn, v, h, event.eventTime * 1_000_000L)
        return true
    }

    /** 出口就绪：判据统一走 Uplink.resolve（与 HidKeys.send 完全同口径） */""", 1),
    ]),
    # ————— CHANGELOG：Unreleased 补反馈#8 —————
    ("docs/CHANGELOG.md", [
        ("- **本文件回填 v212~v217**（反馈#6）；CI 新增常驻 `repo-edit` 大文件锚点编辑器\n",
         """- **本文件回填 v212~v217**（反馈#6）；CI 新增常驻 `repo-edit` 大文件锚点编辑器
- **物理触摸板指针映射（反馈#8 Q1）**：蓝牙触摸板 → 电脑，hover 相对化 + 按键 + 滚轮
  直通三出口（PhysKeyBridge 增拦 dispatchGenericMotionEvent，TouchpadModule 增 feedPointer）
- **三指滑动手势（反馈#8 Q2）**：GestureEngine 三指阶梯判定 —— ≥60px 主轴明确为滑动
  （上=Alt+Tab、下=Win+D、左右=Ctrl+Win+←/→），[12,60) 保留中键按住、点按仍为中键
""", 1),
    ]),
]

def main() -> int:
    loaded = {}
    errors = []
    for path, entries in EDITS:
        try:
            with open(path, encoding="utf-8") as f:
                text = f.read()
        except OSError as e:
            errors.append(f"{path}: open failed: {e}")
            continue
        loaded[path] = (text, entries)
        for i, (old, _new, expected) in enumerate(entries, 1):
            n = text.count(old)
            if n != expected:
                errors.append(
                    f"{path} edit#{i}: anchor count={n} (expect {expected})\n---anchor---\n{old[:400]}\n---"
                )
    if errors:
        print("VALIDATION FAILED：")
        for e in errors:
            print(e)
        return 1
    for path, (text, entries) in loaded.items():
        for old, new, _expected in entries:
            text = text.replace(old, new)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        print(f"OK {path}: {len(entries)} edit(s)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
