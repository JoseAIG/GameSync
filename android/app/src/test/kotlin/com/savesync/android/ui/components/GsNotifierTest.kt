package com.savesync.android.ui.components

import androidx.compose.material3.SnackbarDuration
import androidx.compose.material3.SnackbarHostState
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancel
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.yield
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class GsNotifierTest {

    private suspend fun settle() = repeat(20) { yield() }

    @Test
    fun `three quick shows leave only the third, nothing queued`() = runBlocking {
        val host = SnackbarHostState()
        val scope = CoroutineScope(coroutineContext + Job())
        val notifier = GsNotifier(host, scope, haptics = null)

        notifier.show(GsNotice(GsNoticeKind.Info, "one"))
        notifier.show(GsNotice(GsNoticeKind.Info, "two"))
        notifier.show(GsNotice(GsNoticeKind.Info, "three"))
        settle()

        assertEquals("three", host.currentSnackbarData?.visuals?.message)

        // Dismissing it must not reveal a queued "one" or "two".
        host.currentSnackbarData?.dismiss()
        settle()
        assertNull(host.currentSnackbarData)
        scope.cancel()
    }

    @Test
    fun `a replaced notice never runs its action`() = runBlocking {
        val host = SnackbarHostState()
        val scope = CoroutineScope(coroutineContext + Job())
        val notifier = GsNotifier(host, scope, haptics = null)
        var firstRan = false
        var secondRan = false

        notifier.show(GsNotice(GsNoticeKind.Error, "failed", "Retry") { firstRan = true })
        settle()
        notifier.show(GsNotice(GsNoticeKind.Error, "failed again", "Retry") { secondRan = true })
        settle()
        host.currentSnackbarData?.performAction()
        settle()

        assertFalse(firstRan)
        assertTrue(secondRan)
        scope.cancel()
    }

    @Test
    fun `durations and dismiss follow the kind`() {
        assertEquals(SnackbarDuration.Short, durationFor(GsNotice(GsNoticeKind.Success, "")))
        assertEquals(SnackbarDuration.Short, durationFor(GsNotice(GsNoticeKind.Info, "")))
        assertEquals(SnackbarDuration.Long, durationFor(GsNotice(GsNoticeKind.Warning, "")))
        assertEquals(SnackbarDuration.Long, durationFor(GsNotice(GsNoticeKind.Error, "")))
        assertEquals(SnackbarDuration.Long, durationFor(GsNotice(GsNoticeKind.Success, "", "Undo")))
        assertTrue(GsSnackbarVisuals(GsNotice(GsNoticeKind.Error, "")).withDismissAction)
        assertFalse(GsSnackbarVisuals(GsNotice(GsNoticeKind.Warning, "")).withDismissAction)
    }

    @Test
    fun `haptics confirm success and reject errors only`() {
        assertEquals(GsHaptic.Confirm, hapticFor(GsNoticeKind.Success))
        assertEquals(GsHaptic.Reject, hapticFor(GsNoticeKind.Error))
        assertNull(hapticFor(GsNoticeKind.Info))
        assertNull(hapticFor(GsNoticeKind.Warning))
    }

    @Test
    fun `single-save messages map to a kind`() {
        assertEquals(GsNoticeKind.Success, noticeKindForSaveMessage("↑ Uploaded to server."))
        assertEquals(GsNoticeKind.Success, noticeKindForSaveMessage("Already in sync with server."))
        assertEquals(
            GsNoticeKind.Warning,
            noticeKindForSaveMessage("⚠ Conflict detected — use Upload or Download to force."),
        )
        assertEquals(GsNoticeKind.Error, noticeKindForSaveMessage("✗ Error: timeout."))
        assertEquals("Uploaded successfully", stripLeadingGlyph("↑ Uploaded successfully"))
        assertEquals("Already using canonical name", stripLeadingGlyph("✓ Already using canonical name"))
        assertTrue(isServerNotConfigured("Server URL not configured"))
        assertFalse(isServerNotConfigured("timeout"))
    }
}
