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

/**
 * How a NavHost destination change moves.  Decided from the route pair only,
 * never from which of NavHost's four transition lambdas was invoked: tab
 * switches use popUpTo + restoreState, which Navigation may report as a pop,
 * and tab → tab must look the same either way.
 */
enum class NavMotion {
    /** Cross-fade: tab → tab, pushed screen → an unrelated tab, reduced motion. */
    FADE,
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
        else -> NavMotion.FADE
    }
}

/** The arriving screen's transition for this motion. */
fun NavMotion.enter(reduce: Boolean): EnterTransition {
    if (reduce) return fadeIn(GsMotion.fade())
    val slide = GsMotion.standard(IntOffset.VisibilityThreshold)
    return when (this) {
        NavMotion.FADE -> fadeIn(GsMotion.fadeIn())
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
        NavMotion.PUSH -> slideOutHorizontally(slide) { -it / 10 } + fadeOut(GsMotion.fadeOut())
        NavMotion.POP -> slideOutHorizontally(slide) { it / 4 } + fadeOut(GsMotion.fadeOut())
    }
}
