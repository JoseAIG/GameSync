package com.savesync.android.ui.screens

import com.savesync.android.storage.DownloadEntity
import org.junit.Assert.assertEquals
import org.junit.Test

class DownloadsCopyTest {

    private fun download(status: String, final: String = "/sdcard/ROMs/psx/Game.chd") = DownloadEntity(
        id = "1", romId = "r", system = "PS1", displayName = "Game", filename = "Game.chd",
        partFilePath = "$final.part", finalFilePath = final,
        totalBytes = 0, downloadedBytes = 0, status = status, errorMessage = null,
        extractFormat = null, createdAt = 0, updatedAt = 0,
    )

    private val gb = 1024L * 1024 * 1024

    @Test
    fun `subtitles carry numbers, not the state the pill shows`() {
        assertEquals(
            "1.00 GB of 4.00 GB · 25%",
            buildSubtitle(download(DownloadEntity.Status.PAUSED), gb, 4 * gb, null),
        )
        assertEquals(
            "1.00 GB of 4.00 GB · 25% · 1.00 MB/s · ETA 51m 12s",
            buildSubtitle(download(DownloadEntity.Status.DOWNLOADING), gb, 4 * gb, 1024L * 1024),
        )
        assertEquals("Waiting to start", buildSubtitle(download(DownloadEntity.Status.QUEUED), 0, 0, null))
    }

    @Test
    fun `a finished download says where it went`() {
        assertEquals(
            "Saved to ROMs/psx · 1.20 GB",
            buildSubtitle(download(DownloadEntity.Status.COMPLETED), (1.2 * gb).toLong(), 0, null),
        )
        assertEquals("ROMs/psx", savedToLabel("/sdcard/ROMs/psx/Game.chd"))
    }

    @Test
    fun `server host for messages`() {
        assertEquals("192.168.1.100", serverHostOf("http://192.168.1.100:8000"))
        assertEquals("the server", serverHostOf(""))
    }
}
