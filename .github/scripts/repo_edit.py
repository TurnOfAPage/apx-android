#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（批 M' · 修 run#25 失配重发）：悬浮球实体化。
  修：①view锚2 属性缩进 16→8 空格（原文 8）；②CHANGELOG 锚改批K 尾（批L 未落库的连锁依赖解除）
  - bg_floating_ball：35% 黑半透明 → 95% 白实底 + 20% 黑描边
  - view：48→56dp；中心 ● 白字 → 深色；注释同步

两阶段：①按期望次数全量校验；②全过才写盘。
"""
import sys

EDITS = [
    ("android/app/src/main/res/drawable/bg_floating_ball.xml", [
        ("""    <solid android:color="#59000000" />
    <stroke
        android:width="1dp"
        android:color="#66FFFFFF" />
""",
         """    <solid android:color="#F2FFFFFF" />
    <stroke
        android:width="1dp"
        android:color="#33000000" />
""", 1),
    ]),
    ("android/app/src/main/res/layout/view_floating_ball.xml", [
        ("""<!-- 悬浮球：48dp 半透明圆球（反馈#7） -->
<FrameLayout xmlns:android="http://schemas.android.com/apk/res/android"
    android:layout_width="48dp"
    android:layout_height="48dp"
    android:background="@drawable/bg_floating_ball"
""",
         """<!-- 悬浮球：56dp 白实底圆球（反馈#7 + 实测改版） -->
<FrameLayout xmlns:android="http://schemas.android.com/apk/res/android"
    android:layout_width="56dp"
    android:layout_height="56dp"
    android:background="@drawable/bg_floating_ball"
""", 1),
        ("""        android:text="●"
        android:textSize="14sp"
        android:textColor="#FFFFFFFF" />
""",
         """        android:text="●"
        android:textSize="16sp"
        android:textColor="#CC000000" />
""", 1),
    ]),
    ("docs/CHANGELOG.md", [
        ("  校准）——修深色下黑字看不清；悬浮球字体问题随球体样式批一并修\n",
         """  校准）——修深色下黑字看不清；悬浮球字体问题随球体样式批一并修
- **悬浮球实体化（批 M）**：球体 35% 黑半透明 → 95% 白实底 + 描边（深浅背景都醒目），
  48dp→56dp（触控目标更好点），中心点改深色；修实测「太小/像一个点」
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
