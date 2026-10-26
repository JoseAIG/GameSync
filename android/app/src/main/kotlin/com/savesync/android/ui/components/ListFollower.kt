package com.savesync.android.ui.components

import android.os.SystemClock
import androidx.compose.animation.core.Animatable
import androidx.compose.foundation.gestures.scrollBy
import androidx.compose.foundation.lazy.LazyListLayoutInfo
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.State
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import com.savesync.android.ui.theme.GsMotion
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import kotlin.math.abs
import kotlin.math.min
import kotlin.math.sign

/**
 * Keeps the pad cursor's row on screen without moving the list on every
 * press.  The list only scrolls when the row comes within one row of an
 * edge, and then by just enough to keep that margin.  Held navigation
 * glides instead of stuttering: each step re-targets the running scroll
 * and keeps its velocity (§3.1 rule 1), where `animateScrollToItem`
 * restarted every step from rest.
 */
@Stable
class ListFollower(
    private val listState: LazyListState,
    private val scope: CoroutineScope,
    /** Bottom strip (px) covered by a showing notice; treated as off-screen. */
    private val occludedBottom: State<Int>? = null,
    /** The soft edge a press past the first or last row bumps (M5-2). */
    val edgeBump: EdgeBumpState? = null,
) {
    private val anim = Animatable(0f)

    /** Keep [index] (list item index, i.e. including header items) visible with one row of margin. */
    fun follow(index: Int, reduce: Boolean) {
        val info = listState.layoutInfo
        val visible = info.visibleItemsInfo
        if (visible.isEmpty()) return
        val window = windowOf(info)
        val item = visible.firstOrNull { it.index == index }
        val offset: Int
        val size: Int
        if (item != null) {
            offset = item.offset
            size = item.size
        } else {
            val first = visible.first()
            val last = visible.last()
            if (abs(index - first.index) > visible.size) {
                jump(index)
                return
            }
            // Just outside the viewport: estimate from the nearest neighbour.
            val stride = strideOf(visible.map { VisibleRow(it.index, it.offset, it.size) })
            if (index < first.index) {
                offset = first.offset - (first.index - index) * stride
                size = first.size
            } else {
                offset = last.offset + (index - last.index) * stride
                size = last.size
            }
        }
        val average = visible.sumOf { it.size } / visible.size
        val margin = min(average, (window.last - window.first) / 4)
        val delta = computeFollowDelta(offset, size, window.first, window.last, margin) ?: return
        if (reduce) {
            scope.launch { listState.scrollBy(delta.toFloat()) }
            return
        }
        // Read synchronously, BEFORE the new scroll {} cancels the old one: a
        // cancelled animateTo zeroes the Animatable's velocity.
        val v0 = anim.velocity
        // delta was measured from the live layout, i.e. from the current value,
        // so the remaining in-flight distance is already part of it.
        val target = anim.value + delta
        scope.launch {
            listState.scroll {
                var last = anim.value
                anim.animateTo(target, GsMotion.quick(), initialVelocity = v0) {
                    scrollBy(value - last)
                    last = value
                }
            }
        }
    }

    /** Jump without animation, placing [index] a third of the way down. */
    fun jump(index: Int) {
        val info = listState.layoutInfo
        val viewport = info.viewportEndOffset - info.viewportStartOffset
        scope.launch { listState.scrollToItem(index.coerceAtLeast(0), -(viewport / 3)) }
    }

    /**
     * Fling re-anchor (§7): when [index] (list item index) is not on screen
     * — the user flung the list by touch — the row to put the cursor on
     * instead: the first fully visible row going down, the last going up.
     * Null when [index] is on screen.
     */
    fun reanchorTarget(index: Int, down: Boolean): Int? {
        val info = listState.layoutInfo
        val visible = info.visibleItemsInfo
        if (visible.isEmpty() || visible.any { it.index == index }) return null
        val window = windowOf(info)
        return reanchorIndex(
            visible.map { VisibleRow(it.index, it.offset, it.size) },
            window.first,
            window.last,
            down,
        )
    }

    /**
     * One Up/Down press of a list cursor over [count] rows that start at
     * list item [headerItems].  A cursor a fling left off-screen re-anchors
     * and stops there; the reveal press (see [GsInputTracker]) only shows the
     * cursor; otherwise it steps by [delta] and the list follows.  Returns the
     * new selection.
     */
    fun step(
        selected: Int,
        delta: Int,
        count: Int,
        reveal: Boolean,
        reduce: Boolean,
        headerItems: Int = 0,
    ): Int {
        if (count <= 0) return selected
        val current = selected.coerceIn(0, count - 1)
        reanchorTarget(current + headerItems, down = delta > 0)?.let {
            return (it - headerItems).coerceIn(0, count - 1)
        }
        if (reveal) return current
        val next = (current + delta).coerceIn(0, count - 1)
        // Pressed past the first or last row: the list gives a little,
        // the way the press pointed, instead of not answering at all.
        if (next == current && delta != 0) edgeBump?.hit(delta, reduce)
        follow(next + headerItems, reduce)
        return next
    }

    private fun windowOf(info: LazyListLayoutInfo): IntRange {
        val viewStart = info.viewportStartOffset + info.beforeContentPadding
        val viewEnd = info.viewportEndOffset - info.afterContentPadding - (occludedBottom?.value ?: 0)
        return viewStart..maxOf(viewStart, viewEnd)
    }
}

@Composable
fun rememberListFollower(listState: LazyListState): ListFollower {
    val scope = rememberCoroutineScope()
    val occluded = LocalGsNotifier.current.occludedBottom
    val haptics = LocalGsHaptics.current
    val bumpPx = with(LocalDensity.current) { EDGE_BUMP.toPx() }
    return remember(listState, scope, occluded, haptics, bumpPx) {
        ListFollower(listState, scope, occluded, EdgeBumpState(scope, haptics, bumpPx))
    }
}

// ── Soft list edges (M5-2) ──────────────────────────────────────────────────

/** How far a list gives when pressed past an edge. */
val EDGE_BUMP = 6.dp

/**
 * An edge hit this soon after the previous one is a repeat: no bump, no
 * tick.  The Activity's synthesised stick pulses all have repeatCount 0, so
 * time is the only way to tell a held stick from a fresh press.
 */
const val EDGE_REPEAT_MS = 350L

/** Whether an edge hit at [now] repeats the one at [lastHit] (see [EDGE_REPEAT_MS]). */
fun isEdgeRepeat(now: Long, lastHit: Long?, windowMs: Long = EDGE_REPEAT_MS): Boolean =
    lastHit != null && now - lastHit in 0 until windowMs

/**
 * The list's soft edge: a D-pad press at the first or last row moves the
 * list [EDGE_BUMP] the way the press pointed (Down at the last row → up,
 * Up at the first row → down) with the PRESS spring, then back with QUICK,
 * and ticks once.  Both legs run in one coroutine on one Animatable, so a
 * new hit re-targets from where the list is (§3.1 rules 1–2).  Under
 * reduced motion only the tick.  Read [offset] only through [gsEdgeBump].
 */
@Stable
class EdgeBumpState(
    private val scope: CoroutineScope,
    private val haptics: GsHaptics?,
    private val bumpPx: Float,
    private val clock: () -> Long = SystemClock::uptimeMillis,
) {
    internal val offset = Animatable(0f)
    private var lastHit: Long? = null

    /** A press past an edge: [direction] +1 = Down at the last row, -1 = Up at the first. */
    fun hit(direction: Int, reduce: Boolean) {
        val now = clock()
        val repeat = isEdgeRepeat(now, lastHit)
        // Every hit restarts the window: a held stick bumps once.
        lastHit = now
        if (repeat) return
        // Same frame as the bump starts (harmony, §13).
        haptics?.perform(GsHaptic.Tick)
        if (reduce) return
        val target = -direction.sign * bumpPx
        // Read before the new animation cancels the old one (§3.1 rule 1).
        val v0 = offset.velocity
        scope.launch {
            offset.animateTo(target, GsMotion.press(), initialVelocity = v0)
            offset.animateTo(0f, GsMotion.quick())
        }
    }
}

/**
 * Applies [follower]'s soft edge to a list: the translation is read in the
 * layer (no recomposition per frame) and clipped to the list's own bounds.
 */
fun Modifier.gsEdgeBump(follower: ListFollower): Modifier {
    val bump = follower.edgeBump ?: return this
    return this
        .clipToBounds()
        .graphicsLayer { translationY = bump.offset.value }
}

/** A laid-out row, for the pure geometry below. */
data class VisibleRow(val index: Int, val offset: Int, val size: Int)

/**
 * How far to scroll so the item at [itemOffset]..[itemOffset]+[itemSize]
 * sits inside [viewStart]..[viewEnd] with [margin] to spare; null when it
 * already does (only the highlight moves).  Negative scrolls up.
 */
fun computeFollowDelta(itemOffset: Int, itemSize: Int, viewStart: Int, viewEnd: Int, margin: Int): Int? {
    val top = viewStart + margin
    val bottom = viewEnd - margin
    return when {
        itemOffset < top -> itemOffset - top
        itemOffset + itemSize > bottom ->
            // Taller than the window between the margins: align its top.
            if (itemSize > bottom - top) itemOffset - top else itemOffset + itemSize - bottom
        else -> null
    }
}

/**
 * The row a re-anchored cursor lands on: the first fully visible row when
 * moving [down], the last one when moving up.  Falls back to the first /
 * last partly visible row when none is fully visible.
 */
fun reanchorIndex(rows: List<VisibleRow>, viewStart: Int, viewEnd: Int, down: Boolean): Int? {
    if (rows.isEmpty()) return null
    val full = rows.filter { it.offset >= viewStart && it.offset + it.size <= viewEnd }
    val pool = full.ifEmpty { rows }
    return if (down) pool.first().index else pool.last().index
}

/** Average distance between consecutive laid-out rows (size + spacing). */
internal fun strideOf(rows: List<VisibleRow>): Int {
    if (rows.size < 2) return rows.firstOrNull()?.size ?: 0
    val first = rows.first()
    val last = rows.last()
    val span = last.index - first.index
    return if (span <= 0) first.size else (last.offset - first.offset) / span
}
