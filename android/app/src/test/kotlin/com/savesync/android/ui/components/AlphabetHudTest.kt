package com.savesync.android.ui.components

import org.junit.Assert.assertEquals
import org.junit.Test

/** M5-3: the hold's mode is observable state, and each new letter ticks once. */
class AlphabetHudTest {

    @Test
    fun `one tick per letter change`() {
        val nav = HoldNavState()
        var ticks = 0
        nav.tick = { ticks++ }
        nav.beginHold()
        nav.setMode(HoldMode.Alpha)
        nav.announceLetter("A")
        nav.announceLetter("A")   // end of list: the same letter again
        nav.announceLetter("B")
        assertEquals(2, ticks)
        assertEquals("B", nav.letter.value)
    }

    @Test
    fun `a new hold ticks its first letter even if it repeats the last one`() {
        val nav = HoldNavState()
        var ticks = 0
        nav.tick = { ticks++ }
        nav.announceLetter("M")
        nav.setMode(HoldMode.Idle)
        nav.beginHold()
        nav.announceLetter("M")
        assertEquals(2, ticks)
    }

    @Test
    fun `an empty letter shows nothing and never ticks`() {
        val nav = HoldNavState()
        var ticks = 0
        nav.tick = { ticks++ }
        nav.announceLetter("")
        assertEquals(0, ticks)
        assertEquals("", nav.letter.value)
    }

    @Test
    fun `mode is a state that follows the hold`() {
        val nav = HoldNavState()
        assertEquals(HoldMode.Idle, nav.mode.value)
        nav.beginHold()
        assertEquals(HoldMode.Page, nav.mode.value)
        nav.setMode(HoldMode.Alpha)
        assertEquals(HoldMode.Alpha, nav.mode.value)
        nav.setMode(HoldMode.Idle)
        assertEquals(HoldMode.Idle, nav.mode.value)
    }

    @Test
    fun `the HUD lingers half a second`() {
        assertEquals(500L, ALPHA_HUD_LINGER_MS)
    }
}
