#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（批 L · 诊断日志包）：为实体键盘/触摸板问题抓日志（issue 实测 1/2/6 号问题）。
  - forward 包装：每键打 keyCode/action/usage/mod/EditText焦点/结果（区分放行①/消费③/转发⑤）
  - forwardPointer 包装：source/action/坐标/双指轴 v·h/焦点/结果（定位双指无效与点击路径）
  - motion Wrapper：isPointer/handled（定位 source 判定放行）
  - 全部 android.util.Log（release 必出）；诊断期每事件一行，下版移除
  - CHANGELOG 注明

两阶段：①按期望次数全量校验；②全过才写盘。
"""
import sys

EDITS = [
    ("android/app/src/main/java/com/allperiph/ui/PhysKeyBridge.kt", [
        # 1. 键盘转发包装
        ("""    private fun forward(event: KeyEvent): Boolean {
""",
         """    private fun forward(event: KeyEvent): Boolean {
        val ret = forwardImpl(event)
        android.util.Log.d("ApxBridge", "KEY kc=${event.keyCode} act=${event.action} rep=${event.repeatCount} usage=${usageOf(event.keyCode)} mod=${modBit(event.keyCode)} etFocus=${act.currentFocus is EditText} ret=$ret")
        return ret
    }

    private fun forwardImpl(event: KeyEvent): Boolean {
""", 1),
        # 2. 指针转发包装（双指轴与位置）
        ("""    private fun forwardPointer(event: MotionEvent): Boolean {
""",
         """    private fun forwardPointer(event: MotionEvent): Boolean {
        val ret = forwardPointerImpl(event)
        android.util.Log.d("ApxBridge", "PTR src=${event.source} act=${event.actionMasked} x=${event.x} y=${event.y} v=${event.getAxisValue(android.view.MotionEvent.AXIS_VSCROLL)} h=${event.getAxisValue(android.view.MotionEvent.AXIS_HSCROLL)} etFocus=${act.currentFocus is EditText} ret=$ret")
        return ret
    }

    private fun forwardPointerImpl(event: MotionEvent): Boolean {
""", 1),
        # 3. motion Wrapper：source 判定打点（批F 改过的体）
        ("""            if (isPointer) applyCursorLock(handled)
            return if (handled) true else delegate.dispatchGenericMotionEvent(event)
""",
         """            if (isPointer) applyCursorLock(handled)
            android.util.Log.d("ApxBridge", "MOTION isPointer=$isPointer handled=$handled src=${event.source} act=${event.actionMasked}")
            return if (handled) true else delegate.dispatchGenericMotionEvent(event)
""", 1),
    ]),
    ("docs/CHANGELOG.md", [
        ("  校准）——修深色下黑字看不清；悬浮球字体问题随球体样式批一并修\n",
         """  校准）——修深色下黑字看不清；悬浮球字体问题随球体样式批一并修
- **诊断日志包（批 L · 临时，下版移除）**：键盘/指针/motion 三入口逐事件打点
  （keyCode/usage/mod、source/action/双指轴、焦点与分支结果）——用于定位实体键盘
  功能键、双指手势、触控板点击误触三类实测问题（logcat tag=ApxBridge）
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
