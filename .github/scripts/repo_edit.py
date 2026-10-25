#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

本批（反馈#10 批 B，方案 B）：设置首页改「分类列表 → 二级页」：
  - page_settings：四分段 tabSettingsBar（通用/外观/遥控/关于）替换为 5 行分类列表
    （新增“帮助与说明”）+ 二级页返回行；新增 groupHelp 帮助组（文字说明集中地）
  - SettingsFragment.setupSettingsTabs：分段选中逻辑 → 首页/二级页导航逻辑
  - MainActivity：每次进设置页回分类首页
  - strings：分类/返回/帮助 14 条
  - CHANGELOG 补条
说明：各处散落的长描述 TextView 收进帮助页属批 C（下一批）。

两阶段：①按期望次数全量校验；②全过才写盘。失配 → 退出非零 → 不提交。
"""
import sys

CAT_ROWS = """        <!-- 分类列表首页（反馈#10 批 B，方案 B）：点行进二级页；二级页经返回行回首屏 -->
        <LinearLayout
            android:id="@+id/categoryHome"
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:layout_marginBottom="10dp"
            android:background="@drawable/bg_card"
            android:orientation="vertical"
            android:paddingTop="4dp"
            android:paddingBottom="4dp">

            <TextView
                android:id="@+id/catGeneral"
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingStart="16dp"
                android:paddingEnd="16dp"
                android:paddingTop="12dp"
                android:paddingBottom="12dp"
                android:text="@string/ui_main_cat_general"
                android:textColor="#DD000000"
                android:textSize="15sp" />

            <TextView
                android:id="@+id/catLook"
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingStart="16dp"
                android:paddingEnd="16dp"
                android:paddingTop="12dp"
                android:paddingBottom="12dp"
                android:text="@string/ui_main_cat_look"
                android:textColor="#DD000000"
                android:textSize="15sp" />

            <TextView
                android:id="@+id/catRemote"
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingStart="16dp"
                android:paddingEnd="16dp"
                android:paddingTop="12dp"
                android:paddingBottom="12dp"
                android:text="@string/ui_main_cat_remote"
                android:textColor="#DD000000"
                android:textSize="15sp" />

            <TextView
                android:id="@+id/catAbout"
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingStart="16dp"
                android:paddingEnd="16dp"
                android:paddingTop="12dp"
                android:paddingBottom="12dp"
                android:text="@string/ui_main_cat_about"
                android:textColor="#DD000000"
                android:textSize="15sp" />

            <TextView
                android:id="@+id/catHelp"
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:paddingStart="16dp"
                android:paddingEnd="16dp"
                android:paddingTop="12dp"
                android:paddingBottom="12dp"
                android:text="@string/ui_main_cat_help"
                android:textColor="#DD000000"
                android:textSize="15sp" />
        </LinearLayout>

        <!-- 二级页返回行（默认隐藏，进分类后显示，点击回首屏） -->
        <LinearLayout
            android:id="@+id/btnBackHome"
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:layout_marginBottom="6dp"
            android:background="@drawable/bg_card"
            android:orientation="horizontal"
            android:paddingStart="16dp"
            android:paddingEnd="16dp"
            android:paddingTop="12dp"
            android:paddingBottom="12dp"
            android:visibility="gone">

            <TextView
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:text="@string/ui_main_cat_back"
                android:textColor="#DD000000"
                android:textSize="15sp" />
        </LinearLayout>

        <LinearLayout
            android:id="@+id/groupGeneral"""

HELP_GROUP = """
        <!-- 帮助与说明（反馈#10 批 B）：文字性说明集中地，各分类只留一行式条目 -->
        <LinearLayout
            android:id="@+id/groupHelp"
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:orientation="vertical"
            android:visibility="gone">

            <TextView
                style="@style/APSectionTitle"
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:text="@string/ui_help_title_project" />

            <LinearLayout
                style="@style/APCard"
                android:layout_width="match_parent"
                android:layout_height="wrap_content">

                <TextView
                    android:layout_width="match_parent"
                    android:layout_height="wrap_content"
                    android:text="@string/ui_help_project"
                    android:textColor="#CC000000"
                    android:textSize="13sp" />
            </LinearLayout>

            <TextView
                style="@style/APSectionTitle"
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:text="@string/ui_help_title_link" />

            <LinearLayout
                style="@style/APCard"
                android:layout_width="match_parent"
                android:layout_height="wrap_content">

                <TextView
                    android:layout_width="match_parent"
                    android:layout_height="wrap_content"
                    android:text="@string/ui_help_link"
                    android:textColor="#CC000000"
                    android:textSize="13sp" />
            </LinearLayout>

            <TextView
                style="@style/APSectionTitle"
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:text="@string/ui_help_title_gesture" />

            <LinearLayout
                style="@style/APCard"
                android:layout_width="match_parent"
                android:layout_height="wrap_content">

                <TextView
                    android:layout_width="match_parent"
                    android:layout_height="wrap_content"
                    android:text="@string/ui_help_gesture"
                    android:textColor="#CC000000"
                    android:textSize="13sp" />
            </LinearLayout>

            <TextView
                style="@style/APSectionTitle"
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:text="@string/ui_help_title_faq" />

            <LinearLayout
                style="@style/APCard"
                android:layout_width="match_parent"
                android:layout_height="wrap_content">

                <TextView
                    android:layout_width="match_parent"
                    android:layout_height="wrap_content"
                    android:text="@string/ui_help_faq"
                    android:textColor="#CC000000"
                    android:textSize="13sp" />
            </LinearLayout>
        </LinearLayout>
"""

EDITS = [
    # ————— 1. 分段条 → 分类列表（含返回行），保留 groupGeneral 开头作锚 —————
    ("android/app/src/main/res/layout/page_settings.xml", [
        ("""        <!-- 设置页分段：点顶部切换，避免一屏滚太长 -->
        <LinearLayout
            android:id="@+id/tabSettingsBar"
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:orientation="horizontal"
            android:background="@drawable/bg_card"
            android:padding="4dp"
            android:layout_marginBottom="10dp">

            <TextView
                android:id="@+id/tabSegGeneral"
                android:layout_width="0dp"
                android:layout_height="wrap_content"
                android:layout_weight="1"
                android:gravity="center"
                android:paddingTop="10dp"
                android:paddingBottom="10dp"
                android:text="@string/lay_settings_tab_general"
                android:textSize="13sp"
                android:textStyle="bold" />

            <TextView
                android:id="@+id/tabSegLook"
                android:layout_width="0dp"
                android:layout_height="wrap_content"
                android:layout_weight="1"
                android:gravity="center"
                android:paddingTop="10dp"
                android:paddingBottom="10dp"
                android:text="@string/lay_settings_tab_look"
                android:textSize="13sp" />

            <TextView
                android:id="@+id/tabSegRemote"
                android:layout_width="0dp"
                android:layout_height="wrap_content"
                android:layout_weight="1"
                android:gravity="center"
                android:paddingTop="10dp"
                android:paddingBottom="10dp"
                android:text="@string/ui_touchpad_text_remote"
                android:textSize="13sp" />

            <TextView
                android:id="@+id/tabSegAbout"
                android:layout_width="0dp"
                android:layout_height="wrap_content"
                android:layout_weight="1"
                android:gravity="center"
                android:paddingTop="10dp"
                android:paddingBottom="10dp"
                android:text="@string/lay_settings_tab_about"
                android:textSize="13sp" />
        </LinearLayout>

        <LinearLayout
            android:id="@+id/groupGeneral"""",
         CAT_ROWS, 1),
        # 2. groupAbout 之后加帮助组
        ("        </LinearLayout><!-- /groupAbout -->\n",
         "        </LinearLayout><!-- /groupAbout -->\n" + HELP_GROUP, 1),
    ]),
    # ————— 3. 分段选中 → 首页/二级页导航 —————
    ("android/app/src/main/java/com/allperiph/ui/SettingsFragment.kt", [
        ("""    fun setupSettingsTabs() {
        if (!ensureViews()) return
        val act = activity ?: return
        val tabs = listOf(
            act.findViewById<TextView>(R.id.tabSegGeneral),
            act.findViewById<TextView>(R.id.tabSegLook),
            act.findViewById<TextView>(R.id.tabSegRemote),
            act.findViewById<TextView>(R.id.tabSegAbout),
        )
        val groups = listOf(
            act.findViewById<LinearLayout>(R.id.groupGeneral),
            act.findViewById<LinearLayout>(R.id.groupLook),
            act.findViewById<LinearLayout>(R.id.groupRemote),
            act.findViewById<LinearLayout>(R.id.groupAbout),
        )
        fun select(i: Int) {
            tabs.forEachIndexed { j, t ->
                val on = i == j
                t.setBackgroundResource(if (on) R.drawable.bg_btn_primary else 0)
                t.setTextColor(
                    if (on) resources.getColor(R.color.md_on_primary)
                    else resources.getColor(R.color.md_on_surface_variant)
                )
            }
            groups.forEachIndexed { j, g -> g.visibility = if (i == j) View.VISIBLE else View.GONE }
        }
        tabs.forEachIndexed { j, t -> t.setOnClickListener { select(j) } }
        select(0)
    }
""",
         """    fun setupSettingsTabs() {
        if (!ensureViews()) return
        val act = activity ?: return
        // 反馈#10 批 B（方案 B）：首页 = 分类列表，点行进二级页，返回行回首屏。
        // 分组容器复用原四个 group*，另加帮助组 groupHelp；原四分段 tab 已删。
        val categories = listOf(
            act.findViewById<View>(R.id.catGeneral) to R.id.groupGeneral,
            act.findViewById<View>(R.id.catLook) to R.id.groupLook,
            act.findViewById<View>(R.id.catRemote) to R.id.groupRemote,
            act.findViewById<View>(R.id.catAbout) to R.id.groupAbout,
            act.findViewById<View>(R.id.catHelp) to R.id.groupHelp,
        )
        val home = act.findViewById<View>(R.id.categoryHome)
        val back = act.findViewById<View>(R.id.btnBackHome)
        fun hideAllGroups() {
            categories.forEach { (_, gid) -> act.findViewById<View>(gid)?.visibility = View.GONE }
        }
        fun scrollTop() {
            (act.findViewById<View>(R.id.pageSettings) as? android.widget.ScrollView)?.fullScroll(View.FOCUS_UP)
        }
        fun showHome() {
            home?.visibility = View.VISIBLE
            back?.visibility = View.GONE
            hideAllGroups()
            scrollTop()
        }
        categories.forEach { (row, gid) ->
            row?.setOnClickListener {
                home?.visibility = View.GONE
                back?.visibility = View.VISIBLE
                hideAllGroups()
                act.findViewById<View>(gid)?.visibility = View.VISIBLE
                scrollTop()
            }
        }
        back?.setOnClickListener { showHome() }
        showHome()
    }
""", 1),
    ]),
    # ————— 4. 进设置页回分类首页 —————
    ("android/app/src/main/java/com/allperiph/ui/MainActivity.kt", [
        ("        if (i == PAGE_TOUCHPAD) renderChips()\n",
         """        if (i == PAGE_TOUCHPAD) renderChips()
        // 反馈#10 批 B：进设置页一律落回分类首页（setupSettingsTabs 内部 showHome）
        if (i == PAGE_SETTINGS) settingsFragment?.setupSettingsTabs()
""", 1),
    ]),
    # ————— 5. strings：分类/返回/帮助 14 条 —————
    ("android/app/src/main/res/values/strings.xml", [
        ("</resources>", """    <string name="ui_main_cat_general">通用\n模板 · 功能开关 · 方向锁定</string>
    <string name="ui_main_cat_look">外观\n主题 · 配色 · 背景 · 动效</string>
    <string name="ui_main_cat_remote">遥控 · 工具\nTV/PC 控制 · 诊断 · 文件 · 自检/日志/悬浮球</string>
    <string name="ui_main_cat_about">关于与维护\n版本 · 更新日志 · 反馈 · 电池白名单</string>
    <string name="ui_main_cat_help">帮助与说明\n项目说明 · 链路解释 · 手势 · 常见问题</string>
    <string name="ui_main_cat_back">‹ 返回分类列表</string>
    <string name="ui_help_title_project">项目说明</string>
    <string name="ui_help_title_link">链路与延迟</string>
    <string name="ui_help_title_gesture">手势与操作</string>
    <string name="ui_help_title_faq">常见问题</string>
    <string name="ui_help_project">全能外设把手机/平板变成电脑的键盘、鼠标、副屏与声卡：USB 有线或 Wi-Fi 连上电脑后，手机屏就是触控板，实体键盘打字也会转发到电脑。所有通信仅限局域网，不上传任何数据（MIT 开源）。</string>
    <string name="ui_help_link">USB 有线：性能最高且同时充电，需 root，目前仅支持 Windows；Wi-Fi：免 root，走 9511 控制面 / 9502 媒体 / 9501 发现信标 / 9512 文件；蓝牙：HID 键盘鼠标。延迟与速率看「链路诊断」行；出现 UDC/带宽告警时，重启手机再开关 USB 模式通常可恢复。</string>
    <string name="ui_help_gesture">触控板：单指移动、双指滚动与捏合缩放、双指点按右键；三指点按为中键，三指滑动映射 Windows 手势（位移≥60px 触发）：上=Alt+Tab、下=Win+D、左右=Ctrl+Win+←/→。横屏支持触控板左右分栏、四指换页；实体键盘的触摸板也会映射到电脑。设置里的「悬浮球」可在其它应用上方保留一块触控板面板（需悬浮窗授权）。</string>
    <string name="ui_help_faq">① USB 模式需要 root（会提示授权）；② USB 模式仅支持 Windows，macOS 暂无副屏；③ 请把本应用加入电池优化白名单（设置→应用→电池→无限制），否则后台几十秒可能被杀、导致连不上；④ Windows 收 USB 麦克风/音箱、macOS 系统音频授权等进阶用法见仓库 docs/使用说明.md。</string>
</resources>""", 1),
    ]),
    # ————— 6. CHANGELOG —————
    ("docs/CHANGELOG.md", [
        ("  四指循环/侧边限宽/页签映射/标题数组全链路收编（StatusFragment 寄生渲染零改动）\n",
         """  四指循环/侧边限宽/页签映射/标题数组全链路收编（StatusFragment 寄生渲染零改动）
- **设置页分类列表化（反馈#10 批 B，方案 B）**：原「通用/外观/遥控/关于」顶部分段
  改为 5 行分类列表（新增「帮助与说明」）→ 点行进二级页 + 返回行回首屏；
  新增 groupHelp 帮助组（项目说明/链路与延迟/手势与操作/常见问题四卡），
  进设置页一律落回分类首页
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
