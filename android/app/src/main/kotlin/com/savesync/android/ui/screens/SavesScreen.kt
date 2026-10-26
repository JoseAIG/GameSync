package com.savesync.android.ui.screens

import android.content.Intent
import android.net.Uri
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.VisibilityThreshold
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.ui.unit.IntOffset
import com.savesync.android.ui.theme.GsMotion
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.LinearProgressIndicator
import com.savesync.android.ui.components.PullThresholdHaptic
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material.icons.filled.FilterList
import androidx.compose.material.icons.filled.Language
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Sync
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.google.accompanist.swiperefresh.SwipeRefresh
import com.google.accompanist.swiperefresh.rememberSwipeRefreshState
import com.savesync.android.MainActivity
import com.savesync.android.emulators.SaveEntry
import com.savesync.android.findComponentActivity
import com.savesync.android.storage.SyncStateEntity
import com.savesync.android.ui.MainViewModel
import com.savesync.android.ui.SaveSyncStatus
import com.savesync.android.ui.SyncState
import com.savesync.android.ui.SaveDetailState
import com.savesync.android.sync.SyncResult
import com.savesync.android.ui.SaveRow
import com.savesync.android.ui.resolveSelection
import com.savesync.android.ui.syncAllCopy
import com.savesync.android.ui.syncAllTargets
import com.savesync.android.ui.components.ButtonGlyph
import com.savesync.android.ui.components.GsButton
import com.savesync.android.ui.components.GsConfirmDialog
import com.savesync.android.ui.components.GsActionSlot
import com.savesync.android.ui.components.BannerState
import com.savesync.android.ui.components.GsChromeSpec
import com.savesync.android.ui.components.GsScreenChrome
import com.savesync.android.ui.components.GsSubTabRow
import com.savesync.android.ui.components.GsHint
import com.savesync.android.ui.components.GsHitTarget
import com.savesync.android.ui.components.GsListRow
import com.savesync.android.ui.components.GsPill
import com.savesync.android.ui.components.LocalShowButtonHints
import com.savesync.android.ui.components.SystemFilterChip
import com.savesync.android.ui.theme.GsColors
import com.savesync.android.ui.theme.LocalReduceMotion
import com.savesync.android.ui.theme.gsExpandIn
import com.savesync.android.ui.theme.gsShrinkOut
import androidx.compose.runtime.withFrameNanos
import com.savesync.android.ui.components.rememberListFollower
import com.savesync.android.ui.theme.gsTextMuted
import com.savesync.android.ui.theme.textVariantOf
import com.savesync.android.ui.components.GsHaptic
import com.savesync.android.ui.components.GsNotice
import com.savesync.android.ui.components.GsNoticeKind
import com.savesync.android.ui.components.LocalGsHaptics
import com.savesync.android.ui.components.LocalGsNotifier
import com.savesync.android.ui.components.isServerNotConfigured
import com.savesync.android.ui.components.noticeKindForSaveMessage
import com.savesync.android.ui.components.stripLeadingGlyph
import com.savesync.android.ui.components.firstLetter
import com.savesync.android.ui.components.handleHorizontalHoldKeyEvent
import com.savesync.android.ui.components.rememberHoldNavState
import com.savesync.android.ui.components.LocalInputTracker
import com.savesync.android.ui.components.isRevealGuarded
import kotlinx.coroutines.launch
import java.text.SimpleDateFormat
import java.net.URI
import java.util.Date
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SavesScreen(
    viewModel: MainViewModel,
    syncStateEntities: List<SyncStateEntity>,
    onNavigateToDetail: (String) -> Unit = {},
    onNavigateToTab: (Int) -> Unit = {},
    /** The emulator save-folder screen (missing-folder banner and notice). */
    onNavigateToEmulators: () -> Unit = {},
) {
    val context = LocalContext.current
    // Rows arrive with what is known at once, then with hashed statuses.
    val rows by viewModel.saveRows.collectAsState()
    val saves = remember(rows) { rows.map { it.entry } }
    val checking by viewModel.savesChecking.collectAsState()
    // A save scan is running (pull-to-refresh, Settings → Rescan, startup).
    val scanning by viewModel.savesScanning.collectAsState()
    // Titles waiting on a conflict decision (banner, tab dot, filter).
    val conflictIds by viewModel.conflictTitleIds.collectAsState()
    // Save-folder overrides that point nowhere (computed off the main thread).
    val overrideWarnings by viewModel.overrideWarnings.collectAsState()

    /** Banner tap / notice "Review": show the saves waiting on a conflict. */
    fun reviewConflicts() {
        onNavigateToTab(SAVES_TAB)
        viewModel.setStatusFilter(SaveSyncStatus.CONFLICT)
    }
    val settings by viewModel.settings.collectAsState()
    val syncState by viewModel.syncState.collectAsState()
    val selectedFilter by viewModel.selectedFilter.collectAsState()
    val availableFilters by viewModel.availableFilters.collectAsState()
    val searchQuery by viewModel.searchQuery.collectAsState()
    val statusFilter by viewModel.statusFilter.collectAsState()
    val availableStatusFilters by viewModel.availableStatusFilters.collectAsState()
    // Shared with SaveDetailScreen: the outcome of an A-button smart sync.
    val saveDetailState by viewModel.saveDetailState.collectAsState()
    // App-wide notices (MainApp): they outlive this screen and replace,
    // rather than queue behind, whatever is showing.
    val notifier = LocalGsNotifier.current
    val haptics = LocalGsHaptics.current
    // Title id of the save A just smart-synced, so a conflict it reports
    // can open that save's detail screen (where Force Upload / Download live).
    var pendingSyncTitle by remember { mutableStateOf<String?>(null) }

    // Saveable, so rotation keeps the search open and the cursor in place.
    var searchVisible by rememberSaveable { mutableStateOf(false) }
    // The saves the open Sync-all dialog will sync, captured when it opened:
    // its label and its action read this one list.
    var syncAllConfirm by remember { mutableStateOf<List<SaveEntry>?>(null) }

    // ── Manual D-pad selection state ─────────────────────────────────────
    // We track the selected index ourselves and scroll the list manually so
    // the gamepad cursor doesn't leak to the toolbar actions above it.
    // The cursor follows the game, not the index: rows can arrive or
    // reorder as statuses resolve, and A must act on the game highlighted.
    var selectedTitleId by rememberSaveable { mutableStateOf<String?>(null) }
    var lastIndex by rememberSaveable { mutableIntStateOf(0) }
    val listState = rememberLazyListState()
    val follower = rememberListFollower(listState)
    val coroutineScope = rememberCoroutineScope()
    val reduceMotion = LocalReduceMotion.current
    // Hold-to-accelerate state for d-pad left/right: page scroll → faster
    // page scroll → alphabet jump.  See HoldNav.kt.
    val holdNav = rememberHoldNavState()
    val inputTracker = LocalInputTracker.current
    val searchFocusRequester = remember { FocusRequester() }
    // Parent container steals focus on entry so the search TextField below
    // doesn't auto-focus and pop the keyboard when this tab opens.
    val listFocusRequester = remember { FocusRequester() }

    val webLibraryUrl = remember(settings.serverUrl) { buildWebLibraryUrl(settings.serverUrl) }

    /** The highlighted row's index: its game's row, else the old index clamped. */
    fun currentIndex(): Int =
        resolveSelection(saves.map { it.titleId }, selectedTitleId, lastIndex).coerceAtLeast(0)
    fun select(index: Int) {
        val entry = saves.getOrNull(index) ?: return
        lastIndex = index
        selectedTitleId = entry.titleId
    }
    // A new list (status emission, filter, scan): keep the cursor on its
    // game, or on the row now at its old index when the game left the list.
    LaunchedEffect(saves) {
        val i = resolveSelection(saves.map { it.titleId }, selectedTitleId, lastIndex)
        if (i >= 0) select(i)
    }

    // Focus the search field as soon as it opens, without waiting for the
    // expand: one frame lets AnimatedVisibility compose the field, so the
    // requester is attached.
    LaunchedEffect(searchVisible) {
        if (searchVisible) {
            withFrameNanos { }
            runCatching { searchFocusRequester.requestFocus() }
        }
    }

    // On entry, claim focus for the list container so the search TextField
    // doesn't auto-focus. Matches the Steam Deck "land on the game list, not
    // the search" behaviour.
    LaunchedEffect(Unit) {
        runCatching { listFocusRequester.requestFocus() }
    }

    // SELECT steps the system filter (the sub-tab chip) — the Activity emits
    // on systemCycleEvents and we apply it here so the chip stays in sync.
    val activity = context.findComponentActivity() as? MainActivity
    LaunchedEffect(activity, availableFilters, selectedFilter) {
        activity?.systemCycleEvents?.collect { delta ->
            if (availableFilters.isEmpty()) return@collect
            val idx = availableFilters.indexOf(selectedFilter).let { if (it < 0) 0 else it }
            val next = (idx + delta + availableFilters.size) % availableFilters.size
            viewModel.setFilter(availableFilters[next])
        }
    }
    // L2 / R2 step the sync-status filter (All → each status present → All).
    LaunchedEffect(activity, availableStatusFilters, statusFilter) {
        activity?.statusCycleEvents?.collect { delta ->
            val all = listOf<SaveSyncStatus?>(null) + availableStatusFilters
            val idx = all.indexOf(statusFilter).let { if (it < 0) 0 else it }
            val next = (idx + delta + all.size) % all.size
            viewModel.setStatusFilter(all[next])
        }
    }

    // Outcome of an A-button smart sync.  A conflict can only be settled on
    // the detail screen (Force Upload / Force Download), so go there.
    LaunchedEffect(saveDetailState) {
        when (val s = saveDetailState) {
            is SaveDetailState.Success -> {
                val conflictTitle = pendingSyncTitle
                pendingSyncTitle = null
                viewModel.resetDetailState()
                if (conflictTitle != null && s.message.contains("Conflict")) {
                    // Conflict found: reject haptic on the same frame as the
                    // navigation that shows it.
                    haptics?.perform(GsHaptic.Reject)
                    onNavigateToDetail(conflictTitle)
                } else {
                    val kind = noticeKindForSaveMessage(s.message)
                    notifier.show(
                        GsNotice(kind, stripLeadingGlyph(s.message)),
                        hapticOverride = if (kind == GsNoticeKind.Warning) GsHaptic.Reject else null,
                    )
                }
            }
            is SaveDetailState.Error -> {
                pendingSyncTitle = null
                viewModel.resetDetailState()
                notifier.show(errorNotice(s.message) { onNavigateToTab(SETTINGS_TAB) })
            }
            else -> Unit
        }
    }

    // Show sync result in snackbar
    LaunchedEffect(syncState) {
        when (val state = syncState) {
            is SyncState.Success -> {
                val r = state.result
                notifier.show(
                    syncAllNotice(
                        result = r,
                        overrideWarnings = state.overrideWarnings,
                        onReview = { reviewConflicts() },
                        onFix = onNavigateToEmulators,
                    ),
                    // A conflict is a Warning that still rejects.
                    hapticOverride = if (r.errors.isEmpty() && r.conflicts.isNotEmpty()) GsHaptic.Reject else null,
                )
                viewModel.resetSyncState()
            }
            is SyncState.Error -> {
                notifier.show(errorNotice(state.message) { onNavigateToTab(SETTINGS_TAB) })
                viewModel.resetSyncState()
            }
            else -> Unit
        }
    }

    val isSyncing = syncState is SyncState.Syncing

    val isSyncingOne = saveDetailState is SaveDetailState.Working

    /**
     * A: smart sync the highlighted save — upload or download per its
     * status.  Saturn saves need the archive picker on the detail screen,
     * so they open it instead; a conflict found by the sync does too.
     */
    fun smartSyncSelected() {
        val entry = saves.getOrNull(currentIndex()) ?: return
        if (isSyncing || isSyncingOne) return
        if (entry.systemName == "SAT") {
            onNavigateToDetail(entry.titleId)
            return
        }
        pendingSyncTitle = entry.titleId
        viewModel.syncSave(entry)
    }

    /**
     * X / the header sync button: confirm a sync of exactly the local saves
     * this list shows.  With none, say so instead of opening an empty dialog.
     */
    // The header button's lambda is remembered; it reads the current list.
    val currentSaves by rememberUpdatedState(saves)
    val currentlySyncing by rememberUpdatedState(isSyncing)
    fun requestSyncAll() {
        if (currentlySyncing) return
        val targets = syncAllTargets(currentSaves)
        if (targets.isEmpty()) {
            notifier.show(GsNotice(GsNoticeKind.Info, "Nothing to sync in this view"))
        } else {
            syncAllConfirm = targets
        }
    }

    // Header actions and footer hints, drawn by MainApp's persistent chrome.
    // Remembered on what they show, so a D-pad step never touches the header.
    val busy = isSyncing || isSyncingOne
    val saveCount = saves.size
    val chromeSpec = remember(saveCount, busy, searchVisible, webLibraryUrl) {
        GsChromeSpec(
            actions = {
                if (saveCount > 0) {
                    Text(
                        text = "$saveCount",
                        style = MaterialTheme.typography.labelMedium,
                        color = GsColors.Dim,
                        modifier = Modifier.padding(end = 4.dp)
                    )
                }
                // Sync all (X button); spins for a single A sync too, in
                // the same fixed slot so nothing shifts under the finger.
                GsActionSlot(busy = busy) {
                    IconButton(onClick = { requestSyncAll() }) {
                        Icon(Icons.Default.Sync, contentDescription = "Sync all (X)", tint = GsColors.Text)
                    }
                }
                // Search (touch; Y opens details on a controller)
                IconButton(onClick = {
                    searchVisible = !searchVisible
                    if (!searchVisible) viewModel.setSearchQuery("")
                }) {
                    Icon(Icons.Default.Search, contentDescription = "Search", tint = GsColors.Text)
                }
                // Web ROM library (touch only)
                IconButton(
                    onClick = {
                        webLibraryUrl?.let { url ->
                            context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)))
                        }
                    },
                    enabled = webLibraryUrl != null
                ) {
                    Icon(Icons.Default.Language, contentDescription = "Web ROM Library", tint = GsColors.Text)
                }
            },
            hints = if (searchVisible) listOf(
                GsHint(GsButton.A, "Sync"),
                GsHint(GsButton.B, "Close search"),
                GsHint(GsButton.X, "Sync all"),
                GsHint(GsButton.Y, "Details"),
            ) else listOf(
                GsHint(GsButton.A, "Sync"),
                GsHint(GsButton.X, "Sync all"),
                GsHint(GsButton.Y, "Details"),
                GsHint(GsButton.SELECT, "System"),
                GsHint(GsButton.L2R2, "Status"),
                GsHint(GsButton.L1, "Tabs"),
                GsHint(GsButton.START, "Exit"),
            ),
        )
    }
    GsScreenChrome("saves", chromeSpec)

    Column(modifier = Modifier.fillMaxSize()) {
        // Sub-tab chips move with their list (SELECT / L2-R2 step them).
        GsSubTabRow {
            SystemFilterChip(
                label = selectedFilter,
                options = availableFilters,
                onSelect = { viewModel.setFilter(it) },
            )
            if (availableStatusFilters.isNotEmpty() || statusFilter != null) {
                StatusFilterChip(
                    statusFilter = statusFilter,
                    options = availableStatusFilters,
                    onSelect = { viewModel.setStatusFilter(it) },
                )
            }
        }
        // ── Master gamepad handler ───────────────────────────────────────
        // D-pad up/down steps the list, left/right page-scrolls with hold
        // acceleration (then letter jumps).  A smart-syncs the row, X syncs
        // everything, Y opens details, B only closes search.  L1/R1 (tabs),
        // L2/R2 (status filter), SELECT (system) and START (exit) are
        // handled at the Activity level and never reach here.
        // Ongoing conditions, within the banner budget.
        StatusBanners(
            state = BannerState(
                conflictCount = conflictIds.size,
                missingFolderCount = overrideWarnings.size,
            ),
            folderText = missingFolderBannerText(overrideWarnings.keys.toList()),
            onReviewConflicts = { reviewConflicts() },
            // Touch only; a pad reaches it through Settings → Emulator configuration.
            onFixFolders = onNavigateToEmulators,
        )
        // Reserved 2dp under the chips: a scan behind a list shows a thin
        // bar here, without pushing the rows down.
        Box(Modifier.fillMaxWidth().height(2.dp)) {
            if (scanning && rows.isNotEmpty()) {
                LinearProgressIndicator(
                    modifier = Modifier.fillMaxSize(),
                    color = GsColors.Accent,
                    trackColor = Color.Transparent,
                )
            }
        }
        // The pull indicator tracks the scan only (the header slot shows a
        // sync).  Pulling rescans the emulator folders — which also asks the
        // server for its server-only saves — and re-checks the server.
        val swipeState = rememberSwipeRefreshState(isRefreshing = scanning)
        PullThresholdHaptic(swipeState)
        SwipeRefresh(
            state = swipeState,
            onRefresh = {
                viewModel.scanSaves()
                viewModel.pingServer()
            },
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .focusRequester(listFocusRequester)
                .focusable()
                .onPreviewKeyEvent { event ->
                    if (holdNav.handleHorizontalHoldKeyEvent(
                            event,
                            coroutineScope,
                            onPage = { d ->
                                if (saves.isNotEmpty()) {
                                    val page = listState.layoutInfo.visibleItemsInfo.size
                                        .coerceAtLeast(1)
                                    val to = (currentIndex() + d * page)
                                        .coerceIn(0, saves.size - 1)
                                    select(to)
                                    follower.follow(to, reduceMotion)
                                }
                            },
                            onAlphabet = { d ->
                                if (saves.isNotEmpty()) {
                                    val cur = currentIndex().coerceIn(0, saves.size - 1)
                                    val curLetter = firstLetter(saves[cur].displayName)
                                    val step = if (d > 0) 1 else -1
                                    var row = cur + step
                                    var found = -1
                                    while (row in saves.indices) {
                                        val ltr = firstLetter(saves[row].displayName)
                                        if (ltr.isNotEmpty() && ltr != curLetter) {
                                            found = row; break
                                        }
                                        row += step
                                    }
                                    val to = if (found >= 0) found
                                        else if (d > 0) saves.size - 1 else 0
                                    select(to)
                                    follower.follow(to, reduceMotion)
                                }
                            },
                        )
                    ) return@onPreviewKeyEvent true
                    if (event.type != KeyEventType.KeyDown) return@onPreviewKeyEvent false
                    // First key after touch on a device without a gamepad: it only
                    // reveals the cursor, so nothing acts on a row that wasn't lit.
                    if (inputTracker?.isRevealPress == true && isRevealGuarded(event.key)) {
                        return@onPreviewKeyEvent true
                    }
                    when (event.key) {
                        // D-pad / analog stick — vertical: scroll list
                        // Re-anchors first if a fling left the cursor
                        // off-screen; otherwise steps and the list follows.
                        Key.DirectionDown -> {
                            select(follower.step(
                                currentIndex(), 1, saves.size,
                                reveal = inputTracker?.isRevealPress == true,
                                reduce = reduceMotion,
                            ))
                            true
                        }
                        Key.DirectionUp -> {
                            select(follower.step(
                                currentIndex(), -1, saves.size,
                                reveal = inputTracker?.isRevealPress == true,
                                reduce = reduceMotion,
                            ))
                            true
                        }
                        // A → smart sync the selected save.  A held A's
                        // repeats are ignored, so one press is one action.
                        Key.ButtonA -> {
                            if (event.nativeKeyEvent.repeatCount == 0) smartSyncSelected()
                            true
                        }
                        // Enter (keyboard) keeps opening the detail screen
                        Key.Enter -> {
                            saves.getOrNull(currentIndex())?.let { onNavigateToDetail(it.titleId) }
                            true
                        }
                        // B / Escape → close search if open; otherwise
                        // unclaimed (the Activity swallows it on a tab).
                        Key.ButtonB, Key.Escape, Key.Back -> {
                            if (searchVisible) {
                                searchVisible = false
                                viewModel.setSearchQuery("")
                                runCatching { listFocusRequester.requestFocus() }
                                true
                            } else false
                        }
                        // Y → details of the selected save
                        Key.ButtonY -> {
                            saves.getOrNull(currentIndex())?.let { onNavigateToDetail(it.titleId) }
                            true
                        }
                        // X → sync all (with confirmation)
                        Key.ButtonX -> {
                            requestSyncAll()
                            true
                        }
                        else -> false
                    }
                }
        ) {
            Column(modifier = Modifier.fillMaxSize()) {
                // Grows down from the header and shrinks back into it.
                AnimatedVisibility(
                    visible = searchVisible,
                    enter = gsExpandIn(reduceMotion),
                    exit = gsShrinkOut(reduceMotion),
                ) {
                    SearchBar(
                        query = searchQuery,
                        onQueryChange = { viewModel.setSearchQuery(it) },
                        onDismiss = {
                            searchVisible = false
                            viewModel.setSearchQuery("")
                        },
                        modifier = Modifier.focusRequester(searchFocusRequester)
                    )
                }

                // A status filter hides rows until their status is known, so
                // they appear as they resolve instead of vanishing later.
                if (checking > 0) {
                    Text(
                        text = "Checking $checking " + (if (checking == 1) "save" else "saves") + "…",
                        style = MaterialTheme.typography.bodySmall,
                        color = gsTextMuted(),
                        modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
                    )
                }
                if (rows.isEmpty() && scanning) {
                    // Never "No saves found" while the folders are being read.
                    CenterLoader(text = "Scanning emulator folders…")
                } else if (rows.isEmpty()) {
                    if (checking == 0) {
                        EmptyState(
                            activeFilter = selectedFilter,
                            statusFilter = statusFilter,
                            searchQuery = searchQuery
                        )
                    }
                } else {
                    SavesList(
                        rows = rows,
                        syncStateEntities = syncStateEntities,
                        listState = listState,
                        selectedTitleId = { selectedTitleId },
                        onSaveClick = onNavigateToDetail,
                        onSelectIndex = { select(it) },
                        modifier = Modifier.weight(1f)
                    )
                }
            }
        }
    }

    // ── Sync confirmation dialog ─────────────────────────────────────────
    syncAllConfirm?.let { targets ->
        val copy = syncAllCopy(
            count = targets.size,
            filtersApplied = searchQuery.isNotBlank() || statusFilter != null,
        )
        GsConfirmDialog(
            title = copy.title,
            message = copy.message,
            confirmLabel = "Sync",
            onConfirm = {
                syncAllConfirm = null
                viewModel.syncNow(targets)
            },
            onDismiss = { syncAllConfirm = null },
        )
    }
}

@Composable
private fun SearchBar(
    query: String,
    onQueryChange: (String) -> Unit,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier
) {
    OutlinedTextField(
        value = query,
        onValueChange = onQueryChange,
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp, vertical = 4.dp)
            .onPreviewKeyEvent { event ->
                if (event.type != KeyEventType.KeyDown) return@onPreviewKeyEvent false
                when (event.key) {
                    Key.Escape, Key.ButtonB, Key.Back -> {
                        onDismiss()
                        true
                    }
                    else -> false
                }
            },
        placeholder = { Text("Search games… (Esc to close)") },
        leadingIcon = {
            Icon(
                Icons.Default.Search,
                contentDescription = "Search",
                tint = MaterialTheme.colorScheme.onSurfaceVariant
            )
        },
        trailingIcon = {
            IconButton(onClick = {
                if (query.isNotEmpty()) onQueryChange("") else onDismiss()
            }) {
                Icon(
                    Icons.Default.Clear,
                    contentDescription = if (query.isNotEmpty()) "Clear search" else "Close search",
                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        },
        singleLine = true,
        colors = OutlinedTextFieldDefaults.colors(
            focusedContainerColor = MaterialTheme.colorScheme.surface,
            unfocusedContainerColor = MaterialTheme.colorScheme.surface
        ),
        shape = MaterialTheme.shapes.medium
    )
}

@Composable
private fun EmptyState(
    activeFilter: String,
    statusFilter: SaveSyncStatus?,
    searchQuery: String
) {
    // Scrollable, so pull-to-refresh works on an empty list too; the inner
    // column is at least a viewport tall so the message stays centred.
    BoxWithConstraints(modifier = Modifier.fillMaxSize()) {
        val viewport = maxHeight
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .heightIn(min = viewport),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            val hasAnyFilter = activeFilter != "All" || statusFilter != null || searchQuery.isNotBlank()
            if (hasAnyFilter) {
                Text(
                    text = "No matching saves",
                    style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(Modifier.height(8.dp))
                Text(
                    text = "Try adjusting your filters or search query.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            } else {
                Text(
                    text = "No saves found",
                    style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(Modifier.height(8.dp))
                Text(
                    text = "Install an emulator and create some save files,\nthen pull down to refresh.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun SavesList(
    rows: List<SaveRow>,
    syncStateEntities: List<SyncStateEntity>,
    listState: androidx.compose.foundation.lazy.LazyListState,
    /**
     * The highlighted game, read lazily so a D-pad step recomposes only the
     * two rows that change.  Keyed by title id: the cursor follows the game.
     */
    selectedTitleId: () -> String?,
    onSaveClick: (String) -> Unit,
    onSelectIndex: (Int) -> Unit,
    modifier: Modifier = Modifier
) {
    // Built once per sync-state change, not once per row per frame.
    val syncMap = remember(syncStateEntities) { syncStateEntities.associateBy { it.titleId } }
    val timeFormat = remember { SimpleDateFormat("MMM d, HH:mm", Locale.getDefault()) }
    val reduce = LocalReduceMotion.current

    LazyColumn(
        modifier = modifier.fillMaxWidth(),
        state = listState,
        verticalArrangement = Arrangement.spacedBy(4.dp),
        contentPadding = PaddingValues(horizontal = 12.dp, vertical = 8.dp)
    ) {
        itemsIndexed(rows, key = { _, row -> row.entry.titleId }) { index, row ->
            val entry = row.entry
            val isSelected by remember(entry.titleId) {
                derivedStateOf { selectedTitleId() == entry.titleId }
            }
            SaveCard(
                entry = entry,
                syncState = syncMap[entry.titleId],
                syncStatus = row.status,
                isSelected = isSelected,
                timeFormat = timeFormat,
                onClick = {
                    onSelectIndex(index)
                    onSaveClick(entry.titleId)
                },
                // Rows glide to their new place when the list changes.
                modifier = if (reduce) Modifier
                    else Modifier.animateItemPlacement(GsMotion.standard(IntOffset.VisibilityThreshold)),
            )
        }
    }
}

/**
 * Compact single-row card: game name on the left, system + sync status on the right.
 * [isSelected] draws a highlight border for D-pad/gamepad cursor visibility.
 */
@Composable
private fun SaveCard(
    entry: SaveEntry,
    syncState: SyncStateEntity?,
    /** null = still checking. */
    syncStatus: SaveSyncStatus?,
    isSelected: Boolean,
    timeFormat: SimpleDateFormat,
    onClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    GsListRow(isSelected = isSelected, onClick = onClick, modifier = modifier) {
        // Left: game name + optional canonical subtitle
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = entry.displayName,
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.SemiBold,
                color = GsColors.Text,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
            entry.canonicalName?.let { canonical ->
                Text(
                    text = canonical,
                    style = MaterialTheme.typography.labelSmall,
                    color = gsTextMuted(),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }

        // Right: system, last-synced time, status pill
        Row(
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            SystemBadge(entry.systemName)
            if (!entry.isServerOnly) {
                syncState?.lastSyncedAt?.let { ts ->
                    Text(
                        text = timeFormat.format(Date(ts)),
                        style = MaterialTheme.typography.labelSmall,
                        color = gsTextMuted(),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
            SyncStatusBadge(syncStatus)
        }
    }
}

/**
 * Sub-tab chip for the sync-status filter (L2/R2 step it; tap for the list).
 */
@Composable
private fun StatusFilterChip(
    statusFilter: SaveSyncStatus?,
    options: List<SaveSyncStatus>,
    onSelect: (SaveSyncStatus?) -> Unit,
) {
    var expanded by remember { mutableStateOf(false) }
    Box {
        GsHitTarget(onClick = { expanded = true }) { indication ->
            Row(
                modifier = Modifier
                    .clip(RoundedCornerShape(16.dp))
                    .background(GsColors.Panel)
                    .then(indication)
                    .padding(horizontal = 10.dp, vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                if (LocalShowButtonHints.current) ButtonGlyph(GsButton.L2R2)
                Icon(
                    Icons.Default.FilterList,
                    contentDescription = "Status filter",
                    tint = if (statusFilter != null) statusChipColor(statusFilter) else GsColors.Dim,
                    modifier = Modifier.size(16.dp),
                )
                Text(
                    text = statusFilter?.label ?: "All status",
                    style = MaterialTheme.typography.labelLarge,
                    color = if (statusFilter != null) textVariantOf(statusChipColor(statusFilter)) else GsColors.Dim,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
        DropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false }
        ) {
            DropdownMenuItem(
                text = {
                    Text(
                        text = "All status",
                        fontWeight = if (statusFilter == null) FontWeight.Bold else FontWeight.Normal
                    )
                },
                onClick = {
                    onSelect(null)
                    expanded = false
                },
                leadingIcon = if (statusFilter == null) {
                    { Text("✓", fontWeight = FontWeight.Bold) }
                } else null
            )
            options.forEach { status ->
                DropdownMenuItem(
                    text = {
                        Text(
                            text = "${statusIcon(status)} ${status.label}",
                            fontWeight = if (status == statusFilter) FontWeight.Bold else FontWeight.Normal,
                            color = statusChipColor(status)
                        )
                    },
                    onClick = {
                        onSelect(if (status == statusFilter) null else status)
                        expanded = false
                    },
                    leadingIcon = if (status == statusFilter) {
                        { Text("✓", fontWeight = FontWeight.Bold) }
                    } else null
                )
            }
        }
    }
}

// ── Non-focusable badge composables ──────────────────────────────────────────

/** A status pill; null renders the neutral "Checking…" pill (no icon). */
@Composable
fun SyncStatusBadge(status: SaveSyncStatus?) {
    if (status == null) {
        GsPill(text = "Checking…", color = GsColors.Muted, textColor = gsTextMuted())
    } else {
        GsPill(text = "${statusIcon(status)} ${status.label}", color = statusChipColor(status))
    }
}

/** System code tag: a quiet outlined chip, so the status pill stays the loud one. */
@Composable
fun SystemBadge(systemName: String) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(6.dp))
            .background(GsColors.Bg2)
            .border(1.dp, GsColors.Line, RoundedCornerShape(6.dp))
            .padding(horizontal = 6.dp, vertical = 2.dp)
    ) {
        Text(
            text = systemName,
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.Bold,
            color = GsColors.Accent2
        )
    }
}

@Composable
fun SystemChip(systemName: String) = SystemBadge(systemName)

/**
 * RetroAchievements badge in RA gold, so it reads as a reward rather than
 * as another sync status.  A title-only match is outlined ("RA?").
 */
@Composable
fun RaBadge(titleOnly: Boolean = false) {
    if (titleOnly) {
        Box(
            modifier = Modifier
                .clip(RoundedCornerShape(10.dp))
                .border(1.dp, GsColors.Ra, RoundedCornerShape(10.dp))
                .padding(horizontal = 7.dp, vertical = 2.dp)
        ) {
            Text("RA?", style = MaterialTheme.typography.labelSmall,
                fontWeight = FontWeight.Bold, color = GsColors.Ra)
        }
    } else {
        GsPill(text = "RA", color = GsColors.Ra, filled = true)
    }
}

// ── Helper functions ─────────────────────────────────────────────────────────

private fun statusIcon(status: SaveSyncStatus): String = when (status) {
    SaveSyncStatus.SYNCED -> "✓"
    SaveSyncStatus.LOCAL_ONLY -> "●"
    SaveSyncStatus.SERVER_ONLY -> "☁"
    SaveSyncStatus.LOCAL_NEWER -> "↑"
    SaveSyncStatus.SERVER_NEWER -> "↓"
    SaveSyncStatus.CONFLICT -> "⚠"
    SaveSyncStatus.UNKNOWN -> "?"
}

/** Status pill colours, shared with every console client. */
fun statusChipColor(status: SaveSyncStatus): Color = when (status) {
    SaveSyncStatus.SYNCED -> GsColors.Ok
    SaveSyncStatus.LOCAL_ONLY -> GsColors.Info
    SaveSyncStatus.SERVER_ONLY -> GsColors.Warn
    SaveSyncStatus.LOCAL_NEWER -> GsColors.Info
    SaveSyncStatus.SERVER_NEWER -> GsColors.Info
    SaveSyncStatus.CONFLICT -> GsColors.Err
    SaveSyncStatus.UNKNOWN -> GsColors.Dim
}

fun systemChipColor(systemName: String): Color {
    return when (systemName.uppercase()) {
        "GBA"             -> Color(0xFF6A1B9A)
        "GBC"             -> Color(0xFF8E24AA)
        "GB"              -> Color(0xFF546E7A)
        "NDS", "DS"       -> Color(0xFF1565C0)
        "3DS"             -> Color(0xFF0277BD)
        "NES", "FC"       -> Color(0xFFB71C1C)
        "SNES", "SFC"     -> Color(0xFFE65100)
        "N64"             -> Color(0xFF558B2F)
        "GC"              -> Color(0xFF7B1FA2)
        "WII"             -> Color(0xFF00838F)
        "WIIU"            -> Color(0xFF006064)
        "PS1", "PSX"      -> Color(0xFF1A237E)
        "PS2"             -> Color(0xFF0D47A1)
        "PSP", "PPSSPP"   -> Color(0xFF01579B)
        "GEN", "MD"       -> Color(0xFF37474F)
        "SMS"             -> Color(0xFF455A64)
        "SG1000"          -> Color(0xFF1E88E5)
        "GG"              -> Color(0xFF4CAF50)
        "SEGACD"          -> Color(0xFF263238)
        "SAT"             -> Color(0xFF4E342E)
        "DC"              -> Color(0xFFF57C00)
        "ARCADE", "FBA",
        "MAME"            -> Color(0xFFC62828)
        "NEOCD", "NGP"    -> Color(0xFFAD1457)
        "PCE", "TG16"     -> Color(0xFF00695C)
        "PCECD", "TGCD"   -> Color(0xFF00838F)
        "PCSG"            -> Color(0xFF689F38)
        "PCFX"            -> Color(0xFF827717)
        "WSWAN", "WSWANC" -> Color(0xFF2E7D32)
        "LYNX"            -> Color(0xFF4527A0)
        "A2600", "A7800"  -> Color(0xFF6D4C41)
        "RETRO"           -> Color(0xFF37474F)
        else              -> Color(0xFF546E7A)
    }
}

/** The Settings tab's index in the tab strip (MainActivity.TAB_ROUTES). */
internal const val SETTINGS_TAB = 4
/** The Saves tab's index. */
internal const val SAVES_TAB = 0

/**
 * An Error notice for a view-model error string.  "Server URL not
 * configured" offers a Settings action, since that is where it is fixed.
 */
internal fun errorNotice(message: String, openSettings: () -> Unit): GsNotice =
    if (isServerNotConfigured(message)) {
        GsNotice(GsNoticeKind.Error, message, actionLabel = "Settings", onAction = openSettings)
    } else {
        GsNotice(GsNoticeKind.Error, message)
    }

/**
 * The Sync-all result notice.  Errors win and keep the first error's text
 * (the fork's Long snackbar); conflicts are a Warning with "Review"; a
 * missing emulator folder keeps the result a Warning that names the first
 * one, with "Fix" → the emulator screen.  Folder warnings are a condition,
 * not an error: they never count as errors.
 */
internal fun syncAllNotice(
    result: SyncResult,
    overrideWarnings: List<String>,
    onReview: () -> Unit,
    onFix: () -> Unit,
): GsNotice {
    val base = syncAllMessage(result.uploaded, result.downloaded, result.conflicts.size, result.errors)
    if (result.errors.isNotEmpty()) return GsNotice(GsNoticeKind.Error, base)
    val message = overrideWarnings.firstOrNull()?.let { "$base\n$it" } ?: base
    return when {
        result.conflicts.isNotEmpty() ->
            GsNotice(GsNoticeKind.Warning, message, actionLabel = "Review", onAction = onReview)
        overrideWarnings.isNotEmpty() ->
            GsNotice(GsNoticeKind.Warning, message, actionLabel = "Fix", onAction = onFix)
        else -> GsNotice(GsNoticeKind.Success, base)
    }
}

/** "Sync done: 3 uploaded, 2 downloaded, 1 conflict" + the first error on its own line. */
internal fun syncAllMessage(uploaded: Int, downloaded: Int, conflicts: Int, errors: List<String>): String =
    buildString {
        append("Sync done: $uploaded uploaded, $downloaded downloaded")
        if (conflicts > 0) append(", $conflicts conflict" + if (conflicts == 1) "" else "s")
        if (errors.isNotEmpty()) {
            append(", ${errors.size} error" + if (errors.size == 1) "" else "s")
            append("\n").append(errors.first())
        }
    }

private fun buildWebLibraryUrl(serverUrl: String): String? {
    val trimmed = serverUrl.trim()
    if (trimmed.isBlank()) return null

    return runCatching {
        val apiUri = URI(trimmed)
        val scheme = apiUri.scheme ?: return null
        val host = apiUri.host ?: return null
        val port = when (apiUri.port) {
            8000 -> 80
            -1 -> -1
            else -> apiUri.port
        }
        URI(scheme, null, host, port, "/", null, null).toString()
    }.getOrNull()
}
