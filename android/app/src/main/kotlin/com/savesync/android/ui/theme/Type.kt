package com.savesync.android.ui.theme

import androidx.compose.material3.Typography
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

/**
 * System font throughout.  Tracking is negative on large text, about zero on
 * body and slightly positive on small labels; leading tightens as size grows.
 * Every size is `sp`, so it follows the system font scale.
 */
private fun style(size: Int, line: Int, weight: FontWeight, tracking: Float) = TextStyle(
    fontFamily = FontFamily.Default,
    fontSize = size.sp,
    lineHeight = line.sp,
    fontWeight = weight,
    letterSpacing = tracking.sp,
)

val GsTypography = Typography(
    headlineSmall = style(22, 28, FontWeight.SemiBold, -0.2f),
    titleLarge = style(20, 26, FontWeight.Bold, -0.1f),
    titleMedium = style(16, 22, FontWeight.SemiBold, 0f),
    titleSmall = style(14, 20, FontWeight.SemiBold, 0.1f),
    bodyLarge = style(16, 24, FontWeight.Normal, 0f),
    bodyMedium = style(14, 20, FontWeight.Normal, 0f),
    bodySmall = style(12, 16, FontWeight.Normal, 0.2f),
    labelLarge = style(14, 20, FontWeight.Medium, 0.1f),
    labelMedium = style(12, 16, FontWeight.Medium, 0.3f),
    labelSmall = style(11, 16, FontWeight.Bold, 0.4f),
)
