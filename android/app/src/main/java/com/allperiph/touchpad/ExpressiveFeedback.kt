package com.allperiph.touchpad

import android.animation.TimeInterpolator
import android.animation.ValueAnimator
import android.view.View
import android.view.animation.OvershootInterpolator
import androidx.dynamicanimation.animation.DynamicAnimation
import androidx.dynamicanimation.animation.FloatValueHolder
import androidx.dynamicanimation.animation.SpringAnimation
import androidx.dynamicanimation.animation.SpringForce
import kotlin.math.abs

/**
 * Material 3 Expressive 弹性触控反馈（View 版）—— Issue #15
 *
 * 适配 TouchpadActivity 的传统 View 架构，提供 Spring 物理动画。
 * 替代原来基于 duration + OvershootInterpolator 的固定动画，
 * 获得真实弹簧物理反馈（阻尼/刚度可调）。
 *
 * 用法：
 * ```kotlin
 * // 按下缩回
 * ExpressiveFeedback.press(v)
 * // 松手回弹
 * ExpressiveFeedback.release(v)
 * ```
 */
object ExpressiveFeedback {

    // —————— Spring 参数（Material 3 Expressive 规范） ——————

    /** 按下：中低刚度，略过阻尼（轻微下沉感） */
    private const val PRESS_DAMPING = 0.6f
    private const val PRESS_STIFFNESS = 500f

    /** 松手回弹：低阻尼 + 中刚度 → overshoot 弹性 */
    private const val RELEASE_DAMPING = 0.4f
    private const val RELEASE_STIFFNESS = 800f

    /** 取消（手指滑出）：中阻尼，快速归位 */
    private const val CANCEL_DAMPING = 0.5f
    private const val CANCEL_STIFFNESS = 600f

    // —————— 按压缩放 ——————

    /**
     * 按下：scale → 0.95（Spring 物理动画）
     * 替代原来的 v.animate().scaleX(0.90f).setDuration(120)
     */
    fun press(v: View, scale: Float = 0.95f) {
        springScale(v, scale, PRESS_DAMPING, PRESS_STIFFNESS)
    }

    /**
     * 松手：弹性回弹到 1.0（带 overshoot）
     * 替代原来的 v.animate().scaleX(1f).setDuration(360).setInterpolator(OvershootInterpolator(2.2f))
     */
    fun release(v: View) {
        springScale(v, 1f, RELEASE_DAMPING, RELEASE_STIFFNESS)
    }

    /**
     * 取消（手指滑出屏幕）：快速归位，不弹跳
     */
    fun cancel(v: View) {
        springScale(v, 1f, CANCEL_DAMPING, CANCEL_STIFFNESS)
    }

    // —————— Spring 缩放核心 ——————

    private fun springScale(
        v: View,
        targetScale: Float,
        dampingRatio: Float,
        stiffness: Float
    ) {
        // 复用已有动画，避免冲突
        v.animate().cancel()

        val anim = SpringAnimation(FloatValueHolder(v.scaleX))
            .setSpring(
                SpringForce(targetScale)
                    .setDampingRatio(dampingRatio)
                    .setStiffness(stiffness)
            )
        anim.addUpdateListener { a, value, _ ->
            v.scaleX = value
            v.scaleY = value
        }
        anim.start()
    }

    // —————— 连接状态 morph ——————

    /**
     * 状态变化时的 morph 过渡：缩放 + 透明度一起弹
     * 用于设备连接指示器/状态胶囊
     */
    fun morphTo(v: View, targetAlpha: Float, targetScale: Float = 1f) {
        v.animate().cancel()
        // alpha 用 duration（视觉渐变，不需要弹簧）
        v.animate()
            .alpha(targetAlpha)
            .setDuration(200)
            .start()
        // scale 用 Spring（弹性过渡）
        springScale(v, targetScale, RELEASE_DAMPING, RELEASE_STIFFNESS)
    }

    // —————— 滚动惯性 ——————

    /**
     * 用于触控板滚动的惯性动画：给定初始速度，Spring 自然衰减
     * 返回 ValueAnimator，调用方负责把值映射到滚动位置
     */
    fun scrollInertia(
        from: Float,
        velocity: Float,
        onUpdate: (Float) -> Unit
    ): SpringAnimation {
        val holder = FloatValueHolder(from)
        val anim = SpringAnimation(holder)
            .setSpring(
                SpringForce(from)
                    .setStartVelocity(velocity)
                    .setDampingRatio(0.85f)
                    .setStiffness(SpringForce.STIFFNESS_LOW)
            )
        anim.addUpdateListener { _, value, _ -> onUpdate(value) }
        anim.start()
        return anim
    }

    // —————— 连接状态指示器颜色过渡 ——————

    /**
     * 状态胶囊的颜色 morph：ARGB 渐变 + 轻微弹跳
     */
    fun morphColor(v: View, fromColor: Int, toColor: Int) {
        val anim = ValueAnimator.ofFloat(0f, 1f)
        anim.duration = 300
        anim.interpolator = OvershootInterpolator(1.5f)
        anim.addUpdateListener { a ->
            val f = a.animatedValue as Float
            val r = ((fromColor shr 16 and 0xFF) * (1 - f) + (toColor shr 16 and 0xFF) * f).toInt()
            val g = ((fromColor shr 8 and 0xFF) * (1 - f) + (toColor shr 8 and 0xFF) * f).toInt()
            val b = ((fromColor and 0xFF) * (1 - f) + (toColor and 0xFF) * f).toInt()
            val a2 = ((fromColor ushr 24 and 0xFF) * (1 - f) + (toColor ushr 24 and 0xFF) * f).toInt()
            v.setBackgroundColor((a2 shl 24) or (r shl 16) or (g shl 8) or b)
        }
        anim.start()
    }
}