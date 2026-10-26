package com.savesync.android.ui.components

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SnackbarData
import androidx.compose.material3.SnackbarDuration
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.SnackbarResult
import androidx.compose.material3.SnackbarVisuals
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.unit.dp
import com.savesync.android.ui.theme.GsColors
import com.savesync.android.ui.theme.GsElevation
import com.savesync.android.ui.theme.GsShapes
import com.savesync.android.ui.theme.LocalHighContrast
import com.savesync.android.ui.theme.gsTextMuted
import com.savesync.android.ui.theme.textVariantOf
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

// ── Notices: one global notifier ────────────────────────────────────────────
//
// A notice reports an *event* (a sync finished, a ROM was queued).  An
// ongoing *condition* (server unreachable, conflicts waiting) is a banner,
// not a notice — see GsBanners.kt.

enum class GsNoticeKind { Success, Info, Warning, Error }

data class GsNotice(
    val kind: GsNoticeKind,
    val message: String,
    val actionLabel: String? = null,
    val onAction: (() -> Unit)? = null,
)

/** Success and Info are Short; Warning, Error and anything with an action Long. */
internal fun durationFor(n: GsNotice): SnackbarDuration = when {
    n.actionLabel != null -> SnackbarDuration.Long
    n.kind == GsNoticeKind.Warning || n.kind == GsNoticeKind.Error -> SnackbarDuration.Long
    else -> SnackbarDuration.Short
}

/** Success confirms, Error rejects; Info and Warning are silent. */
internal fun hapticFor(kind: GsNoticeKind): GsHaptic? = when (kind) {
    GsNoticeKind.Success -> GsHaptic.Confirm
    GsNoticeKind.Error -> GsHaptic.Reject
    GsNoticeKind.Info, GsNoticeKind.Warning -> null
}

internal class GsSnackbarVisuals(val notice: GsNotice) : SnackbarVisuals {
    override val message: String get() = notice.message
    override val actionLabel: String? get() = notice.actionLabel
    override val withDismissAction: Boolean get() = notice.kind == GsNoticeKind.Error
    override val duration: SnackbarDuration get() = durationFor(notice)
}

/**
 * The app's single feedback channel.  Lives in MainApp with MainApp's
 * coroutine scope, so a message outlives the screen that raised it (Save
 * detail can navigate back on the same frame).
 */
@Stable
class GsNotifier(
    val host: SnackbarHostState,
    private val scope: CoroutineScope,
    private val haptics: GsHaptics?,
) {
    private var job: Job? = null
    private val occluded = mutableIntStateOf(0)

    /**
     * Height in px of the strip at the bottom of the content that a showing
     * snackbar covers; 0 when nothing shows.  List follow-scroll treats it as
     * outside the viewport.
     */
    val occludedBottom: State<Int> get() = occluded

    internal fun setOccludedBottom(px: Int) {
        if (occluded.intValue != px) occluded.intValue = px
    }

    /**
     * Never suspends the caller.  Replaces whatever is showing *or still
     * queued*: cancelling the previous show's job makes `showSnackbar` clear
     * its data, so three quick calls leave only the third.
     * [hapticOverride] replaces the kind's default haptic (a conflict is a
     * Warning that still rejects).
     */
    fun show(n: GsNotice, haptic: Boolean = true, hapticOverride: GsHaptic? = null) {
        job?.cancel()
        if (haptic) (hapticOverride ?: hapticFor(n.kind))?.let { haptics?.perform(it) }
        job = scope.launch {
            val result = host.showSnackbar(GsSnackbarVisuals(n))
            if (result == SnackbarResult.ActionPerformed) n.onAction?.invoke()
        }
    }
}

val LocalGsNotifier = staticCompositionLocalOf<GsNotifier> { error("GsNotifier not provided") }

private fun kindColor(kind: GsNoticeKind): Color = when (kind) {
    GsNoticeKind.Success -> GsColors.Ok
    GsNoticeKind.Info -> GsColors.Info
    GsNoticeKind.Warning -> GsColors.Warn
    GsNoticeKind.Error -> GsColors.Err
}

/**
 * The notifier's host: a Panel card with a 4dp edge bar in the kind colour,
 * Text body and the action in the kind's text colour.  [onOccludedChange]
 * receives the covered height (px) while a snackbar shows, 0 otherwise.
 */
@Composable
fun GsSnackbarHost(
    host: SnackbarHostState,
    modifier: Modifier = Modifier,
    onOccludedChange: (Int) -> Unit = {},
) {
    val measured = remember { mutableIntStateOf(0) }
    val showing = host.currentSnackbarData != null
    SideEffect { onOccludedChange(if (showing) measured.intValue else 0) }
    SnackbarHost(
        hostState = host,
        modifier = modifier.onSizeChanged {
            measured.intValue = it.height
            onOccludedChange(if (host.currentSnackbarData != null) it.height else 0)
        },
    ) { data -> GsSnackbar(data) }
}

@Composable
private fun GsSnackbar(data: SnackbarData) {
    val kind = (data.visuals as? GsSnackbarVisuals)?.notice?.kind ?: GsNoticeKind.Info
    val base = kindColor(kind)
    val textColor = textVariantOf(base)
    val high = LocalHighContrast.current
    Surface(
        modifier = Modifier
            .padding(12.dp)
            .widthIn(max = 640.dp)
            .fillMaxWidth(),
        shape = GsShapes.Pill,
        color = GsColors.Panel,
        shadowElevation = GsElevation.Snackbar,
        border = if (high) BorderStroke(1.dp, textColor) else null,
    ) {
        Row(
            modifier = Modifier.height(IntrinsicSize.Min),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                Modifier
                    .width(4.dp)
                    .fillMaxHeight()
                    .background(base)
            )
            Text(
                data.visuals.message,
                color = GsColors.Text,
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier
                    .weight(1f)
                    .padding(horizontal = 12.dp, vertical = 12.dp),
            )
            // Touch-only conveniences: every next step a notice offers also
            // has a pad path, so the actions never take D-pad focus.
            data.visuals.actionLabel?.let { label ->
                TextButton(
                    onClick = { data.performAction() },
                    modifier = Modifier.focusProperties { canFocus = false },
                ) {
                    Text(label, color = textColor, style = MaterialTheme.typography.labelLarge)
                }
            }
            if (data.visuals.withDismissAction) {
                IconButton(
                    onClick = { data.dismiss() },
                    modifier = Modifier.focusProperties { canFocus = false },
                ) {
                    Icon(Icons.Filled.Close, contentDescription = "Dismiss", tint = gsTextMuted())
                }
            }
        }
    }
}

// ── Message → notice mapping for the view-model's string results ────────────

/** The view model's "no server" error; its notice offers "Settings". */
internal fun isServerNotConfigured(message: String): Boolean =
    message.contains("Server URL not configured", ignoreCase = true)

/**
 * A single-save sync reports through one string (MainViewModel.syncSave):
 * an embedded error makes it an Error, a conflict a Warning, anything else
 * a Success.
 */
internal fun noticeKindForSaveMessage(message: String): GsNoticeKind = when {
    message.contains("Error", ignoreCase = true) -> GsNoticeKind.Error
    message.contains("Conflict", ignoreCase = true) -> GsNoticeKind.Warning
    message.contains("Not uploaded", ignoreCase = true) -> GsNoticeKind.Warning
    else -> GsNoticeKind.Success
}

/** Strips a leading glyph ("✓ ", "↑ ") the view model puts on success messages. */
internal fun stripLeadingGlyph(message: String): String =
    message.trim().replace(Regex("^[✓✗⚠↑↓]\\s*"), "")
