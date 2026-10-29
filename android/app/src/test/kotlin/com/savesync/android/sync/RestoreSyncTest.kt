package com.savesync.android.sync

import com.savesync.android.api.SaveSyncApi
import com.savesync.android.api.SyncRequest
import com.savesync.android.api.SyncResponse
import com.savesync.android.emulators.SaveEntry
import com.savesync.android.storage.SyncStateDao
import com.savesync.android.storage.SyncStateEntity
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.lang.reflect.Proxy

/**
 * Undo / "Restore previous copy" must hold: the next sync (the background
 * worker's path) never downloads the server's copy over a restored save.
 */
class RestoreSyncTest {

    @get:Rule
    val tmp = TemporaryFolder()

    private class FakeDao : SyncStateDao {
        val rows = mutableMapOf<String, SyncStateEntity>()
        override fun getAll(): Flow<List<SyncStateEntity>> = flowOf(rows.values.toList())
        override suspend fun getById(titleId: String) = rows[titleId]
        override suspend fun upsert(entity: SyncStateEntity) { rows[entity.titleId] = entity }
        override suspend fun deleteById(titleId: String) { rows.remove(titleId) }
        override suspend fun canonicalizeGamecodeTitleIds(): Int = 0
    }

    /** A server deciding exactly as server/app/routes/sync.py does; any byte transfer fails the test. */
    private class ThreeWayServer(val serverHash: String) {
        val calls = mutableListOf<String>()
        val api: SaveSyncApi = Proxy.newProxyInstance(
            SaveSyncApi::class.java.classLoader,
            arrayOf(SaveSyncApi::class.java),
        ) { _, method, args ->
            when (method.name) {
                "sync" -> {
                    val req = args[0] as SyncRequest
                    val upload = mutableListOf<String>()
                    val download = mutableListOf<String>()
                    val conflict = mutableListOf<String>()
                    val upToDate = mutableListOf<String>()
                    for (t in req.titles) {
                        when {
                            t.save_hash == serverHash -> upToDate += t.title_id
                            t.last_synced_hash == null -> download += t.title_id
                            t.last_synced_hash == serverHash -> upload += t.title_id
                            t.last_synced_hash == t.save_hash -> download += t.title_id
                            else -> conflict += t.title_id
                        }
                    }
                    SyncResponse(
                        upload = upload, download = download, conflict = conflict,
                        up_to_date = upToDate, server_only = emptyList(),
                    )
                }
                "toString" -> "ThreeWayServer"
                else -> { calls += method.name; throw UnsupportedOperationException(method.name) }
            }
        } as SaveSyncApi
    }

    private fun hashOf(text: String): String {
        val f = File(tmp.root, "h-${text.hashCode()}").apply { writeText(text) }
        return HashUtils.sha256File(f).also { f.delete() }
    }

    private fun downloadThenUndo(previousBase: String?): Triple<File, FakeDao, LocalBackupStore> = runBlocking {
        val store = LocalBackupStore(File(tmp.root, "backups"))
        val dao = FakeDao()
        val save = tmp.newFile("zelda.srm").apply { writeText("mine") }
        previousBase?.let { dao.upsert(SyncStateEntity("Z", it, 1L, "Zelda", "GBA")) }
        // The download: a copy of "mine" is kept, then the server's save lands.
        backedUpWrite(store, "Z", save, previousBase, 1L) {
            AtomicSaveWriter.replaceFile(save, "server".toByteArray())
        }
        dao.upsert(SyncStateEntity("Z", hashOf("server"), 2L, "Zelda", "GBA"))
        // Undo.
        assertTrue(store.restore("Z", save, dao))
        Triple(save, dao, store)
    }

    private fun entry(save: File) =
        SaveEntry(titleId = "Z", displayName = "Zelda", systemName = "GBA", saveFile = save, saveDir = null)

    @Test
    fun `undo of a download where only the server changed is not downloaded again`() = runBlocking {
        val (save, dao, store) = downloadThenUndo(previousBase = hashOf("mine"))
        val server = ThreeWayServer(hashOf("server"))
        val result = SyncEngine(server.api, dao, "console", backupStore = store).sync(listOf(entry(save)))
        assertEquals(0, result.downloaded)
        assertEquals(listOf("Zelda"), result.conflicts)
        assertTrue("no bytes moved", server.calls.isEmpty())
        assertEquals("mine", save.readText())
    }

    @Test
    fun `undo of a first download (no history) is not downloaded again`() = runBlocking {
        val (save, dao, store) = downloadThenUndo(previousBase = null)
        val server = ThreeWayServer(hashOf("server"))
        val result = SyncEngine(server.api, dao, "console", backupStore = store).sync(listOf(entry(save)))
        assertEquals(0, result.downloaded)
        assertEquals(listOf("Zelda"), result.conflicts)
        assertEquals("mine", save.readText())
    }

    @Test
    fun `a restored save that matches the server becomes synced again`() = runBlocking {
        val (save, dao, store) = downloadThenUndo(previousBase = hashOf("mine"))
        // Another device uploaded the very save that was restored.
        val server = ThreeWayServer(hashOf("mine"))
        val result = SyncEngine(server.api, dao, "console", backupStore = store).sync(listOf(entry(save)))
        assertTrue(result.conflicts.isEmpty())
        assertEquals(hashOf("mine"), dao.rows["Z"]!!.lastSyncedHash)
    }
}
