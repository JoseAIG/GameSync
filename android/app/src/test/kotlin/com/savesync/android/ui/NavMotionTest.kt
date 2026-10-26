package com.savesync.android.ui

import org.junit.Assert.assertEquals
import org.junit.Test

class NavMotionTest {

    @Test
    fun `tab to its pushed screen is a push`() {
        assertEquals(NavMotion.PUSH, navMotionFor("saves", "detail/{titleId}", reduce = false))
        assertEquals(NavMotion.PUSH, navMotionFor("settings", "emulators", reduce = false))
    }

    @Test
    fun `pushed screen back to its parent tab is a pop`() {
        assertEquals(NavMotion.POP, navMotionFor("detail/{titleId}", "saves", reduce = false))
        assertEquals(NavMotion.POP, navMotionFor("emulators", "settings", reduce = false))
    }

    @Test
    fun `R1 slides content forward`() {
        assertEquals(NavMotion.TAB_FORWARD, navMotionFor("saves", "catalog", reduce = false))
        assertEquals(NavMotion.TAB_FORWARD, navMotionFor("downloads", "settings", reduce = false))
    }

    @Test
    fun `L1 slides content back`() {
        assertEquals(NavMotion.TAB_BACK, navMotionFor("catalog", "saves", reduce = false))
        assertEquals(NavMotion.TAB_BACK, navMotionFor("settings", "downloads", reduce = false))
    }

    @Test
    fun `a tap two tabs away slides by its sign`() {
        assertEquals(NavMotion.TAB_FORWARD, navMotionFor("saves", "installed", reduce = false))
        assertEquals(NavMotion.TAB_BACK, navMotionFor("settings", "catalog", reduce = false))
    }

    @Test
    fun `a wrap across the whole strip fades`() {
        // R1 on Settings → Saves, L1 on Saves → Settings (or a first↔last tap).
        assertEquals(NavMotion.FADE, navMotionFor("settings", "saves", reduce = false))
        assertEquals(NavMotion.FADE, navMotionFor("saves", "settings", reduce = false))
    }

    @Test
    fun `pushed screen to an unrelated tab fades`() {
        // L1/R1 from the detail screen.
        assertEquals(NavMotion.FADE, navMotionFor("detail/{titleId}", "catalog", reduce = false))
        assertEquals(NavMotion.FADE, navMotionFor("emulators", "downloads", reduce = false))
    }

    @Test
    fun `reduced motion fades everywhere`() {
        assertEquals(NavMotion.FADE, navMotionFor("saves", "detail/{titleId}", reduce = true))
        assertEquals(NavMotion.FADE, navMotionFor("detail/{titleId}", "saves", reduce = true))
        assertEquals(NavMotion.FADE, navMotionFor("saves", "catalog", reduce = true))
        assertEquals(NavMotion.FADE, navMotionFor("catalog", "saves", reduce = true))
    }

    @Test
    fun `unknown or missing routes fade`() {
        assertEquals(NavMotion.FADE, navMotionFor(null, "saves", reduce = false))
        assertEquals(NavMotion.FADE, navMotionFor("saves", null, reduce = false))
    }
}
