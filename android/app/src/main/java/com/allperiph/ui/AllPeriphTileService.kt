package com.allperiph.ui

import android.content.Intent
import android.service.quicksettings.Tile
import android.service.quicksettings.TileService
import android.util.Log
import com.allperiph.wireless.ControlTarget

/**
 * 快捷设置 Tile：下拉通知栏一键开关触控板模式。
 * 当 TCP 已连接时显示「已连接」，否则显示「未连接」。
 */
class AllPeriphTileService : TileService() {

    override fun onStartListening() {
        super.onStartListening()
        updateTile()
    }

    override fun onClick() {
        super.onClick()
        // v196 之前这里只调 updateTile() —— 点了 Tile 什么也不发生，
        // 「一键开关触控板」名不副实（用户反馈：Tile 点不动）。
        // 现在点 Tile 直接打开 App 的控制页（ControlShortcutActivity 已在 manifest 注册）：
        // 开关放在页面上点，比在 4×2 的小 Tile 上做多态交互更靠谱。
        runCatching {
            startActivityAndCollapse(
                Intent(this, ControlShortcutActivity::class.java).apply {
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                }
            )
        }
        updateTile()
    }

    private fun updateTile() {
        val tile = qsTile ?: return
        val connected = ControlTarget.isControlling()
        tile.state = if (connected) Tile.STATE_ACTIVE else Tile.STATE_INACTIVE
        val appName = getString(R.string.app_name)
        tile.label = if (connected) getString(R.string.ui_tile_label_on, appName)
        else getString(R.string.ui_tile_label_off, appName)
        // 副标题写清「点它会发生什么」—— 原来写"点击刷新状态"（点了也只是自己刷新自己）
        tile.subtitle = if (connected) {
            getString(
                R.string.ui_tile_subtitle_on,
                ControlTarget.label.ifBlank { getString(R.string.common_device) },
            )
        } else {
            getString(R.string.ui_tile_subtitle_off)
        }
        tile.updateTile()
    }

    companion object {
        private const val TAG = "AllPeriphTile"
    }
}