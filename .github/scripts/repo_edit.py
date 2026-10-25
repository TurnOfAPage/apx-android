#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（批 L' · 修 run#24 失配重发）：诊断日志包。
  修：forwardPointer 签名参数是全限定 android.view.MotionEvent（文件未 import 短名）
  修：CHANGELOG 锚改批M 条目尾（批L 首跑未写入的连锁依赖解除）
  - forward 包装：每键打 keyCode/action/usage/mod/EditText焦点/结果
  - forwardPointer 包装：source/action/坐标/双指轴/焦点/结果
  - motion Wrapper：isPointer/handled
  - 全部 android.util.Log；诊断期临时，下版移除

两阶段：①按期望次数全量校验；②全过才写盘。
"""
import sys

EDITS = [
    ("android/app/src/main/java/com/allperiph/ui/PhysKeyBridge.kt", [
        ("""    private fun forward(event: KeyEvent): Boolean {
""",
         """    private fun forward(event: KeyEvent): Boolean {
        val ret = forwardImpl(event)
        android.util.Log.d("ApxBridge", "KEY kc=${event.keyCode} act=${event.action} rep=${event.repeatCount} usage=${usageOf(event.keyCode)} mod=${modBit(event.keyCode)} etFocus=${act.currentFocus is EditText} ret=$ret")
        return ret
    }

    private fun forwardImpl(event: KeyEvent): Boolean {
""", 1),
        ("""    private fun forwardPointer(event: android.view.MotionEvent): Boolean {
""",
         """    private fun forwardPointer(event: android.view.MotionEvent): Boolean {
        val ret = forwardPointerImpl(event)
        android.util.Log.d("ApxBridge", "PTR src=${event.source} act=${event.actionMasked} x=${event.x} y=${event.y} v=${event.getAxisValue(android.view.MotionEvent.AXIS_VSCROLL)} h=${event.getAxisValue(android.view.MotionEvent.AXIS_HSCROLL)} etFocus=${act.currentFocus is EditText} ret=$ret")
        return ret
    }

    private fun forwardPointerImpl(event: android.view.MotionEvent): Boolean {
""", 1),
        ("""            if (isPointer) applyCursorLock(handled)
            return if (handled) true else delegate.dispatchGenericMotionEvent(event)
""",
         """            if (isPointer) applyCursorLock(handled)
            android.util.Log.d("ApxBridge", "MOTION isPointer=$isPointer handled=$handled src=${event.source} act=${event.actionMasked}")
            return if (handled) true else delegate.dispatchGenericMotionEvent(event)
""", 1),
    ]),
    ("docs/CHANGELOG.md", [
        ("  48dp→56dp（触控目标更好点），中心点改深色；修实测「太小/像一个点」\n",
         """  48dp→56dp（触控目标更好点），中心点改深色；修实测「太小/像一个点」
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
