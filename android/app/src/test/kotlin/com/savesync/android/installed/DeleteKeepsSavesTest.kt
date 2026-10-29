package com.savesync.android.installed

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

/**
 * Deleting an installed game removes the ROM, never the save beside it.
 * Built from InstalledRom directly so the result doesn't depend on how the
 * test machine's filesystem folds the case of system folder names.
 */
class DeleteKeepsSavesTest {

    @get:Rule
    val tmp = TemporaryFolder()

    private fun gameFolder(vararg names: String): Pair<File, File> {
        val systemRoot = tmp.newFolder("roms", "psx")
        val game = File(systemRoot, "Game").apply { mkdirs() }
        for (n in names) File(game, n).writeBytes(ByteArray(64))
        return systemRoot to game
    }

    @Test
    fun `a folder holding a save is not removed whole and the save survives`() {
        val (systemRoot, game) = gameFolder("game.cue", "game.bin", "game.srm")
        File(game, "game.cue").writeText("FILE \"game.bin\" BINARY\n")
        val rom = InstalledRom(
            path = File(game, "game.cue"),
            system = "PS1",
            displayName = "game",
            filename = "game.cue",
            size = 128,
            systemRoot = systemRoot,
            companionFiles = listOf(File(game, "game.bin")),
        )

        assertFalse(InstalledRomsScanner.wouldRemoveWholeFolder(rom))
        val result = InstalledRomsScanner.deleteInstalled(rom)

        assertNull(result.removedDir)
        assertTrue(result.errors.isEmpty())
        assertFalse(File(game, "game.cue").exists())
        assertFalse(File(game, "game.bin").exists())
        assertTrue("the save stays", File(game, "game.srm").exists())
    }

    @Test
    fun `every save extension blocks the whole-folder delete`() {
        for (ext in InstalledRomsScanner.SAVE_EXTENSIONS) {
            tmp.root.listFiles()?.forEach { it.deleteRecursively() }
            val (systemRoot, game) = gameFolder("game.chd", "game.$ext")
            val rom = InstalledRom(
                path = File(game, "game.chd"),
                system = "PS1",
                displayName = "game",
                filename = "game.chd",
                size = 64,
                systemRoot = systemRoot,
                companionFiles = emptyList(),
            )
            assertFalse(ext, InstalledRomsScanner.wouldRemoveWholeFolder(rom))
        }
    }

    @Test
    fun `save states beside the game block the whole-folder delete`() {
        for (state in listOf("game.state", "game.state1", "game.state12", "game.state.auto", "game.state3.png")) {
            tmp.root.listFiles()?.forEach { it.deleteRecursively() }
            val (systemRoot, game) = gameFolder("game.chd", state)
            val rom = InstalledRom(
                path = File(game, "game.chd"),
                system = "PS1",
                displayName = "game",
                filename = "game.chd",
                size = 64,
                systemRoot = systemRoot,
                companionFiles = emptyList(),
            )
            assertFalse(state, InstalledRomsScanner.wouldRemoveWholeFolder(rom))
            val result = InstalledRomsScanner.deleteInstalled(rom)
            assertNull(result.removedDir)
            assertTrue(state, File(game, state).exists())
        }
    }

    @Test
    fun `a delete confirmed as file-only never removes the folder`() {
        val (systemRoot, game) = gameFolder("game.chd", "readme.txt")
        val rom = InstalledRom(
            path = File(game, "game.chd"),
            system = "PS1",
            displayName = "game",
            filename = "game.chd",
            size = 64,
            systemRoot = systemRoot,
            companionFiles = emptyList(),
        )
        val result = InstalledRomsScanner.deleteInstalled(rom, allowWholeFolder = false)
        assertNull(result.removedDir)
        assertFalse(File(game, "game.chd").exists())
        assertTrue(File(game, "readme.txt").exists())
    }

    @Test
    fun `a folder with only the game and clutter is still removed whole`() {
        val (systemRoot, game) = gameFolder("game.chd", "readme.txt")
        val rom = InstalledRom(
            path = File(game, "game.chd"),
            system = "PS1",
            displayName = "game",
            filename = "game.chd",
            size = 64,
            systemRoot = systemRoot,
            companionFiles = emptyList(),
        )
        assertTrue(InstalledRomsScanner.wouldRemoveWholeFolder(rom))
        assertEquals(2, InstalledRomsScanner.wholeFolderFileCount(rom))
        val result = InstalledRomsScanner.deleteInstalled(rom)
        assertEquals(game, result.removedDir)
        assertFalse(game.exists())
    }
}
