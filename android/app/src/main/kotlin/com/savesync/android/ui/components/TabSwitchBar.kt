package com.savesync.android.ui.components

import androidx.compose.foundation.background
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
                    TabSwitchBar(activeTabIndex = activeTabIndex, onTabClick = onTabClick)
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

/**
 * The tab strip: L1 glyph, one pill per top-level tab, R1 glyph (the
 * glyphs only while controller hints are shown — see [LocalShowButtonHints]).  The
 * active tab is a teal pill with ink text; the rest are dim text.  Pills
 * are tappable, so touch users switch tabs the same way they always did.
 */
@Composable
fun TabSwitchBar(
    activeTabIndex: Int,
    onTabClick: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier.horizontalScroll(rememberScrollState()),
        horizontalArrangement = Arrangement.spacedBy(4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val glyphs = LocalShowButtonHints.current
        if (glyphs) ButtonGlyph(GsButton.L1)
        TAB_LABELS.forEachIndexed { idx, label ->
            val selected = idx == activeTabIndex
            // The hit area is at least 48dp tall; the visual pill inside it
            // keeps its 5dp vertical padding.
            GsHitTarget(onClick = { onTabClick(idx) }, role = Role.Tab) { indication ->
                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(16.dp))
                        .background(if (selected) GsColors.Accent else Color.Transparent)
                        .then(indication)
                        .padding(horizontal = 12.dp, vertical = 5.dp),
                ) {
                    Text(
                        text = label,
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = if (selected) FontWeight.Bold else FontWeight.Medium,
                        color = if (selected) GsColors.Ink else GsColors.Dim,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
        }
        if (glyphs) ButtonGlyph(GsButton.R1)
    }
}
