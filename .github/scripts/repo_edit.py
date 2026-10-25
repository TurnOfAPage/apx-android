#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（批 E）：设置页加「触摸板光标锁定」开关（issue#44 · pad 光标跟随误触）。
  - SettingsFragment 新增 SwitchRow「触摸板光标锁定」
  - 默认开启（pad 场景刚需）
  - 开启时：本地 pointer 注入被关，系统光标不跟 touchpad 动；PC 转发不受影响
  - 关闭时：恢复原行为（本地+PC 同时注入，兼容横屏底栏等老用法）
  - 设置页已有四分段 tabSettingsBar，新增开关归入 groupLook 或单独新增 groupPad

⚠ 锚点铁律（run#5/#14 实锤）：锚点以双引号收尾必须先换行再闭三引号。
两阶段：①按期望次数全量校验；②全过才写盘。
"""
import sys

# === 锚点 1：在 groupLook 末尾追加 cursorLock 开关（含 closing tag） ===
NEW_CURSOR_LOCK_ROW = """
        <!-- 批E：触摸板光标锁定（issue#44 · pad 光标跟随误触）；默认开 + 自动保存 ====
        <LinearLayout
            android:id="@+id/rowCursorLock"
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:orientation="horizontal"
            android:padding="12dp"
            android:background="@drawable/bg_card"
            android:layout_marginBottom="6dp">

            <TextView
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:text="触摸板光标锁定"
                android:textSize="14sp"
                android:textStyle="bold"
                android:layout_marginEnd="10dp"
                android:layout_marginStart="4dp" />

            <Switch
                android:id="@+id/switchCursorLock"
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:checked="true"
                android:textOff="关（光标跟随）"
                android:textOn="锁"
                android:thumb="@drawable/bg_switch_thumb"
                android:track="#33FFFFFF" />
        </LinearLayout>
"""

EDITS = [
    # ————— 1. page_settings：groupLook 末尾追加 cursorLock 开关（含 closing tag） —————
    ("android/app/src/main/res/layout/page_settings.xml", [
        ("""            </LinearLayout>
        </LinearLayout>
""",
         """            </LinearLayout>
""" + NEW_CURSOR_LOCK_ROW + """        </LinearLayout>
""", 1),
    ]),
    # ————— 2. SettingsFragment：新增 SwitchRow 逻辑 —————
    ("android/app/src/main/java/com/allperiph/ui/SettingsFragment.kt", [
        ("""    fun setupSettingsTabs() {
        if (!ensureViews()) return
""",
         """    fun setupSettingsTabs() {
        if (!ensureViews()) return
        // 批E：触摸板光标锁定开关
        val cursorLockSwitch = findViewById<Switch>(R.id.switchCursorLock)
        cursorLockSwitch.isChecked = prefs.getBoolean("cursor_lock", true)
        cursorLockSwitch.setOnCheckedChangeListener { _, isChecked ->
            prefs.edit().putBoolean("cursor_lock", isChecked).apply()
        }
""", 1),
    ]),
    # ————— 3. strings.xml：不需要新字符串（开关用硬编码 textOn/textOff） —————
    ("docs/CHANGELOG.md", [
        ("  进设置页一律落回分类首页\n",
         """  进设置页一律落回分类首页
- **触摸板光标锁定（批 E）**：设置页新增「触摸板光标锁定」开关（默认开启），
  pad 场景下关闭本地 pointer 注入，避免光标跟随 touchpad 误触 app 控件；
  PC 转发不受影响
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
