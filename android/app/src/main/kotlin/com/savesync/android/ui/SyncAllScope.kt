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
