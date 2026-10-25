#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（批 E · 重发，修 run#18 锚点失配）：设置页「触摸板光标锁定」开关（issue#44）。
  - groupGeneral 首项插入 rowCursorLock（标题+副题+Switch 默认开）
  - SettingsFragment：act.findViewById 安全调用 + act.getSharedPreferences("apx")
  - 全限定名规避 import；bg_switch_thumb 不存在 → 纯 Switch 默认样式
  - 锚点全部改用唯一内容锚：
      page_settings → 模板注释单行（12sp/8sp 双 close 组合在 #3 改后 count 歧义）
      SettingsFragment → 批B 三行注释组合
      CHANGELOG → 批D 已验证锚行

⚠ 锚点铁律（run#5/#14 实锤）：锚点以双引号收尾必须先换行再闭三引号。
两阶段：①按期望次数全量校验；②全过才写盘。
"""
import sys

CURSOR_ROW = """        <!-- 批E：触摸板光标锁定（issue#44 · pad 光标跟随误触）；默认开 + 自动保存 -->
        <LinearLayout
            android:id="@+id/rowCursorLock"
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:orientation="horizontal"
            android:gravity="center_vertical"
            android:padding="12dp"
            android:background="@drawable/bg_card"
            android:layout_marginBottom="6dp">

            <TextView
                android:layout_width="0dp"
                android:layout_height="wrap_content"
                android:layout_weight="1"
                android:text="触摸板光标锁定\n开启后系统光标不随触控板移动，仅转发到电脑（Pad 防误触）"
                android:textColor="#DD000000"
                android:textSize="14sp" />

            <Switch
                android:id="@+id/switchCursorLock"
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:checked="true" />
        </LinearLayout>

"""

KOTLIN_BLOCK = """        // 批E：触摸板光标锁定开关（issue#44）；默认开，写 apx prefs 供触控板链路读取
        val cursorLockSwitch = act.findViewById<android.widget.Switch>(R.id.switchCursorLock)
        val apxPrefs = act.getSharedPreferences("apx", android.content.Context.MODE_PRIVATE)
        cursorLockSwitch?.isChecked = apxPrefs.getBoolean("cursor_lock", true)
        cursorLockSwitch?.setOnCheckedChangeListener { _, isChecked ->
            apxPrefs.edit().putBoolean("cursor_lock", isChecked).apply()
        }
"""

EDITS = [
    ("android/app/src/main/res/layout/page_settings.xml", [
        ("""        <!-- 快捷键模板：默认折叠，只显示当前这一套 -->
""",
         CURSOR_ROW + """        <!-- 快捷键模板：默认折叠，只显示当前这一套 -->
""", 1),
    ]),
    ("android/app/src/main/java/com/allperiph/ui/SettingsFragment.kt", [
        ("""        val act = activity ?: return
        // 反馈#10 批 B（方案 B）：首页 = 分类列表，点行进二级页，返回行回首屏。
""",
         """        val act = activity ?: return
""" + KOTLIN_BLOCK + """        // 反馈#10 批 B（方案 B）：首页 = 分类列表，点行进二级页，返回行回首屏。
""", 1),
    ]),
    ("docs/CHANGELOG.md", [
        ("  单行提示与运行时诊断语境文字保留\n",
         """  单行提示与运行时诊断语境文字保留
- **触摸板光标锁定（批 E · issue#44）**：设置页新增「触摸板光标锁定」开关
  （默认开启，pref=cursor_lock），Pad 上系统光标不再随触控板移动、防误触；
  仅关本地 pointer，PC 转发不受影响
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
