package com.savesync.android.ui

import com.savesync.android.emulators.SaveEntry

/**
 * What "Sync all" acts on: exactly the local saves the Saves list shows
 * (system filter, search and status filter applied).  Server-only rows are
 * left out, as before — they have no local file to compare — so the count in
 * the confirm dialog and the saves [MainViewModel.syncNow] receives come from
 * this one expression.  SyncEngine.sync only acts on titles in its request,
 * so a filtered request never touches a save that isn't listed.
 */
fun syncAllTargets(visible: List<SaveEntry>): List<SaveEntry> = visible.filter { !it.isServerOnly }

/** Title and message of the Sync-all confirm dialog. */
data class SyncAllCopy(val title: String, val message: String)

/**
 * "Sync 3 saves" / "Uploads or downloads each one as needed."; with a search
 * or a status filter active the message adds that only the shown saves sync.
 */
fun syncAllCopy(count: Int, filtersApplied: Boolean): SyncAllCopy {
    val title = "Sync $count " + if (count == 1) "save" else "saves"
    val message = buildString {
        append("Uploads or downloads each one as needed.")
        if (filtersApplied) append(" Only the saves shown (filters applied).")
    }
    return SyncAllCopy(title, message)
}

// ── Sync all's preview (SyncEngine.plan) ────────────────────────────────────

/** One summary line of the Sync-all preview; the dialog picks its icon from [kind]. */
enum class PreviewLineKind { Upload, ReplaceLocal, NewLocal, Conflict, UpToDate, Unchecked }

data class PreviewLine(val kind: PreviewLineKind, val text: String)

/**
 * The preview's summary: "3 to upload", "2 will replace local saves
 * (backups kept)", "1 conflict (skipped)", "8 up to date" …  Uploads count
 * the safe ones plus the suspects the user included; lines with nothing to
 * say are left out.
 */
fun syncPreviewLines(preview: com.savesync.android.sync.SyncPreview, includeSuspects: Set<String>): List<PreviewLine> {
    fun plural(n: Int, one: String, many: String) = if (n == 1) one else many
    val uploads = preview.safeUploads + includeSuspects.count { it in preview.suspectIds }
    val replace = preview.count(com.savesync.android.sync.PlannedAction.DOWNLOAD)
    val new = preview.count(com.savesync.android.sync.PlannedAction.DOWNLOAD_NEW)
    val conflicts = preview.count(com.savesync.android.sync.PlannedAction.CONFLICT)
    val upToDate = preview.count(com.savesync.android.sync.PlannedAction.UP_TO_DATE)
    return buildList {
        if (uploads > 0) add(PreviewLine(PreviewLineKind.Upload, "$uploads to upload"))
        if (replace > 0) add(
            PreviewLine(
                PreviewLineKind.ReplaceLocal,
                "$replace " + plural(replace, "will replace a local save (backup kept)", "will replace local saves (backups kept)"),
            )
        )
        if (new > 0) add(PreviewLine(PreviewLineKind.NewLocal, "$new new from the server"))
        if (conflicts > 0) add(
            PreviewLine(PreviewLineKind.Conflict, "$conflicts " + plural(conflicts, "conflict", "conflicts") + " (skipped)")
        )
        if (upToDate > 0) add(PreviewLine(PreviewLineKind.UpToDate, "$upToDate up to date"))
        if (preview.sizeCheckSkipped > 0) add(
            PreviewLine(
                PreviewLineKind.Unchecked,
                "Size check skipped for ${preview.sizeCheckSkipped} " + plural(preview.sizeCheckSkipped, "save", "saves"),
            )
        )
    }
}

/** A suspect row's sizes: "12 KB here · 48 KB on the server" (+ file counts when they differ). */
fun suspectSizeText(s: com.savesync.android.sync.SuspectUpload, formatBytes: (Long) -> String): String = buildString {
    append(formatBytes(s.localSize)).append(" here")
    s.serverSize?.let { append(" · ").append(formatBytes(it)).append(" on the server") }
    if (s.localFileCount != null && s.serverFileCount != null && s.localFileCount != s.serverFileCount) {
        append(" · ${s.localFileCount} of ${s.serverFileCount} files")
    }
}

/** A plan's titles grouped for "Details", in the summary's order; empty groups left out. */
fun previewDetailGroups(preview: com.savesync.android.sync.SyncPreview): List<Pair<String, List<String>>> {
    val by = preview.titles.groupBy { it.action }
    return listOf(
        com.savesync.android.sync.PlannedAction.UPLOAD to "Upload",
        com.savesync.android.sync.PlannedAction.DOWNLOAD to "Replace local save",
        com.savesync.android.sync.PlannedAction.DOWNLOAD_NEW to "New from the server",
        com.savesync.android.sync.PlannedAction.CONFLICT to "Conflict (skipped)",
        com.savesync.android.sync.PlannedAction.UP_TO_DATE to "Up to date",
    ).mapNotNull { (action, label) ->
        by[action]?.map { it.displayName }?.sorted()?.takeIf { it.isNotEmpty() }?.let { label to it }
    }
}
