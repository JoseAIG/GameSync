package com.savesync.android.ui.components

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** M5-7: a filter change hints the direction it moved. */
class FilterNudgeTest {
    private val systems = listOf("All", "GBA", "PSP", "SNES")

    @Test
    fun `a press keeps its own direction, even across a wrap`() {
        // R2 / SELECT on the last option wraps to the first: still forward.
        assertEquals(1, filterNudgeSign(systems, "SNES", "All", pressDelta = 1))
        // L2 on the first wraps to the last: still back.
        assertEquals(-1, filterNudgeSign(systems, "All", "SNES", pressDelta = -1))
    }

    @Test
    fun `a tap points the way it moved through the options`() {
        assertEquals(1, filterNudgeSign(systems, "GBA", "SNES", pressDelta = null))
        assertEquals(-1, filterNudgeSign(systems, "SNES", "All", pressDelta = null))
    }

    @Test
    fun `no change, no hint`() {
        assertEquals(0, filterNudgeSign(systems, "PSP", "PSP", pressDelta = 1))
    }

    @Test
    fun `a value outside the options still hints forward`() {
        assertEquals(1, filterNudgeSign(systems, "GONE", "GBA", pressDelta = null))
    }

    @Test
    fun `nullable options (All status) work`() {
        val statuses = listOf<String?>(null, "Conflict", "Synced")
        assertEquals(1, filterNudgeSign(statuses, null, "Conflict", pressDelta = null))
        assertEquals(-1, filterNudgeSign(statuses, "Synced", null, pressDelta = null))
    }

    @Test
    fun `RA only on is forward, off is back`() {
        assertEquals(1, raNudgeSign(true))
        assertEquals(-1, raNudgeSign(false))
    }

    @Test
    fun `a pending press step is read once`() {
        val pending = intArrayOf(-1)
        assertEquals(-1, pending.takeStep())
        assertNull(pending.takeStep())
    }
}
