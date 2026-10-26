package com.savesync.android.ui.screens

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.gestures.animateScrollBy
import androidx.compose.foundation.gestures.scrollBy
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.relocation.BringIntoViewRequester
import androidx.compose.foundation.relocation.bringIntoViewRequester
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDownward
import androidx.compose.material.icons.filled.ArrowUpward
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Cloud
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material.icons.outlined.HelpOutline
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.savesync.android.sync.SuspectUpload
import com.savesync.android.ui.PreviewLineKind
import com.savesync.android.ui.SyncPlanState
import com.savesync.android.ui.components.ButtonGlyph
import com.savesync.android.ui.components.GsButton
import com.savesync.android.ui.components.GsDialog
import com.savesync.android.ui.components.GsDialogChoice
import com.savesync.android.ui.components.GsListRow
import com.savesync.android.ui.components.GsPill
import com.savesync.android.ui.components.LocalShowButtonHints
import com.savesync.android.ui.previewDetailGroups
import com.savesync.android.ui.suspectSizeText
import com.savesync.android.ui.syncAllCopy
import com.savesync.android.ui.syncPreviewLines
import com.savesync.android.ui.theme.GsColors
import com.savesync.android.ui.theme.GsMotion
import com.savesync.android.ui.theme.LocalReduceMotion
import com.savesync.android.ui.theme.gsExpandIn
import com.savesync.android.ui.theme.gsShrinkOut
import com.savesync.android.ui.theme.gsTextMuted
import kotlinx.coroutines.launch

/**
 * Sync all's preview (M5-1): what the sync will do, before it does it.
 * "Checking with the server…" while the plan is made, then a summary, and
 * — when an upload looks like a rollback — those saves listed under a Warn
 * header, each skipped until included.
 *
 * Keys (dialog-internal, A = confirm as everywhere): A "Sync" (or "Done"
 * when nothing is left to do), B cancels, Y shows every title, the D-pad
 * moves the suspect cursor (and scrolls past either end), X includes or
 * skips the suspect under it.  A is never a toggle here.  Touch taps a
 * suspect row to toggle it.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
internal fun SyncAllDialog(
    state: SyncPlanState,
    filtersApplied: Boolean,
    onCancel: () -> Unit,
    onRun: (includeSuspects: Set<String>) -> Unit,
) {
    val count = when (state) {
        is SyncPlanState.Planning -> state.count
        is SyncPlanState.Ready -> state.count
        is SyncPlanState.Failed -> state.count
        SyncPlanState.Idle -> 0
    }
    val copy = syncAllCopy(count, filtersApplied)
    val preview = (state as? SyncPlanState.Ready)?.preview
    val suspects = preview?.suspects.orEmpty()
    var included by remember(preview) { mutableStateOf(emptySet<String>()) }
    var cursor by remember(preview) { mutableIntStateOf(0) }
    var details by remember(preview) { mutableStateOf(false) }
    val scroll = rememberScrollState()
    val scope = rememberCoroutineScope()
    val reduce = LocalReduceMotion.current
    val rowPx = with(LocalDensity.current) { 48.dp.toPx() }
    val requesters = remember(preview) { List(suspects.size) { BringIntoViewRequester() } }
    LaunchedEffect(cursor, preview) { requesters.getOrNull(cursor)?.bringIntoView() }

    fun toggle(index: Int) {
        val id = suspects.getOrNull(index)?.titleId ?: return
        included = if (id in included) included - id else included + id
    }
    fun scrollBody(dy: Int) {
        scope.launch {
            if (reduce) scroll.scrollBy(dy * rowPx) else scroll.animateScrollBy(dy * rowPx, GsMotion.quick())
        }
    }

    val actionable = preview?.actionableCount(includeSuspects = included) ?: 0
    val choices = buildList {
        add(GsDialogChoice(GsButton.B, "Cancel", onCancel))
        when (state) {
            is SyncPlanState.Ready -> {
                add(GsDialogChoice(GsButton.Y, if (details) "Hide details" else "Details", { details = !details }))
                add(
                    GsDialogChoice(
                        GsButton.A,
                        if (actionable == 0) "Done" else "Sync",
                        { onRun(included) },
                        color = GsColors.Accent,
                        textColor = GsColors.Ink,
                    )
                )
            }
            is SyncPlanState.Failed -> add(
                GsDialogChoice(GsButton.A, "Done", onCancel, color = GsColors.Accent, textColor = GsColors.Ink)
            )
            else -> Unit
        }
    }

    GsDialog(
        title = copy.title,
        message = copy.message,
        onDismiss = onCancel,
        choices = choices,
        onNavigate = when {
            suspects.isNotEmpty() -> { dy ->
                val next = cursor + dy
                if (next in suspects.indices) cursor = next else scrollBody(dy)
            }
            preview != null -> { dy -> scrollBody(dy) }
            else -> null
        },
        onButtonX = if (suspects.isNotEmpty()) ({ toggle(cursor) }) else null,
        bodyScrollState = scroll,
    ) {
        when (state) {
            is SyncPlanState.Planning, SyncPlanState.Idle -> {
                LinearProgressIndicator(
                    modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
                    color = GsColors.Accent,
                    trackColor = GsColors.PanelHi,
                )
                Text(
                    "Checking with the server…",
                    style = MaterialTheme.typography.bodySmall,
                    color = gsTextMuted(),
                )
            }
            is SyncPlanState.Failed -> Text(
                "Couldn't check with the server: ${state.message}",
                style = MaterialTheme.typography.bodyMedium,
                color = GsColors.ErrText,
            )
            is SyncPlanState.Ready -> {
                val p = state.preview
                Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    p.errors.firstOrNull()?.let { first ->
                        Text(
                            first + if (p.errors.size > 1) " (+${p.errors.size - 1} more)" else "",
                            style = MaterialTheme.typography.bodySmall,
                            color = GsColors.ErrText,
                        )
                    }
                    val lines = syncPreviewLines(p, included)
                    if (lines.isEmpty() && suspects.isEmpty()) {
                        Text("Nothing to sync.", style = MaterialTheme.typography.bodyMedium, color = GsColors.Text)
                    }
                    lines.forEach { line ->
                        val (icon, tint) = previewIcon(line.kind)
                        SummaryLine(icon, tint, line.text)
                    }
                }
                if (suspects.isNotEmpty()) {
                    Spacer(Modifier.height(12.dp))
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Filled.Warning, contentDescription = null, tint = GsColors.WarnText, modifier = Modifier.size(16.dp))
                        Spacer(Modifier.width(8.dp))
                        Text(
                            "Smaller than the server copy — new game or old backup?",
                            style = MaterialTheme.typography.labelLarge,
                            color = GsColors.WarnText,
                        )
                    }
                    Spacer(Modifier.height(6.dp))
                    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        suspects.forEachIndexed { i, s ->
                            SuspectRow(
                                suspect = s,
                                included = s.titleId in included,
                                selected = i == cursor,
                                onClick = {
                                    cursor = i
                                    toggle(i)
                                },
                                modifier = Modifier.bringIntoViewRequester(requesters[i]),
                            )
                        }
                    }
                    if (LocalShowButtonHints.current) {
                        Spacer(Modifier.height(6.dp))
                        Row(
                            modifier = Modifier.clearAndSetSemantics { contentDescription = "X: Include or skip" },
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            ButtonGlyph(GsButton.X)
                            Spacer(Modifier.width(5.dp))
                            Text("Include/skip", style = MaterialTheme.typography.labelMedium, color = gsTextMuted())
                        }
                    }
                }
                AnimatedVisibility(
                    visible = details,
                    enter = gsExpandIn(reduce),
                    exit = gsShrinkOut(reduce),
                ) {
                    Column(
                        modifier = Modifier.padding(top = 12.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        previewDetailGroups(p).forEach { (label, names) ->
                            Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                                Text(label, style = MaterialTheme.typography.labelMedium, color = gsTextMuted())
                                names.forEach { name ->
                                    Text(
                                        name,
                                        style = MaterialTheme.typography.bodySmall,
                                        color = GsColors.Text,
                                        maxLines = 1,
                                        overflow = TextOverflow.Ellipsis,
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

/** Icon and tint of a summary line (M5-6 icons; the text beside it carries the meaning). */
private fun previewIcon(kind: PreviewLineKind): Pair<ImageVector, Color> = when (kind) {
    PreviewLineKind.Upload -> Icons.Filled.ArrowUpward to GsColors.InfoText
    // It overwrites a save on this device (a copy is kept): Warn, like Force Download.
    PreviewLineKind.ReplaceLocal -> Icons.Filled.ArrowDownward to GsColors.WarnText
    PreviewLineKind.NewLocal -> Icons.Filled.Cloud to GsColors.InfoText
    PreviewLineKind.Conflict -> Icons.Filled.Warning to GsColors.ErrText
    PreviewLineKind.UpToDate -> Icons.Filled.CheckCircle to GsColors.OkText
    PreviewLineKind.Unchecked -> Icons.Outlined.HelpOutline to GsColors.TextMuted
}

@Composable
private fun SummaryLine(icon: ImageVector, tint: Color, text: String) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Icon(icon, contentDescription = null, tint = tint, modifier = Modifier.size(16.dp))
        Spacer(Modifier.width(8.dp))
        Text(text, style = MaterialTheme.typography.bodyMedium, color = GsColors.Text)
    }
}

/** One suspected rollback: name, both sizes, and whether it goes ("Include") or not ("Skip"). */
@Composable
private fun SuspectRow(
    suspect: SuspectUpload,
    included: Boolean,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    GsListRow(
        isSelected = selected,
        onClick = onClick,
        modifier = modifier,
        containerColor = GsColors.Bg2,
        clickLabel = if (included) "Skip" else "Include",
        stateDescription = if (included) "Included" else "Skipped",
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                suspect.displayName,
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.SemiBold,
                color = GsColors.Text,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                suspectSizeText(suspect) { formatBytes(it).ifEmpty { "0 B" } },
                style = MaterialTheme.typography.bodySmall,
                color = gsTextMuted(),
            )
        }
        GsPill(
            text = if (included) "Include" else "Skip",
            color = if (included) GsColors.Info else GsColors.Warn,
            decorative = true,
        )
    }
}
