package com.allperiph.tv

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import com.allperiph.shared.util.Log

/**
 * 开机 / 应用更新后自动拉起被控服务。
 *
 * 电视与盒子常年插电挂在墙上，视为**常驻家电**：开机即自启，不要求用户此前点过
 * 「开始接受控制」——否则用户每次断电重开都要翻出遥控器点一下 App 才能被手机发现。
 *
 * 唯一无法触发的情况是系统对「已被强停（force-stop）的应用」不投递广播，这是 Android
 * 的限制；用户手动打开一次 App 即恢复（[MainActivity] 启动时也会拉起服务）。
 *
 * v1.98 fix: 改用 [TvServerService.startIfNeeded] 代替 [TvServerService.start]，
 * 使开机自启尊重用户此前手动停止服务的操作（检查 KEY_USER_STOPPED 标志）。
 * 此前无论用户是否手动关过服务，开机后都会无条件拉起，不符合用户预期。
 */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent?) {
        val act = intent?.action ?: return
        if (act != Intent.ACTION_BOOT_COMPLETED && act != Intent.ACTION_MY_PACKAGE_REPLACED) return
        // startIfNeeded() 内部先检查 isUserStopped()：
        //   - 用户未手动停止 → 等价于 start()，拉起服务并记 enabled=true
        //   - 用户手动停止过 → 跳过，尊重用户意愿
        // start() 会无条件清掉 user_stopped 并 enabled=true，不符合预期。
        TvServerService.startIfNeeded(context)
        Log.i("开机自启：已检查用户偏好并按需拉起 TV 被控服务")
    }
}
