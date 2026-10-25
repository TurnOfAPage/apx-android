#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（反馈#10 批 C）：把散落的长描述文字收进帮助页。
  - page_status：删「功能说明卡」九行与「三步上手卡」三行（文字迁往帮助页）；
    boxIdleFeatures 容器**保留为空壳**（StatusFragment/MainActivity 经 findViewById 整卡
    引用，ensureViews 找不到会 return false / lateinit NPE；子 TextView 无 id，代码不可达）
  - page_settings：groupHelp 追加「功能说明」「三步上手」两卡（原文逐行搬运）
  - strings：新增两个卡标题
  - CHANGELOG 补条
保留：tvIdleHint（单行）、跟手程度卡（运行时诊断语境）。

⚠ 锚点铁律（run#5/#14 实锤）：锚点内容以双引号收尾时必须先换行再闭三引号。

两阶段：①按期望次数全量校验；②全过才写盘。失配 → 退出非零 → 不提交。
"""
import sys

FEATURE_LINES = """            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="8dp"
                android:text="@string/lay_status_text_touchpad_cursor_swipe"
                android:textAppearance="@style/APTextCaption" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="6dp"
                android:text="@string/lay_status_text_remote_keyboard"
                android:textAppearance="@style/APTextCaption" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="6dp"
                android:text="@string/lay_status_text_hotkey_template"
                android:textAppearance="@style/APTextCaption" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="6dp"
                android:text="@string/lay_status_text_volume_play"
                android:textAppearance="@style/APTextCaption" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="6dp"
                android:text="@string/lay_status_text_mic"
                android:textAppearance="@style/APTextCaption" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="6dp"
                android:text="@string/lay_status_text_speaker"
                android:textAppearance="@style/APTextCaption" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="6dp"
                android:text="@string/lay_idle_bluetooth"
                android:textAppearance="@style/APTextCaption" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="6dp"
                android:text="@string/lay_status_text_hotkey_touchpad_keyboard"
                android:textAppearance="@style/APTextCaption" />
"""

HOWTO_LINES = """            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="8dp"
                android:text="@string/lay_status_text_disconnect_open"
                android:textAppearance="@style/APTextCaption" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="6dp"
                android:text="@string/lay_status_text_open_menu"
                android:textAppearance="@style/APTextCaption" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="6dp"
                android:text="@string/lay_status_text_hotkey_mic_touchpad_keyboard"
                android:textAppearance="@style/APTextCaption" />
"""

NEW_HELP_SECTIONS = """
            <!-- 批C：功能说明（原状态页 boxIdleFeatures 九行搬来，原文零改动） -->
            <TextView
                style="@style/APSectionTitle"
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:text="@string/ui_help_title_features" />

            <LinearLayout
                style="@style/APCard"
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:orientation="vertical">

""" + FEATURE_LINES + """            </LinearLayout>

            <!-- 批C：三步上手（原状态页常显卡搬来，原文零改动） -->
            <TextView
                style="@style/APSectionTitle"
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:text="@string/ui_help_title_howto" />

            <LinearLayout
                style="@style/APCard"
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:orientation="vertical">

""" + HOWTO_LINES + """            </LinearLayout>
        </LinearLayout>
"""

EDITS = [
    # ————— 1. page_status：功能说明卡 → 空壳占位（Kotlin 整卡引用保留） —————
    ("android/app/src/main/res/layout/page_status.xml", [
        ("""        <!-- 未启动时的功能说明：打开开关前先让用户知道能得到什么（启动后自动隐藏） -->
        <LinearLayout
            android:id="@+id/boxIdleFeatures"
            style="@style/APCard"
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:orientation="vertical">

            <TextView
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:text="@string/lay_status_text_open"
                android:textAppearance="@style/APTextSubtitle" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="8dp"
                android:text="@string/lay_status_text_touchpad_cursor_swipe"
                android:textAppearance="@style/APTextCaption" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="6dp"
                android:text="@string/lay_status_text_remote_keyboard"
                android:textAppearance="@style/APTextCaption" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="6dp"
                android:text="@string/lay_status_text_hotkey_template"
                android:textAppearance="@style/APTextCaption" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="6dp"
                android:text="@string/lay_status_text_volume_play"
                android:textAppearance="@style/APTextCaption" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="6dp"
                android:text="@string/lay_status_text_mic"
                android:textAppearance="@style/APTextCaption" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="6dp"
                android:text="@string/lay_status_text_speaker"
                android:textAppearance="@style/APTextCaption" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="6dp"
                android:text="@string/lay_idle_bluetooth"
                android:textAppearance="@style/APTextCaption" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="6dp"
                android:text="@string/lay_status_text_hotkey_touchpad_keyboard"
                android:textAppearance="@style/APTextCaption" />
        </LinearLayout>
""",
         """        <!-- 批C：功能说明九行已搬进帮助页 groupHelp；本容器保留为空壳——
             StatusFragment ensureViews 找不到会 return false、MainActivity lateinit 会 NPE，
             子 TextView 无 id 故代码不可达；透明空壳高为 0、零渲染。 -->
        <LinearLayout
            android:id="@+id/boxIdleFeatures"
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:background="@android:color/transparent"
            android:orientation="vertical"
            android:padding="0dp" />
""", 1),
        # 2. 三步上手卡整体删除（无 id，无任何代码引用）
        ("""        <!-- 三步上手：开启后用户经常不知道下一步，这里给固定引导（常显，方便随时回看） -->
        <LinearLayout
            style="@style/APCard"
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:orientation="vertical">

            <TextView
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:text="@string/lay_howto_title"
                android:textAppearance="@style/APTextSubtitle" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="8dp"
                android:text="@string/lay_status_text_disconnect_open"
                android:textAppearance="@style/APTextCaption" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="6dp"
                android:text="@string/lay_status_text_open_menu"
                android:textAppearance="@style/APTextCaption" />

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingTop="6dp"
                android:text="@string/lay_status_text_hotkey_mic_touchpad_keyboard"
                android:textAppearance="@style/APTextCaption" />
        </LinearLayout>
""",
         "        <!-- 批C：三步上手三行已搬进帮助页 groupHelp -->\n", 1),
    ]),
    # ————— 3. 帮助页追加两卡（原文逐行搬运） —————
    ("android/app/src/main/res/layout/page_settings.xml", [
        ("""                <TextView
                    android:layout_width="match_parent"
                    android:layout_height="wrap_content"
                    android:text="@string/ui_help_faq"
                    android:textColor="#CC000000"
                    android:textSize="13sp" />
            </LinearLayout>
        </LinearLayout>
""",
         """                <TextView
                    android:layout_width="match_parent"
                    android:layout_height="wrap_content"
                    android:text="@string/ui_help_faq"
                    android:textColor="#CC000000"
                    android:textSize="13sp" />
            </LinearLayout>
""" + NEW_HELP_SECTIONS, 1),
    ]),
    # ————— 4. 两张新卡标题 —————
    ("android/app/src/main/res/values/strings.xml", [
        ("</resources>", """    <string name="ui_help_title_features">功能说明</string>
    <string name="ui_help_title_howto">三步上手</string>
</resources>""", 1),
    ]),
    # ————— 5. CHANGELOG —————
    ("docs/CHANGELOG.md", [
        ("  进设置页一律落回分类首页\n",
         """  进设置页一律落回分类首页
- **长描述收进帮助页（反馈#10 批 C）**：状态页「功能说明」九行与「三步上手」三行
  原文搬进帮助组新卡（功能说明 / 三步上手）；boxIdleFeatures 保留透明空壳
  （Kotlin 整卡引用：找不到会 ensureViews 失败 / lateinit NPE），三步卡无 id 直接删；
  单行提示与运行时诊断语境文字保留
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
