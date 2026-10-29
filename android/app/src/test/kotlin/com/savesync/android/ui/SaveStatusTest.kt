package com.savesync.android.ui

import com.savesync.android.emulators.SaveEntry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class SaveStatusTest {

    @get:Rule
    val tmp = TemporaryFolder()

    // ── Three-way mapping (the header pill and sync agree) ────────────────

    @Test
    fun `three-way mapping follows the server's decision table`() {
        assertEquals(SaveSyncStatus.SYNCED, threeWayStatus("a", "a", "a"))
        // Equal to the server wins even without a base.
        assertEquals(SaveSyncStatus.SYNCED, threeWayStatus("a", null, "a"))
        assertEquals(SaveSyncStatus.LOCAL_NEWER, threeWayStatus("b", "a", "a"))
        assertEquals(SaveSyncStatus.SERVER_NEWER, threeWayStatus("a", "a", "b"))
        assertEquals(SaveSyncStatus.CONFLICT, threeWayStatus("b", "a", "c"))
        // No base and different hashes: never synced, not "local only".
        assertEquals(SaveSyncStatus.LOCAL_ONLY, threeWayStatus("b", null, "c"))
    }

    @Test
    fun `labels never claim more than is known`() {
        assertEquals("Never synced", SaveSyncStatus.LOCAL_ONLY.label)
        assertEquals("Unknown", SaveSyncStatus.UNKNOWN.label)
    }

    @Test
    fun `detail status uses the three values once all are known`() {
        assertEquals(
            SaveSyncStatus.SERVER_NEWER,
            detailSyncStatus(true, "a", "a", ServerSide.Hash("b"), fallback = SaveSyncStatus.SYNCED),
        )
        assertEquals(
            SaveSyncStatus.LOCAL_NEWER,
            detailSyncStatus(true, "b", "a", ServerSide.Missing, fallback = null),
        )
        assertEquals(
            SaveSyncStatus.LOCAL_ONLY,
            detailSyncStatus(true, "b", null, ServerSide.Missing, fallback = null),
        )
    }

    @Test
    fun `detail status falls back to the row while loading`() {
        assertEquals(
            SaveSyncStatus.LOCAL_NEWER,
            detailSyncStatus(true, null, "a", ServerSide.Hash("a"), fallback = SaveSyncStatus.LOCAL_NEWER),
        )
        assertNull(detailSyncStatus(true, "a", "a", ServerSide.Unknown, fallback = null))
        assertEquals(
            SaveSyncStatus.SERVER_ONLY,
            detailSyncStatus(false, null, null, ServerSide.Hash("x"), fallback = null),
        )
    }

    // ── Cheap vs hashed statuses ──────────────────────────────────────────

    @Test
    fun `cheap status is known without hashing only for metadata cases`() {
        assertEquals(SaveSyncStatus.SERVER_ONLY, cheapSyncStatus(isServerOnly = true, lastSyncedHash = "a"))
        assertEquals(SaveSyncStatus.LOCAL_ONLY, cheapSyncStatus(isServerOnly = false, lastSyncedHash = null))
        assertEquals(SaveSyncStatus.CONFLICT, cheapSyncStatus(false, "a", conflict = true))
        // Synced once: needs a hash, so it is "checking", never assumed Synced.
        assertNull(cheapSyncStatus(isServerOnly = false, lastSyncedHash = "a"))
    }

    @Test
    fun `an unhashable save is unknown, not synced`() {
        assertEquals(SaveSyncStatus.UNKNOWN, localStatus("", "a"))
        assertEquals(SaveSyncStatus.LOCAL_NEWER, localStatus("b", "a"))
        assertEquals(SaveSyncStatus.SYNCED, localStatus("a", "a"))
    }

    // ── Filtering ─────────────────────────────────────────────────────────

    private fun row(id: String, status: SaveSyncStatus?, system: String = "GBA") = SaveRow(
        SaveEntry(titleId = id, displayName = id, systemName = system, saveFile = null, saveDir = null),
        status,
    )

    @Test
    fun `a status filter lists only computed matches and counts the rest as checking`() {
        val rows = listOf(
            row("A", SaveSyncStatus.LOCAL_NEWER),
            row("B", SaveSyncStatus.SYNCED),
            row("C", null),
            row("D", null, system = "SNES"),
        )
        val view = filterSaveRows(rows, "GBA", "", SaveSyncStatus.LOCAL_NEWER)
        assertEquals(listOf("A"), view.rows.map { it.entry.titleId })
        // D is filtered out by system, so only C counts.
        assertEquals(1, view.checking)
    }

    @Test
    fun `without a status filter checking rows are listed`() {
        val rows = listOf(row("A", null), row("B", SaveSyncStatus.SYNCED))
        val view = filterSaveRows(rows, "All", "", null)
        assertEquals(2, view.rows.size)
        assertEquals(0, view.checking)
    }

    @Test
    fun `search matches name, title id and canonical name`() {
        val e = SaveEntry("GBA_minish", "minish", "GBA", saveFile = null, saveDir = null, canonicalName = "The Minish Cap")
        val rows = listOf(SaveRow(e, SaveSyncStatus.SYNCED), row("other", SaveSyncStatus.SYNCED))
        assertEquals(1, filterSaveRows(rows, "All", "MINISH CAP", null).rows.size)
        assertEquals(1, filterSaveRows(rows, "All", "gba_min", null).rows.size)
    }

    // ── Cursor keyed by title id ──────────────────────────────────────────

    @Test
    fun `the cursor follows its game when rows reorder`() {
        assertEquals(2, resolveSelection(listOf("X", "Y", "B", "Z"), "B", lastIndex = 0))
    }

    @Test
    fun `a game that left the list hands the cursor to the row at its old index`() {
        assertEquals(1, resolveSelection(listOf("A", "C", "D"), "B", lastIndex = 1))
        assertEquals(1, resolveSelection(listOf("A", "C"), "Z", lastIndex = 5))
        assertEquals(-1, resolveSelection(emptyList(), "A", lastIndex = 0))
    }

    // ── Cache key fingerprint ─────────────────────────────────────────────

    @Test
    fun `fingerprint covers every file of a folder save`() {
        val dir = tmp.newFolder("slot")
        File(dir, "a.bin").writeBytes(ByteArray(10))
        File(dir, "sub").mkdirs()
        File(dir, "sub/b.bin").writeBytes(ByteArray(5))
        File(dir, "a.bin").setLastModified(1_000_000L)
        File(dir, "sub/b.bin").setLastModified(2_000_000L)
        val e = SaveEntry("T", "T", "3DS", saveFile = null, saveDir = dir)
        assertEquals(2_000_000L to 15L, e.statFingerprint())
    }

    @Test
    fun `server source prefers the server's label, then platform and console`() {
        assertEquals("3ds_emu", serverSourceLabel("3ds_emu", "android", "abcdef1234"))
        assertEquals("android · 1234", serverSourceLabel(null, "android", "abcdef1234"))
        assertEquals("android", serverSourceLabel("", "android", null))
        assertEquals("Unknown device", serverSourceLabel(null, null, null))
        // A route, not a device: the console id says more.
        assertEquals("android · 1234", serverSourceLabel("raw", "android", "abcdef1234"))
        assertEquals("Device 1234", serverSourceLabel("gc_card", null, "abcdef1234"))
        assertEquals("GameCube memory card", serverSourceLabel("gc_card", null, null))
        assertEquals("Raw save upload", serverSourceLabel("raw", "GBA", null))
    }

    @Test
    fun `newer side is display only and tolerates clock noise`() {
        assertEquals(-1, newerSide(localMs = 10_000L, serverMs = 5_000L))
        assertEquals(1, newerSide(localMs = 5_000L, serverMs = 10_000L))
        assertEquals(0, newerSide(localMs = 5_000L, serverMs = 6_000L))
        assertEquals(0, newerSide(localMs = 0L, serverMs = 6_000L))
    }
}
