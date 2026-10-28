package com.allperiph.shared.event

import android.os.Handler
import android.os.Looper
import java.util.concurrent.CopyOnWriteArrayList
import com.allperiph.shared.util.Log

/**
 * 极简同步事件总线（无第三方依赖）。
 * - 默认在**发布者线程**回调，适合数据面（传感器/USB 读写）
 * - 传 Handler 可切线程，UI 用 [onMain]
 */
object EventBus {

    fun interface Disposable {
        fun dispose()
    }

    private class Sub<T : Any>(
        val type: Class<T>,
        val handler: Handler?,
        val block: (T) -> Unit,
    ) {
        fun deliver(event: Any) {
            if (!type.isInstance(event)) return
            @Suppress("UNCHECKED_CAST")
            val typed = event as T
            val h = handler
            when {
                h == null || h.looper == Looper.myLooper() -> safe(typed)
                else -> h.post { safe(typed) }
            }
        }

        private fun safe(e: T) = runCatching { block(e) }
            .onFailure { Log.w("EventBus", "listener failed on ${type.simpleName}: ${it.message}") }
    }

    private val subs = CopyOnWriteArrayList<Sub<*>>()

    fun post(event: Any) {
        for (s in subs) s.deliver(event)
    }

    // 只在 inline 中做 reified → Class 的翻译，实体逻辑委托给下面的非 inline 重载：
    // public inline 函数不能访问 private 的 Sub / subs，直接写体会编译失败。
    inline fun <reified T : Any> on(handler: Handler? = null, noinline block: (T) -> Unit): Disposable =
        on(T::class.java, handler, block)

    /* ⚠️ 这里必须转调下面那个带 MAIN_HANDLER 的重载。
       历史写法是 `on(T::class.java, null, block)` —— handler 为 null 时 Sub.deliver 走
       「发布者线程直接回调」，也就是一个叫 onMain 的函数**并不在主线程回调**。
       谁按名字用它去碰 View，就会踩 CalledFromWrongThreadException（或更糟：什么都不发生）。
       目前唯一调用方是手柄页的出口状态行，正是「碰 View」的场景。 */
    inline fun <reified T : Any> onMain(noinline block: (T) -> Unit): Disposable =
        onMain(T::class.java, block)

    fun <T : Any> on(type: Class<T>, handler: Handler? = null, block: (T) -> Unit): Disposable {
        val sub = Sub(type, handler, block)
        subs.add(sub)
        return Disposable { subs.remove(sub) }
    }

    fun <T : Any> onMain(type: Class<T>, block: (T) -> Unit): Disposable =
        on(type, MAIN_HANDLER, block)

    private val MAIN_HANDLER by lazy { Handler(Looper.getMainLooper()) }
}
