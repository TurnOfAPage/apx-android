package com.allperiph.touchpad

import android.app.Activity
import android.app.AlertDialog
import android.content.ClipboardManager
import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Button
import android.widget.Toast
import android.widget.ViewFlipper
import com.allperiph.R
import com.allperiph.core.ModuleId
import com.allperiph.shared.net.FileSender
import com.allperiph.hid.HidKeys
import com.allperiph.hid.HotkeyStore
import com.allperiph.ui.AgentController
import com.allperiph.ui.HotkeyController

import com.allperiph.controlled.ControlledService
import com.allperiph.wireless.ControlTarget
import com.allperiph.wireless.TvControllerClient
import com.allperiph.wireless.TvDiscovery
import com.allperiph.wireless.WirelessModule
import kotlin.math.abs

class TouchpadActivity : Activity() {
    private val cBg get() = getColor(R.color.miuix_bg)
    private val cCard get() = getColor(R.color.miuix_card)
    private val cText get() = getColor(R.color.miuix_text)
    private val cText2 get() = getColor(R.color.miuix_text_secondary)
    private val cText3 get() = getColor(R.color.miuix_text_tertiary)
    private val cDivider get() = getColor(R.color.miuix_divider)
    private val cStroke get() = getColor(R.color.miuix_stroke)
    private val kbSkinPicked get() = com.allperiph.ui.ThemeSkin.picked(this, com.allperiph.ui.ThemeSkin.KEYBOARD).isNotBlank()
    private val cAccent get() = if (kbSkinPicked) com.allperiph.ui.ThemeSkin.current(this, com.allperiph.ui.ThemeSkin.KEYBOARD).accent else getColor(R.color.miuix_accent)
    private val cAccentSoft get() = if (kbSkinPicked) com.allperiph.ui.ThemeSkin.current(this, com.allperiph.ui.ThemeSkin.KEYBOARD).accentSoft else getColor(R.color.miuix_accent_soft)
    private val dens get() = com.allperiph.ui.KeyPref.density(this)
    private val cDangerSoft get() = getColor(R.color.md_error_container)
    private val cDanger get() = getColor(R.color.miuix_danger)
    private val cTabIdle get() = getColor(R.color.state_idle)
    private class Page(val title: String, val sub: String, val icon: Int)
    private class Tab(val icon: ImageView, val label: TextView)
    private val pages by lazy {
        listOf(
            Page(getString(R.string.ui_main_item_touchpad), getString(R.string.ui_touchpad_label_hotkey_edit_longpress), R.drawable.ic_apx_touchpad),
            Page(getString(R.string.ui_touchpad_label_keyboard), getString(R.string.ui_touchpad_label_remote), R.drawable.ic_apx_keyboard),
            Page(getString(R.string.notify_action_screen), getString(R.string.ui_touchpad_label_screen), R.drawable.ic_apx_power),
        )
    }
    private val kbNames = listOf("快捷", "遥控", "游戏", "数字", "九宫格", "方向", "F 区", "自定义")
    private var titleView: TextView? = null
    private var subView: TextView? = null
    private var statusView: TextView? = null
    private var deviceChip: TextView? = null
    private var fileBtn: TextView? = null
    private var clipBtn: TextView? = null
    private var controlledChip: TextView? = null
    private var displayStatusView: TextView? = null
    private lateinit var flipper: ViewFlipper
    private val REQ_PICK_FILE = 9001
    private var tabs: List<Tab> = emptyList()
    private var chipRow: LinearLayout? = null
    private lateinit var kbPager: ViewFlipper
    private var kbTabs: List<TextView> = emptyList()
    private lateinit var hotkeyBoard: com.allperiph.ui.HotkeyBoard
    private val modBtns = LinkedHashMap<Int, MutableList<TextView>>()
    private val handler = Handler(Looper.getMainLooper())
    private var hidReady = false
    private var frameCount = 0L
    private var lastShown = 0L
    private val dragTask = Runnable { (AgentController.module(ModuleId.TOUCHPAD) as? TouchpadModule)?.armDrag() }
    private val rightDragTask = Runnable { (AgentController.module(ModuleId.TOUCHPAD) as? TouchpadModule)?.armRightDrag() }
    private var volRepeat: Runnable? = null
    private var edgeStartX = -1f
    private var edgeSwiped = false

    override fun attachBaseContext(newBase: Context) { super.attachBaseContext(com.allperiph.ui.ThemePref.wrap(newBase)) }
    override fun onDestroy() { ControlTarget.controlClient?.onReverseClipboard = null; super.onDestroy() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(android.view.WindowManager.LayoutParams.FLAG_SECURE)
        hidReady = AgentController.runtime?.hid?.isReady() == true
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setBackgroundColor(cBg) }
        com.allperiph.ui.Backdrop.apply(root, cBg)
        root.addView(buildHeader())
        flipper = ViewFlipper(this).apply { addView(touchpadPage()); addView(keyboardPage()); addView(displayPage()) }
        root.addView(flipper, LinearLayout.LayoutParams(-1, 0, 1f))
        root.addView(buildTabBar())
        setContentView(root, ViewGroup.LayoutParams(-1, -1))
        refreshTabs(); updateControlledChip()
        showPage(intent?.getIntExtra(EXTRA_PAGE, PAGE_TOUCHPAD) ?: PAGE_TOUCHPAD)
        TvDiscovery.start()
    }

    override fun onNewIntent(intent: Intent?) { super.onNewIntent(intent); showPage(intent?.getIntExtra(EXTRA_PAGE, PAGE_TOUCHPAD) ?: PAGE_TOUCHPAD) }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQ_PICK_FILE && resultCode == RESULT_OK) { data?.data?.let { sendFile(it) } }
    }

    private fun buildHeader(): View {
        val wrap = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(20), dp(18), dp(20), dp(12)) }
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        val tv = TextView(this).apply { text = pages[0].title; setTextColor(cText); textSize = 24f; typeface = Typeface.DEFAULT_BOLD }
        val st = TextView(this).apply {
            text = if (hidReady) getString(R.string.ui_touchpad_text_no_frames) else getString(R.string.ui_touchpad_text_not)
            setTextColor(if (hidReady) cAccent else cDanger); textSize = 11f; gravity = Gravity.CENTER
            background = pill(if (hidReady) cAccentSoft else cDangerSoft); setPadding(dp(10), dp(6), dp(10), dp(6))
        }
        val exit = TextView(this).apply {
            text = getString(R.string.ui_common_exit); setTextColor(cText2); textSize = 13f; gravity = Gravity.CENTER
            background = pill(cCard); setPadding(dp(14), dp(6), dp(14), dp(6)); isClickable = true; isFocusable = true
            setOnClickListener { finish() }
        }
        row.addView(tv)
        val chipsRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        val chipsScroll = android.widget.HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false; addView(chipsRow) }
        row.addView(chipsScroll, LinearLayout.LayoutParams(0, -2, 1f))
        chipsRow.addView(st)
        val ctl = TextView(this).apply {
            text = getString(R.string.ui_touchpad_text_controlled); setTextColor(cText2); textSize = 12f; gravity = Gravity.CENTER
            background = pill(cCard); setPadding(dp(12), dp(6), dp(12), dp(6)); isClickable = true; isFocusable = true
            setOnClickListener { toggleControlled() }; setOnLongClickListener { showInjectCaps(); true }
        }
        controlledChip = ctl; chipsRow.addView(ctl, LinearLayout.LayoutParams(-2, -2).apply { leftMargin = dp(8) })
        val dev = TextView(this).apply {
            text = ControlTarget.label; setTextColor(cText2); textSize = 12f; gravity = Gravity.CENTER
            background = pill(cCard); setPadding(dp(12), dp(6), dp(12), dp(6)); isClickable = true; isFocusable = true
            setOnClickListener { showDevicePicker() }
        }
        deviceChip = dev; chipsRow.addView(dev, LinearLayout.LayoutParams(-2, -2).apply { leftMargin = dp(8) })
        val remote = TextView(this).apply {
            text = getString(R.string.ui_touchpad_text_remote); setTextColor(cAccent); textSize = 12f; gravity = Gravity.CENTER
            background = pill(cCard); setPadding(dp(12), dp(6), dp(12), dp(6)); isClickable = true; isFocusable = true
            setOnClickListener { startActivity(android.content.Intent(this@TouchpadActivity, com.allperiph.ui.RemoteActivity::class.java)) }
        }
        val file = TextView(this).apply {
            text = getString(R.string.ui_touchpad_text_file); setTextColor(cText2); textSize = 12f; gravity = Gravity.CENTER
            background = pill(cCard); setPadding(dp(12), dp(6), dp(12), dp(6)); isClickable = true; isFocusable = true
            setOnClickListener { val intent = Intent(Intent.ACTION_GET_CONTENT).apply { type = "*/*"; addCategory(Intent.CATEGORY_OPENABLE) }; startActivityForResult(intent, REQ_PICK_FILE) }
        }
        val clip = TextView(this).apply {
            text = getString(R.string.ui_touchpad_text_clipboard); setTextColor(cText2); textSize = 12f; gravity = Gravity.CENTER
            background = pill(cCard); setPadding(dp(12), dp(6), dp(12), dp(6)); isClickable = true; isFocusable = true
            setOnClickListener { sendClipboard() }
        }
        fileBtn = file; clipBtn = clip
        chipsRow.addView(remote, LinearLayout.LayoutParams(-2, -2).apply { leftMargin = dp(8) })
        chipsRow.addView(file, LinearLayout.LayoutParams(-2, -2).apply { leftMargin = dp(8) })
        chipsRow.addView(clip, LinearLayout.LayoutParams(-2, -2).apply { leftMargin = dp(8) })
        chipsRow.addView(exit, LinearLayout.LayoutParams(-2, -2).apply { leftMargin = dp(8) })
        wrap.addView(row)
        val sub = TextView(this).apply { text = pages[0].sub; setTextColor(cText2); textSize = 13f }
        wrap.addView(sub, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(6) })
        titleView = tv; statusView = st; subView = sub
        return wrap
    }

    private fun touchpadPage(): View {
        val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(16), 0, dp(16), dp(10)) }
        val area = FrameLayout(this).apply { background = card(cCard, dp(22)); isClickable = false; isFocusable = false }
        area.addView(TextView(this).apply { text = getString(R.string.ui_touchpad_text_cursor_swipe); setTextColor(cText3); textSize = 13f; gravity = Gravity.CENTER; isClickable = false }, FrameLayout.LayoutParams(-1, -2, Gravity.CENTER))
        col.addView(area, LinearLayout.LayoutParams(-1, 0, 1f))
        val head = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; setPadding(dp(4), dp(14), dp(4), dp(8)) }
        head.addView(TextView(this).apply { text = getString(R.string.ui_touchpad_text_hotkey); setTextColor(cText); textSize = 14f; typeface = Typeface.DEFAULT_BOLD }, LinearLayout.LayoutParams(0, -2, 1f))
        head.addView(TextView(this).apply { text = getString(R.string.ui_common_edit); setTextColor(cAccent); textSize = 12f; isClickable = true; isFocusable = true; setPadding(dp(10), dp(4), dp(4), dp(4)); setOnClickListener { hotkeyBoard.managerDialog() } })
        col.addView(head)
        val chips = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        chipRow = chips; col.addView(chips, LinearLayout.LayoutParams(-1, -2))
        hotkeyBoard = com.allperiph.ui.HotkeyBoard(this, chips, { boardStyle() }); hotkeyBoard.render()
        return col
    }

    private fun boardStyle(): com.allperiph.ui.HotkeyBoard.Style = com.allperiph.ui.HotkeyBoard.Style(chipText = cText, chipBg = cCard, chipStroke = cStroke, chipRadiusDp = 14, padVDp = 13, perRow = 4, accent = cAccent, textPrimary = cText, textSecondary = cText2)

    private fun keyboardPage(): View {
        val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(10), 0, dp(10), dp(12)) }
        val tabRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(dp(6), dp(2), dp(6), dp(10)) }
        kbTabs = kbNames.mapIndexed { i, name ->
            val t = TextView(this).apply {
                text = name; textSize = 13f; gravity = Gravity.CENTER; setPadding(0, dp(9), 0, dp(9))
                isClickable = true; isFocusable = true; setOnClickListener { showKeyboard(i) }
                if (name == "自定义") { setOnLongClickListener { com.allperiph.ui.CustomKeyboardDialog.show(this@TouchpadActivity) { val idx = kbNames.indexOf("自定义"); if (idx in 0 until kbNames.size) showKeyboard(idx) }; true } }
            }
            tabRow.addView(t, LinearLayout.LayoutParams(dp(76), -2).apply { setMargins(dp(3), 0, dp(3), 0) }); t
        }
        col.addView(android.widget.HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false; addView(tabRow) }, LinearLayout.LayoutParams(-1, -2))
        kbPager = ViewFlipper(this).apply { addView(quickKeyboard()); addView(remoteKeyboard()); addView(gameKeyboard()); addView(numpadKeyboard()); addView(grid9Keyboard()); addView(arrowKeyboard()); addView(fnKeyboard()); addView(customKeyboard()) }
        col.addView(kbPager, LinearLayout.LayoutParams(-1, 0, 1f)); showKeyboard(0)
        return col
    }

    private fun showKeyboard(i: Int) {
        if (!::kbPager.isInitialized || i !in kbNames.indices) return
        kbPager.displayedChild = i
        kbTabs.forEachIndexed { k, t -> val on = k == i; t.background = pill(if (on) cAccent else cAccentSoft); t.setTextColor(if (on) cCard else cAccent); t.typeface = if (on) Typeface.DEFAULT_BOLD else Typeface.DEFAULT }
    }

    private fun quickKeyboard(): View {
        val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val modRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        HidKeys.MOD_LABELS.forEach { (text, bit) -> val b = keyCap(text, true) { HidKeys.toggleSticky(bit); refreshMods() }; modBtns.getOrPut(bit) { mutableListOf() }.add(b); modRow.addView(b, LinearLayout.LayoutParams(0, -1, 1f).apply { setMargins(dp(2), dp(2), dp(2), dp(6)) }) }
        col.addView(modRow, LinearLayout.LayoutParams(-1, 0, 1f))
        com.allperiph.ui.KeyPref.letterRows(this).forEach { labels ->
            val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }; val g = dp(dens.gapDp)
            labels.forEach { text -> val b = keyCap(text, false) { HidKeys.tap(HidKeys.usageByLabel(text)); refreshMods() }; row.addView(b, LinearLayout.LayoutParams(0, -1, 1f).apply { setMargins(g, g, g, g) }) }
            col.addView(row, LinearLayout.LayoutParams(-1, 0, 1f))
        }
        return col
    }

    private fun remoteKeyboard(): View {
        val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER_VERTICAL }
        val rows = listOf(
            listOf(Triple(getString(R.string.ui_touchpad_media_prev), R.drawable.ic_apx_prev, HotkeyController.BIT_PREV_TRACK), Triple(getString(R.string.ui_touchpad_label_play_pause), R.drawable.ic_apx_play, HotkeyController.BIT_PLAY_PAUSE), Triple(getString(R.string.ui_touchpad_media_next), R.drawable.ic_apx_next, HotkeyController.BIT_NEXT_TRACK)),
            listOf(Triple(getString(R.string.ui_touchpad_label_volume), R.drawable.ic_apx_vol_down, HotkeyController.BIT_VOLUME_DOWN), Triple(getString(R.string.ui_touchpad_media_mute), R.drawable.ic_apx_mute, HotkeyController.BIT_MUTE), Triple(getString(R.string.ui_touchpad_label_volume_2), R.drawable.ic_apx_vol_up, HotkeyController.BIT_VOLUME_UP)),
        )
        rows.forEach { defs -> val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }; defs.forEach { (text, icon, bit) -> row.addView(mediaTile(text, icon, bit), LinearLayout.LayoutParams(0, dp(110), 1f).apply { setMargins(dp(6), dp(6), dp(6), dp(6)) }) }; col.addView(row, LinearLayout.LayoutParams(-1, -2)) }
        val pr = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        fun tileLp() = LinearLayout.LayoutParams(0, dp(110), 1f).apply { setMargins(dp(6), dp(6), dp(6), dp(6)) }
        pr.addView(mediaTile(getString(R.string.ui_touchpad_label_power), R.drawable.ic_apx_power, HotkeyController.BIT_POWER), tileLp())
        pr.addView(actionTile(getString(R.string.ui_touchpad_label_poweroff), 0xFFD23F31.toInt()) { confirmPower(0, getString(R.string.ui_touchpad_label_poweroff)) }, tileLp())
        pr.addView(actionTile(getString(R.string.ui_touchpad_label_reboot), 0xFFF0A020.toInt()) { confirmPower(1, getString(R.string.ui_touchpad_label_reboot)) }, tileLp())
        col.addView(pr, LinearLayout.LayoutParams(-1, -2))
        return col
    }

    private fun actionTile(text: String, accent: Int, onClick: () -> Unit): View = TextView(this).apply { this.text = text; setTextColor(accent); typeface = android.graphics.Typeface.DEFAULT_BOLD; textSize = 16f; gravity = Gravity.CENTER; isClickable = true; isFocusable = true; background = card(cCard, dp(18)); setOnClickListener { com.allperiph.ui.Feedback.tap(this); onClick() } }

    private fun confirmPower(action: Int, label: String) {
        val c = com.allperiph.wireless.ControlTarget.controlClient
        if (c == null) { Toast.makeText(this, getString(R.string.ui_touchpad_toast_device_connect_not), Toast.LENGTH_SHORT).show(); return }
        AlertDialog.Builder(this, R.style.Theme_AllPeriph_Miuix_Dialog).setTitle(getString(R.string.ui_touchpad_dialog_device, label)).setMessage(getString(R.string.ui_touchpad_dialog_no_only_need, label)).setPositiveButton(label) { _, _ -> c.power(action); Toast.makeText(this, getString(R.string.ui_touchpad_toast_send_done, label), Toast.LENGTH_SHORT).show() }.setNegativeButton(getString(R.string.ui_common_cancel), null).show()
    }

    private fun showInjectCaps() {
        val inj = com.allperiph.shared.inject.TvInjector
        val lines = inj.capabilities().joinToString("\n") { (name, ok, how) -> if (ok) "✓  $name" else "✗  $name\n      → $how" }
        AlertDialog.Builder(this, R.style.Theme_AllPeriph_Miuix_Dialog).setTitle(getString(R.string.ui_touchpad_dialog_selftest_channel_inject)).setMessage(getString(R.string.ui_touchpad_dialog_control_state, lines, inj.channelText())).setPositiveButton(getString(R.string.ui_common_close), null).show()
    }

    private fun mediaTile(text: String, icon: Int, bit: Int): View {
        val tile = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER; background = card(cCard, dp(18)); isClickable = true; isFocusable = true }
        tile.addView(ImageView(this).apply { setImageResource(icon); scaleType = ImageView.ScaleType.FIT_CENTER; setColorFilter(cAccent) }, LinearLayout.LayoutParams(dp(30), dp(30)))
        tile.addView(TextView(this).apply { this.text = text; setTextColor(cText2); textSize = 12f; gravity = Gravity.CENTER }, LinearLayout.LayoutParams(-2, -2).apply { topMargin = dp(10) })
        if (bit == HotkeyController.BIT_VOLUME_UP || bit == HotkeyController.BIT_VOLUME_DOWN) {
            tile.setOnTouchListener { v, ev ->
                when (ev.actionMasked) {
                    MotionEvent.ACTION_DOWN -> { v.background = card(cAccentSoft, dp(18)); com.allperiph.ui.Feedback.tap(v); rippleOn(v, ev.x, ev.y); mediaTap(bit); val task = object : Runnable { override fun run() { com.allperiph.ui.Feedback.haptic(v); mediaTap(bit); handler.postDelayed(this, 250) } }; volRepeat = task; handler.postDelayed(task, 400) }
                    MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> { v.background = card(cCard, dp(18)); volRepeat?.let { handler.removeCallbacks(it) }; volRepeat = null; HotkeyController.release(bit) }
                }; true
            }
        } else { tile.setOnClickListener { com.allperiph.ui.Feedback.tap(it); rippleOn(it, tile.width / 2f, tile.height / 2f); mediaTap(bit) } }
        return tile
    }

    private fun mediaTap(bit: Int) { HotkeyController.press(bit); handler.postDelayed({ HotkeyController.release(bit) }, 60) }

    private fun gameKeyboard(): View {
        val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val layout = listOf(listOf("Esc", "1", "2", "3", "4", "Tab"), listOf("Q", "W", "E", "R", "F", "B"), listOf("A", "S", "D", "Shift", "Space", "Ctrl"), listOf("↑", "↓", "←", "→", "Enter", "Del"))
        layout.forEach { labels ->
            val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
            labels.forEach { text -> val bit = HidKeys.MOD_LABELS.firstOrNull { it.first == text }?.second; val b = keyCap(text, bit != null) { if (bit != null) HidKeys.toggleSticky(bit) else HidKeys.tap(HidKeys.usageByLabel(text)); refreshMods() }; if (bit != null) modBtns.getOrPut(bit) { mutableListOf() }.add(b); row.addView(b, LinearLayout.LayoutParams(0, -1, 1f).apply { setMargins(dp(2), dp(2), dp(2), dp(2)) }) }
            col.addView(row, LinearLayout.LayoutParams(-1, 0, 1f))
        }
        return col
    }

    private fun keyCap(text: String, modifier: Boolean, onTap: () -> Unit): TextView = TextView(this).apply {
        this.text = text; textSize = dens.textSize; gravity = Gravity.CENTER
        setTextColor(if (modifier) cAccent else cText)
        val corner = dp(dens.cornerDp)
        val restBg = { if (modifier) card(cAccentSoft, corner) else strokeCard(cCard, corner, cStroke) }
        background = restBg(); isClickable = true; isFocusable = true
        setOnClickListener { onTap() }
        setOnTouchListener { v, e ->
            when (e.actionMasked) {
                MotionEvent.ACTION_DOWN -> { ExpressiveFeedback.press(v); v.background = card(cAccentSoft, corner); com.allperiph.ui.Feedback.tap(v); rippleOn(v, e.x, e.y) }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> { ExpressiveFeedback.release(v); v.background = restBg() }
            }; false
        }
    }

    private fun keyCap(text: String, usage: Int): TextView = keyCap(text, false) { HidKeys.tap(usage) }

    private fun customKeyboard(): View {
        val rows = com.allperiph.ui.KeyPref.customRows(this)
        if (rows.isNullOrEmpty()) { return TextView(this).apply { text = getString(R.string.ui_touchpad_text_edit_longpress); gravity = Gravity.CENTER; setTextColor(cText2); textSize = 14f } }
        val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }; val g = dp(dens.gapDp)
        rows.forEach { labels -> val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }; labels.forEach { text -> val b = keyCap(text, false) { HidKeys.tap(HidKeys.usageByLabel(text)); refreshMods() }; row.addView(b, LinearLayout.LayoutParams(0, -1, 1f).apply { setMargins(g, g, g, g) }) }; col.addView(row, LinearLayout.LayoutParams(-1, 0, 1f)) }
        return col
    }

    private fun rippleOn(v: View, x: Float, y: Float) {
        if (v.width <= 0 || v.height <= 0) return
        val dur = com.allperiph.ui.ThemeSkin.motionMs(this, 620L); if (dur <= 0L) return
        val ripple = com.allperiph.ui.GlassRipple(x, y, getColor(R.color.nav_pill_shine), cAccent, maxOf(v.width, v.height) * 1.05f, dur).apply { onFinish = { if (v.foreground === this) v.foreground = null } }
        v.foreground = ripple; ripple.play()
    }

    private fun refreshMods() { modBtns.forEach { (bit, list) -> val on = HidKeys.isSticky(bit); list.forEach { b -> b.background = card(if (on) cAccent else cAccentSoft, dp(9)); b.setTextColor(if (on) cCard else cAccent) } } }

    private fun keyGrid(rows: List<List<com.allperiph.ui.KeyLayouts.Key>>): View {
        val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        rows.forEach { row -> val r = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }; row.forEach { k -> r.addView(keyCap(k.label, k.usage), LinearLayout.LayoutParams(0, -1, k.weight).apply { setMargins(dp(3), dp(3), dp(3), dp(3)) }) }; col.addView(r, LinearLayout.LayoutParams(-1, 0, 1f)) }
        return col
    }

    private fun numpadKeyboard() = keyGrid(com.allperiph.ui.KeyLayouts.NUMPAD)
    private fun grid9Keyboard() = keyGrid(com.allperiph.ui.KeyLayouts.GRID9)
    private fun arrowKeyboard() = keyGrid(com.allperiph.ui.KeyLayouts.ARROWS)
    private fun fnKeyboard() = keyGrid(com.allperiph.ui.KeyLayouts.FN)

    private fun buildTabBar(): View {
        val wrap = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setBackgroundColor(cCard) }
        wrap.addView(View(this).apply { setBackgroundColor(cDivider) }, LinearLayout.LayoutParams(-1, dp(1)))
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        tabs = pages.mapIndexed { i, p -> val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER; setPadding(0, dp(9), 0, dp(9)); isClickable = true; isFocusable = true; setOnClickListener { showPage(i) } }; val icon = ImageView(this).apply { setImageResource(p.icon); scaleType = ImageView.ScaleType.FIT_CENTER }; val labelView = TextView(this).apply { text = p.title; textSize = 10f; gravity = Gravity.CENTER }; col.addView(icon, LinearLayout.LayoutParams(dp(24), dp(24))); col.addView(labelView, LinearLayout.LayoutParams(-2, -2).apply { topMargin = dp(4) }); row.addView(col, LinearLayout.LayoutParams(0, -2, 1f)); Tab(icon, labelView) }
        wrap.addView(row, LinearLayout.LayoutParams(-1, -2)); return wrap
    }

    private fun showPage(i: Int) {
        if (i !in pages.indices) return; handler.removeCallbacks(dragTask); handler.removeCallbacks(rightDragTask)
        flipper.displayedChild = i; titleView?.text = pages[i].title; subView?.text = pages[i].sub
        if (i == PAGE_TOUCHPAD) hotkeyBoard.render(); if (i == PAGE_DISPLAY) updateDisplayStatus(); refreshTabs()
    }

    private fun displayPage(): View {
        val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(16), dp(12), dp(16), dp(10)) }
        val card = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setBackgroundResource(R.drawable.bg_card); setPadding(dp(18), dp(18), dp(18), dp(18)) }
        card.addView(TextView(this).apply { text = getString(R.string.ui_touchpad_text_screen); setTextColor(cText); textSize = 16f; typeface = Typeface.DEFAULT_BOLD })
        val status = TextView(this).apply { text = WirelessModule.statusText(); setTextColor(cText2); textSize = 13f; setPadding(0, dp(8), 0, dp(8)) }
        displayStatusView = status; card.addView(status)
        card.addView(TextView(this).apply { text = getString(R.string.ui_touchpad_text_screen_recv_fullscreen_local); setTextColor(cText2); textSize = 12f })
        col.addView(card, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(12) })
        col.addView(TextView(this).apply { text = getString(R.string.ui_touchpad_text_device); setTextColor(cAccent); textSize = 13f; typeface = Typeface.DEFAULT_BOLD; letterSpacing = 0.06f; setPadding(dp(4), 0, 0, dp(8)) })
        val linkCard = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setBackgroundResource(R.drawable.bg_card); setPadding(dp(18), dp(18), dp(18), dp(18)) }
        val tvBtn = Button(this).apply { text = getString(R.string.ui_main_item_link); setBackgroundResource(R.drawable.bg_btn_primary); setTextColor(getColor(R.color.md_on_primary)); textSize = 15f; typeface = Typeface.DEFAULT_BOLD; setPadding(0, dp(14), 0, dp(14)); setOnClickListener { showDevicePicker() } }
        linkCard.addView(tvBtn)
        val pcBtn = Button(this).apply { text = getString(R.string.ui_main_item_screen); setBackgroundResource(R.drawable.bg_btn_ghost); setTextColor(cAccent); textSize = 15f; setPadding(0, dp(13), 0, dp(13)); setOnClickListener { startActivity(Intent(this@TouchpadActivity, com.allperiph.screen.ScreenActivity::class.java)) } }
        linkCard.addView(pcBtn, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(10) })
        col.addView(linkCard, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(12) }); return col
    }

    private fun updateDisplayStatus() { displayStatusView?.text = WirelessModule.statusText() }
    private fun refreshTabs() { tabs.forEachIndexed { i, t -> val on = i == flipper.displayedChild; val color = if (on) cAccent else cTabIdle; t.icon.setColorFilter(color); t.label.setTextColor(color); t.label.typeface = if (on) Typeface.DEFAULT_BOLD else Typeface.DEFAULT } }

    override fun onTouchEvent(ev: MotionEvent): Boolean {
        when (ev.actionMasked) {
            MotionEvent.ACTION_DOWN -> { edgeSwiped = false; val w = resources.displayMetrics.widthPixels; edgeStartX = if (flipper.displayedChild == PAGE_TOUCHPAD && (ev.x < dp(28) || ev.x > w - dp(28))) ev.x else -1f; if (edgeStartX < 0f) handler.postDelayed(dragTask, 550) }
            MotionEvent.ACTION_POINTER_DOWN -> if (ev.pointerCount == 2 && edgeStartX < 0f) { handler.postDelayed(rightDragTask, 550) }
            MotionEvent.ACTION_MOVE -> if (edgeStartX >= 0f && !edgeSwiped) { val dx = ev.x - edgeStartX; if (abs(dx) > dp(80)) { edgeSwiped = true; val n = flipper.childCount; showPage(if (dx < 0) (flipper.displayedChild + 1) % n else (flipper.displayedChild + n - 1) % n) } }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> { handler.removeCallbacks(dragTask); handler.removeCallbacks(rightDragTask); edgeStartX = -1f }
        }
        if (flipper.displayedChild == PAGE_TOUCHPAD && edgeStartX < 0f && !edgeSwiped) { (AgentController.module(ModuleId.TOUCHPAD) as? TouchpadModule)?.feed(ev); frameCount++; val now = System.currentTimeMillis(); if (now - lastShown > 250) { lastShown = now; if (hidReady) statusView?.text = getString(R.string.ui_touchpad_text_done, frameCount) } }
        return true
    }

    private fun label(text: String): TextView = TextView(this).apply { this.text = text; setTextColor(cText2); textSize = 12f }
    private fun card(color: Int, radius: Int): GradientDrawable = GradientDrawable().apply { shape = GradientDrawable.RECTANGLE; setColor(color); cornerRadius = radius.toFloat() }
    private fun strokeCard(color: Int, radius: Int, stroke: Int): GradientDrawable = card(color, radius).apply { setStroke(1, stroke) }
    private fun pill(color: Int): GradientDrawable = card(color, dp(999))
    private var backDialog: AlertDialog? = null

    @Deprecated("Deprecated in Java")
    @Suppress("DEPRECATION")
    override fun onBackPressed() {
        if (backDialog?.isShowing == true) return
        backDialog = AlertDialog.Builder(this, R.style.Theme_AllPeriph_Miuix_Dialog).setTitle(getString(R.string.ui_touchpad_dialog_exit_console)).setMessage(getString(R.string.ui_touchpad_dialog_home)).setPositiveButton(getString(R.string.ui_common_exit)) { _, _ -> finish() }.setNegativeButton(getString(R.string.ui_common_cancel), null).create().also { it.show() }
    }

    private fun updateDeviceChip() {
        val c = deviceChip ?: return; val ready = ControlTarget.isControlling()
        c.text = (if (ready) getString(R.string.ui_touchpad_text_device_2, ControlTarget.label) else ControlTarget.label) + " · ${com.allperiph.core.Uplink.hint()}"
        c.setTextColor(if (ready) cAccent else cText2)
        fileBtn?.visibility = if (ready) View.VISIBLE else View.GONE
        clipBtn?.visibility = if (ready) View.VISIBLE else View.GONE
    }

    private fun updateControlledChip() { val c = controlledChip ?: return; val on = ControlledService.isRunning(); c.text = if (on) getString(R.string.ui_touchpad_text_controlled_ok) else getString(R.string.ui_touchpad_text_controlled); c.setTextColor(if (on) cAccent else cText2) }
    private fun toggleControlled() { if (ControlledService.isRunning()) { ControlledService.stop(this); updateControlledChip(); Toast.makeText(this, getString(R.string.ui_touchpad_toast_close_done), Toast.LENGTH_SHORT).show(); return }; ControlledService.start(this); updateControlledChip(); openControlledPerms() }
    private fun openControlledPerms() { if (Build.VERSION.SDK_INT >= 23 && !Settings.canDrawOverlays(this)) { try { startActivity(Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:$packageName"))) } catch (_: Throwable) {} }; runCatching { startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS)) }; Toast.makeText(this, R.string.controlled_enable_hint, Toast.LENGTH_LONG).show() }
    private fun recvClipboard(text: String) { val cm = getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager; cm?.setPrimaryClip(ClipData.newPlainText("APX", text)); val preview = if (text.length > 24) text.take(24) + "…" else text; Toast.makeText(this, getString(R.string.ui_touchpad_toast_clipboard, preview), Toast.LENGTH_LONG).show() }

    private fun showDevicePicker() {
        TvDiscovery.start(); val items = ArrayList<CharSequence>(); items.add(getString(R.string.ui_main_item_local)); val tvs = TvDiscovery.list(); tvs.forEach { items.add("${it.name}  ${it.ip}  ${it.typeLabel}") }; items.add(getString(R.string.ui_main_item_input_manual))
        AlertDialog.Builder(this, R.style.Theme_AllPeriph_Miuix_Dialog).setTitle(getString(R.string.ui_main_dialog_device_pick)).setItems(items.toTypedArray()) { _, which -> when { which == 0 -> { ControlTarget.clear(); updateDeviceChip() }; which == items.size - 1 -> promptTvIp(); else -> { val tv = tvs.getOrNull(which - 1) ?: return@setItems; connectTv(tv.ip, tv.port, tv.name, tv.type) } } }.setNegativeButton(getString(R.string.ui_common_cancel), null).show()
    }

    private fun promptTvIp() {
        val edit = EditText(this).apply { hint = getString(R.string.ui_touchpad_hint_manual_ip); inputType = android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_URI; setPadding(dp(20), dp(12), dp(20), dp(12)) }
        AlertDialog.Builder(this, R.style.Theme_AllPeriph_Miuix_Dialog).setTitle(getString(R.string.ui_touchpad_dialog_input)).setView(edit).setPositiveButton(getString(R.string.ui_common_connect)) { _, _ -> val ip = edit.text.toString().trim(); if (ip.isNotEmpty()) connectTv(ip, TvControllerClient.PORT, ip, "tv") }.setNegativeButton(getString(R.string.ui_common_cancel), null).show()
    }

    private fun connectTv(ip: String, port: Int, name: String, type: String = "tv") {
        Thread({ val c = TvControllerClient(ip, port); val ok = c.connect(); runOnUiThread { if (isFinishing || isDestroyed) { c.disconnect(); return@runOnUiThread }; if (ok) { ControlTarget.controlClient?.takeIf { it !== c }?.disconnect(); ControlTarget.controlClient = c; ControlTarget.host = ip; ControlTarget.label = name; ControlTarget.type = type; c.onReverseClipboard = { text -> runOnUiThread { recvClipboard(text) } }; if (type == "tv") hotkeyBoard.enterTvMode() else hotkeyBoard.exitTvMode(); updateDeviceChip(); Toast.makeText(this, getString(R.string.ui_main_toast_done, name), Toast.LENGTH_SHORT).show() } else { ControlTarget.clear(); hotkeyBoard.exitTvMode(); updateDeviceChip(); Toast.makeText(this, getString(R.string.ui_touchpad_toast_connect_failed, ip), Toast.LENGTH_SHORT).show() } } }, "tv-connect").start()
    }

    private fun sendFile(uri: Uri) {
        val host = ControlTarget.host; if (host.isEmpty() || !ControlTarget.isControlling()) { Toast.makeText(this, getString(R.string.ui_main_toast_device_not), Toast.LENGTH_SHORT).show(); return }
        Toast.makeText(this, getString(R.string.ui_main_toast_file_send), Toast.LENGTH_SHORT).show()
        Thread({ val ok = FileSender.send(this, host, uri) { p -> runOnUiThread { statusView?.text = "发送 $p%" } }; runOnUiThread { Toast.makeText(this, if (ok) getString(R.string.ui_main_text_file_send_done) else getString(R.string.ui_touchpad_text_send_failed), Toast.LENGTH_SHORT).show(); statusView?.text = if (hidReady) getString(R.string.ui_touchpad_text_no_frames) else getString(R.string.ui_touchpad_text_not) } }, "tv-file").start()
    }

    private fun sendClipboard() {
        val c = ControlTarget.controlClient; if (c == null || !ControlTarget.isControlling()) { Toast.makeText(this, getString(R.string.ui_main_toast_device_not), Toast.LENGTH_SHORT).show(); return }
        val cm = getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager; val text = cm?.primaryClip?.getItemAt(0)?.text?.toString()
        if (text.isNullOrEmpty()) { Toast.makeText(this, getString(R.string.ui_touchpad_toast_clipboard_empty), Toast.LENGTH_SHORT).show(); return }
        val ok = c.sendClipboard(text); Toast.makeText(this, if (ok) getString(R.string.ui_touchpad_text_clipboard_send_done) else getString(R.string.ui_touchpad_text_send_failed), Toast.LENGTH_SHORT).show()
    }

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()
    companion object { const val EXTRA_PAGE = "page"; private const val PAGE_TOUCHPAD = 0; private const val PAGE_KEYBOARD = 1; private const val PAGE_DISPLAY = 2 }
}