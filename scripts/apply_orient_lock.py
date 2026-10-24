#!/usr/bin/env python3
"""一次性编辑应用器：方向锁定按钮 + 横竖屏界面优化（9 处）。

应用全部编辑 → 自毁（删掉本脚本与 workflow 骨架）→ commit push 到 feat/orient-lock。
任一处 old_text 命中数 != 1 立即失败退出（不 commit，可修脚本后重跑）。
"""
import subprocess
import sys

EDITS = [
    (
        "android/app/src/main/res/values/strings.xml",
        '''    <string name="lay_settings_cd_hotkey">快捷键自动排序开关</string>''',
        '''    <string name="lay_settings_cd_hotkey">快捷键自动排序开关</string>

    <string name="ui_main_btn_orient_lock">锁定</string>
    <string name="ui_main_btn_orient_locked_land">已锁·横屏</string>
    <string name="ui_main_btn_orient_locked_port">已锁·竖屏</string>
    <string name="ui_main_cd_orient_lock">锁定或解锁屏幕方向</string>
    <string name="lay_settings_text_orient_lock">屏幕方向锁定（关掉横竖自动切换，横持自动弹出键盘也暂停）</string>
    <string name="lay_settings_cd_orient_lock">屏幕方向锁定开关</string>''',
    ),
    (
        "android/app/src/main/res/layout/header_bar.xml",
        '''    <TextView
        android:id="@+id/badgeStatus"
        style="@style/APBadge"
        android:layout_width="wrap_content"
        android:layout_height="wrap_content"
        android:text="@string/common_unknown" />
</LinearLayout>''',
        '''    <TextView
        android:id="@+id/badgeStatus"
        style="@style/APBadge"
        android:layout_width="wrap_content"
        android:layout_height="wrap_content"
        android:text="@string/common_unknown" />

    <!-- 方向锁定快捷开关（竖屏可见；横屏 header 隐藏，由设置页开关承接） -->
    <TextView
        android:id="@+id/btnOrientLock"
        style="@style/APTextButton"
        android:layout_width="wrap_content"
        android:layout_height="wrap_content"
        android:layout_marginStart="12dp"
        android:text="@string/ui_main_btn_orient_lock"
        android:contentDescription="@string/ui_main_cd_orient_lock" />
</LinearLayout>''',
    ),
    (
        "android/app/src/main/res/layout/page_settings.xml",
        '''                    android:contentDescription="@string/lay_settings_cd_hotkey" />
            </LinearLayout>
        </LinearLayout>''',
        '''                    android:contentDescription="@string/lay_settings_cd_hotkey" />
            </LinearLayout>

            <View
                android:layout_width="match_parent"
                android:layout_height="1dp"
                android:layout_marginTop="8dp"
                android:background="@color/card_stroke" />

            <LinearLayout
                android:layout_width="match_parent"
                android:layout_height="wrap_content"
                android:gravity="center_vertical"
                android:orientation="horizontal"
                android:paddingTop="12dp">

                <LinearLayout
                    android:layout_width="0dp"
                    android:layout_height="wrap_content"
                    android:layout_weight="1"
                    android:orientation="vertical">

                    <TextView
                        android:layout_width="wrap_content"
                        android:layout_height="wrap_content"
                        android:text="@string/lay_settings_text_orient_lock"
                        android:textAppearance="@style/APTextSubtitle" />
                </LinearLayout>

                <Switch
                    android:id="@+id/swOrientLock"
                    android:layout_width="wrap_content"
                    android:layout_height="wrap_content"
                    android:contentDescription="@string/lay_settings_cd_orient_lock" />
            </LinearLayout>
        </LinearLayout>''',
    ),
    (
        "android/app/src/main/java/com/allperiph/ui/MainActivity.kt",
        '''        @Volatile private var forcedOrient = 0          // 0=尚未强制 1=已强制横屏 2=已强制竖屏
        @Volatile private var lastOrientSwitchMs = 0L

        /** 旋屏重建时保存/恢复当前页签 */
        private const val KEY_PAGE = "page"''',
        '''        @Volatile private var forcedOrient = 0          // 0=尚未强制 1=已强制横屏 2=已强制竖屏
        @Volatile private var lastOrientSwitchMs = 0L

        /**
         * 用户方向锁定（顶栏按钮 / 设置页开关）：true = 冻结当前朝向 ——
         * 物理检测与兜底轮询全部暂停。放 companion 跨重建保持（与 forcedOrient 同理，
         * 实例字段会随重建归零 → 重建后锁失效），并由 SharedPreferences 持久化到重启后。
         */
        @Volatile private var orientLocked = false
        /** 锁定时刻锁定的朝向（1=横 2=竖），用于顶栏按钮文案与恢复 */
        @Volatile private var lockedOrient = 0

        /** 方向锁定持久化 */
        private const val PREF_ORIENT = "apx_orient_lock"
        private const val KEY_LOCK = "locked"

        /** 旋屏重建时保存/恢复当前页签 */
        private const val KEY_PAGE = "page"''',
    ),
    (
        "android/app/src/main/java/com/allperiph/ui/MainActivity.kt",
        '''        findViewById<View>(R.id.contentStack).post { snapIndicator() }
        // 朝向兜底：个别 ROM（MIUI 某些省电/分屏场景）旋屏不派发 onConfigurationChanged，
        // 导致「横屏键盘没了」。每秒比对一次实际朝向，不一致就补切（幂等，代价可忽略）。
        handler.postDelayed(orientPoll, 1000)
    }

    /** orientPoll 上次 re-request 方向的时刻（3s 冷却，防每秒请求风暴） */
    private var lastPollFixMs = 0L''',
        '''        findViewById<View>(R.id.contentStack).post { snapIndicator() }
        // 方向锁定恢复：重建后 requestedOrientation 已复位为 manifest 默认，必须重新上锁
        restoreOrientLock()
        // 朝向兜底：个别 ROM（MIUI 某些省电/分屏场景）旋屏不派发 onConfigurationChanged，
        // 导致「横屏键盘没了」。每秒比对一次实际朝向，不一致就补切（幂等，代价可忽略）。
        handler.postDelayed(orientPoll, 1000)
    }

    /** 从 SharedPreferences 恢复用户方向锁定（Activity 重建后 requestedOrientation 已复位，必须重新锁） */
    private fun restoreOrientLock() {
        if (!getSharedPreferences(PREF_ORIENT, MODE_PRIVATE).getBoolean(KEY_LOCK, false)) return
        orientLocked = true
        lockedOrient = if (landscape) 1 else 2
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LOCKED
        Log.i(TAG, "恢复方向锁定：orient=$lockedOrient")
        renderOrientLock()
    }

    /**
     * 方向锁定开关（顶栏按钮 + 设置页开关双入口，幂等：传入目标状态而不是翻转）。
     *  - 锁定：SCREEN_ORIENTATION_LOCKED 冻结当前朝向，orientListener（物理检测）与
     *    orientPoll（兜底轮询）全部暂停 —— 与朝向循环止血补丁正交互补：补丁让自动切换
     *    不发疯，锁定给用户一键彻底停。
     *  - 解锁：交还系统（UNSPECIFIED；不用 PORTRAIT —— MIUI 收到竖屏请求会写死
     *    user_rotation=0，之后横持请求全被忽略，见 orientListener 注释），并把 forcedOrient
     *    归零，让下一个物理事件立即重新接管方向（保留旧值会让 listener 误判"已到位"直接 return）。
     */
    private fun setOrientLock(lock: Boolean) {
        if (lock == orientLocked) { renderOrientLock(); return }
        orientLocked = lock
        if (lock) {
            lockedOrient = if (resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE) 1 else 2
            requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LOCKED
            Log.i(TAG, "方向锁定：orient=$lockedOrient")
        } else {
            requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
            forcedOrient = 0
            lockedOrient = 0
            Log.i(TAG, "方向解锁：交还系统，物理检测恢复")
        }
        getSharedPreferences(PREF_ORIENT, MODE_PRIVATE).edit().putBoolean(KEY_LOCK, lock).apply()
        renderOrientLock()
    }

    private fun toggleOrientLock() = setOrientLock(!orientLocked)

    /** 双入口状态同步：顶栏按钮三态文案（锁定中显示锁定方向）+ 设置开关勾选态（程序化 setChecked 不回调 listener） */
    private fun renderOrientLock() {
        findViewById<TextView>(R.id.btnOrientLock)?.apply {
            text = when {
                !orientLocked -> context.getString(R.string.ui_main_btn_orient_lock)
                lockedOrient == 1 -> context.getString(R.string.ui_main_btn_orient_locked_land)
                else -> context.getString(R.string.ui_main_btn_orient_locked_port)
            }
        }
        findViewById<Switch>(R.id.swOrientLock)?.isChecked = orientLocked
    }

    /** orientPoll 上次 re-request 方向的时刻（3s 冷却，防每秒请求风暴） */
    private var lastPollFixMs = 0L''',
    ),
    (
        "android/app/src/main/java/com/allperiph/ui/MainActivity.kt",
        '''    private val orientPoll = object : Runnable {
        override fun run() {
            val configLand = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE''',
        '''    private val orientPoll = object : Runnable {
        override fun run() {
            if (orientLocked) {
                // 锁定中：只保持心跳，不比对不 re-request（避免解锁瞬间积压的方向请求）
                handler.postDelayed(this, 1000)
                return
            }
            val configLand = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE''',
    ),
    (
        "android/app/src/main/java/com/allperiph/ui/MainActivity.kt",
        '''            override fun onOrientationChanged(deg: Int) {
                if (deg == ORIENTATION_UNKNOWN) return
                if (!::pager.isInitialized) return''',
        '''            override fun onOrientationChanged(deg: Int) {
                if (deg == ORIENTATION_UNKNOWN) return
                if (orientLocked) return   // 锁定中：物理旋转不再接管方向
                if (!::pager.isInitialized) return''',
    ),
    (
        "android/app/src/main/java/com/allperiph/ui/MainActivity.kt",
        '''        findViewById<Button>(R.id.btnExportTheme).setOnClickListener { exportTheme() }''',
        '''        // 方向锁定：顶栏按钮（竖屏快捷）+ 设置页开关（横屏经四指滑可达），两入口由 renderOrientLock 同步
        findViewById<TextView>(R.id.btnOrientLock)?.setOnClickListener {
            Feedback.tap(it)
            toggleOrientLock()
        }
        findViewById<Switch>(R.id.swOrientLock)?.apply {
            isChecked = orientLocked
            setOnCheckedChangeListener { _, on -> setOrientLock(on) }
        }
        findViewById<Button>(R.id.btnExportTheme).setOnClickListener { exportTheme() }''',
    ),
    (
        "android/app/src/main/java/com/allperiph/ui/MainActivity.kt",
        '''        if (::pager.isInitialized) {
            val target = if (landscape) PAGE_KEYBOARD else PAGE_TOUCHPAD
            if (pager.displayedChild != target) showPage(target)
        }

        if (landscape) navGlass.setIndicator(null)''',
        '''        if (::pager.isInitialized) {
            val target = if (landscape) PAGE_KEYBOARD else PAGE_TOUCHPAD
            if (pager.displayedChild != target) showPage(target)

            // 横屏下状态/设置页左右限宽居中：两页复用竖屏整宽块，宽屏拉满后卡片行过长、
            // 视线要横跨整屏。留白随屏宽自适应（窄屏不强切）。子项顺序 = 页常量：
            // 0 触控板 1 键盘 2 状态 3 设置（两套 activity_main.xml 的 ViewFlipper 顺序）。
            val sidePad = if (landscape) {
                ((resources.displayMetrics.widthPixels - dp(680)).coerceAtLeast(0)) / 2
            } else 0
            for (i in PAGE_STATUS..PAGE_SETTINGS) {
                val child = pager.getChildAt(i) ?: continue
                val lp = child.layoutParams as? android.widget.FrameLayout.LayoutParams ?: continue
                if (lp.marginStart != sidePad) {
                    lp.marginStart = sidePad
                    lp.marginEnd = sidePad
                    child.layoutParams = lp
                }
            }
        }

        if (landscape) navGlass.setIndicator(null)''',
    ),
]


def run(*args: str) -> None:
    subprocess.run(args, check=True)


def main() -> None:
    for path, old, new in EDITS:
        with open(path, encoding="utf-8") as f:
            src = f.read()
        hits = src.count(old)
        if hits != 1:
            print(f"::error::{path}: old_text 命中 {hits} 次（期望 1）—— 中止，未提交任何更改")
            sys.exit(1)
        with open(path, "w", encoding="utf-8") as f:
            f.write(src.replace(old, new, 1))
        print(f"applied: {path}")

    # 自毁：一次性工具不进 PR（运行中的进程不受删文件影响）
    run("git", "rm", "-f", ".github/workflows/apply-edits.yml", "scripts/apply_orient_lock.py")
    run("git", "add", "-A")
    run(
        "git",
        "-c", "user.name=github-actions[bot]",
        "-c", "user.email=41898282+github-actions[bot]@users.noreply.github.com",
        "commit", "-m",
        "feat(orient): 方向锁定按钮 + 横竖屏界面优化（顶栏/设置双入口、锁定暂停轮询与物理检测、横屏状态/设置页限宽居中）",
    )
    run("git", "push", "origin", "HEAD:refs/heads/feat/orient-lock")
    print("done: 9 edits applied, tools self-removed, pushed to feat/orient-lock")


if __name__ == "__main__":
    main()
