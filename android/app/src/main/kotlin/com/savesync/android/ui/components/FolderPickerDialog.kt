package com.savesync.android.ui.components

import android.os.Environment
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.ContentTransform
import androidx.compose.animation.core.VisibilityThreshold
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.horizontalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.Storage
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.savesync.android.emulators.DirCheck
import com.savesync.android.emulators.EmulatorRegistry
import com.savesync.android.emulators.FolderScan
import com.savesync.android.ui.theme.GsColors
import com.savesync.android.ui.theme.GsElevation
import com.savesync.android.ui.theme.GsMotion
import com.savesync.android.ui.theme.GsShapes
import com.savesync.android.ui.theme.LocalReduceMotion
import com.savesync.android.ui.theme.gsTextMuted
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

/**
 * Folder picker that tells you what is in each folder (the Android
 * counterpart of the console clients' FBI-style picker).
 *
 * Navigation is focus traversal, as in v055: the D-pad moves focus between
 * rows and buttons, A opens the focused row through the system key-map
 * fallback (A → DPAD_CENTER) and B cancels (B → BACK).  X goes up one level
 * (X is otherwise unused here).  All disk reads — the listing, the storage
 * volumes and the contents line — happen on Dispatchers.IO, never in
 * composition; the previous folder stays on screen until the next arrives.
 *
 * [emulatorKey]: when picking that emulator's save folder, the contents line
 * is its own check ([EmulatorRegistry.describeSaveDir]) instead of the
 * generic count.  Choosing a folder in which nothing is recognisable asks
 * first.
 */
@Composable
fun FolderPickerDialog(
    /** Pre-selected path to open in (blank = external storage root) */
    initialPath: String = "",
    onDismiss: () -> Unit,
    onFolderSelected: (String) -> Unit,
    emulatorKey: String? = null,
) {
    val reduce = LocalReduceMotion.current
    var target by remember { mutableStateOf<File?>(null) }
    var listing by remember { mutableStateOf<DirListing?>(null) }
    var storageRoots by remember { mutableStateOf<List<File>>(emptyList()) }
    var confirmUnrecognised by remember { mutableStateOf<DirListing?>(null) }
    // Path → (first visible index, offset): going up restores the parent's
    // place; drilling in starts at the top.
    val positions = remember { mutableMapOf<String, Pair<Int, Int>>() }

    LaunchedEffect(Unit) {
        val start = withContext(Dispatchers.IO) { resolveStartDir(initialPath) }
        if (target == null) target = start
        storageRoots = withContext(Dispatchers.IO) { discoverStorageRoots() }
    }
    LaunchedEffect(target) {
        val dir = target ?: return@LaunchedEffect
        val previous = listing?.dir
        val loaded = withContext(Dispatchers.IO) { loadListing(dir, emulatorKey) }
        loaded.focusChild = previous?.let { childToward(dir.absolutePath, it.absolutePath) }
        listing = loaded
    }

    fun open(dir: File) {
        val from = listing?.dir ?: target
        if (from != null && !isAncestor(dir.absolutePath, from.absolutePath)) {
            positions.remove(dir.absolutePath)
        }
        target = dir
    }

    fun goUp() {
        listing?.parent?.let { open(it) }
    }

    // Keys pressed in a Dialog window never reach MainActivity, so the app's
    // input tracker can't switch to Pad mode here.  The picker tracks it
    // itself: a key shows the focused row's highlight (the first one only
    // reveals it, as on the list screens), a touch hides it again.
    val outerMode = LocalInputMode.current
    var keysInDialog by remember { mutableStateOf(false) }
    // A row (not a button, whose ring always shows) holds focus.
    var listFocused by remember { mutableStateOf(false) }
    val padMode = outerMode == GsInputMode.Pad || keysInDialog
    val config = LocalConfiguration.current
    val short = pickerIsShort(config.screenHeightDp, config.fontScale)

    // Leaving (GsDialogHost exit): ignore input, so nothing acts twice.
    val dismissing = gsDialogDismissing()
    val currentDismissing by rememberUpdatedState(dismissing)
    Dialog(
        onDismissRequest = { if (!currentDismissing) onDismiss() },
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        CompositionLocalProvider(
            LocalInputMode provides if (padMode) GsInputMode.Pad else GsInputMode.Touch,
        ) {
          GsDialogMotion {
            Surface(
                modifier = Modifier
                    .widthIn(max = 640.dp)
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = if (short) 8.dp else 24.dp)
                    .pointerInput(Unit) {
                        awaitEachGesture {
                            awaitFirstDown(requireUnconsumed = false, pass = PointerEventPass.Initial)
                            keysInDialog = false
                        }
                    },
                shape = GsShapes.Dialog,
                color = GsColors.Panel,
                border = BorderStroke(1.dp, GsColors.Line),
                shadowElevation = GsElevation.DialogShadow,
                tonalElevation = GsElevation.DialogTonal,
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(max = 560.dp)
                        .padding(if (short) 12.dp else 16.dp)
                        // Focus is always inside the dialog, so its root sees X.
                        .onPreviewKeyEvent { event ->
                            if (currentDismissing) return@onPreviewKeyEvent true
                            if (event.type != KeyEventType.KeyDown) return@onPreviewKeyEvent false
                            if (!padMode) {
                                keysInDialog = true
                                // The reveal press: nothing fires on a row the
                                // user couldn't see highlighted.
                                if (listFocused && event.key in PICKER_REVEAL_SWALLOWED) {
                                    return@onPreviewKeyEvent true
                                }
                            }
                            if (event.key == Key.ButtonX) {
                                if (event.nativeKeyEvent.repeatCount == 0) goUp()
                                true
                            } else {
                                false
                            }
                        },
                ) {
                    Text(
                        "Select folder",
                        style = MaterialTheme.typography.titleMedium,
                        color = GsColors.Text,
                    )
                    Spacer(Modifier.size(4.dp))
                    Text(
                        text = listing?.dir?.absolutePath ?: target?.absolutePath ?: "",
                        style = MaterialTheme.typography.bodySmall,
                        color = GsColors.Accent2,
                        maxLines = if (short) 1 else 2,
                        overflow = TextOverflow.Ellipsis
                    )
                    // Live contents line: what GameSync would find here.
                    val check = listing?.check
                    Text(
                        text = check?.summary ?: "Checking…",
                        style = MaterialTheme.typography.bodySmall,
                        color = if (check != null && !check.ok) GsColors.WarnText else gsTextMuted(),
                        maxLines = if (short) 1 else 3,
                        overflow = TextOverflow.Ellipsis,
                    )
                    Spacer(Modifier.size(if (short) 4.dp else 8.dp))

                    // Storage shortcuts (when more than one volume is mounted)
                    if (storageRoots.size > 1) {
                        Row(
                            modifier = Modifier
                                .horizontalScroll(rememberScrollState())
                                .padding(bottom = 8.dp),
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                        ) {
                            val internal = Environment.getExternalStorageDirectory().absolutePath
                            storageRoots.forEach { root ->
                                OutlinedButton(
                                    onClick = { open(root) },
                                    modifier = Modifier.gsFocusRing(),
                                ) {
                                    Icon(Icons.Default.Storage, contentDescription = null, modifier = Modifier.size(14.dp))
                                    Spacer(Modifier.width(4.dp))
                                    Text(
                                        if (root.absolutePath == internal) "Internal" else root.name,
                                        style = MaterialTheme.typography.labelSmall,
                                    )
                                }
                            }
                        }
                    }

                    // ── Directory listing: slides the way you went ───────────
                    AnimatedContent(
                        targetState = listing,
                        contentKey = { it?.dir?.absolutePath },
                        transitionSpec = {
                            pickerTransition(
                                pickerMotionFor(initialState?.dir?.absolutePath, targetState?.dir?.absolutePath, reduce)
                            )
                        },
                        modifier = Modifier
                            .fillMaxWidth()
                            .weight(1f)
                            .clipToBounds()
                            .onFocusChanged { listFocused = it.hasFocus },
                        label = "folderListing",
                    ) { shown ->
                        if (shown == null) {
                            Box(Modifier.fillMaxWidth().padding(12.dp)) {
                                Text("Loading…", style = MaterialTheme.typography.bodySmall, color = gsTextMuted())
                            }
                        } else {
                            FolderList(
                                listing = shown,
                                positions = positions,
                                onOpen = { open(it) },
                            )
                        }
                    }

                    // ── Action buttons, always visible at the bottom ─────────
                    Spacer(Modifier.size(if (short) 8.dp else 12.dp))
                    Row(modifier = Modifier.fillMaxWidth()) {
                        OutlinedButton(
                            onClick = onDismiss,
                            modifier = Modifier.weight(1f).gsFocusRing()
                        ) {
                            Text("Cancel")
                        }
                        Spacer(Modifier.width(12.dp))
                        val current = listing
                        Button(
                            onClick = {
                                val l = current ?: return@Button
                                if (l.check.ok) onFolderSelected(l.dir.absolutePath)
                                else confirmUnrecognised = l
                            },
                            enabled = current != null && current.dir == target,
                            modifier = Modifier.weight(2f).gsFocusRing()
                        ) {
                            Icon(Icons.Default.CheckCircle, contentDescription = null, modifier = Modifier.size(16.dp))
                            Spacer(Modifier.width(6.dp))
                            Text(
                                text = "Select \"${current?.dir?.name ?: ""}\"",
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                    }
                    // A short window keeps its rows: the buttons already carry
                    // the labels, so the hints go first.
                    if (!short) {
                        GsFooterHints(
                            hints = listOf(
                                GsHint(GsButton.A, "Open"),
                                GsHint(GsButton.X, "Up"),
                                GsHint(GsButton.B, "Cancel"),
                            ),
                            modifier = Modifier.padding(top = 8.dp),
                        )
                    }
                }
            }
          }
        }
    }

    GsDialogHost(confirmUnrecognised) { l, _ ->
        GsConfirmDialog(
            title = "Nothing recognisable here — use anyway?",
            message = l.check.summary,
            confirmLabel = "Use anyway",
            dismissLabel = "Keep browsing",
            onConfirm = {
                confirmUnrecognised = null
                onFolderSelected(l.dir.absolutePath)
            },
            onDismiss = { confirmUnrecognised = null },
        )
    }
}

/** Keys the picker's reveal press consumes, as on the list screens: A (and Enter / centre), X and Y. */
private val PICKER_REVEAL_SWALLOWED = setOf(
    Key.ButtonA, Key.Enter, Key.NumPadEnter, Key.DirectionCenter, Key.ButtonX, Key.ButtonY,
)

/**
 * True when the window is too short (in text-scaled terms) for the picker's
 * full header and footer: it then keeps one line of path and contents,
 * tighter padding and no footer hints, so the folder list keeps its rows.
 */
internal fun pickerIsShort(screenHeightDp: Int, fontScale: Float): Boolean =
    screenHeightDp / fontScale.coerceAtLeast(1f) < PICKER_SHORT_HEIGHT_DP

internal const val PICKER_SHORT_HEIGHT_DP = 480

/** One folder's contents, read on IO. */
private class DirListing(
    val dir: File,
    /** The ".." target, when it exists and can be read. */
    val parent: File?,
    val subDirs: List<File>,
    val check: DirCheck,
) {
    /** When we came up from a child: the name of the row to focus. */
    var focusChild: String? = null
}

private fun resolveStartDir(initialPath: String): File {
    if (initialPath.isNotBlank()) {
        val f = File(initialPath)
        if (f.isDirectory) return f
    }
    return Environment.getExternalStorageDirectory()
}

/** /storage/emulated/0 plus any SD cards / USB volumes. */
private fun discoverStorageRoots(): List<File> = buildList {
    add(Environment.getExternalStorageDirectory())
    try {
        File("/storage").listFiles()
            ?.filter { it.isDirectory && it.name != "emulated" && it.name != "self" }
            ?.forEach { add(it) }
    } catch (_: Exception) {
    }
}.distinctBy { it.absolutePath }

private fun loadListing(dir: File, emulatorKey: String?): DirListing {
    val subDirs = dir.listFiles()
        ?.filter { it.isDirectory && !it.name.startsWith(".") }
        ?.sortedBy { it.name.lowercase() }
        ?: emptyList()
    val parent = dir.parentFile?.takeIf { it.canRead() }
    val check = if (emulatorKey != null) EmulatorRegistry.describeSaveDir(emulatorKey, dir)
        else FolderScan.describe(dir)
    return DirListing(dir, parent, subDirs, check)
}

/** How the listing moves when the folder changes. */
internal enum class PickerMotion { FADE, DRILL_IN, GO_UP }

/**
 * Drill in (the new folder is inside the old one) slides in from the right,
 * going up slides back the other way, anything else (another volume, the
 * first listing, reduced motion) fades.
 */
internal fun pickerMotionFor(fromPath: String?, toPath: String?, reduce: Boolean): PickerMotion = when {
    reduce || fromPath == null || toPath == null || fromPath == toPath -> PickerMotion.FADE
    isAncestor(fromPath, toPath) -> PickerMotion.DRILL_IN
    isAncestor(toPath, fromPath) -> PickerMotion.GO_UP
    else -> PickerMotion.FADE
}

/** True when [ancestor] strictly contains [path]. */
internal fun isAncestor(ancestor: String, path: String): Boolean {
    val a = ancestor.trimEnd('/')
    return path != a && path.startsWith("$a/")
}

/** Going up from [fromPath] to [ancestorPath]: the name of the child we came out of, else null. */
internal fun childToward(ancestorPath: String, fromPath: String): String? {
    if (!isAncestor(ancestorPath, fromPath)) return null
    return fromPath.removePrefix(ancestorPath.trimEnd('/') + "/").substringBefore('/')
}

private fun pickerTransition(motion: PickerMotion): ContentTransform {
    val slide = GsMotion.standard(IntOffset.VisibilityThreshold)
    return when (motion) {
        PickerMotion.FADE -> fadeIn(GsMotion.fade()) togetherWith fadeOut(GsMotion.fade())
        PickerMotion.DRILL_IN ->
            (slideInHorizontally(slide) { it / 6 } + fadeIn(GsMotion.fadeIn())) togetherWith
                (slideOutHorizontally(slide) { -it / 6 } + fadeOut(GsMotion.fadeOut()))
        PickerMotion.GO_UP ->
            (slideInHorizontally(slide) { -it / 6 } + fadeIn(GsMotion.fadeIn())) togetherWith
                (slideOutHorizontally(slide) { it / 6 } + fadeOut(GsMotion.fadeOut()))
    }
}

/**
 * One folder's rows, with their own scroll state.  When it appears, focus
 * moves to its first row (".." when present) — or, coming up from a child,
 * to that child's row, so the restored scroll position stays put.
 */
@Composable
private fun FolderList(
    listing: DirListing,
    positions: MutableMap<String, Pair<Int, Int>>,
    onOpen: (File) -> Unit,
) {
    val path = listing.dir.absolutePath
    val state = remember(path) {
        val saved = positions[path]
        LazyListState(saved?.first ?: 0, saved?.second ?: 0)
    }
    DisposableEffect(state) {
        onDispose { positions[path] = state.firstVisibleItemIndex to state.firstVisibleItemScrollOffset }
    }
    val hasParent = listing.parent != null
    val focusIndex = remember(listing) {
        val child = listing.focusChild?.let { name -> listing.subDirs.indexOfFirst { it.name == name } } ?: -1
        if (child >= 0) child + (if (hasParent) 1 else 0) else 0
    }
    val focusRequester = remember(listing) { FocusRequester() }
    LaunchedEffect(listing) {
        withFrameNanos { }
        runCatching { focusRequester.requestFocus() }
    }

    LazyColumn(state = state, modifier = Modifier.fillMaxWidth()) {
        if (hasParent) {
            item(key = "..") {
                FolderRow(
                    icon = Icons.Default.KeyboardArrowUp,
                    name = "..",
                    contentDescription = "Up one level",
                    focusRequester = focusRequester.takeIf { focusIndex == 0 },
                    onClick = { listing.parent?.let(onOpen) },
                )
            }
        }
        if (listing.subDirs.isEmpty()) {
            item(key = "empty") {
                Text(
                    "No folders inside",
                    style = MaterialTheme.typography.bodySmall,
                    color = gsTextMuted(),
                    modifier = Modifier.padding(12.dp),
                )
            }
        } else {
            itemsIndexed(listing.subDirs, key = { _, dir -> dir.absolutePath }) { i, dir ->
                val rowIndex = i + if (hasParent) 1 else 0
                FolderRow(
                    icon = Icons.Default.Folder,
                    name = dir.name,
                    contentDescription = null,
                    focusRequester = focusRequester.takeIf { focusIndex == rowIndex },
                    onClick = { onOpen(dir) },
                )
            }
        }
    }
}

/**
 * A picker row: a [GsListRow] whose highlight is this row's focus, so the
 * D-pad cursor is the focus itself (no separate selected index).
 */
@Composable
private fun FolderRow(
    icon: ImageVector,
    name: String,
    contentDescription: String?,
    focusRequester: FocusRequester?,
    onClick: () -> Unit,
) {
    var focused by remember { mutableStateOf(false) }
    GsListRow(
        isSelected = focused,
        onClick = onClick,
        clickLabel = "Open",
        modifier = Modifier
            .padding(vertical = 2.dp)
            .then(if (focusRequester != null) Modifier.focusRequester(focusRequester) else Modifier)
            .onFocusChanged { focused = it.isFocused || it.hasFocus },
    ) {
        Icon(
            imageVector = icon,
            contentDescription = contentDescription,
            tint = GsColors.Accent,
            modifier = Modifier.size(20.dp)
        )
        Text(
            text = name,
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.Medium,
            color = GsColors.Text,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f),
        )
    }
}
