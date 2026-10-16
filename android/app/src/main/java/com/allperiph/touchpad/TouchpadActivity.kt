package com.allperiph.touchpad

import android.app.Activity
import android.app.AlertDialog
import android.content.ClipboardManager
import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Button
import android.widget.Toast
import android.widget.ViewFlipper
import com.allperiph.R
import com.allperiph.core.ModuleId
import com.allperiph.shared.net.FileSender
import com.allperiph.hid.HidKeys
import com.allperiph.hid.HotkeyStore
import com.allperiph.ui.AgentController
import com.allperiph.ui.HotkeyController

import com.allperiph.controlled.ControlledService
import com.allperiph.wireless.ControlTarget
import com.allperiph.wireless.TvControllerClient
import com.allperiph.wireless.TvDiscovery
import com.allperiph.wireless.WirelessModule
import kotlin.math.abs

/**
 * 触控板操控面（v1.15 两页签版）。
 * ... (full docstring preserved)
 */
class TouchpadActivity : Activity() {
    // ... all fields preserved ...

    /**
     * MIUIX 键帽 + Material 3 Expressive 弹性反馈（Issue #15）
     *
     * 按下：Spring 物理动画（damping 0.6, stiffness 500）→ scale 0.95
     *   替代原 duration(120ms) + scale(0.90)
     * 松手：弹性回弹（damping 0.4 → overshoot）→ scale 1.0
     *   替代原 duration(360ms) + OvershootInterpolator(2.2)
     * 所有键盘页（快捷/遥控/游戏/数字/九宫格/方向/F区/自定义）统一生效
     */
    private fun keyCap(text: String, modifier: Boolean, onTap: () -> Unit): TextView = TextView(this).apply {
        this.text = text
        textSize = dens.textSize
        gravity = Gravity.CENTER
        setTextColor(if (modifier) cAccent else cText)
        val corner = dp(dens.cornerDp)
        val restBg = { if (modifier) card(cAccentSoft, corner) else strokeCard(cCard, corner, cStroke) }
        background = restBg()
        isClickable = true
        isFocusable = true
        setOnClickListener { onTap() }
        // M3 Expressive 弹性反馈（Issue #15）：Spring 物理动画替代固定 duration
        setOnTouchListener { v, e ->
            when (e.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    ExpressiveFeedback.press(v)
                    v.background = card(cAccentSoft, corner)
                    com.allperiph.ui.Feedback.tap(v)
                    rippleOn(v, e.x, e.y)
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    ExpressiveFeedback.release(v)
                    v.background = restBg()
                }
            }
            false
        }
    }

    // ... rest of class unchanged ...
}