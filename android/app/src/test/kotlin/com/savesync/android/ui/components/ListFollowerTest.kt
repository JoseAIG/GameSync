package com.savesync.android.ui.components

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ListFollowerTest {

    // A 600px window with 60px rows and a 60px margin.
    private val viewStart = 0
    private val viewEnd = 600
    private val margin = 60

    @Test
    fun `a row inside the margins doesn't scroll — only the highlight moves`() {
        assertNull(computeFollowDelta(itemOffset = 60, itemSize = 60, viewStart, viewEnd, margin))
        assertNull(computeFollowDelta(itemOffset = 300, itemSize = 60, viewStart, viewEnd, margin))
        assertNull(computeFollowDelta(itemOffset = 480, itemSize = 60, viewStart, viewEnd, margin))
    }

    @Test
    fun `a row entering the bottom margin scrolls down by just enough`() {
        // Bottom edge at 600; it must end at 540.
        assertEquals(60, computeFollowDelta(itemOffset = 540, itemSize = 60, viewStart, viewEnd, margin))
        assertEquals(1, computeFollowDelta(itemOffset = 481, itemSize = 60, viewStart, viewEnd, margin))
    }

    @Test
    fun `a row entering the top margin scrolls up by just enough`() {
        assertEquals(-60, computeFollowDelta(itemOffset = 0, itemSize = 60, viewStart, viewEnd, margin))
        assertEquals(-130, computeFollowDelta(itemOffset = -70, itemSize = 60, viewStart, viewEnd, margin))
    }

    @Test
    fun `the window respects a content-padding start offset`() {
        // viewStart -24 (beforeContentPadding folded in by the caller).
        assertNull(computeFollowDelta(itemOffset = 40, itemSize = 60, -24, 576, margin))
        assertEquals(-8, computeFollowDelta(itemOffset = 28, itemSize = 60, -24, 576, margin))
    }

    @Test
    fun `a row taller than the window aligns its top`() {
        assertEquals(40, computeFollowDelta(itemOffset = 100, itemSize = 900, viewStart, viewEnd, margin))
    }

    @Test
    fun `an occluded bottom strip counts as off-screen`() {
        // A 120px snackbar: the window ends at 480, so a row at 420 must scroll.
        assertNull(computeFollowDelta(420, 60, viewStart, viewEnd, margin))
        assertEquals(60, computeFollowDelta(420, 60, viewStart, viewEnd - 120, margin))
    }

    private val rows = listOf(
        VisibleRow(index = 10, offset = -30, size = 60),   // partly above
        VisibleRow(index = 11, offset = 34, size = 60),
        VisibleRow(index = 12, offset = 98, size = 60),
        VisibleRow(index = 13, offset = 162, size = 60),
        VisibleRow(index = 14, offset = 226, size = 60),   // partly below a 260 window
    )

    @Test
    fun `fling re-anchor lands on the first fully visible row going down`() {
        assertEquals(11, reanchorIndex(rows, viewStart = 0, viewEnd = 260, down = true))
    }

    @Test
    fun `fling re-anchor lands on the last fully visible row going up`() {
        assertEquals(13, reanchorIndex(rows, viewStart = 0, viewEnd = 260, down = false))
    }

    @Test
    fun `re-anchor falls back to partly visible rows`() {
        val tall = listOf(VisibleRow(3, -100, 800))
        assertEquals(3, reanchorIndex(tall, 0, 600, down = true))
        assertNull(reanchorIndex(emptyList(), 0, 600, down = true))
    }

    @Test
    fun `stride includes the item spacing`() {
        assertEquals(64, strideOf(rows))
        assertEquals(60, strideOf(listOf(VisibleRow(0, 0, 60))))
    }
}
