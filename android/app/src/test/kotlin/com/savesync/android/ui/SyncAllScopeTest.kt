package com.savesync.android.ui

import com.savesync.android.emulators.SaveEntry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class SyncAllScopeTest {

    private fun entry(id: String, serverOnly: Boolean = false) = SaveEntry(
        titleId = id,
        displayName = id,
        systemName = "GBA",
        saveFile = File("/nonexistent/$id.sav"),
        saveDir = null,
        isServerOnly = serverOnly,
    )

    @Test
    fun `targets are the visible local rows only, in order`() {
        val visible = listOf(entry("A"), entry("B", serverOnly = true), entry("C"))
        assertEquals(listOf("A", "C"), syncAllTargets(visible).map { it.titleId })
    }

    @Test
    fun `a search that shows two saves syncs two saves`() {
        // The whole library has five; the search shows two of them.
        val visible = listOf(entry("ZELDA_1"), entry("ZELDA_2"))
        val targets = syncAllTargets(visible)
        val copy = syncAllCopy(targets.size, filtersApplied = true)
        assertEquals(2, targets.size)
        assertEquals("Sync 2 saves", copy.title)
    }

    @Test
    fun `nothing to sync when only server rows are shown`() {
        assertTrue(syncAllTargets(listOf(entry("S", serverOnly = true))).isEmpty())
    }

    @Test
    fun `copy mentions filters only when they apply`() {
        val plain = syncAllCopy(3, filtersApplied = false)
        assertEquals("Uploads or downloads each one as needed.", plain.message)
        assertFalse(plain.message.contains("filters"))
        val filtered = syncAllCopy(1, filtersApplied = true)
        assertEquals("Sync 1 save", filtered.title)
        assertTrue(filtered.message.endsWith("Only the saves shown (filters applied)."))
    }
}
