package com.allperiph.ui

import android.inputmethodservice.InputMethodService
import android.view.KeyEvent
import android.view.View
import android.view.inputmethod.EditorInfo
import android.widget.LinearLayout
import android.widget.TextView
import com.allperiph.R
import com.allperiph.shared.util.Log

/**
 * 全能外设输入法（批 S · issue 实测「app 失焦无法用键盘输入法」）：
 *  切到本输入法后，物理键盘在任意界面打字 → onKeyDown/onKeyUp 全量转发到电脑
 *  （PhysKeyBridge.forwardFromIme，跳过「本地输入态放行」规则），本地不落字。
 *  没连电脑 / 桥未登记时 forward 返回 false → 走 super 自然降级为手机本地输入。
 *  默认不启用——用户在系统输入法设置里主动开启；设置页引导入口随批 S-2。
 */
class ApxImeService : InputMethodService() {

    companion object { private const val TAG = "ApxIme" }

    override fun onCreateInputView(): View {
        // 极简面板：IME 存在性载体与提示；真正输入在 onKeyDown 转发到 PC，本地不显示文字
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(48, 32, 48, 32)
        }
        root.addView(TextView(this).apply {
            text = getString(R.string.ui_ime_panel_hint)
            textSize = 14f
        })
        return root
    }

    override fun onStartInput(attribute: EditorInfo?, restarting: Boolean) {
        super.onStartInput(attribute, restarting)
        Log.d(TAG, "start input pkg=${attribute?.packageName} bridge=${PhysKeyBridge.current != null}")
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
        if (event != null && PhysKeyBridge.current?.forwardFromIme(event) == true) return true
        return super.onKeyDown(keyCode, event)
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent?): Boolean {
        if (event != null && PhysKeyBridge.current?.forwardFromIme(event) == true) return true
        return super.onKeyUp(keyCode, event)
    }
}
