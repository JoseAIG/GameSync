package com.savesync.android.sync

import com.savesync.android.storage.SyncStateDao
import com.savesync.android.storage.SyncStateEntity
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class LocalBackupsTest {

    @get:Rule
    val tmp = TemporaryFolder()

    /** In-memory sync-state table. */
    private class FakeDao : SyncStateDao {
        val rows = mutableMapOf<String, SyncStateEntity>()
        override fun getAll(): Flow<List<SyncStateEntity>> = flowOf(rows.values.toList())
        override suspend fun getById(titleId: String) = rows[titleId]
        override suspend fun upsert(entity: SyncStateEntity) { rows[entity.titleId] = entity }
        override suspend fun deleteById(titleId: String) { rows.remove(titleId) }
        override suspend fun canonicalizeGamecodeTitleIds(): Int = 0
    }

    /** A clock that moves a minute per call, so every snapshot gets its own name. */
    private fun steppingClock(): () -> Long {
        var t = 1_700_000_000_000L
        return { t += 60_000L; t }
    }

    private fun store() = LocalBackupStore(File(tmp.root, "local_backups"), clock = steppingClock())

    @Test
    fun `six snapshots leave the newest five, the oldest removed`() {
        val s = store()
        val save = tmp.newFile("game.srm")
        val taken = (1..6).map { i ->
            save.writeText("v$i")
            s.snapshot("GBA_game", save, "download", previousLastSyncedHash = "h$i")!!
        }
        val kept = s.list("GBA_game")
        assertEquals(5, kept.size)
        assertFalse("oldest pruned", taken.first().exists())
        assertEquals(taken.drop(1).reversed(), kept.map { it.dir })
        assertEquals("v6", s.latest("GBA_game")!!.payload.readText())
        assertEquals("h6", s.latest("GBA_game")!!.previousLastSyncedHash)
    }

    @Test
    fun `a download retried over the same save renews its snapshot instead of pushing older ones out`() {
        val s = store()
        val save = tmp.newFile("game.srm")
        (1..4).forEach { i ->
            save.writeText("v$i")
            s.snapshot("T", save, "download", previousLastSyncedHash = "h$i")
        }
        // The fifth download fails after its snapshot, every auto sync.
        save.writeText("v5")
        repeat(6) { s.snapshot("T", save, "download", previousLastSyncedHash = "h5") }
        val kept = s.list("T")
        assertEquals(5, kept.size)
        assertEquals(listOf("v5", "v4", "v3", "v2", "v1"), kept.map { it.payload.readText() })
    }

    @Test
    fun `a folder snapshot is renewed only when every file matches`() {
        val s = store()
        val dir = tmp.newFolder("00000001")
        File(dir, "a.bin").writeBytes(byteArrayOf(1))
        val first = s.snapshot("3DS", dir, "download", "h")!!
        val renewed = s.snapshot("3DS", dir, "download", "h")!!
        assertEquals(1, s.list("3DS").size)
        assertFalse(first.exists())
        assertTrue(renewed.exists())
        File(dir, "b.bin").writeBytes(byteArrayOf(2))
        s.snapshot("3DS", dir, "download", "h")
        assertEquals(2, s.list("3DS").size)
    }

    @Test
    fun `nothing is copied before the first download`() {
        val s = store()
        assertNull(s.snapshot("T", File(tmp.root, "missing.sav"), "download", null))
        val emptyDir = tmp.newFolder("emptyslot")
        assertNull(s.snapshot("T", emptyDir, "download", null))
        assertTrue(s.list("T").isEmpty())
    }

    @Test
    fun `a failed snapshot blocks the write`() {
        // The backup root is a file, so no snapshot folder can be made.
        val badRoot = tmp.newFile("not-a-dir")
        val s = LocalBackupStore(badRoot)
        val save = tmp.newFile("card.mcd").apply { writeText("precious") }
        var wrote = false
        try {
            backedUpWrite(s, "PS1_x", save, "h", null) {
                wrote = true
                AtomicSaveWriter.replaceFile(save, "server".toByteArray())
            }
            fail("expected BackupFailedException")
        } catch (e: BackupFailedException) {
            assertEquals("Couldn't back up the current save — not overwritten", e.message)
        }
        assertFalse(wrote)
        assertEquals("precious", save.readText())
    }

    @Test
    fun `restore puts the file back and marks it restored, not the old or the restored hash`() = runBlocking {
        val s = store()
        val dao = FakeDao()
        val save = tmp.newFile("zelda.srm").apply { writeText("mine") }
        dao.upsert(SyncStateEntity("GBA_zelda", "base", 10L, "Zelda", "GBA"))
        // A download: snapshot, then the server's copy replaces it and the
        // sync state records the server hash.
        backedUpWrite(s, "GBA_zelda", save, "base", 10L) {
            AtomicSaveWriter.replaceFile(save, "server".toByteArray())
        }
        dao.upsert(SyncStateEntity("GBA_zelda", "serverhash", 20L, "Zelda", "GBA"))

        assertTrue(s.restore("GBA_zelda", save, dao))

        assertEquals("mine", save.readText())
        assertEquals(LocalBackupStore.RESTORED_BASE, dao.rows["GBA_zelda"]!!.lastSyncedHash)
        assertEquals(10L, dao.rows["GBA_zelda"]!!.lastSyncedAt)
        assertEquals("Zelda", dao.rows["GBA_zelda"]!!.displayName)
        // The replaced copy was kept too, so the restore can be undone.
        assertEquals("server", s.latest("GBA_zelda")!!.payload.readText())
        assertEquals("serverhash", s.latest("GBA_zelda")!!.previousLastSyncedHash)
    }

    @Test
    fun `restore of a first download is marked restored, not left without history`() = runBlocking {
        val s = store()
        val dao = FakeDao()
        val save = tmp.newFile("first.sav").apply { writeText("local") }
        s.snapshot("T", save, "download", previousLastSyncedHash = null)
        save.writeText("server")
        dao.upsert(SyncStateEntity("T", "serverhash", 5L, "T", "GBA"))

        assertTrue(s.restore("T", save, dao))

        assertEquals("local", save.readText())
        // No history would be the server's "download" case again.
        assertEquals(LocalBackupStore.RESTORED_BASE, dao.rows["T"]!!.lastSyncedHash)
    }

    @Test
    fun `restore with a merge writes the merge of the copy and the current file`() = runBlocking {
        val s = store()
        val dao = FakeDao()
        val shared = tmp.newFile("backup.bin").apply { writeText("A0|B0") }
        s.snapshot("SAT_A", shared, "download", "a0")
        shared.writeText("A1|B1")

        assertTrue(s.restore("SAT_A", shared, dao, merge = { old, now ->
            // Take A from the copy, keep B as it is now.
            (String(old).substringBefore('|') + "|" + String(now!!).substringAfter('|')).toByteArray()
        }))

        assertEquals("A0|B1", shared.readText())
    }

    @Test
    fun `restore of a folder save is exact`() = runBlocking {
        val s = store()
        val dao = FakeDao()
        val dir = tmp.newFolder("00000001")
        File(dir, "a.bin").writeBytes(byteArrayOf(1, 2, 3))
        File(dir, "sub").mkdirs()
        File(dir, "sub/b.bin").writeBytes(byteArrayOf(4))
        assertNotNull(s.snapshot("3DS", dir, "download", "base"))
        // The download replaced the tree with different files.
        dir.deleteRecursively(); dir.mkdirs()
        File(dir, "c.bin").writeBytes(byteArrayOf(9))

        assertTrue(s.restore("3DS", dir, dao))

        assertArrayEquals(byteArrayOf(1, 2, 3), File(dir, "a.bin").readBytes())
        assertArrayEquals(byteArrayOf(4), File(dir, "sub/b.bin").readBytes())
        assertFalse("files of the replaced save are gone", File(dir, "c.bin").exists())
        assertEquals(LocalBackupStore.RESTORED_BASE, dao.rows["3DS"]!!.lastSyncedHash)
    }

    @Test
    fun `nothing to restore returns false and touches nothing`() = runBlocking {
        val s = store()
        val dao = FakeDao()
        val save = tmp.newFile("x.sav").apply { writeText("keep") }
        assertFalse(s.restore("none", save, dao))
        assertEquals("keep", save.readText())
    }

    @Test
    fun `stale partial snapshots are removed at start, finished ones kept`() {
        val root = tmp.newFolder("start-backups")
        val store = LocalBackupStore(root)
        val save = tmp.newFile("start.srm").apply { writeText("v1") }
        val done = store.snapshot("T", save, LocalBackupStore.REASON_DOWNLOAD, null)!!
        val stale = File(root, "T/20260101-000000.partial").apply { mkdirs(); File(this, "start.srm").writeText("half") }

        store.removeStalePartials()

        assertFalse(stale.exists())
        assertTrue(done.isDirectory)
        assertEquals(listOf(done), store.list("T").map { it.dir })
    }
}
