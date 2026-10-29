package com.savesync.android.sync

import android.util.Log
import com.savesync.android.api.ConflictInfo
import com.savesync.android.api.NormalizeRequest
import com.savesync.android.api.NormalizeRomEntry
import com.savesync.android.api.SaveSyncApi
import com.savesync.android.api.SaturnArchiveLookupRequest
import com.savesync.android.api.SaturnArchiveLookupResult
import com.savesync.android.api.SaveMeta
import com.savesync.android.api.SyncRequest
import com.savesync.android.api.SyncTitle
import com.savesync.android.emulators.SaveEntry
import com.savesync.android.installed.InstalledRomsScanner
import com.savesync.android.storage.AppDatabase
import com.savesync.android.storage.SyncStateDao
import com.savesync.android.storage.SyncStateEntity
import com.savesync.android.systems.SystemAliases
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.RequestBody.Companion.toRequestBody
import java.io.File
import java.io.IOException

private const val TAG = "SyncEngine"

// Disc-based systems where each game conventionally lives in its own
// subfolder rather than as a flat file in the system directory (PS1
// .cue+.bin pairs, Saturn, Dreamcast, Sega CD).  Mirrors
// ``shared/systems.py::CD_FOLDER_SYSTEMS`` — keep the two in sync.
// When downloading a ROM for one of these systems we wrap the file in a
// subfolder named after the filename stem so multi-track layouts stay
// grouped and the system folder stays tidy.
private val CD_FOLDER_SYSTEMS: Set<String> = setOf("SEGACD", "SAT", "DC", "PS1")

private data class SaturnCanonicalSnapshot(
    val bytes: ByteArray,
    val archiveNames: List<String>
)

data class SyncResult(
    val uploaded: Int,
    val downloaded: Int,
    /** Display names of the conflicting titles. */
    val conflicts: List<String>,
    val errors: List<String>,
    /** Title ids of [conflicts], so the UI can keep them findable. */
    val conflictTitleIds: List<String> = emptyList(),
    /** Titles this sync left in agreement with the server (uploaded, downloaded, up to date). */
    val resolvedTitleIds: List<String> = emptyList(),
    /** The server's details for each conflicting title id. */
    val conflictInfo: Map<String, com.savesync.android.api.ConflictInfo> = emptyMap(),
    /** Uploads held back because they look like a rollback (display names). */
    val skippedSuspects: List<String> = emptyList(),
    /** Title ids of [skippedSuspects], same order. */
    val skippedSuspectIds: List<String> = emptyList(),
    /** Titles skipped because they changed after the preview was made (display names). */
    val changedSincePreview: List<String> = emptyList(),
)

class SyncEngine(
    private val api: SaveSyncApi,
    /** Each title's last synced hash (the three-way base). */
    private val dao: SyncStateDao,
    private val consoleId: String,
    private val saturnSyncFormat: SaturnSyncFormat = SaturnSyncFormat.MEDNAFEN,
    /** Where saves are copied before a download replaces them (default: the app's). */
    backupStore: LocalBackupStore? = null,
) {
    constructor(
        api: SaveSyncApi,
        db: AppDatabase,
        consoleId: String,
        saturnSyncFormat: SaturnSyncFormat = SaturnSyncFormat.MEDNAFEN,
        backupStore: LocalBackupStore? = null,
    ) : this(api, db.syncStateDao(), consoleId, saturnSyncFormat, backupStore)

    /**
     * Every download path snapshots the current save here before
     * [AtomicSaveWriter] replaces it.  Resolved lazily; if it can't be had,
     * the download fails rather than overwrite without a copy.
     */
    private val backups: LocalBackupStore by lazy { backupStore ?: LocalBackups.store }

    /**
     * What each download by this engine wrote, by title id: the base
     * [recordDownloaded] records.  Hashed from the bytes (or the staged
     * folder) before the swap, so a save the emulator writes after the swap
     * never becomes the base.
     */
    private val writtenSaves = java.util.concurrent.ConcurrentHashMap<String, WrittenSave>()

    /** Test seam: runs after a download has landed, before its base is recorded. */
    internal var beforeDownloadRecorded: (SaveEntry) -> Unit = {}

    private fun noteWritten(entry: SaveEntry, written: WrittenSave?) {
        if (written == null || written.hash.isEmpty()) writtenSaves.remove(entry.titleId)
        else writtenSaves[entry.titleId] = written
    }

    /** [writtenFileSave] for a download about to put [bytes] in [entry]'s save file; null if unpredictable. */
    private fun writtenFile(entry: SaveEntry, bytes: ByteArray, downloadedCanonical: ByteArray? = null): WrittenSave? =
        runCatching { writtenFileSave(entry, bytes, saturnSyncFormat, downloadedCanonical) }
            .onFailure { Log.w(TAG, "Couldn't hash the downloaded save for ${entry.titleId}", it) }
            .getOrNull()

    /** The title's sync state now, recorded in the snapshot (see [LocalBackup]). */
    private suspend fun syncStateForBackup(entry: SaveEntry): SyncStateEntity? = dao.getById(entry.titleId)

    private suspend fun autoResolveSharedSaturnArchives(entry: SaveEntry): List<String> {
        if (!isSharedYabaSanshiroContainer(entry)) return emptyList()

        val stored = SaturnArchiveStateStore.get(entry.titleId)
        if (stored.isNotEmpty()) return stored

        val saveFile = entry.saveFile ?: return emptyList()
        if (!saveFile.exists()) return emptyList()

        val archiveNames = try {
            SaturnSaveFormatConverter.archiveNames(saveFile.readBytes())
        } catch (e: Exception) {
            Log.w(TAG, "Could not parse YabaSanshiro backup for ${entry.titleId}", e)
            return emptyList()
        }
        if (archiveNames.isEmpty()) return emptyList()

        val lookupResults = try {
            api.lookupSaturnArchives(
                SaturnArchiveLookupRequest(
                    title_id = entry.titleId,
                    archive_names = archiveNames
                )
            ).results
        } catch (e: Exception) {
            Log.w(TAG, "Saturn archive lookup failed for ${entry.titleId}", e)
            return emptyList()
        }

        val selected = lookupResults
            .filter { it.matches_current_title || it.status == "exact_current" || it.status == "includes_current" }
            .flatMap { it.archive_names }
            .map { it.trim() }
            .filter { it.isNotEmpty() }
            .distinct()

        if (selected.isNotEmpty()) {
            SaturnArchiveStateStore.put(entry.titleId, selected)
        }
        return selected
    }

    /**
     * Sync [saves] in one go: [plan] then [execute] with nothing excluded.
     * Uploads that look like a rollback are still held back (see
     * [RollbackCheck]); [SyncResult.skippedSuspects] names them.
     */
    suspend fun sync(saves: List<SaveEntry>): SyncResult = execute(plan(saves), emptySet())

    /**
     * Work out what a sync of [saves] would do, moving no save bytes: hash
     * each save, ask the server (POST /sync; card meta for memory cards),
     * and check planned uploads against the server's size and file count.
     */
    suspend fun plan(saves: List<SaveEntry>): SyncPreview {
        // The plan is as old as its first hash, not its last server answer:
        // execute()'s freshness window must count the hashing and network
        // time too, or a minute-old hash could pass as "fresh".
        val startedAtMs = System.currentTimeMillis()
        // The list may predate a download or an emulator session: a "server-only"
        // row whose predicted file now exists is a local save with possibly newer
        // play on it, and must go through the three-way check, not a blind download.
        val resolvedSaves = normalizeCanonicalSlugEntries(saves.map { it.reconcileWithDisk() })
        val errors = mutableListOf<String>()
        val titles = mutableListOf<PlannedTitle>()
        val saveMap = resolvedSaves.associateBy { it.titleId }
        // Card meta already fetched for a raw-card upload: its size check is free.
        val rawCardMeta = mutableMapOf<String, SaveMeta>()
        // What each SyncTitle sent, for the rollback check.
        val sentSize = mutableMapOf<String, Long>()
        // Local saves this plan couldn't compare (unreadable, card meta failed):
        // left alone, never downloaded over through the server_only list.
        val uncompared = mutableSetOf<String>()

        val (rawCardEntries, otherEntries) = resolvedSaves.partition {
            isPs1RawEntry(it) || isPs2RawEntry(it) || isGcRawEntry(it)
        }
        for (entry in rawCardEntries) {
            try {
                val (action, meta) = planRawCard(entry)
                if (action != null) titles += PlannedTitle(entry.titleId, entry.displayName, action, PlanRoute.RAW_CARD)
                if (meta != null) rawCardMeta[entry.titleId] = meta
            } catch (e: Exception) {
                Log.e(TAG, "Raw-card check failed for ${entry.titleId}", e)
                errors.add("Sync error: ${entry.displayName}: ${e.message}")
                uncompared += entry.titleId
            }
        }

        resolvedSaves.forEach { entry ->
            if (isSharedYabaSanshiroContainer(entry)) {
                autoResolveSharedSaturnArchives(entry)
            }
        }

        val syncStateByTitle = resolvedSaves.associate { entry ->
            entry.titleId to dao.getById(entry.titleId)
        }

        // Server-only entries normally have no local file yet, but shared Saturn
        // containers like YabaSanshiro can still have a usable local sync source.
        val (serverOnlyEntries, localEntries) = otherEntries.partition { entry ->
            entry.isServerOnly && !hasLocalSyncSource(entry, syncStateByTitle[entry.titleId])
        }
        for (entry in serverOnlyEntries) {
            if (!hasLocalSaveTarget(entry)) {
                Log.i(TAG, "Skipping server-only entry without local target: ${entry.titleId}")
                continue
            }
            titles += PlannedTitle(entry.titleId, entry.displayName, PlannedAction.DOWNLOAD_NEW, PlanRoute.SERVER_ONLY)
        }

        // Build sync request titles for entries that have a local save file
        val syncTitles = localEntries.mapNotNull { entry ->
            val errorsBefore = errors.size
            buildSyncTitle(entry, syncStateByTitle[entry.titleId], errors).also {
                if (it == null && errors.size > errorsBefore) uncompared += entry.titleId
            }
        }
        syncTitles.forEach { sentSize[it.title_id] = it.size }
        val sentHashes = syncTitles.associate { it.title_id to it.save_hash }

        var conflictInfo: Map<String, ConflictInfo> = emptyMap()
        if (syncTitles.isNotEmpty()) {
            val syncResponse = try {
                api.sync(SyncRequest(titles = syncTitles, console_id = consoleId))
            } catch (e: Exception) {
                Log.e(TAG, "Sync API call failed", e)
                errors.add("Sync failed: ${e.message}")
                null
            }
            if (syncResponse != null) {
                val planned = titles.mapTo(HashSet()) { it.titleId }
                fun add(titleId: String, action: PlannedAction, route: PlanRoute = PlanRoute.SYNC) {
                    if (!planned.add(titleId)) return
                    val entry = saveMap[titleId]
                    titles += PlannedTitle(titleId, entry?.displayName ?: titleId, action, route)
                }
                syncResponse.upload.filter { it in saveMap }.forEach { add(it, PlannedAction.UPLOAD) }
                syncResponse.download.filter { it in saveMap }.forEach { add(it, PlannedAction.DOWNLOAD) }
                // Saves the server holds that weren't in the request.  One already
                // planned as a server-only download is not downloaded twice (the
                // second copy would push a real backup out of the five kept).
                // A local save left out because it couldn't be compared is not
                // "server only": downloading would skip the three-way check.
                syncResponse.server_only.forEach { titleId ->
                    if (titleId in uncompared) return@forEach
                    val entry = saveMap[titleId] ?: return@forEach
                    if (!hasLocalSaveTarget(entry)) return@forEach
                    val action = if (entry.exists()) PlannedAction.DOWNLOAD else PlannedAction.DOWNLOAD_NEW
                    add(titleId, action, PlanRoute.SERVER_ONLY)
                }
                syncResponse.conflict.forEach { add(it, PlannedAction.CONFLICT) }
                syncResponse.up_to_date.forEach { add(it, PlannedAction.UP_TO_DATE) }
                conflictInfo = syncResponse.conflict_info.orEmpty().associateBy { it.title_id }
            }
        }

        val (suspects, sizeCheckSkipped) = findSuspectUploads(titles, saveMap, syncStateByTitle, sentSize, rawCardMeta)

        return SyncPreview(
            titles = titles,
            suspects = suspects,
            sizeCheckSkipped = sizeCheckSkipped,
            errors = errors,
            createdAtMs = startedAtMs,
            entries = saveMap,
            conflictInfo = conflictInfo,
            sentHashes = sentHashes,
        )
    }

    /**
     * Carry out [preview].  Titles in [excluded] are left alone, and so is
     * every suspect upload not in [includeSuspects].  A preview older than
     * [FRESH_PREVIEW_MS] (the user read the dialog) is re-checked first: a
     * title whose decision changed since — new play, another device's
     * upload — is skipped and named in [SyncResult.changedSincePreview].
     */
    suspend fun execute(
        preview: SyncPreview,
        excluded: Set<String>,
        includeSuspects: Set<String> = emptySet(),
    ): SyncResult {
        val skip = RollbackCheck.skippedTitles(preview.suspectIds, excluded, includeSuspects)
        val errors = preview.errors.toMutableList()
        var uploaded = 0
        var downloaded = 0
        val conflicts = mutableListOf<String>()
        val conflictIds = mutableListOf<String>()
        val resolvedIds = mutableListOf<String>()
        val skippedSuspects = mutableListOf<String>()
        val skippedSuspectIds = mutableListOf<String>()
        val changed = mutableListOf<String>()

        val moving = preview.titles.filter { it.action in SyncPreview.MOVING_ACTIONS && it.titleId !in skip }
        val stale = System.currentTimeMillis() - preview.createdAtMs > FRESH_PREVIEW_MS
        val fresh: FreshDecisions? = if (stale && moving.isNotEmpty()) recheck(moving, preview.entries) else null
        fresh?.errors?.let { errors.addAll(it) }

        for (title in preview.titles) {
            val entry = preview.entries[title.titleId]
            when (title.action) {
                PlannedAction.CONFLICT -> {
                    conflicts.add(entry?.displayName ?: title.titleId)
                    conflictIds.add(title.titleId)
                    continue
                }
                PlannedAction.UP_TO_DATE -> {
                    resolvedIds.add(title.titleId)
                    preview.sentHashes[title.titleId]?.let { adoptAgreedBase(title.titleId, it) }
                    continue
                }
                else -> Unit
            }
            if (title.titleId in skip) {
                if (title.titleId in preview.suspectIds) {
                    skippedSuspects.add(title.displayName)
                    skippedSuspectIds.add(title.titleId)
                }
                continue
            }
            if (entry == null) continue
            if (fresh != null) {
                // The re-check couldn't be made (server unreachable): its
                // error is reported, and the title doesn't run.
                if (title.titleId in fresh.failedIds) continue
                if (!stillPlanned(title.action, fresh.actions[title.titleId])) {
                    changed.add(title.displayName)
                    continue
                }
                // An upload that became a rollback suspect since the preview
                // (the save shrank or lost files) is not sent on stale
                // sizes; an included suspect only if it is unchanged.
                val now = fresh.suspects[title.titleId]
                val seen = preview.suspects.firstOrNull { it.titleId == title.titleId }
                if (now != null &&
                    (seen == null || seen.localSize != now.localSize || seen.localFileCount != now.localFileCount)
                ) {
                    changed.add(title.displayName)
                    continue
                }
            }
            try {
                val ok = runPlanned(title, entry)
                if (ok) {
                    if (title.action == PlannedAction.UPLOAD) uploaded++ else downloaded++
                    resolvedIds.add(title.titleId)
                } else {
                    errors.add(
                        if (title.action == PlannedAction.UPLOAD) "Upload failed: ${entry.displayName}"
                        else "Download failed: ${entry.displayName}"
                    )
                }
            } catch (e: Exception) {
                Log.e(TAG, "${title.action} failed for ${title.titleId}", e)
                errors.add(
                    when {
                        title.route == PlanRoute.RAW_CARD -> "Sync error: ${entry.displayName}: ${e.message}"
                        title.action == PlannedAction.UPLOAD -> "Upload error: ${entry.displayName}: ${e.message}"
                        else -> "Download error: ${entry.displayName}: ${e.message}"
                    }
                )
            }
        }
        // Held-back uploads are named in the result; the caller reports them
        // (SyncWorker logs them and leaves a note for the next app open).

        return SyncResult(
            uploaded = uploaded,
            downloaded = downloaded,
            conflicts = conflicts,
            errors = errors,
            conflictTitleIds = conflictIds,
            resolvedTitleIds = resolvedIds,
            conflictInfo = preview.conflictInfo,
            skippedSuspects = skippedSuspects,
            skippedSuspectIds = skippedSuspectIds,
            changedSincePreview = changed,
        )
    }

    /**
     * The server reports [titleId] up to date: [hash] is what both sides
     * agree on, so it is the base.  Recorded when the stored base differs:
     * a restored save carries [LocalBackupStore.RESTORED_BASE] so sync never
     * moves it unasked, and an upload during which the save changed records
     * the copy hashed before it (see [updateSyncState]); once the server
     * matches, the title reads Synced again instead of changed here.
     */
    private suspend fun adoptAgreedBase(titleId: String, hash: String) {
        val row = dao.getById(titleId) ?: return
        if (row.lastSyncedHash == hash || hash.isEmpty()) return
        dao.upsert(row.copy(lastSyncedHash = hash, lastSyncedAt = System.currentTimeMillis()))
    }

    /** Upload or download one planned title; true when it landed. */
    private suspend fun runPlanned(title: PlannedTitle, entry: SaveEntry): Boolean {
        // Hashed just before an upload reads the save: if the emulator writes
        // it during the transfer, this (not the newer copy) is what the
        // server got, and recording the newer one would make the next sync
        // download the server's older copy over it.
        val hashBeforeUpload = if (title.action == PlannedAction.UPLOAD) syncStateHash(entry) else null
        return runPlanned(title, entry, hashBeforeUpload)
    }

    private suspend fun runPlanned(title: PlannedTitle, entry: SaveEntry, hashBeforeUpload: String?): Boolean = when (title.route) {
        PlanRoute.RAW_CARD -> {
            val ok = when (title.action) {
                PlannedAction.UPLOAD -> uploadRawCard(entry)
                else -> downloadRawCard(entry)
            }
            if (!ok) throw IOException(if (title.action == PlannedAction.UPLOAD) "Upload failed" else "Download failed")
            if (title.action == PlannedAction.UPLOAD) updateSyncState(entry, hashBeforeUpload) else recordDownloaded(entry, fromFile = true)
            true
        }
        PlanRoute.SERVER_ONLY -> {
            val ok = downloadSave(entry, entry.titleId)
            if (ok) {
                // The base is what the download wrote.  (Unpredictable shapes
                // re-read the disk: a "server only" entry from the file just
                // written, as its own hash would be empty.)
                recordDownloaded(entry, fromFile = entry.isServerOnly)
            }
            ok
        }
        PlanRoute.SYNC -> {
            val ok = if (title.action == PlannedAction.UPLOAD) uploadSave(entry) else downloadSave(entry, entry.titleId)
            if (ok) {
                if (title.action == PlannedAction.UPLOAD) updateSyncState(entry, hashBeforeUpload)
                else recordDownloaded(entry, fromFile = false)
            }
            ok
        }
    }

    /** The decisions a re-check made, and the routes it couldn't re-check. */
    private class FreshDecisions(
        val actions: Map<String, PlannedAction>,
        /** Titles the re-check couldn't decide (server unreachable): they don't run. */
        val failedIds: Set<String>,
        val errors: List<String>,
        /** Uploads that look like a rollback now, from the sizes on disk now. */
        val suspects: Map<String, SuspectUpload> = emptyMap(),
    )

    /**
     * Decide [titles] again from what is on disk and on the server now.  The
     * /sync request is the same one plan() made, for these titles only
     * (POST /sync changes nothing on the server).
     */
    private suspend fun recheck(titles: List<PlannedTitle>, entries: Map<String, SaveEntry>): FreshDecisions {
        val actions = mutableMapOf<String, PlannedAction>()
        val failed = mutableSetOf<String>()
        val errors = mutableListOf<String>()

        val current = mutableMapOf<String, SaveEntry>()
        val states = mutableMapOf<String, SyncStateEntity?>()
        val sentSize = mutableMapOf<String, Long>()
        val rawMeta = mutableMapOf<String, SaveMeta>()

        val syncRoute = titles.filter { it.route == PlanRoute.SYNC }
        if (syncRoute.isNotEmpty()) {
            val ignored = mutableListOf<String>()
            val request = syncRoute.mapNotNull { t ->
                val entry = entries[t.titleId]?.reconcileWithDisk() ?: return@mapNotNull null
                val state = dao.getById(t.titleId)
                current[t.titleId] = entry
                states[t.titleId] = state
                buildSyncTitle(entry, state, ignored)?.also { sentSize[it.title_id] = it.size }
            }
            try {
                if (request.isNotEmpty()) {
                    val r = api.sync(SyncRequest(titles = request, console_id = consoleId))
                    r.upload.forEach { actions[it] = PlannedAction.UPLOAD }
                    r.download.forEach { actions[it] = PlannedAction.DOWNLOAD }
                    r.conflict.forEach { actions[it] = PlannedAction.CONFLICT }
                    r.up_to_date.forEach { actions[it] = PlannedAction.UP_TO_DATE }
                }
            } catch (e: Exception) {
                Log.e(TAG, "Sync re-check failed", e)
                syncRoute.mapTo(failed) { it.titleId }
                errors.add("Sync failed: ${e.message}")
            }
        }
        for (t in titles.filter { it.route == PlanRoute.SERVER_ONLY }) {
            val entry = entries[t.titleId]?.reconcileWithDisk() ?: continue
            // A save that appeared since the preview may hold new play: it
            // is no longer a plain "new from the server" download.
            actions[t.titleId] = if (entry.exists()) PlannedAction.DOWNLOAD else PlannedAction.DOWNLOAD_NEW
        }
        for (t in titles.filter { it.route == PlanRoute.RAW_CARD }) {
            val entry = entries[t.titleId] ?: continue
            try {
                val (action, meta) = planRawCard(entry)
                action?.let { actions[t.titleId] = it }
                meta?.let { rawMeta[t.titleId] = it }
                current[t.titleId] = entry
            } catch (e: Exception) {
                Log.e(TAG, "Raw-card re-check failed for ${t.titleId}", e)
                failed.add(t.titleId)
                errors.add("Sync error: ${entry.displayName}: ${e.message}")
            }
        }
        val uploadsNow = titles.filter { it.titleId !in failed && actions[it.titleId] == PlannedAction.UPLOAD }
        val (suspects, _) = findSuspectUploads(uploadsNow, current, states, sentSize, rawMeta)
        return FreshDecisions(actions, failed, errors, suspects.associateBy { it.titleId })
    }

    /** The /sync request line for [entry], or null (no local save, or unreadable: noted in [errors]). */
    private fun buildSyncTitle(entry: SaveEntry, lastSyncState: SyncStateEntity?, errors: MutableList<String>): SyncTitle? {
        if (!hasLocalSyncSource(entry, lastSyncState)) return null
        return try {
            SyncTitle(
                title_id = entry.titleId,
                save_hash = canonicalHash(entry, lastSyncState),
                timestamp = localTimestamp(entry) / 1000L,  // convert ms to seconds
                size = canonicalSize(entry, lastSyncState),
                last_synced_hash = lastSyncState?.lastSyncedHash,
                console_id = consoleId
            )
        } catch (e: Exception) {
            Log.e(TAG, "Error computing hash for ${entry.titleId}", e)
            errors.add("Hash error: ${entry.displayName}")
            null
        }
    }

    /**
     * The planned uploads that look like a rollback (see [RollbackCheck]),
     * and how many could not be checked.  Memory cards use the card meta
     * the plan already fetched; the rest ask GET /saves/{id}/meta for at
     * most [RollbackCheck.MAX_SIZE_CHECKS] uploads, a few at a time.  No
     * server copy (404) is never a suspect.
     */
    private suspend fun findSuspectUploads(
        titles: List<PlannedTitle>,
        saveMap: Map<String, SaveEntry>,
        syncStates: Map<String, SyncStateEntity?>,
        sentSize: Map<String, Long>,
        rawCardMeta: Map<String, SaveMeta>,
    ): Pair<List<SuspectUpload>, Int> {
        val uploads = titles.filter { it.action == PlannedAction.UPLOAD }
        if (uploads.isEmpty()) return emptyList<SuspectUpload>() to 0
        fun suspectOf(t: PlannedTitle, meta: SaveMeta): SuspectUpload? {
            val entry = saveMap[t.titleId] ?: return null
            val size = sentSize[t.titleId] ?: runCatching { canonicalSize(entry, syncStates[t.titleId]) }.getOrNull() ?: return null
            val files = localFileCount(entry)
            if (!RollbackCheck.isSuspect(size, files, meta.save_size, meta.file_count)) return null
            return SuspectUpload(t.titleId, t.displayName, size, meta.save_size, files, meta.file_count)
        }

        val suspects = mutableListOf<SuspectUpload>()
        val (rawUploads, syncUploads) = uploads.partition { it.route == PlanRoute.RAW_CARD }
        rawUploads.forEach { t -> rawCardMeta[t.titleId]?.let { suspectOf(t, it) }?.let(suspects::add) }

        val (checked, unchecked) = RollbackCheck.splitForChecks(syncUploads)
        var skipped = unchecked.size
        val gate = Semaphore(RollbackCheck.PARALLEL_CHECKS)
        val results = coroutineScope {
            checked.map { t ->
                async {
                    gate.withPermit {
                        try {
                            MetaCheck.Found(api.getSaveMeta(t.titleId))
                        } catch (e: retrofit2.HttpException) {
                            if (e.code() == 404) MetaCheck.NoServerCopy else MetaCheck.Failed
                        } catch (e: Exception) {
                            Log.w(TAG, "Size check failed for ${t.titleId}", e)
                            MetaCheck.Failed
                        }
                    }
                }
            }.awaitAll()
        }
        checked.zip(results).forEach { (t, r) ->
            when (r) {
                is MetaCheck.Found -> suspectOf(t, r.meta)?.let(suspects::add)
                MetaCheck.NoServerCopy -> Unit
                MetaCheck.Failed -> skipped++
            }
        }
        // Keep the plan's order.
        val order = uploads.withIndex().associate { (i, t) -> t.titleId to i }
        return suspects.sortedBy { order[it.titleId] ?: Int.MAX_VALUE } to skipped
    }

    private sealed class MetaCheck {
        class Found(val meta: SaveMeta) : MetaCheck()
        object NoServerCopy : MetaCheck()
        object Failed : MetaCheck()
    }

    /** Files in a save folder (what the server counts as `file_count`); null for single-file saves. */
    private fun localFileCount(entry: SaveEntry): Int? {
        val dir = entry.saveDir ?: return null
        if (!dir.isDirectory) return null
        return dir.walkTopDown().count { it.isFile }
    }

    private suspend fun normalizeCanonicalSlugEntries(saves: List<SaveEntry>): List<SaveEntry> {
        val canonicalSlugSaves = saves.filter {
            it.titleId.contains('_') && (it.systemName == "PS1" || it.systemName == "3DS")
        }
        if (canonicalSlugSaves.isEmpty()) return saves

        return try {
            val response = api.normalizeRoms(
                NormalizeRequest(
                    roms = canonicalSlugSaves.map {
                        NormalizeRomEntry(system = it.systemName, filename = it.displayName)
                    }
                )
            )
            val serialMap = canonicalSlugSaves.indices.associate { i ->
                val oldId = canonicalSlugSaves[i].titleId
                val newId = response.results.getOrNull(i)?.title_id ?: oldId
                oldId to newId
            }.filter { (old, new) -> old != new && !new.contains('_') }

            if (serialMap.isEmpty()) saves
            else saves.map { entry ->
                val resolved = serialMap[entry.titleId]
                if (resolved != null) entry.copy(titleId = resolved) else entry
            }
        } catch (e: Exception) {
            Log.w(TAG, "normalizeCanonicalSlugEntries failed", e)
            saves
        }
    }

    private fun isSaturnEntry(entry: SaveEntry): Boolean = isSaturnSaveEntry(entry)

    private fun isSharedYabaSanshiroContainer(entry: SaveEntry): Boolean =
        isSharedYabaSanshiroSave(entry, saturnSyncFormat)

    private fun saturnCanonicalSnapshot(
        entry: SaveEntry,
        @Suppress("UNUSED_PARAMETER") syncState: SyncStateEntity? = null
    ): SaturnCanonicalSnapshot? {
        val canonicalBytes = saturnCanonicalBytes(entry, saturnSyncFormat) ?: return null
        return SaturnCanonicalSnapshot(
            bytes = canonicalBytes,
            archiveNames = SaturnSaveFormatConverter.archiveNames(canonicalBytes)
        )
    }

    private fun hasLocalSyncSource(
        entry: SaveEntry,
        @Suppress("UNUSED_PARAMETER") syncState: SyncStateEntity? = null
    ): Boolean {
        if (entry.exists()) return true
        return isSharedYabaSanshiroContainer(entry) &&
            entry.saveFile?.exists() == true &&
            SaturnArchiveStateStore.get(entry.titleId).isNotEmpty()
    }

    private fun canonicalBytes(
        entry: SaveEntry,
        syncState: SyncStateEntity? = null
    ): ByteArray {
        return saturnCanonicalSnapshot(entry, syncState)?.bytes ?: entry.readBytes()
    }

    private fun canonicalHash(
        entry: SaveEntry,
        @Suppress("UNUSED_PARAMETER") syncState: SyncStateEntity? = null
    ): String = localSyncHash(entry, saturnSyncFormat)

    private fun canonicalSize(
        entry: SaveEntry,
        syncState: SyncStateEntity? = null
    ): Long {
        return when {
            isSaturnEntry(entry) -> canonicalBytes(entry, syncState).size.toLong()
            entry.isPspSlot ->
                entry.saveDir!!.walkTopDown().filter { it.isFile }.sumOf { it.length() }
            entry.isMultiFile && entry.saveDir != null ->
                entry.saveDir.walkTopDown().filter { it.isFile }.sumOf { it.length() }
            entry.saveFile != null -> entry.saveFile.length()
            else -> 0L
        }
    }

    private fun localTimestamp(entry: SaveEntry): Long = localSaveTimestamp(entry, saturnSyncFormat)

    private suspend fun writeDownloadedBytes(entry: SaveEntry, canonicalBytes: ByteArray): Boolean {
        val prev = syncStateForBackup(entry)
        return when {
            entry.isMultiFile && entry.saveDir != null -> {
                var written: WrittenSave? = null
                backedUpWrite(backups, entry.titleId, entry.saveDir, prev?.lastSyncedHash, prev?.lastSyncedAt) {
                    AtomicSaveWriter.replaceDirectory(entry.saveDir, keepExisting = true) { staging ->
                        unzipBytesToDirectory(canonicalBytes, staging)
                        written = stagedDirHash(entry, staging)?.let(::WrittenSave)
                    }
                }
                noteWritten(entry, written)
                true
            }
            entry.saveFile != null -> {
                entry.saveFile.parentFile?.mkdirs()
                if (isSharedYabaSanshiroContainer(entry)) {
                    // Read, merged and swapped under the swap lock: another
                    // title's download or restore into the same container
                    // can't land in between and be dropped.
                    var written: WrittenSave? = null
                    backedUpWrite(backups, entry.titleId, entry.saveFile, prev?.lastSyncedHash, prev?.lastSyncedAt) {
                        val merged = AtomicSaveWriter.replaceFileMerging(entry.saveFile) { existing ->
                            SaturnSaveFormatConverter.mergeCanonicalIntoYabaSanshiro(existing, canonicalBytes)
                        }
                        written = writtenFile(entry, merged, downloadedCanonical = canonicalBytes)
                    }
                    noteWritten(entry, written)
                    return true
                }
                val bytesToWrite = if (isSaturnEntry(entry)) {
                    SaturnSaveFormatConverter.fromCanonical(canonicalBytes, saturnSyncFormat)
                } else {
                    canonicalBytes
                }
                val written = writtenFile(entry, bytesToWrite, downloadedCanonical = canonicalBytes)
                backedUpWrite(backups, entry.titleId, entry.saveFile, prev?.lastSyncedHash, prev?.lastSyncedAt) {
                    AtomicSaveWriter.replaceFile(entry.saveFile, bytesToWrite)
                }
                noteWritten(entry, written)
                true
            }
            else -> false
        }
    }

    /**
     * Save archives that travel as a bundle of relative paths: 3DS (Azahar)
     * and Wii U (Cemu).  Both are recursive trees, so the flat PSP slot bundle
     * would lose their directory structure.
     */
    private fun isTreeBundleEntry(entry: SaveEntry): Boolean = entry.isTreeSaveDir

    suspend fun uploadSave(rawEntry: SaveEntry): Boolean {
        // A stale server-only flag would make readBytes()/computeHash() return
        // nothing and upload an empty save.
        val entry = rawEntry.reconcileWithDisk()
        if (isSharedYabaSanshiroContainer(entry)) {
            autoResolveSharedSaturnArchives(entry)
        }
        return if (entry.isPspSlot) {
            uploadPspBundle(entry)
        } else if (isTreeBundleEntry(entry)) {
            uploadTreeBundle(entry)
        } else if (isPs1RawEntry(entry)) {
            uploadPs1Card(entry)
        } else if (isPs2RawEntry(entry)) {
            uploadPs2Card(entry)
        } else if (isGcRawEntry(entry)) {
            uploadGcCard(entry)
        } else {
            uploadSaveRaw(entry)
        }
    }

    private suspend fun uploadTreeBundle(entry: SaveEntry): Boolean {
        val saveDir = entry.saveDir ?: return false
        return try {
            val bundleBytes = BundleUtils.createTreeBundle(entry.titleId, saveDir)
            val requestBody = bundleBytes.toRequestBody("application/octet-stream".toMediaType())
            val response = api.uploadSaveBundle(
                titleId = entry.titleId,
                source = if (entry.systemName == "WIIU") "wiiu_emu" else "3ds_emu",
                force = true,
                consoleId = consoleId,
                // The server names a Wii U save from its 4-char product code
                // (meta.xml), not its title id — without this the stored
                // metadata keeps the raw hex id as the game name.  The
                // meta.xml name rides along for titles the Wii U DAT misses.
                gameCode = entry.gameCode ?: "",
                gameName = entry.displayName.takeIf {
                    entry.systemName == "WIIU" && it != entry.titleId
                } ?: "",
                body = requestBody
            )
            response.status == "ok"
        } catch (e: Exception) {
            Log.e(TAG, "uploadTreeBundle failed for ${entry.titleId}", e)
            false
        }
    }

    private suspend fun uploadPs1Card(entry: SaveEntry): Boolean {
        return try {
            val saveFile = entry.saveFile ?: return false
            val response = api.uploadPs1Card(
                titleId = entry.titleId,
                slot = 0,
                consoleId = consoleId,
                body = saveFile.readBytes().toRequestBody("application/octet-stream".toMediaType())
            )
            response.status == "ok"
        } catch (e: Exception) {
            Log.e(TAG, "uploadPs1Card failed for ${entry.titleId}", e)
            false
        }
    }

    private suspend fun uploadPs2Card(entry: SaveEntry): Boolean {
        return try {
            val saveFile = entry.saveFile ?: return false
            val response = api.uploadPs2Card(
                titleId = entry.titleId,
                format = "ps2",
                consoleId = consoleId,
                body = saveFile.readBytes().toRequestBody("application/octet-stream".toMediaType())
            )
            response.status == "ok"
        } catch (e: Exception) {
            Log.e(TAG, "uploadPs2Card failed for ${entry.titleId}", e)
            false
        }
    }

    private suspend fun uploadSaveRaw(entry: SaveEntry): Boolean {
        return try {
            val syncState = dao.getById(entry.titleId)
            val bytes = canonicalBytes(entry, syncState)
            val requestBody = bytes.toRequestBody("application/octet-stream".toMediaType())
            val response = api.uploadSaveRaw(
                titleId = entry.titleId,
                source = "android",
                consoleId = consoleId,
                body = requestBody
            )
            response.status == "ok"
        } catch (e: Exception) {
            Log.e(TAG, "uploadSaveRaw failed for ${entry.titleId}", e)
            false
        }
    }

    /**
     * Uploads a PSP/PSX slot directory as a bundle v4.
     * Includes ALL files in the slot dir (DATA.BIN, ICON0.PNG, PARAM.SFO, etc.)
     * so the server hash matches the PSP homebrew client's algorithm.
     * Used for both PSP games and PS1 PSone Classics running under PPSSPP.
     */
    private suspend fun uploadPspBundle(entry: SaveEntry): Boolean {
        val slotDir = entry.saveDir ?: return false
        return try {
            val bundleBytes = BundleUtils.createBundle(entry.titleId, slotDir)
            val requestBody = bundleBytes.toRequestBody("application/octet-stream".toMediaType())
            val response = api.uploadSaveBundle(
                titleId = entry.titleId,
                source = "psp_emu",
                force = true,
                consoleId = consoleId,
                body = requestBody
            )
            response.status == "ok"
        } catch (e: Exception) {
            Log.e(TAG, "uploadPspBundle failed for ${entry.titleId}", e)
            false
        }
    }

    suspend fun downloadSave(entry: SaveEntry, titleId: String): Boolean {
        writtenSaves.remove(entry.titleId)
        return if (entry.isPspSlot) {
            downloadPspBundle(entry, titleId)
        } else if (isTreeBundleEntry(entry)) {
            downloadTreeBundle(entry, titleId)
        } else if (entry.systemName == "PS1" && entry.saveFile != null) {
            downloadPs1Card(entry, titleId)
        } else if (entry.systemName == "PS2" && entry.saveFile != null) {
            downloadPs2Card(entry, titleId)
        } else if (entry.systemName == "GC") {
            downloadGcCard(entry, titleId)
        } else {
            downloadSaveRaw(entry, titleId)
        }
    }

    private fun hasLocalSaveTarget(entry: SaveEntry): Boolean {
        return entry.saveFile != null || entry.saveDir != null
    }

    private suspend fun upsertKnownSyncState(
        entry: SaveEntry,
        hash: String,
        saturnArchiveNames: List<String>? = null
    ) {
        val now = System.currentTimeMillis()
        saturnArchiveNames?.let { SaturnArchiveStateStore.put(entry.titleId, it) }
        dao.upsert(
            SyncStateEntity(
                titleId = entry.titleId,
                lastSyncedHash = hash,
                lastSyncedAt = now,
                displayName = entry.displayName,
                systemName = entry.systemName
            )
        )
    }

    /**
     * Download a ROM from the server's ROM catalog.
     * Saves to the ROM scan directory under the appropriate system subfolder.
     * Returns the downloaded File on success.
     *
     * On failure, throws [IOException] with a descriptive message so the
     * caller can surface the real reason to the user (instead of collapsing
     * every failure to a generic "not found"). Network/timeout exceptions
     * are re-thrown verbatim so the UI layer can tell "server took too
     * long" apart from "server said no".
     */
    @Throws(IOException::class)
    suspend fun downloadRom(
        romId: String,
        system: String,
        romScanDir: String,
        expectedFilename: String? = null,
        romDirOverrides: Map<String, String> = emptyMap(),
        extractFormat: String? = null,
        cdGamesPerContentFolder: Boolean = false,
    ): File {
        val response = try {
            api.downloadRom(romId, extract = extractFormat)
        } catch (e: IOException) {
            Log.e(TAG, "downloadRom network error for $romId", e)
            throw e
        }
        if (!response.isSuccessful) {
            val code = response.code()
            val detail = runCatching { response.errorBody()?.string() }.getOrNull()
                ?.trim()
                ?.takeIf { it.isNotEmpty() }
            val message = when (code) {
                404 -> detail ?: "ROM not found on server"
                503 -> detail ?: "Server-side conversion is not configured (HTTP 503)"
                504 -> detail ?: "Server-side conversion timed out (HTTP 504)"
                500 -> detail ?: "Server-side conversion failed (HTTP 500)"
                else -> "HTTP $code" + (detail?.let { ": $it" } ?: "")
            }
            Log.e(TAG, "ROM download failed for $romId: $message")
            throw IOException(message)
        }
        val body = response.body()
            ?: throw IOException("Server returned empty response for ROM $romId")

        val rawFilename = expectedFilename
            ?: response.headers()["Content-Disposition"]
                ?.let { cd ->
                    val match = Regex("filename=\"?([^\"]+)\"?").find(cd)
                    match?.groupValues?.get(1)
                }
            ?: "$romId.rom"
        // Server/header-supplied name: collapse to a bare basename so a
        // crafted Content-Disposition (e.g. "../../foo") can't escape the
        // target ROM directory.
        val filename = sanitizeDownloadName(rawFilename) ?: "$romId.rom"

        // Delegate folder selection to the shared helper so the download
        // path, the installed-ROMs scanner, and the Steam Deck client all
        // agree on where an incoming ROM lands.  The helper canonicalises
        // alias codes (``SCD`` → ``SEGACD``, ``GEN`` → ``MD``, …) before
        // picking a candidate, which fixes the regression where catalog
        // downloads for Sega CD landed in ``roms/SCD/`` instead of the
        // user's existing ``roms/segacd/``.
        val outDir = InstalledRomsScanner.resolveRomTargetDir(
            scanRoot = File(romScanDir),
            system = system,
            romDirOverrides = romDirOverrides,
        )
        outDir.mkdirs()

        // CD-ROM style games (PS1 multi-track .cue+.bin, Saturn,
        // Dreamcast, Sega CD) CAN live in a per-game subfolder by
        // convention so the data track, cue sheet and any companion
        // files stay grouped.  Whether to do that is the user's choice
        // — gated on [cdGamesPerContentFolder] from settings, mirroring
        // the same toggle that controls SAVE-folder layout.  When off,
        // CD ROMs land flat in ``<outDir>/<filename>``.
        val canonicalSystem = SystemAliases.normalizeSystemCode(system)
        val finalDir = if (cdGamesPerContentFolder && canonicalSystem in CD_FOLDER_SYSTEMS) {
            val stem = filename.substringBeforeLast('.', filename)
            File(outDir, stem).also { it.mkdirs() }
        } else {
            outDir
        }
        val outFile = File(finalDir, filename)

        try {
            body.byteStream().use { input ->
                outFile.outputStream().use { output ->
                    val buffer = ByteArray(ROM_BUFFER_SIZE)
                    var bytesRead: Int
                    while (input.read(buffer).also { bytesRead = it } != -1) {
                        output.write(buffer, 0, bytesRead)
                    }
                }
            }
        } catch (e: IOException) {
            Log.e(TAG, "downloadRom write/stream error for $romId", e)
            runCatching { outFile.delete() }
            throw e
        }

        Log.i(TAG, "ROM downloaded: ${outFile.absolutePath} (${outFile.length()} bytes)")
        return outFile
    }

    private suspend fun downloadPs1Card(entry: SaveEntry, titleId: String): Boolean {
        return try {
            val saveFile = entry.saveFile ?: return false
            val response = api.downloadPs1Card(titleId, slot = 0)
            if (!response.isSuccessful) {
                val message = response.errorBody()?.string()?.let(::extractErrorDetail)
                    ?: "Download failed (${response.code()})"
                Log.e(TAG, "PS1 card download HTTP error for $titleId slot0: $message")
                throw IOException(message)
            }
            val body = response.body() ?: return false
            val bytes = body.bytes()
            val prev = syncStateForBackup(entry)
            val written = writtenFile(entry, bytes)
            backedUpWrite(backups, entry.titleId, saveFile, prev?.lastSyncedHash, prev?.lastSyncedAt) {
                AtomicSaveWriter.replaceFile(saveFile, bytes)
            }
            noteWritten(entry, written)
            true
        } catch (e: BackupFailedException) {
            throw e
        } catch (e: Exception) {
            Log.e(TAG, "downloadPs1Card failed for $titleId", e)
            false
        }
    }

    private suspend fun downloadPs2Card(entry: SaveEntry, titleId: String): Boolean {
        return try {
            val saveFile = entry.saveFile ?: return false
            val response = api.downloadPs2Card(titleId, format = "ps2")
            if (!response.isSuccessful) {
                val message = response.errorBody()?.string()?.let(::extractErrorDetail)
                    ?: "Download failed (${response.code()})"
                Log.e(TAG, "PS2 card download HTTP error for $titleId: $message")
                throw IOException(message)
            }
            val body = response.body() ?: return false
            val bytes = body.bytes()
            val prev = syncStateForBackup(entry)
            val written = writtenFile(entry, bytes)
            backedUpWrite(backups, entry.titleId, saveFile, prev?.lastSyncedHash, prev?.lastSyncedAt) {
                AtomicSaveWriter.replaceFile(saveFile, bytes)
            }
            noteWritten(entry, written)
            true
        } catch (e: BackupFailedException) {
            throw e
        } catch (e: Exception) {
            Log.e(TAG, "downloadPs2Card failed for $titleId", e)
            false
        }
    }

    private suspend fun downloadSaveRaw(entry: SaveEntry, titleId: String): Boolean {
        val response = api.downloadSaveRaw(titleId)
        if (!response.isSuccessful) {
            val message = response.errorBody()?.string()?.let(::extractErrorDetail)
                ?: "Download failed (${response.code()})"
            Log.e(TAG, "Download HTTP error for $titleId: $message")
            throw IOException(message)
        }
        return try {
            val body = response.body() ?: return false
            val bytes = body.bytes()
            val wrote = writeDownloadedBytes(entry, bytes)
            if (wrote && isSaturnEntry(entry)) {
                val serverHash = response.headers()["X-Save-Hash"]
                    ?: HashUtils.sha256Bytes(bytes)
                val archiveNames = SaturnSaveFormatConverter.archiveNames(bytes)
                upsertKnownSyncState(entry, serverHash, archiveNames)
            }
            wrote
        } catch (e: BackupFailedException) {
            throw e
        } catch (e: Exception) {
            Log.e(TAG, "downloadSaveRaw failed for $titleId", e)
            false
        }
    }

    /**
     * Downloads a PPSSPP save as a bundle v4 and extracts all files to the slot directory.
     * The slot directory is created if it doesn't exist (server-only saves).
     */
    private suspend fun downloadPspBundle(entry: SaveEntry, titleId: String): Boolean {
        val slotDir = entry.saveDir ?: return false
        val response = api.downloadSaveBundle(titleId)
        if (!response.isSuccessful) {
            val message = response.errorBody()?.string()?.let(::extractErrorDetail)
                ?: "Download failed (${response.code()})"
            Log.e(TAG, "Download bundle HTTP error for $titleId: $message")
            throw IOException(message)
        }
        return try {
            val bytes = response.body()?.bytes() ?: return false
            val files = BundleUtils.parseBundle(bytes)
            if (files.isEmpty()) return false
            // Overlay onto the existing slot (files the bundle doesn't carry
            // are kept, as before), but swap the result in atomically.
            val prev = syncStateForBackup(entry)
            var written: WrittenSave? = null
            backedUpWrite(backups, entry.titleId, slotDir, prev?.lastSyncedHash, prev?.lastSyncedAt) {
                AtomicSaveWriter.replaceDirectory(slotDir, files, keepExisting = true) { staged ->
                    written = stagedDirHash(entry, staged)?.let(::WrittenSave)
                }
            }
            noteWritten(entry, written)
            true
        } catch (e: BackupFailedException) {
            throw e
        } catch (e: Exception) {
            Log.e(TAG, "downloadPspBundle failed for $titleId", e)
            false
        }
    }

    private suspend fun downloadTreeBundle(entry: SaveEntry, titleId: String): Boolean {
        val saveDir = entry.saveDir ?: return false
        val response = api.downloadSaveBundle(titleId)
        if (!response.isSuccessful) {
            val message = response.errorBody()?.string()?.let(::extractErrorDetail)
                ?: "Download failed (${response.code()})"
            Log.e(TAG, "Download tree bundle HTTP error for $titleId: $message")
            throw IOException(message)
        }
        return try {
            val bytes = response.body()?.bytes() ?: return false
            val files = BundleUtils.parseBundle(bytes)
            if (files.isEmpty()) return false
            // The bundle is the whole save: write it next to the current one,
            // verify it, then swap.  The old save is only removed once the new
            // one is in place, so a failure never leaves a half-written save.
            val prev = syncStateForBackup(entry)
            var written: WrittenSave? = null
            backedUpWrite(backups, entry.titleId, saveDir, prev?.lastSyncedHash, prev?.lastSyncedAt) {
                AtomicSaveWriter.replaceDirectory(saveDir, files) { staged ->
                    written = stagedDirHash(entry, staged)?.let(::WrittenSave)
                }
            }
            noteWritten(entry, written)
            true
        } catch (e: BackupFailedException) {
            throw e
        } catch (e: Exception) {
            Log.e(TAG, "downloadTreeBundle failed for $titleId", e)
            false
        }
    }

    suspend fun recordSyncedState(entry: SaveEntry, hashBeforeUpload: String? = null) {
        updateSyncState(entry.reconcileWithDisk(), hashBeforeUpload)
    }

    /** The hash sync state would record for [entry] as it is on disk now; null if it can't be read. */
    suspend fun syncStateHash(entry: SaveEntry): String? = runCatching {
        val e = entry.reconcileWithDisk()
        saturnCanonicalSnapshot(e)?.let { HashUtils.sha256Bytes(it.bytes) } ?: e.computeHash()
    }.getOrNull()?.takeIf { it.isNotEmpty() }

    /** Records [entry] as synced after [downloadSave] landed: see [recordDownloaded]. */
    suspend fun recordDownloadedState(entry: SaveEntry) {
        recordDownloaded(entry, fromFile = true)
    }

    /**
     * Records [entry] as synced after a download.  The base is what the
     * download wrote, hashed as [localSyncHash] hashes the save, not the
     * save re-read now: the emulator may have written it since the swap,
     * and with that copy as the base the next sync would see "only the
     * server changed" and download over the new play.  A shape the download
     * couldn't hash as written falls back to reading the disk ([fromFile]:
     * [updateSyncStateFromFile], else [updateSyncState]).
     */
    private suspend fun recordDownloaded(entry: SaveEntry, fromFile: Boolean) {
        beforeDownloadRecorded(entry)
        val written = writtenSaves.remove(entry.titleId)
        if (written == null) {
            if (fromFile) updateSyncStateFromFile(entry) else updateSyncState(entry)
            return
        }
        try {
            written.saturnArchiveNames?.let { SaturnArchiveStateStore.put(entry.titleId, it) }
            dao.upsert(
                SyncStateEntity(
                    titleId = entry.titleId,
                    lastSyncedHash = written.hash,
                    lastSyncedAt = System.currentTimeMillis(),
                    displayName = entry.displayName,
                    systemName = entry.systemName
                )
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to update sync state for ${entry.titleId}", e)
        }
    }

    /**
     * Records [entry] as synced.  [hashBeforeUpload] is the save's hash just
     * before it was uploaded: when the save has changed since (the emulator
     * wrote it during the transfer), that copy is the one the server got,
     * so it is the base, and the next sync uploads the newer play (or finds
     * the server already has it) rather than downloading over it.
     */
    private suspend fun updateSyncState(entry: SaveEntry, hashBeforeUpload: String? = null) {
        try {
            val saturnSnapshot = saturnCanonicalSnapshot(entry)
            val onDisk = saturnSnapshot?.let { HashUtils.sha256Bytes(it.bytes) } ?: entry.computeHash()
            val hash = if (hashBeforeUpload != null && hashBeforeUpload != onDisk) hashBeforeUpload else onDisk
            saturnSnapshot?.archiveNames?.let { SaturnArchiveStateStore.put(entry.titleId, it) }
            val now = System.currentTimeMillis()
            dao.upsert(
                SyncStateEntity(
                    titleId = entry.titleId,
                    lastSyncedHash = hash,
                    lastSyncedAt = now,
                    displayName = entry.displayName,
                    systemName = entry.systemName
                )
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to update sync state for ${entry.titleId}", e)
        }
    }

    /**
     * Like [updateSyncState] but reads the hash from disk rather than from the
     * entry object. Used after downloading a server-only save, because
     * [SaveEntry.computeHash] returns "" for entries marked [SaveEntry.isServerOnly].
     */
    private suspend fun updateSyncStateFromFile(entry: SaveEntry) {
        try {
            val saturnSnapshot = saturnCanonicalSnapshot(entry)
            val hash = when {
                // PSP/PSX slot: hash = sha256(all files sorted by name, data only, no paths)
                entry.isPspSlot && entry.saveDir?.exists() == true ->
                    HashUtils.sha256DirFiles(entry.saveDir)
                isTreeBundleEntry(entry) && entry.saveDir?.exists() == true ->
                    HashUtils.sha256DirTreeFiles(entry.saveDir)
                saturnSnapshot != null ->
                    HashUtils.sha256Bytes(saturnSnapshot.bytes)
                entry.saveFile?.exists() == true && entry.systemName == "PS2" ->
                    Ps2CardEcc.normalizedHash(entry.saveFile) ?: HashUtils.sha256File(entry.saveFile)
                entry.saveFile?.exists() == true -> HashUtils.sha256File(entry.saveFile)
                entry.saveDir?.exists() == true  -> HashUtils.sha256Dir(entry.saveDir)
                else -> return  // nothing to record
            }
            saturnSnapshot?.archiveNames?.let { SaturnArchiveStateStore.put(entry.titleId, it) }
            val now = System.currentTimeMillis()
            dao.upsert(
                SyncStateEntity(
                    titleId = entry.titleId,
                    lastSyncedHash = hash,
                    lastSyncedAt = now,
                    displayName = entry.displayName,
                    systemName = entry.systemName
                )
            )
        } catch (e: Exception) {
            Log.e(TAG, "Failed to update sync state (from file) for ${entry.titleId}", e)
        }
    }

    private fun unzipBytesToDirectory(bytes: ByteArray, targetDir: java.io.File) {
        val bais = java.io.ByteArrayInputStream(bytes)
        val zis = java.util.zip.ZipInputStream(bais)
        var entry = zis.nextEntry
        while (entry != null) {
            val outFile = safeChildFile(targetDir, entry.name)
            if (outFile == null) {
                Log.w(TAG, "Rejected unsafe zip entry name '${entry.name}'")
            } else if (entry.isDirectory) {
                outFile.mkdirs()
            } else {
                outFile.parentFile?.mkdirs()
                outFile.outputStream().use { os ->
                    zis.copyTo(os)
                }
            }
            zis.closeEntry()
            entry = zis.nextEntry
        }
        zis.close()
    }

    /**
     * Resolves [name] under [baseDir] and returns the [File] only if it stays
     * within [baseDir] (defends against path traversal / Zip-Slip from
     * server-controlled bundle and archive entry names). Returns null when the
     * resolved path would escape the directory.
     */
    /**
     * Reduces a server/header-supplied filename to a single safe path segment
     * (strips any directory components and rejects ``.``/``..``). Returns null
     * if nothing usable remains.
     */
    private fun sanitizeDownloadName(name: String): String? {
        val base = name.replace('\\', '/').substringAfterLast('/').trim()
        return if (base.isEmpty() || base == "." || base == "..") null else base
    }

    private fun safeChildFile(baseDir: File, name: String): File? {
        val target = File(baseDir, name)
        val basePath = baseDir.canonicalPath
        val targetPath = target.canonicalPath
        return if (targetPath == basePath || targetPath.startsWith(basePath + File.separator)) {
            target
        } else {
            null
        }
    }

    private fun extractErrorDetail(body: String): String {
        return Regex("\"detail\"\\s*:\\s*\"([^\"]+)\"")
            .find(body)
            ?.groupValues
            ?.getOrNull(1)
            ?.replace("\\n", "\n")
            ?: body
    }

    private fun isPs1RawEntry(entry: SaveEntry): Boolean {
        return entry.systemName == "PS1" && entry.saveFile != null && !entry.isPspSlot
    }

    private fun isPs2RawEntry(entry: SaveEntry): Boolean {
        return entry.systemName == "PS2" && entry.saveFile != null
    }

    private fun isGcRawEntry(entry: SaveEntry): Boolean {
        return entry.systemName == "GC"
    }

    private suspend fun uploadGcCard(entry: SaveEntry): Boolean {
        return try {
            val saveFile = entry.saveFile ?: return false
            val bytes = saveFile.readBytes()
            val requestBody = bytes.toRequestBody("application/octet-stream".toMediaType())
            val response = api.uploadGcCard(
                titleId = entry.titleId,
                format = "gci",
                consoleId = consoleId,
                body = requestBody
            )
            response.status == "ok"
        } catch (e: Exception) {
            Log.e(TAG, "uploadGcCard failed for ${entry.titleId}", e)
            false
        }
    }

    private suspend fun downloadGcCard(entry: SaveEntry, titleId: String): Boolean {
        return try {
            val saveFile = entry.saveFile ?: return false
            val response = api.downloadGcCard(titleId, format = "gci")
            if (!response.isSuccessful) {
                val message = response.errorBody()?.string()?.let(::extractErrorDetail)
                    ?: "Download failed (${response.code()})"
                Log.e(TAG, "downloadGcCard error for $titleId: $message")
                return false
            }
            val bytes = response.body()?.bytes() ?: return false
            val prev = syncStateForBackup(entry)
            val written = writtenFile(entry, bytes)
            backedUpWrite(backups, entry.titleId, saveFile, prev?.lastSyncedHash, prev?.lastSyncedAt) {
                AtomicSaveWriter.replaceFile(saveFile, bytes)
            }
            noteWritten(entry, written)
            true
        } catch (e: BackupFailedException) {
            throw e
        } catch (e: Exception) {
            Log.e(TAG, "downloadGcCard failed for $titleId", e)
            false
        }
    }

    /**
     * A memory card's plan from its card meta (see [decideRawCard]), with
     * that meta (null when the server has no card) for the size check.
     */
    private suspend fun planRawCard(entry: SaveEntry): Pair<PlannedAction?, SaveMeta?> {
        val localExists = entry.exists()
        val localHash = if (localExists) canonicalHash(entry) else ""
        val lastSyncHash = dao.getById(entry.titleId)?.lastSyncedHash

        val serverMeta = try {
            when (entry.systemName) {
                "PS1" -> api.getPs1CardMeta(entry.titleId, slot = 0)
                "PS2" -> api.getPs2CardMeta(entry.titleId, format = "ps2")
                "GC"  -> api.getGcCardMeta(entry.titleId)
                else -> null
            }
        } catch (e: retrofit2.HttpException) {
            if (e.code() == 404) null else throw e
        }
        return decideRawCard(
            localExists = localExists,
            localHash = localHash,
            lastSyncedHash = lastSyncHash,
            serverHash = serverMeta?.save_hash,
            serverHasSave = serverMeta != null,
        ) to serverMeta
    }

    private suspend fun uploadRawCard(entry: SaveEntry): Boolean = when (entry.systemName) {
        "PS1" -> uploadPs1Card(entry)
        "PS2" -> uploadPs2Card(entry)
        "GC"  -> uploadGcCard(entry)
        else -> false
    }

    private suspend fun downloadRawCard(entry: SaveEntry): Boolean = when (entry.systemName) {
        "PS1" -> downloadPs1Card(entry, entry.titleId)
        "PS2" -> downloadPs2Card(entry, entry.titleId)
        "GC"  -> downloadGcCard(entry, entry.titleId)
        else -> false
    }

    companion object {
        /**
         * A preview older than this is re-checked before it runs: the user
         * has been reading the dialog, and the saves may have moved on.
         */
        const val FRESH_PREVIEW_MS = 5_000L
    }
}

/** A Saturn save held as one native backup file (not a PSP slot or a folder). */
internal fun isSaturnSaveEntry(entry: SaveEntry): Boolean =
    entry.systemName == "SAT" && entry.saveFile != null && !entry.isMultiFile && !entry.isPspSlot

/** YabaSanshiro's shared `backup.bin`, which holds many games' saves at once. */
internal fun isSharedYabaSanshiroSave(entry: SaveEntry, saturnSyncFormat: SaturnSyncFormat): Boolean =
    isSaturnSaveEntry(entry) &&
        saturnSyncFormat == SaturnSyncFormat.YABASANSHIRO &&
        entry.saveFile?.name.equals("backup.bin", ignoreCase = true)

/**
 * The canonical bytes sync sends for a Saturn save, or null when [entry] is
 * not one.  A shared YabaSanshiro container yields only the archives chosen
 * for this title and throws until that choice exists.
 */
internal fun saturnCanonicalBytes(entry: SaveEntry, saturnSyncFormat: SaturnSyncFormat): ByteArray? {
    if (!isSaturnSaveEntry(entry)) return null
    val saveFile = entry.saveFile ?: return null
    return if (isSharedYabaSanshiroSave(entry, saturnSyncFormat)) {
        val archiveNames = SaturnArchiveStateStore.get(entry.titleId)
        if (archiveNames.isEmpty()) {
            throw UnsupportedOperationException(
                "This YabaSanshiro save still needs Saturn archive selection metadata."
            )
        }
        SaturnSaveFormatConverter.extractCanonical(saveFile.readBytes(), archiveNames)
    } else {
        SaturnSaveFormatConverter.toCanonical(saveFile.readBytes())
    }
}

/**
 * When [entry] last changed, in ms: the `timestamp` sync sends.  The newest
 * file of a PSP slot, of saveFile + extraFiles, or under a multi-file folder
 * (for Azahar, the newest file under data/00000001/) — see
 * [SaveEntry.getTimestamp] — except YabaSanshiro's shared container, whose
 * own mtime is used.  Reads the disk.
 */
fun localSaveTimestamp(entry: SaveEntry, saturnSyncFormat: SaturnSyncFormat): Long = when {
    isSharedYabaSanshiroSave(entry, saturnSyncFormat) && entry.saveFile?.exists() == true ->
        entry.saveFile.lastModified()
    else -> entry.getTimestamp()
}

/**
 * The hash sync compares for [entry]: what SyncEngine sends as `save_hash`
 * and records as `lastSyncedHash`.  Saturn saves hash in canonical form, so a
 * raw file hash would never match the recorded one.  Anything that shows a
 * save's status must use this, or the status disagrees with what sync does.
 * May throw (unreadable file, unparsable Saturn save).
 */
fun localSyncHash(entry: SaveEntry, saturnSyncFormat: SaturnSyncFormat): String =
    saturnCanonicalBytes(entry, saturnSyncFormat)?.let { HashUtils.sha256Bytes(it) }
        ?: entry.computeHash()

/** What a download wrote: the sync base to record, and a Saturn save's archive names. */
internal class WrittenSave(val hash: String, val saturnArchiveNames: List<String>? = null)

/**
 * [localSyncHash] of [entry] once its save file holds [written] (companion
 * files as they are on disk), computed from the bytes rather than the disk.
 * A shared YabaSanshiro container hashes this title's part, picked by the
 * archives in [downloadedCanonical] (the server's copy), as the archive
 * store will then hold.  Null for a shape whose hash isn't the save file's
 * (a save folder), or a shared container with no archives to pick.
 * May throw (unparsable Saturn bytes, unreadable companion file).
 */
internal fun writtenFileSave(
    entry: SaveEntry,
    written: ByteArray,
    saturnSyncFormat: SaturnSyncFormat,
    downloadedCanonical: ByteArray? = null,
): WrittenSave? {
    val saveFile = entry.saveFile ?: return null
    if (isSaturnSaveEntry(entry)) {
        val canonical = if (isSharedYabaSanshiroSave(entry, saturnSyncFormat)) {
            val names = downloadedCanonical?.let { SaturnSaveFormatConverter.archiveNames(it) }.orEmpty()
            if (names.isEmpty()) return null
            SaturnSaveFormatConverter.extractCanonical(written, names)
        } else {
            SaturnSaveFormatConverter.toCanonical(written)
        }
        return WrittenSave(HashUtils.sha256Bytes(canonical), SaturnSaveFormatConverter.archiveNames(canonical))
    }
    // computeHash() hashes a save folder ahead of the file for these.
    if (entry.saveDir != null) return null
    val hash = when {
        entry.extraFiles.isNotEmpty() -> {
            val digest = java.security.MessageDigest.getInstance("SHA-256")
            (listOf(saveFile) + entry.extraFiles)
                .filter { it == saveFile || it.exists() }
                .sortedBy { it.name }
                .forEach { f -> if (f == saveFile) digest.update(written) else digest.update(f.readBytes()) }
            digest.digest().joinToString("") { "%02x".format(it) }
        }
        entry.systemName == "PS2" -> Ps2CardEcc.normalizedHash(written) ?: HashUtils.sha256Bytes(written)
        else -> HashUtils.sha256Bytes(written)
    }
    return WrittenSave(hash)
}

/**
 * [SaveEntry.computeHash] of [entry]'s save folder, taken from [staged]: the
 * folder about to be swapped in.  Null when the folder isn't what the hash
 * covers.
 */
internal fun stagedDirHash(entry: SaveEntry, staged: File): String? = when {
    entry.isPspSlot -> HashUtils.sha256DirFiles(staged)
    entry.isTreeSaveDir -> HashUtils.sha256DirTreeFiles(staged)
    entry.saveFile != null -> null
    entry.isMultiFile -> HashUtils.sha256Dir(staged)
    else -> null
}

private const val ROM_BUFFER_SIZE = 8192
