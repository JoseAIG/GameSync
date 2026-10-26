package com.savesync.android.ui.components

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class GsSemanticsTest {

    @Test
    fun `a row reads its status, then selected when it is the pad cursor`() {
        assertEquals("Synced, selected", rowStateDescription("Synced", selected = true))
        assertEquals("Synced", rowStateDescription("Synced", selected = false))
        assertEquals("selected", rowStateDescription(null, selected = true))
        assertNull(rowStateDescription(null, selected = false))
        assertNull(rowStateDescription(" ", selected = false))
    }

    @Test
    fun `hints speak button names TalkBack can say`() {
        assertEquals("A", GsButton.A.spoken)
        assertEquals("L2 or R2", GsButton.L2R2.spoken)
        assertEquals("Start", GsButton.START.spoken)
        assertEquals("Select", GsButton.SELECT.spoken)
        assertEquals("D-pad", GsButton.DPAD.spoken)
    }
}
