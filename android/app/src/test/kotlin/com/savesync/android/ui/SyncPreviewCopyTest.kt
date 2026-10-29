package com.savesync.android.ui

import com.savesync.android.sync.PlanRoute
import com.savesync.android.sync.PlannedAction
import com.savesync.android.sync.PlannedTitle
import com.savesync.android.sync.SuspectUpload
import com.savesync.android.sync.SyncPreview
import org.junit.Assert.assertEquals
import org.junit.Test

/** M5-1: what Sync all's preview says, and that its A label follows the plan. */
class SyncPreviewCopyTest {

    private fun t(id: String, action: PlannedAction) = PlannedTitle(id, "Game $id", action, PlanRoute.SYNC)

    private fun preview(titles: List<PlannedTitle>, suspects: List<String> = emptyList(), unchecked: Int = 0) = SyncPreview(
        titles = titles,
        suspects = suspects.map { SuspectUpload(it, "Game $it", 10, 1000, null, null) },
        sizeCheckSkipped = unchecked,
        errors = emptyList(),
        createdAtMs = 0L,
        entries = emptyMap(),
        conflictInfo = emptyMap(),
    )

    @Test
    fun `summary counts each kind, suspects left out of uploads until included`() {
        val p = preview(
            listOf(
                t("U1", PlannedAction.UPLOAD), t("U2", PlannedAction.UPLOAD), t("S", PlannedAction.UPLOAD),
                t("D1", PlannedAction.DOWNLOAD), t("D2", PlannedAction.DOWNLOAD),
                t("N", PlannedAction.DOWNLOAD_NEW),
                t("C", PlannedAction.CONFLICT),
                t("OK", PlannedAction.UP_TO_DATE),
            ),
            suspects = listOf("S"),
            unchecked = 3,
        )
        assertEquals(
            listOf(
                "2 to upload",
                "2 will replace local saves (backups kept)",
                "1 new from the server",
                "1 conflict (skipped)",
                "1 up to date",
                "Size check skipped for 3 saves",
            ),
            syncPreviewLines(p, emptySet()).map { it.text },
        )
        assertEquals("3 to upload", syncPreviewLines(p, setOf("S")).first().text)
    }

    @Test
    fun `nothing left to do reads Done`() {
        val p = preview(listOf(t("S", PlannedAction.UPLOAD), t("OK", PlannedAction.UP_TO_DATE)), suspects = listOf("S"))
        assertEquals(0, p.actionableCount())
        assertEquals(1, p.actionableCount(includeSuspects = setOf("S")))
        assertEquals(listOf(PreviewLineKind.UpToDate), syncPreviewLines(p, emptySet()).map { it.kind })
    }

    @Test
    fun `singular wording`() {
        val p = preview(listOf(t("D", PlannedAction.DOWNLOAD)))
        assertEquals("1 will replace a local save (backup kept)", syncPreviewLines(p, emptySet()).single().text)
    }

    @Test
    fun `suspect sizes, with file counts only when they differ`() {
        val fmt: (Long) -> String = { "${it}B" }
        assertEquals("10B here · 1000B on the server", suspectSizeText(SuspectUpload("A", "A", 10, 1000, null, null), fmt))
        assertEquals(
            "10B here · 20B on the server · 2 of 5 files",
            suspectSizeText(SuspectUpload("A", "A", 10, 20, 2, 5), fmt),
        )
    }

    @Test
    fun `details group titles by action, sorted`() {
        val p = preview(listOf(t("b", PlannedAction.UPLOAD), t("a", PlannedAction.UPLOAD), t("c", PlannedAction.UP_TO_DATE)))
        assertEquals(
            listOf("Upload" to listOf("Game a", "Game b"), "Up to date" to listOf("Game c")),
            previewDetailGroups(p),
        )
    }

    @Test
    fun `details list a skipped suspect as held back, an included one as upload`() {
        val p = preview(listOf(t("S", PlannedAction.UPLOAD), t("U", PlannedAction.UPLOAD)), suspects = listOf("S"))
        assertEquals(
            listOf("Upload" to listOf("Game U"), "Held back (smaller than the server's copy)" to listOf("Game S")),
            previewDetailGroups(p),
        )
        assertEquals(
            listOf("Upload" to listOf("Game S", "Game U")),
            previewDetailGroups(p, includeSuspects = setOf("S")),
        )
    }
}
