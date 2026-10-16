package com.allperiph.touchpad

import android.animation.Animator
import android.animation.AnimatorListenerAdapter
import android.animation.ValueAnimator
import android.view.View
import android.view.animation.OvershootInterpolator
import android.view.animation.AccelerateDecelerateInterpolator

/**
 * Material 3 Expressive 弹性触控反馈 —— Issue #15
 *
 * 仅用 framework API（android.animation），不引入第三方依赖，
 * 符合 build.gradle "生产代码不引入任何第三方依赖" 的约束。
 *
 * 注意：SpringAnimation/SpringForce 属于 androidx.dynamicanimation 库，
 * 项目不能用，故用 ValueAnimator + OvershootInterpolator 模拟弹簧物理。
 */
object ExpressiveFeedback {

    private const val PRESS_DURATION = 120L
    private const val RELEASE_DURATION = 360L
    private const val CANCEL_DURATION = 200L
    private const val OVERSHOOT_TENSION = 2.2f

    /** 按下：快速缩到 scale（略过阻尼感） */
    fun press(v: View, scale: Float = 0.95f) {
        cancelRunning(v)
        animateScale(v, v.scaleX, scale, PRESS_DURATION, AccelerateDecelerateInterpolator())
    }

    /** 松手：弹性回弹到 1.0（overshoot 过冲后稳定） */
    fun release(v: View) {
        cancelRunning(v)
        animateScale(v, v.scaleX, 1f, RELEASE_DURATION, OvershootInterpolator(OVERSHOOT_TENSION))
    }

    /** 取消（手指滑出）：快速归位，不弹跳 */
    fun cancel(v: View) {
        cancelRunning(v)
        animateScale(v, v.scaleX, 1f, CANCEL_DURATION, AccelerateDecelerateInterpolator())
    }

    /** 状态变化 morph：透明度 + 缩放一起过渡 */
    fun morphTo(v: View, targetAlpha: Float, targetScale: Float = 1f) {
        cancelRunning(v)
        v.animate().alpha(targetAlpha).setDuration(200)
            .setInterpolator(AccelerateDecelerateInterpolator()).start()
        animateScale(v, v.scaleX, targetScale, RELEASE_DURATION, OvershootInterpolator(OVERSHOOT_TENSION))
    }

    private const val ANIM_TAG = 0x7f0a0001

    private fun cancelRunning(v: View) {
        v.animate().cancel()
        (v.getTag(ANIM_TAG) as? ValueAnimator)?.cancel()
    }

    private fun animateScale(
        v: View,
        from: Float,
        to: Float,
        duration: Long,
        interpolator: android.view.animation.Interpolator
    ) {
        val anim = ValueAnimator.ofFloat(from, to).apply {
            this.duration = duration
            this.interpolator = interpolator
            addUpdateListener { a ->
                val value = a.animatedValue as Float
                v.scaleX = value
                v.scaleY = value
            }
            addListener(object : AnimatorListenerAdapter() {
                override fun onAnimationEnd(a: Animator) {
                    if (v.getTag(ANIM_TAG) === this) v.setTag(ANIM_TAG, null)
                }
            })
        }
        v.setTag(ANIM_TAG, anim)
        anim.start()
    }
}