package com.allperiph.ui

import com.allperiph.core.Uplink

/**
 * 手柄出口的**给用户看的一句话**（纯逻辑，无 Android 依赖 → 可进 JVM 单测）。
 *
 * 为什么要有它：手柄页此前既不显示出口、也不解释为什么没反应。用户按下去，
 * 手机端按钮没有按下反馈、PC 端也毫无动静，于是只能得出「手柄功能不可用」。
 * 实际原因分三类，界面必须能区分：
 *   ① **没有出口**（没连 PC 客户端、USB 没挂上、蓝牙没配对）→ 输入被直接丢弃；
 *   ② **无线出口**：球在 PC 端 —— Linux 端是真 uinput 手柄；Windows 端没装 ViGEmBus
 *      时只能降级成键盘/鼠标；macOS 端**注入不了真手柄**（`IOHIDUserDeviceCreate` 需要
 *      Apple 受限 entitlement `com.apple.developer.hid.virtual.device`，root 不够、
 *      ad-hoc 签名会被 AMFI 直接 SIGKILL，属第三方拿不到的能力，不是没做）；
 *   ③ **蓝牙 / USB 出口**：手机自己就是 HID 设备，PC 免驱认出真手柄，**不依赖对端软件**。
 *
 * 所以结论要按出口给：只有 ③ 是「插上/配对就一定是真手柄」。文案放在这里而不是
 * `strings.xml`：它要能被 `GamepadExitTest` 在无 Context 的 JVM 里钉死（与
 * `Uplink.describe` 同一取舍，仓库里中文文案本来就大量内联在纯 Kotlin 对象里）。
 */
object GamepadExit {

    /** 一个出口对应的展示信息。 */
    data class Info(
        /** 状态行：出口是什么 + 是否免驱真手柄。 */
        val headline: String,
        /** 补充说明 / 该做什么；为空表示不需要额外提示。 */
        val advice: String,
        /** 该出口是否**免驱**就是真手柄（与对端是否装驱动无关）。 */
        val driverlessRealPad: Boolean,
    )

    /** 无线出口的已知限制（对端平台不同，结果不同）。 */
    const val WIRELESS_CAVEAT: String =
        "无线出口能否变成真手柄要看对端：Linux 端是真手柄；" +
            "Windows 端没装 ViGEmBus 时降级成键盘 / 鼠标；" +
            "macOS 端没有手柄通道（虚拟 HID 设备要 Apple 受限授权，第三方拿不到），" +
            "已按「按钮→键盘、左摇杆→鼠标、右摇杆→滚动」降级。" +
            "想要免驱、免装驱动的真手柄，请改用蓝牙或 USB 出口。"

    /** 无出口时的操作指引 —— 三条都指向「怎么给它一个出口」。 */
    const val NO_EXIT_ADVICE: String =
        "输入会被直接丢弃，先给手柄找一个出口：" +
            "① 在 PC 客户端（或浏览器面板）里连接本机，走无线；" +
            "② 或用 USB 线连上并开启「USB 外设」，走 USB HID；" +
            "③ 或让 PC 配对蓝牙 HID。后两者免驱，PC 直接认成手柄。"

    /**
     * 按当前出口给出手柄页要显示的两行字。
     *
     * @param path [Uplink] 的出口常量：`WIRELESS` / `USB` / `BLUETOOTH` / `NONE`
     */
    fun describe(path: String): Info = when (path) {
        Uplink.BLUETOOTH -> Info(
            headline = "出口：蓝牙 HID（免驱，PC 认成真手柄）",
            advice = "",
            driverlessRealPad = true,
        )

        Uplink.USB -> Info(
            headline = "出口：USB HID（免驱，PC 认成真手柄）",
            advice = "",
            driverlessRealPad = true,
        )

        Uplink.WIRELESS -> Info(
            headline = "出口：无线 9511（真手柄取决于对端）",
            advice = WIRELESS_CAVEAT,
            driverlessRealPad = false,
        )

        else -> Info(
            headline = "出口：无（输入会被丢弃）",
            advice = NO_EXIT_ADVICE,
            driverlessRealPad = false,
        )
    }
}
