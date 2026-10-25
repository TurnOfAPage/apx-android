#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（批 N · USB HID 背压修复，issue#46 日志实锤）：
  问题：host 未取走上一报告时 sendInputReport 失败，旧实现直接丢帧——
        实体触摸板位移/双指/按钮大量丢失（日志高频「USB HID 写入被拒」）。
  方案：
    - 失败帧合并进 pending（位移/滚轮/按钮/媒体键；溢出保留 ≤127 不丢）
    - 下一帧发送时自动并入 pending（pointer 幂等）
    - 失败后 16ms 补发 tick；连续 miss>64（≈1s）放弃清空防 host 长期不取空转
    - 按钮状态 lastSentButtons 兜底，重试帧不丢点击
  改动：TouchpadModule 单文件（字段组 + USB 分支 + scheduleUsbRetry + CHANGELOG）

两阶段：①按期望次数全量校验；②全过才写盘。
"""
import sys

PENDING_FIELDS = """    private var ptrButtons = 0

    // 批N：USB 背压合并——被拒帧的位移/滚轮/按钮/媒体键累积到下帧或 16ms 补发（pointer 幂等不丢位置）
    private var pdDx = 0
    private var pdDy = 0
    private var pdWheel = 0
    private var pdPan = 0
    private var pdConsumer = 0
    private var pdButtons = -1
    private var lastSentButtons = 0
    @Volatile private var retryScheduled = false
    @Volatile private var retryMiss = 0
"""

USB_BRANCH = """            Uplink.USB -> {
                // 批N：背压合并——失败帧位移/滚轮/按钮/媒体键并入下帧；溢出保留
                val rawX = f.dx + pdDx; val rawY = f.dy + pdDy
                val rawW = f.wheel + pdWheel; val rawP = f.pan + pdPan
                val dx = rawX.coerceIn(-127, 127); val dy = rawY.coerceIn(-127, 127)
                val wh = rawW.coerceIn(-127, 127); val pn = rawP.coerceIn(-127, 127)
                val cons = if (f.consumer != 0) f.consumer else pdConsumer
                val btns = if (pdButtons >= 0) pdButtons else f.buttons
                val ok = if (cons != 0) {
                    ctx.hid.sendInputReport(
                        byteArrayOf(0x04, cons.toByte(), (cons shr 8).toByte(), 0)
                    )
                } else {
                    ctx.hid.sendInputReport(
                        byteArrayOf(0x02, btns.toByte(), dx.toByte(), dy.toByte(), wh.toByte(), pn.toByte())
                    )
                }
                if (ok) {
                    pdDx = 0; pdDy = 0; pdWheel = 0; pdPan = 0; pdConsumer = 0; pdButtons = -1
                    retryMiss = 0
                    lastSentButtons = btns
                    Uplink.set(path)
                } else {
                    pdDx = rawX - dx; pdDy = rawY - dy
                    pdWheel = rawW - wh; pdPan = rawP - pn
                    if (cons != 0) pdConsumer = cons
                    pdButtons = btns
                    val why = "USB HID 写入被拒（主机未取走上一报告或节点异常）"
                    if (Uplink.reason != why) Log.w(TAG, why)
                    Uplink.set(path, why)
                    scheduleUsbRetry()
                }
            }
"""

RETRY_FN = """    /** 批N：USB 背压补发——16ms 后携带积压重试；连续 miss>64（≈1s）放弃防空转 */
    private fun scheduleUsbRetry() {
        if (retryScheduled) return
        retryScheduled = true
        android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
            retryScheduled = false
            val ctx = ctxRef ?: return@postDelayed
            val has = pdDx != 0 || pdDy != 0 || pdWheel != 0 || pdPan != 0 || pdConsumer != 0 || pdButtons >= 0
            if (!has) { retryMiss = 0; return@postDelayed }
            if (retryMiss > 64) {
                pdDx = 0; pdDy = 0; pdWheel = 0; pdPan = 0; pdConsumer = 0; pdButtons = -1
                retryMiss = 0
                Log.w(TAG, "USB 背压持续被拒，积压已放弃（host 长期未取报告）")
                return@postDelayed
            }
            retryMiss++
            val btns = if (pdButtons >= 0) pdButtons else lastSentButtons
            val ok = if (pdConsumer != 0) {
                ctx.hid.sendInputReport(byteArrayOf(0x04, pdConsumer.toByte(), (pdConsumer shr 8).toByte(), 0))
            } else {
                ctx.hid.sendInputReport(byteArrayOf(0x02, btns.toByte(), pdDx.toByte(), pdDy.toByte(), pdWheel.toByte(), pdPan.toByte()))
            }
            if (ok) {
                pdDx = 0; pdDy = 0; pdWheel = 0; pdPan = 0; pdConsumer = 0; pdButtons = -1
                retryMiss = 0
                lastSentButtons = btns
            } else scheduleUsbRetry()
        }, 16)
    }

"""

EDITS = [
    ("android/app/src/main/java/com/allperiph/touchpad/TouchpadModule.kt", [
        # 1. pending 字段组（声明行唯一）
        ("""    private var ptrButtons = 0
""",
         PENDING_FIELDS, 1),
        # 2. USB 分支整体替换（背压合并 + 溢出保留 + 补发调度）
        ("""            Uplink.USB -> {
                val ok = if (f.consumer != 0) {
                    ctx.hid.sendInputReport(
                        byteArrayOf(0x04, f.consumer.toByte(), (f.consumer shr 8).toByte(), 0)
                    )
                } else {
                    ctx.hid.sendInputReport(
                        byteArrayOf(0x02, f.buttons.toByte(), f.dx.toByte(), f.dy.toByte(), f.wheel.toByte(), f.pan.toByte())
                    )
                }
                if (ok) Uplink.set(path)
                else {
                    val why = "USB HID 写入被拒（主机未取走上一报告或节点异常）"
                    if (Uplink.reason != why) Log.w(TAG, why)
                    Uplink.set(path, why)
                }
            }
""",
         USB_BRANCH, 1),
        # 3. scheduleUsbRetry 插在 dispatch 定义前
        ("""    private fun dispatch(ctx: ModuleContext, f: TouchpadFrame) {
""",
         RETRY_FN + """    private fun dispatch(ctx: ModuleContext, f: TouchpadFrame) {
""", 1),
    ]),
    ("docs/CHANGELOG.md", [
        ("  反馈问题随 issue 附上即可，免接线 logcat\n",
         """  反馈问题随 issue 附上即可，免接线 logcat
- **USB HID 背压修复（批 N · issue#46 日志实锤）**：host 未取走上一报告时旧实现
  直接丢帧（日志高频「写入被拒」）——现失败帧合并进 pending（位移/滚轮/按钮/媒体键，
  溢出保留），下帧自动并入 + 16ms 补发重试，连续 1s 被拒才放弃；实体触摸板位移、
  双指滚动、点击不再因背压丢失
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
