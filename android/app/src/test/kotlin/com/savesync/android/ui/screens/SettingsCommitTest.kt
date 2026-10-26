package com.savesync.android.ui.screens

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class SettingsCommitTest {

    @Test
    fun `a URL needs an http or https scheme`() {
        assertNull(serverUrlError("http://192.168.1.100:8000"))
        assertNull(serverUrlError("https://example.invalid"))
        assertNull(serverUrlError("HTTP://192.168.1.100:8000"))
        assertNull(serverUrlError("  http://192.168.1.100:8000  "))
        assertEquals("Must start with http:// or https://", serverUrlError("192.168.1.100:8000"))
        assertNotNull(serverUrlError("ftp://192.168.1.100"))
        assertNotNull(serverUrlError("http:/192.168.1.100"))
    }

    @Test
    fun `a blank URL is valid - it means no server`() {
        assertNull(serverUrlError(""))
        assertNull(serverUrlError("   "))
    }

    @Test
    fun `the stored URL drops keyboard whitespace`() {
        assertEquals("http://192.168.1.100:8000", normalizeServerUrl(" http://192.168.1.100:8000 \n"))
    }

    @Test
    fun `nothing is written before the stored settings are read`() {
        // Leaving the tab before DataStore answers must not blank a saved URL.
        assertNull(textToCommit(typed = "", committed = null, valid = true))
        assertNull(textToCommit(typed = "http://a", committed = null, valid = true))
    }

    @Test
    fun `an unchanged or invalid value is not written`() {
        assertNull(textToCommit(typed = "http://a", committed = "http://a", valid = true))
        assertNull(textToCommit(typed = "a", committed = "http://a", valid = false))
    }

    @Test
    fun `a changed valid value is written`() {
        assertEquals("http://b", textToCommit(typed = "http://b", committed = "http://a", valid = true))
        // Clearing the field is a real change too.
        assertEquals("", textToCommit(typed = "", committed = "http://a", valid = true))
    }
}
