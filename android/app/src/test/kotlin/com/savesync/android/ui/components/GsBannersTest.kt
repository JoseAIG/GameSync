package com.savesync.android.ui.components

import org.junit.Assert.assertEquals
import org.junit.Test

class GsBannersTest {

    @Test
    fun `nothing wrong shows nothing`() {
        assertEquals(emptyList<BannerKind>(), bannersFor(BannerState(serverOnline = true)))
        assertEquals(emptyList<BannerKind>(), bannersFor(BannerState(serverOnline = null)))
    }

    @Test
    fun `no server outranks unreachable`() {
        assertEquals(
            listOf(BannerKind.NoServer),
            bannersFor(BannerState(serverUrlBlank = true, serverOnline = false)),
        )
        assertEquals(
            listOf(BannerKind.ServerUnreachable),
            bannersFor(BannerState(serverOnline = false)),
        )
    }

    @Test
    fun `at most two banners in priority order`() {
        assertEquals(
            listOf(BannerKind.ServerUnreachable, BannerKind.Conflicts),
            bannersFor(BannerState(serverOnline = false, conflictCount = 3, missingFolderCount = 1)),
        )
        assertEquals(
            listOf(BannerKind.NoServer, BannerKind.Conflicts),
            bannersFor(BannerState(serverUrlBlank = true, conflictCount = 1, missingFolderCount = 2)),
        )
        assertEquals(
            listOf(BannerKind.Conflicts, BannerKind.FolderMissing),
            bannersFor(BannerState(serverOnline = true, conflictCount = 1, missingFolderCount = 1)),
        )
    }

    @Test
    fun `a pushed-out banner reappears when a higher one clears`() {
        assertEquals(
            listOf(BannerKind.FolderMissing),
            bannersFor(BannerState(serverOnline = true, missingFolderCount = 1)),
        )
    }
}
