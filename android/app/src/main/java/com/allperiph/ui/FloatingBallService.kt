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
import android.widget.TextView
import com.allperiph.R
import com.allperiph.shared.util.Log
import com.allperiph.touchpad.TouchpadModule
import kotlin.math.abs

/**
 * 悬浮球（反馈#7）：独立系统 overlay 触控板窗口。
 *
 * 为什么需要：连接/HID 已能后台保活（AgentForegroundService + KeepAlive），但**输入面**
 * 一直挂在前台 Activity —— HyperOS 小窗失焦时系统不给它派发输入事件（freeform 未聚焦
 * 窗口不收 input），实测小窗后台无法输入、必须展开小窗，Pad 上很影响显示。
 * overlay 窗口与前台/焦点无关，其他 App 照常可点，球/面板照常收触摸。
 *
 * 形态：
 *  - 小球（48dp）：单击展开/收起，拖动移位，长按关闭服务；
 *  - 面板（底部 300dp 半透卡）：整块即触控板，touch 直接喂 [TouchpadModule.feed]
 *    —— 与触控板页完全同一条手势链路（GestureEngine 的 bounds 由首个事件自适应，
 *    不需要配置 surface）；面板外屏幕区域不受影响（FLAG_NOT_TOUCH_MODAL）。
 *
 * 权限：复用已有 SYSTEM_ALERT_WINDOW（副屏同款）；未授权时先跳系统授权页。
 * 入口：设置页「工具」小节 → 悬浮球。
 */
class FloatingBallService : Service() {

    companion object {
        private const val TAG = "FloatingBall"
        private const val NOTIFICATION_ID = 700
        private const val PREFS = "apx_floating_ball"
        private const val KEY_ON = "on"
        const val ACTION_TOGGLE = "com.allperiph.ui.floating_ball.toggle"
        const val ACTION_STOP = "com.allperiph.ui.floating_ball.stop"

        /** 三指手势遮蔽的持有者标识（与主页 / 独立操控面区分开） */
        private const val HOLDER_BALL = "ball"

        /** 服务实际存活状态（设置行显示用，比持久化的开关位更真实） */
        @Volatile
        var isRunning: Boolean = false
            private set

        /**
         * 设置页入口：无悬浮窗权限 → 跳系统授权页（HyperOS：设置→隐私保护→显示悬浮窗）；
         * 有权限 → 开/关切换（服务未活 = 开，已活 = 停）。
         */
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
    private var panel: View? = null

    // 球拖动/点击判定
    private var dragDX = 0f
    private var dragDY = 0f
    private var downRawX = 0f
    private var downRawY = 0f
    private var downAt = 0L
    private var moved = false

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
        when (intent?.action) {
            ACTION_STOP -> {
                shutdown()
                return START_NOT_STICKY
            }
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
        // 服务被杀/关闭时面板可能还开着：遮蔽必须一起还，否则系统三指截屏一直被关着
        com.allperiph.touchpad.GestureShield.release(this, HOLDER_BALL)
        detach(ball)
        detach(panel)
        ball = null
        panel = null
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    private fun detach(v: View?) {
        v?.let { runCatching { wm.removeView(it) } }
    }

    // ————————————————————————— 小球 —————————————————————————

    private fun addBall() {
        if (ballAttached) return
        // issue#44：悬浮球此前用 Service 自带（亮色）上下文 inflate，深色下取色永远是亮的
        val v = ball ?: LayoutInflater.from(ThemePref.wrap(this)).inflate(R.layout.view_floating_ball, null).also { ball = it }
        wm.addView(v, ballLayoutParams())
        ballAttached = true
        setupBallTouch(v)
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
        x = resources.displayMetrics.widthPixels - dp(64)
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
                    moved = false
                }
                MotionEvent.ACTION_MOVE -> {
                    if (abs(ev.rawX - downRawX) > 12 || abs(ev.rawY - downRawY) > 12) moved = true
                    view.x = ev.rawX + dragDX
                    view.y = ev.rawY + dragDY
                }
                MotionEvent.ACTION_UP -> {
                    val held = System.currentTimeMillis() - downAt
                    when {
                        moved -> { /* 拖动结束：落位即可 */ }
                        held >= 400 -> shutdown()                            // 长按关闭悬浮球
                        else -> if (panel == null) expand() else collapse()  // 单击展开/收起
                    }
                }
            }
            true
        }
    }

    // ————————————————————————— 展开面板（底部触控板） —————————————————————————

    private fun expand() {
        if (panel != null) return
        val v = LayoutInflater.from(ThemePref.wrap(this)).inflate(R.layout.view_touchpad_panel, null)
        v.findViewById<TextView>(R.id.tvPanelClose)?.setOnClickListener { collapse() }
        v.findViewById<View>(R.id.touchSurface).setOnTouchListener { _, ev ->
            // 与触控板页同一条链路：feed → GestureEngine → 三出口（USB 鼠标帧/蓝牙/无线）
            (AgentController.module(com.allperiph.core.ModuleId.TOUCHPAD) as? TouchpadModule)?.feed(ev)
            true
        }
        val lp = WindowManager.LayoutParams(
            WindowManager.LayoutParams.MATCH_PARENT,
            dp(300),
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
            PixelFormat.TRANSLUCENT,
        ).apply { gravity = Gravity.BOTTOM }
        wm.addView(v, lp)
        panel = v
        // 悬浮球的触控板同样是三指滑动面：展开期间一并遮蔽系统三指截屏，
        // 收起就还（这个面板压在别的应用上，不还的话用户在桌面想三指截屏会按不动）
        com.allperiph.touchpad.GestureShield.acquireWithHint(this, HOLDER_BALL)
        // 展开时收起球（避免叠在面板上）
        ball?.let { if (ballAttached) { detach(it); ballAttached = false } }
    }

    private fun collapse() {
        detach(panel)
        panel = null
        // 收起就走延迟还原（1.2s 内又展开就不用白跑两次 su）
        com.allperiph.touchpad.GestureShield.releaseSoon(this, HOLDER_BALL)
        addBall()
    }
}
