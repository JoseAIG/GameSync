package com.savesync.android.emulators

import com.savesync.android.emulators.impl.AzaharEmulator
import com.savesync.android.emulators.impl.RetroArchEmulator
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class SaveDirOverrideWarningMapTest {

    @get:Rule
    val tmp = TemporaryFolder()

    @Test
    fun `a missing Azahar folder is reported under its key, others are not`() {
        val missing = File(tmp.root, "nowhere").path
        val map = EmulatorRegistry.saveDirOverrideWarningMap(
            mapOf(
                AzaharEmulator.EMULATOR_KEY to missing,
                // RetroArch has no folder check: never reported.
                RetroArchEmulator.EMULATOR_KEY to missing,
            )
        )
        assertEquals(setOf(AzaharEmulator.EMULATOR_KEY), map.keys)
        assertTrue(map.getValue(AzaharEmulator.EMULATOR_KEY).contains(missing))
        // The list form used elsewhere stays the same set of messages.
        assertEquals(map.values.toList(), EmulatorRegistry.saveDirOverrideWarnings(
            mapOf(AzaharEmulator.EMULATOR_KEY to missing, RetroArchEmulator.EMULATOR_KEY to missing)
        ))
    }

    @Test
    fun `no overrides, no warnings`() {
        assertTrue(EmulatorRegistry.saveDirOverrideWarningMap(emptyMap()).isEmpty())
        assertTrue(EmulatorRegistry.saveDirOverrideWarningMap(mapOf(AzaharEmulator.EMULATOR_KEY to "")).isEmpty())
    }
}
