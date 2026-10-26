package com.savesync.android.ui.screens

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.snapshotFlow
import kotlinx.coroutines.flow.drop
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.platform.LocalContext
import com.savesync.android.MainActivity
import com.savesync.android.api.RomEntry
import com.savesync.android.api.hasRa
import com.savesync.android.api.raIsTitleOnly
import com.savesync.android.api.preferredDownloadExtractFormat
import com.savesync.android.api.msuPackKind
import com.savesync.android.sync.MsuPack
import com.savesync.android.api.preferredDownloadFilename
import com.savesync.android.api.withRelated
import com.savesync.android.catalog.RomCatalogFilter
import com.savesync.android.findComponentActivity
import com.savesync.android.ui.MainViewModel
import com.savesync.android.ui.components.ButtonGlyph
import com.savesync.android.ui.components.GsListScrollEdges
import com.savesync.android.ui.components.gsEdgeBump
import com.savesync.android.ui.components.AlphabetHud
import com.savesync.android.ui.components.GsDialogHost
import com.savesync.android.ui.components.GsBanner
import com.savesync.android.ui.components.GsButton
import com.savesync.android.ui.components.GsDialog
import com.savesync.android.ui.components.GsDialogChoice
import com.savesync.android.ui.components.GsActionSlot
import com.savesync.android.ui.components.GsChromeSpec
import com.savesync.android.ui.components.GsScreenChrome
import com.savesync.android.ui.components.GsSubTabRow
import com.savesync.android.ui.components.GsHint
import com.savesync.android.ui.components.GsListRow
import com.savesync.android.ui.components.LocalShowButtonHints
import com.savesync.android.ui.components.SystemFilterChip
import com.savesync.android.ui.theme.GsColors
import com.savesync.android.ui.theme.LocalReduceMotion
import com.savesync.android.ui.theme.gsExpandIn
import com.savesync.android.ui.theme.gsShrinkOut
import androidx.compose.runtime.withFrameNanos
import com.savesync.android.ui.components.rememberListFollower
import com.savesync.android.ui.theme.gsDim
import com.savesync.android.ui.theme.gsTextMuted
import com.savesync.android.ui.components.GsNotice
import com.savesync.android.ui.components.GsNoticeKind
import com.savesync.android.ui.components.LocalGsNotifier
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.runtime.derivedStateOf
import androidx.compose.ui.text.style.TextOverflow
import com.savesync.android.ui.components.firstLetter
import com.savesync.android.ui.components.handleHorizontalHoldKeyEvent
import com.savesync.android.ui.components.rememberHoldNavState
import com.savesync.android.ui.components.LocalInputTracker
import com.savesync.android.ui.components.isRevealGuarded
import kotlinx.coroutines.launch
import androidx.compose.runtime.produceState

/**
 * Label the [SystemFilterChip] shows when no specific system is
 * selected. The screen stores `null` internally (matching the API's
 * "show everything" semantics); this sentinel is only used at the
 * UI boundary.
 */
private const val ALL_SYSTEMS_LABEL = "All Systems"

/**
 * Browse the server's entire ROM catalog with a smart tokenised search
 * (roman numerals, region-tag stripping) and a per-system filter.
 *
 * Mirrors the Steam Deck ROM Catalog tab UX: tap a row → confirm →
 * stream the ROM into the right ``<romScanDir>/<System>/`` folder via
 * the existing [MainViewModel.downloadRom] pipeline.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RomCatalogScreen(
    viewModel: MainViewModel,
    onNavigateToTab: (Int) -> Unit = {},
) {
    val catalog by viewModel.romCatalog.collectAsState()
    val loading by viewModel.romCatalogLoading.collectAsState()
    val loaded by viewModel.romCatalogLoaded.collectAsState()
    val error by viewModel.romCatalogError.collectAsState()
    // "Offline — cached catalog" when the server could not be reached.
    val notice by viewModel.romCatalogNotice.collectAsState()
    val downloadState by viewModel.romDownloadState.collectAsState()

    val notifier = LocalGsNotifier.current
    val scope = rememberCoroutineScope()
    val reduceMotion = LocalReduceMotion.current

    // Filter state uses rememberSaveable so the user's place in the catalog
    // (selected system + search query + whether the search bar is open)
    // survives tab switching.  NavHost preserves the destination's
    // SavedState bundle across tab pops with saveState=true /
    // restoreState=true, and rememberSaveable rides on top of that — so
    // queueing a download then bouncing between Catalog ↔ Downloads
    // doesn't reset the filter back to the default system.
    var query by rememberSaveable { mutableStateOf("") }
    var systemFilter by rememberSaveable { mutableStateOf<String?>(null) }
    var searchVisible by rememberSaveable { mutableStateOf(false) }
    // X (Square on a PlayStation pad) / the RA chip: only games with a
    // RetroAchievements set.  Same toggle as the MiSTer client's catalog.
    var raOnly by rememberSaveable { mutableStateOf(false) }
    // confirmTarget intentionally stays as plain remember — re-showing the
    // download confirmation dialog after the user navigated away is creepy
    // (they'd come back to a modal they didn't open).
    var confirmTarget by remember { mutableStateOf<RomEntry?>(null) }

    // ── Gamepad navigation state ────────────────────────────────────────
    // We drive selection ourselves so D-pad / analog stick scrolls the
    // rom grid without focus leaking up to the search field.
    var selectedIndex by remember { mutableIntStateOf(0) }
    val listState = rememberLazyListState()
    val follower = rememberListFollower(listState)
    // Hold-to-accelerate state for d-pad left/right (page → alphabet jump).
    val holdNav = rememberHoldNavState()
    val inputTracker = LocalInputTracker.current
    val listFocusRequester = remember { FocusRequester() }
    val searchFocusRequester = remember { FocusRequester() }

    // ── Per-system scroll memory ────────────────────────────────────────
    // The same LazyListState is reused as the user cycles through systems
    // (because the LazyColumn always renders the actively-filtered list),
    // so without this map a system-filter change would either keep the
    // user at the previous system's scroll offset (now pointing at a
    // totally unrelated game) or — depending on filtered.size — clamp them
    // back to the top.  Map: system code (or "ALL" for no filter) →
    // firstVisibleItemIndex when the user last left that system.  We
    // store just the index (no offset) — close enough that the user
    // recognises their place; saving offset too would mostly add noise.
    //
    // listSaver flattens the map to a [k, v, k, v, ...] list which the
    // SavedState bundle handles natively, so this state survives both
    // tab switching (via NavHost saveState=true) and configuration
    // changes (rotation, theme switch).
    val scrollPositions: MutableMap<String, Int> = rememberSaveable(
        saver = listSaver(
            save = { map -> map.flatMap { listOf(it.key, it.value) } },
            restore = { entries ->
                mutableMapOf<String, Int>().apply {
                    var i = 0
                    while (i + 1 < entries.size) {
                        val k = entries[i] as? String ?: ""
                        val v = entries[i + 1] as? Int ?: 0
                        if (k.isNotEmpty()) put(k, v)
                        i += 2
                    }
                }
            }
        )
    ) { mutableMapOf() }
    val systems = remember(catalog, raOnly) { RomCatalogFilter.uniqueSystems(catalog, raOnly) }
    val activeSystem = systemFilter?.takeIf { it in systems }
    // Stable map key for the active filter — null filter == "ALL".  The RA
    // view keeps its own scroll positions: its rows are a different list.
    val systemKey = (activeSystem ?: "ALL") + if (raOnly) "|RA" else ""

    // Lazy first-load when the tab is opened; coming back to the tab while
    // the load failed tries again by itself.
    LaunchedEffect(Unit) {
        if (!loading && (!loaded || error != null)) viewModel.fetchRomCatalog()
    }
    val settings by viewModel.settings.collectAsState()
    val serverConfigured by viewModel.serverConfigured.collectAsState()

    // Claim focus for the rom list on entry so the search OutlinedTextField
    // doesn't auto-focus and pop the keyboard. Search is only focused when
    // the user explicitly presses Y / taps the icon.
    LaunchedEffect(Unit) {
        runCatching { listFocusRequester.requestFocus() }
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

    // Surface enqueue outcomes as notices.  The Downloads tab owns
    // progress + final-status display; these only confirm that the request
    // was accepted (or rejected before it left the device).
    LaunchedEffect(downloadState) {
        when (val s = downloadState) {
            is MainViewModel.RomDownloadState.Downloading -> {
                // The one confirmation; ongoing status is the Downloads tab
                // badge.  "View" is a touch shortcut (R1 reaches the tab).
                notifier.show(
                    GsNotice(
                        GsNoticeKind.Success,
                        "Queued ${s.name}",
                        actionLabel = "View",
                        onAction = { onNavigateToTab(DOWNLOADS_TAB) },
                    )
                )
                viewModel.resetRomDownloadState()
            }
            is MainViewModel.RomDownloadState.Success -> {
                // Legacy state — preserved for any caller that still uses
                // the synchronous path (e.g. unit tests).  The DownloadManager
                // never emits this directly.
                notifier.show(GsNotice(GsNoticeKind.Success, "Downloaded ${s.file.name}"))
                viewModel.resetRomDownloadState()
            }
            is MainViewModel.RomDownloadState.Error -> {
                val retry = s.retry
                notifier.show(
                    when {
                        retry != null -> GsNotice(
                            GsNoticeKind.Error,
                            "Download failed: ${s.message}",
                            actionLabel = "Retry",
                            onAction = retry,
                        )
                        // "Server URL / ROM directory not configured": fixed in Settings.
                        s.message.contains("not configured", ignoreCase = true) -> GsNotice(
                            GsNoticeKind.Error,
                            s.message,
                            actionLabel = "Settings",
                            onAction = { onNavigateToTab(SETTINGS_TAB) },
                        )
                        else -> GsNotice(GsNoticeKind.Error, "Download failed: ${s.message}")
                    }
                )
                viewModel.resetRomDownloadState()
            }
            else -> Unit
        }
    }

    val filtered = remember(catalog, query, activeSystem, raOnly) {
        RomCatalogFilter.filter(catalog, query, activeSystem, raOnly)
    }

    fun toggleRaOnly() {
        raOnly = !raOnly
        val message = if (raOnly) {
            val count = RomCatalogFilter.filter(catalog, "", null, raOnly = true).size
            "RetroAchievements games only: $count"
        } else {
            "Showing all games"
        }
        // A filter echo, not an outcome: no haptic.
        notifier.show(GsNotice(GsNoticeKind.Info, message), haptic = false)
    }

    // Keep the cursor in range when filters/search change the list size.
    LaunchedEffect(filtered.size) {
        if (filtered.isNotEmpty()) {
            selectedIndex = selectedIndex.coerceIn(0, filtered.size - 1)
        } else {
            selectedIndex = 0
        }
    }

    // Continuously snapshot the active system's scroll position into the
    // per-system map, so flipping back to a previously-visited system
    // restores where you were instead of dumping you at the top.
    //
    // ``drop(1)`` is critical: snapshotFlow always emits the current value
    // immediately on subscription, but when ``systemKey`` changes the
    // restore effect below fires concurrently and we don't want this saver
    // to overwrite the new system's saved position with the previous
    // (stale) listState value before the restore lands.  Skipping the
    // initial emission means we only persist values that came from real
    // scroll events, not from coroutine restart.
    LaunchedEffect(listState, systemKey) {
        snapshotFlow { listState.firstVisibleItemIndex }
            .drop(1)
            .collect { idx -> scrollPositions[systemKey] = idx }
    }

    // Restore saved scroll position when the user switches systems.  We
    // gate on ``lastRestoredKey`` so subsequent filter updates within
    // the same system (e.g. typing in the search box) don't re-scroll
    // the list — only an actual system change triggers a restore.
    var lastRestoredKey by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(systemKey, filtered.size) {
        if (lastRestoredKey == systemKey) return@LaunchedEffect
        if (filtered.isEmpty()) return@LaunchedEffect  // wait for items
        val saved = scrollPositions[systemKey] ?: 0
        val target = saved.coerceIn(0, filtered.size - 1)
        listState.scrollToItem(target)
        // Place the gamepad cursor on the same item so D-pad navigation
        // resumes from the visible row instead of jumping back to item 0.
        selectedIndex = target
        lastRestoredKey = systemKey
    }

    // Helper that cycles the system filter (null = all systems), driven by
    // SELECT (activity-level systemCycleEvents flow).
    fun cycleSystem(delta: Int) {
        if (systems.isEmpty()) return
        val all = listOf<String?>(null) + systems
        val idx = all.indexOf(activeSystem).let { if (it < 0) 0 else it }
        val next = (idx + delta + all.size) % all.size
        systemFilter = all[next]
    }

    // SELECT cycles the system filter (the sub-tab chip) — the Activity
    // emits on systemCycleEvents and we apply it here so the chip stays in
    // sync.
    val activity = LocalContext.current.findComponentActivity() as? MainActivity
    LaunchedEffect(activity, systems, systemFilter) {
        activity?.systemCycleEvents?.collect { delta -> cycleSystem(delta) }
    }

    // Header actions and footer hints, drawn by MainApp's persistent chrome.
    val showSpinner = loading && catalog.isNotEmpty()
    val clearSearchHint = searchVisible || query.isNotEmpty()
    val chromeSpec = remember(showSpinner, raOnly, clearSearchHint) {
        GsChromeSpec(
            actions = {
                // A refresh behind a cached list spins here; the slot is
                // always reserved so the search button never moves.
                GsActionSlot(busy = showSpinner) {}
                IconButton(onClick = {
                    searchVisible = !searchVisible
                    if (!searchVisible) query = ""
                }) {
                    Icon(Icons.Filled.Search, contentDescription = "Search (Y)", tint = GsColors.Text)
                }
            },
            hints = listOf(
                GsHint(GsButton.A, "Install"),
                GsHint(GsButton.X, if (raOnly) "All games" else "RA only"),
                GsHint(GsButton.Y, "Search"),
            ) + (if (clearSearchHint) listOf(GsHint(GsButton.B, "Clear search")) else emptyList()) +
                listOf(
                    GsHint(GsButton.SELECT, "System"),
                    GsHint(GsButton.L1, "Tabs"),
                    GsHint(GsButton.START, "Exit"),
                ),
        )
    }
    GsScreenChrome("catalog", chromeSpec)

    Column(modifier = Modifier.fillMaxSize()) {
        // Sub-tab chips move with their list (SELECT / X step them).
        GsSubTabRow {
            SystemFilterChip(
                label = activeSystem ?: ALL_SYSTEMS_LABEL,
                options = listOf(ALL_SYSTEMS_LABEL) + systems,
                onSelect = { choice ->
                    systemFilter = choice.takeIf { it != ALL_SYSTEMS_LABEL }
                },
            )
            FilterChip(
                selected = raOnly,
                onClick = { toggleRaOnly() },
                label = { Text("RetroAchievements") },
                leadingIcon = if (LocalShowButtonHints.current) {
                    { ButtonGlyph(GsButton.X) }
                } else null,
                colors = FilterChipDefaults.filterChipColors(
                    containerColor = GsColors.Panel,
                    labelColor = GsColors.Dim,
                    selectedContainerColor = GsColors.Ra,
                    selectedLabelColor = GsColors.Ink,
                ),
            )
        }
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .focusRequester(listFocusRequester)
                .focusable()
                .onPreviewKeyEvent { event ->
                    if (holdNav.handleHorizontalHoldKeyEvent(
                            event,
                            scope,
                            onPage = { d ->
                                if (filtered.isNotEmpty()) {
                                    val page = listState.layoutInfo.visibleItemsInfo.size
                                        .coerceAtLeast(1)
                                    selectedIndex = (selectedIndex + d * page)
                                        .coerceIn(0, filtered.size - 1)
                                    follower.follow(selectedIndex, reduceMotion)
                                }
                            },
                            onAlphabet = { d ->
                                if (filtered.isNotEmpty()) {
                                    val cur = selectedIndex.coerceIn(0, filtered.size - 1)
                                    val curLetter = firstLetter(filtered[cur].name)
                                    val step = if (d > 0) 1 else -1
                                    var row = cur + step
                                    var found = -1
                                    while (row in filtered.indices) {
                                        val ltr = firstLetter(filtered[row].name)
                                        if (ltr.isNotEmpty() && ltr != curLetter) {
                                            found = row; break
                                        }
                                        row += step
                                    }
                                    selectedIndex = if (found >= 0) found
                                        else if (d > 0) filtered.size - 1 else 0
                                    holdNav.announceLetter(firstLetter(filtered[selectedIndex].name))
                                    follower.follow(selectedIndex, reduceMotion)
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
                        // Re-anchors first if a fling left the cursor
                        // off-screen; otherwise steps and the list follows.
                        Key.DirectionDown -> {
                            selectedIndex = follower.step(
                                selectedIndex, 1, filtered.size,
                                reveal = inputTracker?.isRevealPress == true,
                                reduce = reduceMotion,
                            )
                            true
                        }
                        Key.DirectionUp -> {
                            selectedIndex = follower.step(
                                selectedIndex, -1, filtered.size,
                                reveal = inputTracker?.isRevealPress == true,
                                reduce = reduceMotion,
                            )
                            true
                        }
                        // A / Enter → open the download-confirm dialog.  A
                        // held A's repeats are ignored, so it can't open the
                        // dialog and confirm it in one press.
                        Key.ButtonA, Key.Enter -> {
                            if (event.nativeKeyEvent.repeatCount == 0) {
                                filtered.getOrNull(selectedIndex)?.let { confirmTarget = it }
                            }
                            true
                        }
                        // X (Square) → RetroAchievements games only / all
                        Key.ButtonX -> {
                            toggleRaOnly()
                            true
                        }
                        // Y → toggle search
                        Key.ButtonY -> {
                            searchVisible = !searchVisible
                            if (!searchVisible) query = ""
                            true
                        }
                        // B / Escape / Back → clear + close the search.
                        // Never starts anything; unclaimed it's swallowed
                        // by the Activity (B never exits from a tab).
                        Key.ButtonB, Key.Escape, Key.Back -> {
                            when {
                                confirmTarget != null -> { confirmTarget = null; true }
                                searchVisible || query.isNotEmpty() -> {
                                    searchVisible = false
                                    query = ""
                                    runCatching { listFocusRequester.requestFocus() }
                                    true
                                }
                                else -> false
                            }
                        }
                        // L1/R1 (tabs), SELECT (system), START (exit) are
                        // intercepted at the Activity level — never reach
                        // here.  Refresh lives in Settings now.
                        else -> false
                    }
                }
        ) {
            // Search bar is only composed when explicitly opened so there's
            // nothing for Android's default focus system to grab on entry.
            // Grows down from the header and shrinks back into it.
            AnimatedVisibility(
                visible = searchVisible,
                enter = gsExpandIn(reduceMotion),
                exit = gsShrinkOut(reduceMotion),
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    OutlinedTextField(
                        value = query,
                        onValueChange = { query = it },
                        singleLine = true,
                        leadingIcon = { Icon(Icons.Filled.Search, contentDescription = null) },
                        placeholder = { Text("Search name, filename, title id…") },
                        modifier = Modifier
                            .weight(1f)
                            .focusRequester(searchFocusRequester)
                    )
                }
            }

            notice?.let { GsBanner(text = it, color = GsColors.Warn) }

            Spacer(Modifier.height(4.dp))

            // List / loading / empty
            Box(modifier = Modifier.weight(1f).fillMaxWidth()) {
                when {
                    loading && catalog.isEmpty() -> {
                        CenterLoader(text = "Loading catalog…")
                    }
                    error != null && catalog.isEmpty() && serverConfigured == false -> {
                        CenterMessage(
                            title = "No server set",
                            detail = "Add your GameSync server in Settings to browse its games.",
                            action = "Open Settings" to { onNavigateToTab(SETTINGS_TAB) },
                        )
                    }
                    error != null && catalog.isEmpty() -> {
                        // A stays "download the selected row" here (a no-op on
                        // an empty list), so Retry is touch-only and carries no
                        // A glyph; a pad user waits for the automatic retry or
                        // uses Settings → Refresh catalog.
                        CenterMessage(
                            title = "Couldn't reach ${serverHostOf(settings.serverUrl)}",
                            detail = "Check the server is running and on the same network.",
                            footnotes = listOf(error ?: "", "Retrying when the server comes back"),
                            action = "Retry" to { viewModel.fetchRomCatalog() },
                        )
                    }
                    filtered.isEmpty() && catalog.isEmpty() -> {
                        CenterMessage(
                            title = "The server's ROM catalog is empty.",
                            detail = "Add ROMs on the server, then Settings → Refresh catalog.",
                        )
                    }
                    filtered.isEmpty() && raOnly && query.isBlank() -> {
                        CenterMessage(
                            title = "No RetroAchievements games" +
                                (activeSystem?.let { " for $it" } ?: "") + ".",
                            detail = "Press X or tap RA to show every game.",
                        )
                    }
                    filtered.isEmpty() -> {
                        CenterMessage(
                            title = "No ROMs match this search.",
                            detail = "Try a different term or clear the system filter.",
                        )
                    }
                    else -> {
                        CatalogList(
                            roms = filtered,
                            listState = listState,
                            selectedIndex = { selectedIndex },
                            onRowClick = { index, rom ->
                                selectedIndex = index
                                confirmTarget = rom
                            },
                            modifier = Modifier.gsEdgeBump(follower),
                        )
                    }
                }
                if (filtered.isNotEmpty()) GsListScrollEdges(listState)
                // Over the list while a hold jumps by letter.
                AlphabetHud(holdNav)
            }

            CatalogFooter(total = catalog.size, shown = filtered.size, raOnly = raOnly)
        }
    }

    GsDialogHost(confirmTarget) { rom, _ ->
        // A Wii U game's update and DLC are separate titles that are useless
        // apart, so one tap queues the whole set in install order.
        val group = rom.withRelated(catalog)
        // The real destination and the space left there (read off the main thread).
        val destination by produceState<MainViewModel.RomDestination?>(null, rom.system) {
            value = viewModel.romDestination(rom.system)
        }
        val totalSize = group.sumOf { it.size }
        val tooBig = destination?.freeBytes?.let { free -> totalSize > 0 && totalSize > free } == true
        GsDialog(
            title = if (group.size > 1) "Download ${group.size} titles?" else "Download ROM?",
            onDismiss = { confirmTarget = null },
            choices = listOf(
                GsDialogChoice(GsButton.B, "Cancel", { confirmTarget = null }),
                GsDialogChoice(
                    GsButton.A,
                    if (tooBig) "Download anyway" else "Download",
                    {
                        group.forEach { part ->
                            val extract = part.preferredDownloadExtractFormat()
                            viewModel.downloadRom(
                                romId = part.rom_id ?: part.title_id,
                                system = part.system,
                                filename = part.preferredDownloadFilename(extract),
                                extractFormat = extract,
                                bundleKind = part.msuPackKind,
                            )
                        }
                        confirmTarget = null
                    },
                    color = GsColors.Accent,
                    textColor = GsColors.Ink,
                ),
            ),
        ) {
            Text(rom.name.ifEmpty { rom.filename }, fontWeight = FontWeight.Bold, color = GsColors.Text)
            Spacer(Modifier.height(4.dp))
            Text("System: ${rom.system}", color = GsColors.Dim)
            if (group.size > 1) {
                Spacer(Modifier.height(6.dp))
                group.forEach { part ->
                    val label = part.contentType?.replaceFirstChar { it.uppercase() }
                        ?: "Game"
                    Text("$label: ${formatBytes(part.size)}", color = GsColors.Dim)
                }
                Text(
                    "Total: ${formatBytes(group.sumOf { it.size })}",
                    fontWeight = FontWeight.Bold,
                    color = GsColors.Text,
                )
            } else if (rom.msuPackKind != null) {
                // Unpacked into its own folder: the ROM plus the audio it
                // streams from beside itself.
                Text("Type: ${MsuPack.label(rom.msuPackKind)}, unpacked into a folder", color = GsColors.Dim)
                if (rom.size > 0) Text("Size: ${formatBytes(rom.size)}", color = GsColors.Dim)
            } else {
                Text("File: ${rom.filename}", color = GsColors.Dim)
                if (rom.size > 0) Text("Size: ${formatBytes(rom.size)}", color = GsColors.Dim)
            }
            Spacer(Modifier.height(8.dp))
            val dest = destination
            Text(
                if (dest != null) "To: ${dest.dir.absolutePath}" else "To: the ROM folder set in Settings",
                style = MaterialTheme.typography.bodySmall,
                color = gsTextMuted(),
            )
            dest?.freeBytes?.let { free ->
                Text(
                    if (tooBig) "Free: ${formatBytes(free).ifEmpty { "0 B" }} — not enough for ${formatBytes(totalSize)}"
                    else "Free: ${formatBytes(free).ifEmpty { "0 B" }}",
                    style = MaterialTheme.typography.bodySmall,
                    color = if (tooBig) GsColors.WarnText else gsTextMuted(),
                    fontWeight = if (tooBig) FontWeight.Medium else null,
                )
            }
        }
    }
}

/**
 * The catalog rows.  [selectedIndex] is read lazily inside each row's
 * derivedStateOf, so a D-pad step recomposes only the two rows whose
 * highlight changes.
 */
@Composable
private fun CatalogList(
    roms: List<RomEntry>,
    listState: LazyListState,
    selectedIndex: () -> Int,
    onRowClick: (Int, RomEntry) -> Unit,
    modifier: Modifier = Modifier,
) {
    LazyColumn(
        state = listState,
        modifier = modifier,
        contentPadding = PaddingValues(horizontal = 12.dp, vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp)
    ) {
        itemsIndexed(
            roms,
            key = { _, rom -> "${rom.system}:${rom.rom_id ?: rom.filename}" }
        ) { index, rom ->
            val isSelected by remember(index) { derivedStateOf { selectedIndex() == index } }
            CatalogRomCard(
                rom = rom,
                isSelected = isSelected,
                onClick = { onRowClick(index, rom) },
            )
        }
    }
}

@Composable
private fun CatalogRomCard(
    rom: RomEntry,
    isSelected: Boolean,
    onClick: () -> Unit,
) {
    GsListRow(isSelected = isSelected, onClick = onClick, clickLabel = "Download") {
        SystemBadge(rom.system)
        Column(modifier = Modifier.weight(1f)) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                Text(
                    rom.name.ifEmpty { rom.filename },
                    fontWeight = FontWeight.SemiBold,
                    color = GsColors.Text,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f, fill = false),
                )
                if (rom.hasRa) RaBadge(rom.raIsTitleOnly)
            }
            val subtitle = buildString {
                append(rom.filename)
                if (rom.size > 0) append("  ·  ${formatBytes(rom.size)}")
                append("  ·  ${rom.title_id}")
            }
            // TextMuted, not Dim: this row may be lifted onto PanelHi.
            Text(
                subtitle,
                style = MaterialTheme.typography.bodySmall,
                color = gsTextMuted(),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        Icon(
            Icons.Filled.Download,
            contentDescription = "Download",
            modifier = Modifier.size(20.dp),
            tint = if (isSelected) GsColors.Accent else GsColors.Muted,
        )
    }
}

@Composable
private fun CatalogFooter(total: Int, shown: Int, raOnly: Boolean = false) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 6.dp),
        horizontalArrangement = Arrangement.End,
    ) {
        Text(
            text = (if (shown == total) "$total ROMs" else "$shown / $total ROMs") +
                if (raOnly) "  ·  RA only (X)" else "",
            style = MaterialTheme.typography.labelSmall,
            color = gsTextMuted(),
        )
    }
}

/** The Downloads tab's index. */
internal const val DOWNLOADS_TAB = 3

/** The host part of a server URL, for messages ("192.168.1.100"); the URL itself if it has none. */
internal fun serverHostOf(serverUrl: String): String =
    runCatching { java.net.URI(serverUrl.trim()).host }.getOrNull()?.takeIf { it.isNotBlank() }
        ?: serverUrl.trim().ifEmpty { "the server" }
