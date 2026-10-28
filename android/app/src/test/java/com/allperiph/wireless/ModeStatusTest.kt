package com.allperiph.wireless

import com.allperiph.core.Uplink
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * 手机模式状态帧（0x31）位布局的 Kotlin 侧钉子。
 *
 * 这份位图两端各写一遍（Android 这里 / PC 侧 `pc/host/include/apxpc/wireless/phone_mode.hpp`），
 * 位序错了不会崩、也不会有人报 bug —— 只会「面板显示蓝牙当前在用、手机其实走的无线」。
 * 期望值与 C++ 用例 `pc/host/tests/phone_mode_test.cpp` **逐字对应**，两边必须一起改。
 */
class ModeStatusTest {

    @Test
    fun bitLayoutIsStable() {
        assertEquals(0x01, ModeStatus.WIRELESS_AVAILABLE)
        assertEquals(0x02, ModeStatus.BLUETOOTH_AVAILABLE)
        assertEquals(0x04, ModeStatus.USB_AVAILABLE)
        assertEquals(0x08, ModeStatus.WIRELESS_ACTIVE)
        assertEquals(0x10, ModeStatus.BLUETOOTH_ACTIVE)
        assertEquals(0x20, ModeStatus.USB_ACTIVE)
    }

    @Test
    fun packMatchesCppExpectations() {
        // 与 phone_mode_test 同一组期望值
        assertEquals(0x09, ModeStatus.pack(true, false, false, "wireless"))
        assertEquals(0x17, ModeStatus.pack(true, true, true, "bluetooth"))
        assertEquals(0x07, ModeStatus.pack(true, true, true, "none"))
        assertEquals(0x00, ModeStatus.pack(false, false, false, "wifi"))
        assertEquals(0x24, ModeStatus.pack(false, false, true, "usb"))
    }

    @Test
    fun activeNameFollowsUplinkConstants() {
        assertEquals("wireless", ModeStatus.activeName(Uplink.WIRELESS))
        assertEquals("bluetooth", ModeStatus.activeName(Uplink.BLUETOOTH))
        assertEquals("usb", ModeStatus.activeName(Uplink.USB))
        // 无出口 / 未知一律 none —— 不能把「可用」当成「在用」
        assertEquals("none", ModeStatus.activeName(Uplink.NONE))
        assertEquals("none", ModeStatus.activeName("whatever"))
    }

    @Test
    fun availabilityBitsAreIndependentOfActiveBits() {
        // 只有可用性、没有「在用」位
        assertEquals(0, ModeStatus.pack(true, true, true, "none") and
            (ModeStatus.WIRELESS_ACTIVE or ModeStatus.BLUETOOTH_ACTIVE or ModeStatus.USB_ACTIVE))
        // 用 usb 时，前三条可用性位必须原样保留（0x07）
        assertEquals(0x07, ModeStatus.pack(true, true, true, "usb") and 0x07)
    }
}
