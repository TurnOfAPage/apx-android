package com.allperiph.ui

import com.allperiph.core.Uplink
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 手柄出口文案的钉子（纯 JVM，无 Android 依赖）。
 *
 * 为什么值得钉：issue#47 里「手柄用不了」的原始诉求，最后落到的是**手机侧根本没说出口是什么**
 * —— 用户按下去没反应，只能得出「功能没做」。而三条出口的真实能力差别很大（只有蓝牙 / USB
 * 免驱就是真手柄，无线出口在 mac 上只能是键鼠降级），文案一旦漂移，界面又会开始撒谎。
 *
 * 另外钉一条防幻觉的：手柄出口的说明里**不得出现与手柄无关的能力**（心率、血氧之类）。
 * 之前那条反馈里用户明确说「没说心率与血氧，你 AI 幻觉了」—— 排期列表里混进无关项、
 * 文案里承诺不存在的能力，是同一类错误，用断言挡住比靠记性可靠。
 */
class GamepadExitTest {

    /** 四条出口常量必须覆盖到互不相同的出口，且只有蓝牙 / USB 是免驱真手柄。 */
    @Test
    fun onlyBluetoothAndUsbAreDriverlessRealPads() {
        assertTrue(GamepadExit.describe(Uplink.BLUETOOTH).driverlessRealPad)
        assertTrue(GamepadExit.describe(Uplink.USB).driverlessRealPad)
        // 无线出口真不真看对端，本机无法承诺 → 必须为 false，且给出说明
        assertFalse(GamepadExit.describe(Uplink.WIRELESS).driverlessRealPad)
        assertFalse(GamepadExit.describe(Uplink.NONE).driverlessRealPad)
    }

    @Test
    fun bluetoothAndUsbNeedNoAdvice() {
        // 免驱出口不需要「你该做什么」，多说只会变噪音
        assertEquals("", GamepadExit.describe(Uplink.BLUETOOTH).advice)
        assertEquals("", GamepadExit.describe(Uplink.USB).advice)
    }

    @Test
    fun wirelessCarriesTheCaveatVerbatim() {
        assertEquals(GamepadExit.WIRELESS_CAVEAT, GamepadExit.describe(Uplink.WIRELESS).advice)
        // 对端平台差异必须逐平台说清：Linux 真手柄 / Windows 要 ViGEmBus / mac 只能降级
        assertTrue(GamepadExit.WIRELESS_CAVEAT.contains("Linux"))
        assertTrue(GamepadExit.WIRELESS_CAVEAT.contains("ViGEmBus"))
        assertTrue(GamepadExit.WIRELESS_CAVEAT.contains("macOS"))
        // 并且给出替代路径，否则用户读完只知道「不行」
        assertTrue(GamepadExit.WIRELESS_CAVEAT.contains("蓝牙"))
    }

    @Test
    fun noExitExplainsAllThreeWaysOut() {
        val info = GamepadExit.describe(Uplink.NONE)
        assertEquals(GamepadExit.NO_EXIT_ADVICE, info.advice)
        assertTrue(info.headline.contains("丢弃"))
        // 三条出口都要点到，少一条就等于少一条可走的路
        assertTrue(GamepadExit.NO_EXIT_ADVICE.contains("无线"))
        assertTrue(GamepadExit.NO_EXIT_ADVICE.contains("USB"))
        assertTrue(GamepadExit.NO_EXIT_ADVICE.contains("蓝牙"))
    }

    @Test
    fun unknownPathFallsBackToNoExitInsteadOfCrashing() {
        // 择路是回落的，来的也可能是没见过的字符串：必须走「无出口」而不是抛异常
        val info = GamepadExit.describe("ethernet")
        assertEquals(GamepadExit.describe(Uplink.NONE), info)
    }

    @Test
    fun fourExitsReadDifferently() {
        val heads = listOf(Uplink.WIRELESS, Uplink.BLUETOOTH, Uplink.USB, Uplink.NONE)
            .map { GamepadExit.describe(it).headline }
        assertEquals(heads.size, heads.toSet().size)
        assertEquals(4, heads.size)
    }

    /** 防幻觉：手柄出口文案不得牵扯与手柄无关的能力（心率 / 血氧等）。 */
    @Test
    fun noUnrelatedCapabilitiesInGamepadCopy() {
        val banned = listOf("心率", "血氧", "血压", "体温", "睡眠")
        for (path in listOf(Uplink.WIRELESS, Uplink.BLUETOOTH, Uplink.USB, Uplink.NONE)) {
            val info = GamepadExit.describe(path)
            for (word in banned) {
                assertNotEquals(true, info.headline.contains(word))
                assertNotEquals(true, info.advice.contains(word))
            }
        }
        assertNotEquals(true, GamepadExit.WIRELESS_CAVEAT.contains("心率"))
        assertNotEquals(true, GamepadExit.NO_EXIT_ADVICE.contains("心率"))
    }
}
