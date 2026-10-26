package com.savesync.android.ui.components

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.draw.drawBehind
import androidx.compose.runtime.getValue
import androidx.compose.runtime.derivedStateOf
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.Crossfade
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.savesync.android.ui.NAV_TAB_ROUTES
import com.savesync.android.ui.theme.GsColors
import com.savesync.android.ui.theme.GsMotion

// ── Persistent chrome ───────────────────────────────────────────────────────
//
// One header and one footer live in MainApp for the whole session, so during
// a screen transition there is exactly one of each and the tab highlight can
// travel.  Screens describe what they want in it with GsScreenChrome.

private val NoActions: @Composable RowScope.() -> Unit = {}

/** What a screen wants in the app's header and footer. */
data class GsChromeSpec(
    /** null = the tab header; else the sub-screen header with Back. */
    val title: String? = null,
    val onBack: (() -> Unit)? = null,
    val actions: @Composable RowScope.() -> Unit = NoActions,
    val hints: List<GsHint> = emptyList(),
    /**
     * Content is scrolled under the header / above the footer: MainApp draws
     * the scroll-edge gradient there (M5-4).  Compute each with
     * `derivedStateOf { state.canScrollBackward }` / `canScrollForward`, so
     * the spec changes only when the boolean flips, not on every frame.
     */
    val scrolledUnderTop: Boolean = false,
    val scrolledUnderBottom: Boolean = false,
)

/** Specs by chrome key (see [chromeKeyFor]); read by MainApp's header and footer. */
@Stable
class GsChromeState {
    val specs = mutableStateMapOf<String, GsChromeSpec>()
}

val LocalGsChrome = staticCompositionLocalOf<GsChromeState> { error("no chrome") }

/** Whether [route] (a route pattern) is a top-level tab. */
fun isTabRoute(route: String?): Boolean = route == null || route in NAV_TAB_ROUTES

/**
 * The chrome key for a destination: the route for a tab, the route with its
 * arguments filled in for a pushed screen ("detail/<titleId>"), so opening
 * a second game never shows the previous game's title for a frame.
 */
fun chromeKeyFor(routePattern: String?, titleId: String?): String = when {
    routePattern == null -> NAV_TAB_ROUTES[0]
    routePattern.startsWith("detail") -> "detail/" + (titleId ?: "")
    else -> routePattern
}

/**
 * Publish [spec] for [key].  Writes only when the spec changed; build it with
 * `remember(<state it depends on>)` so its lambdas are stable and an
 * unrelated recomposition (a progress tick, a D-pad step) doesn't touch the
 * header.  Pushed keys are dropped when the screen leaves; tab keys stay, so
 * returning to a tab shows its last spec on the first frame.
 */
@Composable
fun GsScreenChrome(key: String, spec: GsChromeSpec) {
    val chrome = LocalGsChrome.current
    SideEffect { if (chrome.specs[key] != spec) chrome.specs[key] = spec }
    if (key !in NAV_TAB_ROUTES) {
        DisposableEffect(chrome, key) { onDispose { chrome.specs.remove(key) } }
    }
}

/** Last key shown by each kind of header, so a leaving header keeps its own. */
private class HeaderKeys {
    var tab: String = NAV_TAB_ROUTES[0]
    var pushed: String = ""
}

/**
 * MainApp's header: the tab header on tab routes, the sub-screen header on
 * pushed ones, swapped with a fade.  The tab header cross-fades its actions
 * between tabs.
 */
@Composable
fun GsChromeHeader(
    routePattern: String?,
    key: String,
    onTabClick: (Int) -> Unit,
    onBack: () -> Unit,
    badges: Map<Int, GsTabBadge> = emptyMap(),
    onServerDotClick: (() -> Unit)? = null,
) {
    val chrome = LocalGsChrome.current
    // One strip scroll position for the session, whichever header shows.
    val stripScroll = rememberScrollState()
    val isTab = isTabRoute(routePattern)
    val keys = remember { HeaderKeys() }
    if (isTab) keys.tab = key else keys.pushed = key
    val tabKey = keys.tab
    val pushedKey = keys.pushed
    AnimatedContent(
        targetState = isTab,
        transitionSpec = { fadeIn(GsMotion.fade()) togetherWith fadeOut(GsMotion.fade()) },
        label = "chromeHeader",
    ) { tab ->
        if (tab) {
            GsTopBar(
                activeTabIndex = NAV_TAB_ROUTES.indexOf(tabKey).coerceAtLeast(0),
                onTabClick = onTabClick,
                badges = badges,
                stripScrollState = stripScroll,
                onServerDotClick = onServerDotClick,
                actions = {
                    Crossfade(targetState = tabKey, animationSpec = GsMotion.fade(), label = "chromeActions") { k ->
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            chrome.specs[k]?.actions?.invoke(this)
                        }
                    }
                },
            )
        } else {
            val spec = chrome.specs[pushedKey]
            GsSubScreenBar(
                title = spec?.title ?: "",
                onBack = spec?.onBack ?: onBack,
                actions = spec?.actions ?: NoActions,
                onServerDotClick = onServerDotClick,
            )
        }
    }
}

/** MainApp's footer: the current screen's hints, swapped instantly. */
@Composable
fun GsChromeFooter(key: String) {
    GsFooterHints(LocalGsChrome.current.specs[key]?.hints ?: emptyList())
}

// ── Scroll edges (M5-4) ─────────────────────────────────────────────────────
//
// Instead of a 1dp rule under the header and over the footer, a 6dp
// gradient fades in where content actually scrolls under a fixed surface,
// and only while it does (§12).  The bars themselves stay opaque.

/** Height of a scroll-edge gradient. */
val GS_SCROLL_EDGE = 6.dp

/**
 * MainApp's edges over the content area, from the current screen's spec
 * (for screens that scroll right under the header and over the footer).
 * Reads the spec itself, so only this recomposes when a flag flips.
 */
@Composable
fun BoxScope.GsChromeScrollEdges(key: String) {
    val spec = LocalGsChrome.current.specs[key]
    GsScrollEdges(top = spec?.scrolledUnderTop == true, bottom = spec?.scrolledUnderBottom == true)
}

/**
 * A list's own edges, for a list that sits under chips or above an
 * in-screen footer rather than right against the app's bars.
 */
@Composable
fun BoxScope.GsListScrollEdges(listState: LazyListState, color: Color = GsColors.Bg) {
    val top by remember(listState) { derivedStateOf { listState.canScrollBackward } }
    val bottom by remember(listState) { derivedStateOf { listState.canScrollForward } }
    GsScrollEdges(top = top, bottom = bottom, color = color)
}

/** Top and bottom gradients in [color] @ 0.9 → transparent; alpha follows STANDARD. */
@Composable
fun BoxScope.GsScrollEdges(top: Boolean, bottom: Boolean, color: Color = GsColors.Bg2) {
    ScrollEdge(visible = top, atTop = true, color = color, modifier = Modifier.align(Alignment.TopCenter))
    ScrollEdge(visible = bottom, atTop = false, color = color, modifier = Modifier.align(Alignment.BottomCenter))
}

@Composable
private fun ScrollEdge(visible: Boolean, atTop: Boolean, color: Color, modifier: Modifier) {
    val alpha = animateFloatAsState(
        targetValue = if (visible) 1f else 0f,
        animationSpec = GsMotion.standard(),
        label = "scrollEdge",
    )
    Box(
        modifier
            .fillMaxWidth()
            .height(GS_SCROLL_EDGE)
            // Read in the draw phase: the fade never recomposes anything.
            .drawBehind {
                val a = alpha.value
                if (a <= 0.001f) return@drawBehind
                val solid = color.copy(alpha = 0.9f * a)
                val stops = if (atTop) listOf(solid, Color.Transparent) else listOf(Color.Transparent, solid)
                drawRect(Brush.verticalGradient(stops))
            }
    )
}

/**
 * A screen's sub-tab chips (system filter, status filter …), drawn at the
 * top of the screen's own content so the chips move with their list.
 */
@Composable
fun GsSubTabRow(modifier: Modifier = Modifier, content: @Composable RowScope.() -> Unit) {
    Column(modifier = modifier.fillMaxWidth().background(GsColors.Bg)) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .horizontalScroll(rememberScrollState())
                // No vertical padding: each chip carries its own 48dp
                // touch target.
                .padding(horizontal = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            content = content,
        )
        // No rule under the chips: the list below draws a scroll edge
        // there while it is scrolled (GsListScrollEdges).
    }
}
