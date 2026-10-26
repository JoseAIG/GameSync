package com.savesync.android.ui.components

/**
 * Persistent conditions shown as banners on Saves and Save detail.  A
 * notice reports an event; a banner an ongoing state.  Declared in priority
 * order: when more than [MAX_BANNERS] apply, the lowest drop out.  A banner
 * that is pushed out stays reachable elsewhere (conflicts through the Saves
 * tab dot and the L2/R2 filter, a missing folder through the Emulators card).
 */
enum class BannerKind {
    /** No server URL set (Info). */
    NoServer,
    /** Server set but not answering (Warn). */
    ServerUnreachable,
    /** Saves waiting on a conflict decision (Err). */
    Conflicts,
    /** An emulator save-folder override points nowhere (Warn). */
    FolderMissing,
}

/** What the banner budget looks at.  [serverOnline] null = not checked yet. */
data class BannerState(
    val serverUrlBlank: Boolean = false,
    val serverOnline: Boolean? = null,
    val conflictCount: Int = 0,
    val missingFolderCount: Int = 0,
)

const val MAX_BANNERS = 2

/** The banners to show, highest priority first, at most [MAX_BANNERS]. */
fun bannersFor(state: BannerState): List<BannerKind> = buildList {
    if (state.serverUrlBlank) add(BannerKind.NoServer)
    else if (state.serverOnline == false) add(BannerKind.ServerUnreachable)
    if (state.conflictCount > 0) add(BannerKind.Conflicts)
    if (state.missingFolderCount > 0) add(BannerKind.FolderMissing)
}.take(MAX_BANNERS)
