package com.savesync.android.ui.components

import androidx.compose.foundation.lazy.LazyListState

/**
 * Scroll [index] into place: animated normally, an instant jump under
 * reduced motion (LocalReduceMotion), so no movement code path remains.
 * A stop-gap until the list follow-scroll replaces these calls.
 */
suspend fun LazyListState.gsScrollToItem(index: Int, reduce: Boolean) {
    if (reduce) scrollToItem(index) else animateScrollToItem(index)
}
