package com.savesync.android.ui.screens

import org.junit.Assert.assertEquals
import org.junit.Test
import java.time.LocalDateTime
import java.time.ZoneId
import java.util.Locale

class FormatWhenTest {
    private val zone = ZoneId.of("UTC")
    private fun ms(y: Int, mo: Int, d: Int, h: Int, mi: Int) =
        LocalDateTime.of(y, mo, d, h, mi).atZone(zone).toInstant().toEpochMilli()

    private val now = ms(2026, 10, 26, 22, 0)

    @Test
    fun `today and yesterday are relative plus absolute`() {
        assertEquals("Today 21:14 · 26 Oct 2026", formatWhen(ms(2026, 10, 26, 21, 14), now, zone, Locale.UK))
        assertEquals("Yesterday 08:02 · 25 Oct 2026", formatWhen(ms(2026, 10, 25, 8, 2), now, zone, Locale.UK))
    }

    @Test
    fun `older dates are absolute, none is Never`() {
        assertEquals("19 Oct 2026 · 21:14", formatWhen(ms(2026, 10, 19, 21, 14), now, zone, Locale.UK))
        assertEquals("Never", formatWhen(0L, now, zone, Locale.UK))
    }
}
