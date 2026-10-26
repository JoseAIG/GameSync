package com.savesync.android.ui.components

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class GsChromeTest {

    @Test
    fun `tab routes key by route`() {
        assertEquals("saves", chromeKeyFor("saves", null))
        assertEquals("settings", chromeKeyFor("settings", null))
        assertEquals("emulators", chromeKeyFor("emulators", null))
    }

    @Test
    fun `pushed detail routes key by their argument`() {
        assertEquals("detail/0004000000055D00", chromeKeyFor("detail/{titleId}", "0004000000055D00"))
        // A second game never reuses the first game's header.
        assertNotEquals(chromeKeyFor("detail/{titleId}", "A"), chromeKeyFor("detail/{titleId}", "B"))
    }

    @Test
    fun `no route yet is the start tab`() {
        assertEquals("saves", chromeKeyFor(null, null))
        assertTrue(isTabRoute(null))
    }

    @Test
    fun `only top-level tabs draw the tab header`() {
        assertTrue(isTabRoute("downloads"))
        assertFalse(isTabRoute("detail/{titleId}"))
        assertFalse(isTabRoute("emulators"))
    }

    @Test
    fun `specs compare by value so an unchanged spec isn't rewritten`() {
        val hints = listOf(GsHint(GsButton.A, "Sync"))
        assertEquals(GsChromeSpec(hints = hints), GsChromeSpec(hints = listOf(GsHint(GsButton.A, "Sync"))))
        assertEquals(GsChromeSpec(title = "Zelda"), GsChromeSpec(title = "Zelda"))
    }

    @Test
    fun `scroll-edge flags default off and change the spec only when they flip`() {
        val base = GsChromeSpec(title = "Settings")
        assertFalse(base.scrolledUnderTop)
        assertFalse(base.scrolledUnderBottom)
        // Same flags → equal spec → GsScreenChrome writes nothing.
        assertEquals(base.copy(scrolledUnderTop = true), base.copy(scrolledUnderTop = true))
        assertNotEquals(base, base.copy(scrolledUnderTop = true))
        assertNotEquals(base, base.copy(scrolledUnderBottom = true))
    }

    @Test
    fun `scroll edge is a 6dp gradient`() {
        assertEquals(6f, GS_SCROLL_EDGE.value)
    }
}
