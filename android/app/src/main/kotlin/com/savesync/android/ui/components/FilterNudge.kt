package com.savesync.android.ui.components

import androidx.compose.animation.core.Animatable
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import com.savesync.android.ui.theme.GsMotion
import com.savesync.android.ui.theme.LocalReduceMotion
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import kotlin.math.sign

// ── Filter changes point the way they moved (M5-7) ──────────────────────────
//
// SELECT / L2 / R2 (or a chip tap) replace a list's rows in place.  So the
// change reads as a move along the filter cycle, the list arrives from the
// side the filter moved to: it snaps 16dp that way at 40% opacity, then
// settles with STANDARD.  One LazyColumn stays (its LazyListState and the
// per-system scroll memory live on); only its layer moves.

/** How far the list starts from its place when a filter changes. */
val FILTER_NUDGE = 16.dp
/** The list's opacity at the start of a filter change. */
const val FILTER_NUDGE_ALPHA = 0.4f

/**
 * The direction a filter change hints: the press's own step when it came
 * from SELECT / L2 / R2 ([pressDelta], so a wrap from the last option to
 * the first still reads as "forward"), else the direction through
 * [options] (a chip or a banner tap).  0 = no hint.
 */
fun <T> filterNudgeSign(options: List<T>, old: T, new: T, pressDelta: Int?): Int {
    if (old == new) return 0
    if (pressDelta != null && pressDelta != 0) return pressDelta.sign
    val from = options.indexOf(old)
    val to = options.indexOf(new)
    if (from < 0 || to < 0) return 1
    return (to - from).sign
}

/** A press's pending step (0 = none), read once: null when there was none. */
fun IntArray.takeStep(): Int? {
    val v = this[0]
    this[0] = 0
    return v.takeIf { it != 0 }
}

/** RA-only on reads as forward (+1), off as back (−1). */
fun raNudgeSign(raOnly: Boolean): Int = if (raOnly) 1 else -1

/**
 * A list's filter hint: translation and alpha, read only in [gsFilterNudge]'s
 * layer, so the animation never recomposes the list.
 */
@Stable
class FilterNudge(private val scope: CoroutineScope, private val distancePx: Float) {
    internal val x = Animatable(0f)
    internal val alpha = Animatable(1f)

    /**
     * The filter moved by [sign].  Snapping is allowed here (§3.1 rule 2):
     * the content itself is replaced on this frame.  Quick presses re-snap
     * and restart, since each one replaces the content again.  Under reduced
     * motion only the opacity changes.
     */
    fun kick(sign: Int, reduce: Boolean) {
        if (sign == 0) return
        scope.launch {
            launch {
                x.snapTo(if (reduce) 0f else sign * distancePx)
                x.animateTo(0f, GsMotion.standard())
            }
            alpha.snapTo(FILTER_NUDGE_ALPHA)
            alpha.animateTo(1f, GsMotion.standard())
        }
    }
}

@Composable
fun rememberFilterNudge(): FilterNudge {
    val scope = rememberCoroutineScope()
    val px = with(LocalDensity.current) { FILTER_NUDGE.toPx() }
    return remember(scope, px) { FilterNudge(scope, px) }
}

/** Applies [nudge] to a list (values read in the layer, never in composition). */
fun Modifier.gsFilterNudge(nudge: FilterNudge): Modifier = graphicsLayer {
    translationX = nudge.x.value
    alpha = nudge.alpha.value
}

/**
 * Kicks [nudge] whenever [value] changes after the first composition (a
 * rotation re-creates nothing to animate), with the sign [signOf] gives
 * for the old and new value.
 */
@Composable
fun <T> NudgeOnChange(nudge: FilterNudge, value: T, signOf: (old: T, new: T) -> Int) {
    val reduce = LocalReduceMotion.current
    val last = remember { LastValue<T>() }
    LaunchedEffect(value) {
        if (last.set && last.value != value) {
            @Suppress("UNCHECKED_CAST")
            nudge.kick(signOf(last.value as T, value), reduce)
        }
        last.value = value
        last.set = true
    }
}

private class LastValue<T> {
    var set = false
    var value: T? = null
}
