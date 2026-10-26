package com.savesync.android.ui.components

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class TabSwitchBarTest {

    @Test
    fun `the pill travels between neighbours and across taps`() {
        assertFalse(pillJumps(0, 1, tabCount = 5, reduce = false))   // R1
        assertFalse(pillJumps(3, 2, tabCount = 5, reduce = false))   // L1
        assertFalse(pillJumps(1, 3, tabCount = 5, reduce = false))   // tap two away
    }

    @Test
    fun `a wrap or reduced motion fades the pill instead`() {
        assertTrue(pillJumps(4, 0, tabCount = 5, reduce = false))    // R1 on Settings
        assertTrue(pillJumps(0, 4, tabCount = 5, reduce = false))    // L1 on Saves
        assertTrue(pillJumps(0, 1, tabCount = 5, reduce = true))
    }

    // A 300px viewport over 600px of strip, 24px margin.
    @Test
    fun `a pill already inside the margins doesn't scroll the strip`() {
        assertNull(stripScrollTarget(left = 40f, right = 120f, scroll = 0, viewport = 300, margin = 24f, max = 300))
    }

    @Test
    fun `a clipped pill on the right scrolls it fully into view with margin`() {
        // Downloads at 380..480 must end 24px inside the right edge.
        assertEquals(204, stripScrollTarget(380f, 480f, scroll = 0, viewport = 300, margin = 24f, max = 300))
    }

    @Test
    fun `a clipped pill on the left scrolls back`() {
        assertEquals(26, stripScrollTarget(50f, 120f, scroll = 200, viewport = 300, margin = 24f, max = 300))
        // Never past the start.
        assertEquals(0, stripScrollTarget(10f, 80f, scroll = 100, viewport = 300, margin = 24f, max = 300))
    }

    @Test
    fun `the scroll target is clamped to the strip`() {
        assertEquals(300, stripScrollTarget(560f, 600f, scroll = 0, viewport = 300, margin = 24f, max = 300))
        assertNull(stripScrollTarget(560f, 600f, scroll = 0, viewport = 0, margin = 24f, max = 300))
    }
}
