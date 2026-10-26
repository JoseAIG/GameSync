package com.savesync.android.ui.screens

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import com.savesync.android.emulators.EmulatorCatalog
import com.savesync.android.ui.components.BannerKind
import com.savesync.android.ui.components.BannerState
import com.savesync.android.ui.components.GsBanner
import com.savesync.android.ui.components.LocalShowButtonHints
import com.savesync.android.ui.components.bannersFor
import com.savesync.android.ui.components.conflictBannerText
import com.savesync.android.ui.theme.GsColors
import com.savesync.android.ui.theme.LocalReduceMotion
import com.savesync.android.ui.theme.gsExpandIn
import com.savesync.android.ui.theme.gsShrinkOut

/**
 * The persistent-condition banners of Saves and Save detail, within the
 * banner budget ([bannersFor]: at most two, highest priority first).  Each
 * grows in from the top and shrinks back; a leaving banner keeps the text it
 * had, so it never reads "0 saves" on its way out.  A null action makes that
 * banner plain text.
 */
@Composable
fun StatusBanners(
    state: BannerState,
    modifier: Modifier = Modifier,
    /** The missing-folder banner's text (see [missingFolderBannerText]). */
    folderText: String = "",
    onOpenSettings: (() -> Unit)? = null,
    onReviewConflicts: (() -> Unit)? = null,
    onFixFolders: (() -> Unit)? = null,
) {
    val reduce = LocalReduceMotion.current
    val hintsOn = LocalShowButtonHints.current
    val shown = bannersFor(state)
    // Last non-empty copy, held while the banner animates out.
    val last = remember { arrayOf<Any>(0, "") }
    if (state.conflictCount > 0) last[0] = state.conflictCount
    if (folderText.isNotEmpty()) last[1] = folderText
    Column(modifier = modifier.fillMaxWidth()) {
        BannerKind.entries.forEach { kind ->
            AnimatedVisibility(
                visible = kind in shown,
                enter = gsExpandIn(reduce),
                exit = gsShrinkOut(reduce),
            ) {
                when (kind) {
                    BannerKind.NoServer -> GsBanner(
                        text = "No server set — open Settings to connect",
                        color = GsColors.Info,
                        onClick = onOpenSettings,
                    )
                    BannerKind.ServerUnreachable -> GsBanner(
                        text = "Server unreachable — statuses are from this device only",
                        color = GsColors.Warn,
                    )
                    BannerKind.Conflicts -> GsBanner(
                        text = conflictBannerText(last[0] as Int, hintsOn),
                        color = GsColors.Err,
                        onClick = onReviewConflicts,
                    )
                    BannerKind.FolderMissing -> GsBanner(
                        text = last[1] as String,
                        color = GsColors.Warn,
                        onClick = onFixFolders,
                    )
                }
            }
        }
    }
}

/**
 * The missing-folder banner's copy for the emulators whose save-folder
 * override points nowhere: "Azahar folder not found — 3DS saves are hidden.
 * Tap to fix." for one, "N emulator folders not found — tap to fix" for
 * more.  Empty when none.
 */
internal fun missingFolderBannerText(emulatorKeys: List<String>): String {
    if (emulatorKeys.isEmpty()) return ""
    if (emulatorKeys.size > 1) return "${emulatorKeys.size} emulator folders not found — tap to fix"
    val key = emulatorKeys.first()
    val descriptor = EmulatorCatalog.ALL.firstOrNull { it.key == key }
    val name = descriptor?.displayName ?: key
    val system = descriptor?.systemHint?.removePrefix("Nintendo ")?.takeIf { it.isNotBlank() }
    return if (system != null) "$name folder not found — $system saves are hidden. Tap to fix."
    else "$name folder not found — its saves are hidden. Tap to fix."
}
