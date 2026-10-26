package com.savesync.android.ui.components

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** M5-2: a held stick (or quick re-presses) bumps the edge once. */
class EdgeBumpTest {

    @Test
    fun `the first edge hit is never a repeat`() {
        assertFalse(isEdgeRepeat(now = 1_000, lastHit = null))
    }

    @Test
    fun `a hit within 350 ms of the previous one is a repeat`() {
        assertTrue(isEdgeRepeat(now = 1_000, lastHit = 1_000))
        assertTrue(isEdgeRepeat(now = 1_349, lastHit = 1_000))
        assertFalse(isEdgeRepeat(now = 1_350, lastHit = 1_000))
        assertFalse(isEdgeRepeat(now = 2_000, lastHit = 1_000))
    }

    @Test
    fun `stick pulses every 150 ms stay one bump while held`() {
        // Each hit restarts the window, so pulses at 0, 150, 300 … never bump again.
        var last: Long? = null
        val bumps = (0..10).map { it * 150L }.count { t ->
            val repeat = isEdgeRepeat(t, last)
            last = t
            !repeat
        }
        assertTrue(bumps == 1)
    }

    @Test
    fun `a clock that went backwards is not a repeat`() {
        assertFalse(isEdgeRepeat(now = 900, lastHit = 1_000))
    }
}
