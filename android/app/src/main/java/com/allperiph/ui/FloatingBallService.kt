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

class FloatingBallService : Service() {

    companion object {
        private const val TAG = "FloatingBall"
        private const val NOTIFICATION_ID = 700
        private const val PREFS = "apx_floating_ball"
        private const val KEY_ON = "on"
        const val ACTION_TOGGLE = "com.allperiph.ui.floating_ball.toggle"
        const val ACTION_STOP = "com.allperiph.ui.floating_ball.stop"
        private const val BALL_SIZE_DP = 44
        private const val MENU_RADIUS_DP = 82

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
    private var ballX = 0
    private var ballY = 0

    private var dragDX = 0f
    private var dragDY = 0f
    private var downRawX = 0f
    private var downRawY = 0f
    private var downAt = 0L
    private var longPress = false

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density + 0.5f).toInt()
    private val ballSize get() = dp(BALL_SIZE_DP)

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

    private fun addBall() {
        if (ballAttached) return
        val v = ball
            ?: LayoutInflater.from(ThemePref.wrap(this)).inflate(R.layout.view_floating_ball, null)
                .also { ball = it }
        if (ballX == 0 && ballY == 0) {
            ballX = resources.displayMetrics.widthPixels - ballSize - dp(8)
            ballY = resources.displayMetrics.heightPixels / 3
        }
        wm.addView(v, ballLayoutParams())
        ballAttached = true
        setupBallTouch(v)
        updateBallLabel()
    }

    private fun ballLayoutParams() = WindowManager.LayoutParams(
        ballSize, ballSize,
        WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
        WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
            WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
        PixelFormat.TRANSLUCENT,
    ).apply {
        gravity = Gravity.TOP or Gravity.START
        x = ballX
        y = ballY
    }

    private fun setupBallTouch(v: View) {
        v.setOnTouchListener { view, ev ->
            when (ev.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    dragDX = ballX - ev.rawX
                    dragDY = ballY - ev.rawY
                    downRawX = ev.rawX
                    downRawY = ev.rawY
                    downAt = System.currentTimeMillis()
                    longPress = false
                }
                MotionEvent.ACTION_MOVE -> {
                    if (!longPress && System.currentTimeMillis() - downAt >= 400) {
                        longPress = true
                        com.allperiph.ui.Feedback.haptic(this)
                        collapseMenu()
                    }
                    if (longPress) {
                        ballX = (ev.rawX + dragDX).toInt()
                        ballY = (ev.rawY + dragDY).toInt()
                        runCatching { wm.updateViewLayout(view, ballLayoutParams()) }
                    }
                }
                MotionEvent.ACTION_UP -> {
                    if (longPress) {
                        // 落位
                    } else if (abs(ev.rawX - downRawX) < 12 && abs(ev.rawY - downRawY) < 12) {
                        toggleMenu()
                    }
                }
            }
            true
        }
    }

    private fun toggleMenu() {
        if (menu != null) collapseMenu() else expandMenu()
    }

    private fun expandMenu() {
        if (menu != null) return
        val radius = dp(MENU_RADIUS_DP)
        val btnSize = dp(44)
        val size = (radius + btnSize / 2) * 2
        val center = size / 2
        val cx = ballX + ballSize / 2
        val cy = ballY + ballSize / 2

        val container = FrameLayout(ThemePref.wrap(this)).apply {
            setBackgroundResource(android.R.color.transparent)
        }
        val slot = TextView(ThemePref.wrap(this)).apply {
            setBackgroundResource(android.R.color.transparent)
            isClickable = true
            isFocusable = true
            setOnClickListener { collapseMenu() }
        }
        val slp = FrameLayout.LayoutParams(ballSize, ballSize)
        slp.leftMargin = center - ballSize / 2
        slp.topMargin = center - ballSize / 2
        container.addView(slot, slp)

        val items = listOf(
            MenuItem("✕", getString(R.string.ui_main_floating_ball_close)) { onMenuClose() },
            MenuItem("⌂", getString(R.string.ui_main_floating_ball_open)) { onMenuApp() },
            MenuItem("⇄", getString(R.string.ui_main_floating_ball_mode)) { onMenuMode() },
            MenuItem("⚙", getString(R.string.ui_main_floating_ball_custom)) { onMenuCustom() },
        )
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
            x = cx - center
            y = cy - center
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

    private fun onMenuClose() = shutdown()

    private fun onMenuApp() {
        runCatching {
            startActivity(
                Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            )
        }
    }

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
    }

    private fun onMenuCustom() {
        runCatching {
            startActivity(
                Intent(this, MainActivity::class.java)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            )
        }
    }

    private fun updateBallLabel() {
        val tv = ball?.findViewById<TextView>(R.id.tvBallState) ?: return
        tv.text = "apx"
    }

    private data class MenuItem(
        val glyph: String,
        val label: String,
        val action: () -> Unit,
    )
}
