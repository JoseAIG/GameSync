package com.savesync.android.emulators

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class FolderScanTest {

    @get:Rule
    val tmp = TemporaryFolder()

    @Test
    fun `ROMs are counted by extension, most common first`() {
        val check = FolderScan.summarize(
            fileNames = List(12) { "Game $it.3ds" } + listOf("Other.cia"),
            dirNames = emptyList(),
        )
        assertTrue(check.ok)
        assertEquals("Contains: 13 ROMs (.3ds, .cia)", check.summary)
    }

    @Test
    fun `saves, system folders and the rest`() {
        val check = FolderScan.summarize(
            fileNames = listOf("Zelda.srm", "readme.txt"),
            dirNames = listOf("psx", "GBA", "snes", "Saturn", "misc"),
        )
        assertTrue(check.ok)
        assertEquals(
            "Contains: 1 save (.srm), 4 system folders (GBA, psx, Saturn, +1), 1 other folder, 1 other file",
            check.summary,
        )
    }

    @Test
    fun `an EmuDeck root is recognised`() {
        val check = FolderScan.summarize(emptyList(), listOf("roms", "saves", "storage", "bios"))
        assertTrue(check.ok)
        assertEquals("Contains: EmuDeck folders (bios, roms, saves, storage)", check.summary)
    }

    @Test
    fun `a roms folder alone is not an EmuDeck root`() {
        val check = FolderScan.summarize(emptyList(), listOf("roms"))
        assertFalse(check.ok)
        assertEquals("Contains: 1 folder — nothing GameSync recognises", check.summary)
    }

    @Test
    fun `nothing recognisable is not ok`() {
        val check = FolderScan.summarize(listOf("a.txt", "b.jpg"), listOf("Music", "DCIM"))
        assertFalse(check.ok)
        assertEquals("Contains: 2 folders, 2 files — nothing GameSync recognises", check.summary)
    }

    @Test
    fun `an empty folder is not ok`() {
        assertEquals(DirCheck(false, "Empty folder"), FolderScan.summarize(emptyList(), emptyList()))
    }

    @Test
    fun `describe reads one level and skips hidden entries`() {
        val dir = tmp.newFolder("roms")
        File(dir, "Game.gba").writeText("x")
        File(dir, ".nomedia").writeText("")
        File(dir, ".thumbs").mkdirs()
        File(dir, "gb").mkdirs()
        File(dir, "gb/Deep.gb").writeText("x") // not counted: one level only
        assertEquals(
            DirCheck(true, "Contains: 1 ROM (.gba), 1 system folder (gb)"),
            FolderScan.describe(dir),
        )
    }
}
