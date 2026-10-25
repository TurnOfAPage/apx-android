#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（批 Q' · 修 run#33 失配重发，B1a）：
  修：锚1-4（5行 caption 块）基于本地副本失配 → 改**单行锚 + 插 visibility=gone**，
      完全豁免块结构/缩进差异；效果等价删除（代码保留可逆）。
  帮助承接三锚已用 search 实证（20空格属性/token色/close 12空格全对），原样保留。
  EDITS 顺序：4 caption 先改（此时承接未写入，单行锚唯一）→ 3 承接 → CHANGELOG。
  分组页只留控件；模板/配色/背景/诊断四句说明原文进帮助页。

⚠ 铁律④：Kotlin/Java 串内 \\n 写 \\\\n（python 三引号会转义单反斜杠）。
⚠ 铁律：锚以双引号收尾必须先换行再闭三引号。
两阶段：①按期望次数全量校验；②全过才写盘。
"""
import sys

CAP = [
    ("android:text=\"@string/lay_settings_text_hotkey_template_touchpad\"",
     "android:text=\"@string/lay_settings_text_hotkey_template_touchpad\"\n                android:visibility=\"gone\""),
    ("android:text=\"@string/lay_settings_text_hotkey_theme_touchpad_keyboard\"",
     "android:text=\"@string/lay_settings_text_hotkey_theme_touchpad_keyboard\"\n                android:visibility=\"gone\""),
    ("android:text=\"@string/lay_settings_text_background_2\"",
     "android:text=\"@string/lay_settings_text_background_2\"\n                android:visibility=\"gone\""),
    ("android:text=\"@string/lay_settings_diag_hint\"",
     "android:text=\"@string/lay_settings_diag_hint\"\n                android:visibility=\"gone\""),
]

HELP_CAP_THEME = """                <TextView
                    android:layout_width="match_parent"
                    android:layout_height="wrap_content"
                    android:text="@string/lay_settings_text_hotkey_theme_touchpad_keyboard"
                    android:textAppearance="@style/APTextCaption" />

                <TextView
                    android:layout_width="match_parent"
                    android:layout_height="wrap_content"
                    android:text="@string/lay_settings_text_background_2"
                    android:textAppearance="@style/APTextCaption" />
"""

HELP_CAP_TEMPLATE = """                <TextView
                    android:layout_width="match_parent"
                    android:layout_height="wrap_content"
                    android:text="@string/lay_settings_text_hotkey_template_touchpad"
                    android:textAppearance="@style/APTextCaption" />

"""

HELP_CAP_DIAG = """
                <TextView
                    android:layout_width="match_parent"
                    android:layout_height="wrap_content"
                    android:text="@string/lay_settings_diag_hint"
                    android:textAppearance="@style/APTextCaption" />
"""

EDITS = [
    ("android/app/src/main/res/layout/page_settings.xml", [
        # 1-4. caption 单行锚插 gone（顺序在承接前，单行引用唯一）
        (CAP[0][0], CAP[0][1], 1),
        (CAP[1][0], CAP[1][1], 1),
        (CAP[2][0], CAP[2][1], 1),
        (CAP[3][0], CAP[3][1], 1),
        # 5. 配色+背景 caption 原文搬「功能说明」卡尾（批C 结构锚，search 实证）
        ("""                android:text="@string/lay_status_text_hotkey_touchpad_keyboard"
                android:textAppearance="@style/APTextCaption" />
            </LinearLayout>

            <!-- 批C：三步上手（原状态页常显卡搬来，原文零改动） -->
""",
         """                android:text="@string/lay_status_text_hotkey_touchpad_keyboard"
                android:textAppearance="@style/APTextCaption" />

""" + HELP_CAP_THEME + """            </LinearLayout>

            <!-- 批C：三步上手（原状态页常显卡搬来，原文零改动） -->
""", 1),
        # 6. 模板用法 caption 搬「手势与操作」卡尾（20空格/close 12空格实证）
        ("""                android:text="@string/ui_help_gesture"
                android:textColor="@color/md_on_surface_variant"
                android:textSize="13sp" />
            </LinearLayout>
""",
         """                android:text="@string/ui_help_gesture"
                android:textColor="@color/md_on_surface_variant"
                android:textSize="13sp" />

""" + HELP_CAP_TEMPLATE + """            </LinearLayout>
""", 1),
        # 7. 诊断提示 caption 搬「链路与延迟」卡尾
        ("""                android:text="@string/ui_help_link"
                android:textColor="@color/md_on_surface_variant"
                android:textSize="13sp" />
            </LinearLayout>
""",
         """                android:text="@string/ui_help_link"
                android:textColor="@color/md_on_surface_variant"
                android:textSize="13sp" />
""" + HELP_CAP_DIAG + """            </LinearLayout>
""", 1),
    ]),
    ("docs/CHANGELOG.md", [
        ("  唯一换页方式」；原悬浮设置键并入 pill（永隐防重叠）\n",
         """  唯一换页方式」；原悬浮设置键并入 pill（永隐防重叠）
- **设置页长文收编（批 Q · issue 实测「文字说明太长」）**：通用/外观/工具组的 4 个
  说明 caption（模板/配色/背景/诊断提示）置为不可见并**原文搬进**帮助页对应卡
  （功能说明/手势/链路），分组页只留控件本体；诊断输出与内嵌选择器保留（功能不减）
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
