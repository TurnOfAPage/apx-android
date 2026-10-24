#!/usr/bin/env python3
# -*- coding: utf-8 -*-
'''apx-android 横竖屏改造一次性文本补丁（诊断版）：
  1) 旋转只换布局、不切页（删除朝向→页面强绑定）
  2) 底栏三页签改为 触控板 / 键盘 / 设置（键盘页转正）
  3) 状态页并入设置页：顶栏分段互切，状态页高亮归并到设置页签
任一锚点匹配数 != 1 不中断：继续尝试其余 hunk，把差异详情写入 PATCH_REPORT.txt。
只有全部 hunk 成功时才不产生报告文件。
'''
import io
import os
import traceback

REPORT = []


def patch(path, pairs):
    if not os.path.exists(path):
        REPORT.append("FAIL file=%s : FILE NOT FOUND" % path)
        return
    with io.open(path, encoding="utf-8") as f:
        s = f.read()
    ok = 0
    for i, (old, new) in enumerate(pairs):
        n = s.count(old)
        if n == 1:
            s = s.replace(old, new)
            ok += 1
            continue
        lines = old.split("\n")
        first = lines[0]
        pos = s.find(first)
        REPORT.append("FAIL file=%s hunk#%d count=%d first_line_pos=%d" % (path, i, n, pos))
        REPORT.append("  old_first=%r" % first[:120])
        REPORT.append("  old_last =%r" % lines[-1][:120])
        if pos >= 0:
            REPORT.append("  file_at  =%r" % s[pos:pos + 240])
        REPORT.append("  old_head =%r" % old[:240])
    with io.open(path, "w", encoding="utf-8") as f:
        f.write(s)
    print("[OK] %s: %d/%d hunks applied" % (path, ok, len(pairs)))
    if ok != len(pairs):
        REPORT.append("PARTIAL file=%s applied=%d total=%d" % (path, ok, len(pairs)))


MAIN = "android/app/src/main/java/com/allperiph/ui/MainActivity.kt"
NAV = "android/app/src/main/res/layout/nav_tabs.xml"
HEADER = "android/app/src/main/res/layout/header_bar.xml"

MAIN_HUNKS = [
    # 1) 重建恢复注释：初始页不再按朝向
    (
        '''        // 恢复页签。另：Bundle.getInt() 返回非空 Int（缺省 0），`?.let` 恒执行 ——
        // 所以必须再判一次范围，且只接受确实存过的值，否则会把 buildTabs() 按物理朝向
        // 选好的初始页（横屏冷启动应是沉浸操控面）强行切回第 0 页。''',
        '''        // 恢复页签。另：Bundle.getInt() 返回非空 Int（缺省 0）——
        // 所以必须再判一次范围，且只接受确实存过的值，避免把 buildTabs() 选好的
        // 初始页（固定触控板）强行切走。''',
    ),
    # 2) applyOrientationLayout 头注释
    (
        '''        // 配置方向滞后于物理朝向，导致横屏仍停在触控板页。''',
        '''        // 配置方向滞后于物理朝向，导致布局朝向与实际手持不一致。''',
    ),
    # 3) 核心：删除「朝向即操控面」强切页，只保留布局适配
    (
        '''        // 朝向即操控面：竖屏=触控板，横屏=全键盘（键盘页已移出底栏，横屏自动呈现）。
        // 旋屏时 Activity 不重建（configChanges 含 orientation），这里是唯一的切页时机。
        if (::pager.isInitialized) {
            val target = if (landscape) PAGE_KEYBOARD else PAGE_TOUCHPAD
            if (pager.displayedChild != target) showPage(target)''',
        '''        // 旋转只换布局、不切页：横竖屏各自铺各自的布局，当前页面保持不动
        //（键盘页已转正进底栏，不再“横过来就自动跳键盘”）。
        if (::pager.isInitialized) {''',
    ),
    # 4) 四指循环：横屏唯一换页方式遍历全部四页（状态/设置可达）
    (
        '''                        if (abs(dx) > dp(56)) {
                            fourFingerSwiped = true
                            val next = if (dx < 0) PAGE_KEYBOARD else PAGE_TOUCHPAD
                            if (next != pager.displayedChild) {''',
        '''                        if (abs(dx) > dp(56)) {
                            fourFingerSwiped = true
                            // 横屏无底栏，四指是唯一换页方式：按页序循环（触控板→键盘→状态→设置）
                            val order = intArrayOf(
                                PAGE_TOUCHPAD, PAGE_KEYBOARD, PAGE_STATUS, PAGE_SETTINGS,
                            )
                            val cur = order.indexOf(pager.displayedChild).coerceAtLeast(0)
                            val next = if (dx < 0) {
                                order[(cur + 1) % order.size]
                            } else {
                                order[(cur + order.size - 1) % order.size]
                            }
                            if (next != pager.displayedChild) {''',
    ),
    # 5) 物理朝向 KDoc：语义改为“只换布局不切页”
    (
        '''**竖持=触控板、横持=键盘**，与系统的方向锁定完全无关。''',
        '''**横持铺开横屏布局、竖持铺开竖屏布局**，与系统的方向锁定完全无关。''',
    ),
    (
        '''configuration 随之变化 → [applyOrientationLayout] 自动切键盘；竖持同理切回触控板。''',
        '''configuration 随之变化 → [applyOrientationLayout] 只换布局、不切页。''',
    ),
    # 6) listener 内联注释：刷新布局而非切页
    (
        '''                // 直接按物理朝向切换页面，不等待 onConfigurationChanged ——
                // 某些 ROM 下 requestedOrientation 未即时生效，配置方向滞后，
                // 若只靠 applyOrientationLayout 里的 resources.configuration 判断，
                // 横屏后会仍停在触控板页。''',
        '''                // 立即按物理朝向刷新布局，不等待 onConfigurationChanged ——
                // 某些 ROM 下 requestedOrientation 未即时生效，配置方向滞后。
                // 只换布局不切页：当前页面保持不动。''',
    ),
    # 7) buildTabs：底栏改 触控板 / 键盘 / 设置
    (
        '''        // 键盘页不占底栏位：横屏沉浸模式自动呈现（底栏 3 页签：触控板 / 状态 / 设置）。
        // 朝向切换由 [applyOrientationLayout] + orientPoll 兜底轮询保证。
        listOf(
            intArrayOf(R.id.tabTouchpad, R.id.ivTabTouchpad, R.id.tvTabTouchpad, R.id.tabPillTouchpad),
            intArrayOf(R.id.tabStatus, R.id.ivTabStatus, R.id.tvTabStatus, R.id.tabPillStatus),
            intArrayOf(R.id.tabSettings, R.id.ivTabSettings, R.id.tvTabSettings, R.id.tabPillSettings),
        ).forEachIndexed { tabIndex, ids ->''',
        '''        // 底栏 3 页签：触控板 / 键盘 / 设置（状态页并入设置页，顶栏分页互切）。
        // 旋转只换布局不切页，页面选择权始终在用户手里。
        listOf(
            intArrayOf(R.id.tabTouchpad, R.id.ivTabTouchpad, R.id.tvTabTouchpad, R.id.tabPillTouchpad),
            intArrayOf(R.id.tabKeyboard, R.id.ivTabKeyboard, R.id.tvTabKeyboard, R.id.tabPillKeyboard),
            intArrayOf(R.id.tabSettings, R.id.ivTabSettings, R.id.tvTabSettings, R.id.tabPillSettings),
        ).forEachIndexed { tabIndex, ids ->''',
    ),
    # 8) 初始页固定触控板
    (
        '''        // 初始页跟随朝向：竖屏=触控板，横屏=键盘（与 applyOrientationLayout 保持一致）
        showPage(if (landscape) PAGE_KEYBOARD else PAGE_TOUCHPAD)''',
        '''        // 初始页固定触控板（横竖屏一致；旋转只换布局，不再自动跳键盘页）
        showPage(PAGE_TOUCHPAD)''',
    ),
    # 9) showPage：新增 tabIndexFor + 页签高亮走归并映射
    (
        '''    /** 切页：标题栏、页签染色与选中胶囊同步（内容页各自独立滚动） */
    private fun showPage(i: Int) {
        if (!::pager.isInitialized) return
        pager.displayedChild = i
        tvPageTitle.text = getString(PAGE_TITLES[i])
        tvPageSub.text = getString(PAGE_SUBS[i])
        if (i == PAGE_TOUCHPAD) renderChips()
        tabViews.indices.forEach { k ->
            val on = TAB_PAGES[k] == i''',
        '''    /** 页 → 底栏页签下标（状态页并入设置 tab，高亮归并到「设置」；未映射返回 -1） */
    private fun tabIndexFor(page: Int): Int =
        TAB_PAGES.indexOf(if (page == PAGE_STATUS) PAGE_SETTINGS else page)

    /** 切页：标题栏、页签染色与选中胶囊同步（内容页各自独立滚动） */
    private fun showPage(i: Int) {
        if (!::pager.isInitialized) return
        pager.displayedChild = i
        tvPageTitle.text = getString(PAGE_TITLES[i])
        tvPageSub.text = getString(PAGE_SUBS[i])
        if (i == PAGE_TOUCHPAD) renderChips()
        val activeTab = tabIndexFor(i)
        tabViews.indices.forEach { k ->
            val on = k == activeTab''',
    ),
    # 10) showPage：状态/设置顶栏分段渲染（插在横屏早退之前）
    (
        '''            tabPills[k].background = null // 选中底由底栏 Drawable 里的滑动胶囊承担
        }
        // 横屏沉浸模式：没有底栏，靠四指左右滑换页
        if (landscape) return''',
        '''            tabPills[k].background = null // 选中底由底栏 Drawable 里的滑动胶囊承担
        }
        // 状态/设置 页内分页：顶栏分段（竖屏 header 可见；横屏靠四指循环互切）
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
        // 横屏沉浸模式：没有底栏，靠四指左右滑换页
        if (landscape) return''',
    ),
    # 11) 选中胶囊两处映射
    (
        '''        val tabIndex = TAB_PAGES.indexOf(pager.displayedChild)''',
        '''        val tabIndex = tabIndexFor(pager.displayedChild)''',
    ),
    (
        '''        val pill = tabPills.getOrNull(TAB_PAGES.indexOf(pager.displayedChild)) ?: return''',
        '''        val pill = tabPills.getOrNull(tabIndexFor(pager.displayedChild)) ?: return''',
    ),
    # 12) bindActions：顶栏分段点击
    (
        '''        findViewById<Switch>(R.id.swOrientLock)?.apply {
            isChecked = orientLocked
            setOnCheckedChangeListener { _, on -> setOrientLock(on) }
        }''',
        '''        findViewById<Switch>(R.id.swOrientLock)?.apply {
            isChecked = orientLocked
            setOnCheckedChangeListener { _, on -> setOrientLock(on) }
        }
        // 状态 ↔ 设置 页内分页（状态页已并入设置 tab，两页经顶栏分段互切）
        findViewById<TextView>(R.id.segGoStatus)?.setOnClickListener {
            Feedback.tap(it)
            showPage(PAGE_STATUS)
        }
        findViewById<TextView>(R.id.segGoSettings)?.setOnClickListener {
            Feedback.tap(it)
            showPage(PAGE_SETTINGS)
        }''',
    ),
    # 13) TAB_PAGES 常量
    (
        '''        /**
         * 底栏页签 → ViewFlipper 页面 index 的映射。
         * 键盘页不占底栏位（横屏自动呈现），页签：触控板(0) / 状态(2) / 设置(3)。
         */
        private val TAB_PAGES = intArrayOf(PAGE_TOUCHPAD, PAGE_STATUS, PAGE_SETTINGS)''',
        '''        /**
         * 底栏页签 → ViewFlipper 页面 index 的映射。
         * 页签：触控板(0) / 键盘(1) / 设置(3)；状态页(2) 并入设置 tab（tabIndexFor 归并高亮）。
         */
        private val TAB_PAGES = intArrayOf(PAGE_TOUCHPAD, PAGE_KEYBOARD, PAGE_SETTINGS)''',
    ),
    # 14) setupGlass 兜底注释
    (
        '''        // 导致「横屏键盘没了」。每秒比对一次实际朝向，不一致就补切（幂等，代价可忽略）。''',
        '''        // 导致「横竖屏布局没跟上」。每秒比对一次实际朝向，不一致就补请求（幂等，代价可忽略）。''',
    ),
    # 15) onConfigurationChanged KDoc
    (
        '''     * 横屏 = 沉浸操控面：**去掉标题栏与悬浮底栏**，整屏留给触控板 / 全键盘，
     * 只保留右下角一颗半透明切换点（否则没有页签就没法换页）。
     * 竖屏恢复完整 chrome（标题 + 玻璃底栏）。''',
        '''     * 横屏 = 沉浸操控面：**去掉标题栏与悬浮底栏**，整屏留给触控板 / 键盘 / 状态 / 设置，
     * 页间用四指左右滑循环切换。竖屏恢复完整 chrome（标题 + 玻璃底栏 + 页签底栏）。''',
    ),
]

NAV_HUNKS = [
    ('android:id="@+id/tabStatus"', 'android:id="@+id/tabKeyboard"'),
    ('android:id="@+id/tabPillStatus"', 'android:id="@+id/tabPillKeyboard"'),
    ('android:id="@+id/ivTabStatus"', 'android:id="@+id/ivTabKeyboard"'),
    ('android:id="@+id/tvTabStatus"', 'android:id="@+id/tvTabKeyboard"'),
    ('android:src="@drawable/ic_apx_status"', 'android:src="@drawable/ic_apx_keyboard"'),
    ('android:text="@string/ui_page_state"', 'android:text="@string/ui_touchpad_label_keyboard"'),
]

HEADER_HUNKS = [
    (
        '    <TextView\n        android:id="@+id/badgeStatus"',
        '''    <!-- 状态 ↔ 设置 页内分页（状态页并入设置 tab 后的二级切换；非这两页时隐藏） -->
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

    <TextView
        android:id="@+id/badgeStatus"''',
    ),
]

if __name__ == "__main__":
    try:
        patch(MAIN, MAIN_HUNKS)
        patch(NAV, NAV_HUNKS)
        patch(HEADER, HEADER_HUNKS)
    except Exception:
        REPORT.append("EXCEPTION:\n" + traceback.format_exc())
    if REPORT:
        text = "PATCH REPORT (%d issues)\n" % len(REPORT) + "\n".join(REPORT) + "\n"
        with io.open("PATCH_REPORT.txt", "w", encoding="utf-8") as f:
            f.write(text)
        print(text)
    else:
        print("all patches applied cleanly")
