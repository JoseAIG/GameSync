package com.savesync.android.sync

import com.savesync.android.api.ConflictInfo
import com.savesync.android.emulators.SaveEntry

// ── Sync all, in two steps ──────────────────────────────────────────────────
//
// SyncEngine.plan() hashes the saves and asks the server what to do (POST
// /sync, or the memory-card meta for raw cards) without moving any bytes;
// SyncEngine.execute() then does it.  Between the two the user sees the
// plan (Sync all's preview), and an upload that looks like a rollback — a
// save much smaller than the server's, or with fewer files — is held back
// unless the user includes it.  sync() is execute(plan(saves), emptySet()),
// so the background worker gets the same protection.

/** What the plan does with one title. */
enum class PlannedAction {
    /** This device's save goes to the server. */
    UPLOAD,
    /** The server's save replaces a save on this device (a backup is kept first). */
    DOWNLOAD,
    /** The server's save lands where this device has none yet. */
    DOWNLOAD_NEW,
    /** Both changed since the last sync: left alone, decided on the detail screen. */
    CONFLICT,
    UP_TO_DATE,
}

/** How a planned title is carried out. */
enum class PlanRoute {
    /** Decided by POST /sync. */
    SYNC,
    /** A PS1/PS2/GC memory card, decided from its card meta. */
    RAW_CARD,
    /** A server-only save with a known local target, downloaded without asking /sync. */
    SERVER_ONLY,
}

data class PlannedTitle(
    val titleId: String,
    val displayName: String,
    val action: PlannedAction,
    val route: PlanRoute,
)

/** A planned upload that looks like it would roll the server's save back. */
data class SuspectUpload(
    val titleId: String,
    val displayName: String,
    /** The canonical size sync sends (SyncTitle.size). */
    val localSize: Long,
    val serverSize: Long?,
    /** Files in the save folder; null for single-file saves. */
    val localFileCount: Int?,
    val serverFileCount: Int?,
)

/**
 * A sync plan: what [SyncEngine.execute] will do.  The counts are what the
 * Sync-all dialog shows; [entries] are the saves it was made from.
 */
class SyncPreview internal constructor(
    val titles: List<PlannedTitle>,
    /** Uploads held back unless included (see [RollbackCheck]). */
    val suspects: List<SuspectUpload>,
    /** Uploads whose size could not be checked (over the cap, or the check failed). */
    val sizeCheckSkipped: Int,
    /** Problems found while planning (unreadable saves, server unreachable). */
    val errors: List<String>,
    /** When the plan was made (uptime-independent wall clock, ms). */
    val createdAtMs: Long,
    internal val entries: Map<String, SaveEntry>,
    internal val conflictInfo: Map<String, ConflictInfo>,
    /** The save hash each /sync title was planned with (SyncTitle.save_hash). */
    internal val sentHashes: Map<String, String> = emptyMap(),
) {
    val suspectIds: Set<String> get() = suspects.mapTo(LinkedHashSet()) { it.titleId }

    fun count(action: PlannedAction): Int = titles.count { it.action == action }

    /** Uploads that go ahead by default (suspects excluded). */
    val safeUploads: Int get() = titles.count { it.action == PlannedAction.UPLOAD && it.titleId !in suspectIds }

    /**
     * How many titles would move bytes, after [excluded] and the suspects
     * that are not in [includeSuspects].  Zero means Sync all has nothing
     * to do ("Done").
     */
    fun actionableCount(excluded: Set<String> = emptySet(), includeSuspects: Set<String> = emptySet()): Int {
        val skip = RollbackCheck.skippedTitles(suspectIds, excluded, includeSuspects)
        return titles.count { it.action in MOVING_ACTIONS && it.titleId !in skip }
    }

    companion object {
        internal val MOVING_ACTIONS = setOf(PlannedAction.UPLOAD, PlannedAction.DOWNLOAD, PlannedAction.DOWNLOAD_NEW)
    }
}

/**
 * Rollback protection (FORK.md "Protección contra retrocesos").  An upload
 * replaces the server's copy, which every other device then downloads; if
 * the local save is a fresh game or an old backup, that is a rollback.  Two
 * cheap signs, from what the server's meta already says:
 *  - the canonical size is under half of the server's `save_size`;
 *  - a multi-file save has fewer files than the server's `file_count`.
 * Suspects are skipped unless the user includes them.  Pure, so it is unit
 * tested.
 */
object RollbackCheck {
    /** At most this many uploads get a meta request per sync. */
    const val MAX_SIZE_CHECKS = 20
    /** Meta requests in flight at once. */
    const val PARALLEL_CHECKS = 4

    /**
     * Whether an upload of [localSize] bytes ([localFileCount] files, null
     * for a single-file save) over a server copy of [serverSize] bytes /
     * [serverFileCount] files looks like a rollback.  Unknown server values
     * never make a suspect.
     */
    fun isSuspect(localSize: Long, localFileCount: Int?, serverSize: Long?, serverFileCount: Int?): Boolean {
        val smaller = serverSize != null && serverSize > 0 && localSize * 2 < serverSize
        val fewerFiles = localFileCount != null && serverFileCount != null && localFileCount < serverFileCount
        return smaller || fewerFiles
    }

    /** The uploads to check (the first [MAX_SIZE_CHECKS]) and the rest, which are not checked. */
    fun <T> splitForChecks(uploads: List<T>): Pair<List<T>, List<T>> =
        uploads.take(MAX_SIZE_CHECKS) to uploads.drop(MAX_SIZE_CHECKS)

    /** Titles execute() leaves alone: [excluded], plus every suspect not in [includeSuspects]. */
    fun skippedTitles(suspectIds: Set<String>, excluded: Set<String>, includeSuspects: Set<String>): Set<String> =
        excluded + (suspectIds - includeSuspects)
}

/**
 * A memory card's decision from its meta, as the old raw-card sync made it:
 * no server copy → upload (nothing to do with no local card either); equal
 * hashes → up to date; no local card → download; otherwise the three-way
 * rule, with a conflict when there is no sync history.  Null = nothing to do.
 */
internal fun decideRawCard(
    localExists: Boolean,
    localHash: String,
    lastSyncedHash: String?,
    serverHash: String?,
    serverHasSave: Boolean,
): PlannedAction? {
    if (!serverHasSave) return if (localExists) PlannedAction.UPLOAD else null
    val server = serverHash ?: ""
    if (localExists && localHash == server) return PlannedAction.UP_TO_DATE
    if (!localExists) return PlannedAction.DOWNLOAD_NEW
    return when {
        lastSyncedHash != null && lastSyncedHash == server -> PlannedAction.UPLOAD
        lastSyncedHash != null && lastSyncedHash == localHash -> PlannedAction.DOWNLOAD
        else -> PlannedAction.CONFLICT
    }
}

/**
 * Whether a planned title still goes ahead after a re-check: only when the
 * fresh decision is the one the user saw.  A save that changed since the
 * preview (new play, another device's upload) is skipped, not guessed at.
 */
internal fun stillPlanned(planned: PlannedAction, fresh: PlannedAction?): Boolean = planned == fresh
