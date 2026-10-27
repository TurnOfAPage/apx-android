package com.allperiph.wireless

import com.allperiph.shared.proto.ApxFrame
import com.allperiph.shared.util.Log
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

/**
 * 手机端「TV 控制」客户端：连入 TV 服务端，发送 APX1 控制帧。
 */
class TvControllerClient(
    private val host: String,
    private val port: Int = PORT,
    private val token: String = "",
) {
    @Volatile var ready: Boolean = false; private set
    private var sock: Socket? = null
    private var out: OutputStream? = null
    private val running = AtomicBoolean(false)
    private var seq = 0
    private val writeLock = Any()
    private val txQueue = java.util.concurrent.LinkedBlockingQueue<ByteArray>()
    private var txThread: Thread? = null

    @Volatile var onReverseClipboard: ((String) -> Unit)? = null
    @Volatile var onRemoteInputRequest: ((String, String) -> Unit)? = null
    @Volatile var onRemoteInputText: ((String, Int) -> Unit)? = null
    @Volatile var onRemoteInputDone: (() -> Unit)? = null

    private val rxLock = Any()
    private var rxBuf = ByteArray(4096)
    private var rxLen = 0

    /** sendControl 断线丢帧日志节流（5s 一条）—— 前端与运行日志此前对丢帧完全无感 */
    @Volatile private var lastDropLogAt = 0L

    fun connect(): Boolean {
        if (running.get()) return ready
        running.set(true)
        val ok = openSocket()
        if (ok) thread(name = "tvctrl-ping") { pingLoop() } else running.set(false)
        return ok
    }

    /** 建立 socket + 握手 + 启动 reader（connect 与自动重连共用） */
    private fun openSocket(): Boolean = try {
        val s = Socket()
        s.tcpNoDelay = true
        s.keepAlive = true   // 死链路探测：对端悄悄消失时让内核发探测，帮助 reader 尽早退出走重连
        s.connect(InetSocketAddress(host, port), 5000)
        sock = s; out = s.getOutputStream()
        val tok = token.toByteArray(Charsets.UTF_8)
        val hdr = ByteArray(4); hdr[0] = (tok.size and 0xFF).toByte(); hdr[1] = ((tok.size ushr 8) and 0xFF).toByte(); hdr[2] = 0; hdr[3] = 0
        out!!.write(hdr); if (tok.isNotEmpty()) out!!.write(tok); out!!.flush()
        ready = true; synchronized(rxLock) { rxLen = 0 }
        thread(name = "tvctrl-reader") { readerLoop(s) }
        startTx()
        Log.i("TvCtrl", "已连 TV $host:$port")
        reportPadKeyboardState()
        true
    } catch (t: Throwable) { Log.e("TvCtrl", "连 TV 失败：${t.message}"); runCatching { s_close() }; false }

    fun disconnect() { running.set(false); ready = false; runCatching { txQueue.clear() }; runCatching { txThread?.interrupt() }; txThread = null; s_close() }
    private fun s_close() = runCatching { sock?.close() }.also { sock = null; out = null }

    private fun readerLoop(s: Socket) {
        val ins = s.getInputStream(); val buf = ByteArray(4096)
        var why = "对端关闭连接"
        while (running.get() && !s.isClosed) {
            val n = try { ins.read(buf) } catch (t: Throwable) { why = "读取异常：${t.message}"; -1 }
            if (n <= 0) { if (n == 0) why = "对端关闭连接"; break }
            feed(buf, n)
            for (b in pumpFrames()) handleServerFrame(b)
        }
        ready = false
        if (running.get()) {
            Log.w("TvCtrl", "连接断开（$why），启动自动重连 $host:$port")
            thread(name = "tvctrl-reconn") {
                // 指数退避：固定 2s 无限重连会对端持续拒绝时形成连接风暴，
                // 反复「已连→断开→重连」刷屏且空耗资源；改为 1s 起、翻倍、封顶 30s。
                var delay = 1000L
                while (running.get() && !ready) {
                    try { Thread.sleep(delay) } catch (_: InterruptedException) { break }
                    if (!running.get() || ready) break
                    if (openSocket()) { delay = 1000L; break }
                    delay = (delay * 2).coerceAtMost(30000L)
                }
            }
        }
    }

    private fun feed(data: ByteArray, n: Int) {
        synchronized(rxLock) {
            if (rxLen + n > rxBuf.size) { var c = rxBuf.size * 2; while (c < rxLen + n) c *= 2; rxBuf = rxBuf.copyOf(c) }
            System.arraycopy(data, 0, rxBuf, rxLen, n); rxLen += n
        }
    }

    private fun pumpFrames(): List<ByteArray> {
        val out = ArrayList<ByteArray>()
        synchronized(rxLock) {
            var off = 0
            while (rxLen - off >= ApxFrame.HEADER_SIZE) {
                if (!ApxFrame.isMagic(rxBuf, off, rxLen)) { off++; continue }
                val payloadLen = ApxFrame.payloadLenAt(rxBuf, off)
                if (payloadLen < 0 || payloadLen > ApxFrame.MAX_PAYLOAD) { off++; continue }
                val total = ApxFrame.totalSize(payloadLen)
                if (rxLen - off < total) break
                if (!ApxFrame.verify(rxBuf, off, payloadLen)) { off += total; continue }
                if (ApxFrame.streamIdAt(rxBuf, off) == ApxFrame.STREAM_CONTROL) {
                    val body = ApxFrame.bodyAt(rxBuf, off, payloadLen)
                    if (body != null) out.add(body)
                }
                off += total
            }
            if (off > 0) { System.arraycopy(rxBuf, off, rxBuf, 0, rxLen - off); rxLen -= off }
        }
        return out
    }

    private fun handleServerFrame(body: ByteArray) {
        if (body.isEmpty()) return
        when (body[0].toInt() and 0xFF) {
            'p'.code -> { }
            0x21 -> {
                val len = (body.getOrElse(1) { 0 }.toInt() and 0xFF) or ((body.getOrElse(2) { 0 }.toInt() and 0xFF) shl 8)
                if (body.size >= 3 + len && len > 0) { onReverseClipboard?.invoke(String(body.copyOfRange(3, 3 + len), Charsets.UTF_8)) }
            }
            0x25 -> {
                if (body.size < 2) return
                val hintLen = body[1].toInt() and 0xFF
                if (body.size < 2 + hintLen) return
                val hint = String(body.copyOfRange(2, 2 + hintLen), Charsets.UTF_8)
                val cb = onRemoteInputRequest
                if (cb != null) cb(host, hint) else handleRequestInputFallback(hint)
            }
            0x26 -> {
                if (body.size < 4) return
                val flags = body[1].toInt() and 0xFF
                val textLen = (body[2].toInt() and 0xFF) or ((body[3].toInt() and 0xFF) shl 8)
                if (body.size < 4 + textLen) return
                val text = String(body.copyOfRange(4, 4 + textLen), Charsets.UTF_8)
                val cb = onRemoteInputText
                if (cb != null) cb(text, flags) else injectRemoteTextFallback(text, flags)
            }
            0x27 -> { onRemoteInputDone?.invoke() }
        }
    }

    private fun handleRequestInputFallback(hint: String) {
        val ctx = com.allperiph.App.context
        if (ctx == null) { Log.w("TvCtrl", "收到代输请求，但 App.context 未就绪"); return }
        android.os.Handler(android.os.Looper.getMainLooper()).post {
            runCatching {
                com.allperiph.controlled.RemoteInputActivity.start(ctx, host, hint, fromClient = true)
            }.onFailure { Log.w("TvCtrl", "代输弹窗失败：${it.message}") }
        }
    }

    private fun injectRemoteTextFallback(text: String, flags: Int) {
        val svc = com.allperiph.shared.accessibility.ApxAccessibilityService.instance
        if (svc == null) { Log.w("TvCtrl", "代输没落地：无障碍服务未就绪"); return }
        when {
            flags and ApxFrame.INPUT_FLAG_BACKSPACE != 0 -> svc.deleteChar()
            flags and ApxFrame.INPUT_FLAG_CANCEL != 0 -> Unit
            else -> if (text.isNotEmpty() && !svc.typeText(text) && !svc.paste(text)) {
                Log.w("TvCtrl", "代输注入失败（输入框没聚焦？）")
            }
        }
    }

    private fun pingLoop() {
        while (running.get()) {
            sendControl("ping".toByteArray(Charsets.UTF_8))
            try { Thread.sleep(1000) } catch (_: InterruptedException) { break }
        }
    }

    private fun startTx() {
        if (txThread?.isAlive == true) return
        txThread = thread(name = "tvctrl-tx") {
            while (running.get()) {
                val frame = try { txQueue.take() } catch (_: InterruptedException) { break }
                val target = out
                if (target == null) continue
                try { target.write(frame); target.flush() } catch (t: Throwable) {
                    if (out === target) {
                        Log.w("TvCtrl", "写出失败：${t.message} → 关闭连接触发自动重连")
                        ready = false
                        runCatching { sock?.close() }
                    } else {
                        Log.w("TvCtrl", "忽略旧连接残留写失败：${t.message}")
                    }
                }
            }
        }
    }

    fun sendControl(body: ByteArray): Boolean {
        if (!ready) {
            val now = System.currentTimeMillis()
            if (now - lastDropLogAt >= 5000) {
                lastDropLogAt = now
                Log.w("TvCtrl", "连接未就绪，丢弃输入帧（5s 内仅记一次）")
            }
            return false
        }
        val frame = synchronized(writeLock) { seq = (seq + 1) and 0x7FFFFFFF; ApxFrame.build(ApxFrame.STREAM_CONTROL, body, seq) }
        while (txQueue.size > 512) txQueue.poll()
        return txQueue.offer(frame)
    }

    /** P0-1：dx/dy/wheel 各占一个有符号字节，超出 [-127,127] 会回绕成反向 —— 这里钳位兜底；被钳掉的余数由发送源端（GestureEngine 的 fracX/fracY 亚像素累加器）带入下一帧，不丢位移
     *  第 6 字节 pan（横向滚动，右为正）：v223.1 起无线出口也带上，此前只有 USB/蓝牙出口带。
     *  老 PC 端按 bodyLen>=5 读前 5 字节、忽略多出来的字节；老手机端同理 —— 双向兼容。 */
    fun mouse(buttons: Int, dx: Int, dy: Int, wheel: Int = 0, pan: Int = 0) = sendControl(byteArrayOf(0x01.toByte(), buttons.toByte(), dx.coerceIn(-127, 127).toByte(), dy.coerceIn(-127, 127).toByte(), wheel.coerceIn(-127, 127).toByte(), pan.coerceIn(-127, 127).toByte()))
    fun touch(action: Int, buttons: Int, x: Int, y: Int) = sendControl(byteArrayOf(0x04.toByte(), action.toByte(), buttons.toByte(), (x and 0xFF).toByte(), ((x shr 8) and 0xFF).toByte(), (y and 0xFF).toByte(), ((y shr 8) and 0xFF).toByte(), 0, 0))
    /** v184：特殊键/组合键（0x28）：mod 位图 1=Ctrl 2=Shift 4=Alt，vk 用 Windows VK 码 */
    fun sendSpecialKey(mod: Int, vk: Int) = sendControl(byteArrayOf(0x28.toByte(), mod.toByte(), vk.toByte()))
    fun keyboard(usage: Int, down: Boolean, mod: Int = 0) = if (down) sendControl(byteArrayOf(0x03.toByte(), mod.toByte(), 0, usage.toByte())) else sendControl(byteArrayOf(0x03.toByte(), 0, 0, 0))
    fun consumer(bitmap: Int) = sendControl(byteArrayOf(0x02.toByte(), (bitmap and 0xFF).toByte(), ((bitmap ushr 8) and 0xFF).toByte()))
    fun power(action: Int) = sendControl(byteArrayOf(0x22.toByte(), action.toByte()))
    fun gamepad(buttons: Int, x: Int, y: Int, rx: Int, ry: Int) = sendControl(byteArrayOf(0x07.toByte(), (buttons and 0xFF).toByte(), ((buttons ushr 8) and 0xFF).toByte(), x.toByte(), y.toByte(), rx.toByte(), ry.toByte()))
    fun sendClipboard(text: String): Boolean {
        if (text.isEmpty()) return false
        val bytes = text.toByteArray(Charsets.UTF_8); if (bytes.size > 0xFFFF) return false
        val body = ByteArray(3 + bytes.size); body[0] = 0x20.toByte(); body[1] = (bytes.size and 0xFF).toByte(); body[2] = ((bytes.size ushr 8) and 0xFF).toByte(); bytes.copyInto(body, 3)
        return sendControl(body)
    }

    // 远程输入发送
    fun requestInput(hint: String) {
        val hintBytes = hint.toByteArray(Charsets.UTF_8)
        val body = ByteArray(1 + 1 + hintBytes.size)
        body[0] = ApxFrame.INPUT_REQUEST.toByte()
        body[1] = hintBytes.size.toByte()
        hintBytes.copyInto(body, 2)
        sendControl(body)
    }

    fun sendInputText(text: String, flags: Int = ApxFrame.INPUT_FLAG_INCREMENTAL) {
        val textBytes = text.toByteArray(Charsets.UTF_8)
        val body = ByteArray(1 + 1 + 2 + textBytes.size)
        body[0] = ApxFrame.INPUT_TEXT.toByte(); body[1] = flags.toByte()
        body[2] = (textBytes.size and 0xFF).toByte(); body[3] = ((textBytes.size ushr 8) and 0xFF).toByte()
        textBytes.copyInto(body, 4)
        sendControl(body)
    }

    fun sendInputDone() { sendControl(byteArrayOf(ApxFrame.INPUT_DONE.toByte())) }

    /** 实体键盘状态上报（0x30）：[0x30, avail, nameLen, name...]，PC 端 web 面板据此显示。 */
    fun reportPadKeyboard(available: Boolean, name: String = "") {
        val nb = name.toByteArray(Charsets.UTF_8).take(255).toByteArray()
        val body = ByteArray(3 + nb.size)
        body[0] = 0x30.toByte()
        body[1] = (if (available) 1 else 0).toByte()
        body[2] = nb.size.toByte()
        nb.copyInto(body, 3)
        sendControl(body)
    }

    /** 检测本机实体键盘（排除软键盘），连接建立后上报给 PC。 */
    private fun reportPadKeyboardState() {
        val ctx = com.allperiph.App.context ?: return
        val im = ctx.getSystemService(android.content.Context.INPUT_SERVICE)
            as? android.hardware.input.InputManager ?: return
        var found = false
        var name = ""
        for (id in im.inputDeviceIds) {
            val d = im.getInputDevice(id) ?: continue
            if (id >= 0 && d.sources and android.view.InputDevice.SOURCE_KEYBOARD != 0) {
                found = true
                name = d.name
                break
            }
        }
        reportPadKeyboard(found, name)
    }

    companion object { const val PORT = 9511 }
}
