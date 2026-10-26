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
}
