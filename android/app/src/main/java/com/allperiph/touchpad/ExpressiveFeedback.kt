package com.allperiph.touchpad

import android.animation.SpringAnimation
import android.animation.SpringForce
import android.view.View

/**
 * Material 3 Expressive 弹性触控反馈 —— Issue #15
 *
 * 仅用 framework API（android.animation.SpringAnimation），
 * 不引入第三方依赖，符合 build.gradle 的依赖约束。
 *
 * 替代原来基于 duration + OvershootInterpolator 的固定动画，
 * 获得真实弹簧物理反馈（阻尼/刚度可调）。
 *
 * 用法：
 * ```kotlin
 * // 按下
 * ExpressiveFeedback.press(v)
 * // 松手
 * ExpressiveFeedback.release(v)
 * // 取消
 * ExpressiveFeedback.cancel(v)
 * ```
 */
object ExpressiveFeedback {

    // —————— Spring 参数（Material 3 Expressive 规范） ——————
    // 按下：中低刚度，略过阻尼（轻微下沉感）
    private const val PRESS_DAMPING = 0.6f
    private const val PRESS_STIFFNESS = 500f

    // 松手回弹：低阻尼 + 中刚度 → overshoot 弹性
    private const val RELEASE_DAMPING = 0.4f
    private const val RELEASE_STIFFNESS = 800f

    // 取消（手指滑出）：中阻尼，快速归位
    private const val CANCEL_DAMPING = 0.5f
    private const val CANCEL_STIFFNESS = 600f

    // Spring 动画 tag key（避免同一 View 多个 Spring 冲突）
    private val TAG_SPRING = IntArray(1)

    /**
     * 按下：scale → targetScale（Spring 物理动画）
     * 替代 v.animate().scaleX(0.90f).setDuration(120)
     */
    fun press(v: View, scale: Float = 0.95f) {
        springScale(v, scale, PRESS_DAMPING, PRESS_STIFFNESS)
    }

    /**
     * 松手：弹性回弹到 1.0（带 overshoot）
     * 替代 v.animate().scaleX(1f).setDuration(360).setInterpolator(OvershootInterpolator(2.2f))
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
        // 取消已有的 ViewPropertyAnimator，避免冲突
        v.animate().cancel()

        // 取消同一 View 上正在跑的 Spring
        val old = v.getTag(TAG_SPRING_ID) as? SpringAnimation
        old?.cancel()

        val spring = SpringAnimation(v, SpringAnimation.SCALE_X)
            .setSpring(
                SpringForce(targetScale)
                    .setDampingRatio(dampingRatio)
                    .setStiffness(stiffness)
            )
        // 同步 scaleY
        val springY = SpringAnimation(v, SpringAnimation.SCALE_Y)
            .setSpring(
                SpringForce(targetScale)
                    .setDampingRatio(dampingRatio)
                    .setStiffness(stiffness)
            )

        v.setTag(TAG_SPRING_ID, spring)
        spring.start()
        springY.start()
    }

    // —————— 连接状态 morph ——————

    /**
     * 状态变化时的 morph 过渡：缩放 + 透明度一起弹
     */
    fun morphTo(v: View, targetAlpha: Float, targetScale: Float = 1f) {
        v.animate().cancel()
        v.animate()
            .alpha(targetAlpha)
            .setDuration(200)
            .start()
        springScale(v, targetScale, RELEASE_DAMPING, RELEASE_STIFFNESS)
    }

    // —————— 滚动惯性 ——————

    /**
     * 用于触控板滚动的惯性动画：给定初始速度，Spring 自然衰减
     */
    fun scrollInertia(
        v: View,
        property: Int,  // View.TRANSLATION_X 或 View.TRANSLATION_Y
        from: Float,
        velocity: Float,
        onUpdate: (Float) -> Unit
    ): SpringAnimation {
        val anim = SpringAnimation(FloatValueHolder(from)).apply {
            spring = SpringForce(from)
                .setStartVelocity(velocity)
                .setDampingRatio(0.85f)
                .setStiffness(SpringForce.STIFFNESS_LOW)
        }
        // 简化：用 ViewProperty 版本
        val va = SpringAnimation(v, property)
            .setSpring(
                SpringForce(from)
                    .setStartVelocity(velocity)
                    .setDampingRatio(0.85f)
                    .setStiffness(SpringForce.STIFFNESS_LOW)
            )
        return va
    }

    private const val TAG_SPRING_ID = 0x7f0a0001

    // FloatValueHolder 用于不依附 View 的独立 Spring
    private class FloatValueHolder(var value: Float)
}