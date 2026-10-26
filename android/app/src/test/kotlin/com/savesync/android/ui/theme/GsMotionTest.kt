package com.savesync.android.ui.theme

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs

class GsMotionTest {

    private fun assertWithin(expected: Float, actual: Float, fraction: Float) {
        assertTrue(
            "expected $actual within ${fraction * 100}% of $expected",
            abs(actual - expected) <= expected * fraction,
        )
    }

    @Test
    fun `stiffness follows Apple's response parameter`() {
        assertWithin(438.6f, GsMotion.stiffnessFor(0.3f), 0.01f)
        assertWithin(1754.6f, GsMotion.stiffnessFor(0.15f), 0.01f)
    }

    @Test
    fun `each constant matches the response written next to it`() {
        assertWithin(GsMotion.stiffnessFor(GsMotion.STANDARD_RESPONSE), GsMotion.STANDARD, 0.03f)
        assertWithin(GsMotion.stiffnessFor(GsMotion.GENTLE_RESPONSE), GsMotion.GENTLE, 0.03f)
        assertWithin(GsMotion.stiffnessFor(GsMotion.QUICK_RESPONSE), GsMotion.QUICK, 0.03f)
        assertWithin(GsMotion.stiffnessFor(GsMotion.PRESS_RESPONSE), GsMotion.PRESS, 0.03f)
    }

    @Test
    fun `every spring is critically damped and uses its token`() {
        val specs = listOf(
            GsMotion.standard<Float>() to GsMotion.STANDARD,
            GsMotion.gentle<Float>() to GsMotion.GENTLE,
            GsMotion.quick<Float>() to GsMotion.QUICK,
            GsMotion.press<Float>() to GsMotion.PRESS,
        )
        for ((spec, stiffness) in specs) {
            assertEquals(1f, spec.dampingRatio)
            assertEquals(stiffness, spec.stiffness)
        }
    }

    @Test
    fun `fade tokens stay short`() {
        assertEquals(180, GsMotion.fadeIn<Float>().durationMillis)
        assertEquals(60, GsMotion.fadeIn<Float>().delay)
        assertEquals(90, GsMotion.fadeOut<Float>().durationMillis)
        assertEquals(120, GsMotion.fade<Float>().durationMillis)
    }

    @Test
    fun `reduced motion when either scale is zero`() {
        assertTrue(isReduceMotion(0f, 0f))
        assertTrue(isReduceMotion(0f, 1f))
        assertTrue(isReduceMotion(1f, 0f))
        assertFalse(isReduceMotion(1f, 1f))
        assertFalse(isReduceMotion(0.5f, 1f))
    }

    @Test
    fun `high contrast from medium contrast up`() {
        assertFalse(isHighContrast(0f))
        assertFalse(isHighContrast(0.49f))
        assertTrue(isHighContrast(0.5f))
        assertTrue(isHighContrast(1f))
    }

    @Test
    fun `status colours map to their text variants`() {
        assertEquals(GsColors.OkText, textVariantOf(GsColors.Ok))
        assertEquals(GsColors.InfoText, textVariantOf(GsColors.Info))
        assertEquals(GsColors.WarnText, textVariantOf(GsColors.Warn))
        assertEquals(GsColors.ErrText, textVariantOf(GsColors.Err))
        assertEquals(GsColors.Accent, textVariantOf(GsColors.Accent))
    }
}
