package com.allperiph.wireless

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities

/**
 * 手机「三种接入模式」状态上报（9511 控制面 0x31）。
 *
 * 为什么要有它（issue#47 面板侧诉求）：Web 面板连接卡上有「无线 / 蓝牙 / USB」三个开关，
 * 但**只有无线是真开关**（启停 PC 侧的 9511 会话），蓝牙与 USB 的真开关在手机端
 * （系统配对 / root 配 gadget）—— PC 只是被动 host，摆了开关拨下去什么都不会发生，
 * 用户只能认定「坏了」。改成：手机把自己三条出口的**可用性 + 当前在用哪条**报上去，
 * 面板照实显示。
 *
 * 位序与 `pc/host/include/apxpc/wireless/phone_mode.hpp` 一一对应，两侧各有一条用例
 * 用**同一组期望值**钉住（`phone_mode_test` / `ModeStatusTest`）—— 这个仓库被
 * 「两端各写一遍、悄悄漂移」坑过太多次，位序错不会崩，只会显示错模式。
 */
object ModeStatus {

    const val WIRELESS_AVAILABLE = 0x01
    const val BLUETOOTH_AVAILABLE = 0x02
    const val USB_AVAILABLE = 0x04
    const val WIRELESS_ACTIVE = 0x08
    const val BLUETOOTH_ACTIVE = 0x10
    const val USB_ACTIVE = 0x20

    /**
     * 出口常量（[com.allperiph.core.Uplink]）→ 协议里的「在用」名字。
     * 未知 / 无出口一律 "none"：**不能**把「可用」当成「在用」。
     */
    fun activeName(path: String): String = when (path) {
        com.allperiph.core.Uplink.WIRELESS -> "wireless"
        com.allperiph.core.Uplink.BLUETOOTH -> "bluetooth"
        com.allperiph.core.Uplink.USB -> "usb"
        else -> "none"
    }

    /** 纯打包：三项可用性 + 当前出口名 → flags 字节（面板读这一字节）。 */
    fun pack(
        wirelessAvailable: Boolean,
        bluetoothAvailable: Boolean,
        usbAvailable: Boolean,
        active: String,
    ): Int {
        var f = 0
        if (wirelessAvailable) f = f or WIRELESS_AVAILABLE
        if (bluetoothAvailable) f = f or BLUETOOTH_AVAILABLE
        if (usbAvailable) f = f or USB_AVAILABLE
        f = f or when (active) {
            "wireless" -> WIRELESS_ACTIVE
            "bluetooth" -> BLUETOOTH_ACTIVE
            "usb" -> USB_ACTIVE
            else -> 0
        }
        return f
    }

    /**
     * 此刻的真实状态。三项分开判：
     *   · 无线可用 = 手机有可用局域网（Wi-Fi / 以太网）。9511 是 TCP 上行，没有局域网就无从谈起；
     *   · 蓝牙可用 = 蓝牙适配器开着（真开关是系统里的 HID 配对，本机无权替系统判断「配没配」）；
     *   · USB 可用 = USB HID gadget 口已就绪（插线 + 手机端「有线（USB）」开关）。
     * 「当前在用」取**真实发出过**的出口（[Uplink.current]）；还没发过输入时退回预计出口
     * （[Uplink.resolve]），与手柄页 / 通知栏 / 快捷键走同一条判据。
     */
    fun currentFlags(): Int {
        val rt = com.allperiph.ui.AgentController.runtime
        val bt = com.allperiph.ui.AgentController.module(com.allperiph.core.ModuleId.BTHID)
            as? com.allperiph.bt.BtHidDevice
        val cc = ControlTarget.controlClient
        val wirelessReady = cc != null && cc.ready
        val btReady = bt?.isConnected == true
        val path = if (com.allperiph.core.Uplink.observed) com.allperiph.core.Uplink.current
        else com.allperiph.core.Uplink.resolve(rt?.hid, btReady, wirelessReady)

        return pack(
            wirelessAvailable = hasLan(),
            bluetoothAvailable = bluetoothEnabled(),
            usbAvailable = rt?.hid?.isReady() == true,
            active = activeName(path),
        )
    }

    /** 有可用局域网（Wi-Fi / 以太网）。取不到（无权限 / 无 Context）时如实返回 false。 */
    fun hasLan(): Boolean = runCatching {
        val ctx: Context = com.allperiph.App.context ?: return false
        val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
            ?: return false
        val net = cm.activeNetwork ?: return false
        val caps = cm.getNetworkCapabilities(net) ?: return false
        caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) ||
            caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)
    }.getOrDefault(false)

    /** 蓝牙适配器是否开着（只看开关；配没配对由系统与 [com.allperiph.bt.BtHidDevice] 决定）。 */
    fun bluetoothEnabled(): Boolean = runCatching {
        android.bluetooth.BluetoothAdapter.getDefaultAdapter()?.isEnabled == true
    }.getOrDefault(false)
}
