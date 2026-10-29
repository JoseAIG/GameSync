package com.savesync.android.ui.screens

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ExpandLess
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.Visibility
import androidx.compose.material.icons.filled.VisibilityOff
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.savesync.android.BuildConfig
import com.savesync.android.api.ApiClient
import com.savesync.android.emulators.EmudeckPaths
import com.savesync.android.emulators.Ps2EmulatorChoice
import com.savesync.android.sync.SaturnSyncFormat
import com.savesync.android.sync.SegaCdSyncFormat
import com.savesync.android.ui.MainViewModel
import com.savesync.android.ui.SyncState
import com.savesync.android.ui.components.FolderPickerDialog
import com.savesync.android.ui.components.GsDialogHost
import com.savesync.android.ui.components.GsButton
import com.savesync.android.ui.components.GsChromeSpec
import com.savesync.android.ui.components.GsConfirmDialog
import com.savesync.android.ui.components.GsHint
import com.savesync.android.ui.components.GsScreenChrome
import com.savesync.android.ui.components.LocalShowButtonHints
import com.savesync.android.ui.components.gsFocusRing
import com.savesync.android.ui.theme.GsColors
import com.savesync.android.ui.theme.GsMotion
import com.savesync.android.ui.theme.GsShapes
import com.savesync.android.ui.theme.LocalReduceMotion
import com.savesync.android.ui.theme.gsDim
import com.savesync.android.ui.theme.gsExpandIn
import com.savesync.android.ui.theme.gsShrinkOut
import com.savesync.android.ui.theme.gsTextMuted
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

private val intervalOptions = listOf(5, 15, 30, 60)

/** Shown under the Auto sync switch and in its confirm (FORK.md: background sync). */
private const val AUTO_SYNC_WARNING =
    "Auto sync can replace a save while an emulator is running; the emulator may then " +
        "overwrite it. Keep emulators closed, or leave this off."

/**
 * Settings, saved on change: switches, dropdowns and folder pickers persist
 * the moment they change; the server URL and API key commit on Done, on
 * focus loss and when you leave the tab (see SettingsCommit.kt).  Each
 * control that commits shows a short inline "Saved".
 *
 * Grouped as cards, most-used first: Server, Library, Folders, Save formats,
 * Emulator configuration, Auto sync, Diagnostics (collapsed), About.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    viewModel: MainViewModel,
    onNavigateToTab: (Int) -> Unit = {},
    onNavigateToEmulators: () -> Unit = {}
) {
    val settings by viewModel.settings.collectAsState()
    val scope = rememberCoroutineScope()
    val keyboard = LocalSoftwareKeyboardController.current

    // ── Text fields: local while typing, committed on Done / focus loss / leave.
    var serverUrl by rememberSaveable { mutableStateOf("") }
    var apiKey by rememberSaveable { mutableStateOf("") }
    var fieldsLoaded by rememberSaveable { mutableStateOf(false) }
    var apiKeyVisible by rememberSaveable { mutableStateOf(false) }
    // The values last written (or read): null until the stored settings are
    // read, so nothing can be committed over them before that.
    val committed = remember { arrayOfNulls<String>(2) } // [url, key]

    // ── Immediate controls: shown optimistically, re-synced from storage.
    var autoSync by remember { mutableStateOf(settings.autoSyncEnabled) }
    var intervalMinutes by remember { mutableStateOf(settings.autoSyncIntervalMinutes) }
    var saturnSyncFormat by remember { mutableStateOf(settings.saturnSyncFormat) }
    var segaCdSyncFormat by remember { mutableStateOf(settings.segaCdSyncFormat) }
    var ps2Emulator by remember { mutableStateOf(settings.ps2Emulator) }
    LaunchedEffect(settings.autoSyncEnabled) { autoSync = settings.autoSyncEnabled }
    LaunchedEffect(settings.autoSyncIntervalMinutes) { intervalMinutes = settings.autoSyncIntervalMinutes }
    LaunchedEffect(settings.saturnSyncFormat) { saturnSyncFormat = settings.saturnSyncFormat }
    LaunchedEffect(settings.segaCdSyncFormat) { segaCdSyncFormat = settings.segaCdSyncFormat }
    LaunchedEffect(settings.ps2Emulator) { ps2Emulator = settings.ps2Emulator }

    // One counter per control: a commit bumps it and its "Saved" caption
    // (re)starts its hold.
    val savedTicks = remember { mutableStateOf(mapOf<String, Int>()) }
    fun tickOf(id: String) = savedTicks.value[id] ?: 0
    fun markSaved(id: String, job: Job) {
        scope.launch {
            job.join()
            savedTicks.value = savedTicks.value + (id to tickOf(id) + 1)
        }
    }

    LaunchedEffect(Unit) {
        val stored = viewModel.loadSettings()
        committed[0] = stored.serverUrl
        committed[1] = stored.apiKey
        if (!fieldsLoaded) {
            serverUrl = stored.serverUrl
            apiKey = stored.apiKey
            fieldsLoaded = true
        }
        if (stored.romScanDir.isNotBlank() || stored.emudeckDir.isNotBlank()) {
            viewModel.detectSystemFolders()
        }
    }

    val urlError = serverUrlError(serverUrl)

    fun commitUrl() {
        val v = normalizeServerUrl(serverUrl)
        val write = textToCommit(v, committed[0], valid = serverUrlError(v) == null) ?: return
        committed[0] = write
        markSaved("url", viewModel.commitServerConnection(serverUrl = write))
    }

    fun commitKey() {
        val write = textToCommit(apiKey, committed[1], valid = true) ?: return
        committed[1] = write
        markSaved("key", viewModel.commitServerConnection(apiKey = write))
    }

    // Leaving the tab (or rotating) with an uncommitted edit still saves it.
    // The view model's scope outlives this screen, so the write completes.
    val flushUrl by rememberUpdatedState(::commitUrl)
    val flushKey by rememberUpdatedState(::commitKey)
    DisposableEffect(Unit) {
        onDispose {
            flushUrl()
            flushKey()
        }
    }

    var connectionStatus by remember { mutableStateOf<String?>(null) }
    var connectionOk by remember { mutableStateOf<Boolean?>(null) }
    var confirmAutoSync by rememberSaveable { mutableStateOf(false) }
    var diagnosticsOpen by rememberSaveable { mutableStateOf(false) }
    var showFolderPicker by remember { mutableStateOf(false) }
    var showEmudeckFolderPicker by remember { mutableStateOf(false) }
    var folderPickerForSystem by remember { mutableStateOf<String?>(null) }
    var addSystemExpanded by remember { mutableStateOf(false) }
    var addSystemSelected by remember { mutableStateOf("") }

    val detectedFolders by viewModel.detectedSystemFolders.collectAsState()
    val syncState by viewModel.syncState.collectAsState()
    val catalogLoading by viewModel.romCatalogLoading.collectAsState()
    val savesScanning by viewModel.savesScanning.collectAsState()
    val catalogRefreshStatus by viewModel.catalogRefreshStatus.collectAsState()
    val romScanResults by viewModel.romScanResults.collectAsState()
    val prepareMessage by viewModel.prepareFoldersMessage.collectAsState()
    val retroPaths by viewModel.retroArchPaths.collectAsState()

    val romScanDir = settings.romScanDir
    val emudeckDir = settings.emudeckDir
    val effectiveRomScanDir = EmudeckPaths.romsDir(emudeckDir)?.absolutePath ?: romScanDir
    val reduce = LocalReduceMotion.current

    // With a controller attached, land on the first button so the D-pad has
    // somewhere to start (Settings is plain Material controls: D-pad moves
    // focus, A activates — see MainActivity.dispatchKeyEvent).  Not a text
    // field: focusing one would pop the keyboard by itself.  Test connection
    // is disabled (unfocusable) until a valid URL is set, so wait for the
    // stored fields and fall back to the always-enabled show/hide-key button.
    val firstEntryFocus = remember { FocusRequester() }
    val keyToggleFocus = remember { FocusRequester() }
    val gamepadHints = LocalShowButtonHints.current
    LaunchedEffect(gamepadHints, fieldsLoaded) {
        if (!gamepadHints || !fieldsLoaded) return@LaunchedEffect
        val canTest = serverUrlError(serverUrl) == null && serverUrl.isNotBlank()
        val focused = canTest && runCatching { firstEntryFocus.requestFocus() }.isSuccess
        if (!focused) runCatching { keyToggleFocus.requestFocus() }
    }

    // Footer hints, drawn by MainApp's persistent chrome.
    // The scroll edges under the header / over the footer follow the page.
    val pageScroll = rememberScrollState()
    val scrolledTop by remember { derivedStateOf { pageScroll.canScrollBackward } }
    val scrolledBottom by remember { derivedStateOf { pageScroll.canScrollForward } }
    val chromeSpec = remember(scrolledTop, scrolledBottom) {
        SETTINGS_CHROME.copy(scrolledUnderTop = scrolledTop, scrolledUnderBottom = scrolledBottom)
    }
    GsScreenChrome("settings", chromeSpec)

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(pageScroll)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {

        // ── 1. Server ──────────────────────────────────────────────────────
        SettingsCard("Server") {
            var urlFocused by remember { mutableStateOf(false) }
            OutlinedTextField(
                value = serverUrl,
                onValueChange = { serverUrl = it },
                label = { Text("Server URL") },
                placeholder = { Text("http://192.168.1.100:8000") },
                isError = urlError != null,
                supportingText = {
                    if (urlError != null) {
                        Text(urlError, color = GsColors.ErrText)
                    } else {
                        SavedCaption(tickOf("url"))
                    }
                },
                modifier = Modifier
                    .fillMaxWidth()
                    .onFocusChanged {
                        if (urlFocused && !it.isFocused) commitUrl()
                        urlFocused = it.isFocused
                    }
                    .gsFocusRing(GsShapes.Field),
                singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri, imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = {
                    commitUrl()
                    keyboard?.hide()
                }),
            )

            var keyFocused by remember { mutableStateOf(false) }
            OutlinedTextField(
                value = apiKey,
                onValueChange = { apiKey = it },
                label = { Text("API Key") },
                supportingText = { SavedCaption(tickOf("key")) },
                modifier = Modifier
                    .fillMaxWidth()
                    .onFocusChanged {
                        if (keyFocused && !it.isFocused) commitKey()
                        keyFocused = it.isFocused
                    }
                    .gsFocusRing(GsShapes.Field),
                singleLine = true,
                visualTransformation = if (apiKeyVisible) VisualTransformation.None
                    else PasswordVisualTransformation(),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password, imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = {
                    commitKey()
                    keyboard?.hide()
                }),
                trailingIcon = {
                    IconButton(
                        onClick = { apiKeyVisible = !apiKeyVisible },
                        modifier = Modifier
                            .focusRequester(keyToggleFocus)
                            .gsFocusRing(CircleShape),
                    ) {
                        Icon(
                            if (apiKeyVisible) Icons.Default.VisibilityOff else Icons.Default.Visibility,
                            contentDescription = if (apiKeyVisible) "Hide API key" else "Show API key"
                        )
                    }
                }
            )

            // Test Connection and its result share one row; the result line
            // keeps its height, so nothing below jumps when it changes.
            Row(
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Button(
                    onClick = {
                        // Test what is typed, and keep it: a test is a commit.
                        commitUrl()
                        commitKey()
                        scope.launch {
                            connectionStatus = "Testing…"
                            connectionOk = null
                            try {
                                val api = ApiClient.create(normalizeServerUrl(serverUrl), apiKey)
                                val resp = api.getStatus()
                                connectionStatus = "Connected — v${resp.version}"
                                connectionOk = true
                            } catch (e: Exception) {
                                connectionStatus = "Failed: ${e.message}"
                                connectionOk = false
                            }
                        }
                    },
                    enabled = urlError == null && serverUrl.isNotBlank(),
                    modifier = Modifier
                        .focusRequester(firstEntryFocus)
                        .gsFocusRing()
                ) {
                    Text("Test connection")
                }
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .heightIn(min = 40.dp),
                    contentAlignment = Alignment.CenterStart,
                ) {
                    AnimatedContent(
                        targetState = connectionStatus to connectionOk,
                        transitionSpec = { fadeIn(GsMotion.fade()) togetherWith fadeOut(GsMotion.fadeOut()) },
                        label = "connectionStatus",
                    ) { (status, ok) ->
                        Text(
                            text = status ?: "Not tested yet",
                            color = when (ok) {
                                true -> GsColors.OkText
                                false -> GsColors.ErrText
                                null -> gsDim()
                            },
                            style = MaterialTheme.typography.bodySmall,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                }
            }
        }

        // ── 2. Library: the console clients' "Rescan titles" / "Refresh catalog"
        SettingsCard("Library") {
            Row(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                // Stays enabled (and focusable) while a scan runs, as in
                // v055: disabling the button A was just pressed on would
                // drop the pad's focus.  A press during a scan does nothing.
                Button(
                    onClick = { if (!savesScanning) viewModel.scanSaves() },
                    enabled = syncState !is SyncState.Syncing,
                    modifier = Modifier
                        .weight(1f)
                        .gsFocusRing()
                ) {
                    Text(if (savesScanning) "Rescanning…" else "Rescan saves")
                }
                Button(
                    onClick = { viewModel.refreshCatalog() },
                    enabled = !catalogLoading,
                    modifier = Modifier
                        .weight(1f)
                        .gsFocusRing()
                ) {
                    Text(if (catalogLoading) "Refreshing…" else "Refresh catalog")
                }
            }
            Text(
                text = catalogRefreshStatus
                    ?: ("Refresh catalog asks the server to rescan its ROM folder, " +
                        "throws the cached catalog away and downloads it again."),
                style = MaterialTheme.typography.bodySmall,
                color = if (catalogRefreshStatus != null) GsColors.Text else gsDim()
            )
        }

        // ── 3. Folders ─────────────────────────────────────────────────────
        SettingsCard("Folders") {
            Caption(
                "Optional Emudeck folder. When set, 3DS uses storage/Azahar, " +
                    "GC/Wii uses storage/Dolphin, PS2 uses storage/NetherSX2, " +
                    "PSP uses storage/PPSSPP, and ROMs come from its roms folder. " +
                    "Other emulators keep their normal paths."
            )
            FolderRow(
                label = "Emudeck folder",
                path = emudeckDir,
                savedTick = tickOf("emudeck"),
                onBrowse = { showEmudeckFolderPicker = true },
            )
            if (emudeckDir.isNotBlank()) {
                OutlinedButton(
                    onClick = { markSaved("emudeck", viewModel.setEmudeckDir("")) },
                    modifier = Modifier.fillMaxWidth().gsFocusRing()
                ) {
                    Text("Clear Emudeck folder")
                }
            }

            Spacer(Modifier.height(8.dp))
            Caption(
                "The folder where your ROMs are organized by system subfolder " +
                    "(e.g. GBA/, MegaDrive/, PS1/, SNES/, …). GameSync uses it to detect " +
                    "which games you have installed so it can show server saves for them. " +
                    "When Emudeck is set, GameSync uses its roms folder."
            )
            FolderRow(
                label = "ROM directory",
                path = effectiveRomScanDir,
                savedTick = tickOf("roms"),
                onBrowse = { showFolderPicker = true },
            )
            if (romScanDir.isNotBlank() && emudeckDir.isBlank()) {
                OutlinedButton(
                    onClick = { markSaved("roms", viewModel.setRomScanDir("")) },
                    modifier = Modifier.fillMaxWidth().gsFocusRing()
                ) {
                    Text("Clear ROM directory")
                }
            }

            // Per-system folder overrides
            Spacer(Modifier.height(8.dp))
            val overrides = settings.romDirOverrides
            val allSystems = (detectedFolders.keys + overrides.keys).toSortedSet()
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            "System folders",
                            style = MaterialTheme.typography.labelMedium,
                            color = gsTextMuted(),
                        )
                        Spacer(Modifier.width(8.dp))
                        SavedCaption(tickOf("systems"))
                    }
                    Text(
                        text = "${allSystems.size} system${if (allSystems.size != 1) "s" else ""} detected",
                        style = MaterialTheme.typography.bodySmall,
                        color = gsDim(),
                    )
                }
                OutlinedButton(
                    onClick = { viewModel.detectSystemFolders() },
                    modifier = Modifier.gsFocusRing(),
                ) {
                    Text("Detect", style = MaterialTheme.typography.labelMedium)
                }
            }
            Caption(
                "GameSync auto-detects which subfolder of your ROM directory belongs " +
                    "to each system. Override any folder here — useful when a folder has " +
                    "a non-standard name (e.g. \"Saturn\" instead of \"SAT\")."
            )

            if (allSystems.isEmpty() && effectiveRomScanDir.isNotBlank()) {
                Caption("No system folders detected yet — press Detect.")
            }

            allSystems.forEach { system ->
                val effectivePath = overrides[system] ?: detectedFolders[system] ?: return@forEach
                val isOverridden = system in overrides
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    SystemChip(system)
                    Spacer(Modifier.width(8.dp))
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = effectivePath.substringAfterLast('/'),
                            style = MaterialTheme.typography.bodySmall,
                            fontWeight = if (isOverridden) FontWeight.Bold else FontWeight.Normal,
                            color = GsColors.Text,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                        if (isOverridden) {
                            Text(
                                text = effectivePath,
                                style = MaterialTheme.typography.labelSmall,
                                color = gsTextMuted(),
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                    }
                    IconButton(
                        onClick = { folderPickerForSystem = system },
                        modifier = Modifier.gsFocusRing(CircleShape),
                    ) {
                        Icon(
                            Icons.Default.FolderOpen,
                            contentDescription = "Change $system folder",
                            modifier = Modifier.size(20.dp),
                            tint = GsColors.Accent
                        )
                    }
                    if (isOverridden) {
                        IconButton(
                            onClick = {
                                markSaved("systems", viewModel.clearRomDirOverride(system))
                            },
                            modifier = Modifier.gsFocusRing(CircleShape),
                        ) {
                            Icon(
                                Icons.Default.Close,
                                contentDescription = "Reset $system to auto-detected",
                                modifier = Modifier.size(20.dp),
                                tint = GsColors.ErrText
                            )
                        }
                    }
                }
            }

            // Add a custom system folder not yet auto-detected
            val knownAddSystems = listOf(
                "SAT", "DC", "PS1", "PS2", "PSP", "GBA", "GBC", "GB", "SNES", "NES",
                "N64", "NDS", "3DS", "GC", "WII", "MD", "SMS", "GG", "SEGACD",
                "PCE", "PCECD", "NEOCD",
                "NGP", "WSWAN", "WSWANC", "A2600", "A7800", "LYNX", "MAME", "ARCADE"
            ).filter { it !in allSystems }

            if (knownAddSystems.isNotEmpty()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    ExposedDropdownMenuBox(
                        expanded = addSystemExpanded,
                        onExpandedChange = { addSystemExpanded = it },
                        modifier = Modifier.weight(1f)
                    ) {
                        OutlinedTextField(
                            value = addSystemSelected.ifBlank { "Add system…" },
                            onValueChange = {},
                            readOnly = true,
                            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(addSystemExpanded) },
                            modifier = Modifier
                                .fillMaxWidth()
                                .menuAnchor()
                                .gsFocusRing(GsShapes.Field),
                            textStyle = MaterialTheme.typography.bodySmall
                        )
                        ExposedDropdownMenu(
                            expanded = addSystemExpanded,
                            onDismissRequest = { addSystemExpanded = false }
                        ) {
                            knownAddSystems.forEach { sys ->
                                DropdownMenuItem(
                                    text = { Text(sys, style = MaterialTheme.typography.bodySmall) },
                                    onClick = {
                                        addSystemSelected = sys
                                        addSystemExpanded = false
                                    }
                                )
                            }
                        }
                    }
                    Button(
                        onClick = { folderPickerForSystem = addSystemSelected },
                        enabled = addSystemSelected.isNotBlank(),
                        modifier = Modifier.gsFocusRing(),
                    ) {
                        Icon(Icons.Default.Add, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.width(4.dp))
                        Text("Browse")
                    }
                }
            }
        }

        // ── 4. Save formats ────────────────────────────────────────────────
        SettingsCard("Save formats") {
            SettingsDropdown(
                label = "Saturn download format",
                value = saturnSyncFormat,
                options = SaturnSyncFormat.values().toList(),
                optionLabel = { it.label },
                description = "Server stays on Beetle/Mednafen format; this controls how Saturn saves are written locally.",
                savedTick = tickOf("saturn"),
                onSelect = { format ->
                    saturnSyncFormat = format
                    markSaved("saturn", viewModel.setSaturnSyncFormat(format))
                },
            )
            SettingsDropdown(
                label = "Sega CD save format",
                value = segaCdSyncFormat,
                options = SegaCdSyncFormat.values().toList(),
                optionLabel = { it.label },
                description = "Genesis Plus GX writes <game>.brm; PicoDrive writes <game>.srm. " +
                    "Pick the core you play with. Genesis Plus GX only flushes Sega CD " +
                    "backup RAM on Close Content — killing RetroArch from the task " +
                    "switcher loses the save before GameSync can see it.",
                savedTick = tickOf("segacd"),
                onSelect = { format ->
                    segaCdSyncFormat = format
                    markSaved("segacd", viewModel.setSegaCdSyncFormat(format))
                },
            )
            SettingsDropdown(
                label = "PS2 emulator",
                value = ps2Emulator,
                options = Ps2EmulatorChoice.values().toList(),
                optionLabel = { it.label },
                description = "Which emulator's memcards folder PS2 saves are read from and downloaded to.",
                savedTick = tickOf("ps2"),
                onSelect = { choice ->
                    ps2Emulator = choice
                    markSaved("ps2", viewModel.setPs2Emulator(choice))
                },
            )
        }

        // ── 5. Emulator configuration ──────────────────────────────────────
        Surface(
            onClick = onNavigateToEmulators,
            shape = GsShapes.Card,
            color = GsColors.Panel,
            modifier = Modifier
                .fillMaxWidth()
                .gsFocusRing(GsShapes.Card),
        ) {
            Row(
                modifier = Modifier.padding(16.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        "Emulator configuration",
                        style = MaterialTheme.typography.titleSmall,
                        color = GsColors.Text,
                    )
                    Spacer(Modifier.height(2.dp))
                    Caption(
                        "Per-emulator save folder overrides, RetroArch toggles, " +
                            "and other emulator-specific options."
                    )
                }
                Icon(Icons.Filled.ChevronRight, contentDescription = null, tint = gsTextMuted())
            }
        }

        // ── 6. Auto sync ───────────────────────────────────────────────────
        SettingsCard("Auto sync") {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text("Enable auto sync", color = GsColors.Text, modifier = Modifier.weight(1f))
                SavedCaption(tickOf("autosync"))
                Spacer(Modifier.width(8.dp))
                Switch(
                    checked = autoSync,
                    onCheckedChange = { on ->
                        if (on) {
                            // Off → on asks first; on → off doesn't.
                            confirmAutoSync = true
                        } else {
                            autoSync = false
                            markSaved("autosync", viewModel.setAutoSync(false))
                        }
                    },
                    modifier = Modifier.gsFocusRing(CircleShape),
                )
            }
            WarnCaption(AUTO_SYNC_WARNING)

            AnimatedVisibility(
                visible = autoSync,
                enter = gsExpandIn(reduce),
                exit = gsShrinkOut(reduce),
            ) {
                SettingsDropdown(
                    label = "Sync interval",
                    value = intervalMinutes,
                    options = intervalOptions,
                    optionLabel = { "$it minutes" },
                    description = null,
                    savedTick = tickOf("interval"),
                    onSelect = { minutes ->
                        intervalMinutes = minutes
                        markSaved("interval", viewModel.setAutoSyncInterval(minutes))
                    },
                )
            }
        }

        // ── 7. Diagnostics (collapsed) ─────────────────────────────────────
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .clip(GsShapes.Card)
                .background(GsColors.Panel),
        ) {
            Surface(
                onClick = { diagnosticsOpen = !diagnosticsOpen },
                shape = GsShapes.Card,
                color = Color.Transparent,
                modifier = Modifier
                    .fillMaxWidth()
                    .gsFocusRing(GsShapes.Card),
            ) {
                Row(
                    modifier = Modifier.padding(16.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(
                        "Diagnostics",
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.SemiBold,
                        color = gsTextMuted(),
                        modifier = Modifier.weight(1f),
                    )
                    Icon(
                        if (diagnosticsOpen) Icons.Filled.ExpandLess else Icons.Filled.ExpandMore,
                        contentDescription = if (diagnosticsOpen) "Hide diagnostics" else "Show diagnostics",
                        tint = gsTextMuted(),
                    )
                }
            }
            AnimatedVisibility(
                visible = diagnosticsOpen,
                enter = gsExpandIn(reduce),
                exit = gsShrinkOut(reduce),
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(start = 16.dp, end = 16.dp, bottom = 16.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    OutlinedButton(
                        onClick = { viewModel.runRomScanDiagnostic(romScanDir) },
                        modifier = Modifier.fillMaxWidth().gsFocusRing()
                    ) {
                        Text("Test ROM scan")
                    }
                    if (romScanResults.isNotEmpty()) {
                        Caption("Systems found in ROM directory:")
                        romScanResults.forEach { (system, count) ->
                            val ok = count > 0
                            Text(
                                text = "${if (ok) "✓" else "✗"}  $system  —  $count ROM${if (count != 1) "s" else ""}",
                                style = MaterialTheme.typography.labelSmall,
                                color = if (ok) GsColors.OkText else gsDim()
                            )
                        }
                    }

                    OutlinedButton(
                        onClick = { viewModel.checkRetroArchPaths() },
                        modifier = Modifier.fillMaxWidth().gsFocusRing()
                    ) {
                        Text("Diagnose RetroArch paths")
                    }
                    if (retroPaths.isNotEmpty()) {
                        Caption("RetroArch detected paths:")
                        for ((path, found) in retroPaths) {
                            Text(
                                text = "${if (found) "✓" else "✗"}  $path",
                                style = MaterialTheme.typography.labelSmall,
                                color = if (found) GsColors.OkText else gsDim()
                            )
                        }
                    }

                    // Canonical per-system folders, so catalog downloads land in
                    // predictable places.  Existing files/folders are never touched.
                    OutlinedButton(
                        onClick = { viewModel.prepareRomFolders(romScanDir) },
                        modifier = Modifier.fillMaxWidth().gsFocusRing(),
                        enabled = effectiveRomScanDir.isNotBlank(),
                    ) {
                        Text("Prepare ROM folders")
                    }
                    Caption(
                        "Creates the standard per-system folders " +
                            "(PS1, GBA, SEGACD, …) under your ROM directory. " +
                            "Existing folders are left alone."
                    )
                    prepareMessage?.let { msg ->
                        Text(
                            text = msg,
                            style = MaterialTheme.typography.labelSmall,
                            color = GsColors.OkText,
                        )
                    }
                }
            }
        }

        // (Dolphin GC memory card folder lives on the Emulator Configuration
        // screen with the other save-folder overrides; the legacy
        // `dolphinMemCardDir` value is auto-merged into
        // `saveDirOverrides["Dolphin"]` on first read.)

        // ── 8. About ───────────────────────────────────────────────────────
        SettingsCard("About") {
            Text(
                text = "GameSync v${BuildConfig.VERSION_NAME}",
                style = MaterialTheme.typography.bodyMedium,
                color = GsColors.Text,
            )
            Caption("Console ID: ${settings.consoleId}")
        }
    }

    GsDialogHost(confirmAutoSync.takeIf { it }) { _, _ ->
        GsConfirmDialog(
            title = "Turn on auto sync?",
            message = AUTO_SYNC_WARNING,
            confirmLabel = "Turn on",
            dismissLabel = "Keep off",
            onConfirm = {
                confirmAutoSync = false
                autoSync = true
                markSaved("autosync", viewModel.setAutoSync(true))
            },
            onDismiss = { confirmAutoSync = false },
        )
    }

    // Folder pickers save the moment a folder is chosen.
    GsDialogHost(showFolderPicker.takeIf { it }) { _, _ ->
        FolderPickerDialog(
            initialPath = romScanDir,
            onDismiss = { showFolderPicker = false },
            onFolderSelected = { path ->
                showFolderPicker = false
                markSaved("roms", viewModel.setRomScanDir(path))
            }
        )
    }

    GsDialogHost(showEmudeckFolderPicker.takeIf { it }) { _, _ ->
        FolderPickerDialog(
            initialPath = emudeckDir,
            onDismiss = { showEmudeckFolderPicker = false },
            onFolderSelected = { path ->
                showEmudeckFolderPicker = false
                markSaved("emudeck", viewModel.setEmudeckDir(path))
            }
        )
    }

    GsDialogHost(folderPickerForSystem) { pickerSystem, _ ->
        val currentOverride = settings.romDirOverrides[pickerSystem]
        val detectedPath = detectedFolders[pickerSystem]
        FolderPickerDialog(
            initialPath = currentOverride ?: detectedPath ?: romScanDir,
            onDismiss = { folderPickerForSystem = null },
            onFolderSelected = { path ->
                folderPickerForSystem = null
                addSystemSelected = ""
                markSaved("systems", viewModel.setRomDirOverride(pickerSystem, path))
            }
        )
    }
}

/** A settings group: a Panel card with a quiet header, no dividers. */
@Composable
private fun SettingsCard(title: String, content: @Composable ColumnScope.() -> Unit) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(GsShapes.Card)
            .background(GsColors.Panel)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text(
            title,
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.SemiBold,
            color = gsTextMuted(),
        )
        content()
    }
}

@Composable
private fun Caption(text: String) {
    Text(text = text, style = MaterialTheme.typography.bodySmall, color = gsDim())
}

/** A caution that stays visible: 4dp Warn edge and WarnText. */
@Composable
private fun WarnCaption(text: String) {
    Row(modifier = Modifier.fillMaxWidth().height(IntrinsicSize.Min)) {
        Box(
            Modifier
                .width(4.dp)
                .fillMaxHeight()
                .background(GsColors.Warn)
        )
        Text(
            text,
            style = MaterialTheme.typography.bodySmall,
            color = GsColors.WarnText,
            modifier = Modifier.padding(start = 8.dp, top = 2.dp, bottom = 2.dp),
        )
    }
}

/**
 * Inline "Saved" after a commit: fades in, holds [SAVED_CAPTION_HOLD_MS] and
 * fades out.  Keyed to the control's save counter, so a second change
 * restarts the hold.  It always keeps its space (only its alpha changes),
 * so nothing around it moves.
 */
@Composable
private fun SavedCaption(tick: Int) {
    var visible by remember { mutableStateOf(false) }
    LaunchedEffect(tick) {
        if (tick > 0) {
            visible = true
            delay(SAVED_CAPTION_HOLD_MS)
            visible = false
        }
    }
    val alpha = animateFloatAsState(
        targetValue = if (visible) 1f else 0f,
        animationSpec = GsMotion.fade(),
        label = "savedCaption",
    )
    Row(
        modifier = Modifier.graphicsLayer { this.alpha = alpha.value },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            Icons.Filled.Check,
            contentDescription = null,
            tint = GsColors.OkText,
            modifier = Modifier.size(12.dp),
        )
        Spacer(Modifier.width(2.dp))
        Text("Saved", style = MaterialTheme.typography.labelSmall, color = GsColors.OkText)
    }
}

/** Folder label + path (or "Not set") with its Browse button and "Saved". */
@Composable
private fun FolderRow(label: String, path: String, savedTick: Int, onBrowse: () -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(label, style = MaterialTheme.typography.labelMedium, color = gsTextMuted())
                Spacer(Modifier.width(8.dp))
                SavedCaption(savedTick)
            }
            Text(
                text = path.ifBlank { "Not set" },
                style = MaterialTheme.typography.bodySmall,
                color = if (path.isNotBlank()) GsColors.Text else gsDim(),
            )
        }
        Spacer(Modifier.width(8.dp))
        Button(onClick = onBrowse, modifier = Modifier.gsFocusRing()) {
            Icon(Icons.Default.FolderOpen, contentDescription = null, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(6.dp))
            Text("Browse")
        }
    }
}

/** A read-only dropdown field that commits on pick. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun <T> SettingsDropdown(
    label: String,
    value: T,
    options: List<T>,
    optionLabel: (T) -> String,
    description: String?,
    savedTick: Int,
    onSelect: (T) -> Unit,
) {
    var expanded by remember { mutableStateOf(false) }
    ExposedDropdownMenuBox(expanded = expanded, onExpandedChange = { expanded = it }) {
        OutlinedTextField(
            value = optionLabel(value),
            onValueChange = {},
            readOnly = true,
            label = { Text(label) },
            supportingText = {
                Column {
                    if (description != null) Text(description)
                    SavedCaption(savedTick)
                }
            },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded) },
            modifier = Modifier
                .fillMaxWidth()
                .menuAnchor()
                .gsFocusRing(GsShapes.Field)
        )
        ExposedDropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            options.forEach { option ->
                DropdownMenuItem(
                    text = { Text(optionLabel(option)) },
                    onClick = {
                        expanded = false
                        if (option != value) onSelect(option)
                    }
                )
            }
        }
    }
}

/** Settings has no header actions; its hints never change. */
private val SETTINGS_CHROME = GsChromeSpec(
    hints = listOf(
        GsHint(GsButton.DPAD, "Move"),
        GsHint(GsButton.A, "Select"),
        GsHint(GsButton.L1, "Tabs"),
        GsHint(GsButton.START, "Exit"),
    ),
)
