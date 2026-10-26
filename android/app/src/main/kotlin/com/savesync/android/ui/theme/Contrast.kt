package com.savesync.android.ui.theme

import android.app.UiModeManager
import android.content.Context
import android.os.Build
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.core.content.ContextCompat

/**
 * Android's version of `prefers-contrast: more`.  When true, secondary text
 * steps up one level (TextMuted → Text, Dim → TextMuted), pills, banners and
 * selected rows get a 1dp border in their text colour, and translucent fills
 * become opaque.  Provided once by MainApp.
 */
val LocalHighContrast = compositionLocalOf { false }

/** The system "Contrast" setting at medium or above. */
internal fun isHighContrast(contrast: Float): Boolean = contrast >= 0.5f

/**
 * API 34+: follows `UiModeManager.getContrast()` live.  Below 34 there is no
 * public API for "high contrast text", so this is always false there.
 */
@Composable
fun rememberHighContrast(): Boolean {
    val context = LocalContext.current
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.UPSIDE_DOWN_CAKE) return false
    val uiMode = remember(context) {
        context.getSystemService(Context.UI_MODE_SERVICE) as? UiModeManager
    } ?: return false
    var high by remember { mutableStateOf(isHighContrast(uiMode.contrast)) }
    DisposableEffect(uiMode) {
        val listener = UiModeManager.ContrastChangeListener { c -> high = isHighContrast(c) }
        uiMode.addContrastChangeListener(ContextCompat.getMainExecutor(context), listener)
        high = isHighContrast(uiMode.contrast)
        onDispose { uiMode.removeContrastChangeListener(listener) }
    }
    return high
}

/** Secondary text colour (captions, timestamps); one step up under high contrast. */
@Composable
fun gsTextMuted(): Color = if (LocalHighContrast.current) GsColors.Text else GsColors.TextMuted

/** Dim text colour (only on Panel/Bg, never on PanelHi); one step up under high contrast. */
@Composable
fun gsDim(): Color = if (LocalHighContrast.current) GsColors.TextMuted else GsColors.Dim
