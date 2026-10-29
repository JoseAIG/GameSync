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
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.ResponseBody.Companion.toResponseBody
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import retrofit2.Response
import java.io.ByteArrayOutputStream
import java.io.File
import java.lang.reflect.Proxy
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/**
 * The base recorded after a download is what the download wrote, hashed the
 * way the save's local hash is computed, never the save re-read afterwards:
 * the emulator may write between the swap and that re-read, and that new
 * play as the base would make the next sync download over it.
 */
class DownloadRaceTest {

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

    /** Plans a download for every title and serves [raw] / [bundle] as the server's save. */
    private class Server(val raw: ByteArray? = null, val bundle: ByteArray? = null) {
        val api: SaveSyncApi = Proxy.newProxyInstance(
            SaveSyncApi::class.java.classLoader,
            arrayOf(SaveSyncApi::class.java),
        ) { _, method, args ->
            when (method.name) {
                "sync" -> {
                    val req = args[0] as SyncRequest
                    SyncResponse(
                        upload = emptyList(), download = req.titles.map { it.title_id },
                        conflict = emptyList(), up_to_date = emptyList(), server_only = emptyList(),
                    )
                }
                "downloadSaveRaw" -> Response.success(raw!!.toResponseBody("application/octet-stream".toMediaType()))
                "downloadSaveBundle" -> Response.success(bundle!!.toResponseBody("application/octet-stream".toMediaType()))
                "toString" -> "Server"
                else -> throw UnsupportedOperationException(method.name)
            }
        } as SaveSyncApi
    }

    private fun store() = LocalBackupStore(File(tmp.root, "backups"))

    private fun dao() = FakeDao().apply { rows["Z"] = SyncStateEntity("Z", "old-base", 1L, "Zelda", "GBA") }

    @Test
    fun `a single-file save written after the swap is not the base`() = runBlocking {
        val save = tmp.newFile("zelda.srm").apply { writeText("mine") }
        val dao = dao()
        val engine = SyncEngine(Server(raw = "server".toByteArray()).api, dao, "console", backupStore = store())
        engine.beforeDownloadRecorded = { save.writeText("new play") }
        val entry = SaveEntry(titleId = "Z", displayName = "Zelda", systemName = "GBA", saveFile = save, saveDir = null)

        val result = engine.sync(listOf(entry))

        assertEquals(1, result.downloaded)
        assertEquals("new play", save.readText())
        assertEquals(HashUtils.sha256Bytes("server".toByteArray()), dao.rows["Z"]!!.lastSyncedHash)
    }

    @Test
    fun `a 3DS save tree written after the swap is not the base`() = runBlocking {
        val source = tmp.newFolder("server-tree").apply {
            File(this, "main").writeText("server main")
            File(this, "sub").mkdirs()
            File(this, "sub/extra").writeText("server extra")
        }
        val expected = HashUtils.sha256DirTreeFiles(source)
        val saveDir = File(tmp.root, "sdmc/title/00040000/00055d00/data/00000001").apply {
            mkdirs()
            File(this, "main").writeText("mine")
        }
        val dao = FakeDao().apply { rows["0004000000055D00"] = SyncStateEntity("0004000000055D00", "old-base", 1L, "Z", "3DS") }
        val bundle = BundleUtils.createTreeBundle("0004000000055D00", source)
        val engine = SyncEngine(Server(bundle = bundle).api, dao, "console", backupStore = store())
        engine.beforeDownloadRecorded = { File(saveDir, "main").writeText("new play") }
        val entry = SaveEntry(
            titleId = "0004000000055D00", displayName = "Z", systemName = "3DS",
            saveFile = null, saveDir = saveDir, isMultiFile = true,
        )

        assertEquals(1, engine.sync(listOf(entry)).downloaded)
        assertEquals(expected, dao.rows["0004000000055D00"]!!.lastSyncedHash)
    }

    @Test
    fun `a PSP slot merged with its kept files is recorded as written`() = runBlocking {
        val source = tmp.newFolder("server-slot").apply { File(this, "DATA.BIN").writeText("server data") }
        val slot = tmp.newFolder("ULUS10000DATA00").apply {
            File(this, "DATA.BIN").writeText("mine")
            File(this, "ICON0.PNG").writeText("icon")
        }
        val dao = FakeDao().apply { rows["ULUS10000"] = SyncStateEntity("ULUS10000", "old-base", 1L, "P", "PSP") }
        val engine = SyncEngine(Server(bundle = BundleUtils.createBundle("ULUS10000", source)).api, dao, "console", backupStore = store())
        engine.beforeDownloadRecorded = { File(slot, "DATA.BIN").writeText("new play") }
        val entry = SaveEntry(
            titleId = "ULUS10000", displayName = "P", systemName = "PSP",
            saveFile = null, saveDir = slot, isMultiFile = false,
        )

        assertEquals(1, engine.sync(listOf(entry)).downloaded)
        // The swapped-in slot: the server's DATA.BIN plus the kept ICON0.PNG.
        val written = tmp.newFolder("written").apply {
            File(this, "DATA.BIN").writeText("server data")
            File(this, "ICON0.PNG").writeText("icon")
        }
        assertEquals(HashUtils.sha256DirFiles(written), dao.rows["ULUS10000"]!!.lastSyncedHash)
    }

    @Test
    fun `a zipped save folder is recorded as written`() = runBlocking {
        val zip = ByteArrayOutputStream().also { out ->
            ZipOutputStream(out).use { z ->
                z.putNextEntry(ZipEntry("a.sav")); z.write("server a".toByteArray()); z.closeEntry()
            }
        }.toByteArray()
        val dir = tmp.newFolder("folder-save").apply { File(this, "a.sav").writeText("mine") }
        val dao = dao()
        val engine = SyncEngine(Server(raw = zip).api, dao, "console", backupStore = store())
        engine.beforeDownloadRecorded = { File(dir, "a.sav").writeText("new play") }
        val entry = SaveEntry(titleId = "Z", displayName = "Zelda", systemName = "GBA", saveFile = null, saveDir = dir)

        assertEquals(1, engine.sync(listOf(entry)).downloaded)
        val written = tmp.newFolder("written").apply { File(this, "a.sav").writeText("server a") }
        assertEquals(HashUtils.sha256Dir(written), dao.rows["Z"]!!.lastSyncedHash)
    }

    @Test
    fun `force download of a server-only save records what it wrote`() = runBlocking {
        val save = File(tmp.root, "saves/zelda.srm")
        val dao = FakeDao()
        val engine = SyncEngine(Server(raw = "server".toByteArray()).api, dao, "console", backupStore = store())
        engine.beforeDownloadRecorded = { save.writeText("new play") }
        val entry = SaveEntry(
            titleId = "Z", displayName = "Zelda", systemName = "GBA",
            saveFile = save, saveDir = null, isServerOnly = true,
        )

        assertEquals(true, engine.downloadSave(entry, "Z"))
        engine.recordDownloadedState(entry)
        assertEquals(HashUtils.sha256Bytes("server".toByteArray()), dao.rows["Z"]!!.lastSyncedHash)
    }

    // ── writtenFileSave matches the local hash of the same bytes on disk ──

    private fun entryFor(file: File, system: String, extras: List<File> = emptyList()) =
        SaveEntry(titleId = "T", displayName = "T", systemName = system, saveFile = file, extraFiles = extras, saveDir = null)

    @Test
    fun `written hash of a save with companion files matches computeHash`() {
        val dir = tmp.newFolder("gc")
        val main = File(dir, "b-main.gci")
        val extra = File(dir, "a-extra.gci").apply { writeText("extra") }
        val bytes = "downloaded".toByteArray()
        val entry = entryFor(main, "GC", listOf(extra))
        val predicted = writtenFileSave(entry, bytes, SaturnSyncFormat.MEDNAFEN)!!.hash
        main.writeBytes(bytes)
        assertEquals(entry.computeHash(), predicted)
    }

    @Test
    fun `written hash of a PS2 card uses the server's ECC normalisation`() {
        val card = ByteArray(Ps2CardEcc.PS2_SIZE) { 0xFF.toByte() }
        val file = File(tmp.root, "card.ps2")
        val entry = entryFor(file, "PS2")
        val predicted = writtenFileSave(entry, card, SaturnSyncFormat.MEDNAFEN)!!.hash
        file.writeBytes(card)
        assertEquals(entry.computeHash(), predicted)
        assertEquals(Ps2CardEcc.normalizedHash(file), predicted)
    }

    @Test
    fun `written hash of a Saturn save is its canonical hash`() {
        val canonical = SaturnTestData.canonical("GRANDIA_001")
        val written = SaturnSaveFormatConverter.fromCanonical(canonical, SaturnSyncFormat.YABAUSE)
        val file = File(tmp.root, "grandia.bkr")
        val entry = entryFor(file, "SAT")
        val predicted = writtenFileSave(entry, written, SaturnSyncFormat.YABAUSE)!!
        file.writeBytes(written)
        assertEquals(localSyncHash(entry, SaturnSyncFormat.YABAUSE), predicted.hash)
        assertEquals(listOf("GRANDIA_001"), predicted.saturnArchiveNames)
    }

    @Test
    fun `written hash of a shared YabaSanshiro container is this title's part`() {
        val other = SaturnSaveFormatConverter.fromCanonical(SaturnTestData.canonical("DRACULAX_01"), SaturnSyncFormat.YABASANSHIRO)
        val incoming = SaturnTestData.canonical("GRANDIA_001")
        val merged = SaturnSaveFormatConverter.mergeCanonicalIntoYabaSanshiro(other, incoming)
        val entry = entryFor(File(tmp.root, "backup.bin"), "SAT")

        val predicted = writtenFileSave(entry, merged, SaturnSyncFormat.YABASANSHIRO, downloadedCanonical = incoming)!!

        // What saturnCanonicalBytes() reads back once the store holds the server's archive names.
        val readBack = SaturnSaveFormatConverter.extractCanonical(merged, listOf("GRANDIA_001"))
        assertEquals(HashUtils.sha256Bytes(readBack), predicted.hash)
        assertEquals(listOf("GRANDIA_001"), predicted.saturnArchiveNames)
        // No archives to pick: left to the disk re-read, as before.
        assertNull(writtenFileSave(entry, merged, SaturnSyncFormat.YABASANSHIRO, downloadedCanonical = null))
    }

    @Test
    fun `a save folder entry is not hashed from its file`() {
        val entry = SaveEntry(
            titleId = "T", displayName = "T", systemName = "GBA",
            saveFile = File(tmp.root, "x"), saveDir = tmp.newFolder("d"),
        )
        assertNull(writtenFileSave(entry, ByteArray(1), SaturnSyncFormat.MEDNAFEN))
        assertNotNull(stagedDirHash(entry.copy(saveFile = null), tmp.root))
    }
}

/** Minimal canonical Saturn saves (one archive each), laid out as SaturnSaveFormatConverterTest builds them. */
internal object SaturnTestData {
    fun canonical(name: String): ByteArray {
        val raw = ByteArray(40) { (it * 7 and 0xFF).toByte() }
        val buffer = ByteArray(0x8000)
        val magic = "BackUpRam Format".toByteArray(Charsets.US_ASCII)
        var offset = 0
        while (offset < 0x40) {
            magic.copyInto(buffer, offset, 0, minOf(magic.size, 0x40 - offset))
            offset += magic.size
        }
        fun int(at: Int, v: Int) { for (i in 0 until 4) buffer[at + i] = (v ushr (24 - 8 * i)).toByte() }
        fun u16(at: Int, v: Int) { buffer[at] = (v ushr 8).toByte(); buffer[at + 1] = v.toByte() }
        int(0x80, 0x80000000.toInt())
        name.toByteArray(Charsets.US_ASCII).copyOf(11).copyInto(buffer, 0x84)
        "comment".toByteArray(Charsets.US_ASCII).copyOf(10).copyInto(buffer, 0x90)
        int(0x9A, 23797305)
        int(0x9E, raw.size)
        u16(0xA2, 3)
        u16(0xA4, 0)
        val inline = minOf(raw.size, 0x40 - 0x26)
        raw.copyInto(buffer, 0xA6, 0, inline)
        int(3 * 0x40, 0)
        raw.copyInto(buffer, 3 * 0x40 + 4, inline, raw.size)
        return SaturnSaveFormatConverter.toCanonical(buffer)
    }
}
