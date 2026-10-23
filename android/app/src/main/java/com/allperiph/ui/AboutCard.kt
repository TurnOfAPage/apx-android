package com.allperiph.ui

import android.content.Context
import android.util.AttributeSet
import android.view.LayoutInflater
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import com.allperiph.R

/**
 * 关于卡（设置页 · 关于分段）：版本行 + 检查更新 + 意见反馈。
 *
 * 为什么做成自包含控件而不是去 SettingsFragment 里挂监听：设置页其余三段的
 * 渲染都在那个 31K 文件里，这里只加一张卡 —— 让卡片自己管状态/点击/静默检查，
 * 改动面收敛成「新增两个文件 + 布局里一个标签」，不动既有渲染链路。
 *
 * 行为：
 *  - 构造：版本行显示当前构建标识（发版构建 = release tag，否则 versionName）；
 *  - 静默检查：卡片**第一次可见**时走 [UpdateChecker.autoCheck]（6 小时限频）——
 *    用可见性当"进入关于分段"的信号，避免在别的分段上弹更新窗；
 *  - 手动检查：过程写在版本行，发现新版弹窗跳 Release 页。
 */
class AboutCard @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : LinearLayout(context, attrs) {

    private val tvVersion: TextView

    /** init 完成标志：onVisibilityChanged 可能在构造期（XML 写了 visibility）就被回调，那时字段还没就位。 */
    private var ready = false
    private var autoChecked = false

    init {
        orientation = VERTICAL
        LayoutInflater.from(context).inflate(R.layout.about_card, this, true)
        tvVersion = findViewById(R.id.tvVersion)
        tvVersion.text = context.getString(R.string.about_version, UpdateChecker.currentTag())
        findViewById<Button>(R.id.btnCheckUpdate).setOnClickListener { check(manual = true) }
        findViewById<Button>(R.id.btnFeedback).setOnClickListener { IssueLauncher.open(context) }
        ready = true
    }

    override fun onVisibilityChanged(changedView: View?, visibility: Int) {
        super.onVisibilityChanged(changedView, visibility)
        if (!ready || autoChecked || visibility != VISIBLE) return
        autoChecked = true
        check(manual = false)
    }

    private fun check(manual: Boolean) {
        if (manual) tvVersion.text = context.getString(R.string.about_checking)
        // 在途保护在 UpdateChecker 里：连点也只会发出一次请求
        if (manual) UpdateChecker.check(context) { r -> render(r, true) }
        else UpdateChecker.autoCheck(context) { r -> render(r, false) }
    }

    private fun render(r: UpdateChecker.Result, manual: Boolean) {
        when (r) {
            is UpdateChecker.Result.UpToDate ->
                if (manual) tvVersion.text = context.getString(R.string.about_up_to_date, r.tag)
            is UpdateChecker.Result.NewVersion -> {
                tvVersion.text = context.getString(R.string.about_update_new, r.tag)
                UpdateChecker.showUpdateDialog(context, r.tag, r.url)
            }
            is UpdateChecker.Result.Failed ->
                if (manual) tvVersion.text = context.getString(R.string.about_update_fail)
        }
    }
}
