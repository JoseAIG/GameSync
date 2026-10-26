package com.savesync.android.emulators.impl

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class AzaharEmulatorTest {

    @get:Rule
    val tmp = TemporaryFolder()

    @Test
    fun `discoverSaves reads 3ds title directories`() {
        val titleRoot = File(tmp.root, "azahar-title-root")
        val saveDir = File(titleRoot, "00040000/00054000/data/00000001")
        saveDir.mkdirs()
        File(saveDir, "progress.sav").writeBytes(byteArrayOf(1, 2, 3))
        File(saveDir, "nested").mkdirs()
        File(saveDir, "nested/config.bin").writeBytes(byteArrayOf(4, 5))

        val emulator = AzaharEmulator(candidateTitleRoots = listOf(titleRoot))
        val entries = emulator.discoverSaves()

        assertEquals(1, entries.size)
        val entry = entries.single()
        assertEquals("0004000000054000", entry.titleId)
        assertEquals("3DS", entry.systemName)
        assertTrue(entry.isMultiFile)
        assertEquals(saveDir.absolutePath, entry.saveDir?.absolutePath)
    }

    @Test
    fun `defaultSaveDir builds the expected title path`() {
        val titleRoot = File(tmp.root, "preferred-root")
        val saveDir = AzaharEmulator.defaultSaveDir(
            storageBaseDir = tmp.root,
            titleId = "0004000000030800",
            candidateTitleRoots = listOf(titleRoot)
        )

        assertEquals(
            File(titleRoot, "00040000/00030800/data/00000001").absolutePath,
            saveDir?.absolutePath
        )
    }

    private val zero = "00000000000000000000000000000000"

    private fun azaharUserDir(id0: String = zero, id1: String = zero): Pair<File, File> {
        val user = File(tmp.root, "Azahar")
        val titleRoot = File(user, "sdmc/Nintendo 3DS/$id0/$id1/title")
        val saveDir = File(titleRoot, "00040000/0011c400/data/00000001")
        saveDir.mkdirs()
        File(saveDir, "main").writeBytes(byteArrayOf(1))
        return user to titleRoot
    }

    @Test
    fun `override accepts every level of the azahar user folder`() {
        val (user, titleRoot) = azaharUserDir()
        val levels = listOf(
            user,
            File(user, "sdmc"),
            File(user, "sdmc/Nintendo 3DS"),
            File(user, "sdmc/Nintendo 3DS/$zero"),
            File(user, "sdmc/Nintendo 3DS/$zero/$zero"),
            titleRoot,
        )
        for (level in levels) {
            assertEquals(
                "override at $level",
                titleRoot.absolutePath,
                AzaharEmulator.resolveOverrideTitleRoot(level).absolutePath
            )
            val entries = AzaharEmulator(saveDirOverride = level.path).discoverSaves()
            assertEquals("override at $level", listOf("000400000011C400"), entries.map { it.titleId })
        }
    }

    @Test
    fun `non-zero id0 and id1 are detected`() {
        val id0 = "0123456789abcdef0123456789abcdef"
        val id1 = "fedcba9876543210fedcba9876543210"
        val (user, titleRoot) = azaharUserDir(id0, id1)

        assertEquals(
            titleRoot.absolutePath,
            AzaharEmulator.resolveOverrideTitleRoot(user).absolutePath
        )
        // Auto-detection (no override) under a storage root holding Azahar/.
        val entries = AzaharEmulator(storageBaseDir = tmp.root).discoverSaves()
        assertEquals(listOf("000400000011C400"), entries.map { it.titleId })
    }

    @Test
    fun `missing override does not fall back to auto-detected saves`() {
        azaharUserDir()  // auto-detection would find this one
        val missing = File(tmp.root, "unmounted-sd/Azahar").path

        val entries = AzaharEmulator(
            storageBaseDir = tmp.root,
            saveDirOverride = missing
        ).discoverSaves()

        assertTrue(entries.isEmpty())
        assertNotNull(AzaharEmulator.overrideWarning(missing))
    }

    @Test
    fun `override warning flags folders without a 3ds tree`() {
        val (user, _) = azaharUserDir()
        assertNull(AzaharEmulator.overrideWarning(user.path))
        assertNull(AzaharEmulator.overrideWarning(null))
        assertNull(AzaharEmulator.overrideWarning(""))

        val fresh = File(tmp.root, "FreshAzahar/sdmc")
        fresh.mkdirs()
        assertNotNull(AzaharEmulator.overrideWarning(fresh.parentFile!!.path))
    }
}
