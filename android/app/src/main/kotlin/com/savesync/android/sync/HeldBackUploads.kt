package com.savesync.android.sync

import com.savesync.android.SaveSyncApp
import java.io.File

/**
 * Uploads the background worker held back as suspected rollbacks (see
 * [RollbackCheck]).  The worker has nobody to ask, so it never uploads them;
 * it records their title ids here and the app reports them the next time it
 * opens, clearing each entry only once its notice has been shown.  One entry
 * per line (a title id; older versions wrote display names); repeat-free,
 * newest last.
 */
class HeldBackUploadStore(private val file: File) {

    @Synchronized
    fun record(names: List<String>) {
        if (names.isEmpty()) return
        val all = (read() + names.map { it.replace('\n', ' ').trim() }.filter { it.isNotEmpty() })
            .distinct()
            .takeLast(MAX_NAMES)
        runCatching {
            file.parentFile?.mkdirs()
            file.writeText(all.joinToString("\n"))
        }
    }

    /** The recorded entries, left in place until [clear]ed. */
    @Synchronized
    fun peek(): List<String> = read()

    /**
     * Forgets [shown] (entries a notice has reported).  Entries the worker
     * recorded since they were read stay for the next notice.
     */
    @Synchronized
    fun clear(shown: Collection<String>) {
        if (shown.isEmpty()) return
        val rest = read().filter { it !in shown }
        runCatching {
            if (rest.isEmpty()) file.delete() else file.writeText(rest.joinToString("\n"))
        }
    }

    private fun read(): List<String> =
        runCatching { if (file.exists()) file.readLines().map { it.trim() }.filter { it.isNotEmpty() } else emptyList() }
            .getOrDefault(emptyList())

    companion object {
        private const val FILE_NAME = "held_back_uploads.txt"
        private const val MAX_NAMES = 50

        /** The app's store, in its private files directory. */
        val app: HeldBackUploadStore by lazy {
            HeldBackUploadStore(File(SaveSyncApp.instance.filesDir, FILE_NAME))
        }
    }
}

/** The notice for uploads a background sync held back. */
fun heldBackNoticeText(names: List<String>): String {
    val n = names.size
    val list = names.take(2).joinToString(", ") + if (n > 2) " and ${n - 2} more" else ""
    val what = if (n == 1) "1 save" else "$n saves"
    return "Auto sync didn't upload $what smaller than the server copy ($list). Sync all to review."
}
