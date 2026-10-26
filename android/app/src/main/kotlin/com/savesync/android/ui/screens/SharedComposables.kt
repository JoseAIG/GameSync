package com.savesync.android.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp

/** Centered spinner with an optional label. Shared by the ROM Catalog
 *  and Installed Games tabs. */
@Composable
fun CenterLoader(text: String? = null) {
    Column(
        modifier = Modifier.fillMaxSize(),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        CircularProgressIndicator()
        if (!text.isNullOrBlank()) {
            Spacer(Modifier.height(12.dp))
            Text(
                text,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

/** Centered two-line message: bold title, optional detail paragraph. */
@Composable
fun CenterMessage(title: String, detail: String = "") {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            title,
            style = MaterialTheme.typography.titleMedium,
            textAlign = TextAlign.Center,
        )
        if (detail.isNotBlank()) {
            Spacer(Modifier.height(8.dp))
            Text(
                detail,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
            )
        }
    }
}

/**
 * A time as relative-plus-absolute text: "Today 21:14 · 26 Oct 2026",
 * "Yesterday 08:02 · 25 Oct 2026", else "19 Oct 2026 · 21:14".  "Never"
 * for 0 or less.  [now] and [zone] are parameters for tests.
 */
fun formatWhen(
    ms: Long,
    now: Long = System.currentTimeMillis(),
    zone: java.time.ZoneId = java.time.ZoneId.systemDefault(),
    locale: java.util.Locale = java.util.Locale.getDefault(),
): String {
    if (ms <= 0L) return "Never"
    val at = java.time.Instant.ofEpochMilli(ms).atZone(zone)
    val today = java.time.Instant.ofEpochMilli(now).atZone(zone).toLocalDate()
    val time = java.time.format.DateTimeFormatter.ofPattern("HH:mm", locale).format(at)
    val date = java.time.format.DateTimeFormatter.ofPattern("d MMM yyyy", locale).format(at)
    return when (at.toLocalDate()) {
        today -> "Today $time · $date"
        today.minusDays(1) -> "Yesterday $time · $date"
        else -> "$date · $time"
    }
}

/** Human-readable byte count.  "1.50 GB", "720 KB", "12 B". */
fun formatBytes(bytes: Long): String {
    if (bytes <= 0) return ""
    val units = listOf("GB" to 1024L * 1024 * 1024, "MB" to 1024L * 1024, "KB" to 1024L)
    for ((unit, factor) in units) {
        if (bytes >= factor) return "%.2f %s".format(bytes.toDouble() / factor, unit)
    }
    return "$bytes B"
}
