package com.savesync.android.sync

import org.junit.Assert.assertEquals
import org.junit.Test
import java.io.File

/** The ROM download confirm and the download itself pick the same folder. */
class FinalRomDirTest {

    private val psx = File("roms/psx")

    @Test
    fun `a CD game gets its own folder when the setting is on`() {
        assertEquals(File(psx, "Game (USA)"), DownloadManager.finalRomDir(psx, "PS1", "Game (USA).chd", true))
    }

    @Test
    fun `the setting off, or a cartridge system, lands in the system folder`() {
        assertEquals(psx, DownloadManager.finalRomDir(psx, "PS1", "Game (USA).chd", false))
        val gba = File("roms/gba")
        assertEquals(gba, DownloadManager.finalRomDir(gba, "GBA", "Game.gba", true))
    }
}
