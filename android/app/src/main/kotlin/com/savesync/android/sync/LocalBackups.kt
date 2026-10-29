package com.savesync.android.sync

import android.content.Context
import com.google.gson.Gson
import com.savesync.android.storage.SyncStateDao
import com.savesync.android.storage.SyncStateEntity
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * A snapshot of a save taken before a download replaced it.
 *
 * [previousLastSyncedHash] / [previousLastSyncedAt] are the title's sync
 * state at snapshot time, kept for reference.  A restore does not put that
 * hash back: after a normal download it equals the restored save's own hash,
 * which the server reads as "only the server changed" and downloads over the
 * restore again.  See [LocalBackupStore.restore].
 */
data class LocalBackup(
    val dir: File,
    val titleId: String,
    val takenAt: Long,
    val reason: String,
    val previousLastSyncedHash: String?,
    val previousLastSyncedAt: Long?,
    /** The copy inside [dir]: the saved file, or the saved folder. */
    val payload: File,
    val isDirectory: Boolean,
)

/** A snapshot couldn't be taken, so the save was not overwritten. */
class BackupFailedException(cause: Throwable? = null) :
    IOException("Couldn't back up the current save — not overwritten", cause)

/** On-disk form of a snapshot's `snapshot.json`. */
private data class SnapshotMeta(
    val takenAt: Long = 0L,
    val reason: String = "",
    val previousLastSyncedHash: String? = null,
    val previousLastSyncedAt: Long? = null,
    val name: String = "",
    val isDirectory: Boolean = false,
)

/**
 * Local copies of saves taken before anything overwrites them (FORK.md:
 * local copies before overwriting).  Each title keeps its newest [keep]
 * snapshots under `<root>/<titleId>/<yyyyMMdd-HHmmss>/`: the copied file or
 * folder plus a `snapshot.json`.  A snapshot is built under a `.partial`
 * name and renamed into place once complete, so an interrupted copy never
 * counts as a backup.  Plain JVM code: unit-tested without Android.
 */
class LocalBackupStore(
    val root: File,
    private val keep: Int = LocalBackups.KEEP,
    private val clock: () -> Long = System::currentTimeMillis,
) {
    private val gson = Gson()

    private fun titleDir(titleId: String) = File(root, safeName(titleId))

    /**
     * Copies [target] (a file or a folder) into a new snapshot and prunes the
     * title to the newest [keep].  Returns the snapshot folder, or null when
     * there is nothing to keep (no target yet: a first download).
     *
     * When the newest snapshot (same [reason]) already holds exactly what
     * [target] holds — a download that failed after its snapshot and is
     * retried every auto sync — that snapshot is renewed instead of copied
     * again, so repeats can't push older, different copies out.
     *
     * The copy is flushed to disk before this returns: the write that
     * follows is fsynced, and must never become durable before its backup.
     * @throws BackupFailedException when the copy can't be made; the caller
     *   must then not overwrite [target].
     */
    fun snapshot(
        titleId: String,
        target: File,
        reason: String,
        previousLastSyncedHash: String?,
        previousLastSyncedAt: Long? = null,
        /** A snapshot pruning must not remove (the one a restore reads from). */
        protect: File? = null,
    ): File? {
        val isDir = target.isDirectory
        val hasData = if (isDir) target.walkTopDown().any { it.isFile } else target.isFile
        if (!hasData) return null

        val now = clock()
        val parent = titleDir(titleId)
        var partial: File? = null
        try {
            if (!parent.isDirectory && !parent.mkdirs()) throw IOException("Could not create $parent")
            val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date(now))
            var name = stamp
            var n = 2
            while (File(parent, name).exists() || File(parent, "$name$PARTIAL").exists()) name = "$stamp-${n++}"
            val done = File(parent, name)

            val newest = latest(titleId)
            if (newest != null && newest.reason == reason && newest.isDirectory == isDir &&
                sameContent(newest.payload, target, isDir)
            ) {
                val meta = SnapshotMeta(now, reason, previousLastSyncedHash, previousLastSyncedAt, newest.payload.name, isDir)
                writeSynced(File(newest.dir, META), gson.toJson(meta))
                if (!newest.dir.renameTo(done)) throw IOException("Could not renew snapshot ${newest.dir}")
                return done
            }

            val work = File(parent, "$name$PARTIAL").also { partial = it }
            if (!work.mkdirs()) throw IOException("Could not create $work")
            val payloadName = payloadNameFor(target)
            val copy = File(work, payloadName)
            if (isDir) {
                if (!target.copyRecursively(copy, overwrite = true)) throw IOException("Could not copy $target")
            } else {
                target.copyTo(copy, overwrite = true)
            }
            copy.walkTopDown().filter { it.isFile }.forEach(::syncFile)
            val meta = SnapshotMeta(now, reason, previousLastSyncedHash, previousLastSyncedAt, payloadName, isDir)
            writeSynced(File(work, META), gson.toJson(meta))
            if (!work.renameTo(done)) throw IOException("Could not finish snapshot $done")
            partial = null
            prune(titleId, protect)
            return done
        } catch (e: Exception) {
            partial?.deleteRecursively()
            throw if (e is BackupFailedException) e else BackupFailedException(e)
        }
    }

    /** The title's snapshots, newest first. */
    fun list(titleId: String): List<LocalBackup> {
        val dirs = titleDir(titleId).listFiles { f -> f.isDirectory && !f.name.endsWith(PARTIAL) }
            ?: return emptyList()
        return dirs.mapNotNull { read(titleId, it) }
            .sortedWith(compareByDescending<LocalBackup> { it.takenAt }.thenByDescending { it.dir.name })
    }

    /** The newest snapshot of [titleId], or null. */
    fun latest(titleId: String): LocalBackup? = list(titleId).firstOrNull()

    /**
     * Puts [backup] (default: the newest) back over [target] through
     * [AtomicSaveWriter], then sets the title's last-synced hash to
     * [RESTORED_BASE], a value no real save hashes to.
     *
     * Neither the restored file's hash nor the base it had at snapshot time
     * would hold the restore: the snapshot's base equals the restored save's
     * hash after every "only the server changed" download, and no base at all
     * is the server's "no history → download" case, so the next sync (or the
     * background worker) would download the server copy straight back over
     * it.  With a base that matches neither side the server reports a
     * conflict (or up to date, if the restored save happens to be the
     * server's), and nothing moves until the user picks Force Upload or Force
     * Download.
     *
     * The current save is snapshotted first, so a restore can itself be
     * undone.  Returns false when there is nothing to restore.
     *
     * [merge], for a single-file save, builds what is written from the
     * snapshot's bytes and the current file's (null = none): a container
     * shared by several titles (YabaSanshiro's backup.bin) must get back only
     * this title's part, or every other game in it would roll back too and
     * be uploaded over the server as "only this device changed".
     * @throws BackupFailedException when the current save can't be kept.
     */
    suspend fun restore(
        titleId: String,
        target: File,
        dao: SyncStateDao,
        backup: LocalBackup? = latest(titleId),
        displayName: String = titleId,
        systemName: String = "",
        merge: ((snapshot: ByteArray, current: ByteArray?) -> ByteArray)? = null,
    ): Boolean {
        val chosen = backup ?: return false
        if (!chosen.payload.exists()) return false
        val current = dao.getById(titleId)
        snapshot(titleId, target, REASON_BEFORE_RESTORE, current?.lastSyncedHash, current?.lastSyncedAt, protect = chosen.dir)

        if (chosen.isDirectory) {
            AtomicSaveWriter.replaceDirectory(target, keepExisting = false) { staging ->
                chosen.payload.listFiles()?.forEach { child ->
                    if (!child.copyRecursively(File(staging, child.name), overwrite = true)) {
                        throw IOException("Could not copy $child")
                    }
                }
            }
        } else {
            val bytes = chosen.payload.readBytes()
            val toWrite = merge?.invoke(bytes, target.takeIf { it.isFile }?.readBytes()) ?: bytes
            AtomicSaveWriter.replaceFile(target, toWrite)
        }

        dao.upsert(
            SyncStateEntity(
                titleId = titleId,
                lastSyncedHash = RESTORED_BASE,
                lastSyncedAt = chosen.previousLastSyncedAt ?: current?.lastSyncedAt ?: 0L,
                displayName = current?.displayName ?: displayName,
                systemName = current?.systemName ?: systemName,
            )
        )
        return true
    }

    /** Flushes [file]'s data to the storage device. */
    private fun syncFile(file: File) {
        FileOutputStream(file, true).use { it.fd.sync() }
    }

    private fun writeSynced(file: File, text: String) {
        FileOutputStream(file).use { out ->
            out.write(text.toByteArray(Charsets.UTF_8))
            out.flush()
            out.fd.sync()
        }
    }

    /** Whether snapshot [payload] holds byte-for-byte what [target] holds now. */
    private fun sameContent(payload: File, target: File, isDir: Boolean): Boolean {
        if (!isDir) return payload.isFile && filesEqual(payload, target)
        if (!payload.isDirectory) return false
        fun filesOf(root: File) = root.walkTopDown().filter { it.isFile }.associateBy { it.relativeTo(root).path }
        val kept = filesOf(payload)
        val now = filesOf(target)
        return kept.keys == now.keys && kept.all { (path, f) -> filesEqual(f, now.getValue(path)) }
    }

    private fun filesEqual(a: File, b: File): Boolean {
        if (a.length() != b.length()) return false
        a.inputStream().buffered().use { x ->
            b.inputStream().buffered().use { y ->
                while (true) {
                    val c = x.read()
                    if (c != y.read()) return false
                    if (c == -1) return true
                }
            }
        }
    }

    private fun prune(titleId: String, protect: File? = null) {
        list(titleId).drop(keep).filter { it.dir != protect }.forEach { it.dir.deleteRecursively() }
    }

    private fun read(titleId: String, dir: File): LocalBackup? {
        val metaFile = File(dir, META)
        if (!metaFile.isFile) return null
        val meta = runCatching { gson.fromJson(metaFile.readText(), SnapshotMeta::class.java) }.getOrNull()
            ?: return null
        val payload = File(dir, meta.name.ifEmpty { return null })
        return LocalBackup(
            dir = dir,
            titleId = titleId,
            takenAt = meta.takenAt,
            reason = meta.reason,
            previousLastSyncedHash = meta.previousLastSyncedHash,
            previousLastSyncedAt = meta.previousLastSyncedAt,
            payload = payload,
            isDirectory = meta.isDirectory,
        )
    }

    private fun payloadNameFor(target: File): String =
        safeName(target.name).takeIf { it.isNotEmpty() && it != META } ?: "data"

    private fun safeName(name: String): String = name.replace(Regex("[^A-Za-z0-9._ -]"), "_").trim('.', ' ')

    companion object {
        private const val META = "snapshot.json"
        private const val PARTIAL = ".partial"
        const val REASON_DOWNLOAD = "download"
        const val REASON_BEFORE_RESTORE = "before-restore"

        /**
         * The sync base a restore records: never equal to a save hash, so the
         * three-way rule sees both sides as changed (a conflict) and leaves the
         * restored save alone until the user chooses a side.
         */
        const val RESTORED_BASE = "restored-from-local-backup"
    }
}

/**
 * Snapshot [target] before [write] replaces it.  If the snapshot fails,
 * [write] never runs: a save is only overwritten once a copy of it is kept.
 */
inline fun <T> backedUpWrite(
    store: LocalBackupStore,
    titleId: String,
    target: File,
    previousLastSyncedHash: String?,
    previousLastSyncedAt: Long?,
    write: () -> T,
): T {
    store.snapshot(titleId, target, LocalBackupStore.REASON_DOWNLOAD, previousLastSyncedHash, previousLastSyncedAt)
    return write()
}

/** The app's backup store, under `<filesDir>/local_backups` (excluded from cloud backup). */
object LocalBackups {
    /** Snapshots kept per title. */
    const val KEEP = 5

    @Volatile
    private var instance: LocalBackupStore? = null

    /** Called once from Application.onCreate. */
    fun init(context: Context) {
        if (instance == null) instance = LocalBackupStore(File(context.filesDir, "local_backups"))
    }

    /** The store; throws if [init] never ran, so no download proceeds without backups. */
    val store: LocalBackupStore
        get() = instance ?: throw BackupFailedException(IllegalStateException("Local backups not initialised"))

    fun snapshot(context: Context, titleId: String, target: File): File? {
        init(context)
        return store.snapshot(titleId, target, LocalBackupStore.REASON_DOWNLOAD, null)
    }

    fun latest(titleId: String): LocalBackup? = instance?.latest(titleId)

    suspend fun restore(titleId: String, target: File, dao: SyncStateDao): Boolean =
        store.restore(titleId, target, dao)
}
