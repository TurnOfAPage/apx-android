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
 * 用法：
 * ```kotlin
 * ExpressiveFeedback.press(v)   // 按下
 * ExpressiveFeedback.release(v) // 松手
 * ExpressiveFeedback.cancel(v)  // 取消
 * ```
 */
object ExpressiveFeedback {

    // —————— Spring 参数（Material 3 Expressive 规范） ——————
    private const val PRESS_DAMPING = 0.6f
    private const val PRESS_STIFFNESS = 500f
    private const val RELEASE_DAMPING = 0.4f
    private const val RELEASE_STIFFNESS = 800f
    private const val CANCEL_DAMPING = 0.5f
    private const val CANCEL_STIFFNESS = 600f

    // Spring 动画 tag key（避免同一 View 多个 Spring 冲突）
    private const val TAG_SPRING_ID = 0x7f0a0001

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
     * 返回 SpringAnimation，调用方负责把值映射到滚动位置
     */
    fun scrollInertia(
        v: View,
        property: Int,  // View.TRANSLATION_X 或 View.TRANSLATION_Y
        from: Float,
        velocity: Float
    ): SpringAnimation {
        val anim = SpringAnimation(v, property)
            .setSpring(
                SpringForce(from)
                    .setStartVelocity(velocity)
                    .setDampingRatio(0.85f)
                    .setStiffness(SpringForce.STIFFNESS_LOW)
            )
        return anim
    }
}