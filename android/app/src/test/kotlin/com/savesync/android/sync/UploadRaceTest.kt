package com.savesync.android.sync

import com.savesync.android.api.SaveMeta
import com.savesync.android.api.SaveSyncApi
import com.savesync.android.api.SyncRequest
import com.savesync.android.api.SyncResponse
import com.savesync.android.api.UploadResponse
import com.savesync.android.emulators.SaveEntry
import com.savesync.android.storage.SyncStateDao
import com.savesync.android.storage.SyncStateEntity
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.lang.reflect.Proxy

/**
 * The base recorded after an upload is the copy the server got, even when
 * the emulator writes the save during the transfer; otherwise the next sync
 * would see "only the server changed" and download the older copy over the
 * newer play.
 */
class UploadRaceTest {

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

    /** Plans an upload, or reports up to date when [upToDateHash] is what the client sent. */
    private class Server(val duringUpload: () -> Unit = {}, val upToDateHash: String? = null) {
        var uploaded: ByteArray? = null
        val api: SaveSyncApi = Proxy.newProxyInstance(
            SaveSyncApi::class.java.classLoader,
            arrayOf(SaveSyncApi::class.java),
        ) { _, method, args ->
            when (method.name) {
                "sync" -> {
                    val req = args[0] as SyncRequest
                    val agreed = req.titles.filter { it.save_hash == upToDateHash }.map { it.title_id }
                    SyncResponse(
                        upload = req.titles.map { it.title_id } - agreed.toSet(),
                        download = emptyList(), conflict = emptyList(),
                        up_to_date = agreed, server_only = emptyList(),
                    )
                }
                "getSaveMeta" -> SaveMeta(
                    title_id = args[0] as String, save_hash = "server", save_size = 4,
                    client_timestamp = null, platform = null,
                )
                "uploadSaveRaw" -> {
                    val body = args[3] as okhttp3.RequestBody
                    val buf = okio.Buffer().also { body.writeTo(it) }
                    uploaded = buf.readByteArray()
                    duringUpload()
                    UploadResponse(status = "ok", timestamp = "", sha256 = "")
                }
                "toString" -> "Server"
                else -> throw UnsupportedOperationException(method.name)
            }
        } as SaveSyncApi
    }

    private fun hashOf(text: String): String {
        val f = File(tmp.root, "h-${text.hashCode()}").apply { writeText(text) }
        return HashUtils.sha256File(f).also { f.delete() }
    }

    private fun entry(save: File) =
        SaveEntry(titleId = "Z", displayName = "Zelda", systemName = "GBA", saveFile = save, saveDir = null)

    private fun syncedDao() = FakeDao().apply { rows["Z"] = SyncStateEntity("Z", "server", 1L, "Zelda", "GBA") }

    @Test
    fun `a save written during its upload keeps the uploaded copy as the base`() = runBlocking {
        val save = tmp.newFile("zelda.srm").apply { writeText("old1") }
        val dao = syncedDao()
        val server = Server(duringUpload = { save.writeText("new2") })
        val result = SyncEngine(server.api, dao, "console").sync(listOf(entry(save)))

        assertEquals(1, result.uploaded)
        assertEquals("old1", String(server.uploaded!!))
        // Local "new2" differs from this base, which is the server's: the next sync uploads it.
        assertEquals(hashOf("old1"), dao.rows["Z"]!!.lastSyncedHash)
    }

    @Test
    fun `an upload with no write during it records the save as it is`() = runBlocking {
        val save = tmp.newFile("zelda.srm").apply { writeText("old1") }
        val dao = syncedDao()
        SyncEngine(Server().api, dao, "console").sync(listOf(entry(save)))
        assertEquals(hashOf("old1"), dao.rows["Z"]!!.lastSyncedHash)
    }

    @Test
    fun `up to date with a stale base records the agreed hash`() = runBlocking {
        val save = tmp.newFile("zelda.srm").apply { writeText("new2") }
        val dao = FakeDao().apply { rows["Z"] = SyncStateEntity("Z", hashOf("old1"), 1L, "Zelda", "GBA") }
        val result = SyncEngine(Server(upToDateHash = hashOf("new2")).api, dao, "console").sync(listOf(entry(save)))
        assertEquals(0, result.uploaded)
        assertEquals(hashOf("new2"), dao.rows["Z"]!!.lastSyncedHash)
    }
}
