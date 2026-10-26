package com.savesync.android.ui.screens

/**
 * Pure rules behind Settings' save-on-change model (no Compose, unit-tested).
 *
 * Switches, dropdowns and folder pickers persist the moment they change.
 * The two text fields (server URL, API key) commit on IME Done, on focus
 * loss and when the user leaves the tab — never per keystroke, because a
 * commit rebuilds the API client and pings the server.
 */

private val SERVER_URL_SCHEME = Regex("^https?://", RegexOption.IGNORE_CASE)

/** Inline error for a typed server URL, or null when it may be committed. Blank = "no server". */
internal fun serverUrlError(url: String): String? {
    val v = url.trim()
    return if (v.isEmpty() || SERVER_URL_SCHEME.containsMatchIn(v)) null
    else "Must start with http:// or https://"
}

/** The URL as it is stored: surrounding whitespace from the keyboard dropped. */
internal fun normalizeServerUrl(url: String): String = url.trim()

/**
 * What a text field should write now, or null for nothing.  [committed] is
 * the value last written (or read from storage); null means the stored
 * settings haven't been read yet, so nothing may be written — leaving the
 * tab before they load must never blank a saved URL.
 */
internal fun textToCommit(typed: String, committed: String?, valid: Boolean): String? = when {
    committed == null -> null
    !valid -> null
    typed == committed -> null
    else -> typed
}

/** How long the inline "Saved" caption stays up after a commit. */
internal const val SAVED_CAPTION_HOLD_MS = 1500L
