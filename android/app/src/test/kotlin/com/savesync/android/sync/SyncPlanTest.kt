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
import okhttp3.ResponseBody.Companion.toResponseBody
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import retrofit2.HttpException
import retrofit2.Response
import java.io.File
import java.lang.reflect.Proxy
import java.util.concurrent.atomic.AtomicInteger

/**
 * Sync all's plan/execute split and its rollback protection (M5-1):
 * the size/count heuristic, the 20-check cap, suspects excluded by default
 * in sync() (the worker's path) and in execute(), and a stale preview that
 * no longer matches the disk never acting.
 */
class SyncPlanTest {

    @get:Rule
    val tmp = TemporaryFolder()

    // ── Pure heuristic ──────────────────────────────────────────────────

    @Test
    fun `an upload under half the server size is suspect`() {
        assertTrue(RollbackCheck.isSuspect(localSize = 49, localFileCount = null, serverSize = 100, serverFileCount = null))
        assertFalse(RollbackCheck.isSuspect(localSize = 50, localFileCount = null, serverSize = 100, serverFileCount = null))
        assertFalse(RollbackCheck.isSuspect(localSize = 500, localFileCount = null, serverSize = 100, serverFileCount = null))
    }

    @Test
    fun `fewer files than the server copy is suspect, for multi-file saves only`() {
        assertTrue(RollbackCheck.isSuspect(localSize = 100, localFileCount = 2, serverSize = 100, serverFileCount = 3))
        assertFalse(RollbackCheck.isSuspect(localSize = 100, localFileCount = 3, serverSize = 100, serverFileCount = 3))
        // A single-file save has no local count: the server's count is ignored.
        assertFalse(RollbackCheck.isSuspect(localSize = 100, localFileCount = null, serverSize = 100, serverFileCount = 3))
    }

    @Test
    fun `unknown server values never make a suspect`() {
        assertFalse(RollbackCheck.isSuspect(localSize = 1, localFileCount = 1, serverSize = null, serverFileCount = null))
        assertFalse(RollbackCheck.isSuspect(localSize = 0, localFileCount = null, serverSize = 0, serverFileCount = null))
    }

    @Test
    fun `at most 20 uploads are checked`() {
        val (checked, rest) = RollbackCheck.splitForChecks((1..25).toList())
        assertEquals((1..20).toList(), checked)
        assertEquals((21..25).toList(), rest)
        val (small, none) = RollbackCheck.splitForChecks(listOf(1, 2))
        assertEquals(listOf(1, 2), small)
        assertTrue(none.isEmpty())
    }

    @Test
    fun `suspects are skipped unless included`() {
        val skip = RollbackCheck.skippedTitles(
            suspectIds = setOf("A", "B"),
            excluded = setOf("C"),
            includeSuspects = setOf("B"),
        )
        assertEquals(setOf("A", "C"), skip)
    }

    @Test
    fun `memory card decisions match the three-way rule`() {
        // No card on the server: upload ours, or nothing when we have none.
        assertEquals(PlannedAction.UPLOAD, decideRawCard(true, "h1", null, null, serverHasSave = false))
        assertNull(decideRawCard(false, "", null, null, serverHasSave = false))
        assertEquals(PlannedAction.UP_TO_DATE, decideRawCard(true, "h1", null, "h1", serverHasSave = true))
        assertEquals(PlannedAction.DOWNLOAD_NEW, decideRawCard(false, "", null, "h1", serverHasSave = true))
        assertEquals(PlannedAction.UPLOAD, decideRawCard(true, "h2", "h1", "h1", serverHasSave = true))
        assertEquals(PlannedAction.DOWNLOAD, decideRawCard(true, "h1", "h1", "h2", serverHasSave = true))
        assertEquals(PlannedAction.CONFLICT, decideRawCard(true, "h2", "h1", "h3", serverHasSave = true))
        assertEquals(PlannedAction.CONFLICT, decideRawCard(true, "h2", null, "h3", serverHasSave = true))
    }

    @Test
    fun `a re-checked title runs only when its decision is unchanged`() {
        assertTrue(stillPlanned(PlannedAction.UPLOAD, PlannedAction.UPLOAD))
        assertFalse(stillPlanned(PlannedAction.DOWNLOAD, PlannedAction.CONFLICT))
        assertFalse(stillPlanned(PlannedAction.UPLOAD, null))
    }

    // ── plan() / execute() against a fake server ────────────────────────

    private class FakeDao : SyncStateDao {
        val rows = mutableMapOf<String, SyncStateEntity>()
        override fun getAll(): Flow<List<SyncStateEntity>> = flowOf(rows.values.toList())
        override suspend fun getById(titleId: String) = rows[titleId]
        override suspend fun upsert(entity: SyncStateEntity) { rows[entity.titleId] = entity }
        override suspend fun deleteById(titleId: String) { rows.remove(titleId) }
        override suspend fun canonicalizeGamecodeTitleIds(): Int = 0
    }

    /** A server that plans every title as an upload and reports [serverSize] for each. */
    private class FakeServer(
        val serverSize: Long,
        val planUpload: Boolean = true,
    ) {
        val uploads = mutableListOf<String>()
        val metaRequests = AtomicInteger()
        val syncRequests = mutableListOf<SyncRequest>()
        /** Titles with no save on the server (meta 404). */
        val missing = mutableSetOf<String>()

        val api: SaveSyncApi = Proxy.newProxyInstance(
            SaveSyncApi::class.java.classLoader,
            arrayOf(SaveSyncApi::class.java),
        ) { _, method, args ->
            when (method.name) {
                "sync" -> {
                    val req = args[0] as SyncRequest
                    syncRequests += req
                    val ids = req.titles.map { it.title_id }
                    SyncResponse(
                        upload = if (planUpload) ids else emptyList(),
                        download = emptyList(),
                        conflict = if (planUpload) emptyList() else ids,
                        up_to_date = emptyList(),
                        server_only = emptyList(),
                    )
                }
                "getSaveMeta" -> {
                    metaRequests.incrementAndGet()
                    val id = args[0] as String
                    if (id in missing) throw HttpException(Response.error<Any>(404, "".toResponseBody(null)))
                    SaveMeta(
                        title_id = id, save_hash = "server", save_size = serverSize,
                        client_timestamp = null, platform = null,
                    )
                }
                "uploadSaveRaw" -> {
                    uploads += args[0] as String
                    UploadResponse(status = "ok", timestamp = "", sha256 = "")
                }
                "toString" -> "FakeServer"
                else -> throw UnsupportedOperationException(method.name)
            }
        } as SaveSyncApi
    }

    private fun save(id: String, bytes: Int): SaveEntry {
        val f = File(tmp.root, "$id.srm").apply { writeBytes(ByteArray(bytes) { 7 }) }
        return SaveEntry(titleId = id, displayName = "Game $id", systemName = "GBA", saveFile = f, saveDir = null)
    }

    /** A dao where every title was synced once, with the server's hash as the base (→ upload). */
    private fun syncedDao(vararg ids: String) = FakeDao().apply {
        ids.forEach { rows[it] = SyncStateEntity(it, "server", 1L, it, "GBA") }
    }

    @Test
    fun `sync holds back an upload much smaller than the server copy`() = runBlocking {
        val server = FakeServer(serverSize = 1000)
        val engine = SyncEngine(server.api, syncedDao("SMALL"), "console")
        val result = engine.sync(listOf(save("SMALL", 10)))
        assertTrue("a suspect is never uploaded by sync()", server.uploads.isEmpty())
        assertEquals(listOf("Game SMALL"), result.skippedSuspects)
        assertEquals(0, result.uploaded)
        assertTrue(result.errors.isEmpty())
    }

    @Test
    fun `the plan lists the suspect and execute uploads it only when included`() = runBlocking {
        val server = FakeServer(serverSize = 1000)
        val engine = SyncEngine(server.api, syncedDao("SMALL", "BIG"), "console")
        val preview = engine.plan(listOf(save("SMALL", 10), save("BIG", 900)))
        assertEquals(listOf("SMALL"), preview.suspects.map { it.titleId })
        assertEquals(1, preview.safeUploads)
        assertEquals(1, preview.actionableCount())
        assertEquals(2, preview.actionableCount(includeSuspects = setOf("SMALL")))
        assertTrue("planning moves no bytes", server.uploads.isEmpty())

        val result = engine.execute(preview, emptySet(), includeSuspects = setOf("SMALL"))
        assertEquals(listOf("SMALL", "BIG"), server.uploads)
        assertEquals(2, result.uploaded)
        assertTrue(result.skippedSuspects.isEmpty())
    }

    @Test
    fun `execute with nothing included skips the suspect`() = runBlocking {
        val server = FakeServer(serverSize = 1000)
        val engine = SyncEngine(server.api, syncedDao("SMALL", "BIG"), "console")
        val preview = engine.plan(listOf(save("SMALL", 10), save("BIG", 900)))
        val result = engine.execute(preview, emptySet())
        assertEquals(listOf("BIG"), server.uploads)
        assertEquals(listOf("Game SMALL"), result.skippedSuspects)
    }

    @Test
    fun `excluded titles are left alone`() = runBlocking {
        val server = FakeServer(serverSize = 100)
        val engine = SyncEngine(server.api, syncedDao("A", "B"), "console")
        val preview = engine.plan(listOf(save("A", 100), save("B", 100)))
        engine.execute(preview, excluded = setOf("A"))
        assertEquals(listOf("B"), server.uploads)
    }

    @Test
    fun `no server copy is not a suspect`() = runBlocking {
        val server = FakeServer(serverSize = 1000).apply { missing += "NEW" }
        val engine = SyncEngine(server.api, FakeDao(), "console")
        val preview = engine.plan(listOf(save("NEW", 10)))
        assertTrue(preview.suspects.isEmpty())
        assertEquals(0, preview.sizeCheckSkipped)
    }

    @Test
    fun `only the first 20 uploads get a size check`() = runBlocking {
        val server = FakeServer(serverSize = 1000)
        val ids = (1..25).map { "T%02d".format(it) }
        val engine = SyncEngine(server.api, syncedDao(*ids.toTypedArray()), "console")
        val preview = engine.plan(ids.map { save(it, 10) })
        assertEquals(20, server.metaRequests.get())
        assertEquals(5, preview.sizeCheckSkipped)
        // The 20 checked are suspects; the 5 unchecked are treated as not suspect.
        assertEquals(ids.take(20), preview.suspects.map { it.titleId })
        assertEquals(5, preview.safeUploads)
    }

    @Test
    fun `a stale preview whose save changed does not act`() = runBlocking {
        // The re-check finds a conflict where the preview had an upload.
        val server = FakeServer(serverSize = 100)
        val dao = syncedDao("A")
        val engine = SyncEngine(server.api, dao, "console")
        val entry = save("A", 100)
        val preview = engine.plan(listOf(entry))
        val stale = SyncPreview(
            titles = preview.titles,
            suspects = preview.suspects,
            sizeCheckSkipped = preview.sizeCheckSkipped,
            errors = preview.errors,
            createdAtMs = 0L,
            entries = preview.entries,
            conflictInfo = preview.conflictInfo,
        )
        val conflicting = FakeServer(serverSize = 100, planUpload = false)
        val later = SyncEngine(conflicting.api, dao, "console")
        val result = later.execute(stale, emptySet())
        assertTrue(conflicting.uploads.isEmpty())
        assertEquals(listOf("Game A"), result.changedSincePreview)
        assertEquals(0, result.uploaded)
        assertEquals(1, conflicting.syncRequests.size)
    }

    @Test
    fun `a fresh preview runs without asking again`() = runBlocking {
        val server = FakeServer(serverSize = 100)
        val engine = SyncEngine(server.api, syncedDao("A"), "console")
        val preview = engine.plan(listOf(save("A", 100)))
        engine.execute(preview, emptySet())
        assertEquals(1, server.syncRequests.size)
        assertEquals(listOf("A"), server.uploads)
    }

    // ── Held-back report ────────────────────────────────────────────────

    @Test
    fun `held-back names are reported once`() {
        val store = HeldBackUploadStore(File(tmp.root, "held.txt"))
        store.record(listOf("Zelda", "Mario"))
        store.record(listOf("Mario", "Metroid"))
        assertEquals(listOf("Zelda", "Mario", "Metroid"), store.take())
        assertTrue(store.take().isEmpty())
    }

    @Test
    fun `held-back notice names the first two`() {
        assertEquals(
            "Auto sync didn't upload 1 save smaller than the server copy (Zelda). Sync all to review.",
            heldBackNoticeText(listOf("Zelda")),
        )
        assertEquals(
            "Auto sync didn't upload 3 saves smaller than the server copy (A, B and 1 more). Sync all to review.",
            heldBackNoticeText(listOf("A", "B", "C")),
        )
    }
}
