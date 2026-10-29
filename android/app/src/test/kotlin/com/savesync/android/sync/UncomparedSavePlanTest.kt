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
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.lang.reflect.Proxy

/**
 * A local save the plan couldn't hash is left out of POST /sync, so the
 * server lists it under server_only.  It must not become a blind download.
 */
class UncomparedSavePlanTest {

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

    @Test
    fun `an unreadable local save listed as server only is left alone`() = runBlocking {
        val good = File(tmp.root, "good.srm").apply { writeText("good") }
        val bad = File(tmp.root, "bad.srm").apply { writeText("new play") }
        assumeTrue("needs a file the test can make unreadable", bad.setReadable(false) && !bad.canRead())

        val transfers = mutableListOf<String>()
        val api = Proxy.newProxyInstance(SaveSyncApi::class.java.classLoader, arrayOf(SaveSyncApi::class.java)) { _, m, args ->
            when (m.name) {
                "sync" -> {
                    val sent = (args[0] as SyncRequest).titles.map { it.title_id }
                    // As server/app/routes/sync.py: every stored same-family
                    // title missing from the request is "server only".
                    SyncResponse(
                        upload = emptyList(), download = emptyList(), conflict = emptyList(),
                        up_to_date = sent, server_only = listOf("BAD") - sent.toSet(),
                    )
                }
                "toString" -> "api"
                else -> { transfers += m.name; throw UnsupportedOperationException(m.name) }
            }
        } as SaveSyncApi

        val entries = listOf(
            SaveEntry(titleId = "GOOD", displayName = "Good", systemName = "GBA", saveFile = good, saveDir = null),
            SaveEntry(titleId = "BAD", displayName = "Bad", systemName = "GBA", saveFile = bad, saveDir = null),
        )
        val engine = SyncEngine(api, FakeDao(), "console", backupStore = LocalBackupStore(File(tmp.root, "b")))
        val preview = engine.plan(entries)
        assertTrue(preview.titles.none { it.titleId == "BAD" })
        assertTrue(preview.errors.any { it.startsWith("Hash error: Bad") })

        val result = engine.sync(entries)
        assertEquals(0, result.downloaded)
        assertTrue("no bytes moved", transfers.isEmpty())
        bad.setReadable(true)
        assertEquals("new play", bad.readText())
    }
}
