#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（批 L-fix · 修构建失败）：ApxBridge KEY 日志行去掉 mod 字段。
  根因：modBit 是 forwardImpl 内部局部变量（val modBit = when…），包装函数不在作用域；
  修饰键 keyCode 本身已在 kc= 字段可见（如 SHIFT=59/60），删字段零损失。

两阶段：①按期望次数全量校验；②全过才写盘。
"""
import sys

EDITS = [
    ("android/app/src/main/java/com/allperiph/ui/PhysKeyBridge.kt", [
        ("""        android.util.Log.d("ApxBridge", "KEY kc=${event.keyCode} act=${event.action} rep=${event.repeatCount} usage=${usageOf(event.keyCode)} mod=${modBit(event.keyCode)} etFocus=${act.currentFocus is EditText} ret=$ret")
""",
         """        android.util.Log.d("ApxBridge", "KEY kc=${event.keyCode} act=${event.action} rep=${event.repeatCount} usage=${usageOf(event.keyCode)} etFocus=${act.currentFocus is EditText} ret=$ret")
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
