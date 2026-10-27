package com.allperiph.ui

import android.app.Notification
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.PixelFormat
import android.net.Uri
import android.os.IBinder
import android.provider.Settings
import android.view.Gravity
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.TextView
import com.allperiph.R
import com.allperiph.shared.util.Log
import kotlin.math.abs

/**
 * 悬浮球（v1.0_test 重设计）：圆形半透明磨砂球 + 环绕一圈的菜单圆钮。
 *
 * 交互：
 *  - 单击小球：展开 / 收起围绕球一圈的菜单圆钮；
 *  - 长按小球（400ms）：进入拖动模式，可移动悬浮球位置（松手落位）；
 *  - 菜单圆钮（环绕一圈）：
 *      关闭悬浮球 · 打开 App · 无线/蓝牙/USB 模式切换 · 自定义功能。
 *
 * 移除了旧版的「底部触控板面板」（虚拟触控板 GUI）—— 输入仍走 App 内触控板页，
 * 悬浮球只做快捷入口与模式切换。
 *
 * 权限：复用 SYSTEM_ALERT_WINDOW；入口：设置页「工具」小节 → 悬浮球。
 */
class FloatingBallService : Service() {

    companion object {
        private const val TAG = "FloatingBall"
        private const val NOTIFICATION_ID = 700
        private const val PREFS = "apx_floating_ball"
        private const val KEY_ON = "on"
        const val ACTION_TOGGLE = "com.allperiph.ui.floating_ball.toggle"
        const val ACTION_STOP = "com.allperiph.ui.floating_ball.stop"

        /** 菜单环半径（球心到圆钮心）。 */
        private const val MENU_RADIUS_DP = 82

        /** 服务实际存活状态。 */
        @Volatile
        var isRunning: Boolean = false
            private set

        fun requestToggle(c: Context) {
            if (!Settings.canDrawOverlays(c)) {
                runCatching {
                    c.startActivity(
                        Intent(
                            Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                            Uri.parse("package:${c.packageName}"),
                        ).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
                    )
                }
                Log.i(TAG, "悬浮窗未授权，已跳系统授权页")
                return
            }
            val intent = Intent(c, FloatingBallService::class.java).apply {
                action = if (isRunning) ACTION_STOP else ACTION_TOGGLE
            }
            c.startService(intent)
        }
    }

    private val wm by lazy { getSystemService(Context.WINDOW_SERVICE) as WindowManager }

    private var ball: View? = null
    private var ballAttached = false
    private var menu: View? = null

    // 球拖动 / 点击判定
    private var dragDX = 0f
    private var dragDY = 0f
    private var downRawX = 0f
    private var downRawY = 0f
    private var downAt = 0L
    private var longPress = false

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density + 0.5f).toInt()

    // ————————————————————————— 生命周期 —————————————————————————

    override fun onCreate() {
        super.onCreate()
        Log.i(TAG, "onCreate")
        NotificationChannels.ensure(this)
        startForeground(
            NOTIFICATION_ID,
            Notification.Builder(this, NotificationChannels.CHANNEL_STATUS)
                .setSmallIcon(R.drawable.ic_launcher_app)
                .setContentTitle(getString(R.string.ui_main_label_floating_ball))
                .setContentText(getString(R.string.ui_main_floating_ball_note))
                .setOngoing(true)
                .setContentIntent(
                    PendingIntent.getService(
                        this, 0,
                        Intent(this, FloatingBallService::class.java).apply { action = ACTION_STOP },
                        PendingIntent.FLAG_IMMUTABLE,
                    ),
                )
                .build(),
            ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE,
        )
        isRunning = true
        addBall()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            shutdown()
            return START_NOT_STICKY
        }
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        Log.i(TAG, "onDestroy")
        shutdown()
        isRunning = false
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun shutdown() {
        getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean(KEY_ON, false).apply()
        com.allperiph.touchpad.GestureShield.release(this, "ball")
        detach(menu)
        detach(ball)
        menu = null
        ball = null
        ballAttached = false
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    private fun detach(v: View?) {
        v?.let { runCatching { wm.removeView(it) } }
    }

    // ————————————————————————— 小球 —————————————————————————

    private fun addBall() {
        if (ballAttached) return
        val v = ball
            ?: LayoutInflater.from(ThemePref.wrap(this)).inflate(R.layout.view_floating_ball, null)
                .also { ball = it }
        wm.addView(v, ballLayoutParams())
        ballAttached = true
        setupBallTouch(v)
        updateBallLabel()
    }

    private fun ballLayoutParams() = WindowManager.LayoutParams(
        WindowManager.LayoutParams.WRAP_CONTENT,
        WindowManager.LayoutParams.WRAP_CONTENT,
        WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
        WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
            WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
        PixelFormat.TRANSLUCENT,
    ).apply {
        gravity = Gravity.TOP or Gravity.START
        x = resources.displayMetrics.widthPixels - dp(72)
        y = resources.displayMetrics.heightPixels / 3
    }

    private fun setupBallTouch(v: View) {
        v.setOnTouchListener { view, ev ->
            when (ev.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    dragDX = view.x - ev.rawX
                    dragDY = view.y - ev.rawY
                    downRawX = ev.rawX
                    downRawY = ev.rawY
                    downAt = System.currentTimeMillis()
                    longPress = false
                }
                MotionEvent.ACTION_MOVE -> {
                    if (!longPress && System.currentTimeMillis() - downAt >= 400) {
                        longPress = true
                        com.allperiph.ui.Feedback.haptic(this)
                    }
                    if (longPress) {
                        view.x = ev.rawX + dragDX
                        view.y = ev.rawY + dragDY
                    }
                }
                MotionEvent.ACTION_UP -> {
                    if (longPress) {
                        // 长按拖动结束：落位即可
                    } else if (abs(ev.rawX - downRawX) < 12 && abs(ev.rawY - downRawY) < 12) {
                        toggleMenu()
                    }
                }
            }
            true
        }
    }

    // ————————————————————————— 菜单环 —————————————————————————

    private fun toggleMenu() {
        if (menu != null) collapseMenu() else expandMenu()
    }

    /** 展开：以球为圆心，环绕一圈菜单圆钮（上/右/下/左）。 */
    private fun expandMenu() {
        if (menu != null) return
        val b = ball ?: return
        val cx = b.x + b.width / 2f
        val cy = b.y + b.height / 2f

        val radius = dp(MENU_RADIUS_DP)
        val btnSize = dp(44)
        val size = (radius + btnSize / 2) * 2
        val center = size / 2

        val container = FrameLayout(ThemePref.wrap(this)).apply {
            setBackgroundResource(android.R.color.transparent)
        }

        // 中心占位区（覆盖球位）：点击 = 收起菜单
        val ballSlot = dp(60)
        val slot = TextView(ThemePref.wrap(this)).apply {
            setBackgroundResource(android.R.color.transparent)
            isClickable = true
            isFocusable = true
            setOnClickListener { collapseMenu() }
        }
        val slp = FrameLayout.LayoutParams(ballSlot, ballSlot)
        slp.leftMargin = center - ballSlot / 2
        slp.topMargin = center - ballSlot / 2
        container.addView(slot, slp)

        val items = listOf(
            MenuItem("✕", getString(R.string.ui_main_floating_ball_close)) { onMenuClose() },
            MenuItem("⌂", getString(R.string.ui_main_floating_ball_open)) { onMenuApp() },
            MenuItem(modeGlyph(), getString(R.string.ui_main_floating_ball_mode)) { onMenuMode() },
            MenuItem("⚙", getString(R.string.ui_main_floating_ball_custom)) { onMenuCustom() },
        )

        // 4 个圆钮按上/右/下/左排列（角度 -90°、0°、90°、180°）
        val angles = intArrayOf(-90, 0, 90, 180)
        items.forEachIndexed { i, item ->
            val btn = TextView(ThemePref.wrap(this)).apply {
                text = item.glyph
                contentDescription = item.label
                setBackgroundResource(R.drawable.bg_menu_button)
                gravity = Gravity.CENTER
                textSize = 16f
                setTextColor(getColor(R.color.ball_text))
                isClickable = true
                isFocusable = true
                setOnClickListener {
                    item.action()
                    collapseMenu()
                }
            }
            val a = Math.toRadians(angles[i].toDouble())
            val bx = center + (radius * Math.cos(a)).toInt() - btnSize / 2
            val by = center + (radius * Math.sin(a)).toInt() - btnSize / 2
            val lp = FrameLayout.LayoutParams(btnSize, btnSize)
            lp.leftMargin = bx
            lp.topMargin = by
            container.addView(btn, lp)
        }

        val lp = WindowManager.LayoutParams(
            size, size,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
            PixelFormat.TRANSLUCENT,
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            x = (cx - center).toInt()
            y = (cy - center).toInt()
        }
        wm.addView(container, lp)
        menu = container
        Log.i(TAG, "菜单环展开")
    }

    private fun collapseMenu() {
        detach(menu)
        menu = null
        Log.i(TAG, "菜单环收起")
    }

    // ————————————————————————— 菜单动作 —————————————————————————

    private fun onMenuClose() = shutdown()

    private fun onMenuApp() {
        runCatching {
            startActivity(
                Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            )
        }
    }

    /** 无线 → 蓝牙 → USB → 无线 循环切换（复用 AgentController 的传输开关语义）。 */
    private fun onMenuMode() {
        val transports = listOf("wifi", "bt", "usb")
        val current = transports.firstOrNull { AgentController.isTransportEnabled(this, it) }
        val next = when (current) {
            "wifi" -> "bt"
            "bt" -> "usb"
            "usb" -> "wifi"
            else -> "wifi"
        }
        for (t in transports) {
            if (t != next) AgentController.setTransportEnabled(this, t, false)
        }
        AgentController.setTransportEnabled(this, next, true)
        for (id in AgentController.groupModules(next)) {
            AgentController.setModuleEnabled(this, id, true)
        }
        Log.i(TAG, "模式切换到：${AgentController.transportLabel(next)}")
        updateBallLabel()
    }

    /** 自定义功能：默认打开 App 设置页（后续可在设置里把该圆钮换成任意功能）。 */
    private fun onMenuCustom() {
        runCatching {
            startActivity(
                Intent(this, MainActivity::class.java)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            )
        }
    }

    // ————————————————————————— 状态 —————————————————————————

    private fun modeGlyph(): String = when (
        listOf("wifi", "bt", "usb").firstOrNull { AgentController.isTransportEnabled(this, it) }
    ) {
        "wifi" -> "W"
        "bt" -> "B"
        "usb" -> "U"
        else -> "M"
    }

    private fun updateBallLabel() {
        val tv = ball?.findViewById<TextView>(R.id.tvBallState) ?: return
        tv.text = modeGlyph()
    }

    private data class MenuItem(
        val glyph: String,
        val label: String,
        val action: () -> Unit,
    )
}
