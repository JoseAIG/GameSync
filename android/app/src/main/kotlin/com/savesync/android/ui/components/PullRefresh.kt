package com.savesync.android.ui.components

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.google.accompanist.swiperefresh.SwipeRefreshState

/** Accompanist's default `refreshTriggerDistance`. */
val PULL_TRIGGER_DISTANCE: Dp = 80.dp

/**
 * One haptic Tick per pull gesture, on the frame the pull first crosses the
 * refresh threshold.  Pulling back and forth over the line in the same
 * gesture doesn't tick again; a new gesture can.
 */
class PullThresholdTicker {
    private var tickedThisGesture = false

    /** Feed each pull sample; true exactly when a Tick should fire. */
    fun update(swipeInProgress: Boolean, offsetPx: Float, triggerPx: Float): Boolean {
        if (!swipeInProgress) {
            tickedThisGesture = false
            return false
        }
        if (offsetPx >= triggerPx && !tickedThisGesture) {
            tickedThisGesture = true
            return true
        }
        return false
    }
}

/** Ticks once per gesture when a pull on [state] crosses the refresh threshold. */
@Composable
fun PullThresholdHaptic(state: SwipeRefreshState, trigger: Dp = PULL_TRIGGER_DISTANCE) {
    val haptics = LocalGsHaptics.current
    val triggerPx = with(LocalDensity.current) { trigger.toPx() }
    LaunchedEffect(state, triggerPx, haptics) {
        val ticker = PullThresholdTicker()
        snapshotFlow { state.isSwipeInProgress to state.indicatorOffset }
            .collect { (inProgress, offset) ->
                if (ticker.update(inProgress, offset, triggerPx)) haptics?.perform(GsHaptic.Tick)
            }
    }
}
