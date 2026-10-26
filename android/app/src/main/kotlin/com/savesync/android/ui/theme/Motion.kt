package com.savesync.android.ui.theme

import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.core.FastOutLinearInEasing
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.core.SpringSpec
import androidx.compose.animation.core.TweenSpec
import androidx.compose.animation.core.VisibilityThreshold
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.ui.Alignment
import androidx.compose.ui.unit.IntSize
import kotlin.math.PI
import kotlin.math.pow

/**
 * The app's only motion vocabulary.  Screens never write a `spring(...)` or
 * `tween(...)` literal; they pick one of these.
 *
 * Apple describes a spring by damping ratio and *response* r (seconds);
 * Compose takes stiffness, which for unit mass is (2π / r)².  Everything is
 * critically damped (damping ratio 1): nothing in this app is thrown with
 * momentum, so nothing overshoots.
 */
object GsMotion {
    fun stiffnessFor(responseSeconds: Float): Float =
        (2f * PI.toFloat() / responseSeconds).pow(2)

    /** r ≈ 0.30 s: navigation, expand/collapse, indicator. */
    const val STANDARD = 440f
    /** r ≈ 0.40 s: progress bars, large surfaces. */
    const val GENTLE = 250f
    /** r ≈ 0.26 s: list follow-scroll, chips, badges. */
    const val QUICK = 600f
    /** r ≈ 0.15 s: press scale, edge bump out. */
    const val PRESS = 1750f

    /** Response (seconds) each stiffness constant was derived from. */
    const val STANDARD_RESPONSE = 0.30f
    const val GENTLE_RESPONSE = 0.40f
    const val QUICK_RESPONSE = 0.26f
    const val PRESS_RESPONSE = 0.15f

    fun <T> standard(visibilityThreshold: T? = null): SpringSpec<T> =
        spring(1f, STANDARD, visibilityThreshold)

    fun <T> gentle(visibilityThreshold: T? = null): SpringSpec<T> =
        spring(1f, GENTLE, visibilityThreshold)

    fun <T> quick(visibilityThreshold: T? = null): SpringSpec<T> =
        spring(1f, QUICK, visibilityThreshold)

    fun <T> press(visibilityThreshold: T? = null): SpringSpec<T> =
        spring(1f, PRESS, visibilityThreshold)

    // The fade tokens are the only tweens in the app.  fadeIn's 60 ms delay
    // is a deliberate cross-fade overlap with the outgoing fadeOut.
    const val FADE_IN_MS = 180
    const val FADE_IN_DELAY_MS = 60
    const val FADE_OUT_MS = 90
    const val FADE_MS = 120

    fun <T> fadeIn(): TweenSpec<T> =
        tween(durationMillis = FADE_IN_MS, delayMillis = FADE_IN_DELAY_MS, easing = LinearOutSlowInEasing)

    fun <T> fadeOut(): TweenSpec<T> =
        tween(durationMillis = FADE_OUT_MS, easing = FastOutLinearInEasing)

    /** Reduced-motion replacement for every movement. */
    fun <T> fade(): TweenSpec<T> = tween(durationMillis = FADE_MS)
}

// ── Transition helpers (reduce = LocalReduceMotion) ─────────────────────────

fun gsExpandIn(reduce: Boolean): EnterTransition =
    if (reduce) fadeIn(GsMotion.fade())
    else expandVertically(GsMotion.standard(IntSize.VisibilityThreshold), expandFrom = Alignment.Top) +
        fadeIn(GsMotion.standard())

fun gsShrinkOut(reduce: Boolean): ExitTransition =
    if (reduce) fadeOut(GsMotion.fade())
    else shrinkVertically(GsMotion.standard(IntSize.VisibilityThreshold), shrinkTowards = Alignment.Top) +
        fadeOut(GsMotion.standard())

/** Badges, HUDs. */
fun gsPopIn(reduce: Boolean): EnterTransition =
    if (reduce) fadeIn(GsMotion.fade())
    else scaleIn(GsMotion.quick(), initialScale = 0.8f) + fadeIn(GsMotion.quick())

fun gsPopOut(reduce: Boolean): ExitTransition =
    if (reduce) fadeOut(GsMotion.fade())
    else scaleOut(GsMotion.quick(), targetScale = 0.8f) + fadeOut(GsMotion.quick())
