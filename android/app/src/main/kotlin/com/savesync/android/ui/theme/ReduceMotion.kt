package com.savesync.android.ui.theme

import android.content.ContentResolver
import android.database.ContentObserver
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext

/**
 * True while the system asks for reduced motion.  Under it every slide,
 * scale, translation and animated scroll becomes [GsMotion.fade] or an
 * instant jump; opacity and colour changes that carry meaning stay.
 * Provided once by MainApp.
 */
val LocalReduceMotion = compositionLocalOf { false }

/**
 * Android's accessibility "Remove animations" sets both scales to 0; a user
 * who zeroes either one in developer options also gets no movement.
 */
internal fun isReduceMotion(animatorScale: Float, transitionScale: Float): Boolean =
    animatorScale == 0f || transitionScale == 0f

private fun readReduceMotion(resolver: ContentResolver): Boolean = isReduceMotion(
    Settings.Global.getFloat(resolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f),
    Settings.Global.getFloat(resolver, Settings.Global.TRANSITION_ANIMATION_SCALE, 1f),
)

/** Reads the reduced-motion setting and keeps it current while composed. */
@Composable
fun rememberReduceMotion(): Boolean {
    val context = LocalContext.current
    val resolver = context.contentResolver
    var reduce by remember { mutableStateOf(runCatching { readReduceMotion(resolver) }.getOrDefault(false)) }
    DisposableEffect(resolver) {
        val observer = object : ContentObserver(Handler(Looper.getMainLooper())) {
            override fun onChange(selfChange: Boolean) {
                reduce = runCatching { readReduceMotion(resolver) }.getOrDefault(false)
            }
        }
        listOf(
            Settings.Global.ANIMATOR_DURATION_SCALE,
            Settings.Global.TRANSITION_ANIMATION_SCALE,
        ).forEach { key ->
            runCatching {
                resolver.registerContentObserver(Settings.Global.getUriFor(key), false, observer)
            }
        }
        reduce = runCatching { readReduceMotion(resolver) }.getOrDefault(false)
        onDispose { resolver.unregisterContentObserver(observer) }
    }
    return reduce
}
