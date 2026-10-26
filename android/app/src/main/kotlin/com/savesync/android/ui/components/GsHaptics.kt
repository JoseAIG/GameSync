package com.savesync.android.ui.components

import android.os.Build
import android.view.HapticFeedbackConstants
import android.view.View
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.platform.LocalView

/**
 * The three haptics the app uses, and only on these events (utility rule):
 *  - Confirm: smart sync ok, Sync all ok with no errors, ROM queued, delete
 *    done, restore/undo done.
 *  - Reject: sync or download error, conflict found, Sync all finished with
 *    errors, A pressed while a sync is busy.
 *  - Tick: each letter change during an alphabet jump, a list-edge hit, the
 *    pull-to-refresh threshold crossing.
 * Never on ordinary row steps, tab changes or warnings.  Fire it in the same
 * frame as its visual, never after a suspend.
 */
enum class GsHaptic { Confirm, Reject, Tick }

class GsHaptics(private val view: View) {
    fun perform(kind: GsHaptic) {
        val c = when (kind) {
            GsHaptic.Confirm ->
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) HapticFeedbackConstants.CONFIRM
                else HapticFeedbackConstants.CONTEXT_CLICK
            GsHaptic.Reject ->
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) HapticFeedbackConstants.REJECT
                else HapticFeedbackConstants.LONG_PRESS
            GsHaptic.Tick -> HapticFeedbackConstants.CLOCK_TICK
        }
        // Respects the system "touch feedback" setting by itself.
        view.performHapticFeedback(c)
    }
}

@Composable
fun rememberGsHaptics(): GsHaptics {
    val view = LocalView.current
    return remember(view) { GsHaptics(view) }
}

val LocalGsHaptics = staticCompositionLocalOf<GsHaptics?> { null }
