package com.savesync.android.sync

import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.nio.file.AtomicMoveNotSupportedException
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.security.MessageDigest

/**
 * Crash-safe replacement of save files and save directories.
 *
 * A download must never leave a save half-written: if the app is killed, the
 * phone loses power or a write fails mid-way, the emulator has to find either
 * the old save or the new one, never a mix (or nothing).  Everything is first
 * written to a hidden sibling ("staging") location on the same volume, read
 * back and verified, and only then swapped in with renames.  For directories
 * the previous version is kept as a hidden sibling until the swap has
 * finished, and is restored if the swap fails.
 */
object AtomicSaveWriter {

    private const val STAGING_SUFFIX = ".gamesync-new"
    private const val BACKUP_SUFFIX = ".gamesync-old"

    /**
     * One swap at a time, process-wide.  The staging and backup names are
     * fixed per target, so two writers to the same save (the background
     * worker and a sync started from the UI, or a restore) would otherwise
     * delete each other's staging and swap in a half-filled copy.
     */
    private val swapLock = Any()

    /** Hidden sibling that receives the new contents before the swap. */
    fun stagingFor(target: File): File =
        File(target.absoluteFile.parentFile, ".${target.name}$STAGING_SUFFIX")

    /** Hidden sibling that holds the previous contents during the swap. */
    fun backupFor(target: File): File =
        File(target.absoluteFile.parentFile, ".${target.name}$BACKUP_SUFFIX")

    /**
     * Atomically replaces the single file [target] with [bytes].
     *
     * The bytes are written to a sibling temp file, flushed to disk and
     * verified by SHA-256 before being renamed over [target], so [target]
     * always holds either the complete old or the complete new contents.
     */
    fun replaceFile(target: File, bytes: ByteArray): Unit = synchronized(swapLock) {
        val parent = target.absoluteFile.parentFile
            ?: throw IOException("No parent directory for $target")
        if (!parent.isDirectory && !parent.mkdirs()) {
            throw IOException("Could not create $parent")
        }
        val staging = stagingFor(target)
        try {
            writeAndSync(staging, bytes)
            verify(staging, sha256(bytes))
            moveReplacing(staging, target)
        } finally {
            if (staging.exists()) staging.delete()
        }
    }

    /**
     * [replaceFile] with bytes built from [target]'s current contents (null
     * when there is none), read under the swap lock.  For a file several
     * titles share (YabaSanshiro's backup.bin): another writer in the app
     * can't swap in its change between this read and this swap, so neither
     * merge drops the other's.  Returns the bytes written.
     */
    fun replaceFileMerging(target: File, build: (existing: ByteArray?) -> ByteArray): ByteArray =
        synchronized(swapLock) {
            val bytes = build(target.takeIf { it.isFile }?.readBytes())
            replaceFile(target, bytes)
            bytes
        }

    /**
     * Writes [files] (relative path → bytes) into a fresh directory and swaps
     * it in place of [targetDir].
     *
     * With [keepExisting] = false the new directory holds exactly [files], so
     * files that are no longer part of the save disappear (3DS / Wii U trees).
     * With [keepExisting] = true the current contents are copied first and
     * [files] are written over them, which keeps the old "merge" behaviour of
     * flat PSP slots while still making the swap atomic.
     *
     * Every entry name must stay inside the directory; an unsafe name aborts
     * the whole write and leaves [targetDir] untouched.
     *
     * [onStaged] sees the finished, verified staging directory just before
     * the swap (under the swap lock): exactly what lands in [targetDir].
     */
    fun replaceDirectory(
        targetDir: File,
        files: List<Pair<String, ByteArray>>,
        keepExisting: Boolean = false,
        onStaged: (File) -> Unit = {},
    ) {
        replaceDirectory(targetDir, keepExisting) { staging ->
            for ((name, data) in files) {
                val out = safeChild(staging, name)
                    ?: throw IOException("Rejected unsafe save entry name '$name'")
                out.parentFile?.let { if (!it.isDirectory && !it.mkdirs()) throw IOException("Could not create $it") }
                writeAndSync(out, data)
            }
            for ((name, data) in files) {
                verify(File(staging, name), sha256(data))
            }
            onStaged(staging)
        }
    }

    /**
     * Generic form of [replaceDirectory]: [populate] fills the staging
     * directory (and may throw to abort); the result is then swapped in.
     *
     * [rename] is only overridable so tests can simulate a failing swap.
     */
    internal fun replaceDirectory(
        targetDir: File,
        keepExisting: Boolean,
        rename: (File, File) -> Boolean = { from, to -> from.renameTo(to) },
        populate: (File) -> Unit
    ): Unit = synchronized(swapLock) {
        val target = targetDir.absoluteFile
        val parent = target.parentFile ?: throw IOException("No parent directory for $target")
        if (!parent.isDirectory && !parent.mkdirs()) {
            throw IOException("Could not create $parent")
        }
        val staging = stagingFor(target)
        val backup = backupFor(target)

        recoverInterruptedSwap(target, backup)

        try {
            if (staging.exists()) staging.deleteRecursively()
            if (keepExisting && target.isDirectory) {
                if (!target.copyRecursively(staging, overwrite = true)) {
                    throw IOException("Could not copy $target to $staging")
                }
            } else if (!staging.mkdirs()) {
                throw IOException("Could not create $staging")
            }

            populate(staging)

            // Swap: target → backup, staging → target.  The old save stays on
            // disk as `backup` until the new one is in place.
            val hadTarget = target.exists()
            if (hadTarget && !rename(target, backup)) {
                throw IOException("Could not move $target aside")
            }
            if (!rename(staging, target)) {
                if (hadTarget && !target.exists()) rename(backup, target)
                throw IOException("Could not move $staging into place")
            }
            if (backup.exists()) backup.deleteRecursively()
        } finally {
            if (staging.exists()) staging.deleteRecursively()
        }
    }

    /**
     * A previous run may have died between the two renames, leaving only the
     * backup.  Put it back before doing anything else; a backup left next to
     * an existing target is just a leftover from a finished swap.
     */
    private fun recoverInterruptedSwap(target: File, backup: File) {
        if (!backup.exists()) return
        if (!target.exists()) {
            if (!backup.renameTo(target)) {
                throw IOException("Could not restore interrupted save swap from $backup")
            }
        } else {
            backup.deleteRecursively()
        }
    }

    private fun moveReplacing(from: File, to: File) {
        try {
            Files.move(
                from.toPath(), to.toPath(),
                StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING
            )
        } catch (_: AtomicMoveNotSupportedException) {
            Files.move(from.toPath(), to.toPath(), StandardCopyOption.REPLACE_EXISTING)
        }
    }

    private fun writeAndSync(file: File, data: ByteArray) {
        FileOutputStream(file).use { out ->
            out.write(data)
            out.flush()
            out.fd.sync()
        }
    }

    private fun verify(file: File, expectedSha256: ByteArray) {
        val actual = file.inputStream().use { stream ->
            val digest = MessageDigest.getInstance("SHA-256")
            val buffer = ByteArray(8192)
            var read: Int
            while (stream.read(buffer).also { read = it } != -1) digest.update(buffer, 0, read)
            digest.digest()
        }
        if (!actual.contentEquals(expectedSha256)) {
            throw IOException("Verification failed for $file: content on disk does not match")
        }
    }

    private fun sha256(data: ByteArray): ByteArray =
        MessageDigest.getInstance("SHA-256").digest(data)

    private fun safeChild(baseDir: File, name: String): File? {
        val target = File(baseDir, name)
        val basePath = baseDir.canonicalPath
        val targetPath = target.canonicalPath
        return if (targetPath.startsWith(basePath + File.separator)) target else null
    }
}
