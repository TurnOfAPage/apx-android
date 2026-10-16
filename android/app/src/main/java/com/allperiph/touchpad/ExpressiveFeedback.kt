package com.allperiph.touchpad

import android.animation.FloatValueHolder
import android.animation.SpringAnimation
import android.animation.SpringForce
import android.view.View

/**
 * Material 3 Expressive 弹性触控反馈 —— Issue #15
 *
 * 仅用 framework API（android.animation.SpringAnimation），
 * 不引入第三方依赖，符合 build.gradle 的依赖约束。
 *
 * 实现要点：
 * - SpringAnimation(FloatValueHolder) 构造函数接受独立值持有器，
 *   避免 View.SCALE_X (Property) 与 FloatProperty 类型不匹配
 * - 通过 addUpdateListener 把动画值写回 View
 */
object ExpressiveFeedback {

    private const val PRESS_DAMPING = 0.6f
    private const val PRESS_STIFFNESS = 500f
    private const val RELEASE_DAMPING = 0.4f
    private const val RELEASE_STIFFNESS = 800f
    private const val CANCEL_DAMPING = 0.5f
    private const val CANCEL_STIFFNESS = 600f

    fun press(v: View, scale: Float = 0.95f) {
        springScale(v, scale, PRESS_DAMPING, PRESS_STIFFNESS)
    }

    fun release(v: View) {
        springScale(v, 1f, RELEASE_DAMPING, RELEASE_STIFFNESS)
    }

    fun cancel(v: View) {
        springScale(v, 1f, CANCEL_DAMPING, CANCEL_STIFFNESS)
    }

    private fun springScale(v: View, targetScale: Float, dampingRatio: Float, stiffness: Float) {
        v.animate().cancel()

        val startX = v.scaleX
        val startY = v.scaleY

        // X 轴弹簧
        val holderX = FloatValueHolder(startX)
        val animX = SpringAnimation(holderX)
        animX.setSpring(
            SpringForce(targetScale)
                .setDampingRatio(dampingRatio)
                .setStiffness(stiffness)
        )
        animX.addUpdateListener { _, value, _ -> v.scaleX = value }

        // Y 轴弹簧
        val holderY = FloatValueHolder(startY)
        val animY = SpringAnimation(holderY)
        animY.setSpring(
            SpringForce(targetScale)
                .setDampingRatio(dampingRatio)
                .setStiffness(stiffness)
        )
        animY.addUpdateListener { _, value, _ -> v.scaleY = value }

        animX.start()
        animY.start()
    }

    fun morphTo(v: View, targetAlpha: Float, targetScale: Float = 1f) {
        v.animate().cancel()
        v.animate().alpha(targetAlpha).setDuration(200).start()
        springScale(v, targetScale, RELEASE_DAMPING, RELEASE_STIFFNESS)
    }
}