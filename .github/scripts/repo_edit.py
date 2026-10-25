#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（批 Q · 设置页重排 B1a，issue 实测「文字说明太长」）：
  - 删 4 个说明 caption（模板/配色/背景/诊断提示），**原文引用**搬进帮助页对应卡：
      配色+背景 → 「功能说明」卡尾（批C 结构）
      诊断提示 → 「链路与延迟」卡尾
      模板用法 → 「手势与操作」卡尾（模板操作与手势同属操作类）
  - 保留：诊断输出 tvDiag（动态内容）、全部内嵌选择器（功能本体不动，避免长按插入手势丢失）
  - CHANGELOG 补条

⚠ 铁律④：python 三引号会把单反斜杠 n 转成真实换行——Kotlin/Java 串内 \\n 写 \\\\n。
⚠ 铁律：锚点以双引号收尾必须先换行再闭三引号。
两阶段：①按期望次数全量校验；②全过才写盘。
"""
import sys

CAP_TEMPLATE = """            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:text="@string/lay_settings_text_hotkey_template_touchpad"
                android:textAppearance="@style/APTextCaption" />
"""
CAP_THEME = """            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:text="@string/lay_settings_text_hotkey_theme_touchpad_keyboard"
                android:textAppearance="@style/APTextCaption" />
"""
CAP_BG = """            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:text="@string/lay_settings_text_background_2"
                android:textAppearance="@style/APTextCaption" />
"""
CAP_DIAG = """            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:text="@string/lay_settings_diag_hint"
                android:textAppearance="@style/APTextCaption" />
"""

MOVED_HINT = "        <!-- 批Q：说明文字已收编帮助页（issue 实测「文字说明太长」） -->\n"

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
        # 1-4. 删四张说明 caption（各 text 引用唯一）→ 注释占位
        (CAP_TEMPLATE, MOVED_HINT, 1),
        (CAP_THEME, MOVED_HINT, 1),
        (CAP_BG, MOVED_HINT, 1),
        (CAP_DIAG, MOVED_HINT, 1),
        # 5. 配色+背景 caption 原文搬进「功能说明」卡尾（批C 结构锚）
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
        # 6. 模板用法 caption 搬进「手势与操作」卡尾（卡内 FAQ 前）
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
        # 7. 诊断提示 caption 搬进「链路与延迟」卡尾
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
  说明 caption（模板/配色/背景/诊断提示）原文搬进帮助页对应卡（功能说明/手势/链路），
  分组页只留控件本体；诊断输出与内嵌选择器保留（功能不减）
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
