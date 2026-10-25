#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（反馈#3 miuix 排版重排，三处目标）：
  T1 触控板页手势区卡片排版 / T2 设置页快捷键模板列表 / T3 EnvChecks 诊断页对齐间距。
miuix 依据（compose-miuix-ui/miuix，Card.kt 实读）：CardDefaults.CornerRadius = 16.dp、
InsideMargin = 0（内容层统一给）、间距落 4dp 栅格。

两阶段：①对全部锚点按**期望次数**校验（任何失配一次性全部列出）；②全部通过才写盘。
条目 = (路径, [(old, new, 期望命中次数), ...])；次数 >1 时全量替换。
任何失配 → 退出非零 → 不产生提交。
"""
import sys

EDITS = [
    # ————— T1/T3 全局卡片令牌：18dp → 16dp（对齐 miuix CardDefaults.CornerRadius=16dp） —————
    ("android/app/src/main/res/values/styles.xml", [
        ("""    <!-- 卡片：18dp 圆角白卡 + 极浅描边（MIUIX 卡片） -->
    <style name="APCard">
        <item name="android:background">@drawable/bg_card</item>
        <item name="android:orientation">vertical</item>
        <item name="android:padding">18dp</item>
        <item name="android:layout_marginBottom">12dp</item>
    </style>""",
         """    <!-- 卡片：16dp 圆角白卡 + 极浅描边（对齐 miuix CardDefaults.CornerRadius=16dp） -->
    <style name="APCard">
        <item name="android:background">@drawable/bg_card</item>
        <item name="android:orientation">vertical</item>
        <item name="android:padding">16dp</item>
        <item name="android:layout_marginBottom">12dp</item>
    </style>""", 1),
        ("""    <!-- 低强调卡片（说明性内容） -->
    <style name="APCardFlat">
        <item name="android:background">@drawable/bg_card_flat</item>
        <item name="android:orientation">vertical</item>
        <item name="android:padding">18dp</item>
        <item name="android:layout_marginBottom">12dp</item>
    </style>""",
         """    <!-- 低强调卡片（说明性内容，16dp 圆角对齐 miuix） -->
    <style name="APCardFlat">
        <item name="android:background">@drawable/bg_card_flat</item>
        <item name="android:orientation">vertical</item>
        <item name="android:padding">16dp</item>
        <item name="android:layout_marginBottom">12dp</item>
    </style>""", 1),
    ]),
    ("android/app/src/main/res/drawable/bg_card.xml", [
        ("""<!-- MIUIX 卡片：18dp 圆角白卡 + 极浅描边（色值全部来自全局色板） -->
<shape xmlns:android="http://schemas.android.com/apk/res/android" android:shape="rectangle">
    <solid android:color="@color/md_surface" />
    <corners android:radius="18dp" />""",
         """<!-- miuix 卡片：16dp 圆角白卡 + 极浅描边（CardDefaults.CornerRadius=16dp；色值全部来自全局色板） -->
<shape xmlns:android="http://schemas.android.com/apk/res/android" android:shape="rectangle">
    <solid android:color="@color/md_surface" />
    <corners android:radius="16dp" />""", 1),
    ]),
    ("android/app/src/main/res/drawable/bg_card_flat.xml", [
        ("""<!-- MIUIX 低强调卡片：18dp 圆角，描边更浅 -->
<shape xmlns:android="http://schemas.android.com/apk/res/android" android:shape="rectangle">
    <solid android:color="@color/md_surface" />
    <corners android:radius="18dp" />""",
         """<!-- miuix 低强调卡片：16dp 圆角，描边更浅 -->
<shape xmlns:android="http://schemas.android.com/apk/res/android" android:shape="rectangle">
    <solid android:color="@color/md_surface" />
    <corners android:radius="16dp" />""", 1),
    ]),
    # ————— T3 诊断页/全页分区标题节奏统一：去掉硬编码 paddingTop=10dp，
    #       全部回归「上一张卡 marginBottom 12dp」的统一节奏（原状 12/10/8 混用，
    #       诊断区等 12 个标题 10dp、2 个标题 0dp，就是“间距混乱”来源） —————
    ("android/app/src/main/res/layout/page_settings.xml", [
        ("""            style="@style/APSectionTitle"
            android:layout_width="wrap_content"
            android:layout_height="wrap_content"
            android:paddingTop="10dp"""",
         """            style="@style/APSectionTitle"
            android:layout_width="wrap_content"
            android:layout_height="wrap_content""", 10),
        ("""                    style="@style/APSectionTitle"
                    android:layout_width="wrap_content"
                    android:layout_height="wrap_content"
                    android:paddingTop="10dp"""",
         """                    style="@style/APSectionTitle"
                    android:layout_width="wrap_content"
                    android:layout_height="wrap_content""", 2),
    ]),
    # ————— T1 触控板页手势区：间距归 4dp 栅格（6→8、14→12），上下块与卡片边对齐 —————
    ("android/app/src/main/res/layout/activity_main.xml", [
        ("""                    android:paddingStart="4dp"
                    android:paddingTop="6dp"
                    android:paddingEnd="4dp"
                    android:paddingBottom="4dp">""",
         """                    android:paddingStart="4dp"
                    android:paddingTop="8dp"
                    android:paddingEnd="4dp"
                    android:paddingBottom="4dp">""", 1),
        ("""                    android:paddingStart="4dp"
                    android:paddingTop="14dp"
                    android:paddingEnd="4dp"
                    android:paddingBottom="8dp">""",
         """                    android:paddingStart="4dp"
                    android:paddingTop="12dp"
                    android:paddingEnd="4dp"
                    android:paddingBottom="8dp">""", 1),
    ]),
    # ————— T2 设置页快捷键模板列表：左右缩进与页面 16dp 栅格对齐（12/8 → 16/16）、
    #       描述行间距 3→4、删除按钮右缘与计数文字齐平 —————
    ("android/app/src/main/java/com/allperiph/ui/SettingsFragment.kt", [
        ("                setPadding(h.dp(12), h.dp(12), h.dp(8), h.dp(12))",
         "                setPadding(h.dp(16), h.dp(12), h.dp(16), h.dp(12))", 1),
        ("            }, LinearLayout.LayoutParams(-2, -2).apply { topMargin = h.dp(3) })",
         "            }, LinearLayout.LayoutParams(-2, -2).apply { topMargin = h.dp(4) })", 1),
        ("                    setPadding(h.dp(12), h.dp(6), h.dp(6), h.dp(6))",
         "                    setPadding(h.dp(12), h.dp(6), 0, h.dp(6))", 1),
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
