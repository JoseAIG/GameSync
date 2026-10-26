package com.savesync.android.ui

import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.core.VisibilityThreshold
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.ui.unit.IntOffset
import com.savesync.android.ui.theme.GsMotion
import kotlin.math.abs

/**
 * How a NavHost destination change moves.  Decided from the route pair only,
 * never from which of NavHost's four transition lambdas was invoked: tab
 * switches use popUpTo + restoreState, which Navigation may report as a pop,
 * and tab → tab must look the same either way.
 */
enum class NavMotion {
    /** Cross-fade: an L1/R1 wrap, pushed screen → an unrelated tab, reduced motion. */
    FADE,
    /** Tab → a tab to its right (R1): content moves left. */
    TAB_FORWARD,
    /** Tab → a tab to its left (L1): content moves right. */
    TAB_BACK,
    /** Tab → its pushed screen (save detail, emulator config): slides in from the right. */
    PUSH,
    /** Pushed screen → its parent tab: the exact mirror of [PUSH]. */
    POP,
}

/** Top-level tab routes, in tab-strip order (MainActivity.TAB_ROUTES). */
internal val NAV_TAB_ROUTES: List<String> = listOf("saves", "catalog", "installed", "downloads", "settings")

/** The tab a pushed route belongs to; null when [route] is not a pushed route. */
internal fun pushedParentTab(route: String?): String? = when {
    route == null -> null
    route.startsWith("detail") -> "saves"
    route == "emulators" -> "settings"
    else -> null
}

fun navMotionFor(fromRoute: String?, toRoute: String?, reduce: Boolean): NavMotion {
    if (reduce) return NavMotion.FADE
    val fromTab = fromRoute in NAV_TAB_ROUTES
    val toTab = toRoute in NAV_TAB_ROUTES
    return when {
        fromTab && pushedParentTab(toRoute) != null -> NavMotion.PUSH
        toTab && pushedParentTab(fromRoute) == toRoute -> NavMotion.POP
        fromTab && toTab -> tabMotion(NAV_TAB_ROUTES.indexOf(fromRoute), NAV_TAB_ROUTES.indexOf(toRoute))
        else -> NavMotion.FADE
    }
}

/**
 * Tab → tab moves content only (the chrome is persistent), in the direction
 * of travel.  A distance of the whole strip fades instead: it is an L1/R1
 * wrap (Settings → Saves on R1, where a slide would point the wrong way) or
 * a tap from the first tab to the last, and the route pair can't tell the
 * two apart.
 */
private fun tabMotion(fromIndex: Int, toIndex: Int): NavMotion {
    val d = toIndex - fromIndex
    return when {
        d == 0 -> NavMotion.FADE
        abs(d) == NAV_TAB_ROUTES.size - 1 -> NavMotion.FADE
        d > 0 -> NavMotion.TAB_FORWARD
        else -> NavMotion.TAB_BACK
    }
}

/** The arriving screen's transition for this motion. */
fun NavMotion.enter(reduce: Boolean): EnterTransition {
    if (reduce) return fadeIn(GsMotion.fade())
    val slide = GsMotion.standard(IntOffset.VisibilityThreshold)
    return when (this) {
        NavMotion.FADE -> fadeIn(GsMotion.fadeIn())
        NavMotion.TAB_FORWARD -> slideInHorizontally(slide) { it / 10 } + fadeIn(GsMotion.fadeIn())
        NavMotion.TAB_BACK -> slideInHorizontally(slide) { -it / 10 } + fadeIn(GsMotion.fadeIn())
        NavMotion.PUSH -> slideInHorizontally(slide) { it / 4 } + fadeIn(GsMotion.fadeIn())
        NavMotion.POP -> slideInHorizontally(slide) { -it / 10 } + fadeIn(GsMotion.fadeIn())
    }
}

/** The leaving screen's transition for this motion. */
fun NavMotion.exit(reduce: Boolean): ExitTransition {
    if (reduce) return fadeOut(GsMotion.fade())
    val slide = GsMotion.standard(IntOffset.VisibilityThreshold)
    return when (this) {
        NavMotion.FADE -> fadeOut(GsMotion.fadeOut())
        NavMotion.TAB_FORWARD -> slideOutHorizontally(slide) { -it / 10 } + fadeOut(GsMotion.fadeOut())
        NavMotion.TAB_BACK -> slideOutHorizontally(slide) { it / 10 } + fadeOut(GsMotion.fadeOut())
        NavMotion.PUSH -> slideOutHorizontally(slide) { -it / 10 } + fadeOut(GsMotion.fadeOut())
        NavMotion.POP -> slideOutHorizontally(slide) { it / 4 } + fadeOut(GsMotion.fadeOut())
    }
}
