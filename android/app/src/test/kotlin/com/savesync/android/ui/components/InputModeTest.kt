package com.savesync.android.ui.components

import android.view.InputDevice
import android.view.KeyCharacterMap
import android.view.KeyEvent
import androidx.compose.ui.input.key.Key
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class InputModeTest {

    private val padId = 7

    @Test
    fun `gamepad buttons and d-pad from a real device are pad input`() {
        assertTrue(isPhysicalPadKey(false, padId, InputDevice.SOURCE_GAMEPAD, KeyEvent.KEYCODE_BUTTON_A))
        assertTrue(isPhysicalPadKey(false, padId, InputDevice.SOURCE_DPAD, KeyEvent.KEYCODE_DPAD_DOWN))
        // A TV remote / odd pad that reports no useful source still sends D-pad codes.
        assertTrue(isPhysicalPadKey(false, padId, 0, KeyEvent.KEYCODE_DPAD_UP))
        assertTrue(isPhysicalPadKey(null, padId, 0, KeyEvent.KEYCODE_BUTTON_Y))
    }

    @Test
    fun `a bluetooth keyboard is pad input`() {
        assertTrue(isPhysicalPadKey(false, padId, InputDevice.SOURCE_KEYBOARD, KeyEvent.KEYCODE_ENTER))
    }

    @Test
    fun `soft keyboard keys never switch modes`() {
        // Enter / Del while typing a search.
        assertFalse(
            isPhysicalPadKey(true, padId, InputDevice.SOURCE_KEYBOARD, KeyEvent.KEYCODE_ENTER)
        )
        assertFalse(
            isPhysicalPadKey(
                false,
                KeyCharacterMap.VIRTUAL_KEYBOARD,
                InputDevice.SOURCE_KEYBOARD,
                KeyEvent.KEYCODE_DEL,
            )
        )
        assertFalse(
            isPhysicalPadKey(
                false,
                padId,
                InputDevice.SOURCE_KEYBOARD,
                KeyEvent.KEYCODE_ENTER,
                flags = KeyEvent.FLAG_SOFT_KEYBOARD,
            )
        )
    }

    @Test
    fun `first key after touch reveals the cursor when no gamepad is connected`() {
        val t = GsInputTracker()
        assertEquals(GsInputMode.Touch, t.lastInput)
        assertTrue(t.onPadKey())          // the reveal press
        assertEquals(GsInputMode.Pad, t.lastInput)
        assertFalse(t.onPadKey())         // the next press acts
        t.onTouchDown()
        assertEquals(GsInputMode.Touch, t.lastInput)
        assertTrue(t.onPadKey())
    }

    @Test
    fun `with a gamepad connected no press is spent revealing`() {
        val t = GsInputTracker()
        t.gamepadConnected = true
        t.onTouchDown()
        assertFalse(t.onPadKey())
    }

    @Test
    fun `the reveal flag lives only while its key is dispatched`() {
        val t = GsInputTracker()
        val seen = t.dispatching(reveal = true) { t.isRevealPress }
        assertTrue(seen)
        assertFalse(t.isRevealPress)
        assertFalse(t.dispatching(reveal = false) { t.isRevealPress })
    }

    @Test
    fun `stick input means a pad is in hand`() {
        val t = GsInputTracker()
        t.onStick()
        assertEquals(GsInputMode.Pad, t.lastInput)
        assertFalse(t.onPadKey())
    }

    @Test
    fun `the reveal press swallows actions but not B or the d-pad`() {
        // Up/Down reach ListFollower.step, which only reveals on that press.
        assertFalse(isRevealGuarded(Key.DirectionDown))
        assertTrue(isRevealGuarded(Key.ButtonA))
        assertTrue(isRevealGuarded(Key.Enter))
        assertTrue(isRevealGuarded(Key.ButtonX))
        assertTrue(isRevealGuarded(Key.ButtonY))
        assertFalse(isRevealGuarded(Key.ButtonB))
        assertFalse(isRevealGuarded(Key.DirectionLeft))
    }
}
