package com.savesync.android.ui.components

import android.os.SystemClock
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.sizeIn
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.savesync.android.ui.theme.GsColors
import com.savesync.android.ui.theme.GsElevation
import com.savesync.android.ui.theme.GsShapes
import com.savesync.android.ui.theme.LocalHighContrast
import com.savesync.android.ui.theme.LocalReduceMotion
import com.savesync.android.ui.theme.gsPopIn
import com.savesync.android.ui.theme.gsPopOut
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEvent
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.type
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/** Uppercase first alphabetic character of [name], or "" if none. */
fun firstLetter(name: String): String {
    for (c in name) if (c.isLetter()) return c.uppercase()
    return ""
}

/**
 * Mutable state for d-pad left/right hold-to-accelerate navigation.
 *
 * Each screen wires its DirectionLeft/Right key handler to
 * [handleHorizontalHoldKeyEvent];
 * after the user has been holding for longer than [HOLD_FAST_AFTER_MS]
 * the page scroll cadence speeds up, and past [HOLD_ALPHA_AFTER_MS] we
 * switch to alphabet jumps so the user can sweep a long list in seconds.
 *
 * Mirrors the Steam Deck client's `_nav_x_*` polling state so both apps
 * feel identical when you keep d-pad left/right held.
 */
class HoldNavState {
    var dir: Int = 0
    var heldSince: Long = 0L
    var lastInput: Long = 0L
    var lastAction: Long = 0L
    private var pressedDir: Int = 0
    private var repeatJob: Job? = null
    private var releaseJob: Job? = null

    private val _mode = mutableStateOf(HoldMode.Idle)
    /** Where the hold is in its ramp; the alphabet HUD shows while it is [HoldMode.Alpha]. */
    val mode: State<HoldMode> = _mode

    private val _letter = mutableStateOf("")
    /** The letter the last alphabet jump landed on (what the HUD shows). */
    val letter: State<String> = _letter
    /** Last letter that ticked in this hold; cleared on a fresh press. */
    private var tickedLetter: String = ""

    /** The Tick fired on each letter change (set by [rememberHoldNavState]). */
    var tick: (() -> Unit)? = null

    internal fun setMode(mode: HoldMode) {
        if (_mode.value != mode) _mode.value = mode
    }

    /** A fresh press starts a new hold: page mode, and its first letter will tick. */
    internal fun beginHold() {
        tickedLetter = ""
        setMode(HoldMode.Page)
    }

    /**
     * An alphabet jump landed on [letter]: the HUD shows it and, when it is a
     * new letter, one Tick fires — before the frame that draws it.
     */
    fun announceLetter(letter: String) {
        if (letter.isEmpty()) return
        if (_letter.value != letter) _letter.value = letter
        if (letter != tickedLetter) {
            tickedLetter = letter
            tick?.invoke()
        }
    }

    fun onKeyDown(
        dir: Int,
        scope: CoroutineScope,
        onPage: (Int) -> Unit,
        onAlphabet: (Int) -> Unit,
    ) {
        // Android synthesises HAT / D-pad input as paired ACTION_DOWN +
        // ACTION_UP every ~150 ms while the pad is held (see
        // MainActivity.dispatchGenericMotionEvent).  If we acted on the UP
        // immediately the repeat coroutine would be cancelled before its
        // first 50 ms tick, leaving only the per-DOWN synchronous fire —
        // which onPress drops during the initial debounce window, so the
        // user sees a single scroll then nothing.  Cancel any pending
        // release so a fresh DOWN within the grace window keeps the hold
        // continuous.
        releaseJob?.cancel()
        releaseJob = null

        if (pressedDir == dir && repeatJob?.isActive == true) return

        repeatJob?.cancel()
        pressedDir = dir
        onPress(dir, onPage, onAlphabet)
        repeatJob = scope.launch {
            delay(REPEAT_POLL_MS)
            while (isActive && pressedDir == dir) {
                onPress(dir, onPage, onAlphabet)
                delay(REPEAT_POLL_MS)
            }
        }
    }

    fun onKeyUp(dir: Int, scope: CoroutineScope) {
        if (pressedDir != dir) return
        releaseJob?.cancel()
        releaseJob = scope.launch {
            delay(UP_GRACE_MS)
            pressedDir = 0
            repeatJob?.cancel()
            repeatJob = null
            setMode(HoldMode.Idle)
            // Reset active direction so the next press is treated as fresh
            // by onPress (otherwise this.dir would still match and held
            // would be a stale value from the previous hold).
            this@HoldNavState.dir = 0
            releaseJob = null
        }
    }
}

/** Where a left/right hold is: a single press, faster pages, or letter jumps. */
enum class HoldMode { Idle, Page, Alpha }

@Composable
fun rememberHoldNavState(): HoldNavState {
    val haptics = LocalGsHaptics.current
    return remember(haptics) {
        HoldNavState().also { state -> state.tick = { haptics?.perform(GsHaptic.Tick) } }
    }
}

/** How long the alphabet HUD stays after the hold ends (restarted by each letter). */
const val ALPHA_HUD_LINGER_MS = 500L

/**
 * The alphabet HUD: while a left/right hold is jumping by letter, a rounded
 * square in the middle of the list shows the letter it landed on.  It pops
 * in (opacity only under reduced motion) and leaves [ALPHA_HUD_LINGER_MS]
 * after the hold ends.  It is not focusable and takes no input, so the hold
 * keeps working underneath; TalkBack hears each letter (polite live region).
 * Place it over the list, in a Box.
 */
@Composable
fun AlphabetHud(holdNav: HoldNavState, modifier: Modifier = Modifier) {
    val mode by holdNav.mode
    val letter by holdNav.letter
    val reduce = LocalReduceMotion.current
    val high = LocalHighContrast.current
    var visible by remember { mutableStateOf(false) }
    // Keyed on the letter too: each new letter restarts the linger timer.
    LaunchedEffect(mode, letter) {
        if (mode == HoldMode.Alpha && letter.isNotEmpty()) {
            visible = true
        } else if (visible) {
            delay(ALPHA_HUD_LINGER_MS)
            visible = false
        }
    }
    Box(modifier = modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        AnimatedVisibility(
            visible = visible,
            enter = gsPopIn(reduce),
            exit = gsPopOut(reduce),
            label = "alphabetHud",
        ) {
            Box(
                modifier = Modifier
                    .semantics {
                        liveRegion = LiveRegionMode.Polite
                        contentDescription = letter
                    }
                    .shadow(GsElevation.Hud, GsShapes.Hud)
                    .clip(GsShapes.Hud)
                    .background(if (high) GsColors.Panel else GsColors.Panel.copy(alpha = 0.92f))
                    .border(1.dp, if (high) GsColors.Text else GsColors.Line, GsShapes.Hud)
                    .sizeIn(minWidth = 72.dp, minHeight = 72.dp)
                    .padding(12.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    letter,
                    style = MaterialTheme.typography.headlineSmall.copy(fontSize = 32.sp, lineHeight = 36.sp),
                    fontWeight = FontWeight.Bold,
                    color = GsColors.Accent,
                )
            }
        }
    }
}

private const val HOLD_FAST_AFTER_MS = 500L
private const val HOLD_ALPHA_AFTER_MS = 1500L
private const val FAST_CADENCE_MS = 100L
private const val ALPHA_CADENCE_MS = 250L
private const val REPEAT_POLL_MS = 50L
// Gap after which a subsequent press is treated as a brand-new press
// (user lifted the d-pad and re-pressed).  Slightly larger than the
// analog-stick synth repeat interval (150 ms) so a smooth hold via the
// stick still counts as continuous.
private const val RESET_GAP_MS = 350L
// Grace window after a KeyUp before we actually tear down the repeat
// coroutine.  Must exceed MainActivity.STICK_REPEAT_MS (150 ms) so the
// next synthesised DOWN of a held HAT/stick lands inside the window and
// cancels the pending release.
private const val UP_GRACE_MS = 220L

/**
 * Run one d-pad-horizontal step through the hold ramp.  Pass [onPage]
 * and [onAlphabet] callbacks; this function picks the right one based
 * on how long the user has been holding the same direction.
 */
fun HoldNavState.onPress(
    dir: Int,
    onPage: (Int) -> Unit,
    onAlphabet: (Int) -> Unit,
) {
    val now = SystemClock.uptimeMillis()
    val sinceLastInput = now - lastInput
    val freshPress = this.dir != dir || sinceLastInput > RESET_GAP_MS
    lastInput = now
    if (freshPress) {
        beginHold()
        this.dir = dir
        heldSince = now
        lastAction = now
        onPage(dir)
        return
    }
    val held = now - heldSince
    val sinceLastAction = now - lastAction
    when {
        held >= HOLD_ALPHA_AFTER_MS -> {
            setMode(HoldMode.Alpha)
            if (sinceLastAction >= ALPHA_CADENCE_MS) {
                lastAction = now
                onAlphabet(dir)
            }
        }
        held >= HOLD_FAST_AFTER_MS -> {
            if (sinceLastAction >= FAST_CADENCE_MS) {
                lastAction = now
                onPage(dir)
            }
        }
        // Still in the post-initial-press debounce window — drop the
        // event entirely.  The initial press already fired one onPage.
    }
}

fun HoldNavState.handleHorizontalHoldKeyEvent(
    event: KeyEvent,
    scope: CoroutineScope,
    onPage: (Int) -> Unit,
    onAlphabet: (Int) -> Unit,
): Boolean {
    val dir = when (event.key) {
        Key.DirectionLeft -> -1
        Key.DirectionRight -> 1
        else -> return false
    }
    return when (event.type) {
        KeyEventType.KeyDown -> {
            onKeyDown(dir, scope, onPage, onAlphabet)
            true
        }
        KeyEventType.KeyUp -> {
            onKeyUp(dir, scope)
            true
        }
        else -> false
    }
}
