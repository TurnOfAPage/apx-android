#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（批 P · 横屏操作条，issue 实测）：
  - 撤回反馈#9 的无条件底栏常驻 → 恢复对称式（横屏隐藏、竖屏显示）——
    实测底栏碍事；切页改由右上 pill 条显式按键
  - layout-land 新增 pill 容器（触控板/键盘/锁定/设置四键，gone 起始）
  - btnSettingsFloat 永隐（pill 的 btnLandSettings 接管，避免重叠）
  - MainActivity：pill 可见性随 landscape + 四键 listener（showPage/toggleOrientLock）
  - CHANGELOG 补条

⚠ 铁律④（run#28/构建实锤）：python 三引号会把字符串里单反斜杠 n 转成真实换行——
  Kotlin/Java 字符串内的 \\n 必须写 \\\\n（python 双反斜杠）；XML 属性值内真换行虽合法但语义同转义。
⚠ 铁律（run#5/#14 实锤）：锚点以双引号收尾必须先换行再闭三引号。
两阶段：①按期望次数全量校验；②全过才写盘。
"""
import sys

PILL_XML = """    <!-- 批P：横屏操作条 pill（issue 实测）——触控板/键盘切换 + 方向锁 + 设置，替代四指滑唯一路径 -->
    <LinearLayout
        android:id="@+id/pillLandActions"
        android:layout_width="wrap_content"
        android:layout_height="wrap_content"
        android:layout_gravity="top|end"
        android:layout_marginTop="12dp"
        android:layout_marginEnd="12dp"
        android:background="@drawable/bg_card"
        android:gravity="center_vertical"
        android:orientation="horizontal"
        android:padding="4dp"
        android:visibility="gone">

        <TextView
            android:id="@+id/btnLandTouchpad"
            android:layout_width="wrap_content"
            android:layout_height="wrap_content"
            android:paddingStart="10dp"
            android:paddingEnd="6dp"
            android:paddingTop="6dp"
            android:paddingBottom="6dp"
            android:text="触控板"
            android:textColor="@color/md_on_surface"
            android:textSize="13sp" />

        <TextView
            android:id="@+id/btnLandKeyboard"
            android:layout_width="wrap_content"
            android:layout_height="wrap_content"
            android:paddingStart="6dp"
            android:paddingEnd="6dp"
            android:paddingTop="6dp"
            android:paddingBottom="6dp"
            android:text="键盘"
            android:textColor="@color/md_on_surface"
            android:textSize="13sp" />

        <TextView
            android:id="@+id/btnLandLock"
            android:layout_width="wrap_content"
            android:layout_height="wrap_content"
            android:paddingStart="6dp"
            android:paddingEnd="6dp"
            android:paddingTop="6dp"
            android:paddingBottom="6dp"
            android:text="锁定"
            android:textColor="@color/md_on_surface"
            android:textSize="13sp" />

        <TextView
            android:id="@+id/btnLandSettings"
            android:layout_width="wrap_content"
            android:layout_height="wrap_content"
            android:paddingStart="6dp"
            android:paddingEnd="10dp"
            android:paddingTop="6dp"
            android:paddingBottom="6dp"
            android:text="设置"
            android:textColor="@color/md_on_surface"
            android:textSize="13sp" />
    </LinearLayout>

"""

PILL_KOTLIN = """        // 批P：横屏操作条 pill——触控板/键盘切换 + 方向锁 + 设置（issue 实测：底栏碍事、四指滑不直观）
        findViewById<View>(R.id.pillLandActions)?.visibility = if (landscape) View.VISIBLE else View.GONE
        findViewById<TextView>(R.id.btnLandTouchpad)?.setOnClickListener { Feedback.tap(it); showPage(PAGE_TOUCHPAD) }
        findViewById<TextView>(R.id.btnLandKeyboard)?.setOnClickListener { Feedback.tap(it); showPage(PAGE_KEYBOARD) }
        findViewById<TextView>(R.id.btnLandLock)?.setOnClickListener { Feedback.tap(it); toggleOrientLock() }
        findViewById<TextView>(R.id.btnLandSettings)?.setOnClickListener { Feedback.tap(it); showPage(PAGE_SETTINGS) }
"""

EDITS = [
    ("android/app/src/main/java/com/allperiph/ui/MainActivity.kt", [
        # 1. 撤回#9：无条件 VISIBLE → 对称式（横屏藏、竖屏显，竖屏底栏不受影响）
        ("""        // 反馈#9：横屏也常驻底部分页栏（原：沉浸隐藏 → 只能四指滑，从通知栏进来切页不方便）
        tabBarView.visibility = View.VISIBLE
""",
         """        // 批P：撤回反馈#9（实测横屏底栏碍事）→ 恢复对称式隐藏；切页改走右上 pill 条
        tabBarView.visibility = if (landscape) View.GONE else View.VISIBLE
""", 1),
        # 2. btnSettingsFloat 永隐（pill 接管，避免与 pill 重叠；apply 块后续对 GONE 无害）
        ("""        findViewById<TextView>(R.id.btnSettingsFloat)?.apply {
            visibility = if (landscape) View.VISIBLE else View.GONE
""",
         """        findViewById<TextView>(R.id.btnSettingsFloat)?.apply {
            visibility = View.GONE // 批P：横屏操作条 pill（btnLandSettings）接管
""", 1),
        # 3. pill 初始化段（插在「#2 常驻入口」注释前；landscape 此处已在作用域）
        ("""        // 右上角设置入口常驻（反馈#2）：竖屏顶栏 btnSettingsEntry、横屏悬浮 btnSettingsFloat，
""",
         PILL_KOTLIN + """        // 右上角设置入口常驻（反馈#2）：竖屏顶栏 btnSettingsEntry、横屏悬浮 btnSettingsFloat，
""", 1),
    ]),
    ("android/app/src/main/res/layout-land/activity_main.xml", [
        # 4. pill 容器插入浮键注释前（path 限定 layout-land，竖屏同文不误伤）
        ("""    <!-- 横屏沉浸右上角悬浮设置入口（反馈#2）：横屏 chrome 全隐时的常驻入口 -->
""",
         PILL_XML + """    <!-- 横屏沉浸右上角悬浮设置入口（反馈#2）：横屏 chrome 全隐时的常驻入口 -->
""", 1),
    ]),
    ("docs/CHANGELOG.md", [
        ("  双指滚动、点击不再因背压丢失\n",
         """  双指滚动、点击不再因背压丢失
- **横屏操作条 pill（批 P · issue 实测）**：撤回#9 底栏常驻（恢复对称隐藏，竖屏不受
  影响）；右上新增 pill 条——触控板/键盘显式切换 + 方向锁 + 设置，替代「四指滑是
  唯一换页方式」；原悬浮设置键并入 pill（永隐防重叠）
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
