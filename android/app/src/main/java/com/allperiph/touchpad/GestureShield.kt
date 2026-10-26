package com.allperiph.touchpad

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.widget.Toast
import com.allperiph.R
import com.allperiph.gadget.RootShell
import com.allperiph.shared.util.Log
import java.util.Collections
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

/**
 * MIUI 三指手势遮蔽（root 动态切换）。
 *
 * 背景：手机在触控板里做三指滑动时，系统会先截屏再把手势交给应用
 * （真机核对 `settings list system`）：
 *
 *   three_gesture_down=screen_shot               ← 三指下滑截屏（用户报的误触）
 *   three_gesture_long_press=partial_screen_shot ← 三指长按区域截屏
 *
 * 注意 `three_gesture_screenshot` 在本机本来就是 0，与误触无关，动它没用。
 * 手势由 SystemUI/手势服务在系统层判定，应用内怎么写 onTouchEvent 都晚于系统，
 * 所以只能在系统层改键值。
 *
 * **安全网**（异常退出没还原 = 把用户手机的三指截屏永久改掉，必须防）：
 *  ① 写之前先把原值落到 apx prefs，写完置 active 标记；
 *  ② 进程启动时（App.onCreate）无条件自愈：标记还在就还原；
 *  ③ 还原失败保留标记，下次启动继续试。
 *
 * 无 root 时返回失败，UI 走兜底文案（引导用户手动关掉系统三指手势）。
 */
object GestureShield {

    private const val TAG = "GestureShield"

    private const val PREFS = "apx"
    private const val KEY_ON = "gesture_shield"
    private const val KEY_ACTIVE = "gesture_shield_active"
    private const val KEY_HINTED = "gesture_shield_hinted"
    private const val KEY_ORIG_PREFIX = "gesture_shield_orig_"

    /**
     * system 命名空间下需要遮蔽的键。遮蔽值取 none：MIUI 侧的枚举认不出这个动作，
     * 就当没配这个手势（不会去执行 screen_shot）。
     */
    private val KEYS = listOf("three_gesture_down", "three_gesture_long_press")

    private const val CMD_TIMEOUT = 4_000L
    private const val ORIG_NULL = "null"

    /** 界面切换时先等一下再还原：A.onPause → B.onResume 挨着发生，别为了几十毫秒
     *  白白跑两次 su（每次 fork su 都要 200ms 上下），闪烁也难看 */
    private const val RELEASE_DELAY_MS = 1_200L

    /** root 写入串行化：进入/退出操控面各一次，不需要并发 */
    private val worker: ExecutorService = Executors.newSingleThreadExecutor { r ->
        Thread(r, "apx-gesture-shield").apply { isDaemon = true }
    }

    private val main = Handler(Looper.getMainLooper())

    /** 当前持有遮蔽的界面（引用计数用；进程被杀后由 prefs 的 active 标记自愈） */
    private val holders: MutableSet<String> = Collections.synchronizedSet(HashSet<String>())

    /** 延迟还原任务：这段时间内又有界面 acquire 就取消 */
    private var pendingRestore: Runnable? = null

    // —————————————————————————— 开关 ——————————————————————————

    private fun prefs(ctx: Context) =
        ctx.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun isEnabled(ctx: Context): Boolean = prefs(ctx).getBoolean(KEY_ON, false)

    fun setEnabled(ctx: Context, on: Boolean) {
        prefs(ctx).edit().putBoolean(KEY_ON, on).apply()
        if (!on) {
            holders.clear()
            healIfNeeded(ctx)
        }
    }

    /** root 不可用或写不进去时的提示只弹一次，别每次进界面都弹 */
    fun shouldHintNoRoot(ctx: Context): Boolean {
        val p = prefs(ctx)
        if (p.getBoolean(KEY_HINTED, false)) return false
        p.edit().putBoolean(KEY_HINTED, true).apply()
        return true
    }

    // —————————————————————————— 持有 / 释放 ——————————————————————————

    /**
     * 界面进入前台时调用（主线程安全，实际 root 操作在 worker 上跑）。
     * 同一 holder 重复调用幂等；[onResult] 在 **worker 线程** 回调，UI 需自行切主线程。
     */
    fun acquire(ctx: Context, holder: String, onResult: ((Boolean) -> Unit)? = null) {
        if (!isEnabled(ctx)) {
            onResult?.invoke(false)
            return
        }
        cancelPendingRestore()
        if (!holders.add(holder)) return // 已持有，不重复写
        val app = ctx.applicationContext
        worker.execute { onResult?.invoke(applyShield(app)) }
    }

    /** 界面退出前台时调用；最后一个持有者退出才真正还原 */
    fun release(ctx: Context, holder: String) {
        if (!holders.remove(holder)) return
        if (holders.isNotEmpty()) return
        val app = ctx.applicationContext
        worker.execute { restoreShield(app) }
    }

    /** 界面切换用：最后一个持有者退出后等 [RELEASE_DELAY_MS]，期间有人重新持有就不还原 */
    fun releaseSoon(ctx: Context, holder: String) {
        if (!holders.remove(holder)) return
        if (holders.isNotEmpty()) return
        val app = ctx.applicationContext
        main.postDelayed(
            Runnable {
                if (holders.isEmpty()) worker.execute { restoreShield(app) }
            }.also { pendingRestore = it },
            RELEASE_DELAY_MS,
        )
    }

    private fun cancelPendingRestore() {
        pendingRestore?.let { main.removeCallbacks(it) }
        pendingRestore = null
    }

    /**
     * 进界面：开关开着就上遮蔽；失败且是**首次**时提示一次走手动兜底。
     * 提示用 app context 在主线程弹，调用方不必处理线程。
     */
    fun acquireWithHint(ctx: Context, holder: String) {
        if (!isEnabled(ctx)) return
        acquire(ctx, holder) { ok ->
            if (!ok && shouldHintNoRoot(ctx)) {
                Handler(Looper.getMainLooper()).post {
                    Toast.makeText(
                        ctx.applicationContext,
                        R.string.ui_gesture_shield_toast_no_root,
                        Toast.LENGTH_LONG,
                    ).show()
                }
            }
        }
    }

    // —————————————————————————— 自愈 ——————————————————————————

    /** 进程启动时调用：上次异常退出留下的遮蔽在这里还原 */
    fun healIfNeeded(ctx: Context) {
        val app = ctx.applicationContext
        if (!prefs(app).getBoolean(KEY_ACTIVE, false)) return
        Log.i(TAG, "found leftover shield, restoring")
        worker.execute { restoreShield(app) }
    }

    // —————————————————————————— 实际读写 ——————————————————————————

    private fun applyShield(app: Context): Boolean {
        val p = prefs(app)
        if (p.getBoolean(KEY_ACTIVE, false)) return true

        val shell = RootShell.open() ?: return false
        try {
            val orig = LinkedHashMap<String, String>()
            for (k in KEYS) {
                val v = shell.exec("settings get system $k", CMD_TIMEOUT).out.trim()
                orig[k] = v.ifEmpty { ORIG_NULL }
            }

            var ok = true
            for (k in KEYS) {
                if (!shell.ok("settings put system $k none", CMD_TIMEOUT)) ok = false
            }
            // 回读校验：写没进去就当失败（宁可让用户看到提示，也不要静默无效）
            for (k in KEYS) {
                val back = shell.exec("settings get system $k", CMD_TIMEOUT).out.trim()
                if (back != "none") ok = false
            }
            if (!ok) {
                Log.w(TAG, "shield write failed, rolling back")
                restoreWith(shell, orig)
                return false
            }

            val e = p.edit().putBoolean(KEY_ACTIVE, true)
            for ((k, v) in orig) e.putString(KEY_ORIG_PREFIX + k, v)
            e.apply()
            Log.i(TAG, "shield on, original=$orig")
            return true
        } catch (t: Throwable) {
            Log.w(TAG, "shield on failed: ${t.message}")
            return false
        } finally {
            shell.close()
        }
    }

    private fun restoreShield(app: Context): Boolean {
        val p = prefs(app)
        if (!p.getBoolean(KEY_ACTIVE, false)) return true

        val shell = RootShell.open() ?: return false
        try {
            var ok = true
            for (k in KEYS) {
                val orig = p.getString(KEY_ORIG_PREFIX + k, null)
                val done = if (orig == null || orig == ORIG_NULL) {
                    shell.ok("settings delete system $k", CMD_TIMEOUT)
                } else {
                    shell.ok("settings put system $k '$orig'", CMD_TIMEOUT)
                }
                if (!done) ok = false
            }
            if (ok) {
                p.edit().putBoolean(KEY_ACTIVE, false).apply()
                Log.i(TAG, "shield off")
            } else {
                Log.w(TAG, "restore incomplete, keep flag for next launch")
            }
            return ok
        } catch (t: Throwable) {
            Log.w(TAG, "shield off failed: ${t.message}")
            return false
        } finally {
            shell.close()
        }
    }

    /** 用已有 shell 回写一组原值（applyShield 半途失败时用） */
    private fun restoreWith(shell: RootShell, orig: Map<String, String>) {
        for ((k, v) in orig) {
            if (v == ORIG_NULL) shell.ok("settings delete system $k", CMD_TIMEOUT)
            else shell.ok("settings put system $k '$v'", CMD_TIMEOUT)
        }
    }
}
