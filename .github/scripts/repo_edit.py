#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（修复 #7 编译失败）：settingRow 的 onClick 是 () -> Unit（无参数），
上一批误用了 it.context → Unresolved reference: it → :app:compileReleaseKotlin 挂。
改为 activity?.let 形式（Fragment 取 Context 的惯用法）。

两阶段：①按期望次数全量校验；②全过才写盘。失配 → 退出非零 → 不提交。
"""
import sys

EDITS = [
    ("android/app/src/main/java/com/allperiph/ui/SettingsFragment.kt", [
        ("                FloatingBallService.requestToggle(it.context)\n",
         """                activity?.let { ctx -> FloatingBallService.requestToggle(ctx) }
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
