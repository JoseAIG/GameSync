package com.savesync.android.ui.screens

import androidx.compose.foundation.BorderStroke
import android.content.res.Configuration
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.MutableTransitionState
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.filled.ExpandLess
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.snapshotFlow
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import com.savesync.android.ui.components.ButtonGlyph
import com.savesync.android.ui.components.GsDialogHost
import com.savesync.android.ui.components.GsBanner
import com.savesync.android.ui.components.GsListRow
import com.savesync.android.ui.components.GsDialogChoice
import com.savesync.android.ui.components.GsDialog
import androidx.compose.material.icons.filled.CheckBoxOutlineBlank
import androidx.compose.material.icons.filled.CheckBox
import androidx.compose.material.icons.filled.RadioButtonUnchecked
import androidx.compose.material.icons.filled.RadioButtonChecked
import com.savesync.android.ui.components.GsInputMode
import com.savesync.android.ui.components.GsPill
import com.savesync.android.ui.components.LocalInputMode
import com.savesync.android.ui.components.LocalShowButtonHints
import com.savesync.android.ui.newerSide
import com.savesync.android.ui.theme.GsShapes
import com.savesync.android.ui.theme.LocalHighContrast
import com.savesync.android.ui.theme.LocalReduceMotion
import com.savesync.android.ui.theme.gsExpandIn
import com.savesync.android.ui.theme.gsShrinkOut
import com.savesync.android.ui.theme.gsTextMuted
import kotlinx.coroutines.flow.first
import androidx.compose.material.icons.filled.History
import com.savesync.android.sync.LocalBackup
import com.savesync.android.ui.components.GsConfirmDialog
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.relocation.BringIntoViewRequester
import androidx.compose.foundation.relocation.bringIntoViewRequester
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Cloud
import androidx.compose.material.icons.filled.Smartphone
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.CloudDownload
import androidx.compose.material.icons.filled.CloudUpload
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.Sync
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.savesync.android.api.msuPackKind
import com.savesync.android.api.preferredDownloadExtractFormat
import com.savesync.android.api.preferredDownloadFilename
import com.savesync.android.emulators.SaveEntry
import com.savesync.android.storage.SyncStateEntity
import com.savesync.android.ui.MainViewModel
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.ui.graphics.graphicsLayer
import com.savesync.android.ui.components.GS_BUTTON_PRESS_SCALE
import com.savesync.android.ui.components.GsButton
import com.savesync.android.ui.components.rememberPressScale
import com.savesync.android.ui.components.GsChromeSpec
import com.savesync.android.ui.components.GsScreenChrome
import com.savesync.android.ui.components.GsHint
import com.savesync.android.ui.theme.GsColors
import com.savesync.android.ui.components.GsHaptic
import com.savesync.android.ui.components.GsNotice
import com.savesync.android.ui.components.GsNoticeKind
import com.savesync.android.ui.components.LocalGsNotifier
import com.savesync.android.ui.components.noticeKindForSaveMessage
import com.savesync.android.ui.components.stripLeadingGlyph
import com.savesync.android.ui.NormalizePickerState
import com.savesync.android.ui.SaturnArchivePickerState
import com.savesync.android.ui.SaveDetailState
import com.savesync.android.ui.SaveSyncStatus
import com.savesync.android.ui.ServerMetaState
import com.savesync.android.ui.components.BannerState
import com.savesync.android.ui.components.LocalServerOnline
import com.savesync.android.ui.ServerSide
import com.savesync.android.ui.detailSyncStatus
import androidx.compose.runtime.produceState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun SaveDetailScreen(
    titleId: String,
    viewModel: MainViewModel,
    syncStateEntities: List<SyncStateEntity>,
    onNavigateBack: () -> Unit,
    onNavigateToTab: (Int) -> Unit = {},
    /** Opened from a conflict (detail/{titleId}?reason=conflict): show the conflict card. */
    openedFromConflict: Boolean = false,
) {
    // Unfiltered: a status change that drops this game from the filtered
    // list must not empty the screen.
    val saves by viewModel.allSaves.collectAsState()
    val allRows by viewModel.allRows.collectAsState()
    val detailState by viewModel.saveDetailState.collectAsState()
    val serverMeta by viewModel.serverMeta.collectAsState()
    // null until the stored settings are read: no "No server" flash.
    val serverConfigured by viewModel.serverConfigured.collectAsState()
    // App-wide notices: a message survives this screen leaving, so a result
    // that navigates back does so at once instead of after the snackbar.
    val notifier = LocalGsNotifier.current
    val normalizePickerState by viewModel.normalizePicker.collectAsState()
    val saturnArchivePickerState by viewModel.saturnArchivePicker.collectAsState()
    val saturnArchiveSelectionVersion by viewModel.saturnArchiveSelectionVersion.collectAsState()

    // The scanned list can lag behind disk (a download since the last scan, or
    // the emulator writing the predicted card while we sat in the background).
    // Re-check so a save that really exists gets a hash, an enabled upload, and
    // a three-way sync instead of an unconditional download.
    val entry = saves.find { it.titleId == titleId }?.reconcileWithDisk()
    val syncState = syncStateEntities.find { it.titleId == titleId }

    // Auto-fetch server metadata when screen opens
    LaunchedEffect(titleId, entry?.systemName) {
        viewModel.fetchServerMeta(titleId, entry?.systemName)
        viewModel.fetchRomAvailable()
        viewModel.resetRomDownloadState()
        viewModel.prepareSaveDetail(entry)
    }

    // Show result messages
    LaunchedEffect(detailState) {
        when (val state = detailState) {
            is SaveDetailState.Success -> {
                val kind = noticeKindForSaveMessage(state.message)
                notifier.show(
                    saveResultNotice(state) { id -> viewModel.restoreLatestBackup(id) },
                    // A conflict is a Warning that still rejects.
                    hapticOverride = if (kind == GsNoticeKind.Warning) GsHaptic.Reject else null,
                )
                viewModel.resetDetailState()
                if (state.navigateBack) onNavigateBack()
            }
            is SaveDetailState.Error -> {
                notifier.show(errorNotice(state.message) { onNavigateToTab(SETTINGS_TAB) })
                viewModel.resetDetailState()
            }
            else -> Unit
        }
    }

    // Normalize name and Saturn archive pickers: GsDialogs with their own
    // cursor (Up/Down through onNavigate), A applies, B cancels.
    GsDialogHost(normalizePickerState as? NormalizePickerState.Visible) { pickerState, _ ->
        NormalizePickerDialog(
            state = pickerState,
            onApply = { name -> viewModel.applyNormalizationChoice(pickerState.entry, name) },
            onDismiss = { viewModel.dismissNormalizePicker() },
        )
    }

    GsDialogHost(saturnArchivePickerState as? SaturnArchivePickerState.Visible) { saturnPickerState, _ ->
        SaturnArchivePickerDialog(
            state = saturnPickerState,
            onApply = { selected -> viewModel.applySaturnArchiveSelection(selected) },
            onDismiss = { viewModel.dismissSaturnArchivePicker() },
        )
    }

    // Sub-screen header and hints, drawn by MainApp's persistent chrome.
    // Keyed by the title id, so a second game never shows this one's title.
    val detailTitle = entry?.displayName ?: titleId
    val chromeSpec = remember(detailTitle, onNavigateBack) {
        GsChromeSpec(
            title = detailTitle,
            onBack = onNavigateBack,
            hints = listOf(
                GsHint(GsButton.DPAD, "Move"),
                GsHint(GsButton.A, "Select"),
                GsHint(GsButton.Y, "Details"),
                GsHint(GsButton.B, "Back"),
                GsHint(GsButton.L1, "Tabs"),
            ),
        )
    }
    GsScreenChrome("detail/$titleId", chromeSpec)

    // Coming back from the emulator: refresh the server's side and re-read
    // this device's files (the first ON_RESUME is the screen opening).
    val lifecycleOwner = LocalLifecycleOwner.current
    var resumeTick by remember { mutableIntStateOf(0) }
    val currentSystem by rememberUpdatedState(entry?.systemName)
    DisposableEffect(lifecycleOwner, titleId) {
        var opened = false
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) {
                if (opened) {
                    resumeTick++
                    viewModel.fetchServerMeta(titleId, currentSystem)
                }
                opened = true
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }

    if (entry == null) {
        Box(
            Modifier.fillMaxSize(),
            contentAlignment = Alignment.Center
        ) {
            Text("Save not found")
        }
        return
    }

    val isBusy = detailState is SaveDetailState.Working
    val canDownloadSave = entry.saveFile != null || entry.saveDir != null
    val canUploadSave = viewModel.canUploadFromDetail(entry)

    // Compute local hash — recompute when the underlying file is modified (e.g. after download)
    val fileModTime = when {
        entry.saveFile != null && entry.extraFiles.isNotEmpty() ->
            (listOf(entry.saveFile) + entry.extraFiles).filter { it.exists() }.maxOfOrNull { it.lastModified() } ?: 0L
        else -> entry.saveFile?.lastModified() ?: entry.saveDir?.lastModified() ?: 0L
    }
    // Hashing and walking a save folder read files: off the main thread.
    val localHash by produceState<String?>(null, entry.titleId, fileModTime, saturnArchiveSelectionVersion, resumeTick) {
        value = viewModel.detailLocalHash(entry)
    }
    val localSize by produceState(0L, entry.titleId, fileModTime, saturnArchiveSelectionVersion, resumeTick) {
        value = withContext(Dispatchers.IO) { viewModel.detailLocalSize(entry) }
    }
    // "Modified": the newest file of the save — the same timestamp sync sends.
    val modifiedAt by produceState(0L, entry.titleId, fileModTime, resumeTick) {
        value = viewModel.detailModifiedAt(entry)
    }
    // The header pill: the same three values the Server facts below show
    // (local hash, last-synced hash, server hash), so the two agree.
    val serverSide = when (val m = serverMeta) {
        is ServerMetaState.Found -> if (m.hash.isBlank() || m.hash == "—") ServerSide.Unknown else ServerSide.Hash(m.hash)
        is ServerMetaState.NotFound -> ServerSide.Missing
        else -> ServerSide.Unknown
    }
    val conflictIds by viewModel.conflictTitleIds.collectAsState()
    val conflictInfos by viewModel.conflictInfo.collectAsState()
    val inConflictSet = titleId in conflictIds
    val rowStatus = allRows.firstOrNull { it.entry.titleId == titleId }?.status
    val headerStatus = detailSyncStatus(
        hasLocal = canUploadSave,
        localHash = localHash,
        lastSyncedHash = syncState?.lastSyncedHash,
        server = serverSide,
        fallback = rowStatus,
        conflict = inConflictSet,
    )
    val showConflictCard = headerStatus == SaveSyncStatus.CONFLICT || (openedFromConflict && inConflictSet)
    val serverTime = (serverMeta as? ServerMetaState.Found)?.timestamp ?: 0L
    val newer = newerSide(modifiedAt, serverTime)

    // ── ROM availability (needed before we build the action list) ──
    val romAvailable by viewModel.romAvailable.collectAsState()
    val romsByTitle by viewModel.romsByTitle.collectAsState()
    val romDownloadState by viewModel.romDownloadState.collectAsState()
    val romBusy = romDownloadState is MainViewModel.RomDownloadState.Downloading
    val hasRom = entry.titleId in romAvailable
    val roms = if (hasRom) romsByTitle[entry.titleId].orEmpty() else emptyList()

    // ── Local copy kept by the last download (sync/LocalBackups.kt) ──
    val backupsVersion by viewModel.backupsVersion.collectAsState()
    val latestBackup by produceState<LocalBackup?>(null, titleId, backupsVersion, resumeTick, fileModTime) {
        value = viewModel.latestBackup(titleId)
    }
    // The three confirms: overwriting either side, and putting a copy back.
    var confirmDownload by remember { mutableStateOf(false) }
    var confirmUpload by remember { mutableStateOf(false) }
    var confirmRestore by remember { mutableStateOf<LocalBackup?>(null) }

    // ── Gamepad navigation state ───────────────────────────────────
    val focusRequester = remember { FocusRequester() }
    var selectedActionIndex by remember { mutableIntStateOf(0) }
    // Opened from a conflict, the cursor starts on the Force row of the
    // newer side once both times are known — never on its own after the
    // user moved, and A on it only opens the confirm.
    var cursorMovedByUser by remember { mutableStateOf(false) }
    var conflictCursorPlaced by remember { mutableStateOf(false) }

    // The ordered, gamepad-selectable actions; list order is D-pad order
    // (unchanged).  Smart Sync is the one filled primary; the overrides are
    // outlined (Force Download's icon in Warn: it overwrites), ROM rows
    // tonal, Normalize tertiary.
    val actions: List<DetailAction> = buildList {
        add(DetailAction(
            label = if (isBusy && detailState.isSync) "Syncing…" else "Smart Sync",
            icon = Icons.Default.Sync,
            enabled = !isBusy && (!entry.isServerOnly || canDownloadSave),
            style = ActionStyle.Primary,
            isBusy = isBusy && detailState.isSync,
            onClick = { viewModel.syncSave(entry) }
        ))
        add(DetailAction(
            label = if (isBusy && detailState.isUpload) "Uploading…" else "Force Upload",
            icon = Icons.Default.CloudUpload,
            enabled = !isBusy && canUploadSave,
            style = ActionStyle.Outlined,
            isBusy = isBusy && detailState.isUpload,
            headerAbove = "Override",
            onClick = { confirmUpload = true }
        ))
        add(DetailAction(
            label = if (isBusy && detailState.isDownload) "Downloading…" else "Force Download",
            icon = Icons.Default.CloudDownload,
            iconTint = GsColors.Warn,
            enabled = !isBusy && canDownloadSave,
            style = ActionStyle.Outlined,
            isBusy = isBusy && detailState.isDownload,
            onClick = { confirmDownload = true }
        ))
        if (hasRom) {
            if (roms.size <= 1) {
                val rom = roms.firstOrNull()
                add(DetailAction(
                    label = when (romDownloadState) {
                        is MainViewModel.RomDownloadState.Downloading -> "Downloading ROM…"
                        is MainViewModel.RomDownloadState.Success -> "ROM downloaded"
                        else -> "Download ROM"
                    },
                    icon = Icons.Default.CloudDownload,
                    enabled = !isBusy && !romBusy && rom != null,
                    style = ActionStyle.Tonal,
                    isBusy = romBusy,
                    onClick = {
                        if (rom != null) {
                            val extract = rom.preferredDownloadExtractFormat()
                            viewModel.downloadRom(
                                romId = rom.rom_id ?: rom.title_id,
                                system = rom.system,
                                filename = rom.preferredDownloadFilename(extract),
                                extractFormat = extract,
                                bundleKind = rom.msuPackKind,
                            )
                        }
                    }
                ))
            } else {
                roms.forEachIndexed { index, rom ->
                    add(DetailAction(
                        label = when (romDownloadState) {
                            is MainViewModel.RomDownloadState.Downloading -> "Downloading ${rom.filename}…"
                            is MainViewModel.RomDownloadState.Success -> "${rom.filename} downloaded"
                            else -> "Download ${rom.filename}"
                        },
                        icon = Icons.Default.CloudDownload,
                        enabled = !isBusy && !romBusy,
                        style = ActionStyle.Tonal,
                        isBusy = romBusy,
                        headerAbove = if (index == 0) "Available ROMs" else null,
                        onClick = {
                            val extract = rom.preferredDownloadExtractFormat()
                            viewModel.downloadRom(
                                romId = rom.rom_id ?: rom.title_id,
                                system = rom.system,
                                filename = rom.preferredDownloadFilename(extract),
                                extractFormat = extract,
                                bundleKind = rom.msuPackKind,
                            )
                        }
                    ))
                }
            }
        }
        if (!entry.isServerOnly && entry.systemName != "PSP") {
            add(DetailAction(
                label = if (isBusy && detailState.isNormalize) "Normalizing…" else
                    entry.canonicalName?.let { "Normalize (→ $it)" } ?: "Normalize Name",
                icon = Icons.Default.Edit,
                enabled = !isBusy,
                style = ActionStyle.Tertiary,
                isBusy = isBusy && detailState.isNormalize,
                onClick = { viewModel.normalizeRomAndSave(entry) }
            ))
        }
        // The pad path for Undo: put the copy kept by the last download back.
        latestBackup?.let { backup ->
            add(DetailAction(
                label = if (isBusy && detailState.isRestore) "Restoring…"
                    else "Restore previous copy (${formatWhen(backup.takenAt)})",
                icon = Icons.Filled.History,
                enabled = !isBusy,
                style = ActionStyle.Outlined,
                isBusy = isBusy && detailState.isRestore,
                onClick = { confirmRestore = backup }
            ))
        }
    }

    LaunchedEffect(openedFromConflict, newer) {
        if (openedFromConflict && !conflictCursorPlaced && !cursorMovedByUser && newer != 0) {
            selectedActionIndex = if (newer < 0) FORCE_UPLOAD_INDEX else FORCE_DOWNLOAD_INDEX
            conflictCursorPlaced = true
        }
    }

    // Clamp selection when the list size changes (e.g. ROM availability
    // resolves, or Normalize becomes applicable).
    LaunchedEffect(actions.size) {
        if (actions.isNotEmpty()) {
            selectedActionIndex = selectedActionIndex.coerceIn(0, actions.size - 1)
        } else {
            selectedActionIndex = 0
        }
    }

    // Per-action requesters so D-pad/stick scrolls the selected button
    // into the visible window on smaller screens (verticalScroll).  Every
    // block above the actions (hero, conflict card, facts) scrolls with
    // them, so the selected action stays reachable on a 360dp-tall screen.
    val bringRequesters = remember(actions.size) {
        List(actions.size) { BringIntoViewRequester() }
    }
    LaunchedEffect(selectedActionIndex, actions.size) {
        bringRequesters.getOrNull(selectedActionIndex)?.bringIntoView()
    }

    // Technical details: collapsed; Y (unused here otherwise) or a tap on
    // its header toggles it, and an expand brings it into view without
    // moving the action cursor.
    var detailsExpanded by rememberSaveable { mutableStateOf(false) }
    val detailsTransition = remember { MutableTransitionState(false) }
    detailsTransition.targetState = detailsExpanded
    val detailsRequester = remember { BringIntoViewRequester() }
    LaunchedEffect(detailsExpanded) {
        if (detailsExpanded) {
            withFrameNanos { }
            detailsRequester.bringIntoView()
            snapshotFlow { detailsTransition.isIdle }.first { it }
            detailsRequester.bringIntoView()
        }
    }

    // Claim focus on entry so the D-pad/stick immediately drive nav.
    LaunchedEffect(Unit) {
        runCatching { focusRequester.requestFocus() }
    }

    val wide = LocalConfiguration.current.let {
        it.orientation == Configuration.ORIENTATION_LANDSCAPE || it.screenWidthDp >= 600
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(horizontal = 20.dp, vertical = 12.dp)
            .verticalScroll(rememberScrollState())
            .focusRequester(focusRequester)
            .focusable()
            .onPreviewKeyEvent { event ->
                if (event.type != KeyEventType.KeyDown) return@onPreviewKeyEvent false
                when (event.key) {
                    Key.DirectionDown -> {
                        cursorMovedByUser = true
                        if (actions.isNotEmpty()) {
                            selectedActionIndex = (selectedActionIndex + 1)
                                .coerceAtMost(actions.size - 1)
                        }
                        true
                    }
                    Key.DirectionUp -> {
                        cursorMovedByUser = true
                        if (actions.isNotEmpty()) {
                            selectedActionIndex = (selectedActionIndex - 1)
                                .coerceAtLeast(0)
                        }
                        true
                    }
                    Key.ButtonA, Key.Enter -> {
                        actions.getOrNull(selectedActionIndex)
                            ?.takeIf { it.enabled }?.onClick?.invoke()
                        true
                    }
                    // Y: technical details (Y = details, as on the lists).
                    Key.ButtonY -> {
                        if (event.nativeKeyEvent.repeatCount == 0) detailsExpanded = !detailsExpanded
                        true
                    }
                    Key.ButtonB, Key.Escape, Key.Back -> {
                        onNavigateBack()
                        true
                    }
                    else -> false
                }
            },
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        // Server conditions that change what the statuses below mean.
        StatusBanners(
            state = BannerState(
                serverUrlBlank = serverConfigured == false,
                serverOnline = LocalServerOnline.current,
            ),
            onOpenSettings = { onNavigateToTab(SETTINGS_TAB) },
        )

        // ── Hero: the game, its system and where it stands ─────────
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(
                text = entry.displayName,
                style = MaterialTheme.typography.headlineSmall,
                color = GsColors.Text,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                SystemChip(entry.systemName)
                SyncStatusBadge(headerStatus)
                if (entry.systemName == "RETRO") {
                    Text("Unknown — set below:", style = MaterialTheme.typography.labelSmall, color = gsTextMuted())
                }
            }
            if (entry.systemName == "RETRO") {
                SystemPicker(onSystemSelected = { viewModel.setSaveSystem(entry, it) })
            }
        }

        // ── Conflict: both copies changed; the user chooses ─────────
        if (showConflictCard) {
            val info = conflictInfos[titleId]
            val serverFound = serverMeta as? ServerMetaState.Found
            ConflictCard(
                wide = wide,
                local = ConflictSide(
                    title = "This device",
                    time = formatWhen(modifiedAt),
                    size = formatSize(localSize),
                    console = "This device",
                    newer = newer < 0,
                ),
                server = ConflictSide(
                    title = "Server",
                    time = formatWhen(serverTime),
                    size = formatSize(serverFound?.sizeBytes ?: info?.server_size ?: 0L),
                    console = when {
                        info?.same_console == true -> "This device (an earlier upload)"
                        serverFound != null -> serverFound.source
                        info?.server_console_id != null -> "Device ${info.server_console_id.takeLast(4)}"
                        else -> "Unknown device"
                    },
                    newer = newer > 0,
                ),
            )
        }

        // ── Facts: this device, then the server ─────────────────────
        FactsGroup(
            title = "This device",
            facts = listOf(
                Fact("Modified", if (canUploadSave) formatWhen(modifiedAt) else "No save yet", newer = newer < 0),
                Fact("Size", if (canUploadSave) formatSize(localSize) else "—"),
                Fact("Last synced", formatWhen(syncState?.lastSyncedAt ?: 0L)),
            ),
        )
        when (val meta = serverMeta) {
            is ServerMetaState.Found -> FactsGroup(
                title = "Server",
                facts = listOf(
                    Fact("Saved at", formatWhen(meta.timestamp), newer = newer > 0),
                    Fact("From", meta.source),
                    Fact(
                        "Size",
                        formatSize(meta.sizeBytes) + (meta.fileCount?.let { n ->
                            " · " + if (n == 1) "1 file" else "$n files"
                        } ?: ""),
                    ),
                ),
            )
            is ServerMetaState.NotFound -> FactsGroup("Server", note = "No save on the server yet")
            is ServerMetaState.Error -> FactsGroup("Server", note = "Couldn't reach the server: ${meta.message}", noteIsError = true)
            is ServerMetaState.Loading -> FactsGroup("Server", note = "Checking the server…")
            is ServerMetaState.Idle -> if (serverConfigured == false) FactsGroup("Server", note = "No server set")
        }

        // ── Gamepad-navigable actions ───────────────────────────────
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            actions.forEachIndexed { index, action ->
                action.headerAbove?.let { header ->
                    Text(
                        header,
                        style = MaterialTheme.typography.labelMedium,
                        color = gsTextMuted(),
                        modifier = Modifier.padding(start = 10.dp, top = 4.dp),
                    )
                }
                DetailActionRow(
                    action = action,
                    isSelected = index == selectedActionIndex,
                    onClick = {
                        cursorMovedByUser = true
                        selectedActionIndex = index
                        action.onClick()
                    },
                    modifier = Modifier.bringIntoViewRequester(bringRequesters[index]),
                )
            }

            // ROM download result (not a gamepad-selectable item).
            when (val s = romDownloadState) {
                is MainViewModel.RomDownloadState.Success ->
                    Text(
                        "Saved to ${s.file.absolutePath}",
                        style = MaterialTheme.typography.bodySmall,
                        color = GsColors.OkText,
                    )
                is MainViewModel.RomDownloadState.Error ->
                    Text(
                        s.message,
                        style = MaterialTheme.typography.bodySmall,
                        color = GsColors.ErrText,
                    )
                else -> {}
            }
            if (entry.isServerOnly && !canDownloadSave) {
                Text(
                    text = "Download the ROM first, then rescan to create a local save target for this server-only save.",
                    style = MaterialTheme.typography.bodySmall,
                    color = gsTextMuted(),
                )
            }
        }

        // ── Technical details (collapsed) ───────────────────────────
        Column(modifier = Modifier.bringIntoViewRequester(detailsRequester)) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = 48.dp)
                    .focusProperties { canFocus = false }
                    .clickable(role = Role.Button) { detailsExpanded = !detailsExpanded },
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Text(
                    "Technical details",
                    style = MaterialTheme.typography.titleSmall,
                    color = gsTextMuted(),
                    modifier = Modifier.weight(1f),
                )
                if (LocalShowButtonHints.current) ButtonGlyph(GsButton.Y)
                Icon(
                    if (detailsExpanded) Icons.Filled.ExpandLess else Icons.Filled.ExpandMore,
                    contentDescription = if (detailsExpanded) "Hide details" else "Show details",
                    tint = gsTextMuted(),
                )
            }
            val reduce = LocalReduceMotion.current
            AnimatedVisibility(
                visibleState = detailsTransition,
                enter = gsExpandIn(reduce),
                exit = gsShrinkOut(reduce),
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    InfoRow("Title ID", entry.titleId)
                    InfoRow("File", entry.saveDir?.name ?: entry.saveFile?.name ?: "—")
                    entry.canonicalName?.let { InfoRow("Canonical name", it) }
                    InfoRow("Local hash", localHash?.take(16)?.let { "$it…" } ?: "—")
                    InfoRow(
                        "Server hash",
                        (serverMeta as? ServerMetaState.Found)?.hash?.takeIf { it != "—" }?.take(16)?.let { "$it…" } ?: "—",
                    )
                }
            }
        }
    }

    // ── Confirms ────────────────────────────────────────────────────
    val serverFound = serverMeta as? ServerMetaState.Found
    GsDialogHost(confirmDownload.takeIf { it }) { _, _ ->
        GsConfirmDialog(
            title = "Replace this device's save?",
            message = forceDownloadMessage(
                localWhen = if (canUploadSave) formatWhen(modifiedAt) else null,
                localSize = formatSize(localSize),
                serverWhen = serverFound?.let { formatWhen(it.timestamp) },
                serverSize = serverFound?.let { formatSize(it.sizeBytes) },
                serverFrom = serverFound?.source,
                retroArch = viewModel.isRetroArchEntry(entry),
            ),
            confirmLabel = "Replace",
            dismissLabel = "Keep mine",
            destructive = true,
            onConfirm = {
                confirmDownload = false
                viewModel.downloadSave(entry)
            },
            onDismiss = { confirmDownload = false },
        )
    }
    GsDialogHost(confirmUpload.takeIf { it }) { _, _ ->
        GsConfirmDialog(
            title = "Replace the server's save?",
            message = "The server keeps the previous version in its history.",
            confirmLabel = "Upload",
            onConfirm = {
                confirmUpload = false
                viewModel.uploadSave(entry)
            },
            onDismiss = { confirmUpload = false },
        )
    }
    GsDialogHost(confirmRestore) { backup, _ ->
        val shared = entry.systemName == "SAT" && entry.saveFile?.name.equals("backup.bin", ignoreCase = true)
        GsConfirmDialog(
            title = "Put back the copy from ${formatWhen(backup.takenAt)}?",
            message = "The save on this device now is kept as a copy too, so this can be undone." +
                if (shared) " This file holds other Saturn games' saves as well; they go back to that copy too." else "",
            confirmLabel = "Restore",
            dismissLabel = "Keep current",
            destructive = true,
            onConfirm = {
                confirmRestore = null
                viewModel.restoreLatestBackup(entry.titleId)
            },
            onDismiss = { confirmRestore = null },
        )
    }
}

/** D-pad positions of the override rows (the action order never changes). */
private const val FORCE_UPLOAD_INDEX = 1
private const val FORCE_DOWNLOAD_INDEX = 2

/**
 * The Force Download confirm: both sides' time and size, that a copy of the
 * current save is kept, and — for RetroArch — the save-state caveat.
 */
internal fun forceDownloadMessage(
    localWhen: String?,
    localSize: String,
    serverWhen: String?,
    serverSize: String?,
    serverFrom: String?,
    retroArch: Boolean,
): String = buildString {
    if (localWhen != null) append("This device: $localWhen, $localSize. ")
    if (serverWhen != null) {
        append("Server: $serverWhen")
        serverSize?.let { append(", $it") }
        serverFrom?.let { append(", from $it") }
        append(". ")
    }
    append("A copy of the current save is kept on this device.")
    if (retroArch) append("\nClose RetroArch first — an auto-loaded save state can undo this.")
}

/** How a detail action is drawn: one filled primary, then lighter tiers. */
private enum class ActionStyle { Primary, Outlined, Tonal, Tertiary }

/**
 * One gamepad-selectable action on the detail screen.  [headerAbove] is a
 * small non-selectable label before it ("Override", "Available ROMs").
 */
private data class DetailAction(
    val label: String,
    val icon: ImageVector,
    val enabled: Boolean,
    val style: ActionStyle,
    val isBusy: Boolean,
    val iconTint: Color? = null,
    val headerAbove: String? = null,
    val onClick: () -> Unit,
)

/**
 * A detail action.  In Pad mode the selected one is drawn like a selected
 * GsListRow: a 4dp Accent bar beside it, a PanelHi lift and a 1dp Accent
 * border (Smart Sync, already Accent, gets a Text border instead).  The bar's
 * gutter is always reserved, so selection never shifts anything.  Pressing
 * scales it slightly, in both input modes.
 */
@Composable
private fun DetailActionRow(
    action: DetailAction,
    isSelected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val selected = isSelected && LocalInputMode.current == GsInputMode.Pad
    val source = remember { MutableInteractionSource() }
    val pressed by source.collectIsPressedAsState()
    val scale = rememberPressScale(pressed, GS_BUTTON_PRESS_SCALE)
    val lifted = selected || pressed
    val high = LocalHighContrast.current
    val selectedBorder = if (high) GsColors.Accent else GsColors.Accent.copy(alpha = 0.55f)
    val shape = GsShapes.Card
    val (background, content) = when (action.style) {
        ActionStyle.Primary -> GsColors.Accent to GsColors.Ink
        ActionStyle.Outlined -> (if (lifted) GsColors.PanelHi else GsColors.Panel) to GsColors.Text
        ActionStyle.Tonal -> GsColors.PanelHi to GsColors.Text
        ActionStyle.Tertiary -> (if (lifted) GsColors.PanelHi else Color.Transparent) to gsTextMuted()
    }
    val border: BorderStroke? = when {
        action.style == ActionStyle.Primary -> if (selected) BorderStroke(2.dp, GsColors.Text) else null
        selected -> BorderStroke(1.dp, selectedBorder)
        action.style == ActionStyle.Outlined -> BorderStroke(1.dp, GsColors.Line)
        else -> null
    }
    Row(modifier = modifier.fillMaxWidth().height(IntrinsicSize.Min)) {
        Box(
            Modifier
                .width(4.dp)
                .fillMaxHeight()
                .clip(RoundedCornerShape(2.dp))
                .background(if (selected) GsColors.Accent else Color.Transparent)
        )
        Spacer(Modifier.width(6.dp))
        Row(
            modifier = Modifier
                .weight(1f)
                .heightIn(min = if (action.style == ActionStyle.Primary) 52.dp else 48.dp)
                .graphicsLayer {
                    val s = scale.value
                    scaleX = s
                    scaleY = s
                }
                .clip(shape)
                .background(background)
                .then(if (border != null) Modifier.border(border, shape) else Modifier)
                .focusProperties { canFocus = false }
                .clickable(
                    interactionSource = source,
                    indication = rememberRipple(),
                    enabled = action.enabled,
                    role = Role.Button,
                    onClick = onClick,
                )
                .alpha(if (action.enabled) 1f else 0.38f)
                .padding(horizontal = 16.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (action.isBusy) {
                CircularProgressIndicator(
                    modifier = Modifier.size(18.dp),
                    strokeWidth = 2.dp,
                    color = content,
                )
            } else {
                Icon(action.icon, contentDescription = null, tint = action.iconTint ?: content, modifier = Modifier.size(18.dp))
            }
            Spacer(Modifier.width(10.dp))
            Text(
                action.label,
                color = content,
                style = MaterialTheme.typography.labelLarge,
                fontWeight = if (action.style == ActionStyle.Primary) FontWeight.SemiBold else FontWeight.Medium,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

/** One labelled fact; [newer] adds the Info "Newer" pill beside the value. */
private data class Fact(val label: String, val value: String, val newer: Boolean = false)

/**
 * A titled group of facts in a two-column grid (label above value), or a
 * single [note] when there is nothing to list.  Groups are separated by
 * spacing, not dividers.
 */
@Composable
private fun FactsGroup(
    title: String,
    facts: List<Fact> = emptyList(),
    note: String? = null,
    noteIsError: Boolean = false,
) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        SectionHeader(title)
        if (note != null) {
            Text(
                note,
                style = MaterialTheme.typography.bodyMedium,
                color = if (noteIsError) GsColors.ErrText else gsTextMuted(),
            )
        }
        facts.chunked(2).forEach { pair ->
            Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                pair.forEach { fact -> FactCell(fact, Modifier.weight(1f)) }
                if (pair.size == 1) Spacer(Modifier.weight(1f))
            }
        }
    }
}

@Composable
private fun FactCell(fact: Fact, modifier: Modifier = Modifier) {
    Column(modifier = modifier, verticalArrangement = Arrangement.spacedBy(2.dp)) {
        Text(fact.label, style = MaterialTheme.typography.labelMedium, color = gsTextMuted())
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(
                fact.value,
                style = MaterialTheme.typography.bodyMedium,
                color = GsColors.Text,
                modifier = Modifier.weight(1f, fill = false),
            )
            if (fact.newer) GsPill(text = "Newer", color = GsColors.Info)
        }
    }
}

/**
 * A detail section title with its 16dp icon: a phone for "This device", a
 * cloud for "Server" (TextMuted, decorative — the title says it).
 */
@Composable
private fun SectionHeader(title: String, style: androidx.compose.ui.text.TextStyle = MaterialTheme.typography.titleSmall) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        sectionIcon(title)?.let { icon ->
            Icon(icon, contentDescription = null, tint = gsTextMuted(), modifier = Modifier.size(16.dp))
            Spacer(Modifier.width(6.dp))
        }
        Text(title, style = style, color = gsTextMuted())
    }
}

private fun sectionIcon(title: String): ImageVector? = when (title) {
    "This device" -> Icons.Filled.Smartphone
    "Server" -> Icons.Filled.Cloud
    else -> null
}

/** One side of a conflict, as its panel shows it. */
private data class ConflictSide(
    val title: String,
    val time: String,
    val size: String,
    val console: String,
    val newer: Boolean,
)

/**
 * The conflict decision: a banner saying both copies changed, then the two
 * copies side by side (landscape / wide) or stacked (portrait).  The choice
 * itself is the Force Upload / Force Download actions below.
 */
@Composable
private fun ConflictCard(wide: Boolean, local: ConflictSide, server: ConflictSide) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        GsBanner(
            text = "Both copies changed since the last sync — choose which to keep",
            color = GsColors.Err,
        )
        if (wide) {
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.height(IntrinsicSize.Min)) {
                ConflictPanel(local, Modifier.weight(1f).fillMaxHeight())
                ConflictPanel(server, Modifier.weight(1f).fillMaxHeight())
            }
        } else {
            ConflictPanel(local, Modifier.fillMaxWidth())
            ConflictPanel(server, Modifier.fillMaxWidth())
        }
    }
}

@Composable
private fun ConflictPanel(side: ConflictSide, modifier: Modifier = Modifier) {
    Column(
        modifier = modifier
            .clip(GsShapes.Card)
            .background(GsColors.Panel)
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            sectionIcon(side.title)?.let { icon ->
                Icon(icon, contentDescription = null, tint = gsTextMuted(), modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(6.dp))
            }
            Text(side.title, style = MaterialTheme.typography.titleSmall, color = GsColors.Text)
        }
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(side.time, style = MaterialTheme.typography.bodyMedium, color = GsColors.Text, modifier = Modifier.weight(1f, fill = false))
            if (side.newer) GsPill(text = "Newer", color = GsColors.Info)
        }
        Text(side.size, style = MaterialTheme.typography.bodySmall, color = gsTextMuted())
        Text(side.console, style = MaterialTheme.typography.bodySmall, color = gsTextMuted())
    }
}

private val knownSystems = listOf(
    "GBA", "SNES", "NES", "GB", "GBC", "N64",
    "PS1", "PS2", "PSP", "SAT", "DC",
    "MD", "SEGACD", "GC", "WII",
    "PCE", "PCECD", "NGP", "WSWAN", "WSWANC", "ARCADE", "NEOCD",
    "NDS", "A2600", "LYNX", "MAME"
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun SystemPicker(onSystemSelected: (String) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    var selected by remember { mutableStateOf("Select system…") }

    ExposedDropdownMenuBox(
        expanded = expanded,
        onExpandedChange = { expanded = it }
    ) {
        OutlinedTextField(
            value = selected,
            onValueChange = {},
            readOnly = true,
            label = { Text("Set System") },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded) },
            modifier = Modifier.fillMaxWidth().menuAnchor()
        )
        ExposedDropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false }
        ) {
            knownSystems.forEach { system ->
                DropdownMenuItem(
                    text = { Text(system) },
                    onClick = {
                        selected = system
                        expanded = false
                        onSystemSelected(system)
                    }
                )
            }
        }
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelMedium,
            color = gsTextMuted(),
        )
        Text(
            text = value,
            style = MaterialTheme.typography.bodySmall,
            color = GsColors.Text,
            modifier = Modifier.weight(1f),
            textAlign = TextAlign.End,
        )
    }
}

private val SaveDetailState.isSync get() = this is SaveDetailState.Working && action == "sync"
private val SaveDetailState.isUpload get() = this is SaveDetailState.Working && action == "upload"
private val SaveDetailState.isDownload get() = this is SaveDetailState.Working && action == "download"
private val SaveDetailState.isNormalize get() = this is SaveDetailState.Working && action == "normalize"
private val SaveDetailState.isRestore get() = this is SaveDetailState.Working && action == "restore"

private fun formatSize(bytes: Long): String = when {
    bytes < 1024 -> "$bytes B"
    bytes < 1024 * 1024 -> "${"%.1f".format(bytes / 1024.0)} KB"
    else -> "${"%.2f".format(bytes / (1024.0 * 1024))} MB"
}

// ── Pickers ────────────────────────────────────────────────────────────────

/** Where a picker's cursor starts: the first current/selected option, else the first. */
internal fun <T> pickerCursorStart(options: List<T>, isCurrent: (T) -> Boolean): Int =
    options.indexOfFirst(isCurrent).coerceAtLeast(0)

/** One Up/Down step of a picker cursor, held at the ends (no wrap). */
internal fun movePickerCursor(cursor: Int, dy: Int, size: Int): Int =
    if (size <= 0) 0 else (cursor + dy).coerceIn(0, size - 1)

/**
 * "Choose canonical name": one choice from the server's candidates.  The
 * cursor starts on the current name (else the recommended first entry),
 * Up/Down move it, A applies it, B cancels.  A tap moves the cursor and
 * doesn't apply, so touch and pad pick the same way.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun NormalizePickerDialog(
    state: NormalizePickerState.Visible,
    onApply: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    val options = state.options
    var cursor by remember(state) {
        mutableIntStateOf(pickerCursorStart(options) { it == state.entry.displayName })
    }
    val requesters = remember(state) { List(options.size) { BringIntoViewRequester() } }
    GsDialog(
        title = "Choose canonical name",
        message = "Select the correct version. USA releases are listed first; demos and protos last.",
        onDismiss = onDismiss,
        onNavigate = { dy -> cursor = movePickerCursor(cursor, dy, options.size) },
        choices = listOf(
            GsDialogChoice(GsButton.B, "Cancel", onDismiss),
            GsDialogChoice(
                GsButton.A,
                "Apply",
                { options.getOrNull(cursor)?.let(onApply) },
                color = GsColors.Accent,
                textColor = GsColors.Ink,
            ),
        ),
        body = {
            // Inside the dialog's composition, where the rows are laid out.
            LaunchedEffect(cursor) { requesters.getOrNull(cursor)?.bringIntoView() }
            options.forEachIndexed { index, name ->
                val chosen = index == cursor
                GsListRow(
                    isSelected = chosen,
                    onClick = { cursor = index },
                    clickLabel = "Choose",
                    stateDescription = if (chosen) "Chosen" else null,
                    modifier = Modifier
                        .padding(vertical = 2.dp)
                        .bringIntoViewRequester(requesters[index]),
                ) {
                    // The chosen value shows in both input modes; the row
                    // highlight is the pad cursor.
                    Icon(
                        if (chosen) Icons.Filled.RadioButtonChecked else Icons.Filled.RadioButtonUnchecked,
                        contentDescription = null,
                        tint = if (chosen) GsColors.Accent else gsTextMuted(),
                        modifier = Modifier.size(20.dp),
                    )
                    Column(modifier = Modifier.weight(1f)) {
                        Text(name, style = MaterialTheme.typography.bodyMedium, color = GsColors.Text)
                        when {
                            index == 0 -> Text(
                                "Recommended",
                                style = MaterialTheme.typography.labelSmall,
                                color = GsColors.Accent2,
                            )
                            name == state.entry.displayName -> Text(
                                "Current name",
                                style = MaterialTheme.typography.labelSmall,
                                color = gsTextMuted(),
                            )
                        }
                    }
                }
            }
        },
    )
}

/**
 * "Choose Saturn save archives": several choices.  Up/Down move the
 * cursor, X includes or leaves out the archive under it (X = secondary,
 * dialog-internal; A never toggles because A = "Use selection"), B
 * cancels.  A tap toggles that row and moves the cursor there.  The cursor
 * starts on the first archive already selected.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun SaturnArchivePickerDialog(
    state: SaturnArchivePickerState.Visible,
    onApply: (Set<String>) -> Unit,
    onDismiss: () -> Unit,
) {
    val options = state.options
    var selected by remember(state) {
        mutableStateOf<Set<String>>(options.filter { it.preselected }.mapTo(linkedSetOf()) { it.archiveFamily })
    }
    var cursor by remember(state) {
        mutableIntStateOf(pickerCursorStart(options) { it.preselected })
    }
    val requesters = remember(state) { List(options.size) { BringIntoViewRequester() } }
    fun toggle(index: Int) {
        val family = options.getOrNull(index)?.archiveFamily ?: return
        selected = if (family in selected) selected - family else selected + family
    }
    val cursorChecked = options.getOrNull(cursor)?.archiveFamily in selected
    val pad = LocalInputMode.current == GsInputMode.Pad
    GsDialog(
        title = "Choose Saturn save archives",
        message = "Pick the archive names in YabaSanshiro's backup memory that belong to this game.",
        onDismiss = onDismiss,
        onNavigate = { dy -> cursor = movePickerCursor(cursor, dy, options.size) },
        choices = buildList {
            add(GsDialogChoice(GsButton.B, "Cancel", onDismiss))
            // The pad toggle; touch toggles by tapping the row instead.
            if (pad && options.isNotEmpty()) {
                add(GsDialogChoice(GsButton.X, if (cursorChecked) "Leave out" else "Include", { toggle(cursor) }))
            }
            add(
                GsDialogChoice(
                    GsButton.A,
                    "Use selection",
                    { onApply(selected) },
                    color = GsColors.Accent,
                    textColor = GsColors.Ink,
                )
            )
        },
        body = {
            LaunchedEffect(cursor) { requesters.getOrNull(cursor)?.bringIntoView() }
            if (state.hiddenSelectedArchives.isNotEmpty()) {
                Text(
                    text = "Auto-selected: ${state.hiddenSelectedArchives.joinToString(", ")}",
                    style = MaterialTheme.typography.labelSmall,
                    color = GsColors.Accent2,
                    modifier = Modifier.padding(bottom = 8.dp)
                )
            }
            options.forEachIndexed { index, option ->
                val checked = option.archiveFamily in selected
                GsListRow(
                    isSelected = index == cursor,
                    onClick = {
                        cursor = index
                        toggle(index)
                    },
                    clickLabel = if (checked) "Leave out" else "Include",
                    stateDescription = if (checked) "Included" else "Left out",
                    modifier = Modifier
                        .padding(vertical = 2.dp)
                        .bringIntoViewRequester(requesters[index]),
                ) {
                    Icon(
                        if (checked) Icons.Filled.CheckBox else Icons.Filled.CheckBoxOutlineBlank,
                        contentDescription = null,
                        tint = if (checked) GsColors.Accent else gsTextMuted(),
                        modifier = Modifier.size(20.dp),
                    )
                    Column(modifier = Modifier.weight(1f)) {
                        Text(option.archiveFamily, style = MaterialTheme.typography.bodyMedium, color = GsColors.Text)
                        Text(
                            option.archiveNames.joinToString(", "),
                            style = MaterialTheme.typography.labelSmall,
                            color = GsColors.Accent2,
                        )
                        Text(
                            option.detail,
                            style = MaterialTheme.typography.labelSmall,
                            color = gsTextMuted(),
                        )
                    }
                }
            }
        },
    )
}
