package com.savesync.android.ui

import com.savesync.android.emulators.SaveEntry

/**
 * One row of the Saves list: the save and its sync status.  [status] null
 * means "still checking" — the status needs a hash that hasn't been
 * computed yet — and renders as a neutral "Checking…" pill.  Never guess.
 */
data class SaveRow(val entry: SaveEntry, val status: SaveSyncStatus?)

/**
 * What the server side of a three-way comparison is known to be: not
 * fetched yet (or failed), known to have no save, or a hash.
 */
sealed class ServerSide {
    object Unknown : ServerSide()
    object Missing : ServerSide()
    data class Hash(val hash: String) : ServerSide()
}

/**
 * A status that needs no hashing, or null when it does.  Server-only rows
 * and saves never synced on this device are known from metadata alone.
 * "Never synced" is returned whenever there is no last-synced hash, even if
 * the server holds a copy: without a base the relationship is unknown, and
 * sync treats it as a conflict unless the hashes happen to match.
 */
fun cheapSyncStatus(
    isServerOnly: Boolean,
    lastSyncedHash: String?,
    conflict: Boolean = false,
): SaveSyncStatus? = when {
    conflict -> SaveSyncStatus.CONFLICT
    isServerOnly -> SaveSyncStatus.SERVER_ONLY
    lastSyncedHash == null -> SaveSyncStatus.LOCAL_ONLY
    else -> null
}

/**
 * The list's full status from this device's view: the save's sync hash
 * against the hash recorded at its last sync.  An empty [localHash] (the
 * save couldn't be hashed) is Unknown, never Synced.
 */
fun localStatus(localHash: String, lastSyncedHash: String?): SaveSyncStatus = when {
    lastSyncedHash == null -> SaveSyncStatus.LOCAL_ONLY
    localHash.isEmpty() -> SaveSyncStatus.UNKNOWN
    localHash != lastSyncedHash -> SaveSyncStatus.LOCAL_NEWER
    else -> SaveSyncStatus.SYNCED
}

/**
 * The three-way comparison sync itself makes (server/app/routes/sync.py):
 * equal → Synced; no base → Never synced; only this device changed → Local
 * newer; only the server changed → Server newer; both → Conflict.
 */
fun threeWayStatus(localHash: String, lastSyncedHash: String?, serverHash: String): SaveSyncStatus = when {
    localHash == serverHash -> SaveSyncStatus.SYNCED
    lastSyncedHash == null -> SaveSyncStatus.LOCAL_ONLY
    lastSyncedHash == serverHash -> SaveSyncStatus.LOCAL_NEWER
    lastSyncedHash == localHash -> SaveSyncStatus.SERVER_NEWER
    else -> SaveSyncStatus.CONFLICT
}

/**
 * The Save detail header's status, from the same three values the "Server"
 * block shows, so the two can't disagree.  [hasLocal] false = nothing on this
 * device.  While the local hash or the server answer is still loading, the
 * list row's status ([fallback], may be null = checking) is shown instead.
 */
fun detailSyncStatus(
    hasLocal: Boolean,
    localHash: String?,
    lastSyncedHash: String?,
    server: ServerSide,
    fallback: SaveSyncStatus?,
    conflict: Boolean = false,
): SaveSyncStatus? {
    if (!hasLocal) {
        return when (server) {
            is ServerSide.Hash -> SaveSyncStatus.SERVER_ONLY
            ServerSide.Missing -> SaveSyncStatus.UNKNOWN
            ServerSide.Unknown -> fallback
        }
    }
    if (localHash == null) return if (conflict) SaveSyncStatus.CONFLICT else fallback
    return when (server) {
        ServerSide.Unknown -> if (conflict) SaveSyncStatus.CONFLICT else fallback
        ServerSide.Missing ->
            if (lastSyncedHash == null) SaveSyncStatus.LOCAL_ONLY else SaveSyncStatus.LOCAL_NEWER
        is ServerSide.Hash -> threeWayStatus(localHash, lastSyncedHash, server.hash)
    }
}

/**
 * Cache key of a computed status: a status stays valid while the save's
 * files (newest mtime, total size), its recorded base hash and the hashing
 * rules ([hashRules]: the Saturn format and archive choice) are unchanged.
 * Filter and search changes therefore never re-hash.
 */
data class StatusKey(
    val titleId: String,
    val newestMtime: Long,
    val totalSize: Long,
    val lastSyncedHash: String?,
    val hashRules: String,
)

/** Newest modification time and total size of the files that make up [entry]. */
fun SaveEntry.statFingerprint(): Pair<Long, Long> {
    val files: List<java.io.File> = when {
        saveDir != null -> saveDir.walkTopDown().filter { it.isFile }.toList()
        saveFile != null -> (listOf(saveFile) + extraFiles).filter { it.isFile }
        else -> emptyList()
    }
    return (files.maxOfOrNull { it.lastModified() } ?: 0L) to files.sumOf { it.length() }
}

/** The filtered Saves list plus how many matching rows are still being checked. */
data class SaveListView(val rows: List<SaveRow>, val checking: Int)

/**
 * Applies the system filter, the search and the status filter.  The status
 * filter applies to the computed status: while it is active, rows still
 * checking are left out and counted in [SaveListView.checking], so rows only
 * ever appear as they resolve and never vanish.  Without a status filter
 * they are listed with the "Checking…" pill.
 */
fun filterSaveRows(
    rows: List<SaveRow>,
    systemFilter: String,
    query: String,
    statusFilter: SaveSyncStatus?,
): SaveListView {
    val q = query.lowercase().trim()
    val matching = rows.filter { row ->
        val e = row.entry
        (systemFilter == "All" || e.systemName == systemFilter) &&
            (q.isEmpty() ||
                e.displayName.lowercase().contains(q) ||
                e.titleId.lowercase().contains(q) ||
                e.canonicalName?.lowercase()?.contains(q) == true)
    }
    if (statusFilter == null) return SaveListView(matching, 0)
    return SaveListView(
        rows = matching.filter { it.status == statusFilter },
        checking = matching.count { it.status == null },
    )
}

/**
 * Where the Saves cursor belongs after the list changed: on the same game if
 * it is still listed, else on the row now at its old index (clamped).  The
 * cursor follows the game, not the index, so a status update that reorders
 * or filters rows between two presses never moves A onto another game.
 * Returns -1 for an empty list.
 */
fun resolveSelection(titleIds: List<String>, selectedTitleId: String?, lastIndex: Int): Int {
    if (titleIds.isEmpty()) return -1
    val same = selectedTitleId?.let { titleIds.indexOf(it) } ?: -1
    return if (same >= 0) same else lastIndex.coerceIn(0, titleIds.size - 1)
}

/**
 * The detail screen's "From" for the server copy: the server's
 * last_sync_source, else the platform plus the last four characters of the
 * console id ("android · 3f9a"), else "Unknown device".  Some sources name
 * only the route the save came in by ("raw", "gc_card"), not the device:
 * for those the platform and console id say more, and the route is put in
 * words when they are missing.
 */
fun serverSourceLabel(lastSyncSource: String?, platform: String?, consoleId: String?): String {
    val source = lastSyncSource?.trim()?.takeIf { it.isNotEmpty() }
    val transport = source?.let { TRANSPORT_SOURCES[it.lowercase()] }
    if (source != null && transport == null) return source
    val tail = consoleId?.trim()?.takeIf { it.isNotEmpty() }?.takeLast(4)
    val plat = platform?.trim()?.takeIf { it.isNotEmpty() }
    return when {
        plat != null && tail != null -> "$plat · $tail"
        tail != null -> "Device $tail"
        transport != null -> transport
        plat != null -> plat
        else -> "Unknown device"
    }
}

/** Server sources that name an upload route rather than a device. */
private val TRANSPORT_SOURCES = mapOf(
    "raw" to "Raw save upload",
    "ps1_card" to "PS1 memory card",
    "ps2_card" to "PS2 memory card",
    "gc_card" to "GameCube memory card",
    "ps1_vmc" to "PS1 virtual memory card",
    "ps2_vmc" to "PS2 virtual memory card",
    "gc_vmc" to "GameCube virtual memory card",
)

/**
 * Which side's time is later, for the "Newer" pill: -1 this device, 1 the
 * server, 0 neither (unknown, or within [toleranceMs]).  Display only: the
 * sync decision stays hash-based.
 */
fun newerSide(localMs: Long, serverMs: Long, toleranceMs: Long = 2_000L): Int = when {
    localMs <= 0L || serverMs <= 0L -> 0
    localMs - serverMs > toleranceMs -> -1
    serverMs - localMs > toleranceMs -> 1
    else -> 0
}
