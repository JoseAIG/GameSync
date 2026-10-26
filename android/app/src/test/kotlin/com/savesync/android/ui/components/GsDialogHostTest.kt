package com.savesync.android.ui.components

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** M5-5: a dialog stays composed while it leaves, and only until then. */
class GsDialogHostTest {

    @Test
    fun `a dialog with a target is shown, even before its enter starts`() {
        assertTrue(dialogHostShows(hasTarget = true, currentlyVisible = false, idle = false, expired = false))
        assertTrue(dialogHostShows(hasTarget = true, currentlyVisible = false, idle = true, expired = false))
    }

    @Test
    fun `a closed dialog stays while its exit runs`() {
        // The choice dropped the target; the card is still on screen, animating out.
        assertTrue(dialogHostShows(hasTarget = false, currentlyVisible = true, idle = false, expired = false))
        assertTrue(dialogHostShows(hasTarget = false, currentlyVisible = false, idle = false, expired = false))
    }

    @Test
    fun `the window goes once the exit is idle`() {
        assertFalse(dialogHostShows(hasTarget = false, currentlyVisible = false, idle = true, expired = false))
    }

    @Test
    fun `an exit that never runs is cut off by the ceiling`() {
        assertFalse(dialogHostShows(hasTarget = false, currentlyVisible = true, idle = true, expired = true))
    }

    @Test
    fun `enter and exit share one scale`() {
        assertEquals(0.94f, GS_DIALOG_SCALE)
    }
}
