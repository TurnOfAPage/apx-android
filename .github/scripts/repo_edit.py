#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（批 K · 深色取色修复）：硬编码黑色文字 → 主题 token。
  - page_settings：#DD000000 → @color/md_on_surface（行标题）；
    #CC000000 → @color/md_on_surface_variant（帮助卡正文）
  - page_localsend_send：#99000000/#66000000 → variant（说明行/空态）
  - 深色下硬编码黑=黑底黑字看不清（issue 实测：悬浮球与设置页深色字看不清）
  - runner 特例：expected=-1 表示只要求 >0（多处字面量替换用）

两阶段：①按规则全量校验；②全过才写盘。
"""
import sys

EDITS = [
    ("android/app/src/main/res/layout/page_settings.xml", [
        ("android:textColor=\"#DD000000\"", "android:textColor=\"@color/md_on_surface\"", -1),
        ("android:textColor=\"#CC000000\"", "android:textColor=\"@color/md_on_surface_variant\"", -1),
    ]),
    ("android/app/src/main/res/layout/page_localsend_send.xml", [
        ("android:textColor=\"#99000000\"", "android:textColor=\"@color/md_on_surface_variant\"", -1),
        ("android:textColor=\"#66000000\"", "android:textColor=\"@color/md_on_surface_variant\"", -1),
    ]),
    ("docs/CHANGELOG.md", [
        ("  触控开关保留为接收开关——开关=接收、点行=发送\n",
         """  触控开关保留为接收开关——开关=接收、点行=发送
- **深色模式取色修复（批 K）**：设置页分类/行/帮助卡与互传发送页的硬编码黑色
  文字全部换主题 token（md_on_surface / md_on_surface_variant，项目按 4.5:1 对比度
  校准）——修深色下黑字看不清；悬浮球字体问题随球体样式批一并修
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
            if expected >= 0:
                if n != expected:
                    errors.append(
                        f"{path} edit#{i}: anchor count={n} (expect {expected})\n---anchor---\n{old[:400]}\n---"
                    )
            elif n == 0:
                errors.append(f"{path} edit#{i}: literal not found (expected >=1): {old}")
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
