package com.allperiph.touchpad

import android.animation.SpringAnimation
import android.animation.SpringForce
import android.view.View

/**
 * Material 3 Expressive 弹性触控反馈 —— Issue #15
 *
 * 仅用 framework API（android.animation.SpringAnimation），
 * 不引入第三方依赖，符合 build.gradle 的依赖约束。
 */
object ExpressiveFeedback {

    private const val PRESS_DAMPING = 0.6f
    private const val PRESS_STIFFNESS = 500f
    private const val RELEASE_DAMPING = 0.4f
    private const val RELEASE_STIFFNESS = 800f
    private const val CANCEL_DAMPING = 0.5f
    private const val CANCEL_STIFFNESS = 600f
    private const val TAG_SPRING = 0x7f0a0001

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
        val old = v.getTag(TAG_SPRING) as? SpringAnimation
        old?.cancel()

        val springX = SpringAnimation(v, View.SCALE_X)
        springX.setSpring(
            SpringForce(targetScale)
                .setDampingRatio(dampingRatio)
                .setStiffness(stiffness)
        )

        val springY = SpringAnimation(v, View.SCALE_Y)
        springY.setSpring(
            SpringForce(targetScale)
                .setDampingRatio(dampingRatio)
                .setStiffness(stiffness)
        )

        v.setTag(TAG_SPRING, springX)
        springX.start()
        springY.start()
    }

    fun morphTo(v: View, targetAlpha: Float, targetScale: Float = 1f) {
        v.animate().cancel()
        v.animate().alpha(targetAlpha).setDuration(200).start()
        springScale(v, targetScale, RELEASE_DAMPING, RELEASE_STIFFNESS)
    }
}