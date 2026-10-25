#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（反馈#10 批 A：状态页并入设置页，4 页 → 3 页）：
  - activity_main.xml / layout-land：pager 去掉状态页 include（两套都改）
  - page_settings.xml：顶部平铺状态块 include（总开关/传输开关/链路诊断/运行摘要随迁）
  - page_status.xml：根 ScrollView → LinearLayout（去自带 padding，滚动交给设置页外层）
  - header_bar.xml：移除「状态↔设置」顶栏分段
  - MainActivity：PAGE_STATUS 删除、PAGE_SETTINGS=3→2、四指循环/侧边限宽/页签归并/标题数组/
    分段着色与点击 全链路收编（StatusFragment 寄生按 id 渲染，零改动）
  - CHANGELOG 补条

两阶段：①按期望次数全量校验，失配一次性报全；②全过才写盘。失配 → 退出非零 → 不提交。
"""
import sys

EDITS = [
    # ————— 1. 竖屏 pager 去状态页 —————
    ("android/app/src/main/res/layout/activity_main.xml", [
        ("""            <!-- ③ 状态页 / ④ 设置页（复用块） -->
            <include
                layout="@layout/page_status"
                android:layout_width="match_parent"
                android:layout_height="match_parent" />

            <include
                layout="@layout/page_settings"
""",
         """            <!-- ④ 设置页（状态页已并入设置顶，反馈#10） -->
            <include
                layout="@layout/page_settings"
""", 1),
    ]),
    # ————— 2. 横屏 pager 去状态页 —————
    ("android/app/src/main/res/layout-land/activity_main.xml", [
        ("""            <!-- ③ 状态页 / ④ 设置页（复用块） -->
            <include
                layout="@layout/page_status"
                android:layout_width="match_parent"
                android:layout_height="match_parent" />

            <include
                layout="@layout/page_settings"
""",
         """            <!-- ④ 设置页（状态页已并入设置顶，反馈#10） -->
            <include
                layout="@layout/page_settings"
""", 1),
    ]),
    # ————— 3. 设置页顶部平铺状态块 —————
    ("android/app/src/main/res/layout/page_settings.xml", [
        ("""        <!-- 设置页分段：点顶部切换，避免一屏滚太长 -->
""",
         """        <!-- 状态块（原状态页并入设置页顶，反馈#10）：总开关 / 传输开关 / 链路诊断 /
             运行摘要。根节点已由 ScrollView 改为 LinearLayout 平铺，滚动交给本页外层。 -->
        <include
            layout="@layout/page_status"
            android:layout_width="match_parent"
            android:layout_height="wrap_content" />

        <!-- 设置页分段：点顶部切换，避免一屏滚太长 -->
""", 1),
    ]),
    # ————— 4. page_status 根节点改平铺 —————
    ("android/app/src/main/res/layout/page_status.xml", [
        ("""<!--
  状态页（复用块）：大号启动开关；**启动后**才展开运行摘要 / 链路诊断 / 延迟预算。
  竖屏底部留 96dp 给悬浮底栏；横屏（layout-land）由外层覆盖为 16dp。
-->
<ScrollView xmlns:android="http://schemas.android.com/apk/res/android"
    android:id="@+id/pageStatus"
    android:layout_width="match_parent"
    android:layout_height="match_parent"
    android:clipToPadding="false"
    android:fillViewport="true"
    android:paddingStart="16dp"
    android:paddingEnd="16dp"
    android:paddingBottom="96dp">
""",
         """<!--
  状态块（已并入设置页顶，反馈#10）：根节点改 LinearLayout 平铺，滚动交给设置页外层
  ScrollView；左右/底部留白由设置页统一提供（本块不再自带 padding）。
-->
<LinearLayout xmlns:android="http://schemas.android.com/apk/res/android"
    android:id="@+id/pageStatus"
    android:layout_width="match_parent"
    android:layout_height="wrap_content"
    android:orientation="vertical">
""", 1),
        ("</ScrollView>\n", "</LinearLayout>\n", 1),
    ]),
    # ————— 5. 顶栏移除状态↔设置分段 —————
    ("android/app/src/main/res/layout/header_bar.xml", [
        ("""    <!-- 状态 ↔ 设置 页内分页（状态页并入设置 tab 后的二级切换；非这两页时隐藏） -->
    <LinearLayout
        android:id="@+id/segPages"
        android:layout_width="wrap_content"
        android:layout_height="wrap_content"
        android:layout_marginEnd="8dp"
        android:orientation="horizontal"
        android:visibility="gone">

        <TextView
            android:id="@+id/segGoStatus"
            style="@style/APTextButton"
            android:layout_width="wrap_content"
            android:layout_height="wrap_content"
            android:text="@string/ui_page_state" />

        <TextView
            android:id="@+id/segGoSettings"
            style="@style/APTextButton"
            android:layout_width="wrap_content"
            android:layout_height="wrap_content"
            android:layout_marginStart="6dp"
            android:text="@string/section_settings" />
    </LinearLayout>

""",
         "", 1),
    ]),
    # ————— 6. MainActivity：PAGE 常量链 —————
    ("android/app/src/main/java/com/allperiph/ui/MainActivity.kt", [
        # 6a. 常量 + TAB_PAGES 注释
        ("""        private const val PAGE_TOUCHPAD = 0
        private const val PAGE_KEYBOARD = 1
        private const val PAGE_STATUS = 2
        private const val PAGE_SETTINGS = 3

        /**
         * 底栏页签 → ViewFlipper 页面 index 的映射。
         * 页签：触控板(0) / 键盘(1) / 设置(3)；状态页(2) 并入设置 tab（tabIndexFor 归并高亮）。
         */
        private val TAB_PAGES = intArrayOf(PAGE_TOUCHPAD, PAGE_KEYBOARD, PAGE_SETTINGS)
""",
         """        private const val PAGE_TOUCHPAD = 0
        private const val PAGE_KEYBOARD = 1
        // 状态页已并入设置页（反馈#10）：设置成为第 3 个也是最后一个孩子
        private const val PAGE_SETTINGS = 2

        /**
         * 底栏页签 → ViewFlipper 页面 index 的映射。
         * 页签：触控板(0) / 键盘(1) / 设置(2)。
         */
        private val TAB_PAGES = intArrayOf(PAGE_TOUCHPAD, PAGE_KEYBOARD, PAGE_SETTINGS)
""", 1),
        # 6b. 横屏侧边限宽只作用设置页
        ("""            // 视线要横跨整屏。留白随屏宽自适应（窄屏不强切）。子项顺序 = 页常量：
            // 0 触控板 1 键盘 2 状态 3 设置（两套 activity_main.xml 的 ViewFlipper 顺序）。
            val sidePad = if (landscape) {
                ((resources.displayMetrics.widthPixels - dp(680)).coerceAtLeast(0)) / 2
            } else 0
            for (i in PAGE_STATUS..PAGE_SETTINGS) {
""",
         """            // 视线要横跨整屏。留白随屏宽自适应（窄屏不强切）。子项顺序 = 页常量：
            // 0 触控板 1 键盘 2 设置（状态页已并入设置，反馈#10；横屏限宽只作用于设置页）。
            val sidePad = if (landscape) {
                ((resources.displayMetrics.widthPixels - dp(680)).coerceAtLeast(0)) / 2
            } else 0
            for (i in PAGE_SETTINGS..PAGE_SETTINGS) {
""", 1),
        # 6c. 四指循环去掉状态
        ("""                            // 横屏无底栏，四指是唯一换页方式：按页序循环（触控板→键盘→状态→设置）
                            val order = intArrayOf(
                                PAGE_TOUCHPAD, PAGE_KEYBOARD, PAGE_STATUS, PAGE_SETTINGS,
                            )
""",
         """                            // 横屏四指循环换页（触控板→键盘→设置；状态页已并入设置，反馈#10）
                            val order = intArrayOf(
                                PAGE_TOUCHPAD, PAGE_KEYBOARD, PAGE_SETTINGS,
                            )
""", 1),
        # 6d. tabIndexFor 去归并
        ("""    /** 页 → 底栏页签下标（状态页并入设置 tab，高亮归并到「设置」；未映射返回 -1） */
    private fun tabIndexFor(page: Int): Int =
        TAB_PAGES.indexOf(if (page == PAGE_STATUS) PAGE_SETTINGS else page)
""",
         """    /** 页 → 底栏页签下标（未映射返回 -1；状态页已并入设置，反馈#10） */
    private fun tabIndexFor(page: Int): Int =
        TAB_PAGES.indexOf(page)
""", 1),
        # 6e. showPage 分段着色块移除
        ("""        // 状态/设置 页内分页：顶栏分段（竖屏 header 可见；横屏靠四指循环互切）
        val onSeg = i == PAGE_STATUS || i == PAGE_SETTINGS
        findViewById<View>(R.id.segPages)?.visibility = if (onSeg) View.VISIBLE else View.GONE
        if (onSeg) {
            val cOn = resources.getColor(R.color.md_primary)
            val cOff = resources.getColor(R.color.state_idle)
            findViewById<TextView>(R.id.segGoStatus)?.apply {
                setTextColor(if (i == PAGE_STATUS) cOn else cOff)
                setTypeface(null, if (i == PAGE_STATUS) Typeface.BOLD else Typeface.NORMAL)
            }
            findViewById<TextView>(R.id.segGoSettings)?.apply {
                setTextColor(if (i == PAGE_SETTINGS) cOn else cOff)
                setTypeface(null, if (i == PAGE_SETTINGS) Typeface.BOLD else Typeface.NORMAL)
            }
        }
""",
         """        // 状态已并入设置页（反馈#10）：顶栏「状态↔设置」分段及其着色一并移除
""", 1),
        # 6f. bindActions 分段点击移除
        ("""        // 状态 ↔ 设置 页内分页（状态页已并入设置 tab，两页经顶栏分段互切）
        findViewById<TextView>(R.id.segGoStatus)?.setOnClickListener {
            Feedback.tap(it)
            showPage(PAGE_STATUS)
        }
        findViewById<TextView>(R.id.segGoSettings)?.setOnClickListener {
            Feedback.tap(it)
            showPage(PAGE_SETTINGS)
        }
""",
         """        // 状态已并入设置页（反馈#10）：分段控件与点击绑定移除（视图已在 header_bar 删除）
""", 1),
        # 6g. 标题/副标题数组去状态
        ("""        private val PAGE_TITLES = intArrayOf(
            R.string.ui_main_item_touchpad,        // 触控板
            R.string.ui_touchpad_label_keyboard,   // 键盘
            R.string.ui_page_state,                // 状态
            R.string.section_settings,             // 设置
        )
        private val PAGE_SUBS = intArrayOf(
            R.string.ui_main_item_hotkey_send_cursor_swipe,
            R.string.ui_touchpad_label_remote,
            R.string.ui_main_item_state,
            R.string.ui_main_item_template_theme,
        )
""",
         """        private val PAGE_TITLES = intArrayOf(
            R.string.ui_main_item_touchpad,        // 触控板
            R.string.ui_touchpad_label_keyboard,   // 键盘
            R.string.section_settings,             // 设置
        )
        private val PAGE_SUBS = intArrayOf(
            R.string.ui_main_item_hotkey_send_cursor_swipe,
            R.string.ui_touchpad_label_remote,
            R.string.ui_main_item_template_theme,
        )
""", 1),
    ]),
    # ————— 7. CHANGELOG —————
    ("docs/CHANGELOG.md", [
        ("底部 300dp 面板直接喂触控板链路（单/双/三指全支持），入口在设置页「工具」小节\n",
         """底部 300dp 面板直接喂触控板链路（单/双/三指全支持），入口在设置页「工具」小节
- **状态页并入设置（反馈#10 批 A）**：4 页 → 触控板/键盘/设置 3 页；状态块（总开关/
  传输开关/链路诊断/运行摘要）平铺进设置页顶部，顶栏「状态↔设置」分段移除，
  四指循环/侧边限宽/页签映射/标题数组全链路收编（StatusFragment 寄生渲染零改动）
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
