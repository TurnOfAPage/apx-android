#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（批 O-fix · 修构建失败）：SettingsFragment 导出日志 head 串的换行转义修正。
  根因：批 O 脚本三引号把 Kotlin 串内 \n 写成了真实换行——单行字符串禁换行，
        222~227 连锁语法错（时间/设备/说明逐行裸奔）。
  修：真实换行 → 字面反斜杠 n（python 内用双反斜杠表示）。

两阶段：①按期望次数全量校验；②全过才写盘。
"""
import sys

EDITS = [
    ("android/app/src/main/java/com/allperiph/ui/SettingsFragment.kt", [
        ("""                        val head = "=== AllPeriph 运行日志导出 ===
时间: $stamp
设备: ${android.os.Build.MODEL} / SDK ${android.os.Build.VERSION.SDK_INT}
说明: 仅含本进程最近 5000 行 logcat

"
""",
         """                        val head = "=== AllPeriph 运行日志导出 ===\\n时间: $stamp\\n设备: ${android.os.Build.MODEL} / SDK ${android.os.Build.VERSION.SDK_INT}\\n说明: 仅含本进程最近 5000 行 logcat\\n\\n"
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
