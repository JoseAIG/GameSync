package com.savesync.android.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.savesync.android.emulators.EmulatorCatalog
import com.savesync.android.emulators.EmulatorDescriptor
import com.savesync.android.emulators.EmulatorRegistry
import com.savesync.android.emulators.impl.RetroArchEmulator
import com.savesync.android.ui.MainViewModel
import com.savesync.android.ui.components.GsButton
import com.savesync.android.ui.components.GsDialogHost
import com.savesync.android.ui.components.GsChromeSpec
import com.savesync.android.ui.components.GsScreenChrome
import com.savesync.android.ui.components.GsHint
import com.savesync.android.ui.components.FolderPickerDialog
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import com.savesync.android.ui.components.GsBanner
import com.savesync.android.ui.components.gsFocusRing
import androidx.compose.foundation.shape.CircleShape
import com.savesync.android.ui.theme.GsColors
import com.savesync.android.ui.theme.LocalReduceMotion
import com.savesync.android.ui.theme.gsExpandIn
import com.savesync.android.ui.theme.gsShrinkOut

/**
 * Per-emulator save folder configuration screen, reachable from
 * [SettingsScreen] via the "Emulator Configuration →" button.
 *
 * One card per emulator showing:
 *   • The current effective save folder (override OR auto-detected hint)
 *   • Browse / Clear buttons that write to ``Settings.saveDirOverrides``
 *   • Emulator-specific extras (RetroArch hosts the Beetle Saturn per-core
 *     and CD per-content toggles here so all of one emulator's knobs stay
 *     together)
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun EmulatorsScreen(
    viewModel: MainViewModel,
    onNavigateBack: () -> Unit,
) {
    val settings by viewModel.settings.collectAsState()
    // Computed off the main thread by the view model; composition reads no files.
    val overrideWarnings by viewModel.overrideWarnings.collectAsState()

    var folderPickerForEmulator by remember { mutableStateOf<String?>(null) }

    // Sub-screen header and hints, drawn by MainApp's persistent chrome.
    val pageScroll = rememberScrollState()
    val scrolledTop by remember { derivedStateOf { pageScroll.canScrollBackward } }
    val scrolledBottom by remember { derivedStateOf { pageScroll.canScrollForward } }
    val chromeSpec = remember(onNavigateBack, scrolledTop, scrolledBottom) {
        GsChromeSpec(
            title = "Emulator Configuration",
            onBack = onNavigateBack,
            scrolledUnderTop = scrolledTop,
            scrolledUnderBottom = scrolledBottom,
            hints = listOf(
                GsHint(GsButton.DPAD, "Move"),
                GsHint(GsButton.A, "Select"),
                GsHint(GsButton.B, "Back"),
            ),
        )
    }
    GsScreenChrome("emulators", chromeSpec)

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(pageScroll)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text(
            text = "Set a custom save folder for each emulator. Existing " +
                   "auto-detection (Emudeck root, retroarch.cfg, etc.) " +
                   "is used when no override is configured. Existing " +
                   "saves on disk are still discovered regardless.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

        EmulatorCatalog.ALL.forEach { descriptor ->
            EmulatorCard(
                descriptor = descriptor,
                overridePath = settings.saveDirOverrides[descriptor.key],
                overrideWarning = overrideWarnings[descriptor.key],
                onBrowse = { folderPickerForEmulator = descriptor.key },
                onClear = { viewModel.clearSaveDirOverride(descriptor.key) },
                extras = if (descriptor.key == RetroArchEmulator.EMULATOR_KEY) {
                    {
                        RetroArchExtras(
                            beetleSaturnPerCoreFolder = settings.beetleSaturnPerCoreFolder,
                            cdGamesPerContentFolder = settings.cdGamesPerContentFolder,
                            onBeetleChange = { newBeetle ->
                                viewModel.saveRetroArchToggles(
                                    beetleSaturnPerCoreFolder = newBeetle,
                                    cdGamesPerContentFolder = settings.cdGamesPerContentFolder
                                )
                            },
                            onPerContentChange = { newPerContent ->
                                viewModel.saveRetroArchToggles(
                                    beetleSaturnPerCoreFolder = settings.beetleSaturnPerCoreFolder,
                                    cdGamesPerContentFolder = newPerContent
                                )
                            },
                        )
                    }
                } else null,
            )
        }
    }

    GsDialogHost(folderPickerForEmulator) { pickerKey, _ ->
        FolderPickerDialog(
            initialPath = settings.saveDirOverrides[pickerKey] ?: "",
            // The contents line runs this emulator's own folder check.
            emulatorKey = pickerKey,
            onDismiss = { folderPickerForEmulator = null },
            onFolderSelected = { path ->
                viewModel.setSaveDirOverride(pickerKey, path)
                folderPickerForEmulator = null
            }
        )
    }
}

@Composable
private fun EmulatorCard(
    descriptor: EmulatorDescriptor,
    overridePath: String?,
    overrideWarning: String?,
    onBrowse: () -> Unit,
    onClear: () -> Unit,
    extras: (@Composable () -> Unit)? = null,
) {
    val effectivePath = overridePath?.takeIf { it.isNotBlank() }
    val isOverridden = effectivePath != null

    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant
        ),
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            // Title + subtitle
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = descriptor.displayName,
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold,
                    )
                    Text(
                        text = descriptor.systemHint,
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }

            // Effective save path
            Column(modifier = Modifier.fillMaxWidth()) {
                Text(
                    text = if (isOverridden) "Override" else "Auto-detected",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text(
                    text = effectivePath ?: descriptor.defaultPathHint,
                    style = MaterialTheme.typography.bodySmall,
                    fontWeight = if (isOverridden) FontWeight.Bold else FontWeight.Normal,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
            }

            // The fork's missing-folder warning: a Warn row right above the
            // buttons that fix it, staying until the folder exists or the
            // override is cleared.  The message is kept as it was.
            val reduce = LocalReduceMotion.current
            val lastWarning = remember { arrayOf("") }
            if (overrideWarning != null) lastWarning[0] = overrideWarning
            AnimatedVisibility(
                visible = overrideWarning != null,
                enter = gsExpandIn(reduce),
                exit = gsShrinkOut(reduce),
            ) {
                GsBanner(
                    text = lastWarning[0],
                    color = GsColors.Warn,
                    leadingIcon = Icons.Filled.Warning,
                )
            }

            // Browse / Clear buttons
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                // While the override points nowhere, Change is the way out:
                // promoted to the filled Accent button.
                // One composable either way, styled by the warning: swapping
                // Button for OutlinedButton would drop the focus a pad user
                // had on it when the warning comes or goes.
                val browseLabel = if (isOverridden) "Change" else "Set Override"
                val promoted = overrideWarning != null
                Button(
                    onClick = onBrowse,
                    modifier = Modifier.weight(1f).gsFocusRing(),
                    colors = if (promoted) {
                        ButtonDefaults.buttonColors(
                            containerColor = GsColors.Accent,
                            contentColor = GsColors.Ink,
                        )
                    } else {
                        ButtonDefaults.outlinedButtonColors()
                    },
                    elevation = if (promoted) ButtonDefaults.buttonElevation() else null,
                    border = if (promoted) null else ButtonDefaults.outlinedButtonBorder,
                    contentPadding = ButtonDefaults.ContentPadding,
                ) {
                    Icon(Icons.Default.FolderOpen, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(6.dp))
                    Text(browseLabel)
                }
                if (isOverridden) {
                    OutlinedButton(onClick = onClear, modifier = Modifier.gsFocusRing()) {
                        Icon(
                            Icons.Default.Close,
                            contentDescription = "Clear override",
                            modifier = Modifier.size(18.dp),
                        )
                        Spacer(Modifier.width(6.dp))
                        Text("Clear")
                    }
                }
            }

            // Emulator-specific extras (e.g. RetroArch toggles)
            if (extras != null) {
                HorizontalDivider()
                extras()
            }
        }
    }

    Spacer(Modifier.height(4.dp))
}

@Composable
private fun RetroArchExtras(
    beetleSaturnPerCoreFolder: Boolean,
    cdGamesPerContentFolder: Boolean,
    onBeetleChange: (Boolean) -> Unit,
    onPerContentChange: (Boolean) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text("Beetle Saturn: per-core save folder")
            Text(
                text = if (beetleSaturnPerCoreFolder)
                    "saves/Beetle Saturn/<rom>.bkr (matches RetroArch's Sort Saves by Core)."
                else
                    "saves/<rom>.bkr at the root.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        Switch(
            checked = beetleSaturnPerCoreFolder,
            onCheckedChange = onBeetleChange,
            modifier = Modifier.gsFocusRing(CircleShape),
        )
    }

    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text("CD games: per-content folder")
            Text(
                text = if (cdGamesPerContentFolder)
                    "CD-game saves go in saves/<game>/<rom>.<ext> (PS1, PS2, Saturn, Sega CD, Dreamcast, PCE, NeoCD)."
                else
                    "CD-game saves land at saves/<rom>.<ext> (root).",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        Switch(
            checked = cdGamesPerContentFolder,
            onCheckedChange = onPerContentChange,
            modifier = Modifier.gsFocusRing(CircleShape),
        )
    }
}
