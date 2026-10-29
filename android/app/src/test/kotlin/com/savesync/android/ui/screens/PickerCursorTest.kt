package com.savesync.android.ui.screens

import org.junit.Assert.assertEquals
import org.junit.Test

class PickerCursorTest {

    @Test
    fun `the cursor starts on the current value, else the first option`() {
        val names = listOf("Zelda (USA)", "Zelda (Europe)", "Zelda (Japan)")
        assertEquals(1, pickerCursorStart(names) { it == "Zelda (Europe)" })
        assertEquals(0, pickerCursorStart(names) { it == "Something else" })
        assertEquals(0, pickerCursorStart(emptyList<String>()) { true })
    }

    @Test
    fun `Up and Down stop at the ends`() {
        assertEquals(1, movePickerCursor(0, +1, 3))
        assertEquals(2, movePickerCursor(2, +1, 3))
        assertEquals(0, movePickerCursor(0, -1, 3))
        assertEquals(0, movePickerCursor(0, +1, 0))
    }

    @Test
    fun `A acts on the row under the cursor and confirms only on the stop after them`() {
        // Pad, three rows: rows 0..2 select or toggle, 3 is "Apply" / "Use selection".
        assertEquals(false, pickerAConfirms(0, 3, pad = true))
        assertEquals(false, pickerAConfirms(2, 3, pad = true))
        assertEquals(true, pickerAConfirms(3, 3, pad = true))
        // Touch has no cursor: its A button confirms.
        assertEquals(true, pickerAConfirms(0, 3, pad = false))
    }
}
