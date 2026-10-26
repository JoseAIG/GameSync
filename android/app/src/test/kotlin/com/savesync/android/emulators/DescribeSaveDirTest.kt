package com.savesync.android.emulators

import com.savesync.android.emulators.impl.AzaharEmulator
import com.savesync.android.emulators.impl.RetroArchEmulator
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class DescribeSaveDirTest {

    @get:Rule
    val tmp = TemporaryFolder()

    private val zero = "0".repeat(32)

    /** <user>/sdmc/Nintendo 3DS/<id0>/<id1>/title/<high>/<low>/data/00000001/ */
    private fun azaharUser(vararg titles: String): File {
        val user = tmp.newFolder("Azahar")
        val titleRoot = File(user, "sdmc/Nintendo 3DS/$zero/$zero/title").apply { mkdirs() }
        titles.forEach { id ->
            File(titleRoot, "${id.take(8)}/${id.drop(8)}/data/00000001").mkdirs()
        }
        return user
    }

    @Test
    fun `a valid Azahar tree names where the saves are and counts them`() {
        val user = azaharUser("0004000000055d00", "00040000001b5000")
        val check = EmulatorRegistry.describeSaveDir(AzaharEmulator.EMULATOR_KEY, user)
        assertTrue(check.ok)
        assertEquals("Found sdmc/Nintendo 3DS/…/title (2 titles)", check.summary)
    }

    @Test
    fun `one title is singular`() {
        val user = azaharUser("0004000000055d00")
        assertEquals(
            "Found sdmc/Nintendo 3DS/…/title (1 title)",
            EmulatorRegistry.describeSaveDir(AzaharEmulator.EMULATOR_KEY, user).summary,
        )
    }

    @Test
    fun `a missing title folder is not ok and says why`() {
        val user = tmp.newFolder("Azahar")
        File(user, "sdmc/Nintendo 3DS").mkdirs()
        val check = EmulatorRegistry.describeSaveDir(AzaharEmulator.EMULATOR_KEY, user)
        assertFalse(check.ok)
        // The same words as the persistent override warning.
        assertEquals(AzaharEmulator.overrideWarning(user.path), check.summary)
    }

    @Test
    fun `an empty folder is not ok`() {
        val empty = tmp.newFolder("empty")
        val check = EmulatorRegistry.describeSaveDir(AzaharEmulator.EMULATOR_KEY, empty)
        assertFalse(check.ok)
        assertTrue(check.summary, check.summary.startsWith("No 3DS saves in empty yet"))
    }

    @Test
    fun `a title tree with no saves yet is not ok`() {
        val user = azaharUser()
        val check = EmulatorRegistry.describeSaveDir(AzaharEmulator.EMULATOR_KEY, user)
        assertFalse(check.ok)
        assertTrue(check.summary, check.summary.startsWith("No 3DS saves in sdmc/Nintendo 3DS/…/title yet"))
    }

    @Test
    fun `an emulator without its own check falls back to the generic count`() {
        val dir = tmp.newFolder("saves")
        File(dir, "Game.srm").writeText("x")
        val check = EmulatorRegistry.describeSaveDir(RetroArchEmulator.EMULATOR_KEY, dir)
        assertEquals(FolderScan.describe(dir), check)
        assertTrue(check.ok)
    }
}
