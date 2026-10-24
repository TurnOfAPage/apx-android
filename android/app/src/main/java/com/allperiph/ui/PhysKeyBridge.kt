package com.allperiph.ui

import android.app.Activity
import android.view.KeyEvent
import android.view.Window
import android.widget.EditText
import com.allperiph.bt.BtHidDevice
import com.allperiph.core.ModuleId
import com.allperiph.core.Uplink
import com.allperiph.hid.HidKeys
import com.allperiph.wireless.ControlTarget

/**
 * 物理键盘 → 电脑 转发桥（蓝牙键盘连手机的场景）。
 *
 * 拓扑：蓝牙键盘（外设）→ 手机 → [com.allperiph] 这套软件 → 电脑。
 * 此前全仓库没有任何 KeyEvent 捕获（dispatchKeyEvent / onKeyDown / IME 全零），
 * 物理按键根本进不了软件，这就是「蓝牙键盘在电脑上打不出字」的根因。
 *
 * 实现：包装 [Window.Callback]（Activity 本身就是原回调，所有其余方法 `by delegate`
 * 原样透传），在 [dispatchKeyEvent] 抢在视图树之前拦截 —— 不改 MainActivity
 * （与 TouchpadSplit 同一思路：旋屏不重建 + 96K 大文件改不动就从 Fragment 接线）。
 *
 * 转发规则（按序，任一条不满足就放行给手机本地）：
 *  ① 当前聚焦的是 EditText → 全部放行（手机本地能正常打字，弹窗是独立 window 天然隔离）
 *  ② [Uplink.resolve] 无可用出口 → 放行（没连电脑时蓝牙键盘就是普通手机键盘）
 *  ③ 修饰键（Shift/Ctrl/Alt/Win）→ 消费并自记 heldMods（位图随后续按键发）
 *  ④ 认不出的键（音量/媒体/Back/Home…）→ 放行，手机系统功能不受影响
 *  ⑤ 其余映射 USB HID usage → [HidKeys.combo]（与屏上快捷键同一条链路，
 *     60ms 自动释放；重复事件会续期释放定时器 = 电脑侧自然长按/自动重复）
 *
 * 出口判据不自写：一律 [Uplink.resolve]（与 HidKeys.send / 通知栏 / 界面同口径）。
 */
internal class PhysKeyBridge(private val act: Activity) {

    private var wrapper: Window.Callback? = null
    private var original: Window.Callback? = null

    /** 当前按住的修饰位（事件被我们消费后，靠 heldMods + metaState 双保险拼位图） */
    private var heldMods = 0

    fun attach(window: Window) {
        if (wrapper != null) return
        val orig = window.callback ?: return
        original = orig
        val w = Wrapper(orig)
        wrapper = w
        window.callback = w
    }

    /** 只在窗口回调仍是自己时还原，避免 Fragment 重建后旧桥拆掉新桥的包装 */
    fun detach() {
        val w = wrapper ?: return
        val win = act.window
        val orig = original
        if (win.callback === w && orig != null) win.callback = orig
        wrapper = null
        original = null
        heldMods = 0
    }

    private inner class Wrapper(private val delegate: Window.Callback) : Window.Callback by delegate {
        override fun dispatchKeyEvent(event: KeyEvent): Boolean =
            if (forward(event)) true else delegate.dispatchKeyEvent(event)
    }

    /** @return true = 已转发/已消费；false = 放行给手机本地 */
    private fun forward(event: KeyEvent): Boolean {
        val action = event.action
        if (action != KeyEvent.ACTION_DOWN && action != KeyEvent.ACTION_UP) return false

        // ① 手机本地输入中：修饰键也放行（否则本地 Shift+方向选字会坏）
        if (act.currentFocus is EditText) return false
        // ② 没连电脑：键盘继续当手机键盘用
        if (!ready()) return false

        // ③ 修饰键记账（不发单独帧：协议里 mod 必须跟着按键走，空 keys 即全释放）
        val modBit = when (event.keyCode) {
            KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT -> HidKeys.MOD_SHIFT
            KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.KEYCODE_CTRL_RIGHT -> HidKeys.MOD_CTRL
            KeyEvent.KEYCODE_ALT_LEFT, KeyEvent.KEYCODE_ALT_RIGHT -> HidKeys.MOD_ALT
            KeyEvent.KEYCODE_META_LEFT, KeyEvent.KEYCODE_META_RIGHT -> HidKeys.MOD_WIN
            else -> 0
        }
        if (modBit != 0) {
            heldMods = if (action == KeyEvent.ACTION_DOWN) heldMods or modBit else heldMods and modBit.inv()
            return true
        }

        // ④ 认不出的键放行（音量/媒体/Back/Home…留给手机）
        val usage = usageOf(event.keyCode)
        if (usage == 0) return false

        // ⑤ 发送：与屏上快捷键同链路（60ms 自动释放；重复事件续期 = 电脑侧自然长按）
        if (action == KeyEvent.ACTION_DOWN) {
            HidKeys.combo(HidKeys.Combo("", heldMods or metaMod(event.metaState), usage))
        }
        return true
    }

    /** 出口就绪：判据统一走 Uplink.resolve（与 HidKeys.send 完全同口径） */
    private fun ready(): Boolean {
        val rt = AgentController.runtime
        val bt = AgentController.module(ModuleId.BTHID) as? BtHidDevice
        return Uplink.resolve(
            rt?.hid,
            btConnected = bt?.isConnected == true,
            wireless = ControlTarget.isControlling() && ControlTarget.controlClient != null,
        ) != Uplink.NONE
    }

    private fun metaMod(meta: Int): Int {
        var m = 0
        if (meta and KeyEvent.META_CTRL_ON != 0) m = m or HidKeys.MOD_CTRL
        if (meta and KeyEvent.META_SHIFT_ON != 0) m = m or HidKeys.MOD_SHIFT
        if (meta and KeyEvent.META_ALT_ON != 0) m = m or HidKeys.MOD_ALT
        if (meta and KeyEvent.META_META_ON != 0) m = m or HidKeys.MOD_WIN
        return m
    }

    /** Android keyCode → USB HID Keyboard Page (0x07) usage；认不出返回 0（放行本地） */
    private fun usageOf(kc: Int): Int = when (kc) {
        in KeyEvent.KEYCODE_A..KeyEvent.KEYCODE_Z -> 0x04 + (kc - KeyEvent.KEYCODE_A)
        KeyEvent.KEYCODE_0 -> 0x27
        in KeyEvent.KEYCODE_1..KeyEvent.KEYCODE_9 -> 0x1E + (kc - KeyEvent.KEYCODE_1)
        KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> 0x28
        KeyEvent.KEYCODE_ESCAPE -> 0x29
        KeyEvent.KEYCODE_DEL -> 0x2A                        // 退格
        KeyEvent.KEYCODE_TAB -> 0x2B
        KeyEvent.KEYCODE_SPACE -> 0x2C
        KeyEvent.KEYCODE_MINUS -> 0x2D
        KeyEvent.KEYCODE_EQUALS -> 0x2E
        KeyEvent.KEYCODE_LEFT_BRACKET -> 0x2F
        KeyEvent.KEYCODE_RIGHT_BRACKET -> 0x30
        KeyEvent.KEYCODE_BACKSLASH -> 0x31
        KeyEvent.KEYCODE_SEMICOLON -> 0x33
        KeyEvent.KEYCODE_APOSTROPHE -> 0x34
        KeyEvent.KEYCODE_GRAVE -> 0x35
        KeyEvent.KEYCODE_COMMA -> 0x36
        KeyEvent.KEYCODE_PERIOD -> 0x37
        KeyEvent.KEYCODE_SLASH -> 0x38
        KeyEvent.KEYCODE_CAPS_LOCK -> 0x39
        in KeyEvent.KEYCODE_F1..KeyEvent.KEYCODE_F12 -> 0x3A + (kc - KeyEvent.KEYCODE_F1)
        KeyEvent.KEYCODE_SYSRQ -> 0x46                      // PrtSc
        KeyEvent.KEYCODE_SCROLL_LOCK -> 0x47
        KeyEvent.KEYCODE_BREAK -> 0x48
        KeyEvent.KEYCODE_INSERT -> 0x49
        KeyEvent.KEYCODE_MOVE_HOME -> 0x4A
        KeyEvent.KEYCODE_PAGE_UP -> 0x4B
        KeyEvent.KEYCODE_FORWARD_DEL -> 0x4C                // Del
        KeyEvent.KEYCODE_MOVE_END -> 0x4D
        KeyEvent.KEYCODE_PAGE_DOWN -> 0x4E
        KeyEvent.KEYCODE_DPAD_RIGHT -> 0x4F
        KeyEvent.KEYCODE_DPAD_LEFT -> 0x50
        KeyEvent.KEYCODE_DPAD_DOWN -> 0x51
        KeyEvent.KEYCODE_DPAD_UP -> 0x52
        KeyEvent.KEYCODE_DPAD_CENTER -> 0x28                // 方向确认 = Enter
        KeyEvent.KEYCODE_NUM_LOCK -> 0x53
        KeyEvent.KEYCODE_NUMPAD_DIVIDE -> 0x54
        KeyEvent.KEYCODE_NUMPAD_MULTIPLY -> 0x55
        KeyEvent.KEYCODE_NUMPAD_SUBTRACT -> 0x56
        KeyEvent.KEYCODE_NUMPAD_ADD -> 0x57
        in KeyEvent.KEYCODE_NUMPAD_1..KeyEvent.KEYCODE_NUMPAD_9 -> 0x59 + (kc - KeyEvent.KEYCODE_NUMPAD_1)
        KeyEvent.KEYCODE_NUMPAD_0 -> 0x62
        KeyEvent.KEYCODE_NUMPAD_DOT -> 0x63
        else -> 0
    }
}