package com.savesync.android.ui.components

import android.view.InputDevice
import android.view.KeyCharacterMap
import android.view.KeyEvent
import androidx.compose.runtime.Stable
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.input.key.Key

/**
 * How the user is driving the app right now.  In [Pad] mode list rows draw
 * the gamepad cursor; in [Touch] mode they don't (a phone in portrait would
 * otherwise show row 0 permanently "selected").  Press feedback shows in
 * both modes, because a handheld's screen is touched in Pad mode too.
 */
enum class GsInputMode { Touch, Pad }

/**
 * The mode screens draw for.  MainApp provides
 * `if (gamepadConnected) Pad else tracker.lastInput`, so a handheld with
 * built-in controls is always in Pad mode: touching its screen never hides
 * the cursor the next A press acts on.
 */
val LocalInputMode = compositionLocalOf { GsInputMode.Touch }

/**
 * Owned by MainActivity.  Records the last kind of input and whether the key
 * being dispatched right now is the one that switched a gamepad-less device
 * from Touch to Pad (a Bluetooth keyboard or TV remote on a phone).  That
 * first press only reveals the cursor: list screens re-anchor on Up/Down and
 * swallow A/Enter/X/Y, so nothing fires on a row the user couldn't see
 * highlighted.  This is a guard on the first press, not a rebinding.
 */
@Stable
class GsInputTracker {
    var lastInput by mutableStateOf(GsInputMode.Touch)
        private set

    /** Kept current by MainApp; while true the app is always in Pad mode. */
    @Volatile
    var gamepadConnected: Boolean = false

    /** True only while the key that revealed the cursor is being dispatched. */
    var isRevealPress: Boolean = false
        private set

    /**
     * A key from a physical pad / keyboard arrived.  Returns true when it is
     * the reveal press (the cursor was hidden until now).
     */
    fun onPadKey(): Boolean {
        val reveal = lastInput == GsInputMode.Touch && !gamepadConnected
        lastInput = GsInputMode.Pad
        return reveal
    }

    /** Synthesised stick / HAT input: a joystick means a pad is in hand. */
    fun onStick() {
        lastInput = GsInputMode.Pad
    }

    fun onTouchDown() {
        lastInput = GsInputMode.Touch
    }

    /** Runs [dispatch] with [isRevealPress] set to [reveal], then clears it. */
    inline fun <T> dispatching(reveal: Boolean, dispatch: () -> T): T {
        setReveal(reveal)
        try {
            return dispatch()
        } finally {
            setReveal(false)
        }
    }

    @PublishedApi
    internal fun setReveal(value: Boolean) {
        isRevealPress = value
    }
}

/** The Activity's tracker, for the first-key-after-touch guard. */
val LocalInputTracker = staticCompositionLocalOf<GsInputTracker?> { null }

/**
 * Whether a key event comes from a physical pad, D-pad or keyboard — the
 * input that switches the app to Pad mode.  Events from the soft keyboard
 * (Enter / Del while typing a search) are virtual and never switch modes.
 */
fun isPhysicalPadKey(
    deviceIsVirtual: Boolean?,
    deviceId: Int,
    source: Int,
    keyCode: Int,
    flags: Int = 0,
): Boolean {
    if (deviceIsVirtual == true || deviceId == KeyCharacterMap.VIRTUAL_KEYBOARD) return false
    if ((flags and KeyEvent.FLAG_SOFT_KEYBOARD) != 0) return false
    fun has(mask: Int) = (source and mask) == mask
    val padSource = has(InputDevice.SOURCE_GAMEPAD) || has(InputDevice.SOURCE_DPAD) ||
        has(InputDevice.SOURCE_JOYSTICK) || has(InputDevice.SOURCE_KEYBOARD)
    val padKey = keyCode in KeyEvent.KEYCODE_DPAD_UP..KeyEvent.KEYCODE_DPAD_CENTER ||
        keyCode in DPAD_DIAGONALS ||
        keyCode in KeyEvent.KEYCODE_BUTTON_A..KeyEvent.KEYCODE_BUTTON_MODE ||
        keyCode in KeyEvent.KEYCODE_BUTTON_1..KeyEvent.KEYCODE_BUTTON_16
    return padSource || padKey
}

private val DPAD_DIAGONALS = setOf(
    KeyEvent.KEYCODE_DPAD_UP_LEFT, KeyEvent.KEYCODE_DPAD_DOWN_LEFT,
    KeyEvent.KEYCODE_DPAD_UP_RIGHT, KeyEvent.KEYCODE_DPAD_DOWN_RIGHT,
)

/**
 * Action keys the reveal press (see [GsInputTracker]) swallows on list
 * screens, so nothing fires on a row that wasn't lit.  Up/Down are not in
 * here: they go through [ListFollower.step], which on the reveal press only
 * re-anchors the cursor if a fling left it off-screen.
 */
fun isRevealGuarded(key: Key): Boolean = key in REVEAL_GUARDED

private val REVEAL_GUARDED = setOf(
    Key.ButtonA,
    Key.Enter,
    Key.NumPadEnter,
    Key.ButtonX,
    Key.ButtonY,
)
