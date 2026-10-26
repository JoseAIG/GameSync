package com.savesync.android.sync

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.io.IOException

class AtomicSaveWriterTest {

    @get:Rule
    val tmp = TemporaryFolder()

    /** Azahar-shaped save dir: title/<high>/<low>/data/00000001 */
    private fun azaharSaveDir(): File =
        File(tmp.root, "title/00040000/00055d00/data/00000001")

    private fun seedOldSave(dir: File) {
        dir.mkdirs()
        File(dir, "main").writeBytes(byteArrayOf(1, 1, 1))
        File(dir, "stale.bin").writeBytes(byteArrayOf(9))
    }

    private fun relativeFiles(dir: File): Set<String> =
        dir.walkTopDown().filter { it.isFile }
            .map { it.relativeTo(dir).path.replace('\\', '/') }
            .toSet()

    private fun noLeftovers(dir: File) {
        assertFalse(AtomicSaveWriter.stagingFor(dir).exists())
        assertFalse(AtomicSaveWriter.backupFor(dir).exists())
    }

    @Test
    fun `replaceDirectory swaps in exactly the bundle files`() {
        val dir = azaharSaveDir()
        seedOldSave(dir)

        AtomicSaveWriter.replaceDirectory(
            dir,
            listOf(
                "main" to byteArrayOf(2, 2),
                "sub/extra.dat" to byteArrayOf(3)
            )
        )

        assertEquals(setOf("main", "sub/extra.dat"), relativeFiles(dir))
        assertArrayEquals(byteArrayOf(2, 2), File(dir, "main").readBytes())
        noLeftovers(dir)
    }

    @Test
    fun `replaceDirectory creates a save that did not exist yet`() {
        val dir = azaharSaveDir()

        AtomicSaveWriter.replaceDirectory(dir, listOf("main" to byteArrayOf(5)))

        assertEquals(setOf("main"), relativeFiles(dir))
        noLeftovers(dir)
    }

    @Test
    fun `unsafe entry name aborts and keeps the old save untouched`() {
        val dir = azaharSaveDir()
        seedOldSave(dir)

        try {
            AtomicSaveWriter.replaceDirectory(
                dir,
                listOf("main" to byteArrayOf(2), "../../escape" to byteArrayOf(6))
            )
            fail("expected IOException")
        } catch (_: IOException) {
        }

        assertEquals(setOf("main", "stale.bin"), relativeFiles(dir))
        assertArrayEquals(byteArrayOf(1, 1, 1), File(dir, "main").readBytes())
        assertFalse(File(dir.parentFile, "escape").exists())
        noLeftovers(dir)
    }

    @Test
    fun `failure while populating keeps the old save untouched`() {
        val dir = azaharSaveDir()
        seedOldSave(dir)

        try {
            AtomicSaveWriter.replaceDirectory(dir, keepExisting = false) { staging ->
                File(staging, "main").writeBytes(byteArrayOf(7))
                throw IOException("disk full")
            }
            fail("expected IOException")
        } catch (_: IOException) {
        }

        assertEquals(setOf("main", "stale.bin"), relativeFiles(dir))
        assertArrayEquals(byteArrayOf(1, 1, 1), File(dir, "main").readBytes())
        noLeftovers(dir)
    }

    @Test
    fun `failed swap restores the previous save`() {
        val dir = azaharSaveDir()
        seedOldSave(dir)
        val staging = AtomicSaveWriter.stagingFor(dir).absoluteFile

        try {
            AtomicSaveWriter.replaceDirectory(
                dir,
                keepExisting = false,
                // Moving the old save aside works; moving the new one in fails.
                rename = { from, to -> if (from.absoluteFile == staging) false else from.renameTo(to) }
            ) { s -> File(s, "main").writeBytes(byteArrayOf(8)) }
            fail("expected IOException")
        } catch (_: IOException) {
        }

        assertEquals(setOf("main", "stale.bin"), relativeFiles(dir))
        assertArrayEquals(byteArrayOf(1, 1, 1), File(dir, "main").readBytes())
        noLeftovers(dir)
    }

    @Test
    fun `interrupted swap from an earlier run is recovered before writing`() {
        val dir = azaharSaveDir()
        // Simulate a crash between "old → backup" and "new → target".
        seedOldSave(AtomicSaveWriter.backupFor(dir))
        assertFalse(dir.exists())

        try {
            AtomicSaveWriter.replaceDirectory(dir, keepExisting = false) { throw IOException("boom") }
            fail("expected IOException")
        } catch (_: IOException) {
        }

        // The backed-up save is back in place even though this write failed.
        assertEquals(setOf("main", "stale.bin"), relativeFiles(dir))
        noLeftovers(dir)
    }

    @Test
    fun `keepExisting overlays bundle files onto the current directory`() {
        val dir = File(tmp.root, "PSP/SAVEDATA/ULUS10041")
        seedOldSave(dir)

        AtomicSaveWriter.replaceDirectory(dir, listOf("main" to byteArrayOf(4)), keepExisting = true)

        assertEquals(setOf("main", "stale.bin"), relativeFiles(dir))
        assertArrayEquals(byteArrayOf(4), File(dir, "main").readBytes())
        noLeftovers(dir)
    }

    @Test
    fun `replaceFile writes new contents and leaves no temp file`() {
        val file = File(tmp.root, "saves/game.srm")
        file.parentFile!!.mkdirs()
        file.writeBytes(byteArrayOf(1, 2, 3))

        AtomicSaveWriter.replaceFile(file, byteArrayOf(4, 5))

        assertArrayEquals(byteArrayOf(4, 5), file.readBytes())
        assertFalse(AtomicSaveWriter.stagingFor(file).exists())
        assertEquals(listOf("game.srm"), file.parentFile!!.list()!!.toList())
    }

    @Test
    fun `replaceFile creates missing parent directories`() {
        val file = File(tmp.root, "new/dir/card.mcd")

        AtomicSaveWriter.replaceFile(file, byteArrayOf(1))

        assertTrue(file.isFile)
        assertArrayEquals(byteArrayOf(1), file.readBytes())
    }
}
