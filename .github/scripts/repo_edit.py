#!/usr/bin/env python3
"""repo-edit：大文件锚点替换编辑器。

主会话无法整文件回传（MainActivity.kt ~111KB），把小而精确的改动交给 Actions 云端执行。
两阶段：①先对全部锚点做唯一性校验（count==1），任何失配一次性全部列出；②全部通过才写盘。
任何失配 → 退出非零 → 工作流失败 → 不产生提交。
"""
import sys

# 悬浮入口 XML（竖屏/横屏主布局共用；显隐由 MainActivity.updateSettingsEntry 控制）
ENTRY_FLOAT = '''

    <!-- 横屏沉浸右上角悬浮设置入口（反馈#2）：横屏 chrome 全隐时的常驻入口 -->
    <TextView
        android:id="@+id/btnSettingsFloat"
        style="@style/APTextButton"
        android:layout_width="wrap_content"
        android:layout_height="wrap_content"
        android:layout_gravity="top|end"
        android:layout_marginTop="10dp"
        android:layout_marginEnd="16dp"
        android:background="@drawable/bg_card"
        android:elevation="4dp"
        android:paddingStart="14dp"
        android:paddingEnd="14dp"
        android:paddingTop="8dp"
        android:paddingBottom="8dp"
        android:text="@string/ui_main_btn_settings_entry"
        android:visibility="gone" />
'''

EDITS = [
    # ————————————————— MainActivity.kt（~111KB，7 处） —————————————————
    ("android/app/src/main/java/com/allperiph/ui/MainActivity.kt", [
        # 1) 锁定中：布局钉在锁定方向，不再跟随系统旋转配置（Pad 反馈#2）
        ("""    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)""",
         """    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        if (orientLocked) {
            // 锁定中（反馈#2，Pad 实测）：布局钉在锁定方向，不再跟随系统配置翻面 ——
            // 部分 ROM/Pad 仍会派发旋转配置，布局跟着转 = 设置页被翻走、没法操作。
            val want = lockedOrient == 1
            if (landscape != want) {
                landscape = want
                applyOrientationLayout()
            }
            handler.post { snapIndicator() }
            return
        }"""),
        # 2) onResume：锁定中不开方向传感器（old 只用纯代码行，避开中文字差）
        ("""        setupOrientListener()
        orientListener?.enable()""",
         """        setupOrientListener()
        // 锁定中不使能传感器（反馈#2：锁定后仍绑陀螺仪 → 从源头停掉）
        if (!orientLocked) orientListener?.enable()"""),
        # 3) setOrientLock：锁定即停物理检测，解锁恢复
        ("""        if (lock) {
            lockedOrient = if (resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) 1 else 2
            requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LOCKED
            Log.i(TAG, "方向锁定：orient=$lockedOrient")
        } else {
            requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
            forcedOrient = 0
            lockedOrient = 0
            Log.i(TAG, "方向解锁：交还系统，物理检测恢复")
        }""",
         """        if (lock) {
            lockedOrient = if (resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) 1 else 2
            requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LOCKED
            orientListener?.disable() // 锁定即停物理检测（反馈#2：锁定≠只冻 requestedOrientation）
            Log.i(TAG, "方向锁定：orient=$lockedOrient")
        } else {
            requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
            forcedOrient = 0
            lockedOrient = 0
            orientListener?.enable()  // 解锁恢复检测（onResume 同款幂等）
            Log.i(TAG, "方向解锁：交还系统，物理检测恢复")
        }"""),
        # 4) applyOrientationLayout 尾部：刷新右上角入口显隐
        ("""        if (landscape) navGlass.setIndicator(null)
        applyImmersive()
        applyNavBarWidth()
    }""",
         """        if (landscape) navGlass.setIndicator(null)
        applyImmersive()
        applyNavBarWidth()
        updateSettingsEntry()
    }"""),
        # 5) showPage：切页后刷新入口（在横屏提前 return 之前）
        ("""        // 横屏沉浸模式：没有底栏，靠四指左右滑换页
        if (landscape) return""",
         """        updateSettingsEntry()
        // 横屏沉浸模式：没有底栏，靠四指左右滑换页
        if (landscape) return"""),
        # 6) bindActions：右上角两入口接线
        ("""        findViewById<Switch>(R.id.swOrientLock)?.apply {
            isChecked = orientLocked
            setOnCheckedChangeListener { _, on -> setOrientLock(on) }
        }""",
         """        findViewById<Switch>(R.id.swOrientLock)?.apply {
            isChecked = orientLocked
            setOnCheckedChangeListener { _, on -> setOrientLock(on) }
        }
        // 右上角设置入口常驻（反馈#2）：竖屏顶栏 btnSettingsEntry、横屏悬浮 btnSettingsFloat，
        // 不再依赖「四指滑 / 底栏页签」才能进设置页。
        findViewById<TextView>(R.id.btnSettingsEntry)?.setOnClickListener {
            Feedback.tap(it)
            showPage(PAGE_SETTINGS)
        }
        findViewById<TextView>(R.id.btnSettingsFloat)?.setOnClickListener {
            Feedback.tap(it)
            // 横屏设置页上它当「返回」用（四指滑之外再给一条退路），其余页面进设置
            showPage(
                if (::pager.isInitialized && pager.displayedChild == PAGE_SETTINGS)
                    PAGE_TOUCHPAD
                else PAGE_SETTINGS
            )
        }"""),
        # 7) 新增 updateSettingsEntry()（挂在 renderOrientLock 末尾）
        ("""        findViewById<Switch>(R.id.swOrientLock)?.isChecked = orientLocked
    }""",
         """        findViewById<Switch>(R.id.swOrientLock)?.isChecked = orientLocked
    }

    /**
     * 右上角设置入口显隐（反馈#2）。
     *  - 竖屏：顶栏按钮常驻（设置页自身隐藏，避免与顶栏分段/底栏页签重复）
     *  - 横屏：顶栏与底栏整条隐藏 → 悬浮按钮常驻；设置页上文案改「返回」
     */
    private fun updateSettingsEntry() {
        val onSettings = ::pager.isInitialized && pager.displayedChild == PAGE_SETTINGS
        findViewById<TextView>(R.id.btnSettingsEntry)?.visibility =
            if (!landscape && !onSettings) View.VISIBLE else View.GONE
        findViewById<TextView>(R.id.btnSettingsFloat)?.apply {
            visibility = if (landscape) View.VISIBLE else View.GONE
            text = getString(
                if (onSettings) R.string.ui_main_btn_settings_back
                else R.string.ui_main_btn_settings_entry,
            )
        }
    }"""),
    ]),
    # ————————————————— header_bar.xml（竖屏顶栏按钮） —————————————————
    ("android/app/src/main/res/layout/header_bar.xml", [
        ("""        android:text="@string/ui_main_btn_orient_lock"
        android:contentDescription="@string/ui_main_cd_orient_lock" />
</LinearLayout>""",
         """        android:text="@string/ui_main_btn_orient_lock"
        android:contentDescription="@string/ui_main_cd_orient_lock" />

    <!-- 设置入口常驻右上角（反馈#2）：任何页面一点即达 -->
    <TextView
        android:id="@+id/btnSettingsEntry"
        style="@style/APTextButton"
        android:layout_width="wrap_content"
        android:layout_height="wrap_content"
        android:layout_marginStart="12dp"
        android:text="@string/ui_main_btn_settings_entry"
        android:contentDescription="@string/ui_main_btn_settings_entry" />
</LinearLayout>"""),
    ]),
    # ————————————————— 竖屏主布局：加悬浮入口（竖屏默认不显示） —————————————————
    ("android/app/src/main/res/layout/activity_main.xml", [
        ("""    <!-- MIUIX 液态悬浮底栏 -->
    <include
        layout="@layout/nav_tabs"
        android:layout_width="match_parent"
        android:layout_height="wrap_content" />
</FrameLayout>""",
         """    <!-- MIUIX 液态悬浮底栏 -->
    <include
        layout="@layout/nav_tabs"
        android:layout_width="match_parent"
        android:layout_height="wrap_content" />""" + ENTRY_FLOAT + """</FrameLayout>"""),
    ]),
    # ————————————————— 横屏主布局：加悬浮入口（横屏常驻右上角） —————————————————
    ("android/app/src/main/res/layout-land/activity_main.xml", [
        ("""        <include
            layout="@layout/nav_tabs"
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:visibility="gone" />
    </LinearLayout>
</FrameLayout>""",
         """        <include
            layout="@layout/nav_tabs"
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:visibility="gone" />
    </LinearLayout>""" + ENTRY_FLOAT + """</FrameLayout>"""),
    ]),
    # ————————————————— strings.xml：两个文案 —————————————————
    ("android/app/src/main/res/values/strings.xml", [
        ("</resources>",
         """    <string name="ui_main_btn_settings_entry">设置</string>
    <string name="ui_main_btn_settings_back">返回</string>
</resources>"""),
    ]),
]

def main() -> int:
    # 阶段①：全量校验，一次性报出所有失配
    loaded = {}
    errors = []
    for path, pairs in EDITS:
        try:
            with open(path, encoding="utf-8") as f:
                text = f.read()
        except OSError as e:
            errors.append(f"{path}: open failed: {e}")
            continue
        loaded[path] = (text, pairs)
        for i, (old, _new) in enumerate(pairs, 1):
            n = text.count(old)
            if n != 1:
                errors.append(
                    f"{path} edit#{i}: anchor count={n} (expect 1)\n---anchor---\n{old[:400]}\n---"
                )
    if errors:
        print("VALIDATION FAILED：")
        for e in errors:
            print(e)
        return 1
    # 阶段②：全部通过，统一替换写盘
    for path, (text, pairs) in loaded.items():
        for old, new in pairs:
            text = text.replace(old, new, 1)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        print(f"OK {path}: {len(pairs)} edit(s)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
