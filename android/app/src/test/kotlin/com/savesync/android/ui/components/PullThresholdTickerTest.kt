package com.savesync.android.ui.components

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PullThresholdTickerTest {

    @Test
    fun `ticks once when the pull crosses the threshold`() {
        val t = PullThresholdTicker()
        val samples = listOf(10f, 50f, 79f, 80f, 120f, 70f, 95f)
        val ticks = samples.count { t.update(true, it, 80f) }
        assertEquals(1, ticks)
    }

    @Test
    fun `a new gesture can tick again`() {
        val t = PullThresholdTicker()
        assertTrue(t.update(true, 100f, 80f))
        assertFalse(t.update(false, 0f, 80f))
        assertTrue(t.update(true, 90f, 80f))
    }

    @Test
    fun `never ticks without a gesture or below the threshold`() {
        val t = PullThresholdTicker()
        assertFalse(t.update(false, 200f, 80f))
        assertFalse(t.update(true, 79.9f, 80f))
    }
}
