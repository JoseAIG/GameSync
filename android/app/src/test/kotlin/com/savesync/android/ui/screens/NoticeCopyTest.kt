package com.savesync.android.ui.screens

import com.savesync.android.ui.components.GsNoticeKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class NoticeCopyTest {

    @Test
    fun `sync-all message keeps the first error`() {
        assertEquals("Sync done: 3 uploaded, 2 downloaded", syncAllMessage(3, 2, 0, emptyList()))
        assertEquals("Sync done: 0 uploaded, 0 downloaded, 1 conflict", syncAllMessage(0, 0, 1, emptyList()))
        assertEquals(
            "Sync done: 1 uploaded, 0 downloaded, 2 errors\nAzahar folder not found",
            syncAllMessage(1, 0, 0, listOf("Azahar folder not found", "timeout")),
        )
    }

    @Test
    fun `no server offers Settings, other errors no action`() {
        var opened = false
        val noServer = errorNotice("Server URL not configured") { opened = true }
        assertEquals(GsNoticeKind.Error, noServer.kind)
        assertEquals("Settings", noServer.actionLabel)
        noServer.onAction?.invoke()
        assertEquals(true, opened)

        val other = errorNotice("timeout") { }
        assertEquals(GsNoticeKind.Error, other.kind)
        assertNull(other.actionLabel)
    }

    private fun result(
        uploaded: Int = 3,
        conflicts: List<String> = emptyList(),
        errors: List<String> = emptyList(),
    ) = com.savesync.android.sync.SyncResult(uploaded, 0, conflicts, errors)

    @Test
    fun `a missing folder makes the sync-all notice a Warning that names it, with Fix`() {
        var fixed = false
        val n = syncAllNotice(
            result(),
            overrideWarnings = listOf("Azahar save folder not found: /x."),
            onReview = {},
            onFix = { fixed = true },
        )
        assertEquals(GsNoticeKind.Warning, n.kind)
        assertEquals("Sync done: 3 uploaded, 0 downloaded\nAzahar save folder not found: /x.", n.message)
        assertEquals("Fix", n.actionLabel)
        n.onAction?.invoke()
        assertEquals(true, fixed)
    }

    @Test
    fun `errors still win over folder warnings, and warnings are not errors`() {
        val n = syncAllNotice(result(errors = listOf("timeout")), listOf("Azahar folder"), {}, {})
        assertEquals(GsNoticeKind.Error, n.kind)
        assertEquals("Sync done: 3 uploaded, 0 downloaded, 1 error\ntimeout", n.message)
    }

    @Test
    fun `conflicts offer Review, a clean sync is a Success`() {
        val c = syncAllNotice(result(conflicts = listOf("Zelda")), emptyList(), {}, {})
        assertEquals(GsNoticeKind.Warning, c.kind)
        assertEquals("Review", c.actionLabel)
        val ok = syncAllNotice(result(), emptyList(), {}, {})
        assertEquals(GsNoticeKind.Success, ok.kind)
        assertNull(ok.actionLabel)
    }

    @Test
    fun `missing-folder banner copy`() {
        assertEquals(
            "Azahar folder not found — 3DS saves are hidden. Tap to fix.",
            missingFolderBannerText(listOf(com.savesync.android.emulators.impl.AzaharEmulator.EMULATOR_KEY)),
        )
        assertEquals("2 emulator folders not found — tap to fix", missingFolderBannerText(listOf("a", "b")))
        assertEquals("", missingFolderBannerText(emptyList()))
    }

    @Test
    fun `a download that kept a copy offers Undo with the title`() {
        var undone: String? = null
        val n = saveResultNotice(
            com.savesync.android.ui.SaveDetailState.Success(
                "Replaced with the server's save · backup kept",
                undoTitleId = "GBA_zelda",
            )
        ) { undone = it }
        assertEquals(GsNoticeKind.Success, n.kind)
        assertEquals("Undo", n.actionLabel)
        n.onAction?.invoke()
        assertEquals("GBA_zelda", undone)

        val plain = saveResultNotice(com.savesync.android.ui.SaveDetailState.Success("Already in sync with server.")) {}
        assertNull(plain.actionLabel)
    }

    @Test
    fun `force download confirm names both sides and the kept copy`() {
        val m = forceDownloadMessage(
            localWhen = "Today 21:14 · 26 Oct 2026", localSize = "8.0 KB",
            serverWhen = "Yesterday 10:00 · 25 Oct 2026", serverSize = "8.0 KB", serverFrom = "3ds_emu",
            retroArch = false,
        )
        assertEquals(
            "This device: Today 21:14 · 26 Oct 2026, 8.0 KB. Server: Yesterday 10:00 · 25 Oct 2026, 8.0 KB, " +
                "from 3ds_emu. A copy of the current save is kept on this device.",
            m,
        )
        val ra = forceDownloadMessage(null, "0 B", null, null, null, retroArch = true)
        assertEquals(
            "A copy of the current save is kept on this device.\nClose RetroArch first — an auto-loaded save state can undo this.",
            ra,
        )
    }
}
