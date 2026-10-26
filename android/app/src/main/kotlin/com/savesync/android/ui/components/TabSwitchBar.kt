package com.savesync.android.ui.components

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.VisibilityThreshold
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.background
import androidx.compose.foundation.indication
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.toSize
import com.savesync.android.ui.theme.GsMotion
import com.savesync.android.ui.theme.LocalReduceMotion
import com.savesync.android.ui.theme.gsPopIn
import com.savesync.android.ui.theme.gsPopOut
import kotlin.math.abs
import kotlin.math.roundToInt
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import com.savesync.android.ui.theme.GsColors

/** Stable ordering for the top-level tabs, mirrored by MainActivity.TAB_ROUTES. */
val TAB_LABELS: List<String> = listOf("Saves", "Catalog", "Installed", "Downloads", "Settings")

/**
 * Whether the server answered its last status check: true = reachable,
 * false = not, null = not checked yet / no server configured.  Provided
 * once by MainApp so every header's status dot agrees.
 */
val LocalServerOnline = compositionLocalOf<Boolean?> { null }

/**
 * The header bar every GameSync client draws: the app name, the top-level
 * tabs between L1 / R1 glyphs (active tab = teal pill with ink text), the
 * screen's touch actions and the server status dot.  Drawn once, by MainApp
 * (see GsChrome.kt); a screen's sub-tab chips live in its own content
 * ([GsSubTabRow]).
 *
 * Wide screens (landscape / tablets) keep the name, tabs and actions on one
 * line; narrow phones put the tab strip on a second, horizontally
 * scrollable line so nothing is squeezed.
 */
@Composable
fun GsTopBar(
    activeTabIndex: Int,
    onTabClick: (Int) -> Unit,
    modifier: Modifier = Modifier,
    actions: @Composable RowScope.() -> Unit = {},
    badges: Map<Int, GsTabBadge> = emptyMap(),
    /** The strip's scroll position; MainApp's header keeps one for the session. */
    stripScrollState: ScrollState = rememberScrollState(),
) {
    val online = LocalServerOnline.current
    Column(
        modifier = modifier
            .fillMaxWidth()
            .background(GsColors.Bg2)
    ) {
        BoxWithConstraints(modifier = Modifier.fillMaxWidth()) {
            val wide = maxWidth >= 720.dp
            if (wide) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(min = 52.dp)
                        .padding(start = 16.dp, end = 8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    AppName()
                    Spacer(Modifier.width(16.dp))
                    TabSwitchBar(
                        activeTabIndex = activeTabIndex,
                        onTabClick = onTabClick,
                        scrollState = stripScrollState,
                        badges = badges,
                    )
                    Spacer(Modifier.weight(1f))
                    actions()
                    ServerDot(online)
                }
            } else {
                Column(modifier = Modifier.fillMaxWidth()) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .heightIn(min = 48.dp)
                            .padding(start = 16.dp, end = 8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        AppName()
                        Spacer(Modifier.weight(1f))
                        actions()
                        ServerDot(online)
                    }
                    // No bottom padding: each pill's 48dp hit area already
                    // leaves room under the visual pill.
                    TabSwitchBar(
                        activeTabIndex = activeTabIndex,
                        onTabClick = onTabClick,
                        scrollState = stripScrollState,
                        badges = badges,
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(start = 12.dp, end = 12.dp),
                    )
                }
            }
        }
        Box(
            Modifier
                .fillMaxWidth()
                .height(1.dp)
                .background(GsColors.Line)
        )
    }
}

/**
 * Header bar for a pushed (non-tab) screen: back affordance, title, and the
 * same server dot.  B / the system Back gesture leave the screen.
 */
@Composable
fun GsSubScreenBar(
    title: String,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
    actions: @Composable RowScope.() -> Unit = {},
    badges: Map<Int, GsTabBadge> = emptyMap(),
    /** The strip's scroll position; MainApp's header keeps one for the session. */
    stripScrollState: ScrollState = rememberScrollState(),
) {
    val online = LocalServerOnline.current
    Column(modifier = modifier.fillMaxWidth().background(GsColors.Bg2)) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = 52.dp)
                .padding(start = 8.dp, end = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                modifier = Modifier
                    // 48dp hit area; the chip itself has no fill, so it
                    // looks the same.
                    .heightIn(min = 48.dp)
                    .clip(RoundedCornerShape(16.dp))
                    .clickable(onClick = onBack)
                    .padding(horizontal = 8.dp, vertical = 6.dp),
                contentAlignment = Alignment.Center,
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    if (LocalShowButtonHints.current) {
                        ButtonGlyph(GsButton.B)
                        Spacer(Modifier.width(6.dp))
                    } else {
                        Text("←", color = GsColors.Accent, style = MaterialTheme.typography.titleMedium)
                        Spacer(Modifier.width(4.dp))
                    }
                    Text("Back", color = GsColors.Dim, style = MaterialTheme.typography.labelLarge)
                }
            }
            Spacer(Modifier.width(8.dp))
            Text(
                title,
                color = GsColors.Text,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.weight(1f),
            )
            actions()
            ServerDot(online)
        }
        Box(
            Modifier
                .fillMaxWidth()
                .height(1.dp)
                .background(GsColors.Line)
        )
    }
}

@Composable
private fun AppName() {
    Text(
        text = "GameSync",
        color = GsColors.Accent,
        style = MaterialTheme.typography.titleLarge,
        fontWeight = FontWeight.Bold,
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
    )
}

@Composable
private fun ServerDot(online: Boolean?) {
    val color = when (online) {
        true -> GsColors.Ok
        false -> GsColors.Err
        null -> GsColors.Muted
    }
    Box(
        modifier = Modifier
            .padding(start = 8.dp, end = 8.dp)
            .size(10.dp)
            .clip(CircleShape)
            .background(color)
    )
}

/** A badge after a tab's label: Downloads' active count, Saves' conflict dot. */
data class GsTabBadge(
    val visible: Boolean,
    /** What TalkBack reads for the whole tab while the badge shows ("Downloads, 2 active"). */
    val contentDescription: String,
    val content: @Composable () -> Unit,
)

/** Margin kept between the active pill and the strip's edges when it scrolls into view. */
private val STRIP_EDGE_MARGIN = 24.dp

/**
 * The tab strip: L1 glyph, one label per top-level tab, R1 glyph (the
 * glyphs only while controller hints are shown — see [LocalShowButtonHints]).
 * A single teal pill sits behind the active label and travels to the next
 * one on the STANDARD spring, re-targeting with its velocity when R1 is
 * pressed twice quickly; label colours change with it.  A wrap (Settings ↔
 * Saves) and reduced motion fade the pill out, move it and fade it back,
 * since a slide across the whole strip would point the wrong way.  Labels
 * are tappable tabs with a 48dp hit area.
 */
@Composable
fun TabSwitchBar(
    activeTabIndex: Int,
    onTabClick: (Int) -> Unit,
    modifier: Modifier = Modifier,
    scrollState: ScrollState = rememberScrollState(),
    badges: Map<Int, GsTabBadge> = emptyMap(),
) {
    val reduce = LocalReduceMotion.current
    val density = LocalDensity.current
    // Each label's pill bounds in px, in the strip's (scrolled) content space.
    val bounds = remember { mutableStateMapOf<Int, Rect>() }
    var viewportPx by remember { mutableIntStateOf(0) }

    // The pill follows the active tab, except across a wrap / under reduced
    // motion, where it fades out, jumps and fades back in.
    var shownIndex by remember { mutableIntStateOf(activeTabIndex) }
    var snapGeneration by remember { mutableIntStateOf(0) }
    val pillAlpha = remember { Animatable(1f) }
    LaunchedEffect(activeTabIndex, reduce) {
        if (shownIndex != activeTabIndex) {
            if (pillJumps(shownIndex, activeTabIndex, TAB_LABELS.size, reduce)) {
                pillAlpha.animateTo(0f, GsMotion.fadeOut())
                shownIndex = activeTabIndex
                snapGeneration++
            } else {
                shownIndex = activeTabIndex
            }
        }
        pillAlpha.animateTo(1f, GsMotion.fadeIn())
    }

    val target = bounds[shownIndex]
    // Re-keyed on a jump and on the first measurement (also after a rotation
    // between the wide and narrow header, which is a new strip): a fresh
    // animateDpAsState starts at its target, i.e. snaps.
    val pillX = key(snapGeneration, target != null) {
        animateDpAsState(
            targetValue = with(density) { (target?.left ?: 0f).toDp() },
            animationSpec = GsMotion.standard(Dp.VisibilityThreshold),
            label = "tabPillX",
        )
    }
    val pillWidth = key(snapGeneration, target != null) {
        animateDpAsState(
            targetValue = with(density) { (target?.width ?: 0f).toDp() },
            animationSpec = GsMotion.standard(Dp.VisibilityThreshold),
            label = "tabPillWidth",
        )
    }

    // Keep the active pill fully visible with a margin, so Downloads and
    // Settings never start clipped on a 360dp phone.
    val activeBounds = bounds[activeTabIndex]
    LaunchedEffect(activeTabIndex, activeBounds, viewportPx) {
        val r = activeBounds ?: return@LaunchedEffect
        val to = stripScrollTarget(
            left = r.left,
            right = r.right,
            scroll = scrollState.value,
            viewport = viewportPx,
            margin = with(density) { STRIP_EDGE_MARGIN.toPx() },
            max = scrollState.maxValue,
        ) ?: return@LaunchedEffect
        if (reduce) scrollState.scrollTo(to) else scrollState.animateScrollTo(to, GsMotion.standard())
    }

    Row(
        modifier = modifier
            .onSizeChanged { viewportPx = it.width }
            .horizontalScroll(scrollState)
            // Drawn behind the labels, in the scrolled content's space; the
            // animated values are read here, in the draw phase only.
            .drawBehind {
                val r = bounds[shownIndex] ?: return@drawBehind
                val alpha = pillAlpha.value
                if (alpha <= 0f) return@drawBehind
                drawRoundRect(
                    color = GsColors.Accent,
                    topLeft = Offset(pillX.value.toPx(), r.top),
                    size = Size(pillWidth.value.toPx(), r.height),
                    cornerRadius = CornerRadius(16.dp.toPx()),
                    alpha = alpha,
                )
            }
            .selectableGroup(),
        horizontalArrangement = Arrangement.spacedBy(4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val glyphs = LocalShowButtonHints.current
        if (glyphs) ButtonGlyph(GsButton.L1)
        TAB_LABELS.forEachIndexed { idx, label ->
            TabLabel(
                label = label,
                selected = idx == activeTabIndex,
                badge = badges[idx],
                reduce = reduce,
                onClick = { onTabClick(idx) },
                onPillPositioned = { r -> if (bounds[idx] != r) bounds[idx] = r },
            )
        }
        if (glyphs) ButtonGlyph(GsButton.R1)
    }
}

@Composable
private fun TabLabel(
    label: String,
    selected: Boolean,
    badge: GsTabBadge?,
    reduce: Boolean,
    onClick: () -> Unit,
    onPillPositioned: (Rect) -> Unit,
) {
    val source = remember { MutableInteractionSource() }
    val badgeShown = badge?.visible == true
    // Ink on the pill, Dim off it; changes with the pill so both arrive together.
    val labelColor by animateColorAsState(
        targetValue = if (selected) GsColors.Ink else GsColors.Dim,
        animationSpec = GsMotion.standard(),
        label = "tabLabel",
    )
    // The hit area is at least 48dp tall; the visual pill inside it keeps
    // its 5dp vertical padding.
    Box(
        modifier = Modifier
            .heightIn(min = 48.dp)
            .widthIn(min = 48.dp)
            .then(
                if (badgeShown) Modifier.semantics { contentDescription = badge!!.contentDescription }
                else Modifier
            )
            .selectable(
                selected = selected,
                interactionSource = source,
                indication = null,
                role = Role.Tab,
                onClick = onClick,
            ),
        contentAlignment = Alignment.Center,
    ) {
        Row(
            modifier = Modifier
                .onGloballyPositioned { c ->
                    // Strip content space: this pill's box → the hit area → the strip.
                    val strip = c.parentLayoutCoordinates?.parentLayoutCoordinates
                    if (strip != null && strip.isAttached) {
                        onPillPositioned(Rect(strip.localPositionOf(c, Offset.Zero), c.size.toSize()))
                    }
                }
                .clip(RoundedCornerShape(16.dp))
                .indication(source, rememberRipple())
                .padding(horizontal = 12.dp, vertical = 5.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = label,
                style = MaterialTheme.typography.titleSmall,
                // One weight for every label: a bolder selected label would
                // change its width while the pill travels to it.
                fontWeight = FontWeight.SemiBold,
                color = labelColor,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = if (badgeShown) Modifier.clearAndSetSemantics { } else Modifier,
            )
            if (badge != null) {
                AnimatedVisibility(
                    visible = badge.visible,
                    enter = gsPopIn(reduce),
                    exit = gsPopOut(reduce),
                ) {
                    Box(
                        Modifier
                            .padding(start = 6.dp)
                            .clearAndSetSemantics { }
                    ) { badge.content() }
                }
            }
        }
    }
}

/** An 8dp status dot for a tab badge (Saves' conflicts). */
@Composable
fun GsDotBadge(color: Color) {
    Box(
        Modifier
            .size(8.dp)
            .clip(CircleShape)
            .background(color)
    )
}

/** A capsule count for a tab badge (Ink on Info is 7.1:1); grows with the font. */
@Composable
fun GsCountBadge(count: Int) {
    Box(
        modifier = Modifier
            .heightIn(min = 16.dp)
            .widthIn(min = 16.dp)
            .clip(RoundedCornerShape(50))
            .background(GsColors.Info)
            .padding(horizontal = 4.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = "$count",
            style = MaterialTheme.typography.labelSmall,
            color = GsColors.Ink,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

/**
 * Whether the pill fades out, jumps and fades in rather than travelling:
 * under reduced motion, and across a wrap (the whole strip's distance).
 */
fun pillJumps(fromIndex: Int, toIndex: Int, tabCount: Int, reduce: Boolean): Boolean =
    reduce || abs(toIndex - fromIndex) == tabCount - 1

/**
 * Where to scroll the strip so the pill [left]..[right] (content px) is fully
 * visible with [margin] on both sides; null when it already is.
 */
fun stripScrollTarget(left: Float, right: Float, scroll: Int, viewport: Int, margin: Float, max: Int): Int? {
    if (viewport <= 0) return null
    val wanted = when {
        left - margin < scroll -> left - margin
        right + margin > scroll + viewport -> right + margin - viewport
        else -> return null
    }
    val to = wanted.roundToInt().coerceIn(0, max.coerceAtLeast(0))
    return if (to == scroll) null else to
}
