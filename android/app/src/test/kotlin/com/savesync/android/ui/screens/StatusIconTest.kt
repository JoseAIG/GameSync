package com.savesync.android.ui.screens

import com.savesync.android.ui.SaveSyncStatus
import org.junit.Assert.assertEquals
import org.junit.Test

/** M5-6: every status has its own icon (the label stays beside it). */
class StatusIconTest {
    @Test
    fun `each status maps to a distinct material icon`() {
        val names = SaveSyncStatus.values().map { statusIconVector(it).name }
        assertEquals(names.size, names.toSet().size)
        assertEquals("Filled.CheckCircle", statusIconVector(SaveSyncStatus.SYNCED).name)
        assertEquals("Filled.Smartphone", statusIconVector(SaveSyncStatus.LOCAL_ONLY).name)
        assertEquals("Filled.Cloud", statusIconVector(SaveSyncStatus.SERVER_ONLY).name)
        assertEquals("Filled.Warning", statusIconVector(SaveSyncStatus.CONFLICT).name)
    }
}
