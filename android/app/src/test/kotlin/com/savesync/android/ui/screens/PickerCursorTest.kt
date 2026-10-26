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
}
